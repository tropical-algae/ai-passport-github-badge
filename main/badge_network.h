#pragma once
#include "badge_model.h"
#include "esp_err.h"

typedef void (*badge_network_config_cb_t)(const badge_config_t *config, void *context);
/* Callback must only enqueue: the network worker calls it after a submitted
 * Wi-Fi configuration obtains an IP. Persist that successful configuration. */
esp_err_t badge_network_init(const badge_config_t *config,
                             badge_network_config_cb_t callback, void *context);
esp_err_t badge_network_apply(const badge_config_t *config);
/* Acknowledge the callback's NVS result; only a successful acknowledgement
 * closes the provisioning hotspot after its 60-second grace period. */
esp_err_t badge_network_config_result(bool saved);
esp_err_t badge_network_provision_start(void);
esp_err_t badge_network_provision_stop(void);
/* provisioning_busy remains true until queued hotspot transitions finish,
 * including when a START has not yet made provisioning true. */
void badge_network_status(badge_status_t *status);
