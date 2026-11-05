#pragma once
#include "badge_ui.h"

typedef enum { BADGE_SETTINGS_CLOSED, BADGE_SETTINGS_MAIN,
               BADGE_SETTINGS_BRIGHTNESS, BADGE_SETTINGS_THEME,
               BADGE_SETTINGS_SCREEN_TIMEOUT } badge_settings_t;
enum { BADGE_SCREEN_TIMEOUT_COUNT = 6, BADGE_BRIGHTNESS_COUNT = 6 };
extern const uint8_t badge_screen_timeout_minutes[BADGE_SCREEN_TIMEOUT_COUNT];
extern const uint8_t badge_brightness_percent[BADGE_BRIGHTNESS_COUNT];
typedef enum { BADGE_MODAL_NONE, BADGE_MODAL_PROFILE, BADGE_MODAL_WIFI } badge_modal_t;
typedef struct {
    uint8_t page, focus, language, pr_tab, star_tab;
    badge_settings_t settings;
    badge_modal_t modal;
} badge_ui_state_t;

void badge_ui_state_clamp(badge_ui_state_t *state, const badge_data_t *data);
/* Credentials are usable only after the requested hotspot session is ready. */
bool badge_ui_provision_ready(const badge_status_t *status);
badge_action_t badge_ui_state_key(badge_ui_state_t *state, badge_key_t key,
                                 bool long_press, const badge_data_t *data,
                                 const badge_config_t *config);
