#include "badge_sync.h"
#include "badge_sync_parse.h"
#include "badge_sync_logic.h"
#include "badge_storage.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* No response array is retained: the transport frames one bounded JSON object
 * at a time. A repository page, aggregates, and resumable cursors are the only
 * collection working set. Network tasks never call LVGL. */
#define JSON_OBJECT_BYTES (12U * 1024U)
#define HTTP_BODY_BYTES (1024U * 1024U)
#define REPO_PAGE 10
#define AGG_LANGS 128
#define STAR_LANGS 100
#define SEEN_EVENTS 600
#define JOB_MAGIC 0x47484a33U
#define EVENTS_MAGIC 0x47484532U
#define API "https://api.github.com"
#define MAX_TIMESTAMP 253402300799LL /* Snapshot validator: end of year 9999. */

enum { ST_PROFILE, ST_AVATAR, ST_EVENTS, ST_SEARCH, ST_ISSUES, ST_STARS, ST_GISTS,
       ST_REPOS, ST_WATCHERS, ST_FINISHED };
#define ALL_STAGES ((1U << ST_FINISHED) - 1U)
static const uint32_t stage_sections[ST_FINISHED] = {
    BADGE_PROFILE, BADGE_AVATAR, BADGE_ACTIVITY, BADGE_PULLS, BADGE_ISSUES,
    BADGE_STARS, BADGE_GISTS, BADGE_REPOSITORIES | BADGE_LANGUAGES, BADGE_REPOSITORIES
};
static const char *const stage_names[ST_FINISHED] = {
    "PROFILE", "AVATAR", "ACTIVITY", "PULLS", "ISSUES", "STARS", "GISTS", "REPOS", "WATCHERS"
};

typedef struct { char name[32]; uint64_t all, original; } lang_sum_t;
typedef struct {
    uint32_t magic;
    char username[40];
    int64_t started;
    uint32_t stage, search_index, search_values[7], search_incomplete;
    uint32_t partial_lists;
    uint32_t done, failed, limited, watcher_index, search_done;
    int64_t deadlines[ST_FINISHED];
    char errors[ST_FINISHED][64];
    uint32_t page, entry, page_count, repos_seen, originals_seen, metadata_seen;
    uint64_t previous_repo_ids[REPO_PAGE], page_repo_ids[REPO_PAGE];
    uint32_t previous_repo_count, page_repo_count;
    bool page_last, page_loaded, languages_truncated;
    badge_repo_t page_repos[REPO_PAGE], top[BADGE_REPOS];
    badge_scope_t top_scopes[BADGE_REPOS];
    uint8_t top_count;
    uint32_t lang_count;
    lang_sum_t langs[AGG_LANGS];
    uint64_t all_bytes, original_bytes;
    uint32_t stars_count, stars_with_language, star_lang_count;
    char star_languages[STAR_LANGS][32];
    badge_star_t stars[2];
    uint32_t gist_count, gist_public;
    badge_gist_t gists[2];
    uint32_t checksum;
} sync_job_t;

typedef struct {
    uint32_t magic;
    uint32_t id_count, id_next;
    uint64_t ids[SEEN_EVENTS];
    badge_day_t days[BADGE_DAYS];
    badge_event_t recent[BADGE_EVENTS];
    uint8_t recent_count;
    uint32_t checksum;
} event_store_t;

typedef struct {
    badge_data_t *data;
    const badge_config_t *config;
    sync_job_t *job;
    badge_sync_publish_t publish;
    void *publish_context;
    int http_status;
    int64_t retry, reset;
    int remaining;
    bool rate_seen, retry_seen, search_bucket, rate_limited, storage_failed;
    unsigned request_count, response_scope;
    bool allow_large_drop;
    bool publish_failed;
    unsigned response_items, response_skipped;
    char reason[64];
} sync_t;

typedef struct {
    char name[96], company[96], location[96], blog[160], joined[11], avatar_url[240];
    uint32_t public_repos, followers, following, public_gists;
} profile_value_t;

static atomic_bool s_cancel;
void badge_sync_cancel(void) { atomic_store(&s_cancel, true); }
static int64_t now_sec(void) { return (int64_t)time(NULL); }

static void copy_text(char *out, size_t size, const char *text)
{
    if (!size) return;
    if (!text) text = "";
    size_t length = strlen(text);
    if (length >= size) length = size - 1;
    /* Never cut through a UTF-8 sequence. */
    while (length && ((unsigned char)text[length] & 0xc0) == 0x80) --length;
    memcpy(out, text, length); out[length] = 0;
}

static const cJSON *field(const cJSON *json, const char *key)
{ return cJSON_GetObjectItemCaseSensitive(json, key); }
static const char *string_field(const cJSON *json, const char *key)
{
    const cJSON *value = field(json, key);
    return cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
}
static uint32_t number_field(const cJSON *json, const char *key)
{
    const cJSON *value = field(json, key);
    if (!cJSON_IsNumber(value) || value->valuedouble < 0) return 0;
    return value->valuedouble > UINT32_MAX ? UINT32_MAX : (uint32_t)value->valuedouble;
}

static uint32_t checksum(const void *bytes, size_t size)
{
    const unsigned char *p = bytes; uint32_t sum = 2166136261U;
    for (size_t i = 0; i < size; ++i) sum = (sum ^ p[i]) * 16777619U;
    return sum;
}

static bool read_blob(const char *path, void *bytes, size_t size)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    bool ok = fread(bytes, 1, size, file) == size && fgetc(file) == EOF;
    fclose(file);
    if (!ok || size < sizeof(uint32_t)) return false;
    uint32_t expected;
    memcpy(&expected, (char *)bytes + size - sizeof(expected), sizeof(expected));
    return checksum(bytes, size - sizeof(expected)) == expected;
}

static bool events_valid(const event_store_t *store)
{
    if (store->magic != EVENTS_MAGIC || store->id_count > SEEN_EVENTS ||
        store->id_next >= SEEN_EVENTS || store->recent_count > BADGE_EVENTS ||
        (store->id_count < SEEN_EVENTS && store->id_next != store->id_count)) return false;
    for (uint32_t i = 0; i < store->id_count; ++i) {
        if (!store->ids[i]) return false;
        for (uint32_t k = 0; k < i; ++k) if (store->ids[i] == store->ids[k]) return false;
    }
    for (unsigned i = 0; i < BADGE_DAYS; ++i) {
        const badge_day_t *day = &store->days[i];
        if (day->coverage > BADGE_KNOWN || day->pushes > day->events ||
            day->day < 0 || day->day > 100000 ||
            (day->coverage == BADGE_UNKNOWN && (day->events || day->pushes))) return false;
        if (i && day->day != store->days[i - 1].day + 1) return false;
    }
    for (unsigned i = 0; i < store->recent_count; ++i) {
        const badge_event_t *event = &store->recent[i]; int32_t day;
        if (!memchr(event->type, 0, sizeof(event->type)) || !event->type[0] ||
            !memchr(event->repo, 0, sizeof(event->repo)) ||
            !memchr(event->date, 0, sizeof(event->date)) || !badge_iso_day(event->date, &day)) return false;
    }
    return true;
}

bool badge_sync_events_valid(const char *path)
{
    if (!path) return false;
    event_store_t *store = malloc(sizeof(*store));
    if (!store) return false;
    bool ok = read_blob(path, store, sizeof(*store)) && events_valid(store);
    free(store); return ok;
}

static bool write_blob(const char *path, void *bytes, size_t size)
{
    uint32_t sum = checksum(bytes, size - sizeof(uint32_t));
    memcpy((char *)bytes + size - sizeof(sum), &sum, sizeof(sum));
    return badge_file_write(path, bytes, size);
}

static bool save_job(sync_t *s)
{
    char path[160];
    bool ok = badge_cache_path(s->config->username, "job", path, sizeof(path)) &&
              write_blob(path, s->job, sizeof(*s->job));
    if (!ok) { s->storage_failed = true; copy_text(s->reason, sizeof(s->reason), "CURSOR STORAGE ERROR"); }
    return ok;
}

static void progress(sync_t *s, const char *message)
{
    copy_text(s->data->sync_message, sizeof(s->data->sync_message), message);
    if (s->publish && !s->publish(s->data, s->publish_context)) s->publish_failed = true;
}

static uint32_t stage_scope(unsigned stage)
{
    return stage == ST_AVATAR ? BADGE_SYNC_LIMIT_AVATAR :
        (stage == ST_SEARCH || stage == ST_ISSUES) ? BADGE_SYNC_LIMIT_SEARCH : BADGE_SYNC_LIMIT_CORE;
}

