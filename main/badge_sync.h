#pragma once
#include "badge_model.h"
#include "esp_err.h"

/* Return true only after the snapshot is durably stored. The collection
 * cursor never advances beyond an unacknowledged snapshot. */
typedef bool (*badge_sync_publish_t)(const badge_data_t *data, void *context);
enum {
    BADGE_SYNC_LIMIT_CORE = 1U << 0,
    BADGE_SYNC_LIMIT_SEARCH = 1U << 1,
    BADGE_SYNC_LIMIT_AVATAR = 1U << 2
};
typedef struct {
    /* BADGE_* bits for this collection cycle, independent of older cached data. */
    uint32_t refreshed, failed, pending;
    bool cache_error, cancelled, rate_limited;
    int64_t retry_after;
    /* Active server limits, independent of ordinary automatic retry delays. */
    uint32_t limited_scopes;
    int64_t core_retry_after, search_retry_after, avatar_retry_after;
    int64_t rate_limit_until; /* Earliest active limit affecting unfinished work. */
    bool waiting_only; /* Zero HTTP requests and unfinished work still waiting. */
    char detail[64], avatar_error[64];
} badge_sync_result_t;
/* Blocking worker API; no LVGL calls. Updates a caller-owned copy of last good
 * data, retaining failed sections. Config is immutable for this invocation. */
esp_err_t badge_sync_run(const badge_config_t *config, badge_data_t *data,
                         badge_sync_publish_t publish, void *context);
esp_err_t badge_sync_run_ex(const badge_config_t *config, badge_data_t *data,
                            badge_sync_publish_t publish, void *context,
                            badge_sync_result_t *result);
/* Manual retries bypass ordinary failure backoff, never server rate limits. */
esp_err_t badge_sync_run_request(const badge_config_t *config, badge_data_t *data,
                                 badge_sync_publish_t publish, void *context,
                                 bool manual, badge_sync_result_t *result);
/* Safe from another task. The worker exits at the next bounded network step. */
void badge_sync_cancel(void);
/* Validate an exported events journal before import. The outer backup manifest
 * binds its account; this historical journal has no embedded username. */
bool badge_sync_events_valid(const char *path);
