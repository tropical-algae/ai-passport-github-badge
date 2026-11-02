#include "badge_storage.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define RECORD_MAGIC 0x47484231U
typedef struct { uint32_t magic, schema, size, generation, crc; } record_header_t;
static const char *TAG = "badge_storage";
static uint32_t s_cache_namespace;
#define STATE_MAGIC 0x47485333U
#define LEGACY_STATE_MAGIC 0x47485332U
typedef struct {
    uint32_t magic, cache_namespace;
    badge_config_t config;
    uint32_t crc;
} config_state_t;
typedef struct {
    uint32_t magic, cache_namespace;
    uint8_t config[BADGE_LEGACY_CONFIG_SIZE];
    uint32_t crc;
} legacy_config_state_t;
_Static_assert(sizeof(legacy_config_state_t) == 160, "Legacy state ABI changed");
uint32_t badge_storage_namespace(void) { return s_cache_namespace; }

/* Restore an interrupted staged replacement before any later writer can
 * discard the only surviving copy. An existing final name was installed only
 * after the temporary file was flushed and synchronized. */
static bool recover_file(const char *path) {
    char backup[64];
    int length = snprintf(backup, sizeof(backup), "%s.bak", path);
    if (length < 0 || length >= (int)sizeof(backup)) return false;
    /* SPIFFS implements stat(), but access() returns ENOSYS in ESP-IDF.
     * Only ENOENT means absent; preserve records on all other errors. */
    struct stat info;
    if (stat(backup, &info) != 0) return errno == ENOENT;
    if (stat(path, &info) == 0) return unlink(backup) == 0;
    if (errno != ENOENT) return false;
    return rename(backup, path) == 0;
}

static bool recover_files(void) {
    DIR *directory = opendir("/data");
    if (!directory) return false;
    bool ok = true;
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (!entry) { if (errno) ok = false; break; }
        size_t length = strlen(entry->d_name);
        if (length <= 4) continue;
        bool backup = !strcmp(entry->d_name + length - 4, ".bak");
        bool temporary = !strcmp(entry->d_name + length - 4, ".tmp");
        if (!backup && !temporary) continue;
        char path[64];
        int used = snprintf(path, sizeof(path), "/data/%s", entry->d_name);
        if (used < 0 || used >= (int)sizeof(path)) { ok = false; continue; }
        if (backup) {
            path[used - 4] = 0;
            if (!recover_file(path)) ok = false;
        } else if (unlink(path) != 0) ok = false;
    }
    if (closedir(directory) != 0) ok = false;
    return ok;
}

static uint32_t crc32(const void *bytes, size_t len) {
    const uint8_t *p = bytes;
    uint32_t crc = ~0U;
    while (len--) {
        crc ^= *p++;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

esp_err_t badge_storage_init(void) {
    esp_err_t err = nvs_flash_init();
    /* Never erase existing NVS implicitly. A failed store remains visible. */
    if (err != ESP_OK) return err;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/data", .partition_label = "badge_data",
        .max_files = 8, .format_if_mount_failed = false,
    };
    err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_FAIL) {
        /* This partition is application-owned. Only format an erased partition
         * (new layout), never a failed mount containing prior user records. */
        const esp_partition_t *p = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "badge_data");
        bool erased = p != NULL;
        uint8_t block[256];
        for (size_t offset = 0; erased && offset < p->size; offset += sizeof(block)) {
            if (esp_partition_read(p, offset, block, sizeof(block)) != ESP_OK) {
                erased = false;
                break;
            }
            for (size_t i = 0; i < sizeof(block); ++i)
                if (block[i] != 0xff) { erased = false; break; }
        }
        if (!erased) {
            ESP_LOGE(TAG, "Cache mount failed; existing partition preserved");
            return err;
        }
        err = esp_spiffs_format("badge_data");
        if (err == ESP_OK) err = esp_vfs_spiffs_register(&conf);
    }
    if (err == ESP_OK && !recover_files()) {
        ESP_LOGE(TAG, "Cache replacement recovery failed; existing records preserved");
        return ESP_FAIL;
    }
    return err;
}