static int64_t scope_deadline(const sync_job_t *j, uint32_t scope)
{
    int64_t deadline = 0;
    for (unsigned i = 0; i < ST_FINISHED; ++i)
        if ((j->limited & (1U << i)) && (stage_scope(i) & scope) &&
            j->deadlines[i] <= MAX_TIMESTAMP && j->deadlines[i] > deadline)
            deadline = j->deadlines[i];
    return deadline > now_sec() ? deadline : 0;
}

static const char *scope_label(uint32_t scope)
{
    return scope == BADGE_SYNC_LIMIT_CORE ? "CORE" : scope == BADGE_SYNC_LIMIT_SEARCH ? "SEARCH" :
        scope == BADGE_SYNC_LIMIT_AVATAR ? "AVATAR" : "API";
}

static void set_limit(sync_job_t *j, uint32_t scopes, int64_t until, const char *reason)
{
    for (unsigned i = 0; i < ST_FINISHED; ++i) if (stage_scope(i) & scopes) {
        if (!(j->limited & (1U << i)) || until > j->deadlines[i]) j->deadlines[i] = until;
        j->limited |= 1U << i;
        copy_text(j->errors[i], sizeof(j->errors[i]), reason);
    }
}

/* Also migrate v0.2's single-failed-stage limits into bucket-wide limits.
 * The serialized job layout is unchanged, so upgrading retains its deadlines. */
static void normalize_limits(sync_job_t *j)
{
    const unsigned scopes[] = {BADGE_SYNC_LIMIT_CORE, BADGE_SYNC_LIMIT_SEARCH, BADGE_SYNC_LIMIT_AVATAR};
    for (unsigned k = 0; k < 3; ++k) {
        int64_t until = scope_deadline(j, scopes[k]);
        char reason[64]; snprintf(reason, sizeof(reason), "%s RATE LIMIT - WILL RESUME", scope_label(scopes[k]));
        for (unsigned i = 0; i < ST_FINISHED; ++i)
            if ((j->limited & (1U << i)) && stage_scope(i) == scopes[k] && j->errors[i][0] &&
                memchr(j->errors[i], 0, sizeof(j->errors[i]))) {
                copy_text(reason, sizeof(reason), j->errors[i]); break;
            }
        for (unsigned i = 0; i < ST_FINISHED; ++i) if (stage_scope(i) == scopes[k]) {
            if (until) {
                j->limited |= 1U << i; j->deadlines[i] = until;
                copy_text(j->errors[i], sizeof(j->errors[i]), reason);
            }
            else j->limited &= ~(1U << i);
        }
    }
}

static bool decimal_header(const char *value, int64_t *number)
{
    if (!value || !*value) return false;
    uint64_t n = 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p < '0' || *p > '9' || n > ((uint64_t)INT64_MAX - (*p - '0')) / 10) return false;
        n = n * 10 + (*p - '0');
    }
    *number = (int64_t)n; return true;
}

static esp_err_t header_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_HEADER) return ESP_OK;
    sync_t *s = event->user_data; int64_t value;
    if (!strcasecmp(event->header_key, "X-RateLimit-Remaining")) {
        if (decimal_header(event->header_value, &value) && value <= INT_MAX) {
            s->remaining = (int)value; s->rate_seen = true;
        }
    } else if (!strcasecmp(event->header_key, "X-RateLimit-Reset")) {
        if (decimal_header(event->header_value, &value) && value <= MAX_TIMESTAMP - 5) s->reset = value;
    } else if (!strcasecmp(event->header_key, "X-RateLimit-Resource")) {
        if (!strcmp(event->header_value, "core")) s->response_scope = BADGE_SYNC_LIMIT_CORE;
        else if (!strcmp(event->header_value, "search")) s->response_scope = BADGE_SYNC_LIMIT_SEARCH;
    } else if (!strcasecmp(event->header_key, "Retry-After")) {
        /* GitHub specifies delay-seconds. Reject malformed/overflowing values
         * instead of atoi("invalid") accidentally looking like zero quota. */
        if (decimal_header(event->header_value, &value) && value < MAX_TIMESTAMP - now_sec()) {
            s->retry = now_sec() + (value ? value : 1); s->retry_seen = true;
        }
    }
    return ESP_OK;
}

static bool contains_ascii(const char *text, const char *needle)
{
    for (; *text; ++text) {
        const char *a = text, *b = needle;
        while (*a && *b && tolower((unsigned char)*a) == (unsigned char)*b) { ++a; ++b; }
        if (!*b) return true;
    }
    return false;
}

static bool rate_error_body(esp_http_client_handle_t client)
{
    /* Error objects are small; never buffer an unbounded HTML/proxy response. */
    char *body = malloc(2049); if (!body) return false;
    size_t used = 0;
    while (used < 2048 && !atomic_load(&s_cancel)) {
        int got = esp_http_client_read(client, body + used, (int)(2048 - used));
        if (got <= 0) break;
        used += (size_t)got;
    }
    body[used] = 0;
    cJSON *json = cJSON_ParseWithLength(body, used + 1);
    const char *message = string_field(json, "message");
    bool limited = contains_ascii(message, "rate limit") || contains_ascii(message, "abuse detection");
    cJSON_Delete(json); free(body); return limited;
}

