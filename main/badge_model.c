#include "badge_model.h"
#include <stdio.h>
#include <string.h>

void badge_config_defaults(badge_config_t *config) {
    memset(config, 0, sizeof(*config));
    config->interval_sec = BADGE_DEFAULT_INTERVAL;
    config->brightness = 100;
    config->screen_timeout_min = BADGE_DEFAULT_SCREEN_TIMEOUT_MIN;
}

bool badge_username_valid(const char *name) {
    if (!name) return false;
    size_t n = 0;
    for (; n < 40 && name[n]; ++n) {
        unsigned char c = (unsigned char)name[n];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || (c == '-' && n && name[n - 1] != '-')))
            return false;
    }
    return n > 0 && n <= 39 && name[n - 1] != '-';
}

bool badge_screen_timeout_valid(uint16_t minutes) {
    return minutes == 0 || minutes == 1 || minutes == 5 || minutes == 10 || minutes == 30 || minutes == 60;
}

bool badge_config_valid(const badge_config_t *config) {
    return config && memchr(config->username, 0, sizeof(config->username)) &&
        memchr(config->ssid, 0, sizeof(config->ssid)) &&
        memchr(config->password, 0, sizeof(config->password)) &&
        badge_username_valid(config->username) && config->ssid[0] &&
        config->interval_sec >= BADGE_MIN_INTERVAL &&
        config->interval_sec <= BADGE_MAX_INTERVAL && config->theme < 5 &&
        (config->brightness == 10 || (config->brightness >= 20 && config->brightness <= 100 &&
        config->brightness % 20 == 0)) && badge_screen_timeout_valid(config->screen_timeout_min) &&
        config->utc_offset_min >= -840 && config->utc_offset_min <= 840;
}

/* The legacy prefix is frozen: NVS and .ghbk archives through v0.2.1 store
 * these exact 148 bytes. Never reinterpret a future layout using this path. */
_Static_assert(offsetof(badge_config_t, screen_timeout_min) == BADGE_LEGACY_CONFIG_SIZE,
               "Legacy config prefix changed");
_Static_assert(sizeof(badge_config_t) == 152, "Current config ABI changed");
bool badge_config_decode(const void *bytes, size_t size, badge_config_t *config) {
    if (!bytes || !config || (size != BADGE_LEGACY_CONFIG_SIZE && size != sizeof(*config))) return false;
    badge_config_t decoded;
    badge_config_defaults(&decoded);
    memcpy(&decoded, bytes, size);
    if (!badge_config_valid(&decoded)) return false;
    *config = decoded;
    return true;
}

/* Backups are untrusted input. Check every array bound and string before any
 * renderer or synchronizer can consume a restored model. */
bool badge_data_valid(const badge_data_t *d) {
    if (!d || d->schema != BADGE_SCHEMA || (d->valid & ~511U) ||
        (d->complete & ~d->valid) || d->repo_count > BADGE_REPOS ||
        d->scope_count > BADGE_SCOPES || d->event_count > BADGE_EVENTS ||
        d->star_count > 2 || d->gist_count > 2) return false;
#define BOOL_VALID(field) do { unsigned char raw; memcpy(&raw, &(field), 1); if (raw > 1) return false; } while (0)
#define TERMINATED(field) do { if (!memchr((field), 0, sizeof(field))) return false; } while (0)
    TERMINATED(d->username);
    if (!badge_username_valid(d->username)) return false;
    TERMINATED(d->name); TERMINATED(d->company); TERMINATED(d->location);
    TERMINATED(d->blog); TERMINATED(d->joined); TERMINATED(d->avatar_url);
    TERMINATED(d->sync_message);
    for (size_t i = 0; i < BADGE_REPOS; ++i) {
        BOOL_VALID(d->repos[i].fork); BOOL_VALID(d->repos[i].watchers_valid);
        TERMINATED(d->repos[i].name); TERMINATED(d->repos[i].full_name);
        TERMINATED(d->repos[i].license); TERMINATED(d->repos[i].pushed_at);
    }
    for (size_t i = 0; i < BADGE_SCOPES; ++i) {
        BOOL_VALID(d->scopes[i].valid); BOOL_VALID(d->scopes[i].complete);
        if (d->scopes[i].count > BADGE_LANGS) return false;
        TERMINATED(d->scopes[i].title);
        uint64_t sum = 0;
        if (d->scopes[i].total_bytes > UINT64_MAX / 1000U) return false;
        for (size_t j = 0; j < BADGE_LANGS; ++j) {
            TERMINATED(d->scopes[i].languages[j].name);
            if (j < d->scopes[i].count) {
                uint64_t bytes = d->scopes[i].languages[j].bytes;
                if (bytes > d->scopes[i].total_bytes - sum) return false;
                sum += bytes;
            }
        }
    }
    for (size_t i = 0; i < BADGE_DAYS; ++i)
        if (d->days[i].coverage > BADGE_KNOWN || d->days[i].pushes > d->days[i].events ||
            d->days[i].day < 0 || d->days[i].day > 3652500) return false;
    for (size_t i = 0; i < BADGE_EVENTS; ++i) {
        TERMINATED(d->events[i].type); TERMINATED(d->events[i].repo); TERMINATED(d->events[i].date);
    }
    for (size_t i = 0; i < 2; ++i) {
        TERMINATED(d->stars[i].name); TERMINATED(d->stars[i].language);
        TERMINATED(d->gists[i].description); TERMINATED(d->gists[i].updated_at);
    }
#undef BOOL_VALID
#undef TERMINATED
    const int64_t max_time = 253402300799LL; /* End of year 9999. */
    return d->last_attempt >= 0 && d->last_success >= 0 && d->retry_after >= 0 &&
        d->last_attempt <= max_time && d->last_success <= max_time && d->retry_after <= max_time;
}

