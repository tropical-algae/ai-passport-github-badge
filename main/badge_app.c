#include "badge_model.h"
#include "badge_input.h"
#include "badge_idle.h"
#include "badge_storage.h"
#include "badge_backup.h"
#include "badge_network.h"
#include "badge_sync.h"
#include "badge_ui.h"
#include "badge_ui_avatar.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const char *TAG = "github_badge";
typedef struct { bsp_btn_t key; bsp_btn_ev_t event; int64_t at; } key_event_t;
static QueueHandle_t s_keys, s_configs;
static SemaphoreHandle_t s_mail_lock, s_control_lock;
static badge_config_t s_config, s_job_config;
static bool s_job_manual;
static badge_data_t s_data, s_mail, s_job_data;
static badge_status_t s_status;
static atomic_bool s_syncing, s_key_overflow, s_maintenance, s_result_ready;
static badge_sync_result_t s_sync_result;
static esp_err_t s_sync_error;
static bool s_mail_ready, s_storage_ok;
static size_t s_avatar_size;
static uint64_t s_avatar_hash;
static bool s_avatar_decoded, s_avatar_pending;
static char s_avatar_account[40];
static int64_t s_retry_not_before;
static bool s_config_ack_pending, s_config_ack_saved;
static bool s_provision_pending, s_provision_waiting, s_manual_pending;
static bool s_provision_open, s_provision_stop_pending;
static uint32_t s_provision_session, s_network_notice_id;
static int64_t s_provision_retry_at;
static badge_idle_t s_idle;
/* idle.asleep is the requested state. Hardware completion is tracked separately
 * so a failed/partial transition cannot admit keys to an invisible screen. */
static bool s_screen_blocked, s_screen_asleep;
static int64_t s_screen_retry_at;

static void service_screen(int64_t now) {
    bool sleep = s_idle.asleep;
    if ((sleep && s_screen_asleep) || (!sleep && !s_screen_blocked)) return;
    s_screen_blocked = true;
    if (now < s_screen_retry_at) return;
    s_screen_retry_at = now + 1000000;
    if (!bsp_lvgl_lock(100)) {
        ESP_LOGW(TAG, "Screen %s waiting for LVGL lock", sleep ? "sleep" : "wake");
        return;
    }
    s_screen_asleep = false; // A failed wake may already have issued SLPOUT.
    esp_err_t e = sleep ? bsp_lvgl_sleep() : bsp_lvgl_wake(s_config.brightness);
    bsp_lvgl_unlock();
    if (e == ESP_OK) {
        s_screen_asleep = sleep;
        s_screen_blocked = sleep;
        s_screen_retry_at = 0;
    } else {
        ESP_LOGW(TAG, "Screen %s failed: %s; retrying", sleep ? "sleep" : "wake", esp_err_to_name(e));
    }
}

