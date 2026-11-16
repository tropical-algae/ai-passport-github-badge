/* Real badge UI + real LVGL + production BSP sleep/wake. Only the ESP port,
 * time and physical panel are simulated. This checks transmitted pixels and
 * backlight ordering; it cannot model the LCD's analog scanout or PWM signal. */
#include "badge_ui.h"
#include "bsp_display.h"
#include "esp_lvgl_port.h"
#include "freertos/task.h"
#include "src/core/lv_obj_private.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 240, HEIGHT = 320, PIXELS = WIDTH * HEIGHT };
static uint16_t framebuffer[PIXELS], expected[PIXELS], drawbuffer[WIDTH * 40];
static bool covered[PIXELS];
static lv_display_t *display;
static badge_config_t config;
static badge_data_t data;
static badge_status_t status;
static int panel_token, io_token, lock_depth;
static bool tick_active, sleeping, dma_pending, verify_visible_frame;
static bool check_wake_frame;
static uint8_t brightness;
static int64_t now_us = 1000000;
static const uint8_t *dma_pixels;
static lv_area_t dma_area;
static unsigned flushes, lit_flushes, wakes, visible_checks, light_on_calls;

static void check_visible(void)
{
    if (!brightness) return;
    unsigned nonblack = 0;
    for (unsigned i = 0; i < PIXELS; ++i) nonblack += framebuffer[i] != 0;
    assert(nonblack > PIXELS / 2); /* All five themes have a nonblack background. */
    if (verify_visible_frame) assert(!memcmp(framebuffer, expected, sizeof(expected)));
    ++visible_checks;
}

static void complete_dma(void)
{
    if (!dma_pending) return;
    assert(!sleeping);
    const uint8_t *p = dma_pixels;
    for (int y = dma_area.y1; y <= dma_area.y2; ++y) {
        for (int x = dma_area.x1; x <= dma_area.x2; ++x, p += 2) {
            unsigned i = (unsigned)(y * WIDTH + x);
            /* Production checked_flush already converted RGB565 to SPI order. */
            framebuffer[i] = (uint16_t)((uint16_t)p[0] << 8 | p[1]);
            covered[i] = true;
        }
    }
    dma_pending = false;
    ++flushes;
    if (brightness) ++lit_flushes;
    check_visible(); /* Inspect every submitted strip, not just the final frame. */
    lv_display_flush_ready(display);
}

static void flush_wait(lv_display_t *disp)
{
    assert(disp == display);
    complete_dma();
}

esp_lcd_panel_handle_t bsp_display_panel(void) { return &panel_token; }
esp_lcd_panel_io_handle_t bsp_display_io(void) { return &io_token; }
esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg)
{
    (void)cfg;
    lv_init();
    tick_active = true;
    return ESP_OK;
}
bool lvgl_port_lock(uint32_t timeout) { (void)timeout; ++lock_depth; return true; }
void lvgl_port_unlock(void) { assert(lock_depth > 0); --lock_depth; }
esp_err_t lvgl_port_stop(void)
{
    assert(lock_depth && tick_active);
    lv_timer_enable(false);
    tick_active = false;
    return ESP_OK;
}
esp_err_t lvgl_port_resume(void)
{
    assert(lock_depth && !tick_active);
    lv_timer_enable(true);
    tick_active = true;
    return ESP_OK;
}
lv_display_t *lvgl_port_add_disp(const lvgl_port_display_cfg_t *cfg)
{
    assert(lock_depth && cfg->hres == WIDTH && cfg->vres == HEIGHT);
    assert(cfg->buffer_size == WIDTH * 40 && !cfg->double_buffer);
    assert(!cfg->rotation.swap_xy && !cfg->rotation.mirror_x && !cfg->rotation.mirror_y);
    display = lv_display_create(WIDTH, HEIGHT);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, drawbuffer, NULL, sizeof(drawbuffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_wait_cb(display, flush_wait);
    return display;
}
esp_err_t lvgl_port_remove_disp(lv_display_t *disp)
{
    lv_display_delete(disp);
    return ESP_OK;
}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x1, int y1,
                                    int x2, int y2, const void *pixels)
{
    assert(panel == &panel_token && lock_depth && !sleeping && !dma_pending);
    assert(x1 >= 0 && y1 >= 0 && x2 <= WIDTH && y2 <= HEIGHT && x1 < x2 && y1 < y2);
    dma_area = (lv_area_t){x1, y1, x2 - 1, y2 - 1};
    dma_pixels = pixels;
    dma_pending = true;
    return ESP_OK;
}
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int command,
                                   const void *params, size_t size)
{
    assert(io == &io_token && command == -1 && !params && !size && lock_depth);
    complete_dma();
    return ESP_OK;
}
esp_err_t bsp_display_set_backlight(uint8_t value)
{
    assert(lock_depth);
    if (value) {
        ++light_on_calls;
        assert(!sleeping && !dma_pending);
        if (check_wake_frame) {
            for (unsigned i = 0; i < PIXELS; ++i) assert(covered[i]);
            assert(!memcmp(framebuffer, expected, sizeof(expected)));
            ++wakes;
            check_wake_frame = false;
            verify_visible_frame = true;
        }
    }
    brightness = value;
    check_visible();
    return ESP_OK;
}
esp_err_t bsp_display_sleep(bool sleep)
{
    assert(lock_depth && !brightness);
    complete_dma();
    sleeping = sleep;
    now_us += sleep ? 100000 : 122000;
    if (!sleep) memset(covered, 0, sizeof(covered));
    return ESP_OK;
}
int64_t esp_timer_get_time(void) { return now_us; }
void vTaskDelay(TickType_t ticks) { now_us += (int64_t)ticks * 1000; }

