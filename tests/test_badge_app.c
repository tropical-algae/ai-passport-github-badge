#define _POSIX_C_SOURCE 200809L
#include "app_stubs/app_platform.h"
#include "badge_ui_state.h"
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static time_t test_time(time_t *destination);
#define time test_time
#include "../main/badge_app.c"
#undef time

struct test_queue {
    unsigned length, count;
    size_t item_size;
    unsigned char items[16][512];
};
struct test_semaphore { bool held; };
struct test_task { TaskFunction_t entry; void *context; bool alive; };
static struct test_queue test_queues[2];
static struct test_semaphore test_sems[2];
static struct test_task test_worker;
static unsigned test_queue_count, test_sem_count, test_task_creates;
static unsigned test_cancel_calls, test_ap_starts, test_ap_stops, test_saves;
static unsigned test_data_saves, test_backlight_writes, test_sync_calls;
static unsigned test_lvgl_locks, test_steps, test_step_index;
static bool test_ap_active, test_lvgl_held;
static unsigned test_stop_queued, test_start_queued;
static bool test_ap_desired, test_setup_pending, test_stop_failure, test_start_failure;
static bool test_config_ack_saved, test_notice_error;
static uint32_t test_ap_session, test_ready_session, test_notice_id;
static char test_notice[96];
static bool test_running_app, test_running_worker, test_task_failure;
static int64_t test_monotonic;
static time_t test_epoch;
static uint8_t test_backlight;
static badge_ui_state_t test_ui;
static badge_config_t test_saved_config;
static char test_notification[128];
static char test_avatar_path[64];
static unsigned test_avatar_decodes;
static bool test_avatar_failure;
static bool test_fail_next_lvgl_lock, test_finish_on_status, test_save_failure;
static bool test_backup_on_wait, test_backup_admitted;
static unsigned test_screen_sleeps, test_screen_wakes;
static bool test_fail_screen_sleep, test_fail_screen_wake;
static void finish_worker(void);
static jmp_buf test_app_return, test_worker_return;
static void (*test_step_hook)(unsigned completed_steps);

static time_t test_time(time_t *destination)
{
    if (destination) *destination = test_epoch;
    return test_epoch;
}
int64_t esp_timer_get_time(void) { return test_monotonic; }
void test_error_check(esp_err_t error) { assert(error == ESP_OK); }
const char *esp_err_to_name(esp_err_t error) { return error == ESP_OK ? "OK" : "FAIL"; }
void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
size_t esp_get_free_heap_size(void) { return 100000; }
size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 50000; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 2048; }

QueueHandle_t xQueueCreate(unsigned length, size_t item_size)
{
    assert(test_queue_count < 2 && length <= 16 && item_size <= 512);
    QueueHandle_t queue = &test_queues[test_queue_count++];
    *queue = (struct test_queue){.length = length, .item_size = item_size};
    return queue;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks)
{
    (void)ticks;
    if (queue->count == queue->length) return pdFALSE;
    memcpy(queue->items[queue->count++], item, queue->item_size);
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks)
{
    if (queue == s_keys && ticks) {
        assert(!s_control_lock->held); /* Waiting for input must admit HTTP work. */
        if (test_backup_on_wait) {
            test_backup_on_wait = false;
            /* Simulate the HTTP task running while the app is blocked. Do not
             * count the HTTP mutex release as an application loop iteration. */
            bool was_running = test_running_app;
            test_running_app = false;
            char error[64];
            test_backup_admitted = backup_begin(error, sizeof(error));
            test_running_app = was_running;
        }
    }
    if (!queue->count) return pdFALSE;
    memcpy(item, queue->items[0], queue->item_size);
    --queue->count;
    memmove(queue->items, queue->items + 1, queue->count * sizeof(queue->items[0]));
    return pdTRUE;
}
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item)
{
    assert(queue->length == 1);
    memcpy(queue->items[0], item, queue->item_size);
    queue->count = 1;
    return pdTRUE;
}
BaseType_t xQueueReset(QueueHandle_t queue) { queue->count = 0; return pdPASS; }
unsigned uxQueueMessagesWaiting(QueueHandle_t queue) { return queue->count; }
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    assert(test_sem_count < 2);
    return &test_sems[test_sem_count++];
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks)
{
    (void)ticks;
    assert(sem && !sem->held);
    sem->held = true;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem)
{
    assert(sem && sem->held);
    sem->held = false;
    if (sem == s_control_lock && test_running_app) {
        ++test_step_index;
        if (test_step_hook) test_step_hook(test_step_index);
        if (!--test_steps) longjmp(test_app_return, 1);
    }
    return pdTRUE;
}
BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, unsigned stack,
                       void *context, unsigned priority, TaskHandle_t *handle)
{
    (void)stack; (void)priority;
    assert(!strcmp(name, "badge_sync"));
    assert(!test_ap_active && !test_start_queued);
    if (test_task_failure) return pdFALSE;
    assert(!test_worker.alive);
    test_worker = (struct test_task){entry, context, true};
    ++test_task_creates;
    if (handle) *handle = &test_worker;
    return pdPASS;
}
void vTaskDelete(TaskHandle_t task)
{
    assert(!task && test_running_worker);
    test_worker.alive = false;
    longjmp(test_worker_return, 1);
}
void vTaskDelay(TickType_t ticks) { test_monotonic += (int64_t)ticks * 1000; }

