#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "badge_model.h"
#include "esp_err.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include "nvs.h"

static char test_directory[PATH_MAX];
static size_t fail_write_budget = SIZE_MAX;
static bool fail_sync, fail_commit;
static unsigned rename_calls, fail_rename_mask;
static esp_err_t mount_result = ESP_OK, nvs_result = ESP_OK;
static unsigned format_calls;
static uint8_t flash[512];
static badge_config_t committed_config, pending_config;
static size_t committed_size;
static unsigned char committed_state[256], pending_state[256];
static size_t state_size, pending_state_size;
static bool fail_state_read;
static int stat_failure_errno;
static unsigned unsupported_access_calls;
static const char *fail_read_path, *fail_unlink_path;

static const char *mapped(const char *path, char *result, size_t size)
{
    assert(!strncmp(path, "/data", 5));
    int length = snprintf(result, size, "%s%s", test_directory, path + 5);
    assert(length > 0 && (size_t)length < size);
    return result;
}

static FILE *test_fopen(const char *path, const char *mode)
{
    if (fail_read_path && !strcmp(mode, "rb") && !strcmp(path, fail_read_path)) { errno = EIO; return NULL; }
    char name[PATH_MAX];
    return fopen(mapped(path, name, sizeof(name)), mode);
}

static int test_access(const char *path, int mode)
{
    char name[PATH_MAX];
    return access(mapped(path, name, sizeof(name)), mode);
}

/* ESP-IDF 5.5.3 SPIFFS has stat but deliberately no access callback. Using
 * the native host access() here used to hide a device-only ENOSYS failure. */
static int test_spiffs_access(const char *path, int mode)
{
    (void)path; (void)mode;
    ++unsupported_access_calls;
    errno = ENOSYS;
    return -1;
}

static int test_stat(const char *path, struct stat *info)
{
    if (stat_failure_errno) { errno = stat_failure_errno; return -1; }
    char name[PATH_MAX];
    return stat(mapped(path, name, sizeof(name)), info);
}

static int test_unlink(const char *path)
{
    if (fail_unlink_path && !strcmp(path, fail_unlink_path)) { errno = EIO; return -1; }
    char name[PATH_MAX];
    return unlink(mapped(path, name, sizeof(name)));
}

static int test_rename(const char *from, const char *to)
{
    ++rename_calls;
    if (rename_calls < 32 && (fail_rename_mask & (1U << rename_calls))) { errno = ENOSPC; return -1; }
    char source[PATH_MAX], destination[PATH_MAX];
    mapped(from, source, sizeof(source)); mapped(to, destination, sizeof(destination));
    /* SPIFFS rename refuses to replace an existing name. */
    if (access(destination, F_OK) == 0) { errno = EEXIST; return -1; }
    return rename(source, destination);
}

static DIR *test_opendir(const char *path)
{
    char name[PATH_MAX];
    return opendir(mapped(path, name, sizeof(name)));
}

static size_t test_fwrite(const void *data, size_t size, size_t count, FILE *file)
{
    size_t bytes = size * count;
    if (fail_write_budget != SIZE_MAX && bytes > fail_write_budget) {
        size_t written = fwrite(data, 1, fail_write_budget, file);
        fail_write_budget = 0;
        errno = ENOSPC;
        return size ? written / size : 0;
    }
    if (fail_write_budget != SIZE_MAX) fail_write_budget -= bytes;
    return fwrite(data, size, count, file);
}

static int test_fsync(int fd)
{
    if (fail_sync) { errno = EIO; return -1; }
    return fsync(fd);
}

#define fopen test_fopen
#define access test_spiffs_access
#define stat(path, info) test_stat(path, info)
#define unlink test_unlink
#define rename test_rename
#define opendir test_opendir
#define fwrite test_fwrite
#define fsync test_fsync
#include "../main/badge_storage.c"
#undef fopen
#undef access
#undef stat
#undef unlink
#undef rename
#undef opendir
#undef fwrite
#undef fsync

