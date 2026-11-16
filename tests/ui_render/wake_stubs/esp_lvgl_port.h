#pragma once

/* Only the ESP port is simulated. The wake regression uses the pinned LVGL
 * implementation, including its real timers, style updates and renderer. */
#include "lvgl.h"
#include "esp_err.h"
#include "esp_lcd_types.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct { int unused; } lvgl_port_cfg_t;
#define ESP_LVGL_PORT_INIT_CONFIG() {0}
typedef struct {
    esp_lcd_panel_handle_t panel_handle;
    esp_lcd_panel_io_handle_t io_handle;
    uint32_t buffer_size;
    bool double_buffer;
    int hres, vres;
    struct { bool swap_xy, mirror_x, mirror_y; } rotation;
    struct { bool buff_dma, swap_bytes; } flags;
} lvgl_port_display_cfg_t;

esp_err_t lvgl_port_init(const lvgl_port_cfg_t *);
esp_err_t lvgl_port_stop(void);
esp_err_t lvgl_port_resume(void);
bool lvgl_port_lock(uint32_t);
void lvgl_port_unlock(void);
lv_display_t *lvgl_port_add_disp(const lvgl_port_display_cfg_t *);
esp_err_t lvgl_port_remove_disp(lv_display_t *);