esp_err_t bsp_button_init(bsp_btn_cb_t callback, void *context) { (void)callback; (void)context; return ESP_OK; }
esp_err_t bsp_i2c_init(void) { return ESP_OK; }
esp_err_t bsp_display_init(void) { return ESP_OK; }
void *bsp_lvgl_init(void) { return &test_ui; }
bool bsp_lvgl_lock(int timeout)
{
    (void)timeout;
    assert(!test_lvgl_held);
    if (test_fail_next_lvgl_lock) { test_fail_next_lvgl_lock = false; return false; }
    test_lvgl_held = true;
    ++test_lvgl_locks;
    return true;
}
void bsp_lvgl_unlock(void) { assert(test_lvgl_held); test_lvgl_held = false; }
void bsp_display_backlight(uint8_t percent)
{
    test_backlight = percent;
    ++test_backlight_writes;
}
esp_err_t bsp_lvgl_sleep(void)
{
    assert(test_lvgl_held);
    ++test_screen_sleeps;
    bsp_display_backlight(0); // A failed transition can already be dark.
    return test_fail_screen_sleep ? ESP_FAIL : ESP_OK;
}
esp_err_t bsp_lvgl_wake(uint8_t brightness)
{
    assert(test_lvgl_held);
    ++test_screen_wakes;
    if (test_fail_screen_wake) return ESP_FAIL;
    bsp_display_backlight(brightness);
    return ESP_OK;
}
esp_err_t bsp_battery_init(void) { return ESP_OK; }
int bsp_battery_soc(void) { return 73; }

void badge_ui_init(const badge_data_t *data, const badge_config_t *config, const badge_status_t *status)
{ (void)data; (void)config; (void)status; assert(test_lvgl_held); memset(&test_ui, 0, sizeof(test_ui)); }
void badge_ui_update(const badge_data_t *data, const badge_config_t *config, const badge_status_t *status)
{ (void)data; (void)config; (void)status; assert(test_lvgl_held); }
void badge_ui_tick(const badge_status_t *status) { (void)status; assert(test_lvgl_held); }
badge_action_t badge_ui_key(badge_key_t key, bool long_press)
{
    assert(test_lvgl_held);
    return badge_ui_state_key(&test_ui, key, long_press, &s_data, &s_config);
}
void badge_ui_show_provision(void) { assert(test_lvgl_held); test_ui.modal = BADGE_MODAL_WIFI; }
void badge_ui_close_provision(void) { assert(test_lvgl_held); test_ui.modal = BADGE_MODAL_NONE; }
void badge_ui_notify(const char *message, bool error)
{
    (void)error;
    assert(test_lvgl_held);
    snprintf(test_notification, sizeof(test_notification), "%s", message ? message : "");
}
bool badge_ui_set_avatar(const void *bytes, size_t size)
{
    assert(test_lvgl_held);
    if (!bytes || !size) return false;
    assert(!atomic_load(&s_syncing) && !test_ap_active && !test_start_queued);
    ++test_avatar_decodes;
    return !test_avatar_failure;
}

esp_err_t badge_storage_init(void) { return ESP_OK; }
bool badge_config_load(badge_config_t *config) { badge_config_defaults(config); return true; }
bool badge_config_save(const badge_config_t *config)
{
    if (!test_save_failure) test_saved_config = *config;
    ++test_saves;
    return !test_save_failure;
}
bool badge_data_load(const char *username, badge_data_t *data) { (void)username; (void)data; return false; }
bool badge_data_save(const badge_data_t *data) { assert(badge_data_valid(data)); ++test_data_saves; return true; }
bool badge_cache_path(const char *username, const char *suffix, char *out, size_t size)
{
    (void)username;
    if (strcmp(suffix, "avatar") || !test_avatar_path[0]) return false;
    return snprintf(out, size, "%s", test_avatar_path) < (int)size;
}
void badge_backup_init(badge_backup_begin_fn begin, badge_backup_end_fn end) { (void)begin; (void)end; }

