// components/bsp/src/bsp_display_lvgl.c
// LVGL 接入单独成文件:不用 LVGL 的开发者删掉本文件 + idf_component.yml 里的两条依赖即可。
#include "bsp_display.h"
#include "bsp_display_rounding.h"
#include "bsp_pins.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "bsp_lvgl";

#define BSP_LVGL_DRAW_BUFFER_LINES 40

static lv_display_t *s_disp;
static bool s_port_initialized;
static bool s_port_init_failed;
typedef enum { PORT_RUNNING, PORT_STOPPED, PORT_UNKNOWN } port_power_t;
static port_power_t s_port_power;
static bool s_suspended, s_lcd_sleeping;
static bool s_monotonic_tick, s_refresh_paused;
static uint32_t s_tick_offset;
static esp_err_t s_flush_error;

static void checked_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels) {
    if (s_refresh_paused) {
        // Even an explicit lv_refr_now() must not write a sleeping LCD.
        lv_display_flush_ready(disp);
        return;
    }
    // This BSP configures portrait RGB565, single DMA buffer, no SW rotation.
    // Its 40-line buffer fits in one SPI transfer (bus maximum: 80 lines).
    // Keep the port's registered color-DMA completion callback for success.
    lv_draw_sw_rgb565_swap(pixels, (uint32_t)lv_area_get_width(area) *
                                  (uint32_t)lv_area_get_height(area));
    esp_err_t e = esp_lcd_panel_draw_bitmap(bsp_display_panel(), area->x1, area->y1,
                                           area->x2 + 1, area->y2 + 1, pixels);
    if (e != ESP_OK) {
        if (s_flush_error == ESP_OK) {
            s_flush_error = e;
            ESP_LOGW(TAG, "LCD draw failed: %s", esp_err_to_name(e));
        }
        // A rejected draw queues no completion callback. Release LVGL's flush
        // wait explicitly so wake can report failure and retry instead of hang.
        lv_display_flush_ready(disp);
    }
}

static void sleep_refresh_guard(lv_event_t *event) {
    // Dirty-layout requests bypass invalidation. LVGL's built-in REFR_REQUEST
    // callback resumes the timer first; this last-registered guard pauses it
    // again while asleep, without discarding the pending layout changes.
    if (s_refresh_paused)
        lv_timer_pause(lv_display_get_refr_timer(lv_event_get_target(event)));
}

static void rounded_flush_event(lv_event_t *event)
{
    lv_display_t *disp = lv_event_get_target(event);
    const lv_area_t *area = lv_event_get_param(event);
    lv_draw_buf_t *draw_buf = lv_display_get_buf_active(disp);
    if (!area || !draw_buf || !draw_buf->data ||
        lv_display_get_color_format(disp) != LV_COLOR_FORMAT_RGB565) {
        return;
    }

    const int32_t width = lv_area_get_width(area);
    if (draw_buf->header.stride < (uint32_t)width * sizeof(uint16_t)) return;

    for (int32_t y = area->y1; y <= area->y2; ++y) {
        uint16_t *row = (uint16_t *)(draw_buf->data +
                                     (y - area->y1) * draw_buf->header.stride);
        int32_t visible_x1;
        int32_t visible_x2;
        if (!bsp_display_rounded_row_span(y, BSP_LCD_W, BSP_LCD_H,
                                          BSP_LVGL_SCREEN_RADIUS, &visible_x1,
                                          &visible_x2)) {
            memset(row, 0, (size_t)width * sizeof(uint16_t));
            continue;
        }
        // Only clear pixels outside the visible span. The port swaps RGB565
        // bytes after this event; black is 0 in either byte order.
        const int32_t clear_left_end = visible_x1 > area->x2 ? area->x2 : visible_x1 - 1;
        const int32_t clear_right_start = visible_x2 < area->x1 ? area->x1 : visible_x2 + 1;
        for (int32_t x = area->x1; x <= clear_left_end; ++x) {
            row[x - area->x1] = 0;
        }
        for (int32_t x = clear_right_start; x <= area->x2; ++x) {
            row[x - area->x1] = 0;
        }
    }

}