static esp_http_client_handle_t open_http(sync_t *s, const char *url)
{
    unsigned scope = strncmp(url, API "/", sizeof(API)) ? BADGE_SYNC_LIMIT_AVATAR :
        s->search_bucket ? BADGE_SYNC_LIMIT_SEARCH : BADGE_SYNC_LIMIT_CORE;
    s->retry = 0; s->reset = 0; s->remaining = -1; s->rate_seen = s->retry_seen = false;
    s->response_scope = scope; s->http_status = 0;
    if (atomic_load(&s_cancel)) { copy_text(s->reason, sizeof(s->reason), "SYNC CANCELLED"); return NULL; }
    int64_t limited_until = scope_deadline(s->job, scope);
    if (limited_until) {
        s->retry = limited_until; s->rate_limited = true;
        snprintf(s->reason, sizeof(s->reason), "%s RATE LIMIT - WILL RESUME", scope_label(scope));
        return NULL;
    }
    esp_http_client_config_t config = {
        .url = url, .timeout_ms = 15000, .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true, .buffer_size = 1024, .buffer_size_tx = 512,
        .event_handler = header_event, .user_data = s,
        .user_agent = "GitHub-Pixel-Passport/1.0", .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { copy_text(s->reason, sizeof(s->reason), "HTTP MEMORY LIMIT"); return NULL; }
    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
    esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
    ++s->request_count;
    if (esp_http_client_open(client, 0) != ESP_OK || esp_http_client_fetch_headers(client) < 0) {
        copy_text(s->reason, sizeof(s->reason), "NETWORK / TLS ERROR");
        esp_http_client_cleanup(client); return NULL;
    }
    s->http_status = esp_http_client_get_status_code(client);
    bool primary = s->rate_seen && s->remaining == 0;
    bool body_limited = s->http_status == 403 && !primary && !s->retry_seen && rate_error_body(client);
    bool secondary = !primary && (s->http_status == 429 || body_limited);
    bool server_wait = s->http_status != 200 && s->retry_seen;
    if (primary || secondary || server_wait) {
        /* A proxy or changed endpoint can report an unexpected resource. Keep
         * both its explicit bucket and the rejected endpoint protected. */
        uint32_t scopes = primary ? s->response_scope | scope :
            scope == BADGE_SYNC_LIMIT_AVATAR ? scope : BADGE_SYNC_LIMIT_CORE | BADGE_SYNC_LIMIT_SEARCH;
        /* Respect Retry-After first. The primary reset clock is meaningful only
         * when remaining=0; a nonzero primary bucket can coexist with abuse limits. */
        unsigned backoff = 60;
        if (s->retry_seen) limited_until = s->retry;
        else if (primary) limited_until = s->reset > now_sec() ? s->reset + 5 :
            now_sec() + (s->response_scope == BADGE_SYNC_LIMIT_SEARCH ? 65 : 3600);
        else {
            unsigned previous = 0;
            if (sscanf(s->job->errors[s->job->stage], "SECONDARY RATE LIMIT / WAIT %uS", &previous) == 1 && previous >= 60)
                backoff = previous >= 1800 ? 3600 : previous * 2;
            limited_until = now_sec() + backoff;
        }
        s->retry = limited_until; s->rate_limited = true;
        if (secondary && !s->retry_seen)
            snprintf(s->reason, sizeof(s->reason), "SECONDARY RATE LIMIT / WAIT %uS", backoff);
        else snprintf(s->reason, sizeof(s->reason), "%s %s - WILL RESUME", scope_label(scopes),
                      server_wait && !primary && !secondary ? "RETRY-AFTER" : "RATE LIMIT");
        set_limit(s->job, scopes, limited_until, s->reason);
        /* Persist limits before cleanup/rollback. They survive a failed dataset
         * publication, a fresh collection cycle, and a power loss after this response. */
        if (!save_job(s)) { esp_http_client_cleanup(client); return NULL; }
    }
    if (s->http_status != 200) {
        if (!(primary || secondary || server_wait)) {
            if (s->http_status == 404) copy_text(s->reason, sizeof(s->reason), "ACCOUNT / RESOURCE NOT FOUND");
            else snprintf(s->reason, sizeof(s->reason), "GITHUB HTTP %d - RETRY", s->http_status);
        }
        esp_http_client_cleanup(client); return NULL;
    }
    return client;
}

typedef bool (*json_fn)(const cJSON *json, void *ctx);
typedef struct { json_fn callback; void *context; } json_callback_t;
static bool parse_object(const char *json, size_t size, void *ctx)
{
    json_callback_t *callback = ctx;
    cJSON *object = cJSON_ParseWithLength(json, size + 1);
    if (!cJSON_IsObject(object)) { cJSON_Delete(object); return false; }
    bool ok = callback->callback(object, callback->context);
    cJSON_Delete(object);
    return ok;
}

static bool request_json(sync_t *s, const char *path, bool array, json_fn callback, void *context)
{
    char url[384];
    if (snprintf(url, sizeof(url), API "%s", path) >= (int)sizeof(url)) return false;
    esp_http_client_handle_t client = open_http(s, url);
    if (!client) return false;
    char *object_buffer = malloc(JSON_OBJECT_BYTES);
    if (!object_buffer) {
        copy_text(s->reason, sizeof(s->reason), "JSON MEMORY LIMIT");
        esp_http_client_cleanup(client); return false;
    }
    json_callback_t cb = { callback, context };
    badge_json_stream_t stream;
    badge_json_stream_init(&stream, object_buffer, JSON_OBJECT_BYTES, array, parse_object, &cb);
    stream.skip_large_objects = s->allow_large_drop;
    char chunk[768]; size_t total = 0; bool ok = true;
    for (;;) {
        if (atomic_load(&s_cancel)) { ok = false; break; }
        int length = esp_http_client_read(client, chunk, sizeof(chunk));
        if (length < 0) { ok = false; break; }
        if (!length) break;
        total += (size_t)length;
        if (total > HTTP_BODY_BYTES) {
            copy_text(s->reason, sizeof(s->reason), "RESPONSE LIMIT / BODY TOO LARGE"); ok = false; break;
        }
        if (!badge_json_stream_feed(&stream, chunk, (size_t)length)) {
            copy_text(s->reason, sizeof(s->reason), "RESPONSE LIMIT / INVALID DATA"); ok = false; break;
        }
        vTaskDelay(1);
    }
    ok = ok && badge_json_stream_done(&stream) && esp_http_client_is_complete_data_received(client);
    s->response_items = stream.items; s->response_skipped = stream.dropped;
    free(object_buffer);
    esp_http_client_cleanup(client);
    if (!ok && !s->reason[0]) copy_text(s->reason, sizeof(s->reason), "INCOMPLETE RESPONSE - RETRY");
    return ok;
}

static bool profile_json(const cJSON *json, void *ctx)
{
    profile_value_t *d = ctx;
    if (!*string_field(json, "login") || !cJSON_IsNumber(field(json, "public_repos"))) return false;
    copy_text(d->name, sizeof(d->name), string_field(json, "name"));
    copy_text(d->company, sizeof(d->company), string_field(json, "company"));
    copy_text(d->location, sizeof(d->location), string_field(json, "location"));
    copy_text(d->blog, sizeof(d->blog), string_field(json, "blog"));
    copy_text(d->joined, sizeof(d->joined), string_field(json, "created_at"));
    copy_text(d->avatar_url, sizeof(d->avatar_url), string_field(json, "avatar_url"));
    d->public_repos = number_field(json, "public_repos");
    d->followers = number_field(json, "followers");
    d->following = number_field(json, "following");
    d->public_gists = number_field(json, "public_gists");
    return true;
}

typedef struct {
    sync_t *sync; event_store_t *store;
    unsigned page_count, cycle_count;
    int32_t oldest_day;
    badge_event_t recent[BADGE_EVENTS];
    uint8_t recent_count;
} events_context_t;

static bool events_json(const cJSON *json, void *ctx)
{
    events_context_t *e = ctx;
    const char *id_string = string_field(json, "id");
    const char *date = string_field(json, "created_at");
    const char *type = string_field(json, "type");
    for (const char *digit = id_string; *digit; ++digit) if (*digit < '0' || *digit > '9') return false;
    char *end = NULL;
    errno = 0;
    uint64_t id = strtoull(id_string, &end, 10);
    int32_t day;
    if (errno == ERANGE || !id || !end || *end || !*type || !badge_iso_day(date, &day)) return false;
    ++e->page_count; ++e->cycle_count;
    if (day < e->oldest_day) e->oldest_day = day;
    if (e->recent_count < BADGE_EVENTS) {
        badge_event_t *v = &e->recent[e->recent_count++];
        copy_text(v->type, sizeof(v->type), type);
        copy_text(v->repo, sizeof(v->repo), string_field(field(json, "repo"), "name"));
        copy_text(v->date, sizeof(v->date), date);
    }
    badge_history_observe(e->sync->data, e->store->ids, SEEN_EVENTS,
                          &e->store->id_count, &e->store->id_next, id, day, !strcmp(type, "PushEvent"));
    return true;
}

static bool collect_events(sync_t *s)
{
    event_store_t *store = calloc(1, sizeof(*store));
    if (!store) { copy_text(s->reason, sizeof(s->reason), "HISTORY MEMORY LIMIT"); return false; }
    char cache[160], path[200];
    badge_cache_path(s->config->username, "events", cache, sizeof(cache));
    bool loaded = read_blob(cache, store, sizeof(*store)) && events_valid(store);
    if (loaded) {
        memcpy(s->data->days, store->days, sizeof(store->days));
        memcpy(s->data->events, store->recent, sizeof(store->recent));
        s->data->event_count = store->recent_count <= BADGE_EVENTS ? store->recent_count : 0;
    } else {
        memset(store, 0, sizeof(*store)); store->magic = EVENTS_MAGIC;
        memcpy(store->days, s->data->days, sizeof(store->days));
        /* Without the paired deduplication journal, retaining event counts
         * would count observations twice. Unknown is an honest recovery. */
        memset(s->data->days, 0, sizeof(s->data->days));
    }
    int32_t today = (int32_t)(now_sec() / 86400);
    badge_history_roll(s->data, today);
    events_context_t e = { .sync = s, .store = store, .oldest_day = INT32_MAX };
    bool ok = true, exhausted = false, skipped = false;
    s->allow_large_drop = true;
    for (unsigned page = 1; page <= 3; ++page) {
        e.page_count = 0;
        snprintf(path, sizeof(path), "/users/%s/events/public?per_page=100&page=%u", s->config->username, page);
        if (!request_json(s, path, true, events_json, &e)) { ok = false; break; }
        skipped = skipped || s->response_skipped != 0;
        if (s->response_items < 100) { exhausted = true; break; }
    }
    s->allow_large_drop = false;
    if (!ok && !loaded && !e.cycle_count && !skipped) {
        /* No trustworthy new history exists. Keep the visible last-good
         * snapshot without pairing its counts with an empty ID journal. */
        memcpy(s->data->days, store->days, sizeof(store->days));
        free(store);
        return false;
    }
    if (ok && !skipped) {
        /* The boundary day is incomplete. Today's future hours are unknown.
         * At the 300-event cap, older days remain unknown/partial. */
        badge_history_coverage(s->data, today, e.oldest_day, exhausted);
        memcpy(s->data->events, e.recent, sizeof(e.recent));
        s->data->event_count = e.recent_count;
        s->data->valid |= BADGE_ACTIVITY;
        if (exhausted) s->data->complete |= BADGE_ACTIVITY;
        else s->data->complete &= ~BADGE_ACTIVITY;
    } else if (e.cycle_count || skipped) {
        s->data->valid |= BADGE_ACTIVITY;
        s->data->complete &= ~BADGE_ACTIVITY;
        if (e.recent_count) {
            memcpy(s->data->events, e.recent, sizeof(e.recent));
            s->data->event_count = e.recent_count;
        }
    }
    badge_history_totals(s->data);
    memcpy(store->days, s->data->days, sizeof(store->days));
    memcpy(store->recent, s->data->events, sizeof(store->recent));
    store->recent_count = s->data->event_count;
    if (!write_blob(cache, store, sizeof(*store))) {
        copy_text(s->reason, sizeof(s->reason), "HISTORY STORAGE ERROR"); s->storage_failed = true; ok = false;
    }
    free(store);
    /* A bounded-resource partial observation must not block every other page.
     * Quota and transport failures still retain the resumable events stage. */
    if (!ok && strstr(s->reason, "RESPONSE LIMIT")) ok = true;
    return ok;
}

typedef struct { uint32_t count; bool incomplete; } search_value_t;
static bool request_search(sync_t *s, const char *path, search_value_t *value)
{
    char url[384];
    if (snprintf(url, sizeof(url), API "%s", path) >= (int)sizeof(url)) return false;
    esp_http_client_handle_t client = open_http(s, url);
    if (!client) return false;
    badge_search_stream_t stream; badge_search_stream_init(&stream);
    char chunk[768]; size_t total = 0; bool ok = true;
    for (;;) {
        if (atomic_load(&s_cancel)) { ok = false; break; }
        int length = esp_http_client_read(client, chunk, sizeof(chunk));
        if (length < 0) { ok = false; break; }
        if (!length) break;
        total += (size_t)length;
        if (total > HTTP_BODY_BYTES || !badge_search_stream_feed(&stream, chunk, (size_t)length)) { ok = false; break; }
        vTaskDelay(1);
    }
    ok = ok && badge_search_stream_done(&stream) && esp_http_client_is_complete_data_received(client);
    esp_http_client_cleanup(client);
    if (ok) { value->count = stream.count; value->incomplete = stream.incomplete; }
    else copy_text(s->reason, sizeof(s->reason), "SEARCH RESPONSE INCOMPLETE");
    return ok;
}

static void publish_search(sync_t *s)
{
    sync_job_t *j = s->job; badge_data_t *d = s->data;
    if ((j->search_done & 15U) == 15U) {
        if (j->search_values[2] > j->search_values[3] ||
            (uint64_t)j->search_values[1] + j->search_values[3] != j->search_values[0]) j->search_incomplete |= 15U;
        bool complete = (j->search_incomplete & 15U) == 0;
        if (complete || !(d->complete & BADGE_PULLS)) {
            d->pr_total = j->search_values[0]; d->pr_open = j->search_values[1];
            d->pr_merged = j->search_values[2]; d->pr_closed = j->search_values[3];
            d->valid |= BADGE_PULLS;
            if (complete) d->complete |= BADGE_PULLS; else d->complete &= ~BADGE_PULLS;
        }
    }
    if ((j->search_done & 112U) == 112U) {
        if ((uint64_t)j->search_values[5] + j->search_values[6] != j->search_values[4]) j->search_incomplete |= 112U;
        bool complete = (j->search_incomplete & 112U) == 0;
        if (complete || !(d->complete & BADGE_ISSUES)) {
            d->issue_total = j->search_values[4]; d->issue_open = j->search_values[5]; d->issue_closed = j->search_values[6];
            d->valid |= BADGE_ISSUES;
            if (complete) d->complete |= BADGE_ISSUES; else d->complete &= ~BADGE_ISSUES;
        }
    }
}

static bool stars_json(const cJSON *json, void *ctx)
{
    sync_job_t *j = ctx;
    if (!*string_field(json, "full_name")) return false;
    if (j->stars_count < 2) {
        badge_star_t *star = &j->stars[j->stars_count];
        copy_text(star->name, sizeof(star->name), string_field(json, "full_name"));
        copy_text(star->language, sizeof(star->language), string_field(json, "language"));
        star->stars = number_field(json, "stargazers_count");
    }
    ++j->stars_count;
    const char *language = string_field(json, "language");
    if (*language) {
        ++j->stars_with_language;
        for (unsigned i = 0; i < j->star_lang_count; ++i) if (!strcmp(j->star_languages[i], language)) return true;
        if (j->star_lang_count < STAR_LANGS) copy_text(j->star_languages[j->star_lang_count++], sizeof(j->star_languages[0]), language);
    }
    return true;
}

static bool gists_json(const cJSON *json, void *ctx)
{
    sync_job_t *j = ctx;
    if (!*string_field(json, "id")) return false;
    if (j->gist_count < 2) {
        badge_gist_t *gist = &j->gists[j->gist_count];
        const char *description = string_field(json, "description");
        if (!*description) {
            const cJSON *files = field(json, "files");
            description = files && files->child && files->child->string ? files->child->string : "UNTITLED";
        }
        copy_text(gist->description, sizeof(gist->description), description);
        copy_text(gist->updated_at, sizeof(gist->updated_at), string_field(json, "updated_at"));
    }
    ++j->gist_count;
    if (cJSON_IsTrue(field(json, "public"))) ++j->gist_public;
    return true;
}

static bool safe_full_name(const char *name)
{
    unsigned slash = 0;
    if (!*name || strlen(name) >= sizeof(((badge_repo_t *)0)->full_name)) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if (*p == '/') ++slash;
        else if (!(isalnum(*p) || *p == '-' || *p == '_' || *p == '.')) return false;
    }
    return slash == 1;
}