esp_err_t badge_network_init(const badge_config_t *config, badge_network_config_cb_t callback, void *context)
{ (void)config; (void)callback; (void)context; return ESP_OK; }
esp_err_t badge_network_provision_start(void)
{
    assert(!atomic_load(&s_syncing) && !test_worker.alive);
    if (test_start_failure) return ESP_FAIL;
    if (!test_ap_desired) ++test_ap_session;
    test_ap_desired = true;
    test_start_queued = 1;
    test_stop_queued = 0;
    ++test_ap_starts;
    return ESP_OK;
}
esp_err_t badge_network_provision_stop(void)
{
    if (test_stop_failure) return ESP_FAIL;
    test_ap_desired = false;
    test_stop_queued = 1;
    test_start_queued = 0;
    ++test_ap_stops;
    return ESP_OK;
}
esp_err_t badge_network_config_result(bool saved)
{ test_config_ack_saved = saved; return ESP_OK; }
void badge_network_status(badge_status_t *status)
{
    if (test_finish_on_status) { test_finish_on_status = false; finish_worker(); }
    status->connected = true;
    status->provisioning = test_ap_active;
    status->provisioning_busy = test_start_queued || test_stop_queued;
    status->provisioning_session = test_ap_session;
    status->provisioning_ready_session = test_ready_session;
    status->setup_pending = test_setup_pending;
    status->network_notice_id = test_notice_id;
    status->network_notice_error = test_notice_error;
    snprintf(status->network_notice, sizeof(status->network_notice), "%s", test_notice);
    status->setup_saved = false;
}
void badge_sync_cancel(void) { ++test_cancel_calls; }
esp_err_t badge_sync_run_request(const badge_config_t *config, badge_data_t *data,
                                 badge_sync_publish_t publish_data, void *context,
                                 bool manual, badge_sync_result_t *result)
{
    (void)manual;
    assert(test_running_worker && atomic_load(&s_syncing));
    assert(!test_ap_active && !test_start_queued);
    assert(!strcmp(config->username, data->username));
    ++test_sync_calls;
    data->last_attempt = data->last_success = test_epoch;
    data->retry_after = 0;
    snprintf(data->sync_message, sizeof(data->sync_message), "SYNC COMPLETE");
    memset(result, 0, sizeof(*result));
    snprintf(result->detail, sizeof(result->detail), "SYNC COMPLETE");
    assert(publish_data(data, context));
    return ESP_OK;
}

