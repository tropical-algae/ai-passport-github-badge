#pragma once
#include "badge_model.h"

typedef enum { BADGE_KEY_UP, BADGE_KEY_DOWN, BADGE_KEY_OK } badge_key_t;
typedef enum {
    BADGE_ACTION_NONE, BADGE_ACTION_THEME, BADGE_ACTION_BRIGHTNESS,
    BADGE_ACTION_PROVISION, BADGE_ACTION_CLOSE_PROVISION, BADGE_ACTION_SYNC,
    BADGE_ACTION_SCREEN_TIMEOUT
} badge_action_kind_t;
typedef struct { badge_action_kind_t kind; uint8_t value; } badge_action_t;

/* All calls under bsp_lvgl_lock, from the application task. */
void badge_ui_init(const badge_data_t *data, const badge_config_t *config,
                   const badge_status_t *status);
void badge_ui_update(const badge_data_t *data, const badge_config_t *config,
                     const badge_status_t *status);
void badge_ui_tick(const badge_status_t *status);
badge_action_t badge_ui_key(badge_key_t key, bool long_press);
/* Config portal opened on first boot or by settings. */
void badge_ui_show_provision(void);
/* Close only the Wi-Fi dialog; the application controls AP/server lifetime. */
void badge_ui_close_provision(void);
/* Copy up to 127 UTF-8 bytes for a four-second toast. One newline separates
 * status and detail into two independently fitted lines. NULL/empty clears it. */
void badge_ui_notify(const char *message, bool error);