static bool stored_bool_valid(const bool *value)
{
    unsigned char raw;
    memcpy(&raw, value, sizeof(raw));
    return raw <= 1;
}

static bool stored_repo_valid(const badge_repo_t *repo)
{
    return memchr(repo->name, 0, sizeof(repo->name)) &&
        memchr(repo->full_name, 0, sizeof(repo->full_name)) &&
        (!repo->full_name[0] || safe_full_name(repo->full_name)) &&
        memchr(repo->license, 0, sizeof(repo->license)) &&
        memchr(repo->pushed_at, 0, sizeof(repo->pushed_at)) &&
        stored_bool_valid(&repo->fork) && stored_bool_valid(&repo->watchers_valid);
}

static bool stored_scope_valid(const badge_scope_t *scope)
{
    if (!memchr(scope->title, 0, sizeof(scope->title)) || scope->count > BADGE_LANGS ||
        !stored_bool_valid(&scope->valid) || !stored_bool_valid(&scope->complete) ||
        scope->total_bytes > UINT64_MAX / 1000U) return false;
    uint64_t sum = 0;
    for (unsigned i = 0; i < BADGE_LANGS; ++i) {
        if (!memchr(scope->languages[i].name, 0, sizeof(scope->languages[i].name))) return false;
        if (i < scope->count) {
            if (scope->languages[i].bytes > scope->total_bytes - sum) return false;
            sum += scope->languages[i].bytes;
        }
    }
    return true;
}

