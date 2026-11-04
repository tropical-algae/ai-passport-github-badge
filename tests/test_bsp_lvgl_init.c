// No LVGL task may see the display until the rounding callback is registered.
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "../components/bsp/src/bsp_display_lvgl.c"

static lv_display_t display;
static int panel_present = 1, lock_depth, port_live, display_live, callback_live;
static int fail_lock, fail_port, fail_display, fail_event, init_calls, unlocked_flushes;
static int fail_guard, port_refresh_requests;
static void (*refresh_guard)(lv_event_t *);
static int panel_token, io_token;
static lv_obj_t screen;
static bool timer_enabled, tick_active, lcd_asleep, dma_pending;
static bool invalidation_enabled = true;
static int invalidation_count = 1;
static lv_timer_t refresh_timer;
static uint32_t (*tick_callback)(void);
static int64_t monotonic_us = 5000000;
static void (*flush_callback)(lv_display_t *, const lv_area_t *, uint8_t *);
static unsigned fail_draw, draw_calls, flush_ready_calls;
static unsigned brightness;
static unsigned fail_stop, fail_resume, fail_panel, fail_drain, fail_light;
static char power_trace[128];
static size_t power_trace_size;
static void trace(char event) {
    assert(lock_depth && power_trace_size + 1 < sizeof(power_trace));
    power_trace[power_trace_size++] = event;
    power_trace[power_trace_size] = 0;
}
esp_lcd_panel_handle_t bsp_display_panel(void) { return panel_present ? &panel_token : NULL; }
esp_lcd_panel_io_handle_t bsp_display_io(void) { return &io_token; }
esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg) {
    (void)cfg; ++init_calls; assert(!port_live);
    if (fail_port) return ESP_ERR_NO_MEM;
    port_live = 1; timer_enabled = tick_active = true; return ESP_OK;
}
esp_err_t lvgl_port_deinit(void) {
    // The real API is asynchronous: returning does not release the context.
    assert(false && "Display rollback must not deinit/reinitialize the live port");
    return ESP_OK;
}
bool lvgl_port_lock(uint32_t timeout) {
    (void)timeout; assert(port_live);
    if (fail_lock) return false;
    ++lock_depth; return true;
}
void lvgl_port_unlock(void) {
    assert(lock_depth > 0);
    --lock_depth;
    if (!lock_depth && display_live && timer_enabled && !refresh_timer.paused) {
        assert(callback_live && refresh_guard); // Simulate rendering as soon as the lock is free.
        ++unlocked_flushes;
    }
}
lv_display_t *lvgl_port_add_disp(const lvgl_port_display_cfg_t *cfg) {
    assert(lock_depth > 0 && cfg->panel_handle == &panel_token && !display_live);
    assert(cfg->buffer_size == BSP_LCD_W * 40 && !cfg->double_buffer);
    assert(cfg->hres == BSP_LCD_W && cfg->vres == BSP_LCD_H && cfg->flags.buff_dma && cfg->flags.swap_bytes);
    assert(!cfg->rotation.swap_xy && !cfg->rotation.mirror_x && !cfg->rotation.mirror_y);
    assert(lvgl_port_lock(0)); // Real port takes and releases a recursive lock.
    if (!fail_display) display_live = 1;
    lvgl_port_unlock();
    return fail_display ? NULL : &display;
}
esp_err_t lvgl_port_remove_disp(lv_display_t *disp) {
    assert(disp == &display && lock_depth && display_live);
    display_live = callback_live = 0; refresh_guard = NULL; return ESP_OK;
}
void lv_display_add_event_cb(lv_display_t *disp, void (*cb)(lv_event_t *), int code, void *user) {
    (void)user;
    assert(lock_depth && disp == &display);
    if (code == LV_EVENT_FLUSH_START) {
        assert(cb == rounded_flush_event);
        if (!fail_event) callback_live = 1;
    } else {
        assert(code == LV_EVENT_REFR_REQUEST && cb == sleep_refresh_guard && callback_live);
        if (!fail_guard) refresh_guard = cb;
    }
}
uint32_t lv_display_get_event_count(lv_display_t *disp) {
    assert(disp == &display && lock_depth);
    // LVGL's built-in handler and the port's 3 callbacks precede the BSP's two.
    return 4 + callback_live + (refresh_guard != NULL);
}
esp_err_t lvgl_port_stop(void) {
    trace('S'); timer_enabled = false; // Real port changes this before timer_stop.
    if (fail_stop) { --fail_stop; return ESP_FAIL; }
    if (!tick_active) return ESP_ERR_INVALID_STATE;
    tick_active = false; return ESP_OK;
}
esp_err_t lvgl_port_resume(void) {
    trace('R'); timer_enabled = true; // Real port changes this before timer_start.
    if (fail_resume) { --fail_resume; return ESP_FAIL; }
    if (tick_active) return ESP_ERR_INVALID_STATE;
    tick_active = true; return ESP_OK;
}
esp_err_t bsp_display_set_backlight(uint8_t value) {
    trace(value ? '6' : '0');
    if (fail_light) { --fail_light; return ESP_FAIL; }
    if (value) assert(!dma_pending);
    brightness = value;
    return ESP_OK;
}
esp_err_t bsp_display_sleep(bool sleep) {
    trace(sleep ? 'I' : 'O');
    assert(timer_enabled && !tick_active && !brightness && refresh_timer.paused && !invalidation_enabled);
    lcd_asleep = sleep; // Simulate a failure after SLPIN/SLPOUT reached hardware.
    dma_pending = false; // Actual panel commands first drain the SPI queue.
    if (fail_panel) { --fail_panel; return ESP_FAIL; }
    return ESP_OK;
}
lv_obj_t *lv_display_get_screen_active(lv_display_t *disp) {
    assert(lock_depth && disp == &display); return &screen;
}
void lv_obj_invalidate(lv_obj_t *obj) { assert(obj == &screen); trace('V'); }
void lv_refr_now(lv_display_t *disp) {
    assert(disp == &display && timer_enabled && !tick_active && !brightness && flush_callback);
    if (!s_refresh_paused) assert(!lcd_asleep && !refresh_timer.paused && invalidation_enabled);
    else assert(refresh_timer.paused && !invalidation_enabled);
    trace('F');
    uint8_t pixels[] = {0x12, 0x34};
    lv_area_t area = {.x1 = 3, .x2 = 3, .y1 = 4, .y2 = 4};
    flush_callback(disp, &area, pixels);
}
void lv_display_set_flush_cb(lv_display_t *disp, void (*cb)(lv_display_t *, const lv_area_t *, uint8_t *)) {
    assert(lock_depth && disp == &display); flush_callback = cb;
}
void lv_display_flush_ready(lv_display_t *disp) { assert(disp == &display); ++flush_ready_calls; }
void lv_draw_sw_rgb565_swap(void *pixels, uint32_t count) {
    uint8_t *p = pixels;
    for (uint32_t i = 0; i < count; ++i) { uint8_t b = p[2*i]; p[2*i] = p[2*i+1]; p[2*i+1] = b; }
}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x1, int y1,
                                    int x2, int y2, const void *pixels) {
    assert(lock_depth && panel == &panel_token && x1 == 3 && x2 == 4 && y1 == 4 && y2 == 5);
    const uint8_t *p = pixels; assert(p[0] == 0x34 && p[1] == 0x12);
    ++draw_calls;
    if (fail_draw) { --fail_draw; return ESP_FAIL; }
    dma_pending = true; return ESP_OK;
}
void lv_display_enable_invalidation(lv_display_t *disp, bool enabled) {
    assert(lock_depth && disp == &display);
    invalidation_count += enabled ? 1 : -1; // LVGL's public API is reference-counted.
    assert(invalidation_count == 0 || invalidation_count == 1);
    invalidation_enabled = invalidation_count > 0;
}
lv_timer_t *lv_display_get_refr_timer(lv_display_t *disp) {
    assert(lock_depth && disp == &display); return &refresh_timer;
}
void lv_timer_pause(lv_timer_t *timer) { assert(lock_depth && timer == &refresh_timer); timer->paused = true; }
void lv_timer_resume(lv_timer_t *timer) { assert(lock_depth && timer == &refresh_timer); timer->paused = false; }
void lv_timer_enable(bool enabled) { assert(lock_depth); timer_enabled = enabled; }
uint32_t lv_tick_get(void) { return tick_callback ? tick_callback() : 1234; }
void lv_tick_set_cb(uint32_t (*cb)(void)) { assert(lock_depth); tick_callback = cb; }
int64_t esp_timer_get_time(void) { return monotonic_us; }
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int command,
                                    const void *params, size_t size) {
    assert(io == &io_token && command == -1 && !params && !size);
    trace('D');
    if (fail_drain) { --fail_drain; return ESP_FAIL; }
    if (dma_pending) lv_display_flush_ready(&display); // Existing port DMA callback.
    dma_pending = false; return ESP_OK;
}
static void reset_power(void) {
    assert(!lock_depth);
    s_port_power = PORT_RUNNING; s_suspended = s_lcd_sleeping = false;
    timer_enabled = tick_active = true; lcd_asleep = dma_pending = false;
    s_monotonic_tick = s_refresh_paused = false; s_tick_offset = 0; s_flush_error = ESP_OK;
    tick_callback = NULL; monotonic_us = 5000000;
    invalidation_enabled = true; invalidation_count = 1; refresh_timer.paused = false;
    fail_draw = draw_calls = flush_ready_calls = 0;
    port_refresh_requests = 0;
    brightness = 60;
    fail_stop = fail_resume = fail_panel = fail_drain = fail_light = 0;
    power_trace_size = 0; power_trace[0] = 0;
}
static esp_err_t sleep_display(void) {
    assert(bsp_lvgl_lock(100));
    esp_err_t e = bsp_lvgl_sleep(); bsp_lvgl_unlock(); return e;
}
static esp_err_t wake_display(void) {
    assert(bsp_lvgl_lock(100));
    esp_err_t e = bsp_lvgl_wake(60); bsp_lvgl_unlock(); return e;
}
static void power_transitions(void) {
    reset_power();
    assert(sleep_display() == ESP_OK);
    assert(s_suspended && s_lcd_sleeping && timer_enabled && !tick_active && !brightness);
    assert(refresh_timer.paused && !invalidation_enabled && lv_tick_get() == 1234);
    monotonic_us += 60000000;
    assert(lv_tick_get() == 61234); // Other LVGL timers continue to observe real time.
    assert(sleep_display() == ESP_OK && !strcmp(power_trace, "0SI"));
    assert(wake_display() == ESP_OK && !strcmp(power_trace, "0SI0OVFD6"));
    assert(!s_suspended && !lcd_asleep && !dma_pending && brightness == 60);
    assert(!tick_active && timer_enabled && !refresh_timer.paused && invalidation_enabled);
    assert(wake_display() == ESP_OK && !strcmp(power_trace, "0SI0OVFD66"));

    reset_power(); fail_stop = 1;
    assert(sleep_display() == ESP_FAIL);
    assert(s_suspended && s_port_power == PORT_UNKNOWN && timer_enabled && tick_active);
    assert(sleep_display() == ESP_OK); // Retry reconciles ambiguous port state.
    assert(wake_display() == ESP_OK && !tick_active);

    reset_power(); fail_stop = fail_resume = 1;
    assert(sleep_display() == ESP_FAIL && s_port_power == PORT_UNKNOWN);
    assert(wake_display() == ESP_OK && !s_suspended && brightness == 60);

    reset_power(); tick_active = false; // Ambiguous prior stop left tick inactive.
    assert(sleep_display() == ESP_ERR_INVALID_STATE); // Stop fails; rollback repairs it.
    assert(s_port_power == PORT_RUNNING && timer_enabled && tick_active);
    assert(wake_display() == ESP_OK && !s_suspended && brightness == 60);

    reset_power(); fail_light = 1;
    assert(sleep_display() == ESP_FAIL && s_suspended);
    assert(sleep_display() == ESP_OK);
    fail_panel = 1;
    assert(wake_display() == ESP_FAIL && !lcd_asleep && !s_lcd_sleeping);
    assert(sleep_display() == ESP_OK && lcd_asleep); // Partial wake must re-sleep.
    fail_drain = 1;
    assert(wake_display() == ESP_FAIL && !brightness && dma_pending && s_suspended);
    assert(wake_display() == ESP_OK && !dma_pending && brightness == 60);

    reset_power(); fail_panel = 1;
    assert(sleep_display() == ESP_FAIL && !s_lcd_sleeping);
    assert(sleep_display() == ESP_OK && s_lcd_sleeping);

    reset_power(); assert(sleep_display() == ESP_OK); fail_draw = 1;
    assert(wake_display() == ESP_FAIL && !brightness && s_suspended);
    assert(draw_calls == 1 && flush_ready_calls == 1 && refresh_timer.paused && !invalidation_enabled);
    assert(wake_display() == ESP_OK && brightness == 60 && draw_calls == 2);
}
static void dirty_layout_cannot_restart_sleeping_display(void) {
    reset_power(); assert(sleep_display() == ESP_OK);
    assert(bsp_lvgl_lock(100));
    for (unsigned i = 0; i < 3; ++i) {
        // lv_obj_mark_layout_as_dirty sends REFR_REQUEST even with invalidation
        // disabled. Simulate built-in resume, port wake event, then BSP guard.
        lv_timer_resume(&refresh_timer);
        ++port_refresh_requests;
        assert(refresh_guard && !refresh_timer.paused);
        lv_event_t request = {.target = &display};
        refresh_guard(&request);
        assert(refresh_timer.paused && invalidation_count == 0 && s_refresh_paused);
    }
    assert(port_refresh_requests == 3);
    lv_refr_now(&display); // Explicit refresh is also blocked at the flush boundary.
    assert(!draw_calls && flush_ready_calls == 1 && !dma_pending && lcd_asleep);
    uint8_t untouched[] = {0x12, 0x34};
    lv_area_t area = {.x1 = 3, .x2 = 3, .y1 = 4, .y2 = 4};
    flush_callback(&display, &area, untouched);
    assert(untouched[0] == 0x12 && untouched[1] == 0x34 && flush_ready_calls == 2 && !draw_calls);
    bsp_lvgl_unlock();
    assert(wake_display() == ESP_OK && draw_calls == 1 && brightness == 60);
    assert(bsp_lvgl_lock(100));
    lv_timer_resume(&refresh_timer);
    lv_event_t request = {.target = &display};
    refresh_guard(&request);
    assert(!refresh_timer.paused); // Normal awake layout requests still refresh.
    bsp_lvgl_unlock();
}
static void expect_failure(void) {
    assert(bsp_lvgl_init() == NULL);
    assert(!s_disp && !lock_depth && !display_live);
    assert(!bsp_lvgl_lock(0));
}
int main(void) {
    panel_present = 0; expect_failure(); panel_present = 1;
    fail_port = 1; expect_failure(); fail_port = 0;
    const int failed_init_calls = init_calls;
    expect_failure(); // A partially initialized port cannot safely be re-created.
    assert(init_calls == failed_init_calls);
    s_port_init_failed = false; // Simulate reboot for remaining scenarios.
    fail_lock = 1; expect_failure(); fail_lock = 0;
    const int retained_init_calls = init_calls;
    fail_display = 1; expect_failure(); fail_display = 0;
    fail_event = 1; expect_failure(); fail_event = 0;
    fail_guard = 1; expect_failure(); fail_guard = 0;
    assert(bsp_lvgl_init() == &display);
    assert(init_calls == retained_init_calls); // Display retries reuse the port.
    assert(callback_live && !lock_depth && unlocked_flushes == 1);
    const int before = init_calls;
    assert(bsp_lvgl_init() == &display && init_calls == before);
    assert(bsp_lvgl_lock(5)); bsp_lvgl_unlock();
    uint16_t pixels[BSP_LCD_W] = {0};
    for (int x = 0; x < BSP_LCD_W; ++x) pixels[x] = 0xffff;
    display.buffer = (lv_draw_buf_t){ .data = (uint8_t *)pixels, .header.stride = sizeof(pixels) };
    lv_area_t area = { .x1 = 0, .y1 = 0, .x2 = BSP_LCD_W - 1, .y2 = 0 };
    lv_event_t ev = { .target = &display, .area = &area };
    rounded_flush_event(&ev);
    assert(pixels[0] == 0 && pixels[BSP_LCD_W - 1] == 0 && pixels[BSP_LCD_W / 2] == 0xffff);
    power_transitions();
    dirty_layout_cannot_restart_sleeping_display();
    puts("BSP LVGL initialization and reversible power tests: PASS");
}
