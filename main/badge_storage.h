#pragma once
#include "badge_model.h"
#include "esp_err.h"

/* /data is a 1 MiB SPIFFS partition. Keys are normalized GitHub logins. */
esp_err_t badge_storage_init(void);
bool badge_config_load(badge_config_t *config);
bool badge_config_save(const badge_config_t *config);
bool badge_data_load(const char *username, badge_data_t *data);
bool badge_data_save(const badge_data_t *data);
bool badge_cache_path(const char *username, const char *suffix, char *out, size_t size);
/* Staged replacement with .bak rollback. Dataset snapshots use CRC dual slots. */
bool badge_file_write(const char *path, const void *data, size_t size);

/* Restore writes into a separate namespace, then atomically activates a single
 * NVS blob containing both configuration and namespace. Zero preserves v0.1. */
uint32_t badge_storage_namespace(void);
bool badge_storage_activate(const badge_config_t *config, uint32_t space);
bool badge_cache_path_at(uint32_t space, const char *username, const char *suffix,
                         char *out, size_t size);
bool badge_data_save_at(uint32_t space, const badge_data_t *data);
void badge_storage_remove_account(uint32_t space, const char *username);

bool badge_storage_namespace_empty(uint32_t space, const char *username);