static bool job_valid(const sync_job_t *j, const char *username)
{
    /* A checksum detects torn writes, not valid field values. Every cached
     * string/bound must be checked before strcmp, aggregation or rendering. */
    if (j->magic != JOB_MAGIC || !memchr(j->username, 0, sizeof(j->username)) ||
        strcasecmp(j->username, username) || j->started < 0 || j->started > MAX_TIMESTAMP ||
        j->stage > ST_FINISHED || j->search_index > 7 ||
        (j->done & ~ALL_STAGES) || (j->failed & ~ALL_STAGES) || (j->limited & ~ALL_STAGES) ||
        (j->search_done & ~127U) || (j->search_incomplete & ~127U) ||
        (j->partial_lists & ~(BADGE_STARS | BADGE_GISTS)) ||
        !j->page || j->page_count > REPO_PAGE || j->entry > j->page_count ||
        j->previous_repo_count > REPO_PAGE || j->page_repo_count > REPO_PAGE ||
        j->top_count > BADGE_REPOS || j->watcher_index > 3 || j->watcher_index > j->top_count ||
        j->lang_count > AGG_LANGS || j->originals_seen > j->repos_seen || j->repos_seen > j->metadata_seen ||
        j->all_bytes > UINT64_MAX / 1000U || j->original_bytes > j->all_bytes ||
        j->star_lang_count > STAR_LANGS || j->star_lang_count > j->stars_with_language ||
        j->stars_with_language > j->stars_count || j->gist_public > j->gist_count ||
        !stored_bool_valid(&j->page_last) || !stored_bool_valid(&j->page_loaded) ||
        !stored_bool_valid(&j->languages_truncated)) return false;
    for (unsigned i = 0; i < ST_FINISHED; ++i)
        if (!memchr(j->errors[i], 0, sizeof(j->errors[i])) ||
            j->deadlines[i] < 0 || j->deadlines[i] > MAX_TIMESTAMP) return false;
    for (unsigned i = 0; i < REPO_PAGE; ++i)
        if (!stored_repo_valid(&j->page_repos[i])) return false;
    for (unsigned i = 0; i < BADGE_REPOS; ++i)
        if (!stored_repo_valid(&j->top[i]) || !stored_scope_valid(&j->top_scopes[i])) return false;
    uint64_t all = 0, original = 0;
    for (unsigned i = 0; i < AGG_LANGS; ++i) {
        if (!memchr(j->langs[i].name, 0, sizeof(j->langs[i].name))) return false;
        if (i < j->lang_count) {
            if (!j->langs[i].name[0] || j->langs[i].original > j->langs[i].all ||
                j->langs[i].all > j->all_bytes - all || j->langs[i].original > j->original_bytes - original)
                return false;
            all += j->langs[i].all; original += j->langs[i].original;
        }
    }
    for (unsigned i = 0; i < STAR_LANGS; ++i)
        if (!memchr(j->star_languages[i], 0, sizeof(j->star_languages[i]))) return false;
    for (unsigned i = 0; i < 2; ++i)
        if (!memchr(j->stars[i].name, 0, sizeof(j->stars[i].name)) ||
            !memchr(j->stars[i].language, 0, sizeof(j->stars[i].language)) ||
            !memchr(j->gists[i].description, 0, sizeof(j->gists[i].description)) ||
            !memchr(j->gists[i].updated_at, 0, sizeof(j->gists[i].updated_at))) return false;
    return true;
}

static bool repos_json(const cJSON *json, void *ctx)
{
    sync_job_t *j = ctx;
    if (j->page_count >= REPO_PAGE || j->page_repo_count >= REPO_PAGE || !safe_full_name(string_field(json, "full_name"))) return false;
    const cJSON *id_field = field(json, "id");
    if (!cJSON_IsNumber(id_field) || id_field->valuedouble <= 0 || id_field->valuedouble > 9007199254740991.0) return false;
    uint64_t id = (uint64_t)id_field->valuedouble;
    j->page_repo_ids[j->page_repo_count++] = id;
    for (uint32_t i = 0; i < j->previous_repo_count; ++i) if (j->previous_repo_ids[i] == id) return true;
    for (uint32_t i = 0; i + 1 < j->page_repo_count; ++i) if (j->page_repo_ids[i] == id) return true;
    badge_repo_t *repo = &j->page_repos[j->page_count++];
    memset(repo, 0, sizeof(*repo));
    copy_text(repo->name, sizeof(repo->name), string_field(json, "name"));
    copy_text(repo->full_name, sizeof(repo->full_name), string_field(json, "full_name"));
    const char *license = string_field(field(json, "license"), "spdx_id");
    copy_text(repo->license, sizeof(repo->license), *license ? license : "NONE");
    copy_text(repo->pushed_at, sizeof(repo->pushed_at), string_field(json, "pushed_at"));
    repo->stars = number_field(json, "stargazers_count"); repo->forks = number_field(json, "forks_count");
    repo->fork = cJSON_IsTrue(field(json, "fork"));
    return true;
}

typedef struct { sync_job_t *job; const badge_repo_t *repo; badge_scope_t scope; } languages_context_t;
static bool languages_json(const cJSON *json, void *ctx)
{
    languages_context_t *l = ctx;
    sync_job_t *j = l->job;
    /* Validate first: an invalid field must not partially mutate aggregates. */
    const cJSON *item;
    uint64_t sum = 0;
    cJSON_ArrayForEach(item, json) {
        if (!item->string || !cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > 1e15) return false;
        uint64_t bytes = (uint64_t)item->valuedouble;
        if (sum > UINT64_MAX - bytes) return false;
        sum += bytes;
        for (const cJSON *previous = json->child; previous != item; previous = previous->next)
            if (!strcmp(previous->string, item->string)) return false;
    }
    if (j->all_bytes > UINT64_MAX - sum || (!l->repo->fork && j->original_bytes > UINT64_MAX - sum)) return false;
    cJSON_ArrayForEach(item, json) {
        uint64_t bytes = (uint64_t)item->valuedouble;
        badge_scope_add(&l->scope, item->string, bytes);
        j->all_bytes += bytes; if (!l->repo->fork) j->original_bytes += bytes;
        unsigned i = 0;
        for (; i < j->lang_count; ++i) if (!strcmp(j->langs[i].name, item->string)) break;
        if (i == j->lang_count) {
            if (j->lang_count == AGG_LANGS || strlen(item->string) >= sizeof(j->langs[0].name)) { j->languages_truncated = true; continue; }
            copy_text(j->langs[i].name, sizeof(j->langs[i].name), item->string); ++j->lang_count;
        }
        j->langs[i].all += bytes;
        if (!l->repo->fork) j->langs[i].original += bytes;
    }
    badge_scope_finish(&l->scope);
    l->scope.fetched_repos = 1; l->scope.total_repos = 1; l->scope.complete = true;
    copy_text(l->scope.title, sizeof(l->scope.title), l->repo->name);
    return true;
}

static void publish_repos(sync_t *s, bool finished)
{
    sync_job_t *j = s->job; badge_data_t *d = s->data;
    bool enumerated = finished && j->repos_seen == d->public_repos;
    if (finished || !(d->complete & BADGE_REPOSITORIES)) {
        for (unsigned i = 0; i < j->top_count; ++i) if (!j->top[i].watchers_valid) {
            for (unsigned k = 0; k < d->repo_count; ++k)
                if (d->repos[k].watchers_valid && !strcmp(d->repos[k].full_name, j->top[i].full_name)) {
                    j->top[i].watchers = d->repos[k].watchers;
                    j->top[i].watchers_valid = true;
                    break;
                }
        }
        memcpy(d->repos, j->top, sizeof(j->top)); d->repo_count = j->top_count; d->repos_fetched = j->metadata_seen;
        d->valid |= BADGE_REPOSITORIES;
        if (enumerated) d->complete |= BADGE_REPOSITORIES; else d->complete &= ~BADGE_REPOSITORIES;
    }
    if ((finished || !(d->complete & BADGE_LANGUAGES)) && (j->repos_seen || finished)) {
        memset(d->scopes, 0, sizeof(d->scopes));
        copy_text(d->scopes[0].title, sizeof(d->scopes[0].title), "ORIGINAL ONLY");
        copy_text(d->scopes[1].title, sizeof(d->scopes[1].title), "ALL REPOS");
        for (unsigned i = 0; i < j->lang_count; ++i) {
            if (j->langs[i].original) badge_scope_add(&d->scopes[0], j->langs[i].name, j->langs[i].original);
            if (j->langs[i].all) badge_scope_add(&d->scopes[1], j->langs[i].name, j->langs[i].all);
        }
        d->scopes[0].total_bytes = j->original_bytes; d->scopes[1].total_bytes = j->all_bytes;
        for (unsigned i = 0; i < 2; ++i) {
            badge_scope_finish(&d->scopes[i]);
            d->scopes[i].fetched_repos = i ? j->repos_seen : j->originals_seen;
            d->scopes[i].total_repos = finished ? d->scopes[i].fetched_repos : d->public_repos;
            d->scopes[i].complete = enumerated && !j->languages_truncated;
        }
        for (unsigned i = 0; i < j->top_count; ++i) d->scopes[i + 2] = j->top_scopes[i];
        d->scope_count = 2 + j->top_count;
        d->valid |= BADGE_LANGUAGES;
        if (enumerated && !j->languages_truncated) d->complete |= BADGE_LANGUAGES;
        else d->complete &= ~BADGE_LANGUAGES;
    }
}

static bool watchers_json(const cJSON *json, void *ctx)
{
    badge_repo_t *repo = ctx;
    if (!cJSON_IsNumber(field(json, "subscribers_count"))) return false;
    repo->watchers = number_field(json, "subscribers_count"); repo->watchers_valid = true;
    return true;
}