static void reset_fixture(void)
{
    assert(!test_lvgl_held);
    s_avatar_size = 0; s_avatar_hash = 0; s_avatar_decoded = s_avatar_pending = false;
    s_avatar_account[0] = 0;
    if (test_avatar_path[0]) unlink(test_avatar_path);
    test_avatar_path[0] = 0; test_avatar_decodes = 0; test_avatar_failure = false;
    test_fail_next_lvgl_lock = test_finish_on_status = test_save_failure = false;
    test_backup_on_wait = test_backup_admitted = false;
    test_screen_sleeps = test_screen_wakes = 0;
    test_fail_screen_sleep = test_fail_screen_wake = false;
    s_screen_blocked = s_screen_asleep = false;
    s_screen_retry_at = 0;
    memset(test_queues, 0, sizeof(test_queues));
    memset(test_sems, 0, sizeof(test_sems));
    memset(&test_worker, 0, sizeof(test_worker));
    memset(&test_ui, 0, sizeof(test_ui));
    memset(&s_status, 0, sizeof(s_status));
    memset(&s_sync_result, 0, sizeof(s_sync_result));
    test_queue_count = test_sem_count = test_task_creates = test_cancel_calls = 0;
    test_ap_starts = test_ap_stops = test_saves = test_data_saves = test_sync_calls = 0;
    test_backlight_writes = test_lvgl_locks = 0;
    test_ap_active = false;
    test_ap_desired = test_setup_pending = test_stop_failure = test_start_failure = false;
    test_config_ack_saved = test_notice_error = false;
    test_ap_session = test_ready_session = test_notice_id = 0;
    test_notice[0] = 0;
    test_stop_queued = 0;
    test_start_queued = 0;
    test_task_failure = false;
    test_running_app = test_running_worker = false;
    test_step_hook = NULL;
    test_notification[0] = 0;
    test_monotonic = 1000000;
    badge_idle_init(&s_idle, test_monotonic);
    test_epoch = 1800000000;
    badge_config_defaults(&s_config);
    snprintf(s_config.username, sizeof(s_config.username), "octocat");
    snprintf(s_config.ssid, sizeof(s_config.ssid), "test-wifi");
    badge_data_reset(&s_data, s_config.username);
    s_data.last_attempt = s_data.last_success = test_epoch;
    assert(badge_config_valid(&s_config));
    s_status.connected = true;
    s_status.now = test_epoch;
    s_storage_ok = true;
    s_mail_ready = false;
    s_retry_not_before = 0;
    s_config_ack_pending = s_config_ack_saved = false;
    s_provision_pending = s_provision_waiting = s_manual_pending = false;
    s_provision_open = s_provision_stop_pending = false;
    s_provision_session = s_network_notice_id = 0;
    s_provision_retry_at = 0;
    atomic_store(&s_syncing, false); atomic_store(&s_key_overflow, false);
    atomic_store(&s_maintenance, false); atomic_store(&s_result_ready, false);
    s_keys = xQueueCreate(16, sizeof(key_event_t));
    s_configs = xQueueCreate(1, sizeof(badge_config_t));
    s_mail_lock = xSemaphoreCreateMutex();
    s_control_lock = xSemaphoreCreateMutex();
    test_backlight = s_config.brightness;
}
static void press(badge_key_t key, bool long_press)
{
    perform_action((badge_input_action_t){key, long_press, true});
}
static void choose_setting(unsigned focus)
{
    assert(test_ui.modal == BADGE_MODAL_NONE);
    test_ui.settings = BADGE_SETTINGS_MAIN;
    test_ui.focus = (uint8_t)focus;
    press(BADGE_KEY_OK, false);
}
static void run_app(unsigned steps, void (*hook)(unsigned))
{
    test_steps = steps; test_step_index = 0; test_step_hook = hook;
    test_running_app = true;
    if (!setjmp(test_app_return)) app_task(NULL);
    test_running_app = false; test_step_hook = NULL;
    assert(!test_lvgl_held && !s_control_lock->held && !s_mail_lock->held);
}
static void finish_worker(void)
{
    assert(test_worker.alive && atomic_load(&s_syncing));
    test_running_worker = true;
    if (!setjmp(test_worker_return)) test_worker.entry(test_worker.context);
    test_running_worker = false;
    assert(!test_worker.alive && !atomic_load(&s_syncing) && atomic_load(&s_result_ready));
}
static void process_network_start(void)
{
    assert(test_start_queued && !atomic_load(&s_syncing));
    --test_start_queued;
    test_ap_active = true;
    test_ready_session = test_ap_session;
}
static void process_network_stop(void)
{
    assert(test_stop_queued);
    --test_stop_queued;
    test_ap_active = false;
    test_ready_session = 0;
}
static void process_network_commands(void)
{
    while (test_start_queued) process_network_start();
    while (test_stop_queued) process_network_stop();
}

static void navigation_keeps_fetch_running(void)
{
    reset_fixture();
    choose_setting(4); /* REFETCH DATA */
    assert(atomic_load(&s_syncing) && test_task_creates == 1 && s_job_manual);
    press(BADGE_KEY_OK, true); /* exit settings */
    assert(test_ui.settings == BADGE_SETTINGS_CLOSED);
    press(BADGE_KEY_DOWN, true); /* next page */
    assert(test_ui.page == 1 && atomic_load(&s_syncing));
    choose_setting(0); test_ui.focus = 1; press(BADGE_KEY_OK, false);
    assert(s_config.brightness == 80 && test_backlight == 80);
    choose_setting(2); test_ui.focus = 2; press(BADGE_KEY_OK, false);
    assert(s_config.theme == 2);
    choose_setting(1); test_ui.focus = 1; press(BADGE_KEY_OK, false);
    assert(s_config.screen_timeout_min == 5);
    assert(s_job_config.brightness == 100 && s_job_config.theme == 0 && s_job_config.screen_timeout_min == 10);
    assert(atomic_load(&s_syncing) && !test_cancel_calls && test_task_creates == 1);

    choose_setting(3); /* Wi-Fi setup waits for the background fetch. */
    assert(s_provision_pending && !test_cancel_calls && !test_ap_starts);
    run_app(1, NULL);
    assert(atomic_load(&s_syncing) && !test_ap_starts && s_provision_pending);
    press(BADGE_KEY_OK, false); /* closing queued setup does not stop the fetch */
    assert(!s_provision_pending && !test_cancel_calls && atomic_load(&s_syncing));
    process_network_commands();
    run_app(1, NULL);
    assert(!test_ap_starts && atomic_load(&s_syncing));

    choose_setting(3);
    finish_worker();
    run_app(1, NULL);
    assert(test_ap_starts == 1 && s_provision_waiting && !test_cancel_calls);
    assert(!atomic_load(&s_result_ready) && test_data_saves == 2);
    assert(strstr(test_notification, "FETCH OK"));
    process_network_commands();
    run_app(1, NULL);
    assert(test_ap_active && !s_provision_waiting && test_task_creates == 1);
}