bool badge_config_load(badge_config_t *config) {
    badge_config_defaults(config);
    nvs_handle_t handle;
    if (nvs_open("github_badge", NVS_READONLY, &handle) != ESP_OK) return false;
    union { config_state_t current; legacy_config_state_t legacy; } state;
    size_t size = sizeof(state);
    esp_err_t err = nvs_get_blob(handle, "state_v2", &state, &size);
    if (err == ESP_OK) {
        nvs_close(handle);
        badge_config_t decoded;
        uint32_t space;
        if (size == sizeof(state.current) && state.current.magic == STATE_MAGIC &&
            crc32(&state.current, offsetof(config_state_t, crc)) == state.current.crc &&
            badge_config_decode(&state.current.config, sizeof(state.current.config), &decoded)) {
            space = state.current.cache_namespace;
        } else if (size == sizeof(state.legacy) && state.legacy.magic == LEGACY_STATE_MAGIC &&
            crc32(&state.legacy, offsetof(legacy_config_state_t, crc)) == state.legacy.crc &&
            badge_config_decode(state.legacy.config, sizeof(state.legacy.config), &decoded)) {
            space = state.legacy.cache_namespace;
        } else return false;
        *config = decoded;
        s_cache_namespace = space;
        return true;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); return false; }
    uint8_t saved[BADGE_LEGACY_CONFIG_SIZE];
    size = sizeof(saved);
    err = nvs_get_blob(handle, "config_v1", saved, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(saved) || !badge_config_decode(saved, size, config)) return false;
    s_cache_namespace = 0;
    return true;
}

bool badge_storage_activate(const badge_config_t *config, uint32_t space) {
    if (!badge_config_valid(config)) return false;
    config_state_t state = {0};
    state.magic = STATE_MAGIC;
    state.cache_namespace = space;
    state.config = *config;
    state.crc = crc32(&state, offsetof(config_state_t, crc));
    nvs_handle_t handle;
    if (nvs_open("github_badge", NVS_READWRITE, &handle) != ESP_OK) return false;
    /* Replace the same key atomically; never leave a stale versioned key that
     * can become active after a failed read or a firmware rollback. */
    esp_err_t err = nvs_set_blob(handle, "state_v2", &state, sizeof(state));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) s_cache_namespace = space;
    return err == ESP_OK;
}

bool badge_config_save(const badge_config_t *config) {
    return badge_storage_activate(config, s_cache_namespace);
}

bool badge_cache_path(const char *username, const char *suffix, char *out, size_t size) {
    return badge_cache_path_at(s_cache_namespace, username, suffix, out, size);
}

bool badge_cache_path_at(uint32_t space, const char *username, const char *suffix, char *out, size_t size) {
    if (!badge_username_valid(username) || !suffix || !out) return false;
    for (size_t i = 0; suffix[i]; ++i)
        if (!isalnum((unsigned char)suffix[i]) && suffix[i] != '-' && suffix[i] != '.') return false;
    /* Keep base names within 31 bytes, leaving room for temporary/backup
     * suffixes in the application's 64-byte SPIFFS name configuration. Two
     * case-folded hashes avoid truncating long logins; snapshots additionally
     * verify the full login on read. */
    uint32_t a = 2166136261U, b = 5381U;
    if (space) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            uint8_t byte = (uint8_t)(space >> shift);
            a = (a ^ byte) * 16777619U;
            b = ((b << 5) + b) ^ byte;
        }
    }
    for (size_t i = 0; username[i]; ++i) {
        uint8_t c = (uint8_t)tolower((unsigned char)username[i]);
        a = (a ^ c) * 16777619U;
        b = ((b << 5) + b) ^ c;
    }
    int n = snprintf(out, size, "/data/%08lx%08lx-%s", (unsigned long)a, (unsigned long)b, suffix);
    return n > 0 && (size_t)n < size && n - 6 < 32;
}

bool badge_file_write(const char *path, const void *data, size_t size) {
    if (!path || !data) return false;
    char temp[64], backup[64];
    if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp)) return false;
    if (snprintf(backup, sizeof(backup), "%s.bak", path) >= (int)sizeof(backup)) return false;
    if (!recover_file(path)) return false;
    FILE *f = fopen(temp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    if (fflush(f) != 0) ok = false;
    if (ok && fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (ok) {
        /* SPIFFS rename cannot replace an existing name. Retain a recoverable
         * backup until the new file has its final name. Dataset snapshots use
         * the stronger two-slot CRC scheme below. */
        struct stat info;
        bool previous = stat(path, &info) == 0;
        if (!previous && errno != ENOENT) ok = false;
        if (previous && rename(path, backup) != 0) ok = false;
        if (ok && rename(temp, path) != 0) {
            ok = false;
            if (previous) rename(backup, path);
        }
        if (ok) unlink(backup);
    }
    if (!ok) unlink(temp);
    return ok;
}

static bool read_slot(const char *path, const char *username, badge_data_t *data,
                      uint32_t *generation, bool *io_error) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (io_error && errno != ENOENT) *io_error = true;
        return false;
    }
    record_header_t h;
    bool ok = fread(&h, sizeof(h), 1, f) == 1 && h.magic == RECORD_MAGIC &&
        h.schema == BADGE_SCHEMA && h.size == sizeof(*data);
    if (ok) ok = fread(data, sizeof(*data), 1, f) == 1 && fgetc(f) == EOF;
    bool failed = ferror(f) != 0;
    if (fclose(f) != 0) failed = true;
    if (failed) {
        if (io_error) *io_error = true;
        return false;
    }
    if (ok) ok = crc32(data, sizeof(*data)) == h.crc &&
        badge_data_valid(data) &&
        strcasecmp(data->username, username) == 0;
    if (ok) *generation = h.generation;
    return ok;
}

