#include "badge_idle.h"

void badge_idle_init(badge_idle_t *idle, int64_t now) {
    *idle = (badge_idle_t){.last_activity = now};
}

bool badge_idle_key(badge_idle_t *idle, int key, bool pressed, int64_t at) {
    if (key < 0 || key > 2) return true;
    if (at > idle->last_activity) idle->last_activity = at;
    uint8_t bit = (uint8_t)(1U << key);
    if (pressed) {
        idle->held_keys |= bit;
        if (idle->asleep) {
            idle->asleep = false;
            idle->consume_wake = true;
        }
    } else idle->held_keys &= (uint8_t)~bit;
    bool consume = idle->asleep || idle->consume_wake;
    if (!idle->held_keys) idle->consume_wake = false;
    return consume;
}

bool badge_idle_tick(badge_idle_t *idle, uint16_t timeout_min, int64_t now) {
    if (idle->asleep || !timeout_min || idle->held_keys || now < idle->last_activity)
        return false;
    uint64_t elapsed = (uint64_t)now - (uint64_t)idle->last_activity;
    if (elapsed < (uint64_t)timeout_min * 60000000U) return false;
    idle->asleep = true;
    return true;
}

void badge_idle_cancel_keys(badge_idle_t *idle, int64_t now) {
    idle->held_keys = 0;
    idle->consume_wake = false;
    idle->last_activity = now;
}
