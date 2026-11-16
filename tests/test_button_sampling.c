/* Included after the pinned component's verbatim debounce/state machine and
 * the BSP's real edge-only callback registration/dispatch helpers. */
#include "badge_input.h"
#include "badge_idle.h"
#include <assert.h>

static int64_t now_ms, pressed_from, pressed_until, down_at, up_at;
static unsigned reads, downs, ups, short_actions, long_actions;
static badge_input_t input;
static badge_idle_t idle;
static button_dev_t button;
static button_cb_info_t registry[BUTTON_EVENT_MAX];

esp_err_t iot_button_register_cb(button_handle_t handle, button_event_t event,
    button_event_args_t *args, button_cb_t callback, void *context)
{
    /* Hardware-free registry: production BSP must request exactly the two
     * callbacks the badge needs, leaving SDK long-press callback arrays NULL. */
    assert(event == BUTTON_PRESS_DOWN || event == BUTTON_PRESS_UP);
    assert(!args && callback && !handle->cb_info[event]);
    registry[event] = (button_cb_info_t){.cb = callback, .usr_data = context};
    handle->cb_info[event] = &registry[event];
    handle->size[event] = 1;
    return ESP_OK;
}

static uint8_t key_level(button_driver_t *driver)
{
    (void) driver;
    ++reads;
    return now_ms >= pressed_from && now_ms < pressed_until ? BUTTON_ACTIVE : BUTTON_INACTIVE;
}

static void action(badge_input_action_t result)
{
    if (!result.ready) return;
    assert(result.key == 2);
    if (result.long_press) ++long_actions;
    else ++short_actions;
}

static void edge(bsp_btn_t key, bsp_btn_ev_t event, void *context)
{
    assert(key == BSP_BTN_OK && !context);
    assert(event == BSP_BTN_PRESS || event == BSP_BTN_RELEASE);
    bool pressed = event == BSP_BTN_PRESS;
    if (pressed) { ++downs; down_at = now_ms; }
    else { ++ups; up_at = now_ms; }
    if (!badge_idle_key(&idle, 2, pressed, now_ms * 1000))
        action(badge_input_edge(&input, 2, pressed, now_ms * 1000));
}

static void reset(bool asleep)
{
    now_ms = reads = downs = ups = short_actions = long_actions = 0;
    down_at = up_at = -1;
    badge_input_init(&input);
    badge_idle_init(&idle, 0);
    idle.asleep = asleep;
    static button_driver_t driver = {.get_key_level = key_level};
    button = (button_dev_t){.driver = &driver,
        .long_press_ticks = BSP_BTN_LONG_PRESS_MS / TICKS_INTERVAL,
        .short_press_ticks = BSP_BTN_SHORT_PRESS_MS / TICKS_INTERVAL,
        .event = BUTTON_NONE_PRESS};
    s_cb = edge;
    s_user = NULL;
    s_ready = true;
    assert(register_callbacks(&button, (void *)(intptr_t)BSP_BTN_OK, true) == ESP_OK);
    assert(!button.cb_info[BUTTON_LONG_PRESS_START]);
}

static void run_until(int64_t end)
{
    for (; now_ms < end; ++now_ms) {
        if (now_ms % TICKS_INTERVAL == 0) button_handler(&button);
        /* Edge delivery is immediate; ordinary hold checks run at 20 ms. */
        if (now_ms % 20 == 0) action(badge_input_poll(&input, now_ms * 1000));
    }
}

static void check_gesture(unsigned phase, unsigned duration, bool asleep)
{
    reset(asleep);
    pressed_from = 100 + phase;
    pressed_until = pressed_from + duration;
    run_until(pressed_until + 100);
    assert(downs == 1 && ups == 1);
    assert(down_at >= pressed_from + TICKS_INTERVAL);
    assert(down_at < pressed_from + 2 * TICKS_INTERVAL);
    assert(up_at >= pressed_until + TICKS_INTERVAL);
    assert(up_at < pressed_until + 2 * TICKS_INTERVAL);
    assert(!idle.asleep && !idle.consume_wake && !idle.held_keys);
    bool held = duration * 1000 >= BADGE_HOLD_US;
    assert(short_actions == (!asleep && !held));
    assert(long_actions == (!asleep && held));

    if (asleep) {
        /* A short or long wake gesture is entirely consumed; the next tap
         * works even when the component still has click classification state. */
        pressed_from = now_ms + 50;
        pressed_until = pressed_from + 50;
        run_until(pressed_until + 100);
        assert(downs == 2 && ups == 2);
        assert(short_actions == 1 && long_actions == 0);
    }
}

int main(void)
{
    assert(TICKS_INTERVAL == 10 && DEBOUNCE_TICKS == 2);
    for (unsigned phase = 0; phase < TICKS_INTERVAL; ++phase) {
        const unsigned durations[] = {30, 100, 550, 750, 1500};
        for (unsigned i = 0; i < sizeof(durations) / sizeof(durations[0]); ++i) {
            check_gesture(phase, durations[i], false);
            check_gesture(phase, durations[i], true);
        }
        reset(false);
        pressed_from = 100 + phase;
        pressed_until = pressed_from + 5;
        run_until(300);
        assert(!downs && !ups && !short_actions && !long_actions);
    }
    reset(false);
    pressed_from = pressed_until = 0;
    run_until(1000);
    assert(reads == 100 && !downs && !ups);
    puts("Pinned button sampling: 100 polls/s, phase/tap/debounce/hold/wake PASS");
    return 0;
}
