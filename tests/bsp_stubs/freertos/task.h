// Minimal FreeRTOS delay stub for BSP host tests. The test that includes the
// real BSP implementation provides vTaskDelay(); one simulated tick is 1 ms.
#pragma once

#include <stdint.h>

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

void vTaskDelay(TickType_t ticks);