esp_err_t nvs_flash_init(void) { return nvs_result; }
esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t *configuration)
{
    assert(!configuration->format_if_mount_failed);
    assert(!strcmp(configuration->partition_label, "badge_data"));
    return mount_result;
}
esp_err_t esp_spiffs_format(const char *partition)
{
    assert(!strcmp(partition, "badge_data"));
    ++format_calls; mount_result = ESP_OK; return ESP_OK;
}
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label)
{
    static const esp_partition_t partition = {sizeof(flash)};
    assert(type == ESP_PARTITION_TYPE_DATA && subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS);
    assert(!strcmp(label, "badge_data"));
    return &partition;
}
esp_err_t esp_partition_read(const esp_partition_t *partition, size_t offset, void *destination, size_t size)
{
    assert(offset + size <= partition->size);
    memcpy(destination, flash + offset, size); return ESP_OK;
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(!strcmp(name, "github_badge"));
    (void)mode; *handle = 1; pending_config = committed_config;
    memcpy(pending_state, committed_state, state_size); pending_state_size = state_size; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *size)
{
    (void)handle;
    if (!strcmp(key, "state_v2")) {
        if (fail_state_read) return ESP_FAIL;
        if (!state_size) return ESP_ERR_NVS_NOT_FOUND;
        if (*size < state_size) return ESP_FAIL;
        memcpy(data, committed_state, state_size); *size = state_size; return ESP_OK;
    }
    assert(!strcmp(key, "config_v1"));
    if (!committed_size || *size < committed_size) return ESP_FAIL;
    memcpy(data, &committed_config, committed_size); *size = committed_size; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size)
{
    (void)handle;
    if (!strcmp(key, "state_v2")) {
        assert(size <= sizeof(pending_state));
        memcpy(pending_state, data, size); pending_state_size = size; return ESP_OK;
    }
    assert(!strcmp(key, "config_v1") && size == sizeof(pending_config));
    memcpy(&pending_config, data, size); return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    if (fail_commit) return ESP_FAIL;
    committed_config = pending_config; /* Config_v1 is a legacy read-only fixture. */
    memcpy(committed_state, pending_state, pending_state_size); state_size = pending_state_size; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }

static void assert_file(const char *path, const char *expected)
{
    FILE *file = test_fopen(path, "rb"); assert(file);
    char bytes[100] = {0}; size_t size = fread(bytes, 1, sizeof(bytes), file); fclose(file);
    assert(size == strlen(expected) && !memcmp(bytes, expected, size));
}

static void mount_tests(void)
{
    mount_result = ESP_FAIL; memset(flash, 0xff, sizeof(flash)); flash[511] = 0;
    assert(badge_storage_init() == ESP_FAIL && format_calls == 0);
    memset(flash, 0xff, sizeof(flash));
    assert(badge_storage_init() == ESP_OK && format_calls == 1);
    nvs_result = ESP_FAIL;
    assert(badge_storage_init() == ESP_FAIL && format_calls == 1);
    nvs_result = ESP_OK;
}

/* The fixture deliberately duplicates the released v0.2.1 layout so a
 * current-model field change cannot silently alter compatibility coverage. */
typedef struct {
    char username[40], ssid[33], password[65];
    uint32_t interval_sec;
    int16_t utc_offset_min;
    uint8_t theme, brightness;
} released_config_t;
typedef struct {
    uint32_t magic, cache_namespace;
    released_config_t config;
    uint32_t crc;
} released_state_t;
_Static_assert(sizeof(released_config_t) == 148 && sizeof(released_state_t) == 160,
               "Released config fixture ABI changed");

