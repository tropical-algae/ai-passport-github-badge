#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Display-only inactivity; the application and network tasks remain running. */
typedef struct {
    int64_t last_activity;
    uint8_t held_keys;
    bool asleep, consume_wake;
} badge_idle_t;

void badge_idle_init(badge_idle_t *idle, int64_t now);
/* True consumes the complete wake gesture, including its release/hold. */
bool badge_idle_key(badge_idle_t *idle, int key, bool pressed, int64_t at);
/* True only on the transition to screen off. Zero minutes means never. */
bool badge_idle_tick(badge_idle_t *idle, uint16_t timeout_min, int64_t now);
/* Discard incomplete gestures after a queue overflow or maintenance window. */
void badge_idle_cancel_keys(badge_idle_t *idle, int64_t now);