static bool notify(const char *message, bool error) {
    if (bsp_lvgl_lock(1000)) {
        badge_ui_notify(message, error);
        bsp_lvgl_unlock();
        return true;
    }
    return false;
}
static bool backup_begin(char *error, size_t size) {
    if (!s_control_lock || xSemaphoreTake(s_control_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        snprintf(error, size, "DEVICE BUSY / RETRY"); return false;
    }
    bool ok = s_storage_ok && !atomic_load(&s_syncing) && !atomic_load(&s_maintenance) &&
        !s_config_ack_pending && uxQueueMessagesWaiting(s_configs) == 0;
    if (ok) atomic_store(&s_maintenance, true);
    else snprintf(error, size, "%s", s_storage_ok ? "DEVICE BUSY / RETRY" : "CACHE STORAGE UNAVAILABLE");
    xSemaphoreGive(s_control_lock);
    return ok;
}
static void backup_end(bool restored) {
    /* A committed restore is activated on restart. Keep the old app state
     * frozen until the network task has acknowledged the upload and rebooted. */
    if (!restored) atomic_store(&s_maintenance, false);
}

static void on_key(bsp_btn_t key, bsp_btn_ev_t event, void *context) {
    (void)context;
    if (!s_keys || (event != BSP_BTN_PRESS && event != BSP_BTN_RELEASE)) return;
    key_event_t message = {key, event, esp_timer_get_time()};
    if (xQueueSend(s_keys, &message, 0) != pdTRUE) atomic_store(&s_key_overflow, true);
}
static void on_config(const badge_config_t *config, void *context) {
    (void)context;
    xQueueOverwrite(s_configs, config);
}
static bool publish(const badge_data_t *data, void *context) {
    (void)context;
    bool saved = s_storage_ok && badge_data_save(data);
    if (!saved) ESP_LOGW(TAG, "Snapshot save failed");
    xSemaphoreTake(s_mail_lock, portMAX_DELAY);
    s_mail = *data;
    if (!saved) snprintf(s_mail.sync_message, sizeof(s_mail.sync_message), "CACHE NOT SAVED / STORAGE ERROR");
    s_mail_ready = true;
    xSemaphoreGive(s_mail_lock);
    return saved;
}
static void sync_worker(void *context) {
    (void)context;
    esp_err_t error = badge_sync_run_request(&s_job_config, &s_job_data, publish, NULL, s_job_manual, &s_sync_result);
    int64_t now = time(NULL);
    if (error != ESP_OK && now >= 1577836800 && now <= 253402300799LL && s_job_data.retry_after <= now)
        s_job_data.retry_after = now <= 253402300679LL ? now + 120 : 253402300799LL;
    if (!publish(&s_job_data, NULL)) {
        error = ESP_FAIL;
        s_sync_result.cache_error = true;
        snprintf(s_sync_result.detail, sizeof(s_sync_result.detail), "CACHE NOT SAVED");
    }
    s_sync_error = error;
    atomic_store(&s_result_ready, true);
    ESP_LOGI(TAG, "Sync detail: %s; refreshed=0x%lx failed=0x%lx pending=0x%lx; retry=%lld limit=%lld",
             s_sync_result.detail, (unsigned long)s_sync_result.refreshed,
             (unsigned long)s_sync_result.failed, (unsigned long)s_sync_result.pending,
             (long long)s_sync_result.retry_after, (long long)s_sync_result.rate_limit_until);
    ESP_LOGI(TAG, "Sync finished: %s; free heap=%lu, largest=%lu", esp_err_to_name(error),
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    atomic_store(&s_syncing, false);
    vTaskDelete(NULL);
}
static void start_sync(bool manual) {
    /* A previous key may have queued AP start/stop since the last app tick. */
    badge_network_status(&s_status);
    const char *reason = NULL;
    if (atomic_load(&s_maintenance)) reason = "BACKUP IN PROGRESS";
    else if (s_status.setup_pending || s_config_ack_pending || uxQueueMessagesWaiting(s_configs))
        reason = "WI-FI SETUP IN PROGRESS";
    else if (atomic_load(&s_result_ready)) reason = "FINISHING LAST FETCH";
    else if (atomic_load(&s_syncing)) reason = "FETCH ALREADY RUNNING";
    else if (!s_storage_ok) reason = "CACHE STORAGE UNAVAILABLE";
    else if (!badge_config_valid(&s_config)) reason = "SETUP REQUIRED";
    else if (!s_status.connected) reason = "WIFI NOT CONNECTED";
    else if (s_status.now < 1577836800 || s_status.now > 253402214399LL) reason = "WAITING FOR NETWORK TIME";
    if (reason) { if (manual) notify(reason, true); return; }
    if (s_status.now < s_retry_not_before) {
        if (manual) {
            char wait[24], message[80];
            badge_format_wait(s_retry_not_before, s_status.now, wait, sizeof(wait));
            snprintf(message, sizeof(message), "LOW MEMORY / %s\nWAITING TO START FETCH", wait);
            notify(message, true);
        }
        return;
    }
    /* Automatic retries obey the stored schedule. Manual attempts enter the
     * collector, whose persistent per-bucket limits remain authoritative. */
    if (!manual && !badge_sync_due(&s_data, &s_config, s_status.now)) return;
    if (s_status.provisioning || s_status.provisioning_busy || s_provision_open ||
        s_provision_pending || s_provision_waiting || s_provision_stop_pending) {
        if (manual) {
            if (badge_network_provision_stop() == ESP_OK) {
                s_manual_pending = true;
                s_provision_pending = s_provision_waiting = false;
                s_provision_stop_pending = false;
                s_provision_open = false;
                s_idle.last_activity = esp_timer_get_time();
                if (bsp_lvgl_lock(1000)) { badge_ui_close_provision(); bsp_lvgl_unlock(); }
                notify("CLOSING SETUP / FETCH NEXT", false);
            } else {
                s_manual_pending = false;
                notify("SETUP BUSY\nPRESS REFETCH AGAIN", true);
            }
        }
        return;
    }
    s_job_config = s_config;
    s_job_manual = manual;
    s_job_data = s_data;
    atomic_store(&s_syncing, true);
    if (xTaskCreate(sync_worker, "badge_sync", 10240, NULL, 3, NULL) != pdPASS) {
        atomic_store(&s_syncing, false);
        s_retry_not_before = s_status.now + 120;
        snprintf(s_status.message, sizeof(s_status.message), "SYNC: LOW MEMORY");
        notify("FETCH FAILED / LOW MEMORY", true);
    } else if (manual) notify("FETCHING GITHUB DATA", false);
}
static void load_avatar(void) {
    s_avatar_pending = true;
    bool account_changed = strcmp(s_avatar_account, s_data.username) != 0;
    if (account_changed) {
        if (!bsp_lvgl_lock(1000)) return;
        badge_ui_set_avatar(NULL, 0);
        s_avatar_decoded = false;
        s_avatar_size = 0; s_avatar_hash = 0;
        snprintf(s_avatar_account, sizeof(s_avatar_account), "%s", s_data.username);
        bsp_lvgl_unlock();
    }
    /* Decoding while the collector starts its next TLS request can exhaust
     * internal RAM. Keep the old pixels until the worker releases its memory. */
    if (atomic_load(&s_syncing) || s_status.provisioning || s_status.provisioning_busy) return;
    uint8_t *next = NULL;
    size_t length = 0;
    char path[64];
    if ((s_data.valid & BADGE_AVATAR) &&
        badge_cache_path(s_data.username, "avatar", path, sizeof(path))) {
        FILE *f = fopen(path, "rb");
        if (f) {
            if (fseek(f, 0, SEEK_END) == 0) {
                long size = ftell(f);
                if (size > 0 && size <= 24576 && fseek(f, 0, SEEK_SET) == 0) {
                    next = malloc((size_t)size);
                    if (next && fread(next, 1, (size_t)size, f) == (size_t)size) length = (size_t)size;
                    else { free(next); next = NULL; }
                }
            }
            fclose(f);
        }
    }
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < length; ++i) hash = (hash ^ next[i]) * UINT64_C(1099511628211);
    if (s_avatar_decoded && length && length == s_avatar_size && hash == s_avatar_hash) {
        s_avatar_pending = false;
        free(next);
        return;
    }
    if (bsp_lvgl_lock(1000)) {
        bool decoded = length && badge_ui_set_avatar(next, length);
        if (!length && !(s_data.valid & BADGE_AVATAR)) badge_ui_set_avatar(NULL, 0);
        s_avatar_decoded = decoded;
        if ((s_data.valid & BADGE_AVATAR) && !decoded) {
            badge_ui_notify("AVATAR DECODE FAILED", true);
            ESP_LOGW(TAG, "Cached avatar unavailable or decode failed; encoded=%u free=%lu largest=%lu",
                     (unsigned)length, (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        }
        if (decoded) { s_avatar_size = length; s_avatar_hash = hash; }
        /* A failed decode can be retried after the next sync/publication, even
         * when the encoded bytes are unchanged. Avoid a tight retry loop. */
        s_avatar_pending = false;
        bsp_lvgl_unlock();
    }
    free(next);
}
static badge_status_t display_status(void) {
    badge_status_t view = s_status;
    /* A new dialog must never reuse credentials while its start is deferred. */
    if (s_provision_open && (s_provision_pending || !s_provision_session))
        view.provisioning_busy = true;
    return view;
}
static void refresh_ui(void) {
    if (bsp_lvgl_lock(1000)) {
        badge_status_t view = display_status();
        badge_ui_update(&s_data, &s_config, &view);
        bsp_lvgl_unlock();
    }
}
static void save_preferences(void) {
    if (badge_config_valid(&s_config) && (!s_storage_ok || !badge_config_save(&s_config)))
        notify("APPLIED / NOT SAVED\nCHECK DEVICE STORAGE", true);
}
static void perform_action(badge_input_action_t key) {
    if (!key.ready || s_idle.asleep || s_screen_blocked || !bsp_lvgl_lock(1000)) return;
    badge_action_t action = badge_ui_key((badge_key_t)key.key, key.long_press);
    bsp_lvgl_unlock();
    switch (action.kind) {
    case BADGE_ACTION_THEME:
        s_config.theme = action.value;
        save_preferences();
        refresh_ui();
        break;
    case BADGE_ACTION_BRIGHTNESS:
        s_config.brightness = action.value;
        bsp_display_backlight(s_config.brightness);
        save_preferences();
        refresh_ui();
        break;
    case BADGE_ACTION_SCREEN_TIMEOUT:
        s_config.screen_timeout_min = action.value;
        save_preferences();
        refresh_ui();
        break;
    case BADGE_ACTION_PROVISION:
        /* Do not overlap the AP/server startup peak with an in-flight TLS
         * handshake. Queue setup until the background fetch finishes. */
        s_provision_open = true;
        s_provision_session = 0;
        s_provision_pending = true;
        s_provision_waiting = false;
        s_provision_retry_at = 0;
        s_status.provisioning_busy = true;
        s_idle.last_activity = esp_timer_get_time();
        if (atomic_load(&s_syncing)) notify("FETCH RUNNING\nSETUP AFTER FETCH", false);
        break;
    case BADGE_ACTION_CLOSE_PROVISION:
        s_provision_open = false;
        s_provision_pending = s_provision_waiting = false;
        s_provision_stop_pending = badge_network_provision_stop() != ESP_OK;
        s_provision_retry_at = esp_timer_get_time() + 1000000;
        s_idle.last_activity = esp_timer_get_time();
        if (s_provision_stop_pending) notify("CLOSING HOTSPOT / RETRYING", true);
        break;
    case BADGE_ACTION_SYNC: start_sync(true); break;
    default: break;
    }
}
static void app_task(void *context) {
    (void)context;
    badge_input_t input;
    badge_input_init(&input);
    badge_idle_init(&s_idle, esp_timer_get_time());
    int64_t battery_at = 0, tick_at = 0;
    bool last_syncing = false;
    bool last_ap = false, last_ap_busy = false;
    uint32_t last_ap_session = 0, last_ready_session = 0;
    for (;;) {
        /* Never hold the maintenance gate while waiting for input. The HTTP
         * task has the same priority: releasing and immediately retaking this
         * mutex can otherwise starve backup_begin() until its timeout. */
        key_event_t event;
        // A key wakes the queue immediately; hidden UI needs fewer idle polls.
        TickType_t key_wait = pdMS_TO_TICKS(s_idle.asleep ? 250 : 20);
        bool have_key = xQueueReceive(s_keys, &event, key_wait) == pdTRUE;
        xSemaphoreTake(s_control_lock, portMAX_DELAY);
        if (atomic_load(&s_maintenance)) {
            /* Backup owns storage, not the setup window. Only its close gesture
             * is admitted here; stopping AP services never waits in this task
             * or cancels a Flash commit already in progress. */
            if (atomic_exchange(&s_key_overflow, false)) {
                badge_input_init(&input);
                xQueueReset(s_keys);
                have_key = false;
            }
            if (input.key != BSP_BTN_OK) badge_input_init(&input);
            if (have_key) {
                do {
                    if (s_provision_open && event.key == BSP_BTN_OK)
                        perform_action(badge_input_edge(&input, event.key,
                            event.event == BSP_BTN_PRESS, event.at));
                } while (xQueueReceive(s_keys, &event, 0) == pdTRUE);
            }
            if (s_provision_open) perform_action(badge_input_poll(&input, esp_timer_get_time()));
            else badge_input_init(&input);
            badge_idle_cancel_keys(&s_idle, esp_timer_get_time());
            xSemaphoreGive(s_control_lock);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (have_key) {
            do {
                if (atomic_exchange(&s_key_overflow, false)) {
                    badge_input_init(&input);
                    badge_idle_cancel_keys(&s_idle, esp_timer_get_time());
                    xQueueReset(s_keys);
                } else {
                    bool was_asleep = s_idle.asleep;
                    bool consumed = badge_idle_key(&s_idle, event.key,
                                                   event.event == BSP_BTN_PRESS, event.at);
                    if (was_asleep && !s_idle.asleep) {
                        badge_input_init(&input);
                        s_screen_retry_at = 0; // First wake attempt is immediate.
                    }
                    if (s_screen_blocked) {
                        badge_input_init(&input);
                        /* A wake failure may span further presses. Consume all
                         * of those gestures, including a key still held when
                         * the eventual hardware retry succeeds. */
                        s_idle.consume_wake = s_idle.held_keys != 0;
                        consumed = true;
                    }
                    if (!consumed) perform_action(badge_input_edge(&input, event.key,
                                                   event.event == BSP_BTN_PRESS, event.at));
                }
            } while (xQueueReceive(s_keys, &event, 0) == pdTRUE);
        }
        /* Drain timestamped releases before testing the wall clock: a tap
         * queued during a Flash write must not become a phantom long press. */
        perform_action(badge_input_poll(&input, esp_timer_get_time()));
        badge_config_t configured;
        if (xQueueReceive(s_configs, &configured, 0) == pdTRUE && badge_config_valid(&configured)) {
            for (size_t i = 0; configured.username[i]; ++i)
                configured.username[i] = (char)tolower((unsigned char)configured.username[i]);
            bool account_changed = strcasecmp(configured.username, s_config.username) != 0;
            configured.theme = s_config.theme;
            configured.brightness = s_config.brightness;
            configured.screen_timeout_min = s_config.screen_timeout_min;
            bool saved = badge_config_save(&configured);
            s_config_ack_saved = saved;
            s_config_ack_pending = true;
            if (saved) {
                /* Closing setup never cancels collection. Only a committed
                 * account change invalidates that account's old request. */
                if (account_changed && atomic_load(&s_syncing)) badge_sync_cancel();
                s_config = configured;
            } else snprintf(s_status.message, sizeof(s_status.message), "CONFIG NOT SAVED");
            if (saved && account_changed) {
                badge_data_t *restored = malloc(sizeof(*restored));
                if (restored) {
                    badge_data_reset(restored, s_config.username);
                    if (s_storage_ok) badge_data_load(s_config.username, restored);
                }
                /* The renderer retains s_data. All post-startup changes must
                 * share its LVGL lock, including cache/account replacement. */
                while (!bsp_lvgl_lock(1000)) vTaskDelay(pdMS_TO_TICKS(10));
                if (restored) s_data = *restored;
                else badge_data_reset(&s_data, s_config.username);
                badge_status_t view = display_status();
                badge_ui_update(&s_data, &s_config, &view);
                bsp_lvgl_unlock();
                free(restored);
                s_retry_not_before = 0;
                load_avatar();
            }
            refresh_ui();
        }
        if (s_config_ack_pending && badge_network_config_result(s_config_ack_saved) == ESP_OK)
            s_config_ack_pending = false;
        /* Capture completion before consuming the mailbox. A worker that
         * finishes later in this tick must wait for the next mailbox handoff. */
        bool result_ready = !atomic_load(&s_syncing) && atomic_load(&s_result_ready);
        bool changed = false;
        xSemaphoreTake(s_mail_lock, portMAX_DELAY);
        if (s_mail_ready) {
            if (strcasecmp(s_mail.username, s_config.username) == 0) {
                if (bsp_lvgl_lock(1000)) {
                    s_data = s_mail;
                    badge_status_t view = display_status();
                    badge_ui_update(&s_data, &s_config, &view);
                    bsp_lvgl_unlock();
                    changed = true;
                    s_mail_ready = false;
                }
            } else s_mail_ready = false;
        }
        bool mail_pending = s_mail_ready;
        xSemaphoreGive(s_mail_lock);
        result_ready = result_ready && !mail_pending;
        s_status.now = time(NULL);
        badge_network_status(&s_status);
        if (s_status.network_notice_id != s_network_notice_id &&
            notify(s_status.network_notice, s_status.network_notice_error))
            s_network_notice_id = s_status.network_notice_id;
        if (s_provision_waiting && !s_status.provisioning_busy) s_provision_waiting = false;
        /* Network owns success delivery to the phone and actual AP shutdown.
         * A completion from an older session must not dismiss a newer dialog. */
        if (s_provision_open && !s_provision_pending && s_provision_session &&
            s_status.provisioning_session == s_provision_session &&
            !s_status.provisioning && !s_status.provisioning_busy && bsp_lvgl_lock(1000)) {
            badge_ui_close_provision();
            bsp_lvgl_unlock();
            s_provision_open = false;
            s_idle.last_activity = esp_timer_get_time();
        }
        bool ap_changed = last_ap != s_status.provisioning || last_ap_busy != s_status.provisioning_busy ||
            last_ap_session != s_status.provisioning_session || last_ready_session != s_status.provisioning_ready_session;
        last_ap = s_status.provisioning; last_ap_busy = s_status.provisioning_busy;
        last_ap_session = s_status.provisioning_session; last_ready_session = s_status.provisioning_ready_session;
        s_status.syncing = atomic_load(&s_syncing);
        if (changed) s_avatar_pending = true;
        if (s_avatar_pending || (result_ready && !s_avatar_decoded)) load_avatar();
        if (result_ready && !s_avatar_pending && atomic_exchange(&s_result_ready, false)) {
            if (!s_sync_result.cancelled && !strcasecmp(s_job_config.username, s_config.username)) {
                char message[128], wait[24];
                const char *detail = s_sync_result.detail;
                if (!strncmp(detail, "PARTIAL / ", 10)) detail += 10;
                else if (!strncmp(detail, "SYNC FAILED / ", 14)) detail += 14;
                if (s_sync_result.cache_error) snprintf(message, sizeof(message), "FETCH FAILED\nCACHE NOT SAVED");
                else if (s_sync_error == ESP_OK && (s_data.valid & BADGE_AVATAR) && !s_avatar_decoded)
                    snprintf(message, sizeof(message), "DATA SAVED\nAVATAR DECODE FAILED");
                else if (s_sync_error == ESP_OK) snprintf(message, sizeof(message), "FETCH OK / DATA SAVED");
                else if (s_sync_result.rate_limit_until > s_status.now) {
                    badge_format_wait(s_sync_result.rate_limit_until, s_status.now, wait, sizeof(wait));
                    snprintf(message, sizeof(message), "%s / %s\n%.64s",
                        s_sync_result.waiting_only ? "RATE LIMIT" : "PARTIAL / LIMIT", wait, detail);
                } else snprintf(message, sizeof(message), "%s\n%.64s",
                    s_sync_result.refreshed ? "PARTIAL / RETRY READY" : "FETCH FAILED / RETRY READY", detail);
                notify(message, s_sync_error != ESP_OK || ((s_data.valid & BADGE_AVATAR) && !s_avatar_decoded));
            }
        }
        if (changed || ap_changed || last_syncing != s_status.syncing) refresh_ui();
        last_syncing = s_status.syncing;
        /* Load/decode the final cached image before starting another memory
         * intensive service, and never overlap AP startup with TLS. */
        int64_t monotonic = esp_timer_get_time();
        if (s_provision_stop_pending && monotonic >= s_provision_retry_at) {
            s_provision_stop_pending = badge_network_provision_stop() != ESP_OK;
            s_provision_retry_at = monotonic + 1000000;
        }
        if (s_provision_open && s_provision_pending && !s_provision_stop_pending &&
            monotonic >= s_provision_retry_at && !atomic_load(&s_syncing) &&
            !atomic_load(&s_result_ready) && !s_avatar_pending) {
            if (badge_network_provision_start() == ESP_OK) {
                s_provision_pending = false;
                s_provision_waiting = true;
                badge_network_status(&s_status);
                s_provision_session = s_status.provisioning_session;
            } else {
                s_provision_retry_at = monotonic + 1000000;
                notify("HOTSPOT START FAILED / RETRYING", true);
            }
        }
        if (s_manual_pending && !s_provision_open && !s_provision_pending && !s_provision_stop_pending &&
            !s_status.provisioning && !s_status.provisioning_busy && !s_status.setup_pending &&
            !s_config_ack_pending && !uxQueueMessagesWaiting(s_configs)) {
            s_manual_pending = false;
            start_sync(true);
        }
        if (s_provision_open) s_idle.last_activity = monotonic;
        else badge_idle_tick(&s_idle, s_config.screen_timeout_min, monotonic);
        service_screen(monotonic);
        if (monotonic >= battery_at) {
            s_status.battery_percent = bsp_battery_soc();
            battery_at = monotonic + 30000000;
        }
        if (monotonic >= tick_at) {
            if (bsp_lvgl_lock(500)) {
                badge_status_t view = display_status();
                badge_ui_tick(&view);
                bsp_lvgl_unlock();
            }
            tick_at = monotonic + 1000000;
            start_sync(false);
        }
        xSemaphoreGive(s_control_lock);
    }
}
void app_main(void) {
    /* QR canvases and cached avatars both probe TJPGD during startup. Reject
     * stale local sdkconfig files that would restore the proven boot overflow. */
    _Static_assert(CONFIG_ESP_MAIN_TASK_STACK_SIZE >= 6144,
                   "GitHub badge startup needs CONFIG_ESP_MAIN_TASK_STACK_SIZE >= 6144");
    esp_err_t storage = badge_storage_init();
    s_storage_ok = storage == ESP_OK;
    if (!s_storage_ok) ESP_LOGE(TAG, "Persistent storage unavailable: %s", esp_err_to_name(storage));
    badge_config_load(&s_config);
    badge_data_reset(&s_data, s_config.username);
    if (s_storage_ok && badge_username_valid(s_config.username)) badge_data_load(s_config.username, &s_data);
    if (!s_storage_ok) snprintf(s_data.sync_message, sizeof(s_data.sync_message), "STORAGE ERROR / CACHE UNAVAILABLE");
    s_status.battery_percent = -1;
    s_status.now = time(NULL);
    s_keys = xQueueCreate(16, sizeof(key_event_t));
    s_configs = xQueueCreate(1, sizeof(badge_config_t));
    s_mail_lock = xSemaphoreCreateMutex();
    s_control_lock = xSemaphoreCreateMutex();
    if (!s_keys || !s_configs || !s_mail_lock || !s_control_lock) { ESP_LOGE(TAG, "Queue allocation failed"); return; }
    badge_backup_init(backup_begin, backup_end);
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_display_init());
    if (!bsp_lvgl_init()) { ESP_LOGE(TAG, "Display service failed"); return; }
    if (bsp_battery_init() != ESP_OK) ESP_LOGW(TAG, "Battery gauge unavailable");
    if (bsp_lvgl_lock(1000)) {
        badge_ui_init(&s_data, &s_config, &s_status);
        bsp_lvgl_unlock();
    } else return;
    bsp_display_backlight(s_config.brightness);
    load_avatar();
    esp_err_t network = badge_network_init(&s_config, on_config, NULL);
    if (network != ESP_OK) {
        ESP_LOGE(TAG, "Network unavailable: %s", esp_err_to_name(network));
        snprintf(s_status.message, sizeof(s_status.message), "NETWORK INIT FAILED");
        refresh_ui();
    }
    if (!badge_config_valid(&s_config) && network == ESP_OK) {
        s_provision_open = s_provision_pending = true;
        if (bsp_lvgl_lock(1000)) { badge_ui_show_provision(); bsp_lvgl_unlock(); }
    }
    ESP_ERROR_CHECK(bsp_button_init(on_key, NULL));
    if (xTaskCreate(app_task, "badge_app", 6144, NULL, 5, NULL) != pdPASS)
        ESP_LOGE(TAG, "Application task allocation failed");
    ESP_LOGI(TAG, "GitHub badge ready; data=%u bytes; free=%lu largest=%lu",
             (unsigned)sizeof(badge_data_t), (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "Startup main stack minimum free=%u bytes",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