void badge_data_reset(badge_data_t *data, const char *username) {
    memset(data, 0, sizeof(*data));
    data->schema = BADGE_SCHEMA;
    if (username) snprintf(data->username, sizeof(data->username), "%s", username);
    snprintf(data->sync_message, sizeof(data->sync_message), "WAITING FOR SYNC");
}

bool badge_sync_due(const badge_data_t *data, const badge_config_t *config, int64_t now) {
    if (!badge_config_valid(config) || now < 1577836800) return false;
    if (data->retry_after > 0) return now >= data->retry_after;
    if (data->last_success == 0 || data->last_success > now) return true;
    return now - data->last_success >= config->interval_sec;
}

void badge_history_roll(badge_data_t *data, int32_t today) {
    int32_t weekday = (today + 4) % 7;
    if (weekday < 0) weekday += 7;
    int32_t start = today - weekday - 23 * 7;
    badge_day_t old[BADGE_DAYS];
    memcpy(old, data->days, sizeof(old));
    memset(data->days, 0, sizeof(data->days));
    for (int i = 0; i < BADGE_DAYS; ++i) {
        data->days[i].day = start + i;
        for (int j = 0; j < BADGE_DAYS; ++j) {
            if (old[j].day == start + i) {
                data->days[i] = old[j];
                break;
            }
        }
    }
}

void badge_format_number(uint32_t value, char *out, size_t size) {
    if (value >= 1000000U)
        snprintf(out, size, "%lu.%luM", (unsigned long)(value / 1000000U),
                 (unsigned long)((value % 1000000U) / 100000U));
    else if (value >= 10000U)
        snprintf(out, size, "%lu.%luK", (unsigned long)(value / 1000U),
                 (unsigned long)((value % 1000U) / 100U));
    else if (value >= 1000U)
        snprintf(out, size, "%lu,%03lu", (unsigned long)(value / 1000U),
                 (unsigned long)(value % 1000U));
    else snprintf(out, size, "%lu", (unsigned long)value);
}

void badge_format_wait(int64_t deadline, int64_t now, char *out, size_t size) {
    if (!out || !size) return;
    if (deadline <= now) { snprintf(out, size, "0S"); return; }
    /* Unsigned subtraction also handles extreme imported/synthetic epochs. */
    uint64_t seconds = (uint64_t)deadline - (uint64_t)now;
    if (seconds >= 1000ULL * 86400) snprintf(out, size, "999D+");
    else if (seconds >= 86400) snprintf(out, size, "%luD %02luH",
        (unsigned long)(seconds / 86400), (unsigned long)(seconds % 86400 / 3600));
    else if (seconds >= 3600) snprintf(out, size, "%luH %02luM",
        (unsigned long)(seconds / 3600), (unsigned long)(seconds % 3600 / 60));
    else if (seconds >= 60) snprintf(out, size, "%luM %02luS",
        (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
    else snprintf(out, size, "%luS", (unsigned long)seconds);
}
