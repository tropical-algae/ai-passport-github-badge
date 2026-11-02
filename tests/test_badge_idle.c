#include "badge_idle.h"
#include "badge_input.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const uint16_t choices[] = {1, 5, 10, 30, 60};
    badge_idle_t idle;
    for (unsigned i = 0; i < sizeof(choices) / sizeof(choices[0]); ++i) {
        badge_idle_init(&idle, 100);
        int64_t deadline = 100 + (int64_t)choices[i] * 60000000;
        assert(!badge_idle_tick(&idle, choices[i], deadline - 1));
        assert(badge_idle_tick(&idle, choices[i], deadline));
        assert(idle.asleep && !badge_idle_tick(&idle, choices[i], deadline + 1));
    }
    badge_idle_init(&idle, 0);
    assert(!badge_idle_tick(&idle, 0, INT64_MAX) && !idle.asleep);
    assert(!badge_idle_tick(&idle, 1, -1));
    assert(!badge_idle_key(&idle, 0, true, 59999999));
    assert(!badge_idle_tick(&idle, 1, 120000000)); /* held keys prevent sleep */
    assert(!badge_idle_key(&idle, 0, false, 120000000));
    assert(!badge_idle_tick(&idle, 1, 179999999));
    assert(badge_idle_tick(&idle, 1, 180000000));

    /* A long OK wake must not open settings. Releasing the wake gesture is
     * consumed, including overlapping keys; the next ordinary tap works. */
    badge_input_t input;
    badge_input_init(&input);
    assert(badge_idle_key(&idle, 2, true, 180000001) && !idle.asleep);
    assert(!badge_input_poll(&input, 181000000).ready);
    assert(badge_idle_key(&idle, 0, true, 181000001));
    assert(badge_idle_key(&idle, 2, false, 181000002));
    assert(badge_idle_key(&idle, 0, false, 181000003));
    assert(!idle.consume_wake && !idle.held_keys);
    assert(!badge_idle_key(&idle, 2, true, 182000000));
    badge_input_edge(&input, 2, true, 182000000);
    assert(!badge_idle_key(&idle, 2, false, 182100000));
    badge_input_action_t action = badge_input_edge(&input, 2, false, 182100000);
    assert(action.ready && !action.long_press);
    assert(!badge_idle_tick(&idle, 1, 242099999));
    assert(badge_idle_tick(&idle, 1, 242100000));

    /* A lone stale release cannot wake the display. Overflow discards held
     * state without changing brightness or waking the screen. */
    assert(badge_idle_key(&idle, 1, false, 242200000) && idle.asleep);
    badge_idle_cancel_keys(&idle, 243000000);
    assert(idle.asleep && !idle.held_keys && !idle.consume_wake);
    assert(badge_idle_key(&idle, 1, true, 244000000));
    badge_idle_cancel_keys(&idle, 245000000);
    assert(!idle.asleep && !idle.held_keys);
    assert(badge_idle_tick(&idle, 1, 305000000));
    assert(badge_idle_key(&idle, -1, true, 306000000) && idle.asleep);
    /* Clock ordering is explicit, and even boundary timestamps cannot overflow
     * the input hold duration or the display inactivity calculation. */
    badge_input_init(&input);
    badge_input_edge(&input, 2, true, INT64_MAX);
    assert(!badge_input_poll(&input, INT64_MIN).ready);
    assert(!badge_input_edge(&input, 2, false, INT64_MIN).ready);
    badge_input_edge(&input, 2, true, INT64_MIN);
    action=badge_input_poll(&input, INT64_MAX);
    assert(action.ready && action.long_press && action.key==2);
    assert(!badge_input_edge(&input, 2, false, INT64_MAX).ready);
    badge_input_edge(&input, 0, true, INT64_MIN);
    action=badge_input_edge(&input, 0, false, INT64_MAX);
    assert(action.ready && action.long_press && action.key==0);
    badge_input_edge(&input, 1, true, 100);
    badge_input_edge(&input, 1, true, 500000); /* Duplicate edges do not reset hold. */
    assert(badge_input_poll(&input, BADGE_HOLD_US+100).ready);
    assert(!badge_input_edge(&input, -1, false, INT64_MAX).ready);
    badge_idle_init(&idle, INT64_MIN);
    assert(badge_idle_tick(&idle, 60, INT64_MAX));
    puts("badge idle: PASS (six intervals, inactivity, wake gesture, overflow)");
    return 0;
}
