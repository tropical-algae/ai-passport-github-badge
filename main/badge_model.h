#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BADGE_SCHEMA 1
#define BADGE_REPOS 5
#define BADGE_SCOPES 7
#define BADGE_LANGS 6
#define BADGE_DAYS 168
#define BADGE_EVENTS 6
#define BADGE_DEFAULT_INTERVAL 21600U
#define BADGE_MIN_INTERVAL 3600U
#define BADGE_MAX_INTERVAL 604800U
#define BADGE_AVATAR_SIDE 54
#define BADGE_DEFAULT_SCREEN_TIMEOUT_MIN 10U
#define BADGE_LEGACY_CONFIG_SIZE 148U

typedef struct {
    char username[40], ssid[33], password[65];
    uint32_t interval_sec;
    int16_t utc_offset_min;
    uint8_t theme, brightness; /* theme 0..4; backlight 10, 20, 40, 60, 80, 100 percent */
    uint16_t screen_timeout_min; /* 0 = never; otherwise 1, 5, 10, 30, 60 */
} badge_config_t;

typedef enum {
    BADGE_PROFILE = 1U << 0, BADGE_REPOSITORIES = 1U << 1,
    BADGE_LANGUAGES = 1U << 2, BADGE_ACTIVITY = 1U << 3,
    BADGE_PULLS = 1U << 4, BADGE_ISSUES = 1U << 5,
    BADGE_STARS = 1U << 6, BADGE_GISTS = 1U << 7,
    BADGE_AVATAR = 1U << 8
} badge_section_t;

typedef struct {
    char name[101], full_name[142], license[32], pushed_at[11];
    uint32_t stars, forks, watchers;
    bool fork, watchers_valid;
} badge_repo_t;

typedef struct { char name[32]; uint64_t bytes; } badge_language_t;
typedef struct {
    char title[101];
    badge_language_t languages[BADGE_LANGS]; /* leading five + OTHER */
    uint64_t total_bytes;
    uint32_t fetched_repos, total_repos;
    uint8_t count;
    bool valid, complete;
} badge_scope_t;

typedef enum { BADGE_UNKNOWN, BADGE_PARTIAL, BADGE_KNOWN } badge_coverage_t;
typedef struct {
    int32_t day; /* Unix UTC day; exact observed events, never synthetic commits */
    uint16_t events, pushes;
    uint8_t coverage;
} badge_day_t;
typedef struct { char type[32], repo[142], date[11]; } badge_event_t;
typedef struct {
    char name[142], language[32];
    uint32_t stars;
} badge_star_t;
typedef struct { char description[128], updated_at[11]; } badge_gist_t;

typedef struct {
    uint32_t schema, valid, complete;
    char username[40], name[96], company[96], location[96], blog[160];
    char joined[11], avatar_url[240];
    uint32_t public_repos, followers, following, public_gists;
    badge_repo_t repos[BADGE_REPOS];
    uint32_t repos_fetched;
    uint8_t repo_count;
    badge_scope_t scopes[BADGE_SCOPES];
    uint8_t scope_count;
    badge_day_t days[BADGE_DAYS];
    badge_event_t events[BADGE_EVENTS];
    uint8_t event_count;
    uint32_t events_fetched, push_events, push_days;
    uint32_t pr_total, pr_open, pr_merged, pr_closed;
    uint32_t issue_total, issue_open, issue_closed;
    badge_star_t stars[2];
    uint32_t starred_fetched, starred_with_language, starred_languages;
    uint8_t star_count;
    badge_gist_t gists[2];
    uint32_t gists_fetched, gists_public;
    uint8_t gist_count;
    int64_t last_attempt, last_success, retry_after;
    char sync_message[64];
} badge_data_t;

typedef struct {
    bool connected, provisioning, syncing, setup_saved;
    bool provisioning_busy; /* Queued/in-flight hotspot start or stop. */
    int battery_percent;
    int64_t now;
    char ap_ssid[33], ap_password[20], message[64];
} badge_status_t;

void badge_config_defaults(badge_config_t *config);
bool badge_username_valid(const char *name);
bool badge_config_valid(const badge_config_t *config);
bool badge_screen_timeout_valid(uint16_t minutes);
/* Decode current or legacy fixed-size config; legacy archives default to 10 min. */
bool badge_config_decode(const void *bytes, size_t size, badge_config_t *config);
bool badge_data_valid(const badge_data_t *data);
void badge_data_reset(badge_data_t *data, const char *username);
bool badge_sync_due(const badge_data_t *data, const badge_config_t *config, int64_t now);
void badge_history_roll(badge_data_t *data, int32_t today);
void badge_format_number(uint32_t value, char *out, size_t size);

void badge_format_wait(int64_t deadline, int64_t now, char *out, size_t size);
