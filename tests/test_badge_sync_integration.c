#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "badge_sync.h"
#include "badge_sync_parse.h"
#include "badge_storage.h"
#include "esp_http_client.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static time_t test_now = 1704844800; /* 2024-01-10 UTC */
time_t badge_test_time(time_t *out) { if (out) *out = test_now; return test_now; }
static char directory[160];
static const char *fail_match;
static const char *cancel_match;
static const char *reject_publication_match;
static int fail_status;
static const char *fail_match_second;
static int fail_status_second;
static const char *fixture_remaining, *fixture_retry_after, *fixture_reset, *fixture_message, *fixture_resource;
static bool custom_headers;
static bool truncated_response, fail_write, fail_exact, huge_gists;
static bool repeat_repo_page;
static unsigned repo_page_requests;
static size_t gist_filler_size = 32 * 1024;
static unsigned profiles, lang_a, lang_b, publications, watch_b, starred_requests;
static unsigned avatar_requests, events_requests, search_requests, request_serial, avatar_serial, events_serial;
static const char *profile_name = "Alpha";
static badge_data_t last_publication;
static badge_data_t durable_publication;

struct test_http_client {
    esp_http_client_config_t config;
    char url[400], payload[3000];
    char generated_tail[384];
    size_t length, position, filler_size;
    int status;
    bool truncated;
    bool generated_body;
};

int esp_crt_bundle_attach(void *conf) { (void)conf; return 0; }
void vTaskDelay(unsigned ticks) { (void)ticks; }

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    struct test_http_client *client = calloc(1, sizeof(*client)); assert(client);
    client->config = *config; snprintf(client->url, sizeof(client->url), "%s", config->url);
    assert(config->disable_auto_redirect && config->crt_bundle_attach && config->timeout_ms <= 15000);
    return client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value)
{ (void)client; (void)key; (void)value; return ESP_OK; }

esp_err_t esp_http_client_open(esp_http_client_handle_t c, int write_len)
{
    (void)write_len;
    c->status = 200;
    ++request_serial;
    const char *body = NULL;
    if (strstr(c->url, "/repos/tester/a/languages")) { ++lang_a; body = "{\"C\":100,\"Rust\":300}"; }
    else if (strstr(c->url, "/repos/tester/b/languages")) { ++lang_b; body = "{\"C\":1000,\"Shell\":50}"; }
    else if (strstr(c->url, "/repos/tester/a")) body = "{\"subscribers_count\":7,\"watchers_count\":5}";
    else if (strstr(c->url, "/repos/tester/b")) { ++watch_b; body = "{\"subscribers_count\":11,\"watchers_count\":9}"; }
    else if (strstr(c->url, "/users/tester/repos?")) {
        ++repo_page_requests;
        const char *pair = "{\"id\":1,\"name\":\"a\",\"full_name\":\"tester/a\",\"stargazers_count\":5,\"forks_count\":2,\"fork\":false,\"pushed_at\":\"2024-01-09T10:00:00Z\",\"license\":{\"spdx_id\":\"MIT\"}},{\"id\":2,\"name\":\"b\",\"full_name\":\"tester/b\",\"stargazers_count\":9,\"forks_count\":3,\"fork\":true,\"pushed_at\":\"2024-01-08T10:00:00Z\",\"license\":null}";
        if (repeat_repo_page) {
            snprintf(c->payload, sizeof(c->payload), "[%s,%s,%s,%s,%s]", pair, pair, pair, pair, pair);
            if (repo_page_requests > 2) c->status = 500; /* Bound a regression's requests. */
        } else if (strstr(c->url, "&page=1")) snprintf(c->payload, sizeof(c->payload), "[%s]", pair);
        else body = "[]";
    }
    else if (strstr(c->url, "/events/public?")) {
        ++events_requests; events_serial = request_serial;
        body = "[{\"id\":\"10001\",\"type\":\"PushEvent\",\"created_at\":\"2024-01-09T10:00:00Z\",\"repo\":{\"name\":\"tester/a\"}},{\"id\":\"10002\",\"type\":\"IssuesEvent\",\"created_at\":\"2024-01-08T10:00:00Z\",\"repo\":{\"name\":\"tester/b\"}}]";
    }
    else if (strstr(c->url, "/starred?")) { ++starred_requests; body = "[{\"full_name\":\"other/one\",\"language\":\"C\",\"stargazers_count\":55},{\"full_name\":\"other/two\",\"language\":\"Rust\",\"stargazers_count\":66}]"; }
    else if (strstr(c->url, "/gists?")) {
        if (huge_gists) {
            strcpy(c->payload, "[{\"id\":\"oversized\",\"description\":\"");
            strcpy(c->generated_tail, "\",\"files\":{},\"public\":true},{\"id\":\"small\",\"description\":\"SMALL OK\",\"files\":{},\"public\":true,\"updated_at\":\"2024-01-09T00:00:00Z\"}]");
            c->filler_size = gist_filler_size;
            c->length = strlen(c->payload) + c->filler_size + strlen(c->generated_tail);
            c->generated_body = true;
        } else body = "[{\"id\":\"abcdef\",\"description\":null,\"files\":{\"notes.txt\":{}},\"public\":true,\"updated_at\":\"2024-01-09T12:00:00Z\"}]";
    }
    else if (strstr(c->url, "/search/issues?")) {
        ++search_requests;
        unsigned count;
        bool pr = strstr(c->url, "type:pr") != NULL;
        if (strstr(c->url, "is:merged")) count = 5;
        else if (strstr(c->url, "is:open")) count = pr ? 3 : 1;
        else if (strstr(c->url, "is:closed")) count = pr ? 7 : 3;
        else count = pr ? 10 : 4;
        strcpy(c->payload, "{\"items\":[{\"body\":\"");
        snprintf(c->generated_tail, sizeof(c->generated_tail), "\"}],\"incomplete_results\":false,\"total_count\":%u}", count);
        c->filler_size = 100 * 1024;
        c->length = strlen(c->payload) + c->filler_size + strlen(c->generated_tail);
        c->generated_body = true;
    } else if (strstr(c->url, "https://avatars.githubusercontent.com/u/42?s=54")) {
        ++avatar_requests; avatar_serial = request_serial;
        /* The transport accepts a bounded PNG; UI decoding is a separate test. */
        const unsigned char png[32] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,54,0,0,0,54};
        memcpy(c->payload, png, sizeof(png)); c->length = sizeof(png);
    } else if (!strcmp(c->url, "https://api.github.com/users/tester")) {
        ++profiles;
        snprintf(c->payload, sizeof(c->payload), "{\"login\":\"tester\",\"name\":\"%s\",\"company\":null,\"location\":\"Earth\",\"blog\":\"https://example.com\",\"created_at\":\"2019-04-02T00:00:00Z\",\"public_repos\":2,\"followers\":10,\"following\":20,\"public_gists\":1,\"avatar_url\":\"https://avatars.githubusercontent.com/u/42?v=4\"}", profile_name);
    } else { fprintf(stderr, "unexpected fixture URL: %s\n", c->url); assert(false); }
    if (body) snprintf(c->payload, sizeof(c->payload), "%s", body);
    if (!c->length) c->length = strlen(c->payload);
    if (fail_match && (fail_exact ? !strcmp(c->url, fail_match) : strstr(c->url, fail_match) != NULL)) {
        c->status = fail_status;
        c->truncated = truncated_response;
        if (fixture_message) {
            snprintf(c->payload, sizeof(c->payload), "{\"message\":\"%s\"}", fixture_message);
            c->generated_body = false; c->length = strlen(c->payload);
        }
    }
    if (fail_match_second && strstr(c->url, fail_match_second)) c->status = fail_status_second;
    return ESP_OK;
}