lv_display_t *bsp_lvgl_init(void) {
    if (s_disp) return s_disp;
    if (!bsp_display_panel()) {
        ESP_LOGE(TAG, "请先成功调用 bsp_display_init()");
        return NULL;
    }

    if (!s_port_initialized) {
        // Port 2.9.0 has no public completion handshake for asynchronous deinit.
        // Never overwrite a possibly live context after a partial port failure.
        if (s_port_init_failed) {
            ESP_LOGE(TAG, "LVGL port 初始化未完成，需重启后重试");
            return NULL;
        }
        const lvgl_port_cfg_t pc = ESP_LVGL_PORT_INIT_CONFIG();
        if (lvgl_port_init(&pc) != ESP_OK) {
            s_port_init_failed = true;
            ESP_LOGE(TAG, "lvgl_port_init 失败，需重启后重试");
            return NULL;
        }
        s_port_initialized = true;
    }

    const lvgl_port_display_cfg_t dc = {
        .panel_handle = bsp_display_panel(),
        .io_handle    = bsp_display_io(),
        // ⚠ C3 无 PSRAM,DMA 只能用内部 RAM。40 行单缓冲约 19.2KB，
        // 可减少窗口命令和队列提交次数；仍保留单缓冲，避免双缓冲挤压音频/Wi-Fi。
        .buffer_size   = (uint32_t)BSP_LCD_W * BSP_LVGL_DRAW_BUFFER_LINES,
        .double_buffer = false,
        .hres = BSP_LCD_W, .vres = BSP_LCD_H,
        // 旋转/镜像必须在这里配:esp_lvgl_port 注册显示时会重新下发 MADCTL,
        // 覆盖 bsp_display.c 里 esp_lcd_panel_mirror() 的设置。
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        // swap_bytes:LVGL 输出小端 RGB565,ST7789 走 SPI 要大端 → 需交换高低字节。
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    // The port mutex is recursive. Keep display and both callback registrations in
    // one critical section, before the new display can produce its first flush.
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "LVGL 初始化加锁失败");
        return NULL;
    }
    lv_display_t *disp = lvgl_port_add_disp(&dc);
    bool callbacks_registered = false;
    if (disp) {
        // LVGL 9.5 returns void here; check the list while still holding the lock.
        const uint32_t count = lv_display_get_event_count(disp);
        lv_display_add_event_cb(disp, rounded_flush_event, LV_EVENT_FLUSH_START, NULL);
        if (lv_display_get_event_count(disp) == count + 1) {
            // lv_display_create and lvgl_port_add_disp already registered the
            // built-in/port handlers. LVGL dispatches matching callbacks in
            // registration order, so this guard runs after their resume/wakeup.
            lv_display_add_event_cb(disp, sleep_refresh_guard, LV_EVENT_REFR_REQUEST, NULL);
            callbacks_registered = lv_display_get_event_count(disp) == count + 2;
        }
    }
    if (!callbacks_registered) {
        ESP_LOGE(TAG, "LVGL display 或显示回调注册失败");
        if (disp) lvgl_port_remove_disp(disp);
        lvgl_port_unlock();
        // Retain the initialized port for retry. Deinit is asynchronous and can
        // race the next init (or even run before the task sets running=true).
        return NULL;
    }

    // Mask the final RGB565 flush instead of using root-screen clip_corner.
    // Full-screen rounded clipping creates an ARGB layer that does not fit the
    // 24 KB LVGL pool reliably on this no-PSRAM target.
    lv_display_set_flush_cb(disp, checked_flush);
    s_disp = disp;
    lvgl_port_unlock();

    ESP_LOGI(TAG, "LVGL 就绪，全局圆角=%d，外部填充=黑色", BSP_LVGL_SCREEN_RADIUS);
    return s_disp;
}

bool bsp_lvgl_lock(int timeout_ms) {
    if (!s_disp) return false;
    return lvgl_port_lock(timeout_ms);
}
void bsp_lvgl_unlock(void) {
    if (s_disp) lvgl_port_unlock();
}