static void queued_fetch_survives_settings_exit(void)
{
    reset_fixture();
    test_ap_active = s_status.provisioning = true;
    choose_setting(4);
    assert(s_manual_pending && test_stop_queued && !test_task_creates);
    press(BADGE_KEY_OK, true);
    assert(test_ui.settings == BADGE_SETTINGS_CLOSED && s_manual_pending);
    run_app(1, NULL);
    assert(s_manual_pending && !test_task_creates && !test_cancel_calls);
    process_network_commands();
    run_app(1, NULL);
    assert(!s_manual_pending && atomic_load(&s_syncing) && test_task_creates == 1);
    assert(s_job_manual && !test_cancel_calls);
    finish_worker();
    run_app(1, NULL);
    assert(!atomic_load(&s_result_ready) && strstr(test_notification, "FETCH OK"));
}

static void config_update_keeps_local_preferences(void)
{
    reset_fixture();
    s_config.theme = 3; s_config.brightness = 40; s_config.screen_timeout_min = 30;
    start_sync(true);
    badge_config_t changed = s_config;
    snprintf(changed.username, sizeof(changed.username), "OCTOCAT");
    changed.interval_sec = 43200; changed.theme = 0; changed.brightness = 100; changed.screen_timeout_min = 10;
    on_config(&changed, NULL);
    run_app(1, NULL);
    assert(!test_cancel_calls && atomic_load(&s_syncing));
    assert(s_config.interval_sec == 43200 && s_job_config.interval_sec == BADGE_DEFAULT_INTERVAL);
    assert(s_config.theme == 3 && s_config.brightness == 40 && s_config.screen_timeout_min == 30);
    assert(test_saved_config.screen_timeout_min == 30);
    snprintf(changed.username, sizeof(changed.username), "another-user");
    on_config(&changed, NULL);
    run_app(1, NULL);
    assert(test_cancel_calls == 1 && !strcmp(s_config.username, "another-user"));
    assert(!strcmp(s_job_config.username, "octocat"));
}

static void fetch_waits_for_queued_ap_commands(void)
{
    reset_fixture();
    choose_setting(3);
    run_app(1, NULL);
    assert(test_start_queued == 1 && !test_ap_active && !test_task_creates);
    press(BADGE_KEY_OK, false); /* Latest close supersedes an unprocessed open. */
    choose_setting(4);
    assert(test_stop_queued == 1 && !test_start_queued && s_manual_pending);
    press(BADGE_KEY_OK, true);
    run_app(1, NULL);
    assert(s_manual_pending && !test_task_creates);
    process_network_commands();
    run_app(1, NULL);
    assert(!s_manual_pending && atomic_load(&s_syncing) && test_task_creates == 1 && s_job_manual);
    assert(!test_cancel_calls);
    finish_worker();
    run_app(1, NULL);

    reset_fixture();
    s_data.last_attempt = s_data.last_success = 0;
    choose_setting(3);
    run_app(1, NULL);
    assert(test_start_queued == 1 && !test_task_creates);
    press(BADGE_KEY_OK, false);
    run_app(1, NULL);
    assert(test_stop_queued == 1 && !test_start_queued && !test_task_creates);
    process_network_commands();
    run_app(1, NULL);
    assert(test_task_creates == 1 && !s_job_manual && !test_cancel_calls);
}

static void setup_close_preserves_submission_and_failed_save(void)
{
    reset_fixture();
    choose_setting(3);
    run_app(1, NULL);
    process_network_commands();
    run_app(1, NULL);
    test_setup_pending = true;
    press(BADGE_KEY_OK, false);
    assert(!s_provision_open && test_setup_pending && !test_cancel_calls);
    process_network_commands();
    s_data.last_attempt = s_data.last_success = 0;
    test_monotonic += 1000000;
    run_app(1, NULL);
    assert(!test_task_creates && test_setup_pending);
    badge_config_t changed = s_config;
    snprintf(changed.username, sizeof(changed.username), "new-account");
    test_save_failure = true;
    on_config(&changed, NULL);
    run_app(1, NULL);
    assert(!test_config_ack_saved && !strcmp(s_config.username, "octocat"));
    assert(!strcmp(s_data.username, "octocat") && !test_cancel_calls);
    test_setup_pending = false;
    test_notice_id = 1; test_notice_error = true;
    snprintf(test_notice, sizeof(test_notice), "CONFIG NOT SAVED");
    run_app(1, NULL);
    assert(strstr(test_notification, "CONFIG NOT SAVED"));
    assert(test_ui.modal == BADGE_MODAL_NONE);
}

