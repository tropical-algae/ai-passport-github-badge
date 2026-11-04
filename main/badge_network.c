#include "badge_network.h"
#include "badge_portal.h"
#include "badge_backup.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "cJSON.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"

static const char *TAG = "badge_network";
static const char *AP_ADDRESS = "192.168.4.1";
extern const char portal_html_start[] asm("_binary_badge_portal_html_start");
extern const char portal_html_end[] asm("_binary_badge_portal_html_end");

typedef enum { CMD_START, CMD_STOP, CMD_APPLY, CMD_SUBMIT, CMD_COMMIT_RESULT, CMD_IP, CMD_LOST_IP,
               CMD_DISCONNECTED, CMD_AP_CLIENT, CMD_AP_DHCP, CMD_SCAN, CMD_SCAN_DONE } command_kind_t;
typedef struct {
    command_kind_t kind;
    badge_config_t config;
    int reason;
} command_t;

static SemaphoreHandle_t s_lock;
static QueueHandle_t s_commands;
static esp_netif_t *s_sta, *s_ap;
static httpd_handle_t s_http;
static badge_config_t s_config, s_pending_config;
static badge_network_config_cb_t s_callback;
static void *s_context;
static badge_status_t s_status;
static bool s_pending, s_submit_queued, s_saved, s_save_await, s_sntp_ready, s_initialized, s_submission_failed;
static bool s_waiting_ip;
static bool s_backup_active;
static unsigned s_provision_commands; /* Protected by s_lock, includes executing commands. */
static int64_t s_reboot_deadline;
static bool s_scan_requested, s_scanning;
static badge_portal_network_t s_networks[BADGE_PORTAL_NETWORK_MAX];
static size_t s_network_count;
static char s_scan_message[96];
static int64_t s_scan_deadline;
static char s_nonce[33];
static int s_dns = -1;
static int64_t s_next_connect, s_pending_deadline, s_close_deadline, s_connect_deadline;
static unsigned s_failures;
static esp_event_handler_instance_t s_wifi_handler, s_ip_handler;

static int64_t milliseconds(void) { return esp_timer_get_time() / 1000; }
static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static void message(const char *text)
{
    lock();
    snprintf(s_status.message, sizeof(s_status.message), "%s", text);
    unlock();
}

static void heap_log(const char *phase)
{
    ESP_LOGI(TAG, "%s: heap=%u largest=%u", phase,
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static esp_err_t enqueue(command_kind_t kind, const badge_config_t *config)
{
    if (!s_initialized || !s_commands) return ESP_ERR_INVALID_STATE;
    command_t command = {.kind = kind};
    if (config) command.config = *config;
    bool provision = kind == CMD_START || kind == CMD_STOP;
    /* Publish the transition before the worker can take the command. Keeping
     * it pending through completion prevents TLS from racing an unstarted AP. */
    if (provision) { lock(); ++s_provision_commands; }
    bool sent = xQueueSend(s_commands, &command, 0) == pdTRUE;
    if (provision) {
        if (!sent) --s_provision_commands;
        unlock();
    }
    return sent ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void network_event(void *context, esp_event_base_t base, int32_t id, void *data)
{
    (void) context;
    command_t command = {0};
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        command.kind = CMD_DISCONNECTED;
        command.reason = ((wifi_event_sta_disconnected_t *) data)->reason;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) command.kind = CMD_IP;
    else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) command.kind = CMD_LOST_IP;
    else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) command.kind = CMD_AP_CLIENT;
    else if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED) command.kind = CMD_AP_DHCP;
    else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        command.kind = CMD_SCAN_DONE;
        command.reason = ((wifi_event_sta_scan_done_t *) data)->status;
    }
    else return;
    if (xQueueSend(s_commands, &command, 0) != pdTRUE)
        ESP_LOGW(TAG, "Network event queue full");
}

static bool request_on_ap(httpd_req_t *request)
{
    /* HTTPD uses a dual-stack IPv6 listener when IPv6 is enabled. LwIP then
     * returns even IPv4 connections as sockaddr_in6, so do not truncate it. */
    struct sockaddr_storage local = {0};
    socklen_t size = sizeof(local);
    int socket_fd = httpd_req_to_sockfd(request);
    if (getsockname(socket_fd, (struct sockaddr *) &local, &size) != 0 ||
        !badge_portal_local_address_matches((const struct sockaddr *) &local, size,
                                           inet_addr(AP_ADDRESS))) return false;
    lock();
    bool enabled = s_status.provisioning;
    unlock();
    return enabled;
}

static bool request_host_valid(httpd_req_t *request)
{
    char host[32];
    return httpd_req_get_hdr_value_str(request, "Host", host, sizeof(host)) == ESP_OK &&
           badge_portal_host_valid(host);
}

static bool request_from_ap(httpd_req_t *request)
{
    return request_on_ap(request) && request_host_valid(request);
}