static esp_err_t pause_port(void) {
    if (s_port_power == PORT_STOPPED) return ESP_OK;
    esp_err_t e = lvgl_port_stop();
    if (e == ESP_OK) {
        s_port_power = PORT_STOPPED;
    } else {
        // stop disables lv_timer_enable before it attempts esp_timer_stop.
        // Restore both halves when possible; a failed rollback is UNKNOWN,
        // never mistaken for a running or stopped timer on the next attempt.
        esp_err_t rollback = lvgl_port_resume();
        s_port_power = rollback == ESP_OK ? PORT_RUNNING : PORT_UNKNOWN;
        if (rollback != ESP_OK)
            ESP_LOGE(TAG, "LVGL stop rollback failed: %s", esp_err_to_name(rollback));
    }
    return e;
}

static uint32_t monotonic_tick(void) {
    return (uint32_t)(esp_timer_get_time() / 1000) - s_tick_offset;
}

static void pause_refresh(void) {
    // LVGL tracks this as a counter, not a bool: balance exactly one disable
    // with one enable, including all error/retry paths.
    if (!s_refresh_paused) {
        lv_display_enable_invalidation(s_disp, false);
        s_refresh_paused = true;
    }
    lv_timer_pause(lv_display_get_refr_timer(s_disp));
}

static esp_err_t prepare_hidden_display(void) {
    pause_refresh();
    esp_err_t e = pause_port();
    if (e != ESP_OK) return e;
    if (!s_monotonic_tick) {
        // Preserve LVGL tick continuity, then permanently replace the 5 ms
        // periodic tick with a read-only clock. Other LVGL timers keep real time.
        uint32_t old_tick = lv_tick_get();
        s_tick_offset = (uint32_t)(esp_timer_get_time() / 1000) - old_tick;
        lv_tick_set_cb(monotonic_tick);
        s_monotonic_tick = true;
    }
    // Globally disabling LVGL timers makes 9.5's handler return 1 ms, creating
    // a busy loop in esp_lvgl_port. Keep the handler enabled and only pause the
    // display refresh timer; an idle handler can then take its normal long wait.
    lv_timer_enable(true);
    return ESP_OK;
}

esp_err_t bsp_lvgl_sleep(void) {
    if (!s_disp) return ESP_ERR_INVALID_STATE;
    if (s_suspended && s_lcd_sleeping && s_port_power == PORT_STOPPED) return ESP_OK;
    s_suspended = true;
    esp_err_t e = bsp_display_set_backlight(0);
    if (e != ESP_OK) return e;
    e = prepare_hidden_display();
    if (e != ESP_OK) return e;
    e = bsp_display_sleep(true);
    s_lcd_sleeping = e == ESP_OK;
    return e;
}

esp_err_t bsp_lvgl_wake(uint8_t brightness) {
    if (!s_disp) return ESP_ERR_INVALID_STATE;
    if (!s_suspended) return bsp_display_set_backlight(brightness);
    esp_err_t e = bsp_display_set_backlight(0);
    if (e != ESP_OK) return e;
    // Also handles wake after a partial sleep or a failed first clock switch.
    e = prepare_hidden_display();
    if (e != ESP_OK) return e;
    s_lcd_sleeping = false; // SLPOUT can succeed even when DISPON fails.
    e = bsp_display_sleep(false);
    if (e != ESP_OK) return e;
    if (s_refresh_paused) {
        lv_display_enable_invalidation(s_disp, true);
        s_refresh_paused = false;
    }
    lv_timer_resume(lv_display_get_refr_timer(s_disp));
    s_flush_error = ESP_OK;
    lv_obj_invalidate(lv_display_get_screen_active(s_disp));
    lv_refr_now(s_disp);
    // IDF 5.5.3 SPI tx_param drains all queued color DMA before returning.
    // -1/NULL/0 sends no LCD command: it is a completion barrier, since the
    // final asynchronous flush may still be in flight after lv_refr_now.
    e = esp_lcd_panel_io_tx_param(bsp_display_io(), -1, NULL, 0);
    if (s_flush_error != ESP_OK) e = s_flush_error;
    if (e == ESP_OK) e = bsp_display_set_backlight(brightness);
    if (e == ESP_OK) s_suspended = false;
    else pause_refresh();
    return e;
}
