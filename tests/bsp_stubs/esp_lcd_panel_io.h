#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t, int, const void *, size_t);