static void response_headers(httpd_req_t *request)
{
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Connection", "close");
}

static bool request_nonce_valid(httpd_req_t *request)
{
    char submitted[33];
    if (httpd_req_get_hdr_value_str(request, "X-Setup-Nonce", submitted, sizeof(submitted)) != ESP_OK)
        return false;
    lock();
    bool valid = s_nonce[0] && !strcmp(submitted, s_nonce);
    unlock();
    return valid;
}

static esp_err_t json_message(httpd_req_t *request, const char *status, const char *text)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    cJSON_AddStringToObject(json, "message", text);
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    response_headers(request);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_status(request, status);
    esp_err_t result = httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
    cJSON_free(body);
    return result;
}

static esp_err_t portal_page(httpd_req_t *request)
{
    if (!request_on_ap(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Use the setup Wi-Fi");
    response_headers(request);
    if (!request_host_valid(request)) {
        httpd_resp_set_status(request, "302 Found");
        httpd_resp_set_hdr(request, "Location", "http://192.168.4.1/");
        return httpd_resp_send(request, NULL, 0);
    }
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Content-Security-Policy", "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; frame-ancestors 'none'");
    return httpd_resp_send(request, portal_html_start, portal_html_end - portal_html_start - 1);
}

static esp_err_t portal_state(httpd_req_t *request)
{
    if (!request_from_ap(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Use the setup Wi-Fi");
    badge_config_t config;
    char nonce[33], status_text[64];
    bool pending, saved;
    lock();
    config = s_config;
    snprintf(nonce, sizeof(nonce), "%s", s_nonce);
    snprintf(status_text, sizeof(status_text), "%s", s_status.message);
    pending = s_pending || s_submit_queued || s_save_await || s_backup_active || s_reboot_deadline;
    saved = s_saved;
    unlock();
    cJSON *json = cJSON_CreateObject();
    if (!json) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    cJSON_AddStringToObject(json, "ssid", config.ssid);
    cJSON_AddStringToObject(json, "username", config.username);
    cJSON_AddNumberToObject(json, "interval", config.interval_sec / 3600U);
    cJSON_AddStringToObject(json, "nonce", nonce);
    cJSON_AddStringToObject(json, "message", status_text);
    cJSON_AddBoolToObject(json, "pending", pending);
    cJSON_AddBoolToObject(json, "saved", saved);
    cJSON_AddBoolToObject(json, "open", config.ssid[0] && !config.password[0]);
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    memset(&config, 0, sizeof(config));
    if (!body) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    response_headers(request);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    esp_err_t result = httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
    cJSON_free(body);
    return result;
}

static esp_err_t portal_networks(httpd_req_t *request)
{
    if (!request_from_ap(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Use the setup Wi-Fi");
    badge_portal_network_t networks[BADGE_PORTAL_NETWORK_MAX];
    char scan_message[sizeof(s_scan_message)];
    lock();
    size_t count = s_network_count;
    memcpy(networks, s_networks, sizeof(networks));
    bool scanning = s_scan_requested || s_scanning;
    snprintf(scan_message, sizeof(scan_message), "%s", s_scan_message);
    unlock();
    cJSON *json = cJSON_CreateObject();
    cJSON *list = json ? cJSON_AddArrayToObject(json, "networks") : NULL;
    if (!list) { cJSON_Delete(json); return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory"); }
    cJSON_AddBoolToObject(json, "scanning", scanning);
    cJSON_AddStringToObject(json, "message", scan_message);
    for (size_t i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) { cJSON_Delete(json); return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory"); }
        cJSON_AddItemToArray(list, item);
        cJSON_AddStringToObject(item, "ssid", networks[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", networks[i].rssi);
        cJSON_AddBoolToObject(item, "open", networks[i].open);
    }
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    response_headers(request);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    esp_err_t result = httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
    cJSON_free(body);
    return result;
}

static esp_err_t portal_scan(httpd_req_t *request)
{
    if (!request_from_ap(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Use the setup Wi-Fi");
    if (!request_nonce_valid(request)) return json_message(request, "403 Forbidden", "Setup session expired. Reload this page.");
    if (request->content_len) return json_message(request, "400 Bad Request", "Scan request must have no body.");
    lock();
    bool pending = s_pending || s_submit_queued || s_save_await || s_backup_active || s_reboot_deadline;
    bool scanning = s_scan_requested || s_scanning;
    if (!pending && !scanning) {
        s_scan_requested = true;
        snprintf(s_scan_message, sizeof(s_scan_message), "Scanning nearby 2.4 GHz networks...");
    }
    unlock();
    if (pending) return json_message(request, "409 Conflict", "Wait until Wi-Fi connection completes.");
    if (!scanning && enqueue(CMD_SCAN, NULL) != ESP_OK) {
        lock(); s_scan_requested = false; unlock();
        return json_message(request, "503 Service Unavailable", "Device busy. Try again.");
    }
    return json_message(request, "202 Accepted", "Scanning nearby 2.4 GHz networks...");
}

static esp_err_t portal_config(httpd_req_t *request)
{
    if (!request_from_ap(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Use the setup Wi-Fi");
    if (request->content_len == 0 || request->content_len > BADGE_PORTAL_BODY_MAX)
        return json_message(request, "413 Content Too Large", "Invalid configuration size.");
    char content_type[80];
    if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        (strcmp(content_type, "application/x-www-form-urlencoded") &&
         strcmp(content_type, "application/x-www-form-urlencoded;charset=UTF-8")))
        return json_message(request, "415 Unsupported Media Type", "Use the setup form.");
    char body[BADGE_PORTAL_BODY_MAX + 1];
    size_t received = 0;
    int64_t deadline = milliseconds() + 10000;
    while (received < request->content_len) {
        int result = milliseconds() >= deadline ? HTTPD_SOCK_ERR_TIMEOUT :
            httpd_req_recv(request, body + received, request->content_len - received);
        if (result <= 0) {
            memset(body, 0, sizeof(body));
            return httpd_resp_send_err(request, result == HTTPD_SOCK_ERR_TIMEOUT ? HTTPD_408_REQ_TIMEOUT : HTTPD_400_BAD_REQUEST,
                                       "Incomplete configuration");
        }
        received += (size_t) result;
    }
    body[received] = 0;
    badge_config_t current, candidate;
    char nonce[33], error[128];
    lock();
    current = s_config;
    snprintf(nonce, sizeof(nonce), "%s", s_nonce);
    bool pending = s_pending || s_submit_queued || s_save_await || s_backup_active || s_reboot_deadline;
    unlock();
    if (pending) {
        memset(body, 0, sizeof(body));
        memset(&current, 0, sizeof(current));
        return json_message(request, "409 Conflict", "Still testing Wi-Fi. Please wait.");
    }
    bool valid = badge_portal_parse_form(body, received, &current, nonce, &candidate, error, sizeof(error));
    memset(body, 0, sizeof(body));
    memset(&current, 0, sizeof(current));
    if (!valid) return json_message(request, "400 Bad Request", error);
    /* Reserve the queued submission before replying: a second tab must not
     * start a restore while the worker has not yet processed this command. */
    lock(); s_submit_queued = true; unlock();
    esp_err_t result = enqueue(CMD_SUBMIT, &candidate);
    memset(&candidate, 0, sizeof(candidate));
    if (result != ESP_OK) {
        lock(); s_submit_queued = false; unlock();
        return json_message(request, "503 Service Unavailable", "Device busy. Try again.");
    }
    return json_message(request, "202 Accepted", "Testing Wi-Fi. Settings are saved only after connection succeeds.");
}

static esp_err_t portal_not_found(httpd_req_t *request, httpd_err_code_t error)
{
    (void) error;
    if (!request_on_ap(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Use the setup Wi-Fi");
    if (request->method != HTTP_GET && request->method != HTTP_HEAD)
        return httpd_resp_send_err(request, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
    response_headers(request);
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "http://192.168.4.1/");
    return httpd_resp_send(request, NULL, 0);
}

static bool begin_backup(void)
{
    lock();
    bool allowed = !s_pending && !s_submit_queued && !s_save_await && !s_backup_active && !s_reboot_deadline;
    if (allowed) s_backup_active = true;
    unlock();
    return allowed;
}

static void end_backup(bool reboot)
{
    lock();
    s_backup_active = false;
    if (reboot) s_reboot_deadline = milliseconds() + 3000;
    unlock();
}

/* Per-socket timeouts alone can be kept alive by a trickling upload. Bound the
 * whole transfer so maintenance mode cannot freeze controls indefinitely. */
typedef struct { httpd_req_t *request; size_t transferred, remaining; int64_t deadline; bool started; } backup_http_t;

static bool backup_write(const void *data, size_t length, void *context)
{
    backup_http_t *transfer = context;
    const char *bytes = data;
    if (!transfer->started) {
        /* Validation failures must remain JSON errors, not attachment files.
         * Once streaming begins, even a failed first chunk must close the
         * response rather than append JSON to a partially written backup. */
        transfer->started = true;
        response_headers(transfer->request);
        if (httpd_resp_set_type(transfer->request, "application/octet-stream") != ESP_OK ||
            httpd_resp_set_hdr(transfer->request, "Content-Disposition",
                               "attachment; filename=github-badge-backup.bin") != ESP_OK) return false;
    }
    while (length) {
        if (milliseconds() >= transfer->deadline) return false;
        size_t chunk = length > 1024 ? 1024 : length;
        if (httpd_resp_send_chunk(transfer->request, bytes, chunk) != ESP_OK) return false;
        transfer->transferred += chunk;
        bytes += chunk;
        length -= chunk;
    }
    return true;
}

static int backup_read(void *data, size_t length, void *context)
{
    backup_http_t *transfer = context;
    if (milliseconds() >= transfer->deadline) return HTTPD_SOCK_ERR_TIMEOUT;
    if (length > transfer->remaining) length = transfer->remaining;
    if (!length) return 0;
    if (length > 1024) length = 1024;
    int received = httpd_req_recv(transfer->request, data, length);
    if (received > 0) transfer->remaining -= (size_t) received;
    return received;
}

static esp_err_t portal_backup(httpd_req_t *request)
{
    if (!request_from_ap(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Use the setup Wi-Fi");
    if (!request_nonce_valid(request)) return json_message(request, "403 Forbidden", "Setup session expired. Reload this page.");
    if (request->content_len) return json_message(request, "400 Bad Request", "Backup request must have no body.");
    if (!begin_backup()) return json_message(request, "409 Conflict", "Wait until the current operation finishes.");
    backup_http_t transfer = {.request = request, .deadline = milliseconds() + 30000};
    char error[160] = "Backup failed. Please retry.";
    bool success = badge_backup_export(backup_write, &transfer, error, sizeof(error));
    esp_err_t result;
    if (success) result = httpd_resp_send_chunk(request, NULL, 0);
    /* Do not terminate a partial backup successfully; the client must reject it. */
    else if (transfer.started) result = ESP_FAIL;
    else result = json_message(request, "500 Internal Server Error", error);
    end_backup(false);
    return result;
}

static esp_err_t portal_restore(httpd_req_t *request)
{
    if (!request_from_ap(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Use the setup Wi-Fi");
    if (!request_nonce_valid(request)) return json_message(request, "403 Forbidden", "Setup session expired. Reload this page.");
    char content_type[48];
    if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, "application/octet-stream"))
        return json_message(request, "415 Unsupported Media Type", "Choose a badge backup file.");
    if (!request->content_len || request->content_len > BADGE_BACKUP_MAX_SIZE)
        return json_message(request, "413 Content Too Large", "Backup file is empty or exceeds 128 KiB.");
    if (!begin_backup()) return json_message(request, "409 Conflict", "Wait until the current operation finishes.");
    backup_http_t transfer = {.request = request, .remaining = request->content_len, .deadline = milliseconds() + 30000};
    char error[160] = "Restore failed. Existing data retained.";
    bool success = badge_backup_restore(backup_read, &transfer, request->content_len, error, sizeof(error));
    esp_err_t result = json_message(request, success ? "200 OK" : "400 Bad Request",
        success ? "Backup restored. Badge restarts in 3 seconds; reconnect using the restored Wi-Fi settings." : error);
    end_backup(success);
    return result;
}

static esp_err_t start_http(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.max_open_sockets = 3;
    config.backlog_conn = 2;
    config.max_uri_handlers = 7;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    config.lru_purge_enable = true;
    esp_err_t result = httpd_start(&s_http, &config);
    if (result != ESP_OK) return result;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = portal_page},
        {.uri = "/api/state", .method = HTTP_GET, .handler = portal_state},
        {.uri = "/api/config", .method = HTTP_POST, .handler = portal_config},
        {.uri = "/api/networks", .method = HTTP_GET, .handler = portal_networks},
        {.uri = "/api/scan", .method = HTTP_POST, .handler = portal_scan},
        {.uri = "/api/backup", .method = HTTP_GET, .handler = portal_backup},
        {.uri = "/api/restore", .method = HTTP_POST, .handler = portal_restore},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        result = httpd_register_uri_handler(s_http, &routes[i]);
        if (result != ESP_OK) break;
    }
    if (result == ESP_OK) result = httpd_register_err_handler(s_http, HTTPD_404_NOT_FOUND, portal_not_found);
    if (result != ESP_OK) { httpd_stop(s_http); s_http = NULL; }
    return result;
}

static void start_dns(void)
{
    s_dns = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns < 0) return;
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(53)};
    address.sin_addr.s_addr = inet_addr(AP_ADDRESS);
    int flags = fcntl(s_dns, F_GETFL, 0);
    if (bind(s_dns, (struct sockaddr *) &address, sizeof(address)) != 0 || flags < 0 ||
        fcntl(s_dns, F_SETFL, flags | O_NONBLOCK) != 0) {
        close(s_dns);
        s_dns = -1;
        ESP_LOGW(TAG, "Captive DNS unavailable; manual portal URL remains available");
    }
}

static void poll_dns(void)
{
    if (s_dns < 0) return;
    const uint8_t address[] = {192, 168, 4, 1};
    for (unsigned count = 0; count < 4; ++count) {
        uint8_t packet[512];
        struct sockaddr_in client;
        socklen_t client_size = sizeof(client);
        int received = recvfrom(s_dns, packet, sizeof(packet), 0, (struct sockaddr *) &client, &client_size);
        if (received <= 0) break;
        size_t length = badge_portal_dns_reply(packet, (size_t) received, sizeof(packet), address);
        if (length) sendto(s_dns, packet, length, 0, (struct sockaddr *) &client, client_size);
    }
}

static void cancel_scan(void)
{
    lock();
    bool scanning = s_scanning;
    s_scanning = false;
    s_scan_requested = false;
    unlock();
    s_scan_deadline = 0;
    if (scanning) { esp_wifi_scan_stop(); esp_wifi_clear_ap_list(); }
}

static void start_scan(void)
{
    lock();
    bool allowed = s_scan_requested && s_status.provisioning && !s_pending && !s_submit_queued && !s_save_await && !s_backup_active && !s_reboot_deadline;
    s_scan_requested = false;
    s_scanning = allowed;
    unlock();
    if (!allowed) return;
    /* A stale saved network can keep the radio reconnecting indefinitely.
     * Suspend that attempt for this scan, then resume normal backoff afterward. */
    if (s_waiting_ip) {
        s_waiting_ip = false;
        s_connect_deadline = 0;
        esp_wifi_disconnect();
    }
    wifi_scan_config_t scan = {.show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE};
    scan.scan_time.active.max = 120;
    esp_err_t result = esp_wifi_scan_start(&scan, false);
    if (result == ESP_OK) s_scan_deadline = milliseconds() + 12000;
    else {
        lock();
        s_scanning = false;
        snprintf(s_scan_message, sizeof(s_scan_message), "Radio busy. Refresh to try again, or enter the SSID below.");
        unlock();
    }
}

static void scan_done(int reason)
{
    lock(); bool active = s_scanning; unlock();
    if (!active) return;
    badge_portal_network_t networks[BADGE_PORTAL_NETWORK_MAX] = {0};
    size_t count = 0;
    uint16_t found = 0;
    bool success = !reason && esp_wifi_scan_get_ap_num(&found) == ESP_OK;
    for (uint16_t i = 0; success && i < found; ++i) {
        wifi_ap_record_t record;
        if (esp_wifi_scan_get_ap_record(&record) != ESP_OK) { success = false; break; }
        count = badge_portal_network_add(networks, count, BADGE_PORTAL_NETWORK_MAX,
                                         record.ssid, strnlen((const char *) record.ssid, 32),
                                         record.rssi, record.authmode == WIFI_AUTH_OPEN);
    }
    /* Also releases records after a failed or partial read. No scan-sized heap array. */
    esp_wifi_clear_ap_list();
    s_scan_deadline = 0;
    lock();
    s_scanning = false;
    if (success) {
        memcpy(s_networks, networks, sizeof(networks));
        s_network_count = count;
    }
    snprintf(s_scan_message, sizeof(s_scan_message), "%s", !success ? "Scan failed. Refresh to retry or enter the SSID below." :
             count ? "Select a network, or enter a hidden network below." : "No named networks found. Refresh or enter the SSID below.");
    unlock();
}

static esp_err_t connect_config(const badge_config_t *config)
{
    cancel_scan();
    s_waiting_ip = false;
    s_connect_deadline = 0;
    lock();
    s_status.connected = false;
    unlock();
    esp_wifi_disconnect();
    s_next_connect = 0;
    if (!config->ssid[0]) return ESP_OK;
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, config->ssid, strlen(config->ssid));
    memcpy(wifi.sta.password, config->password, strlen(config->password));
    wifi.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi.sta.pmf_cfg.capable = true;
    esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    memset(&wifi, 0, sizeof(wifi));
    s_failures = 0;
    s_next_connect = config->ssid[0] ? milliseconds() + 400 : 0;
    return result;
}

static esp_err_t start_provisioning(void)
{
    lock();
    bool active = s_status.provisioning;
    unlock();
    if (active) return ESP_OK;
    heap_log("before AP");
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[33], password[20], nonce[33];
    snprintf(ssid, sizeof(ssid), "GitHubBadge-%02X%02X%02X", mac[3], mac[4], mac[5]);
    uint8_t random[22];
    esp_fill_random(random, sizeof(random));
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (unsigned i = 0; i < 12; ++i) password[i] = alphabet[random[i] & 31U];
    password[12] = 0;
    for (unsigned i = 0; i < 16; ++i) snprintf(nonce + i * 2, 3, "%02x", random[i + 6]);
    wifi_config_t ap = {0};
    memcpy(ap.ap.ssid, ssid, strlen(ssid));
    ap.ap.ssid_len = strlen(ssid);
    memcpy(ap.ap.password, password, strlen(password));
    ap.ap.channel = 1;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 1;
    ap.ap.pmf_cfg.capable = true;
    esp_err_t result = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_AP, &ap);
    memset(&ap, 0, sizeof(ap));
    if (result != ESP_OK) { esp_wifi_set_mode(WIFI_MODE_STA); return result; }
    lock();
    s_status.provisioning = true;
    s_saved = false;
    s_network_count = 0;
    s_scan_message[0] = 0;
    s_submission_failed = false;
    snprintf(s_status.ap_ssid, sizeof(s_status.ap_ssid), "%s", ssid);
    snprintf(s_status.ap_password, sizeof(s_status.ap_password), "%s", password);
    snprintf(s_nonce, sizeof(s_nonce), "%s", nonce);
    snprintf(s_status.message, sizeof(s_status.message), "Join setup Wi-Fi, open 192.168.4.1");
    unlock();
    s_close_deadline = 0;
    start_dns();
    result = start_http();
    if (result != ESP_OK) {
        if (s_dns >= 0) { close(s_dns); s_dns = -1; }
        esp_wifi_set_mode(WIFI_MODE_STA);
        lock();
        s_status.provisioning = false;
        memset(s_status.ap_password, 0, sizeof(s_status.ap_password));
        unlock();
        return result;
    }
    heap_log("after AP and HTTP");
    return ESP_OK;
}

static void cancel_pending(void)
{
    lock();
    bool pending = s_pending;
    badge_config_t previous = s_config;
    s_pending = false;
    memset(&s_pending_config, 0, sizeof(s_pending_config));
    unlock();
    s_pending_deadline = 0;
    if (pending) connect_config(&previous);
    memset(&previous, 0, sizeof(previous));
}

static void stop_provisioning(void)
{
    cancel_scan();
    cancel_pending();
    if (s_http) { httpd_stop(s_http); s_http = NULL; }
    if (s_dns >= 0) { close(s_dns); s_dns = -1; }
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_close_deadline = 0;
    lock();
    s_status.provisioning = false;
    memset(s_status.ap_password, 0, sizeof(s_status.ap_password));
    memset(s_nonce, 0, sizeof(s_nonce));
    snprintf(s_status.message, sizeof(s_status.message), "%s", s_status.connected ? "Wi-Fi connected" : "Offline; displaying saved data");
    unlock();
    heap_log("after AP stop");
}

static void got_ip(void)
{
    wifi_ap_record_t record;
    esp_netif_ip_info_t info;
    if (esp_wifi_sta_get_ap_info(&record) != ESP_OK || esp_netif_get_ip_info(s_sta, &info) != ESP_OK || !info.ip.addr) return;
    badge_config_t committed;
    lock();
    if (s_pending && !s_waiting_ip) { unlock(); return; }
    const badge_config_t *expected = s_pending ? &s_pending_config : &s_config;
    /* Drop IP events queued by a previous connection while changing networks. */
    if (strncmp((const char *) record.ssid, expected->ssid, sizeof(record.ssid))) { unlock(); return; }
    s_status.connected = true;
    bool commit = s_pending;
    if (commit) {
        s_config = s_pending_config;
        committed = s_config;
        s_pending = false;
        s_saved = false;
        s_save_await = true;
        memset(&s_pending_config, 0, sizeof(s_pending_config));
    }
    if (commit || !s_status.provisioning || !s_submission_failed)
        snprintf(s_status.message, sizeof(s_status.message), "%s", commit ? "Wi-Fi connected; saving settings" : "Wi-Fi connected");
    unlock();
    s_next_connect = 0;
    s_connect_deadline = 0;
    s_waiting_ip = false;
    s_failures = 0;
    if (s_sntp_ready) esp_netif_sntp_start();
    if (commit) {
        s_pending_deadline = 0;
        if (s_callback) s_callback(&committed, s_context);
        else badge_network_config_result(false);
        memset(&committed, 0, sizeof(committed));
    }
    ESP_LOGI(TAG, "Station obtained IP");
}

static void worker(void *context)
{
    (void) context;
    command_t command;
    for (;;) {
        if (xQueueReceive(s_commands, &command, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (command.kind) {
            case CMD_START:
                if (start_provisioning() != ESP_OK) message("Unable to start setup Wi-Fi; try again");
                break;
            case CMD_STOP: stop_provisioning(); break;
            case CMD_APPLY: {
                lock();
                bool changed = strcmp(command.config.ssid, s_config.ssid) || strcmp(command.config.password, s_config.password);
                s_config = command.config;
                unlock();
                if (changed) { cancel_pending(); connect_config(&command.config); }
                break;
            }
            case CMD_SUBMIT: {
                lock();
                s_submit_queued = false;
                bool allowed = s_status.provisioning && !s_pending && !s_save_await && !s_backup_active && !s_reboot_deadline;
                if (allowed) {
                    s_pending_config = command.config;
                    s_pending = true;
                    s_saved = false;
                    s_submission_failed = false;
                }
                unlock();
                if (allowed) {
                    s_close_deadline = 0;
                    s_pending_deadline = milliseconds() + 45000;
                    if (connect_config(&command.config) != ESP_OK) {
                        cancel_pending();
                        lock(); s_submission_failed = true; unlock();
                        message("Wi-Fi configuration failed; previous settings retained");
                    } else message("Testing Wi-Fi connection; previous settings retained");
                }
                break;
            }
            case CMD_COMMIT_RESULT: {
                lock();
                bool awaited = s_save_await;
                if (awaited) {
                    s_save_await = false;
                    s_saved = command.reason != 0;
                    s_submission_failed = !s_saved;
                    if (s_saved) {
                        snprintf(s_status.message, sizeof(s_status.message), "Saved. Wi-Fi connected; hotspot closes in 60 sec");
                        if (s_status.provisioning) s_close_deadline = milliseconds() + 60000;
                    } else snprintf(s_status.message, sizeof(s_status.message), "Wi-Fi connected; settings NOT saved. Please retry");
                }
                unlock();
                break;
            }
            case CMD_IP: got_ip(); break;
            case CMD_LOST_IP:
            case CMD_DISCONNECTED:
                s_connect_deadline = 0;
                s_waiting_ip = false;
                if (command.kind == CMD_LOST_IP) esp_wifi_disconnect();
                lock();
                s_status.connected = false;
                bool have_ssid = (s_pending ? s_pending_config.ssid[0] : s_config.ssid[0]) != 0;
                bool is_pending = s_pending;
                bool ap_active = s_status.provisioning;
                unlock();
                if (have_ssid && !s_next_connect) {
                    unsigned delay = s_failures < 5 ? (1U << s_failures) : 60U;
                    ++s_failures;
                    s_next_connect = milliseconds() + delay * 1000;
                }
                if (is_pending) message("Wi-Fi not connected yet; check credentials and 2.4 GHz");
                else if (!ap_active) message("Offline; displaying saved data");
                if (command.kind == CMD_DISCONNECTED) ESP_LOGI(TAG, "Station disconnected, reason=%d", command.reason);
                break;
            case CMD_AP_CLIENT: heap_log("phone associated with AP"); break;
            case CMD_AP_DHCP: heap_log("phone received AP IP"); break;
            case CMD_SCAN: start_scan(); break;
            case CMD_SCAN_DONE: scan_done(command.reason); break;
            }
            if (command.kind == CMD_START || command.kind == CMD_STOP) {
                lock();
                --s_provision_commands;
                unlock();
            }
            memset(&command, 0, sizeof(command));
        }
        int64_t now = milliseconds();
        lock();
        int64_t reboot_deadline = s_reboot_deadline;
        bool backup_active = s_backup_active;
        unlock();
        if (reboot_deadline && now >= reboot_deadline) esp_restart();
        if (s_pending_deadline && now >= s_pending_deadline) {
            cancel_pending();
            lock(); s_submission_failed = true; unlock();
            message("Connection failed. Previous settings retained; retry here");
        }
        if (s_scan_deadline && now >= s_scan_deadline) {
            cancel_scan();
            lock(); snprintf(s_scan_message, sizeof(s_scan_message), "Scan timed out. Refresh or enter the SSID below."); unlock();
        }
        lock(); bool scanning = s_scanning || s_scan_requested; unlock();
        if (!scanning && s_next_connect && now >= s_next_connect) {
            s_next_connect = 0;
            esp_err_t result = esp_wifi_connect();
            if (result != ESP_OK) s_next_connect = now + 5000;
            else { s_waiting_ip = true; s_connect_deadline = now + 20000; }
        }
        if (s_connect_deadline && now >= s_connect_deadline) {
            s_connect_deadline = 0;
            s_waiting_ip = false;
            esp_wifi_disconnect();
            s_next_connect = now + 5000;
        }
        if (!backup_active && !reboot_deadline && s_close_deadline && now >= s_close_deadline) stop_provisioning();
        poll_dns();
    }
}

esp_err_t badge_network_init(const badge_config_t *config,
                             badge_network_config_cb_t callback, void *context)
{
    if (!config || !memchr(config->ssid, 0, sizeof(config->ssid)) ||
        !memchr(config->password, 0, sizeof(config->password)) ||
        !memchr(config->username, 0, sizeof(config->username))) return ESP_ERR_INVALID_ARG;
    if (s_initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t result = ESP_ERR_NO_MEM;
    bool wifi_initialized = false, own_event_loop = false;
    s_lock = xSemaphoreCreateMutex();
    s_commands = xQueueCreate(12, sizeof(command_t));
    if (!s_lock || !s_commands) goto failed;
    s_config = *config;
    s_callback = callback;
    s_context = context;
    s_status.battery_percent = -1;
    snprintf(s_status.message, sizeof(s_status.message), "Connecting to Wi-Fi");
    result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) goto failed;
    result = esp_event_loop_create_default();
    own_event_loop = result == ESP_OK;
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) goto failed;
    s_sta = esp_netif_create_default_wifi_sta();
    s_ap = esp_netif_create_default_wifi_ap();
    if (!s_sta || !s_ap) { result = ESP_ERR_NO_MEM; goto failed; }
    esp_netif_dhcps_stop(s_ap);
    esp_netif_ip_info_t ap_ip = {0};
    ap_ip.ip.addr = inet_addr(AP_ADDRESS);
    ap_ip.gw.addr = ap_ip.ip.addr;
    ap_ip.netmask.addr = inet_addr("255.255.255.0");
    result = esp_netif_set_ip_info(s_ap, &ap_ip);
    if (result != ESP_OK) goto failed;
    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ap_ip.ip.addr;
    esp_netif_set_dns_info(s_ap, ESP_NETIF_DNS_MAIN, &dns);
    uint8_t offer_dns = 2; /* OFFER_DNS in the DHCP server option bitmask. */
    esp_netif_dhcps_option(s_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns, sizeof(offer_dns));
    /* No Option 114 claim: captive behavior still needs testing on real phones. */
    wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&wifi);
    if (result != ESP_OK) goto failed;
    wifi_initialized = true;
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) goto failed;
    result = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL, &s_wifi_handler);
    if (result != ESP_OK) goto failed;
    result = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, network_event, NULL, &s_ip_handler);
    if (result != ESP_OK) goto failed;
    /* Install a secure dormant AP configuration before starting the radio, so
     * entering APSTA later can never briefly expose the driver's open default. */
    result = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (result != ESP_OK) goto failed;
    wifi_config_t dormant_ap = {0};
    snprintf((char *) dormant_ap.ap.ssid, sizeof(dormant_ap.ap.ssid), "GitHubBadge-SETUP");
    snprintf((char *) dormant_ap.ap.password, sizeof(dormant_ap.ap.password), "%08lx%08lx",
             (unsigned long) esp_random(), (unsigned long) esp_random());
    dormant_ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    dormant_ap.ap.max_connection = 1;
    result = esp_wifi_set_config(WIFI_IF_AP, &dormant_ap);
    memset(&dormant_ap, 0, sizeof(dormant_ap));
    if (result != ESP_OK) goto failed;
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) goto failed;
    result = esp_wifi_start();
    if (result != ESP_OK) goto failed;
    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    time_config.start = false;
    s_sntp_ready = esp_netif_sntp_init(&time_config) == ESP_OK;
    result = connect_config(config);
    if (result != ESP_OK) goto failed;
    if (xTaskCreate(worker, "badge_network", 4096, NULL, 4, NULL) != pdPASS) { result = ESP_ERR_NO_MEM; goto failed; }
    s_initialized = true;
    if (!config->ssid[0] || !config->username[0]) return badge_network_provision_start();
    return ESP_OK;
failed:
    if (s_wifi_handler) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
    if (s_ip_handler) esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, s_ip_handler);
    s_wifi_handler = s_ip_handler = NULL;
    if (s_sntp_ready) { esp_netif_sntp_deinit(); s_sntp_ready = false; }
    if (wifi_initialized) { esp_wifi_stop(); esp_wifi_deinit(); }
    if (s_sta) esp_netif_destroy_default_wifi(s_sta);
    if (s_ap) esp_netif_destroy_default_wifi(s_ap);
    s_sta = s_ap = NULL;
    if (own_event_loop) esp_event_loop_delete_default();
    if (s_commands) vQueueDelete(s_commands);
    if (s_lock) vSemaphoreDelete(s_lock);
    s_commands = NULL;
    s_lock = NULL;
    return result;
}

esp_err_t badge_network_apply(const badge_config_t *config)
{
    if (!config || !badge_config_valid(config)) return ESP_ERR_INVALID_ARG;
    return enqueue(CMD_APPLY, config);
}

esp_err_t badge_network_config_result(bool saved)
{
    if (!s_initialized || !s_commands) return ESP_ERR_INVALID_STATE;
    command_t command = {.kind = CMD_COMMIT_RESULT, .reason = saved ? 1 : 0};
    return xQueueSend(s_commands, &command, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t badge_network_provision_start(void) { return enqueue(CMD_START, NULL); }
esp_err_t badge_network_provision_stop(void) { return enqueue(CMD_STOP, NULL); }

void badge_network_status(badge_status_t *status)
{
    if (!status || !s_lock) return;
    lock();
    status->connected = s_status.connected;
    status->provisioning = s_status.provisioning;
    status->provisioning_busy = s_provision_commands != 0;
    status->setup_saved = s_saved;
    snprintf(status->ap_ssid, sizeof(status->ap_ssid), "%s", s_status.ap_ssid);
    snprintf(status->ap_password, sizeof(status->ap_password), "%s", s_status.ap_password);
    snprintf(status->message, sizeof(status->message), "%s", s_status.message);
    unlock();
}