static void config_tests(void)
{
    assert(test_spiffs_access("/data/unsupported-access", F_OK) == -1 && errno == ENOSYS);
    unsupported_access_calls = 0;
    struct stat missing_info;
    assert(test_stat("/data/missing-stat", &missing_info) == -1 && errno == ENOENT);
    badge_config_t config, loaded;
    badge_config_defaults(&config); strcpy(config.ssid, "test"); strcpy(config.password, "password"); strcpy(config.username, "octocat");
    assert(!badge_config_load(&loaded) && loaded.interval_sec == BADGE_DEFAULT_INTERVAL && loaded.screen_timeout_min == 10);
    config.screen_timeout_min = 30; config.brightness = 10;
    assert(badge_config_save(&config));
    assert(badge_config_load(&loaded) && !memcmp(&config, &loaded, sizeof(config)));
    config.theme = 3; fail_commit = true;
    assert(!badge_config_save(&config));
    assert(badge_config_load(&loaded) && loaded.theme == 0 && loaded.screen_timeout_min == 30);
    fail_commit = false;
    committed_state[20] ^= 0xff; /* CRC-protected state is rejected. */
    assert(!badge_config_load(&loaded) && !loaded.username[0]);
    /* Both released config_v1 and atomic state_v2 adopt the new default. */
    released_state_t legacy = {.magic = 0x47485332U, .cache_namespace = 42};
    memcpy(&legacy.config, &config, sizeof(legacy.config));
    legacy.crc = crc32(&legacy, offsetof(released_state_t, crc));
    state_size = 0; committed_size = sizeof(legacy.config);
    memcpy(&committed_config, &legacy.config, sizeof(legacy.config));
    assert(badge_config_load(&loaded) && loaded.theme == 3 && loaded.screen_timeout_min == 10);
    assert(badge_storage_namespace() == 0);
    memcpy(committed_state, &legacy, sizeof(legacy)); state_size = sizeof(legacy);
    assert(badge_config_load(&loaded) && loaded.theme == 3 && loaded.screen_timeout_min == 10);
    assert(badge_storage_namespace() == 42);
    /* A corrupt v2 must not fall back to valid but stale config_v1. */
    committed_state[20] ^= 0xff;
    assert(!badge_config_load(&loaded) && !loaded.username[0] && badge_storage_namespace() == 42);
    committed_state[20] ^= 0xff;
    fail_state_read = true;
    assert(!badge_config_load(&loaded) && badge_storage_namespace() == 42); fail_state_read = false;
    assert(badge_config_load(&loaded)); loaded.screen_timeout_min = 0;
    assert(badge_config_save(&loaded));
    assert(state_size == sizeof(config_state_t));
    assert(badge_config_load(&loaded) && loaded.screen_timeout_min == 0 && badge_storage_namespace() == 42);
    /* A CRC-valid current config with an unsupported timeout is rejected. */
    config_state_t current; memcpy(&current, committed_state, sizeof(current));
    current.config.screen_timeout_min = 2; current.crc = crc32(&current, offsetof(config_state_t, crc));
    memcpy(committed_state, &current, sizeof(current));
    assert(!badge_config_load(&loaded) && badge_storage_namespace() == 42);
    config.screen_timeout_min = 10; assert(badge_storage_activate(&config, 0));
}

static void replacement_tests(void)
{
    const char *path = "/data/0123456789abcdef-job";
    assert(badge_file_write(path, "old", 3));
    assert(badge_file_write(path, "new", 3)); assert_file(path, "new");
    stat_failure_errno = EIO;
    assert(!badge_file_write(path, "stat-error", 10)); assert_file(path, "new");
    assert(!badge_storage_namespace_empty(123, "octocat"));
    stat_failure_errno = 0;
    fail_write_budget = 2;
    assert(!badge_file_write(path, "incomplete", 10)); assert_file(path, "new");
    fail_write_budget = SIZE_MAX; fail_sync = true;
    assert(!badge_file_write(path, "unsynced", 8)); assert_file(path, "new");
    fail_sync = false; rename_calls = 0; fail_rename_mask = 1U << 2;
    assert(!badge_file_write(path, "renamefail", 10)); assert_file(path, "new");
    rename_calls = 0; fail_rename_mask = (1U << 2) | (1U << 3);
    assert(!badge_file_write(path, "rollbackfail", 12));
    assert(test_access(path, F_OK) != 0);
    fail_rename_mask = 0;
    /* A recovery stat error must retain the sole .bak rather than treating
     * every failure as 'missing' and discarding the recoverable old record. */
    stat_failure_errno = EIO;
    assert(badge_storage_init() == ESP_FAIL);
    char preserved_backup[80]; snprintf(preserved_backup, sizeof(preserved_backup), "%s.bak", path);
    assert_file(preserved_backup, "new");
    stat_failure_errno = 0;
    assert(badge_storage_init() == ESP_OK); assert_file(path, "new");
    char backup[80], temporary[80];
    snprintf(backup, sizeof(backup), "%s.bak", path); snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    assert(test_rename(path, backup) == 0);
    FILE *file = test_fopen(temporary, "wb"); assert(file); fputs("torn", file); fclose(file);
    assert(badge_storage_init() == ESP_OK); assert_file(path, "new");
    assert(test_access(temporary, F_OK) != 0 && test_access(backup, F_OK) != 0);
    /* A retry before reboot also restores an interrupted backup first. */
    assert(test_rename(path, backup) == 0); fail_write_budget = 1;
    assert(!badge_file_write(path, "retry", 5)); assert_file(path, "new");
    fail_write_budget = SIZE_MAX;
}

