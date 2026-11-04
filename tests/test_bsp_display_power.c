// Exercise the actual checked backlight/LCD functions, not a copy of the logic.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../components/bsp/src/bsp_display.c"

static int panel_token, io_token;
static bool pwm_active, panel_asleep, panel_on;
static uint32_t duty, elapsed_ms;
static unsigned fail_stop, fail_set, fail_update, fail_off, fail_sleep, fail_on;
static char trace[64];
static size_t trace_size;
static void record(char e) { assert(trace_size + 1 < sizeof(trace)); trace[trace_size++] = e; trace[trace_size] = 0; }
static bool fail_once(unsigned *count) { if (!*count) return false; --*count; return true; }
esp_err_t ledc_stop(int mode, int channel, uint32_t level) {
    assert(mode == BSP_BL_LEDC_MODE && channel == BSP_BL_LEDC_CHANNEL && level == 0);
    record('0');
    if (fail_once(&fail_stop)) return ESP_FAIL;
    pwm_active = false; return ESP_OK;
}
esp_err_t ledc_set_duty(int mode, int channel, uint32_t value) {
    assert(mode == BSP_BL_LEDC_MODE && channel == BSP_BL_LEDC_CHANNEL);
    record('D');
    if (fail_once(&fail_set)) return ESP_FAIL;
    duty = value; return ESP_OK;
}
esp_err_t ledc_update_duty(int mode, int channel) {
    assert(mode == BSP_BL_LEDC_MODE && channel == BSP_BL_LEDC_CHANNEL);
    record('U');
    if (fail_once(&fail_update)) return ESP_FAIL;
    pwm_active = true; return ESP_OK; // Mirrors IDF _ledc_update_duty's sig_out_en.
}
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t panel, bool on) {
    assert(panel == &panel_token && !pwm_active); record(on ? 'N' : 'F');
    if (fail_once(on ? &fail_on : &fail_off)) return ESP_FAIL;
    if (on) assert(!panel_asleep && elapsed_ms >= 120);
    panel_on = on; return ESP_OK;
}
esp_err_t esp_lcd_panel_disp_sleep(esp_lcd_panel_handle_t panel, bool sleep) {
    assert(panel == &panel_token && !pwm_active); record(sleep ? 'I' : 'O');
    if (fail_once(&fail_sleep)) return ESP_FAIL;
    panel_asleep = sleep; elapsed_ms = 100; // Pinned IDF panel driver waits here.
    return ESP_OK;
}
void vTaskDelay(TickType_t ticks) { record('W'); elapsed_ms += ticks; }