static void restart_repos(sync_t *s)
{
    sync_job_t *j = s->job;
    j->done &= ~((1U << ST_REPOS) | (1U << ST_WATCHERS));
    j->watcher_index = 0;
    j->stage = ST_REPOS; j->page = 1; j->entry = j->page_count = j->repos_seen = j->originals_seen = 0;
    j->metadata_seen = 0;
    j->page_last = j->page_loaded = j->languages_truncated = false;
    j->previous_repo_count = j->page_repo_count = 0;
    j->top_count = 0; j->lang_count = 0; j->all_bytes = j->original_bytes = 0;
    memset(j->page_repos, 0, sizeof(j->page_repos)); memset(j->top, 0, sizeof(j->top));
    memset(j->top_scopes, 0, sizeof(j->top_scopes)); memset(j->langs, 0, sizeof(j->langs));
    save_job(s);
    s->retry = now_sec() + 60;
    copy_text(s->reason, sizeof(s->reason), "REPOSITORY CHANGED - RESTARTING");
}

static bool collect_avatar(sync_t *s)
{
    const char *source = s->data->avatar_url;
    const char prefix[] = "https://avatars.githubusercontent.com/";
    if (strncmp(source, prefix, sizeof(prefix) - 1)) {
        copy_text(s->reason, sizeof(s->reason), "AVATAR HOST UNSUPPORTED"); return false;
    }
    char url[280]; copy_text(url, sizeof(url), source);
    char *query = strchr(url, '?'); if (query) *query = 0;
    if (strlen(url) + 6 >= sizeof(url)) return false;
    strcat(url, "?s=54");
    esp_http_client_handle_t client = open_http(s, url);
    if (!client) return false;
    unsigned char *bytes = malloc(24 * 1024);
    if (!bytes) {
        copy_text(s->reason, sizeof(s->reason), "AVATAR MEMORY LIMIT");
        esp_http_client_cleanup(client); return false;
    }
    size_t used = 0; bool ok = true;
    while (used < 24 * 1024) {
        if (atomic_load(&s_cancel)) { ok = false; break; }
        int length = esp_http_client_read(client, (char *)bytes + used, (int)(24 * 1024 - used));
        if (length < 0) { ok = false; break; }
        if (!length) break;
        used += (size_t)length;
    }
    unsigned width, height;
    ok = ok && esp_http_client_is_complete_data_received(client) && badge_avatar_dimensions(bytes, used, &width, &height);
    esp_http_client_cleanup(client);
    if (!ok) copy_text(s->reason, sizeof(s->reason), "AVATAR INVALID / INCOMPLETE IMAGE");
    if (ok) {
        char path[160];
        ok = badge_cache_path(s->config->username, "avatar", path, sizeof(path)) && badge_file_write(path, bytes, used);
        if (!ok) { s->storage_failed = true; copy_text(s->reason, sizeof(s->reason), "AVATAR STORAGE ERROR"); }
    }
    free(bytes);
    if (ok) { s->data->valid |= BADGE_AVATAR; s->data->complete |= BADGE_AVATAR; }
    return ok;
}

static bool cached_avatar_valid(const char *username)
{
    char path[160];
    if (!badge_cache_path(username, "avatar", path, sizeof(path))) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    unsigned char *bytes = NULL;
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long length = ok ? ftell(file) : -1;
    ok = length >= 24 && length <= 24 * 1024 && fseek(file, 0, SEEK_SET) == 0;
    if (ok) {
        bytes = malloc((size_t)length);
        ok = bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length &&
             fgetc(file) == EOF && !ferror(file);
    }
    if (fclose(file) != 0) ok = false;
    unsigned width, height;
    if (ok) ok = badge_avatar_dimensions(bytes, (size_t)length, &width, &height);
    free(bytes);
    return ok;
}