static void snapshot_tests(void)
{
    badge_data_t data, loaded;
    badge_data_reset(&data, "octocat"); data.followers = 1;
    assert(badge_data_save(&data));
    data.followers = 2; assert(badge_data_save(&data));
    assert(badge_data_load("OCTOCAT", &loaded) && loaded.followers == 2);
    char a[64], b[64], long_name[] = "abcdefghijklmnopqrstuvwxyz1234567890123";
    assert(strlen(long_name) == 39);
    assert(badge_cache_path(long_name, "events", a, sizeof(a)) && strlen(a) < 40);
    assert(!badge_cache_path("octocat", "../bad/path", a, sizeof(a)));
    assert(badge_cache_path("octocat", "a", a, sizeof(a)));
    assert(badge_cache_path("octocat", "b", b, sizeof(b)));
    /* A transient read error on latest b must not cause the next failing
     * save to truncate b based on an assumed older/missing generation. */
    fail_read_path = b; fail_write_budget = 0; data.followers = 99;
    assert(!badge_data_save(&data));
    fail_read_path = NULL; fail_write_budget = SIZE_MAX;
    assert(badge_data_load("octocat", &loaded) && loaded.followers == 2);
    FILE *file = test_fopen(b, "r+b"); assert(file);
    assert(fseek(file, sizeof(record_header_t) + 100, SEEK_SET) == 0); fputc(0xa5, file); fclose(file);
    assert(badge_data_load("octocat", &loaded) && loaded.followers == 1);
    data.followers = 3; assert(badge_data_save(&data));
    fail_write_budget = sizeof(record_header_t) + 17;
    data.followers = 4; assert(!badge_data_save(&data));
    fail_write_budget = SIZE_MAX;
    assert(badge_data_load("octocat", &loaded) && loaded.followers == 3);
    char wrong[64]; assert(badge_cache_path("someone-else", "a", wrong, sizeof(wrong)));
    assert(test_rename(b, wrong) == 0);
    assert(!badge_data_load("someone-else", &loaded) && !strcmp(loaded.username, "someone-else") && loaded.followers == 0);
    assert(!badge_data_save(NULL));
}

static void retired_namespace_tests(void)
{
    const char *suffixes[] = {"a", "b", "avatar", "events", "job"};
    const char *endings[] = {"", ".bak", ".tmp"};
    char path[64];
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
        for (size_t j = 0; j < sizeof(endings) / sizeof(endings[0]); ++j) {
            char suffix[24]; snprintf(suffix, sizeof(suffix), "%s%s", suffixes[i], endings[j]);
            assert(badge_cache_path_at(1000, "retired", suffix, path, sizeof(path)));
            FILE *file = test_fopen(path, "wb"); assert(file); fputs("retired", file); fclose(file);
        }
    assert(badge_cache_path_at(1001, "retired", "a", path, sizeof(path)));
    assert(badge_file_write(path, "keep", 4));
    char backup[64], final[64];
    assert(badge_cache_path_at(1000, "retired", "job.bak", backup, sizeof(backup)));
    assert(badge_cache_path_at(1000, "retired", "job", final, sizeof(final)));
    fail_unlink_path = backup;
    badge_storage_remove_account(1000, "retired");
    assert_file(final, "retired"); assert_file(backup, "retired");
    fail_unlink_path = NULL;
    badge_storage_remove_account(1000, "retired");
    assert(badge_storage_namespace_empty(1000, "retired"));
    assert_file(path, "keep");
    assert(badge_storage_init() == ESP_OK); /* No .bak resurrects retired files. */
    assert(badge_storage_namespace_empty(1000, "retired"));
}

#ifndef BADGE_BACKUP_TEST
int main(void)
{
    snprintf(test_directory, sizeof(test_directory), "/tmp/github-badge-storage-XXXXXX");
    assert(mkdtemp(test_directory));
    mount_tests(); config_tests(); replacement_tests(); snapshot_tests(); retired_namespace_tests();
    DIR *directory = opendir(test_directory); assert(directory); struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char path[PATH_MAX]; int length = snprintf(path, sizeof(path), "%s/%s", test_directory, entry->d_name);
        assert(length > 0 && (size_t)length < sizeof(path)); assert(unlink(path) == 0);
    }
    closedir(directory); assert(rmdir(test_directory) == 0);
    assert(unsupported_access_calls == 0);
    puts("Badge storage fault-injection tests: PASS (SPIFFS stat compatibility)");
    return 0;
}

#endif