bool badge_data_load(const char *username, badge_data_t *data) {
    if (!data || !badge_username_valid(username)) return false;
    char a[64], b[64];
    if (!badge_cache_path(username, "a", a, sizeof(a)) ||
        !badge_cache_path(username, "b", b, sizeof(b))) return false;
    badge_data_t *other = malloc(sizeof(*other));
    if (!other) return false;
    uint32_t ga = 0, gb = 0;
    bool oka = read_slot(a, username, data, &ga, NULL);
    bool okb = read_slot(b, username, other, &gb, NULL);
    if (okb && (!oka || (int32_t)(gb - ga) > 0)) *data = *other;
    free(other);
    if (!oka && !okb) badge_data_reset(data, username);
    return oka || okb;
}

bool badge_data_save(const badge_data_t *data) {
    return badge_data_save_at(s_cache_namespace, data);
}

bool badge_data_save_at(uint32_t space, const badge_data_t *data) {
    if (!badge_data_valid(data)) return false;
    char a[64], b[64];
    if (!badge_cache_path_at(space, data->username, "a", a, sizeof(a)) ||
        !badge_cache_path_at(space, data->username, "b", b, sizeof(b))) return false;
    badge_data_t *old = malloc(sizeof(*old));
    if (!old) return false;
    uint32_t ga = 0, gb = 0;
    bool io_error = false;
    bool oka = read_slot(a, data->username, old, &ga, &io_error);
    bool okb = read_slot(b, data->username, old, &gb, &io_error);
    free(old);
    /* A temporarily unreadable slot may be the newest valid snapshot. Do not
     * truncate it merely because its generation could not be inspected. */
    if (io_error) return false;
    bool a_latest = oka && (!okb || (int32_t)(ga - gb) >= 0);
    record_header_t h = { RECORD_MAGIC, BADGE_SCHEMA, sizeof(*data),
                         (a_latest ? ga : gb) + 1, crc32(data, sizeof(*data)) };
    /* Write the older slot directly; a power failure cannot invalidate the
     * latest verified snapshot. CRC rejects any torn replacement. */
    FILE *f = fopen(a_latest ? b : a, "wb");
    if (!f) return false;
    bool ok = fwrite(&h, sizeof(h), 1, f) == 1 && fwrite(data, sizeof(*data), 1, f) == 1;
    if (fflush(f) != 0) ok = false;
    if (ok && fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    return ok;
}

void badge_storage_remove_account(uint32_t space, const char *username) {
    const char *suffixes[] = {"a", "b", "avatar", "events", "job"};
    const char *endings[] = {".tmp", ".bak", ""};
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        bool staged_removed = true;
        for (size_t j = 0; j < sizeof(endings) / sizeof(endings[0]); ++j) {
            /* Keep the final file if a backup could not be removed, so boot
             * recovery cannot resurrect stale data in its place. */
            if (!endings[j][0] && !staged_removed) continue;
            char path[64], suffix[24];
            snprintf(suffix, sizeof(suffix), "%s%s", suffixes[i], endings[j]);
            if (!badge_cache_path_at(space, username, suffix, path, sizeof(path)) ||
                (unlink(path) != 0 && errno != ENOENT)) staged_removed = false;
        }
    }
}

bool badge_storage_namespace_empty(uint32_t space, const char *username) {
    struct stat info;
    const char *suffixes[] = {"a", "b", "avatar", "events", "job"};
    const char *endings[] = {"", ".bak", ".tmp"};
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        char path[64];
        for (size_t j = 0; j < sizeof(endings) / sizeof(endings[0]); ++j) {
            char suffix[24]; snprintf(suffix, sizeof(suffix), "%s%s", suffixes[i], endings[j]);
            if (!badge_cache_path_at(space, username, suffix, path, sizeof(path)) ||
                stat(path, &info) == 0 || errno != ENOENT) return false;
        }
    }
    return true;
}
