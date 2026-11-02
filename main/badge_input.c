#include "badge_input.h"
static bool held_long_enough(int64_t start, int64_t end) {
    return end >= start && (uint64_t)end - (uint64_t)start >= BADGE_HOLD_US;
}
void badge_input_init(badge_input_t *input) {
    input->key = -1;
    input->pressed_at = 0;
    input->fired = false;
}
badge_input_action_t badge_input_edge(badge_input_t *input, int key, bool pressed, int64_t at) {
    badge_input_action_t action = {0};
    if (key < 0 || key > 2) return action;
    if (pressed) {
        if (input->key == -1) {
            input->key = key;
            input->pressed_at = at;
            input->fired = false;
        }
    } else if (input->key == key) {
        if (!input->fired && at >= input->pressed_at)
            action = (badge_input_action_t){key, held_long_enough(input->pressed_at, at), true};
        badge_input_init(input);
    }
    return action;
}
badge_input_action_t badge_input_poll(badge_input_t *input, int64_t now) {
    badge_input_action_t action = {0};
    if (input->key >= 0 && input->key <= 2 && !input->fired && held_long_enough(input->pressed_at, now)) {
        action = (badge_input_action_t){input->key, true, true};
        input->fired = true;
    }
    return action;
}