static void refresh(void)
{
    assert(lock_depth);
    lv_refr_now(display);
    complete_dma();
    assert(lv_anim_count_running() == 0);
}

static void sleep_wake(void)
{
    refresh();
    memcpy(expected, framebuffer, sizeof(expected));
    unsigned before = flushes, light_on_before = light_on_calls;
    assert(bsp_lvgl_sleep() == ESP_OK);
    assert(!brightness && sleeping);
    now_us += 60000000;
    /* A normal status publication/refresh while asleep must not write GRAM.
     * This exercises the real style invalidation and display refresh guard. */
    badge_ui_update(&data, &config, &status);
    lv_timer_handler();
    refresh();
    assert(flushes == before);
    check_wake_frame = true;
    assert(bsp_lvgl_wake(config.brightness) == ESP_OK);
    assert(brightness == config.brightness && !check_wake_frame);
    /* A late animation/dirty-layout refresh would run after brightness returns.
     * Check its every DMA strip too, including the first subsequent timer run. */
    const unsigned elapsed_ms[] = {0, 1, 19, 20, 500, 5000};
    for (unsigned i = 0; i < sizeof(elapsed_ms) / sizeof(elapsed_ms[0]); ++i) {
        now_us += (int64_t)elapsed_ms[i] * 1000;
        lv_timer_handler();
        complete_dma();
        check_visible();
        assert(lv_anim_count_running() == 0);
    }
    assert(light_on_calls == light_on_before + 1);
}

static badge_action_t key(badge_key_t value, bool held)
{
    badge_action_t action = badge_ui_key(value, held);
    refresh();
    return action;
}

static void select_theme(unsigned theme)
{
    verify_visible_frame = false;
    key(BADGE_KEY_OK, true);
    key(BADGE_KEY_DOWN, false);
    key(BADGE_KEY_DOWN, false);
    key(BADGE_KEY_OK, false);
    for (unsigned i = config.theme; i != theme; i = (i + 1) % 5)
        key(BADGE_KEY_DOWN, false);
    badge_action_t action = key(BADGE_KEY_OK, false);
    assert(action.kind == BADGE_ACTION_THEME && action.value == theme);
    config.theme = action.value;
    badge_ui_update(&data, &config, &status);
    refresh();
    key(BADGE_KEY_OK, true);
    assert(lv_screen_active()->style_cnt == 1);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    unsigned startup_theme = (unsigned)strtoul(argv[1], NULL, 10);
    assert(startup_theme < 5);
    badge_config_defaults(&config);
    config.theme = (uint8_t)startup_theme;
    strcpy(config.username, "pixeldev");
    badge_data_reset(&data, config.username);
    strcpy(data.name, "Pixel Developer");
    data.valid = data.complete = BADGE_PROFILE;
    data.followers = 1284;
    status.now = 1793620800;
    status.battery_percent = 87;
    status.connected = true;
    lv_display_t *initialized = bsp_lvgl_init();
    assert(initialized && initialized == display);
    assert(bsp_lvgl_lock(1000));
    badge_ui_init(&data, &config, &status);
    refresh();
    assert(bsp_display_set_backlight(config.brightness) == ESP_OK);
    sleep_wake(); /* Startup from restored theme, with no interactive selection. */

    /* User's exact reported color sequence, then every other color. */
    const unsigned sequence[] = {0, 1, 0, 2, 3, 4};
    for (unsigned i = 0; i < sizeof(sequence) / sizeof(sequence[0]); ++i) {
        select_theme(sequence[i]);
        sleep_wake();
    }
#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    lv_mem_monitor_t before, after;
    lv_mem_monitor(&before);
#endif
    for (unsigned round = 0; round < 3; ++round) {
        for (unsigned theme = 0; theme < 5; ++theme) {
            select_theme(theme);
            sleep_wake();
        }
    }
#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    lv_mem_monitor(&after);
    assert(after.free_size == before.free_size && after.used_cnt == before.used_cnt);
#endif
    for (unsigned theme = 0; theme < 5; ++theme) {
        select_theme(theme);
        for (unsigned page = 0; page < 7; ++page) {
            sleep_wake();
            verify_visible_frame = false;
            key(BADGE_KEY_DOWN, true);
        }
    }
    const uint8_t levels[] = {100, 80, 60, 40, 20, 10, 100};
    for (unsigned theme = 0; theme < 5; ++theme) {
        select_theme(theme);
        for (unsigned i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
            config.brightness = levels[i];
            assert(bsp_display_set_backlight(config.brightness) == ESP_OK);
            sleep_wake();
        }
    }
    bsp_lvgl_unlock();
    printf("Real LVGL wake: startup theme %u, %u wakes, %u DMA strips, "
           "%u lit strips, %u visible checks: PASS\n",
           startup_theme, wakes, flushes, lit_flushes, visible_checks);
    return 0;
}
