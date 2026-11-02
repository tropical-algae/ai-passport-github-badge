#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Typed platform boundary for running the real badge application lifecycle. */
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERROR_CHECK(value) test_error_check(value)
void test_error_check(esp_err_t error);
const char *esp_err_to_name(esp_err_t error);
void test_log(const char *tag, const char *format, ...);
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define CONFIG_ESP_MAIN_TASK_STACK_SIZE 6144

typedef int BaseType_t;
typedef unsigned TickType_t;
typedef struct test_queue *QueueHandle_t;
typedef struct test_semaphore *SemaphoreHandle_t;
typedef struct test_task *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(value) (value)
QueueHandle_t xQueueCreate(unsigned length, size_t item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
BaseType_t xQueueReset(QueueHandle_t queue);
unsigned uxQueueMessagesWaiting(QueueHandle_t queue);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, unsigned stack,
                       void *context, unsigned priority, TaskHandle_t *handle);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task);
int64_t esp_timer_get_time(void);
#define MALLOC_CAP_8BIT 1
size_t esp_get_free_heap_size(void);
size_t heap_caps_get_largest_free_block(unsigned caps);

typedef enum { BSP_BTN_UP, BSP_BTN_DOWN, BSP_BTN_OK } bsp_btn_t;
typedef enum { BSP_BTN_PRESS, BSP_BTN_CLICK, BSP_BTN_DOUBLE, BSP_BTN_LONG, BSP_BTN_RELEASE } bsp_btn_ev_t;
typedef void (*bsp_btn_cb_t)(bsp_btn_t key, bsp_btn_ev_t event, void *context);
esp_err_t bsp_button_init(bsp_btn_cb_t callback, void *context);
esp_err_t bsp_i2c_init(void);
esp_err_t bsp_display_init(void);
void *bsp_lvgl_init(void);
bool bsp_lvgl_lock(int timeout);
void bsp_lvgl_unlock(void);
void bsp_display_backlight(uint8_t percent);
esp_err_t bsp_battery_init(void);
int bsp_battery_soc(void);
