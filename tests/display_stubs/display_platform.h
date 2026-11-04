#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
typedef int gpio_num_t;
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
esp_err_t gpio_config(const gpio_config_t *);
esp_err_t gpio_set_level(gpio_num_t, uint32_t);
esp_err_t gpio_hold_en(gpio_num_t);
esp_err_t gpio_hold_dis(gpio_num_t);
void gpio_deep_sleep_hold_en(void);
void gpio_deep_sleep_hold_dis(void);
#define SPI2_HOST 1
#define SPI_DMA_CH_AUTO 3
typedef struct { int mosi_io_num, sclk_io_num, miso_io_num, quadwp_io_num, quadhd_io_num, max_transfer_sz; } spi_bus_config_t;
esp_err_t spi_bus_initialize(int, const spi_bus_config_t *, int);
esp_err_t spi_bus_free(int);
#define LEDC_TIMER_0 0
#define LEDC_LOW_SPEED_MODE 0
#define LEDC_CHANNEL_0 0
#define LEDC_TIMER_10_BIT 10
#define LEDC_AUTO_CLK 0
typedef struct { int speed_mode, timer_num, duty_resolution, freq_hz, clk_cfg; } ledc_timer_config_t;
typedef struct { int gpio_num, speed_mode, channel, timer_sel; uint32_t duty; int hpoint; } ledc_channel_config_t;
esp_err_t ledc_timer_config(const ledc_timer_config_t *);
esp_err_t ledc_channel_config(const ledc_channel_config_t *);
esp_err_t ledc_timer_rst(int, int);
esp_err_t ledc_set_duty(int, int, uint32_t);
esp_err_t ledc_update_duty(int, int);
esp_err_t ledc_stop(int, int, uint32_t);
typedef void *esp_lcd_spi_bus_handle_t;
typedef struct { int cs_gpio_num, dc_gpio_num, pclk_hz, spi_mode, lcd_cmd_bits, lcd_param_bits, trans_queue_depth; } esp_lcd_panel_io_spi_config_t;
typedef struct { int reset_gpio_num, rgb_ele_order, bits_per_pixel; } esp_lcd_panel_dev_config_t;
#define LCD_RGB_ELEMENT_ORDER_RGB 0
esp_err_t esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t, const esp_lcd_panel_io_spi_config_t *, esp_lcd_panel_io_handle_t *);
esp_err_t esp_lcd_new_panel_st7789(esp_lcd_panel_io_handle_t, const esp_lcd_panel_dev_config_t *, esp_lcd_panel_handle_t *);
esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_invert_color(esp_lcd_panel_handle_t, bool);
esp_err_t esp_lcd_panel_mirror(esp_lcd_panel_handle_t, bool, bool);
esp_err_t esp_lcd_panel_set_gap(esp_lcd_panel_handle_t, int, int);
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t, bool);
esp_err_t esp_lcd_panel_disp_sleep(esp_lcd_panel_handle_t, bool);
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t);
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t, int, const void *, size_t);
