#pragma once
#include "badge_model.h"
#include "esp_err.h"

typedef void (*badge_network_config_cb_t)(const badge_config_t *config, void *context);
/* Callback must only enqueue: the network worker calls it after a submitted
 * Wi-Fi configuration obtains an IP. Persist that successful configuration. */
esp_err_t badge_network_init(const badge_config_t *config,
                             badge_network_config_cb_t callback, void *context);
esp_err_t badge_network_apply(const badge_config_t *config);
/* Acknowledge the callback's NVS result. Success promotes the candidate and
 * closes only its original setup session after the phone acknowledges the
 * result (with a five-second fallback). Failure retains the saved settings. */
esp_err_t badge_network_config_result(bool saved);
/* Publish the latest desired AP state, independently of queue capacity. A new
 * false->true request creates a new session; repeated same-state calls are
 * idempotent. Stop preserves accepted validation, persistence and STA work. */
esp_err_t badge_network_provision_start(void);
esp_err_t badge_network_provision_stop(void);
/* busy remains true until actual radio/services match the requested session,
 * including failures awaiting retry. Credentials are ready only when both
 * nonzero session fields match and busy is false. setup_pending additionally
 * gates GitHub TLS while an accepted configuration is being validated/saved. */
void badge_network_status(badge_status_t *status);
