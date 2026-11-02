#pragma once
#include <stdbool.h>
#include <stdint.h>
#define BADGE_HOLD_US 600000LL
typedef struct { int key; int64_t pressed_at; bool fired; } badge_input_t;
typedef struct { int key; bool long_press, ready; } badge_input_action_t;
void badge_input_init(badge_input_t *input);
badge_input_action_t badge_input_edge(badge_input_t *input, int key, bool pressed, int64_t at);
badge_input_action_t badge_input_poll(badge_input_t *input, int64_t now);