static bool run_stage(sync_t *s)
{
    sync_job_t *j = s->job; badge_data_t *d = s->data;
    char path[320];
    switch (j->stage) {
    case ST_PROFILE: {
        /* Parsing a failed transfer must never overwrite the visible profile. */
        profile_value_t candidate = {0};
        snprintf(path, sizeof(path), "/users/%s", s->config->username);
        bool ok = request_json(s, path, false, profile_json, &candidate);
        if (ok) {
            memcpy(d->name, candidate.name, sizeof(d->name)); memcpy(d->company, candidate.company, sizeof(d->company));
            memcpy(d->location, candidate.location, sizeof(d->location)); memcpy(d->blog, candidate.blog, sizeof(d->blog));
            memcpy(d->joined, candidate.joined, sizeof(d->joined)); memcpy(d->avatar_url, candidate.avatar_url, sizeof(d->avatar_url));
            d->public_repos = candidate.public_repos; d->followers = candidate.followers;
            d->following = candidate.following; d->public_gists = candidate.public_gists;
            d->valid |= BADGE_PROFILE; d->complete |= BADGE_PROFILE; ++j->stage;
        }
        if (!ok) return false;
        progress(s, "PROFILE CACHED / AVATAR");
        break;
    }
    case ST_EVENTS:
        if (!collect_events(s)) return false;
        ++j->stage; progress(s, "EVENTS CACHED / PR + ISSUES"); break;
    case ST_SEARCH:
    case ST_ISSUES: {
        static const char *queries[] = {"type:pr", "type:pr%20is:open", "type:pr%20is:merged", "type:pr%20is:closed",
                                        "type:issue", "type:issue%20is:open", "type:issue%20is:closed"};
        unsigned first = j->stage == ST_SEARCH ? 0 : 4;
        unsigned end = j->stage == ST_SEARCH ? 4 : 7;
        j->search_index = first;
        while (j->search_index < end && (j->search_done & (1U << j->search_index))) ++j->search_index;
        if (j->search_index == end) { ++j->stage; break; }
        search_value_t value = {0};
        snprintf(path, sizeof(path), "/search/issues?q=author:%s%%20%s&per_page=1", s->config->username, queries[j->search_index]);
        if (!request_search(s, path, &value)) return false;
        j->search_values[j->search_index] = value.count;
        j->search_done |= 1U << j->search_index;
        if (value.incomplete) j->search_incomplete |= 1U << j->search_index;
        ++j->search_index; publish_search(s);
        if (j->search_index == end) { ++j->stage; progress(s, end == 4 ? "PULLS CACHED / ISSUES" : "ISSUES CACHED / STARS"); }
        /* Seven requests fit GitHub's unauthenticated 10/minute search limit.
         * Serial pacing also avoids a burst on shared event Wi-Fi. */
        vTaskDelay(pdMS_TO_TICKS(1200)); break;
    }
    case ST_STARS: {
        j->stars_count = j->stars_with_language = j->star_lang_count = 0; memset(j->stars, 0, sizeof(j->stars));
        snprintf(path, sizeof(path), "/users/%s/starred?per_page=100&sort=created&direction=desc", s->config->username);
        s->allow_large_drop = true;
        bool ok = request_json(s, path, true, stars_json, j);
        s->allow_large_drop = false;
        if (!ok && strcmp(s->reason, "RESPONSE LIMIT / BODY TOO LARGE")) return false;
        bool complete = ok && s->response_items < 100 && s->response_skipped == 0;
        if (complete) j->partial_lists &= ~BADGE_STARS; else j->partial_lists |= BADGE_STARS;
        if (complete || !(d->complete & BADGE_STARS)) {
            memcpy(d->stars, j->stars, sizeof(j->stars)); d->star_count = j->stars_count < 2 ? j->stars_count : 2;
            d->starred_fetched = j->stars_count; d->starred_with_language = j->stars_with_language; d->starred_languages = j->star_lang_count;
            d->valid |= BADGE_STARS;
            if (complete) d->complete |= BADGE_STARS; else d->complete &= ~BADGE_STARS;
        }
        ++j->stage; progress(s, "PUBLIC STARS CACHED / GISTS"); break;
    }
    case ST_GISTS: {
        j->gist_count = j->gist_public = 0; memset(j->gists, 0, sizeof(j->gists));
        snprintf(path, sizeof(path), "/users/%s/gists?per_page=100", s->config->username);
        /* A gist may describe hundreds of files in one object. Skip a large
         * item explicitly instead of blocking every later application page. */
        s->allow_large_drop = true;
        bool ok = request_json(s, path, true, gists_json, j);
        s->allow_large_drop = false;
        if (!ok && strcmp(s->reason, "RESPONSE LIMIT / BODY TOO LARGE")) return false;
        bool complete = ok && s->response_items < 100 && s->response_skipped == 0;
        if (complete) j->partial_lists &= ~BADGE_GISTS; else j->partial_lists |= BADGE_GISTS;
        if (complete || !(d->complete & BADGE_GISTS)) {
            memcpy(d->gists, j->gists, sizeof(j->gists)); d->gist_count = j->gist_count < 2 ? j->gist_count : 2;
            d->gists_fetched = j->gist_count; d->gists_public = j->gist_public; d->valid |= BADGE_GISTS;
            if (complete) d->complete |= BADGE_GISTS; else d->complete &= ~BADGE_GISTS;
        }
        ++j->stage;
        if (!j->page) j->page = 1;
        progress(s, "REPOSITORIES / LANGUAGES"); break;
    }
    case ST_REPOS:
        if (!j->page_loaded) {
            j->page_count = 0; j->entry = 0; j->page_repo_count = 0;
            snprintf(path, sizeof(path), "/users/%s/repos?type=owner&sort=full_name&direction=asc&per_page=%d&page=%" PRIu32,
                     s->config->username, REPO_PAGE, j->page);
            if (!request_json(s, path, true, repos_json, j)) return false;
            /* A shifted page can overlap its predecessor, but a page with no
             * new IDs must not spend quota traversing the same data forever. */
            if (s->response_items && !j->page_count) {
                copy_text(s->reason, sizeof(s->reason), "REPOSITORY PAGE REPEATED - RETRY");
                return false;
            }
            j->page_loaded = true; j->page_last = s->response_items < REPO_PAGE;
            memcpy(j->previous_repo_ids, j->page_repo_ids, sizeof(j->previous_repo_ids));
            j->previous_repo_count = j->page_repo_count;
            for (unsigned i = 0; i < j->page_count; ++i) {
                badge_scope_t unknown = {0};
                copy_text(unknown.title, sizeof(unknown.title), j->page_repos[i].name);
                badge_repo_rank(j->top, j->top_scopes, &j->top_count, &j->page_repos[i], &unknown);
            }
            j->metadata_seen += j->page_count;
            publish_repos(s, false);
            progress(s, "REPOSITORY LIST CACHED / LANGUAGES");
            if (s->publish_failed) return false;
            if (!save_job(s)) return false;
        }
        if (j->entry < j->page_count) {
            badge_repo_t *repo = &j->page_repos[j->entry];
            languages_context_t language = { .job = j, .repo = repo };
            snprintf(path, sizeof(path), "/repos/%s/languages", repo->full_name);
            /* Successful framed JSON can precede a truncated HTTP trailer.
             * Rollback aggregate changes by reloading the last durable cursor. */
            if (!request_json(s, path, false, languages_json, &language)) {
                if (s->http_status == 404) restart_repos(s);
                return false;
            }
            badge_repo_rank(j->top, j->top_scopes, &j->top_count, repo, &language.scope); ++j->repos_seen; if (!repo->fork) ++j->originals_seen;
            ++j->entry;
            if (j->entry % 5 == 0 || j->entry == j->page_count) {
                publish_repos(s, false);
                char message[64]; snprintf(message, sizeof(message), "LANGUAGES %" PRIu32 " / %" PRIu32, j->repos_seen, d->public_repos);
                progress(s, message);
            }
        } else if (j->page_last) {
            publish_repos(s, true); ++j->stage; j->watcher_index = 0;
            j->done &= ~(1U << ST_WATCHERS);
            progress(s, "REPOSITORIES CACHED / WATCHERS");
        } else { ++j->page; j->page_loaded = false; }
        break;
    case ST_WATCHERS:
        /* The journal can be newer than the last displayed snapshot after
         * power loss. Restore all completed detail results before continuing. */
        memcpy(d->repos, j->top, sizeof(j->top)); d->repo_count = j->top_count;
        if (j->watcher_index < j->top_count && j->watcher_index < 3) {
            badge_repo_t repo = j->top[j->watcher_index];
            snprintf(path, sizeof(path), "/repos/%s", repo.full_name);
            if (!request_json(s, path, false, watchers_json, &repo)) {
                if (s->http_status == 404) restart_repos(s);
                return false;
            }
            j->top[j->watcher_index] = repo; d->repos[j->watcher_index] = repo; ++j->watcher_index;
            progress(s, "WATCHER DETAILS CACHED");
        } else { ++j->stage; progress(s, "WATCHERS CACHED"); }
        break;
    case ST_AVATAR:
        if (!collect_avatar(s) || atomic_load(&s_cancel)) return false;
        ++j->stage; progress(s, "AVATAR CACHED / EVENTS"); break;
    default: return false;
    }
    if (s->publish_failed) { copy_text(s->reason, sizeof(s->reason), "CACHE NOT SAVED - WILL RETRY"); return false; }
    return true;
}

static uint32_t sections_for_stages(uint32_t stages)
{
    uint32_t sections = 0;
    for (unsigned i = 0; i < ST_FINISHED; ++i)
        if (stages & (1U << i)) sections |= stage_sections[i];
    return sections;
}

static unsigned next_stage(const sync_job_t *j, uint32_t attempted)
{
    for (unsigned i = 0; i < ST_FINISHED; ++i)
        if (!(j->done & (1U << i)) && !(attempted & (1U << i))) {
            /* Watchers require the final ranking; do not replace a previously
             * complete list with a partial enumeration after an endpoint fails. */
            if (i == ST_WATCHERS && !(j->done & (1U << ST_REPOS))) continue;
            return i;
        }
    return ST_FINISHED;
}

static bool stage_needs_http(const sync_job_t *j, unsigned stage)
{
    if (stage == ST_WATCHERS) return j->watcher_index < j->top_count && j->watcher_index < 3;
    if (stage == ST_REPOS) return !j->page_loaded || j->entry < j->page_count;
    if (stage == ST_SEARCH) return (j->search_done & 15U) != 15U;
    if (stage == ST_ISSUES) return (j->search_done & 112U) != 112U;
    return true;
}

