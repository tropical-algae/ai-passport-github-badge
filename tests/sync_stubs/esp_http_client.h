#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct test_http_client *esp_http_client_handle_t;
typedef struct {
    int event_id;
    const char *header_key, *header_value;
    void *user_data;
} esp_http_client_event_t;
#define HTTP_EVENT_ON_HEADER 1
typedef struct {
    const char *url, *user_agent;
    int timeout_ms, buffer_size, buffer_size_tx;
    int (*crt_bundle_attach)(void *);
    bool disable_auto_redirect, keep_alive_enable;
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value);
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
int esp_http_client_read(esp_http_client_handle_t client, char *buffer, int length);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