static void setup_awake_hook(unsigned step)
{
    if (step == 1) {
        assert(s_provision_open && !s_idle.asleep);
        test_monotonic += 120000000;
    } else if (step == 2) {
        assert(s_provision_open && !s_idle.asleep);
        test_notice_id = 1;
        snprintf(test_notice, sizeof(test_notice), "WIFI CONNECTED / SAVED");
    } else if (step == 3) {
        assert(test_ui.modal == BADGE_MODAL_WIFI && test_ap_active);
        assert(strstr(test_notification, "SAVED"));
        test_ap_active = test_ap_desired = false; test_ready_session = 0;
    } else if (step == 4) {
        assert(!s_provision_open && test_ui.modal == BADGE_MODAL_NONE && !s_idle.asleep);
        test_monotonic += 59999999;
    } else if (step == 5) {
        assert(!s_idle.asleep);
        ++test_monotonic;
    } else if (step == 6) assert(s_idle.asleep);
}
static void setup_stays_awake_until_actual_close(void)
{
    reset_fixture();
    s_config.screen_timeout_min = 1;
    choose_setting(3);
    run_app(1, NULL);
    process_network_commands();
    run_app(6, setup_awake_hook);
}

static void reopened_setup_keeps_pending_fetch_and_retries_stop(void)
{
    reset_fixture();
    test_ap_active = true;
    choose_setting(4);
    assert(s_manual_pending);
    choose_setting(3);
    run_app(1, NULL);
    process_network_commands();
    run_app(1, NULL);
    assert(s_provision_open && s_manual_pending && !test_task_creates);
    test_stop_failure = true;
    press(BADGE_KEY_OK, false);
    assert(s_provision_stop_pending && test_ap_active && s_manual_pending);
    test_stop_failure = false; test_monotonic += 1000000;
    run_app(1, NULL);
    assert(!s_provision_stop_pending && test_stop_queued && !test_task_creates);
    process_network_commands();
    run_app(1, NULL);
    assert(atomic_load(&s_syncing) && !s_manual_pending && !test_cancel_calls);
    finish_worker();
    run_app(1, NULL);
}

static void backup_close_hook(unsigned step)
{
    if (step == 1) {
        on_key(BSP_BTN_UP, BSP_BTN_PRESS, NULL);
    } else if (step == 2) {
        /* The same app_task input state still holds UP when maintenance starts. */
        atomic_store(&s_maintenance, true);
        on_key(BSP_BTN_UP, BSP_BTN_RELEASE, NULL);
        on_key(BSP_BTN_OK, BSP_BTN_PRESS, NULL);
        test_monotonic += 100000;
        on_key(BSP_BTN_OK, BSP_BTN_RELEASE, NULL);
    } else if (step == 3) {
        assert(!s_provision_open && test_ui.modal == BADGE_MODAL_NONE);
        assert(test_stop_queued && atomic_load(&s_maintenance));
        assert(!test_cancel_calls);
    }
}
static void setup_can_close_during_backup(void)
{
    reset_fixture();
    choose_setting(3);
    run_app(1, NULL);
    process_network_commands();
    run_app(3, backup_close_hook);
    backup_end(false);
    process_network_commands();
    run_app(1, NULL);
}

static void idle_hook(unsigned step)
{
    if (step == 1) {
        assert(!s_idle.asleep && atomic_load(&s_syncing));
        test_monotonic += 60000000LL;
    } else if (step == 2) {
        assert(s_idle.asleep && test_backlight == 0 && atomic_load(&s_syncing));
        assert(!test_cancel_calls && test_task_creates == 1);
        finish_worker(); /* completion publishes while the display stays off */
    } else if (step == 3) {
        assert(s_idle.asleep && test_backlight == 0 && !atomic_load(&s_result_ready));
        assert(test_ui.settings == BADGE_SETTINGS_MAIN);
        on_key(BSP_BTN_OK, BSP_BTN_PRESS, NULL);
        test_monotonic += BADGE_HOLD_US + 1;
    } else if (step == 4) {
        assert(!s_idle.asleep && test_backlight == 60);
        assert(test_ui.settings == BADGE_SETTINGS_MAIN); /* wake hold is consumed */
        on_key(BSP_BTN_OK, BSP_BTN_RELEASE, NULL);
    } else if (step == 5) {
        assert(test_ui.settings == BADGE_SETTINGS_MAIN && !test_cancel_calls);
        on_key(BSP_BTN_OK, BSP_BTN_PRESS, NULL);
        test_monotonic += BADGE_HOLD_US + 1;
        on_key(BSP_BTN_OK, BSP_BTN_RELEASE, NULL);
    } else if (step == 6) {
        assert(test_ui.settings == BADGE_SETTINGS_CLOSED);
        assert(!s_idle.asleep && test_backlight == 60 && test_sync_calls == 1);
    }
}
static void screen_off_preserves_fetch_and_consumes_wake(void)
{
    reset_fixture();
    s_config.screen_timeout_min = 1; s_config.brightness = 60;
    test_backlight = 60;
    choose_setting(4);
    run_app(6, idle_hook);
    assert(test_backlight_writes == 2 && !test_cancel_calls);
    assert(test_screen_sleeps == 1 && test_screen_wakes == 1 && !s_screen_blocked);
}

