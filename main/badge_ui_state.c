#include "badge_ui_state.h"

const uint8_t badge_screen_timeout_minutes[BADGE_SCREEN_TIMEOUT_COUNT] = {1, 5, 10, 30, 60, 0};
const uint8_t badge_brightness_percent[BADGE_BRIGHTNESS_COUNT] = {100, 80, 60, 40, 20, 10};

bool badge_ui_provision_ready(const badge_status_t *s)
{
    return s && s->provisioning && !s->provisioning_busy && s->provisioning_session &&
           s->provisioning_session == s->provisioning_ready_session &&
           s->ap_ssid[0] && s->ap_password[0];
}

static uint8_t wrapped(uint8_t value, int delta, unsigned count)
{
    return count ? (uint8_t)(((int)value + delta + (int)count) % (int)count) : 0;
}

static unsigned focus_count(const badge_ui_state_t *s, const badge_data_t *d)
{
    if (s->settings == BADGE_SETTINGS_MAIN || s->settings == BADGE_SETTINGS_THEME) return 5;
    if (s->settings != BADGE_SETTINGS_CLOSED) return 6;
    if (s->page == 1) return d->repo_count < 3 ? d->repo_count : 3;
    if (s->page == 4 || s->page == 5) return 2;
    return 1;
}

void badge_ui_state_clamp(badge_ui_state_t *s, const badge_data_t *d)
{
    if (s->page >= 7) s->page = 0;
    if ((unsigned)s->settings > BADGE_SETTINGS_SCREEN_TIMEOUT)
        s->settings = BADGE_SETTINGS_CLOSED;
    if ((unsigned)s->modal > BADGE_MODAL_WIFI) s->modal = BADGE_MODAL_NONE;
    if (s->pr_tab > 1) s->pr_tab = 0;
    if (s->star_tab > 1) s->star_tab = 0;
    unsigned count = focus_count(s, d);
    if (s->focus >= count) s->focus = count ? (uint8_t)(count - 1) : 0;
    if (s->language >= d->scope_count || s->language >= BADGE_SCOPES) s->language = 0;
}

badge_action_t badge_ui_state_key(badge_ui_state_t *s, badge_key_t key,
                                 bool long_press, const badge_data_t *d,
                                 const badge_config_t *config)
{
    badge_action_t action = {BADGE_ACTION_NONE, 0};
    badge_ui_state_clamp(s, d);
    if ((unsigned)key > BADGE_KEY_OK) return action;
    if (s->modal != BADGE_MODAL_NONE) {
        if (key == BADGE_KEY_OK) {
            if (s->modal == BADGE_MODAL_WIFI) action.kind = BADGE_ACTION_CLOSE_PROVISION;
            s->modal = BADGE_MODAL_NONE;
        }
        return action;
    }
    if (long_press) {
        if (key == BADGE_KEY_OK) {
            s->settings = s->settings == BADGE_SETTINGS_CLOSED ? BADGE_SETTINGS_MAIN : BADGE_SETTINGS_CLOSED;
            s->focus = 0;
        } else if (s->settings == BADGE_SETTINGS_CLOSED) {
            s->page = wrapped(s->page, key == BADGE_KEY_UP ? -1 : 1, 7);
            s->focus = 0;
        }
        return action;
    }
    if (key != BADGE_KEY_OK) {
        int delta = key == BADGE_KEY_UP ? -1 : 1;
        if (s->settings == BADGE_SETTINGS_CLOSED && s->page == 2)
            s->language = wrapped(s->language, delta, d->scope_count < BADGE_SCOPES ? d->scope_count : BADGE_SCOPES);
        else s->focus = wrapped(s->focus, delta, focus_count(s, d));
        return action;
    }
    if (s->settings == BADGE_SETTINGS_MAIN) {
        switch (s->focus) {
        case 0:
            s->settings = BADGE_SETTINGS_BRIGHTNESS;
            s->focus = 0;
            for (unsigned i = 0; i < BADGE_BRIGHTNESS_COUNT; ++i)
                if (config->brightness == badge_brightness_percent[i]) s->focus = (uint8_t)i;
            break;
        case 1:
            s->settings = BADGE_SETTINGS_SCREEN_TIMEOUT;
            s->focus = 2; /* Default to ten minutes if an invalid value reaches the UI. */
            for (unsigned i = 0; i < BADGE_SCREEN_TIMEOUT_COUNT; ++i)
                if (config->screen_timeout_min == badge_screen_timeout_minutes[i]) s->focus = (uint8_t)i;
            break;
        case 2: s->settings = BADGE_SETTINGS_THEME; s->focus = config->theme < 5 ? config->theme : 0; break;
        case 3: s->modal = BADGE_MODAL_WIFI; action.kind = BADGE_ACTION_PROVISION; break;
        case 4: action.kind = BADGE_ACTION_SYNC; break;
        default: break;
        }
    } else if (s->settings == BADGE_SETTINGS_SCREEN_TIMEOUT) {
        action.kind = BADGE_ACTION_SCREEN_TIMEOUT;
        action.value = badge_screen_timeout_minutes[s->focus];
        s->settings = BADGE_SETTINGS_MAIN;
        s->focus = 1;
    } else if (s->settings == BADGE_SETTINGS_BRIGHTNESS) {
        action.kind = BADGE_ACTION_BRIGHTNESS;
        action.value = badge_brightness_percent[s->focus];
        s->settings = BADGE_SETTINGS_MAIN;
        s->focus = 0;
    } else if (s->settings == BADGE_SETTINGS_THEME) {
        action.kind = BADGE_ACTION_THEME;
        action.value = s->focus;
        s->settings = BADGE_SETTINGS_MAIN;
        s->focus = 2;
    } else if (s->page == 0 && badge_username_valid(config->username)) {
        s->modal = BADGE_MODAL_PROFILE;
    } else if (s->page == 4) s->pr_tab = s->focus;
    else if (s->page == 5) s->star_tab = s->focus;
    return action;
}