// Lifecycle APIs are linked for completeness. These tests start after init and
// must never enter the terminal deep-sleep GPIO hold path.
esp_err_t gpio_config(const gpio_config_t *cfg) { (void)cfg; assert(false); return ESP_FAIL; }
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t value) { (void)pin; (void)value; assert(false); return ESP_FAIL; }
esp_err_t gpio_hold_en(gpio_num_t pin) { (void)pin; assert(false); return ESP_FAIL; }
esp_err_t gpio_hold_dis(gpio_num_t pin) { (void)pin; assert(false); return ESP_FAIL; }
void gpio_deep_sleep_hold_en(void) { assert(false); }
void gpio_deep_sleep_hold_dis(void) { assert(false); }
esp_err_t spi_bus_initialize(int host, const spi_bus_config_t *cfg, int dma) { (void)host; (void)cfg; (void)dma; assert(false); return ESP_FAIL; }
esp_err_t spi_bus_free(int host) { (void)host; assert(false); return ESP_FAIL; }
esp_err_t ledc_timer_config(const ledc_timer_config_t *cfg) { (void)cfg; assert(false); return ESP_FAIL; }
esp_err_t ledc_channel_config(const ledc_channel_config_t *cfg) { (void)cfg; assert(false); return ESP_FAIL; }
esp_err_t ledc_timer_rst(int mode, int timer) { (void)mode; (void)timer; assert(false); return ESP_FAIL; }
esp_err_t esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus, const esp_lcd_panel_io_spi_config_t *cfg, esp_lcd_panel_io_handle_t *io) { (void)bus; (void)cfg; (void)io; assert(false); return ESP_FAIL; }
esp_err_t esp_lcd_new_panel_st7789(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *cfg, esp_lcd_panel_handle_t *panel) { (void)io; (void)cfg; (void)panel; assert(false); return ESP_FAIL; }
esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t p) { (void)p; assert(false); return ESP_FAIL; }
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t p) { (void)p; assert(false); return ESP_FAIL; }
esp_err_t esp_lcd_panel_invert_color(esp_lcd_panel_handle_t p, bool inv) { (void)p; (void)inv; assert(false); return ESP_FAIL; }
esp_err_t esp_lcd_panel_mirror(esp_lcd_panel_handle_t p, bool x, bool y) { (void)p; (void)x; (void)y; assert(false); return ESP_FAIL; }
esp_err_t esp_lcd_panel_set_gap(esp_lcd_panel_handle_t p, int x, int y) { (void)p; (void)x; (void)y; assert(false); return ESP_FAIL; }
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t p) { (void)p; assert(false); return ESP_FAIL; }
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t p) { (void)p; assert(false); return ESP_FAIL; }
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t p, int cmd, const void *data, size_t n) { (void)p; (void)cmd; (void)data; (void)n; assert(false); return ESP_FAIL; }

static void reset(void) {
    s_panel = &panel_token; s_io = &io_token; s_bl_ready = s_ready = true;
    pwm_active = panel_on = true; panel_asleep = false; duty = elapsed_ms = 0;
    fail_stop = fail_set = fail_update = fail_off = fail_sleep = fail_on = 0;
    trace_size = 0; trace[0] = 0;
}
int main(void) {
    reset(); s_bl_ready = false;
    assert(bsp_display_set_backlight(0) == ESP_ERR_INVALID_STATE && !trace_size);
    s_bl_ready = true;
    assert(bsp_display_set_backlight(0) == ESP_OK && !pwm_active);
    assert(bsp_display_set_backlight(60) == ESP_OK && pwm_active && duty == 613);
    assert(bsp_display_set_backlight(255) == ESP_OK && duty == 1023);
    reset(); fail_stop = 1;
    assert(bsp_display_sleep(true) == ESP_FAIL && !strcmp(trace, "0") && !panel_asleep);
    reset(); fail_set = 1;
    assert(bsp_display_set_backlight(60) == ESP_FAIL && !strcmp(trace, "D"));
    reset(); fail_update = 1;
    assert(bsp_display_set_backlight(60) == ESP_FAIL && !strcmp(trace, "DU"));
    reset();
    assert(bsp_display_sleep(true) == ESP_OK && !strcmp(trace, "0FI") && panel_asleep && !panel_on && !pwm_active);
    assert(bsp_display_sleep(false) == ESP_OK && !strcmp(trace, "0FI0OWN") && !panel_asleep && panel_on && !pwm_active);
    assert(elapsed_ms >= 120);
    assert(bsp_display_set_backlight(60) == ESP_OK && pwm_active);
    reset(); fail_off = 1;
    assert(bsp_display_sleep(true) == ESP_FAIL && !strcmp(trace, "0FI") && panel_asleep);
    reset(); fail_sleep = 1;
    assert(bsp_display_sleep(false) == ESP_FAIL && !strcmp(trace, "0O") && !pwm_active);
    reset(); fail_on = 1;
    assert(bsp_display_sleep(false) == ESP_FAIL && !strcmp(trace, "0OWN") && !panel_asleep && !pwm_active);
    assert(bsp_display_sleep(true) == ESP_OK && panel_asleep);
    puts("BSP display checked power transitions: PASS");
}