static void screen_power_retries_partial_failures(void)
{
    reset_fixture();
    choose_setting(4); // A running worker must survive all display retries.
    s_idle.asleep = true;
    test_fail_screen_sleep = true;
    service_screen(test_monotonic);
    assert(s_screen_blocked && !s_screen_asleep && test_screen_sleeps == 1);
    press(BADGE_KEY_OK, true);
    assert(test_ui.settings == BADGE_SETTINGS_MAIN);
    service_screen(test_monotonic + 999999);
    assert(test_screen_sleeps == 1);
    test_monotonic += 1000000;
    test_fail_screen_sleep = false;
    service_screen(test_monotonic);
    assert(s_screen_asleep && test_screen_sleeps == 2);
    service_screen(test_monotonic + 1000000);
    assert(test_screen_sleeps == 2); // Stable sleep needs no repeated commands.
    s_idle.asleep = false;
    test_fail_next_lvgl_lock = true;
    service_screen(test_monotonic);
    assert(s_screen_blocked && !test_screen_wakes);
    test_monotonic += 1000000;
    test_fail_screen_wake = true;
    service_screen(test_monotonic);
    assert(s_screen_blocked && !s_screen_asleep && test_screen_wakes == 1);
    service_screen(test_monotonic + 999999);
    assert(test_screen_wakes == 1);
    // If a failed partial wake times out again, it must re-issue sleep.
    s_idle.asleep = true;
    test_monotonic += 60000000;
    service_screen(test_monotonic);
    assert(s_screen_asleep && test_screen_sleeps == 3);
    s_idle.asleep = false; test_fail_screen_wake = false;
    service_screen(test_monotonic);
    assert(!s_screen_asleep && !s_screen_blocked && test_screen_wakes == 2);
    assert(test_worker.alive && atomic_load(&s_syncing) && !test_cancel_calls);
}

static void failed_wake_hook(unsigned step)
{
    assert(test_ui.settings == BADGE_SETTINGS_MAIN);
    assert(test_worker.alive && atomic_load(&s_syncing) && !test_cancel_calls);
    if (step == 1) {
        test_monotonic += 60000000;
    } else if (step == 2) {
        assert(s_screen_asleep && test_screen_sleeps == 1);
        test_fail_screen_wake = true;
        on_key(BSP_BTN_OK, BSP_BTN_PRESS, NULL);
    } else if (step == 3) {
        assert(s_screen_blocked && test_screen_wakes == 1);
        on_key(BSP_BTN_OK, BSP_BTN_RELEASE, NULL);
        on_key(BSP_BTN_OK, BSP_BTN_PRESS, NULL);
        test_monotonic += BADGE_HOLD_US + 1;
    } else if (step == 4) {
        assert(s_screen_blocked && test_screen_wakes == 1); // Retry is throttled.
        test_fail_screen_wake = false;
        test_monotonic += 1000000;
    } else if (step == 5) {
        assert(!s_screen_blocked && test_screen_wakes == 2 && s_idle.consume_wake);
        on_key(BSP_BTN_OK, BSP_BTN_RELEASE, NULL);
    } else if (step == 6) {
        assert(!s_idle.consume_wake && !s_screen_blocked);
    }
}
static void failed_wake_consumes_further_gestures(void)
{
    reset_fixture(); s_config.screen_timeout_min = 1;
    choose_setting(4);
    run_app(6, failed_wake_hook);
    press(BADGE_KEY_OK, true); // Only a fresh gesture after wake may navigate.
    assert(test_ui.settings == BADGE_SETTINGS_CLOSED);
}