esp_err_t badge_sync_run_request(const badge_config_t *config, badge_data_t *data,
                                 badge_sync_publish_t publish, void *context,
                                 bool manual, badge_sync_result_t *result)
{
    if (result) memset(result, 0, sizeof(*result));
    if (!config || !data || !badge_username_valid(config->username)) return ESP_ERR_INVALID_ARG;
    int64_t started = now_sec();
    if (started < 1577836800 || started > MAX_TIMESTAMP - 86400) {
        copy_text(data->sync_message, sizeof(data->sync_message), "WAITING FOR NETWORK TIME");
        if (result) copy_text(result->detail, sizeof(result->detail), data->sync_message);
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&s_cancel, false);
    sync_t s = { .data = data, .config = config, .publish = publish, .publish_context = context };
    s.job = calloc(1, sizeof(*s.job));
    if (!s.job) {
        copy_text(data->sync_message, sizeof(data->sync_message), "SYNC FAILED / MEMORY LIMIT");
        if (result) copy_text(result->detail, sizeof(result->detail), data->sync_message);
        return ESP_ERR_NO_MEM;
    }
    char cache[160]; badge_cache_path(config->username, "job", cache, sizeof(cache));
    bool loaded = read_blob(cache, s.job, sizeof(*s.job)) && job_valid(s.job, config->username);
    if (loaded) normalize_limits(s.job);
    int64_t preserved_limits[3] = {0};
    const uint32_t scopes[3] = {BADGE_SYNC_LIMIT_CORE, BADGE_SYNC_LIMIT_SEARCH, BADGE_SYNC_LIMIT_AVATAR};
    if (loaded) for (unsigned i = 0; i < 3; ++i) preserved_limits[i] = scope_deadline(s.job, scopes[i]);
    bool resume = loaded && s.job->done != ALL_STAGES;
    /* A snapshot's avatar bit cannot prove that its separate file survived.
     * Check before TLS starts, and repair only this stage of an unfinished
     * collection. Keep quota bits/deadlines and every other completed stage.
     * A previously finished collection still starts a normal fresh cycle. */
    if (!(data->valid & BADGE_AVATAR) || !cached_avatar_valid(config->username)) {
        data->valid &= ~BADGE_AVATAR;
        data->complete &= ~BADGE_AVATAR;
        if (resume) s.job->done &= ~(1U << ST_AVATAR);
    }
    if (resume) {
        /* A valid journal never proves that a missing/CRC-rejected snapshot
         * exists. Restart if a section acknowledged in this cycle is absent. */
        uint32_t required = sections_for_stages(s.job->done);
        if ((data->valid & required) != required) resume = false;
    }
    if (!resume) {
        memset(s.job, 0, sizeof(*s.job)); s.job->magic = JOB_MAGIC;
        copy_text(s.job->username, sizeof(s.job->username), config->username);
        s.job->started = now_sec(); s.job->page = 1;
        for (unsigned i = 0; i < 3; ++i) if (preserved_limits[i]) {
            char message[64]; snprintf(message, sizeof(message), "%s RATE LIMIT - WILL RESUME", scope_label(scopes[i]));
            set_limit(s.job, scopes[i], preserved_limits[i], message);
        }
    } else if (now_sec() - s.job->started >= config->interval_sec) {
        /* Refresh cheap sections on schedule while retaining an expensive
         * repository/language cursor across hourly unauthenticated quotas. */
        uint32_t short_stages = (1U << ST_REPOS) - 1U;
        s.job->done &= ~short_stages; s.job->failed &= ~short_stages;
        s.job->search_index = s.job->search_incomplete = s.job->search_done = 0;
        memset(s.job->search_values, 0, sizeof(s.job->search_values));
        s.job->started = now_sec();
    }
    if (manual) for (unsigned i = 0; i < ST_FINISHED; ++i)
        if (!(s.job->limited & (1U << i))) s.job->deadlines[i] = 0;
    data->last_attempt = now_sec(); data->retry_after = 0;
    int64_t previous_success = data->last_success;
    progress(&s, resume ? "RESUMING CACHED COLLECTION" : "SYNCING ALL PUBLIC PAGES");
    bool fatal = s.publish_failed || !save_job(&s);
    uint32_t attempted = 0;
    while (!fatal) {
        unsigned stage = next_stage(s.job, attempted);
        if (stage == ST_FINISHED) break;
        if (((s.job->failed | s.job->limited) & (1U << stage)) && s.job->deadlines[stage] > now_sec() && stage_needs_http(s.job, stage)) {
            attempted |= 1U << stage;
            continue;
        }
        s.job->stage = stage; s.reason[0] = 0; s.retry = 0; s.http_status = 0;
        s.search_bucket = stage == ST_SEARCH || stage == ST_ISSUES;
        if (atomic_load(&s_cancel)) { fatal = true; break; }
        bool ok = run_stage(&s);
        if (ok) {
            if (s.job->stage != stage) {
                s.job->done |= 1U << stage;
                s.job->failed &= ~(1U << stage);
                if (!(s.job->limited & (1U << stage))) {
                    s.job->errors[stage][0] = 0; s.job->deadlines[stage] = 0;
                }
                attempted |= 1U << stage;
            }
            fatal = s.job->done != ALL_STAGES && !save_job(&s);
        } else {
            fatal = s.publish_failed || s.storage_failed || atomic_load(&s_cancel);
            if (fatal) {
                if (stage == ST_AVATAR) copy_text(s.job->errors[stage], sizeof(s.job->errors[stage]),
                    s.reason[0] ? s.reason : atomic_load(&s_cancel) ? "AVATAR CANCELLED" : "AVATAR CACHE NOT SAVED");
                break;
            }
            /* A parsed language object can precede a broken HTTP trailer.
             * Reload its acknowledged cursor before collecting other pages. */
            if (!read_blob(cache, s.job, sizeof(*s.job))) {
                s.storage_failed = true; fatal = true;
                copy_text(s.reason, sizeof(s.reason), "CURSOR RECOVERY ERROR"); break;
            }
            s.job->failed |= 1U << stage;
            if (!(s.job->limited & (1U << stage)))
                s.job->deadlines[stage] = s.retry > now_sec() ? s.retry : now_sec() + (s.http_status == 404 ? 21600 : 300);
            if (!s.reason[0]) copy_text(s.reason, sizeof(s.reason), "INCOMPLETE RESPONSE");
            copy_text(s.job->errors[stage], sizeof(s.job->errors[stage]), s.reason);
            attempted |= 1U << stage;
            if (stage == ST_REPOS && s.job->repos_seen) publish_repos(&s, false);
            char message[64];
            snprintf(message, sizeof(message), "%.10s FAILED / CONTINUING", stage_names[stage]);
            progress(&s, message);
            fatal = s.publish_failed || !save_job(&s);
        }
        vTaskDelay(1);
    }
    bool ok = !fatal && s.job->done == ALL_STAGES;
    if (ok) {
        data->last_success = now_sec(); data->retry_after = 0;
        progress(&s, s.job->partial_lists || (data->complete & 0x1ffU) != 0x1ffU ?
                 "SYNCED / PARTIAL PUBLIC DATA" : "SYNCED / ALL PAGES CACHED");
        if (s.publish_failed) {
            data->last_success = previous_success;
            ok = false;
        }
    }
    if (!ok) {
        int64_t retry = 0;
        unsigned first_error = ST_FINISHED;
        for (unsigned i = 0; i < ST_FINISHED; ++i) if ((s.job->failed | s.job->limited) & (1U << i) && !(s.job->done & (1U << i))) {
            if (first_error == ST_FINISHED) first_error = i;
            if (!retry || s.job->deadlines[i] < retry) retry = s.job->deadlines[i];
        }
        data->retry_after = retry > now_sec() ? retry : now_sec() + 300;
        char message[64];
        if (atomic_load(&s_cancel)) copy_text(message, sizeof(message), "SYNC CANCELLED / CACHE KEPT");
        else if (s.publish_failed || s.storage_failed) copy_text(message, sizeof(message), "SYNC FAILED / CACHE NOT SAVED");
        else if (first_error < ST_FINISHED)
            snprintf(message, sizeof(message), "PARTIAL / %.8s: %.42s", stage_names[first_error], s.job->errors[first_error]);
        else copy_text(message, sizeof(message), "SYNC PAUSED / WILL RESUME");
        progress(&s, message);
    }
    /* The terminal successful snapshot must be acknowledged before writing a
     * finished journal; interruption leaves work resumable, never falsely done. */
    if (!s.publish_failed && !s.storage_failed && !save_job(&s)) {
        data->last_success = previous_success;
        data->retry_after = now_sec() + 300;
        progress(&s, "SYNC FAILED / CURSOR NOT SAVED"); ok = false;
    }
    if (result) {
        result->refreshed = sections_for_stages(s.job->done);
        result->failed = sections_for_stages(s.job->failed);
        result->pending = sections_for_stages(ALL_STAGES & ~s.job->done);
        result->cache_error = s.publish_failed || s.storage_failed;
        result->cancelled = atomic_load(&s_cancel);
        result->core_retry_after = scope_deadline(s.job, BADGE_SYNC_LIMIT_CORE);
        result->search_retry_after = scope_deadline(s.job, BADGE_SYNC_LIMIT_SEARCH);
        result->avatar_retry_after = scope_deadline(s.job, BADGE_SYNC_LIMIT_AVATAR);
        for (unsigned i = 0; i < 3; ++i) {
            int64_t until = scope_deadline(s.job, scopes[i]);
            if (until) result->limited_scopes |= scopes[i];
        }
        for (unsigned i = 0; i < ST_FINISHED; ++i)
            if ((s.job->limited & (1U << i)) && !(s.job->done & (1U << i)) && s.job->deadlines[i] > now_sec() &&
                (!result->rate_limit_until || s.job->deadlines[i] < result->rate_limit_until))
                result->rate_limit_until = s.job->deadlines[i];
        result->rate_limited = result->limited_scopes != 0;
        result->waiting_only = s.request_count == 0 && s.job->done != ALL_STAGES && !result->cache_error && !result->cancelled;
        result->retry_after = data->retry_after;
        copy_text(result->detail, sizeof(result->detail), data->sync_message);
        copy_text(result->avatar_error, sizeof(result->avatar_error), s.job->errors[ST_AVATAR]);
    }
    free(s.job);
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t badge_sync_run_ex(const badge_config_t *config, badge_data_t *data,
                            badge_sync_publish_t publish, void *context,
                            badge_sync_result_t *result)
{
    return badge_sync_run_request(config, data, publish, context, false, result);
}

esp_err_t badge_sync_run(const badge_config_t *config, badge_data_t *data,
                         badge_sync_publish_t publish, void *context)
{
    return badge_sync_run_ex(config, data, publish, context, NULL);
}