int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c)
{
    if (custom_headers && fail_match && (fail_exact ? !strcmp(c->url, fail_match) : strstr(c->url, fail_match) != NULL)) {
        const char *names[] = {"X-RateLimit-Remaining", "Retry-After", "X-RateLimit-Reset", "X-RateLimit-Resource"};
        const char *values[] = {fixture_remaining, fixture_retry_after, fixture_reset, fixture_resource};
        for (unsigned i = 0; i < 4; ++i) if (values[i]) {
            esp_http_client_event_t event = {HTTP_EVENT_ON_HEADER, names[i], values[i], c->config.user_data};
            c->config.event_handler(&event);
        }
    } else if (c->status == 429) {
        char reset[32]; snprintf(reset, sizeof(reset), "%lld", (long long)test_now + 3600);
        esp_http_client_event_t event = {HTTP_EVENT_ON_HEADER, "X-RateLimit-Reset", reset, c->config.user_data};
        c->config.event_handler(&event);
        event.header_key = "X-RateLimit-Remaining"; event.header_value = "0"; c->config.event_handler(&event);
    }
    return (int64_t)c->length;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c) { return c->status; }
int esp_http_client_read(esp_http_client_handle_t c, char *buffer, int length)
{
    size_t remaining = c->length - c->position;
    if (!remaining) return 0;
    size_t chunk = remaining < 7 ? remaining : 7; /* split UTF-8/JSON tokens */
    if (chunk > (size_t)length) chunk = (size_t)length;
    if (c->generated_body) {
        size_t prefix = strlen(c->payload);
        for (size_t i = 0; i < chunk; ++i) {
            size_t position = c->position + i;
            buffer[i] = position < prefix ? c->payload[position] : position < prefix + c->filler_size ? 'x' : c->generated_tail[position - prefix - c->filler_size];
        }
    } else memcpy(buffer, c->payload + c->position, chunk);
    c->position += chunk;
    if (cancel_match && strstr(c->url, cancel_match)) badge_sync_cancel();
    return (int)chunk;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c)
{ return !c->truncated && c->position == c->length; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { free(c); return ESP_OK; }

bool badge_cache_path(const char *username, const char *suffix, char *out, size_t size)
{ return snprintf(out, size, "%s/%s-%s", directory, username, suffix) < (int)size; }
bool badge_file_write(const char *path, const void *data, size_t size)
{
    if (fail_write) return false;
    char temp[200]; snprintf(temp, sizeof(temp), "%s.tmp", path);
    FILE *file = fopen(temp, "wb"); assert(file);
    bool ok = fwrite(data, 1, size, file) == size; ok = fclose(file) == 0 && ok;
    return ok && rename(temp, path) == 0;
}
static bool publish(const badge_data_t *data, void *context)
{
    assert(badge_data_valid(data)); /* Production storage guards every checkpoint. */
    (void)context; last_publication = *data; ++publications;
    if (reject_publication_match && strstr(data->sync_message, reject_publication_match)) return false;
    durable_publication = *data;
    return true;
}

/* Frozen job prefix, also used to exercise checksum-valid malformed caches. */
typedef struct {
    uint32_t magic; char username[40]; int64_t started;
    uint32_t stage, search_index, search_values[7], search_incomplete, partial_lists;
    uint32_t done, failed, limited, watcher_index, search_done;
    int64_t deadlines[9]; char errors[9][64];
    uint32_t page, entry, page_count, repos_seen, originals_seen, metadata_seen;
    uint64_t previous_repo_ids[10], page_repo_ids[10];
    uint32_t previous_repo_count, page_repo_count;
    bool page_last, page_loaded, languages_truncated;
    badge_repo_t page_repos[10], top[BADGE_REPOS];
    badge_scope_t top_scopes[BADGE_REPOS];
    uint8_t top_count;
} job_prefix_fixture_t;

static void rewrite_job_fixture(const char *username, int damage)
{
    job_prefix_fixture_t prefix;
    char path[200]; badge_cache_path(username, "job", path, sizeof(path));
    FILE *file = fopen(path, "rb"); assert(file);
    assert(fseek(file, 0, SEEK_END) == 0); long length = ftell(file);
    assert(length > (long)sizeof(prefix) && length < 32768 && fseek(file, 0, SEEK_SET) == 0);
    unsigned char *bytes = malloc((size_t)length); assert(bytes);
    assert(fread(bytes, 1, (size_t)length, file) == (size_t)length && fclose(file) == 0);
    memcpy(&prefix, bytes, sizeof(prefix));
    assert(prefix.magic == 0x47484a33U);
    switch (damage) {
    case -1: /* Released v0.2 stored a core limit on only the failed stage. */
        assert(prefix.deadlines[7] > test_now);
        prefix.limited = 1U << 7;
        for (unsigned i = 0; i < 9; ++i) if (i != 7) { prefix.deadlines[i] = 0; prefix.errors[i][0] = 0; }
        break;
    case 0: memset(prefix.username, 'x', sizeof(prefix.username)); break;
    case 1: memset(prefix.errors[3], 'x', sizeof(prefix.errors[3])); break;
    case 2: memset(prefix.page_repos[0].full_name, 'x', sizeof(prefix.page_repos[0].full_name)); break;
    case 3: memset(prefix.top_scopes[0].languages[0].name, 'x', sizeof(prefix.top_scopes[0].languages[0].name)); break;
    case 4: prefix.top_scopes[0].count = BADGE_LANGS + 1; break;
    case 5: { const unsigned char malformed = 2; memcpy(&prefix.page_loaded, &malformed, 1); break; }
    case 6: prefix.started = INT64_MIN; break;
    case 7: prefix.deadlines[3] = INT64_MAX; break;
    case 8: prefix.search_done = 1U << 31; break;
    case 9: prefix.entry = prefix.page_count + 1; break;
    default: assert(false);
    }
    memcpy(bytes, &prefix, sizeof(prefix));
    uint32_t crc = 2166136261U;
    for (size_t i = 0; i < (size_t)length - sizeof(crc); ++i) crc = (crc ^ bytes[i]) * 16777619U;
    memcpy(bytes + length - sizeof(crc), &crc, sizeof(crc));
    assert(badge_file_write(path, bytes, (size_t)length)); free(bytes);
}

static void check_complete(const badge_data_t *data)
{
    assert((data->valid & 0x1ffU) == 0x1ffU && (data->complete & 0x1ffU) == 0x1ffU);
    assert(data->last_success == test_now && !data->retry_after);
    assert(data->repo_count == 2 && data->repos[0].fork && !strcmp(data->repos[0].name, "b"));
    assert(data->repos[0].watchers_valid && data->repos[0].watchers == 11);
    assert(data->repos[1].watchers == 7);
    assert(data->scope_count == 4 && data->scopes[0].total_bytes == 400 && data->scopes[1].total_bytes == 1450);
    assert(data->scopes[0].fetched_repos == 1 && data->scopes[1].fetched_repos == 2);
    assert(data->scopes[2].total_bytes == 1050 && !strcmp(data->scopes[2].title, "b"));
    assert(data->pr_total == 10 && data->pr_open == 3 && data->pr_merged == 5 && data->pr_closed == 7);
    assert(data->issue_total == 4 && data->issue_open == 1 && data->issue_closed == 3);
    assert(data->starred_fetched == 2 && data->starred_languages == 2 && data->gists_fetched == 1);
    assert(!strcmp(data->gists[0].description, "notes.txt"));
    assert(data->events_fetched == 2 && data->push_events == 1 && data->push_days == 1 && data->event_count == 2);
}

int main(void)
{
    snprintf(directory, sizeof(directory), "/tmp/badge-sync-test-XXXXXX"); assert(mkdtemp(directory));
    badge_config_t config; badge_config_defaults(&config); snprintf(config.username, sizeof(config.username), "tester");
    badge_data_t data; badge_data_reset(&data, config.username);

    /* The first language succeeds before quota exhaustion; retry must reuse
     * that durable result and not charge it again or double its bytes. */
    fail_match = "/repos/tester/b/languages"; fail_status = 429;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_FAIL);
    assert(profiles == 1 && lang_a == 1 && lang_b == 1 && data.retry_after >= test_now + 3600);
    assert(!data.last_success && data.events_fetched == 2 && data.pr_total == 10);
    assert((data.valid & BADGE_AVATAR) && data.repo_count == 2 && data.repos_fetched == 2);
    assert(data.repos[0].stars == 9 && !data.scopes[2].valid); /* language still pending */
    fail_match = NULL; test_now += 3606;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_OK);
    assert(profiles == 1 && lang_a == 1 && lang_b == 2);
    check_complete(&data);

    badge_scope_t old_scopes[BADGE_SCOPES]; memcpy(old_scopes, data.scopes, sizeof(old_scopes));
    badge_repo_t old_repos[BADGE_REPOS]; memcpy(old_repos, data.repos, sizeof(old_repos));
    int64_t old_success = data.last_success;
    profile_name = "Beta"; test_now += config.interval_sec;
    fail_match = "/repos/tester/b/languages"; fail_status = 200; truncated_response = true;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_FAIL);
    assert(!strcmp(data.name, "Beta") && data.last_success == old_success);
    assert(!memcmp(old_scopes, data.scopes, sizeof(old_scopes)) && !memcmp(old_repos, data.repos, sizeof(old_repos)));
    assert(data.events_fetched == 2); /* second Events pass deduplicates */

    /* A lengthy quota job refreshes priority data without resetting its
     * repository-language cursor. A parsed but truncated response rolls back. */
    unsigned a_before = lang_a, profiles_before = profiles;
    fail_match = NULL; truncated_response = false; test_now += config.interval_sec;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_OK);
    assert(profiles == profiles_before + 1 && lang_a == a_before);
    check_complete(&data);
    assert(last_publication.last_success == test_now && publications > 10);

    /* Failure before a valid new profile preserves every last-good section. */
    fail_match = "/users/tester"; fail_status = 500; test_now += config.interval_sec;
    badge_data_t previous = data;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_FAIL);
    assert(!strcmp(data.name, previous.name) && data.last_success == previous.last_success);
    assert(!memcmp(previous.scopes, data.scopes, sizeof(data.scopes)));
    fail_match = NULL; cancel_match = "/users/tester"; test_now += 301;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_FAIL);
    assert(strstr(data.sync_message, "CANCELLED") && !strcmp(data.name, previous.name));
    assert(!memcmp(previous.scopes, data.scopes, sizeof(data.scopes)));
    cancel_match = NULL;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_OK);
    check_complete(&data);

    /* A reboot can load an older visible snapshot than its watcher cursor.
     * The completed first detail must be restored from the work journal. */
    test_now += config.interval_sec;
    fail_match = "https://api.github.com/repos/tester/a"; fail_exact = true; fail_status = 429;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_FAIL);
    unsigned watch_b_before = watch_b;
    data.repos[0].watchers = 0; data.repos[0].watchers_valid = false;
    fail_match = NULL; fail_exact = false; test_now += 3606;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_OK);
    assert(watch_b == watch_b_before); check_complete(&data);

    /* Failed snapshot persistence must not advance past ST_STARS. Emulate
     * loading the last durable visible snapshot before resuming the job. */
    test_now += config.interval_sec;
    reject_publication_match = "PUBLIC STARS CACHED";
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_FAIL);
    unsigned stars_before = starred_requests;
    data = durable_publication; reject_publication_match = NULL;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_OK);
    assert(starred_requests == stars_before + 1); check_complete(&data);

    /* A valid work journal cannot authorize skipping a section whose main
     * snapshot was rejected by CRC or deleted. Restart from PROFILE. */
    test_now += config.interval_sec;
    fail_match = "/repos/tester/b/languages"; fail_status = 429;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_FAIL);
    profiles_before = profiles; a_before = lang_a;
    badge_data_reset(&data, config.username); fail_match = NULL;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, NULL) == ESP_FAIL);
    assert(profiles == profiles_before && lang_a == a_before); /* quota survives missing snapshot */
    test_now += 3606;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_OK);
    assert(profiles == profiles_before + 1 && lang_a == a_before + 1);
    check_complete(&data); /* event journal still prevents duplicate counts */

    /* A 32 KiB gist object must not permanently block repository/language
     * collection. A complete previous gist section remains the last-good view. */
    test_now += config.interval_sec; huge_gists = true; a_before = lang_a;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_OK);
    assert(lang_a == a_before + 1 && data.last_success == test_now);
    assert(data.complete & BADGE_GISTS);
    assert(!strcmp(data.gists[0].description, "notes.txt") && strstr(data.sync_message, "PARTIAL"));
    /* Without a prior complete section, only successfully parsed items count. */
    badge_data_reset(&data, config.username); test_now += config.interval_sec;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_OK);
    assert(data.valid & BADGE_GISTS); assert(!(data.complete & BADGE_GISTS));
    assert(data.gists_fetched == 1 && data.gists_public == 1 && !strcmp(data.gists[0].description, "SMALL OK"));
    assert(data.repo_count == 2 && (data.complete & BADGE_REPOSITORIES) && (data.complete & BADGE_LANGUAGES));
    assert(strstr(data.sync_message, "PARTIAL"));
    /* The whole-response byte ceiling likewise yields an explicitly partial
     * list, while keeping the bounded-memory collector's later pages usable. */
    gist_filler_size = 2 * 1024 * 1024; test_now += config.interval_sec; a_before = lang_a;
    assert(badge_sync_run(&config, &data, publish, NULL) == ESP_OK);
    assert(lang_a == a_before + 1 && (data.valid & BADGE_GISTS) && !(data.complete & BADGE_GISTS));
    assert(strstr(data.sync_message, "PARTIAL"));
    huge_gists = false;

    /* An events failure used to leave every page after PROFILE empty and never
     * fetch the avatar. Independent sections now all run in the same attempt. */
    badge_data_reset(&data, config.username); test_now += config.interval_sec;
    badge_sync_result_t result;
    fail_match = "/events/public?"; fail_status = 500;
    unsigned avatar_before = avatar_requests, events_before = events_requests;
    profiles_before = profiles; a_before = lang_a;
    assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_FAIL);
    assert((data.valid & (0x1ffU & ~BADGE_ACTIVITY)) == (0x1ffU & ~BADGE_ACTIVITY));
    assert(avatar_requests == avatar_before + 1 && avatar_serial < events_serial);
    assert(result.failed == BADGE_ACTIVITY && result.pending == BADGE_ACTIVITY);
    assert(!result.cache_error && !data.last_success && strstr(result.detail, "ACTIVITY"));
    fail_match = NULL; test_now += 301;
    assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_OK);
    assert(profiles == profiles_before + 1 && avatar_requests == avatar_before + 1 && lang_a == a_before + 1);
    assert(events_requests == events_before + 2 && !result.failed && !result.pending);
    check_complete(&data);

    /* Failure of a PR query must not block the independent Issues page. Its
     * retry also must not repeat any other successful page or avatar request. */
    test_now += config.interval_sec; fail_match = "type:pr"; fail_status = 500;
    unsigned search_before = search_requests; avatar_before = avatar_requests;
    assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_FAIL);
    assert(result.failed == BADGE_PULLS && result.pending == BADGE_PULLS);
    assert((result.refreshed & BADGE_ISSUES) && search_requests == search_before + 4);
    assert(data.issue_total == 4 && data.last_success < test_now);
    test_now += 301; fail_match = NULL;
    assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_OK);
    assert(search_requests == search_before + 8 && avatar_requests == avatar_before + 1);
    check_complete(&data);

    /* Search has its own rate bucket: defer both search sections while still
     * collecting core endpoints. Calls before Retry-After send no requests. */
    test_now += config.interval_sec; fail_match = "/search/issues?"; fail_status = 429;
    a_before = lang_a; search_before = search_requests;
    assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_FAIL);
    assert(result.rate_limited && result.failed == BADGE_PULLS && result.pending == (BADGE_PULLS | BADGE_ISSUES));
    assert(lang_a == a_before + 1 && search_requests == search_before + 1);
    unsigned requests_before = request_serial;
    fail_match = NULL;
    assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_FAIL);
    assert(request_serial == requests_before && result.rate_limited);
    test_now += 3606;
    assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_OK);
    assert(search_requests == search_before + 8 && lang_a == a_before + 1);
    check_complete(&data);

    /* Avatar failure is visible even when an older cached image is retained;
     * retry fetches only the image, and no rendering path invokes this worker. */
    test_now += config.interval_sec; fail_match = "avatars.githubusercontent.com"; fail_status = 500;
    avatar_before = avatar_requests; profiles_before = profiles;
    assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_FAIL);
    assert(result.failed == BADGE_AVATAR && result.pending == BADGE_AVATAR);
    assert(strstr(result.avatar_error, "HTTP 500") && (data.valid & BADGE_AVATAR));
    test_now += 301; fail_match = NULL;
    assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_OK);
    assert(avatar_requests == avatar_before + 2 && profiles == profiles_before + 1);
    check_complete(&data);

    /* Storage failure is fatal and cannot be reported as a successful offline
     * cache. Fail the journal write before the first expensive HTTP request. */
    test_now += config.interval_sec; fail_write = true; requests_before = request_serial;
    old_success = data.last_success;
    assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_FAIL);
    assert(result.cache_error && request_serial == requests_before && data.last_success == old_success);
    assert(strstr(result.detail, "CACHE NOT SAVED")); fail_write = false;

    /* Manual REFETCH retries ordinary errors immediately; automatic callers
     * retain their existing backoff. No successful section is fetched twice. */
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);
    test_now += config.interval_sec; fail_match = "/events/public?"; fail_status = 500;
    events_before = events_requests;
    assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_FAIL);
    fail_match = NULL; requests_before = request_serial;
    assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_FAIL);
    assert(request_serial == requests_before && result.waiting_only && !result.rate_limited);
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);
    assert(events_requests == events_before + 2 && request_serial == requests_before + 1);

    /* A plain access-policy 403 (including malformed quota headers) is not a
     * rate limit and must remain manually retryable. */
    test_now += config.interval_sec; fail_match = "/events/public?"; fail_status = 403;
    custom_headers = true; fixture_remaining = "0oops"; fixture_message = "Access denied";
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(!result.rate_limited && !result.limited_scopes && strstr(result.detail, "HTTP 403"));
    fail_match = NULL; fixture_remaining = fixture_message = NULL; custom_headers = false;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);

    /* Retry-After is authoritative even if the primary reset clock is much
     * later. Both API buckets wait for this secondary limit, not one hour. */
    test_now += config.interval_sec; fail_match = "/events/public?"; fail_status = 403;
    custom_headers = true; fixture_remaining = "37"; fixture_retry_after = "60";
    char reset_header[32]; snprintf(reset_header, sizeof(reset_header), "%lld", (long long)test_now + 3600);
    fixture_reset = reset_header;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(result.core_retry_after == test_now + 60 && result.search_retry_after == test_now + 60);
    assert(result.limited_scopes == (BADGE_SYNC_LIMIT_CORE | BADGE_SYNC_LIMIT_SEARCH));
    assert(result.rate_limit_until == test_now + 60 && !result.waiting_only);
    requests_before = request_serial; fail_match = NULL;
    data = durable_publication; /* cold invocation reloads the persisted quota */
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(request_serial == requests_before && result.waiting_only && result.rate_limit_until == test_now + 60);
    test_now += 61; custom_headers = false; fixture_remaining = fixture_retry_after = fixture_reset = NULL;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);

    /* Headerless secondary-limit JSON uses the documented minimum one minute
     * and doubles the next rejected attempt. Ordinary 403 bodies do not. */
    test_now += config.interval_sec; fail_match = "/events/public?"; fail_status = 403;
    custom_headers = true; fixture_message = "You have exceeded a secondary rate limit. Please wait.";
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(result.rate_limit_until == test_now + 60 && strstr(result.detail, "SECONDARY"));
    test_now += 61;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(result.rate_limit_until == test_now + 120);
    fail_match = NULL; fixture_message = NULL; custom_headers = false; test_now += 121;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);

    /* A manual retry during a search quota can still repair an ordinary core
     * failure, with zero additional requests against the search bucket. */
    test_now += config.interval_sec; fail_match = "/search/issues?"; fail_status = 429;
    fail_match_second = "/events/public?"; fail_status_second = 500;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(result.limited_scopes == BADGE_SYNC_LIMIT_SEARCH);
    search_before = search_requests; events_before = events_requests; fail_match = fail_match_second = NULL;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(search_requests == search_before && events_requests == events_before + 1 && !(result.pending & BADGE_ACTIVITY));
    assert(!result.waiting_only && result.search_retry_after > test_now && !result.core_retry_after);
    requests_before = request_serial;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(request_serial == requests_before && result.waiting_only);
    test_now += 3606;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);

    /* A limit longer than the refresh interval survives both a reboot and
     * priority refresh. Only the independent search/avatar requests may run. */
    config.interval_sec = 3600; test_now += 3600;
    fail_match = "/repos/tester/b/languages"; fail_status = 403; custom_headers = true;
    fixture_remaining = "0"; snprintf(reset_header, sizeof(reset_header), "%lld", (long long)test_now + 7200); fixture_reset = reset_header;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    int64_t long_limit = result.core_retry_after; assert(long_limit == test_now + 7205);
    rewrite_job_fixture(config.username, -1);
    fail_match = NULL; custom_headers = false; fixture_remaining = fixture_reset = NULL;
    test_now += 3601; profiles_before = profiles; a_before = lang_a;
    data = durable_publication;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(profiles == profiles_before && lang_a == a_before && result.core_retry_after == long_limit);
    assert(strstr(result.detail, "RATE LIMIT")); /* Expanded PROFILE reason is not empty. */
    requests_before = request_serial;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(request_serial == requests_before && result.waiting_only);
    test_now = (time_t)long_limit + 1;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);
    config.interval_sec = BADGE_DEFAULT_INTERVAL;

    /* The last successful HTTP response can consume the final core request.
     * Persist that quota even though the collection itself finishes, then
     * start a new manual cycle without bypassing the server deadline. */
    test_now += config.interval_sec; fail_match = "https://api.github.com/repos/tester/a";
    fail_exact = true; fail_status = 200; custom_headers = true; fixture_remaining = "0";
    snprintf(reset_header, sizeof(reset_header), "%lld", (long long)test_now + 3600); fixture_reset = reset_header;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);
    assert(result.limited_scopes == BADGE_SYNC_LIMIT_CORE && !result.rate_limit_until);
    profiles_before = profiles; a_before = lang_a;
    fail_match = NULL; fail_exact = false; custom_headers = false; fixture_remaining = fixture_reset = NULL;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(profiles == profiles_before && lang_a == a_before && result.core_retry_after > test_now);
    test_now += 3606;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);

    /* Resource header disagreement cannot let manual clear the rejected URL's
     * deadline. Protect both the explicit core bucket and this search endpoint. */
    test_now += config.interval_sec; fail_match = "/search/issues?"; fail_status = 403;
    custom_headers = true; fixture_remaining = "0"; fixture_resource = "core"; fixture_retry_after = "60";
    snprintf(reset_header, sizeof(reset_header), "%lld", (long long)test_now + 3600); fixture_reset = reset_header;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(result.limited_scopes == (BADGE_SYNC_LIMIT_CORE | BADGE_SYNC_LIMIT_SEARCH));
    assert(result.core_retry_after == test_now + 60 && result.search_retry_after == test_now + 60);
    fail_match = NULL; requests_before = request_serial;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(request_serial == requests_before && result.waiting_only && strstr(result.detail, "RATE LIMIT"));
    fixture_remaining = fixture_resource = fixture_retry_after = fixture_reset = NULL; custom_headers = false;
    test_now += 61;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);

    /* A huge malformed epoch must not overflow the snapshot's time contract.
     * Zero remaining still gets a conservative normal primary backoff. */
    test_now += config.interval_sec; fail_match = "/events/public?"; fail_status = 403;
    custom_headers = true; fixture_remaining = "0";
    fixture_reset = fixture_retry_after = "9223372036854775806";
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(result.core_retry_after == test_now + 3600 && !result.cache_error && badge_data_valid(&data));
    fail_match = NULL; fixture_remaining = fixture_reset = fixture_retry_after = NULL; custom_headers = false;
    test_now += 3601;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);

    test_now += config.interval_sec; fail_match = "/events/public?"; fail_status = 404;
    assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_FAIL);
    fail_match = NULL;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);

    /* An acknowledged avatar may be missing/corrupt after interrupted storage
     * or recovery. Repair its stage without repeating the retained endpoints. */
    char avatar_path[200]; badge_cache_path(config.username, "avatar", avatar_path, sizeof(avatar_path));
    for (unsigned damage = 0; damage < 4; ++damage) {
        test_now += config.interval_sec; fail_match = "/events/public?"; fail_status = 500;
        assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_FAIL);
        assert(result.pending == BADGE_ACTIVITY && (data.valid & BADGE_AVATAR));
        if (damage == 0) assert(unlink(avatar_path) == 0);
        else if (damage == 1) assert(badge_file_write(avatar_path, "invalid avatar header with sufficient bytes", 42));
        else if (damage == 2) {
            FILE *oversized = fopen(avatar_path, "wb"); assert(oversized);
            assert(fseek(oversized, 24 * 1024, SEEK_SET) == 0 && fputc(0, oversized) == 0);
            assert(fclose(oversized) == 0);
        } else {
            /* A missing snapshot bit also invalidates only the avatar stage. */
            data.valid &= ~BADGE_AVATAR; data.complete &= ~BADGE_AVATAR;
        }
        fail_match = NULL;
        profiles_before = profiles; a_before = lang_a; search_before = search_requests;
        avatar_before = avatar_requests; events_before = events_requests; requests_before = request_serial;
        assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);
        assert(avatar_requests == avatar_before + 1 && events_requests == events_before + 1);
        assert(request_serial == requests_before + 2 && profiles == profiles_before);
        assert(lang_a == a_before && search_requests == search_before);
        assert(!result.failed && !result.pending && (data.valid & BADGE_AVATAR) && (data.complete & BADGE_AVATAR));
        check_complete(&data);
    }

    /* Repairing a missing cached file never bypasses the avatar service's
     * Retry-After. Other unfinished sections may finish while it waits. */
    test_now += config.interval_sec; fail_match = "avatars.githubusercontent.com"; fail_status = 200;
    custom_headers = true; fixture_remaining = "0";
    snprintf(reset_header, sizeof(reset_header), "%lld", (long long)test_now + 60); fixture_reset = reset_header;
    fail_match_second = "/events/public?"; fail_status_second = 500;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(result.pending == BADGE_ACTIVITY && result.limited_scopes == BADGE_SYNC_LIMIT_AVATAR);
    int64_t avatar_limit = result.avatar_retry_after;
    assert(avatar_limit > test_now && unlink(avatar_path) == 0);
    fail_match = fail_match_second = NULL; custom_headers = false; fixture_remaining = fixture_reset = NULL;
    profiles_before = profiles; a_before = lang_a; search_before = search_requests;
    avatar_before = avatar_requests; events_before = events_requests; requests_before = request_serial;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(avatar_requests == avatar_before && events_requests == events_before + 1);
    assert(request_serial == requests_before + 1 && profiles == profiles_before && lang_a == a_before);
    assert(search_requests == search_before && result.pending == BADGE_AVATAR);
    assert(!(data.valid & BADGE_AVATAR) && !(data.complete & BADGE_AVATAR));
    assert(result.avatar_retry_after == avatar_limit && result.rate_limit_until == avatar_limit);
    requests_before = request_serial;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(request_serial == requests_before && result.waiting_only && result.pending == BADGE_AVATAR);
    test_now = (time_t)avatar_limit + 1;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);
    assert(request_serial == requests_before + 1 && avatar_requests == avatar_before + 1);
    assert(profiles == profiles_before && lang_a == a_before && search_requests == search_before);
    check_complete(&data);

    /* Duplicate IDs cannot double-count language bytes. A server repeating a
     * full page stops at the second page, retaining the durable first page. */
    test_now += config.interval_sec; badge_data_reset(&data, config.username);
    repeat_repo_page = true; repo_page_requests = 0; a_before = lang_a;
    unsigned b_before = lang_b;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_FAIL);
    assert(repo_page_requests == 2 && lang_a == a_before + 1 && lang_b == b_before + 1);
    assert(data.repos_fetched == 2 && data.scopes[1].total_bytes == 1450);
    assert((result.pending & (BADGE_REPOSITORIES | BADGE_LANGUAGES)) == (BADGE_REPOSITORIES | BADGE_LANGUAGES));
    repeat_repo_page = false; profiles_before = profiles;
    assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);
    assert(repo_page_requests == 3 && profiles == profiles_before);
    assert(lang_a == a_before + 1 && lang_b == b_before + 1);
    check_complete(&data);

    /* CRC-valid caches still require bounded strings, booleans and cursors.
     * Reject malformed jobs and restart safely from the last-good snapshot. */
    for (int damage = 0; damage < 10; ++damage) {
        test_now += config.interval_sec; fail_match = "/events/public?"; fail_status = 500;
        assert(badge_sync_run_ex(&config, &data, publish, NULL, &result) == ESP_FAIL);
        assert(result.pending == BADGE_ACTIVITY);
        rewrite_job_fixture(config.username, damage);
        fail_match = NULL; profiles_before = profiles;
        assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_OK);
        assert(profiles == profiles_before + 1 && !result.cache_error);
        check_complete(&data);
    }

    /* A broken clock must not fabricate invalid snapshot timestamps, charge
     * API quota, or misreport a timestamp rejection as a storage failure. */
    time_t correct_now = test_now;
    const int64_t invalid_times[] = {INT64_MIN, -1, 0, 253402300799LL, INT64_MAX};
    int64_t correct_attempt = data.last_attempt, correct_success = data.last_success;
    for (unsigned i = 0; i < sizeof(invalid_times) / sizeof(invalid_times[0]); ++i) {
        test_now = (time_t)invalid_times[i]; requests_before = request_serial;
        unsigned published_before = publications;
        assert(badge_sync_run_request(&config, &data, publish, NULL, true, &result) == ESP_ERR_INVALID_STATE);
        assert(request_serial == requests_before && publications == published_before);
        assert(!result.cache_error && strstr(result.detail, "NETWORK TIME"));
        assert(data.last_attempt == correct_attempt && data.last_success == correct_success && badge_data_valid(&data));
    }
    test_now = correct_now;

    char events_path[200]; badge_cache_path(config.username, "events", events_path, sizeof(events_path));
    assert(badge_sync_events_valid(events_path));
    FILE *journal = fopen(events_path, "rb"); assert(journal);
    assert(fseek(journal, 0, SEEK_END) == 0); long journal_size = ftell(journal);
    assert(journal_size > 12 && fseek(journal, 0, SEEK_SET) == 0);
    unsigned char *journal_bytes = malloc((size_t)journal_size); assert(journal_bytes);
    assert(fread(journal_bytes, 1, (size_t)journal_size, journal) == (size_t)journal_size && fclose(journal) == 0);
    /* A correct checksum does not excuse out-of-bounds ring metadata. */
    uint32_t impossible_count = 601; memcpy(journal_bytes + sizeof(uint32_t), &impossible_count, sizeof(impossible_count));
    uint32_t journal_crc = 2166136261U;
    for (size_t i = 0; i < (size_t)journal_size - sizeof(uint32_t); ++i) journal_crc = (journal_crc ^ journal_bytes[i]) * 16777619U;
    memcpy(journal_bytes + journal_size - sizeof(uint32_t), &journal_crc, sizeof(journal_crc));
    assert(badge_file_write(events_path, journal_bytes, (size_t)journal_size)); free(journal_bytes);
    assert(!badge_sync_events_valid(events_path));
    FILE *bad_journal = fopen(events_path, "r+b"); assert(bad_journal);
    assert(fputc(0, bad_journal) != EOF && fclose(bad_journal) == 0);
    assert(!badge_sync_events_valid(events_path));
    const char *suffixes[] = {"job", "events", "avatar"};
    for (unsigned i = 0; i < 3; ++i) { char path[200]; badge_cache_path(config.username, suffixes[i], path, sizeof(path)); unlink(path); }
    assert(rmdir(directory) == 0);
    puts("badge sync full cycle/quota resume/last-good cache integration tests passed");
    return 0;
}