static void avatar_retries_after_worker_releases_memory(void)
{
    reset_fixture();
    snprintf(test_avatar_path, sizeof(test_avatar_path), "/tmp/badge-avatar-XXXXXX");
    int fd = mkstemp(test_avatar_path); assert(fd >= 0);
    assert(write(fd, "image", 5) == 5); close(fd);
    s_data.valid = s_data.complete = BADGE_AVATAR;
    test_avatar_failure = true;
    load_avatar();
    assert(!s_avatar_decoded && test_avatar_decodes == 1);
    start_sync(true);
    test_avatar_failure = false;
    load_avatar();
    assert(s_avatar_pending && test_avatar_decodes == 1); /* no decode during TLS */
    choose_setting(3); /* pending hotspot also must wait for final decoding */
    finish_worker();
    run_app(1, NULL);
    assert(s_avatar_decoded && test_avatar_decodes == 2 && !s_avatar_pending);
    assert(test_start_queued == 1 && strstr(test_notification, "FETCH OK"));
    /* Same successfully decoded bytes are not decoded again. */
    test_start_queued = 0; s_status.provisioning_busy = false;
    load_avatar();
    assert(s_avatar_decoded && test_avatar_decodes == 2);
    /* Changing accounts drops the old avatar immediately, even while busy. */
    snprintf(s_data.username, sizeof(s_data.username), "another-user");
    atomic_store(&s_syncing, true);
    load_avatar();
    assert(!s_avatar_decoded && s_avatar_pending && test_avatar_decodes == 2);
    atomic_store(&s_syncing, false);
    reset_fixture();
}

static void completion_waits_for_final_mailbox(void)
{
    reset_fixture();
    start_sync(true);
    s_data.last_success = 0;
    test_finish_on_status = true; /* Worker finishes after this tick's mailbox read. */
    run_app(1, NULL);
    assert(s_mail_ready && atomic_load(&s_result_ready) && !s_data.last_success);
    assert(!strstr(test_notification, "FETCH OK"));
    test_fail_next_lvgl_lock = true;
    s_provision_pending = true;
    run_app(1, NULL);
    assert(s_mail_ready && atomic_load(&s_result_ready) && !test_ap_starts);
    run_app(1, NULL);
    assert(!s_mail_ready && !atomic_load(&s_result_ready) && s_data.last_success == test_epoch);
    assert(test_ap_starts == 1 && strstr(test_notification, "FETCH OK"));
    reset_fixture();
    start_sync(true);
    finish_worker();
    strcpy(s_config.username, "another-user");
    test_notification[0] = 0;
    run_app(1, NULL);
    assert(!s_mail_ready && !atomic_load(&s_result_ready));
    assert(!strstr(test_notification, "FETCH OK")); /* Old account cannot report new account success. */
}

static void preference_save_failure_is_visible(void)
{
    reset_fixture();
    test_save_failure = true;
    choose_setting(0);
    test_ui.focus = 5;
    press(BADGE_KEY_OK, false);
    assert(s_config.brightness == 10 && test_backlight == 10 && test_ui.settings == BADGE_SETTINGS_MAIN);
    assert(strstr(test_notification, "APPLIED / NOT SAVED"));
    test_save_failure = false;
    choose_setting(0);
    press(BADGE_KEY_OK, false);
    assert(test_saved_config.brightness == 10);
}

static void invalid_clock_does_not_start_fetch(void)
{
    reset_fixture();
    s_status.now = 253402300800LL;
    start_sync(true);
    assert(!test_task_creates && strstr(test_notification, "WAITING FOR NETWORK TIME"));
    s_status.now = 0;
    start_sync(true);
    assert(!test_task_creates);
}

static void backup_can_start_while_waiting_for_input(void)
{
    reset_fixture();
    test_backup_on_wait = true;
    run_app(1, NULL);
    assert(test_backup_admitted && atomic_load(&s_maintenance));
    assert(!test_task_creates && !s_control_lock->held);
    backup_end(false);
    run_app(1, NULL);
    assert(!atomic_load(&s_maintenance));
}

int main(void)
{
    navigation_keeps_fetch_running();
    queued_fetch_survives_settings_exit();
    config_update_keeps_local_preferences();
    fetch_waits_for_queued_ap_commands();
    setup_close_preserves_submission_and_failed_save();
    setup_stays_awake_until_actual_close();
    reopened_setup_keeps_pending_fetch_and_retries_stop();
    setup_can_close_during_backup();
    screen_off_preserves_fetch_and_consumes_wake();
    screen_power_retries_partial_failures();
    failed_wake_consumes_further_gestures();
    avatar_retries_after_worker_releases_memory();
    completion_waits_for_final_mailbox();
    preference_save_failure_is_visible();
    invalid_clock_does_not_start_fetch();
    backup_can_start_while_waiting_for_input();
    puts("badge app background sync, setup deferral, preference and screen-off runtime tests passed");
    return 0;
}
