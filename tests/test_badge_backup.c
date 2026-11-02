#define BADGE_BACKUP_TEST
#include "test_badge_storage.c"
#define fopen test_fopen
#define access test_spiffs_access
#define stat(path, info) test_stat(path, info)
#define unlink test_unlink
#define fwrite test_fwrite
#define fsync test_fsync
#include "../main/badge_backup.c"
#undef fopen
#undef access
#undef stat
#undef unlink
#undef fwrite
#undef fsync

/* Event journal semantics have separate real-sync integration tests. Here the
 * validator stands in for that component to exercise transaction failures. */
bool badge_sync_events_valid(const char *path) {
    FILE *f = test_fopen(path, "rb"); if (!f) return false;
    char bytes[9]; bool ok = fread(bytes, 1, 9, f) == 9 && !memcmp(bytes, "journal-v", 9);
    fclose(f); return ok;
}
static bool busy, ended_restored;
static bool guard_begin(char *error, size_t size) {
    if (busy) { snprintf(error, size, "BUSY"); return false; }
    busy = true; ended_restored = false; return true;
}
static void guard_end(bool restored) { busy = false; ended_restored = restored; }
typedef struct { unsigned char bytes[BADGE_BACKUP_MAX_SIZE]; size_t length, offset, stop; } transport_t;
static bool send_bytes(const void *bytes, size_t size, void *context) {
    transport_t *t = context;
    if (t->length + size > t->stop || t->length + size > sizeof(t->bytes)) return false;
    memcpy(t->bytes + t->length, bytes, size); t->length += size; return true;
}
static int receive_bytes(void *bytes, size_t size, void *context) {
    transport_t *t = context;
    if (t->offset >= t->stop) return -1;
    if (size > 17) size = 17; /* arbitrary TCP segmentation */
    if (size > t->length - t->offset) size = t->length - t->offset;
    if (size > t->stop - t->offset) size = t->stop - t->offset;
    memcpy(bytes, t->bytes + t->offset, size); t->offset += size; return (int)size;
}
static void reset_transfer(transport_t *t) { t->offset = 0; t->stop = SIZE_MAX; }
static void recalc(transport_t *t) {
    backup_header_t *h = (void *)t->bytes;
    uint32_t crc = update_crc(header_crc(h), t->bytes + sizeof(*h), t->length - sizeof(*h));
    h->crc = ~crc;
}
static void check_old(const badge_config_t *old, uint32_t space) {
    badge_config_t loaded; badge_data_t data;
    assert(badge_config_load(&loaded) && !memcmp(&loaded, old, sizeof(*old)));
    assert(badge_storage_namespace() == space);
    assert(badge_data_load(old->username, &data) && data.followers == 999);
}
static void backup_tests(void) {
    badge_backup_init(guard_begin, guard_end);
    badge_config_t original, target, loaded;
    badge_config_defaults(&original); strcpy(original.username, "octocat");
    strcpy(original.ssid, "backup wifi"); strcpy(original.password, "backup-secret");
    original.interval_sec = 43200; original.theme = 4; original.brightness = 10; original.screen_timeout_min = 30;
    assert(badge_storage_activate(&original, 10));
    badge_data_t data, restored; badge_data_reset(&data, original.username);
    data.valid = BADGE_PROFILE | BADGE_ACTIVITY; data.followers = 321;
    data.days[0] = (badge_day_t){20000, 7, 3, BADGE_KNOWN}; data.last_success = 1700000000;
    char path[64]; assert(badge_cache_path(original.username, "events", path, sizeof(path)));
    assert(badge_file_write(path, "journal-v", 9)); assert(badge_data_save(&data));
    transport_t *archive = calloc(1, sizeof(*archive)); assert(archive);
    archive->stop = SIZE_MAX; char error[120];
    assert(badge_backup_export(send_bytes, archive, error, sizeof(error)) && !busy);
    assert(archive->length > sizeof(data));
    /* Existing target state differs in every configurable field. */
    target = original; strcpy(target.username, "another-user"); strcpy(target.ssid, "old wifi");
    strcpy(target.password, "old-secret"); target.interval_sec = 21600; target.theme = 1; target.brightness = 100; target.screen_timeout_min = 0;
    assert(badge_storage_activate(&target, 11));
    badge_data_reset(&data, target.username); data.followers = 999; assert(badge_data_save(&data));
    uint32_t space = badge_storage_namespace();
    fail_state_read = true;
    assert(!badge_config_load(&loaded) && badge_storage_namespace() == space);
    fail_state_read = false;
    /* Every truncated prefix is rejected; old NVS and cache still load. */
    for (size_t n = 0; n < archive->length; n += 193) {
        reset_transfer(archive); archive->stop = n;
        assert(!badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)));
        assert(!busy && !ended_restored); check_old(&target, space);
    }
    reset_transfer(archive); archive->bytes[archive->length - 1] ^= 1;
    assert(!badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)));
    check_old(&target, space); archive->bytes[archive->length - 1] ^= 1;
    /* Recomputed CRC must not make an out-of-bounds model acceptable. */
    badge_data_t payload_model;
    unsigned char *payload_bytes = archive->bytes + sizeof(backup_header_t) + sizeof(original);
    memcpy(&payload_model, payload_bytes, sizeof(payload_model));
    badge_data_t *payload = &payload_model;
    payload->scope_count = 255; memcpy(payload_bytes, payload, sizeof(*payload)); recalc(archive); reset_transfer(archive);
    assert(!badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)));
    check_old(&target, space); payload->scope_count = 0; memcpy(payload_bytes, payload, sizeof(*payload)); recalc(archive);
    payload->scope_count = 1; payload->scopes[0].count = 1;
    payload->scopes[0].total_bytes = 1; payload->scopes[0].languages[0].bytes = 934553069;
    memcpy(payload_bytes, payload, sizeof(*payload)); recalc(archive); reset_transfer(archive);
    assert(!badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)));
    check_old(&target, space);
    payload->scope_count = 0; memset(&payload->scopes[0], 0, sizeof(payload->scopes[0])); memcpy(payload_bytes, payload, sizeof(*payload)); recalc(archive);
    payload->last_success = INT64_MAX; memcpy(payload_bytes, payload, sizeof(*payload)); recalc(archive); reset_transfer(archive);
    assert(!badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)));
    check_old(&target, space); payload->last_success = 1700000000; memcpy(payload_bytes, payload, sizeof(*payload)); recalc(archive);
    /* A valid checksum cannot authorize an unsupported screen timeout. */
    badge_config_t malformed = original; malformed.screen_timeout_min = 2;
    memcpy(archive->bytes + sizeof(backup_header_t), &malformed, sizeof(malformed));
    recalc(archive); reset_transfer(archive);
    assert(!badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)));
    check_old(&target, space);
    memcpy(archive->bytes + sizeof(backup_header_t), &original, sizeof(original)); recalc(archive);
    reset_transfer(archive); stat_failure_errno = EIO;
    assert(!badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)));
    stat_failure_errno = 0; check_old(&target, space);
    reset_transfer(archive); fail_write_budget = 4;
    assert(!badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)));
    fail_write_budget = SIZE_MAX; check_old(&target, space);
    reset_transfer(archive); fail_sync = true;
    assert(!badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)));
    fail_sync = false; check_old(&target, space);
    reset_transfer(archive); fail_commit = true;
    assert(!badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)));
    fail_commit = false; check_old(&target, space);
    reset_transfer(archive); busy = true;
    assert(!badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)));
    assert(archive->offset == 0); busy = false;
    reset_transfer(archive);
    assert(badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)) && ended_restored && !busy);
    assert(badge_config_load(&loaded) && !memcmp(&original, &loaded, sizeof(original)));
    assert(badge_storage_namespace() == space + 2);
    assert(badge_data_load(original.username, &restored) && restored.followers == 321 &&
        restored.days[0].events == 7 && restored.last_success == 1700000000 && restored.retry_after == 0);
    assert(badge_cache_path(original.username, "events", path, sizeof(path)) && badge_sync_events_valid(path));
    /* Ordinary settings saves after a reboot preserve the imported namespace. */
    loaded.theme = 2; assert(badge_config_save(&loaded));
    assert(badge_config_load(&loaded) && loaded.theme == 2 && badge_storage_namespace() == space + 2);
    /* Produce a released v0.2.1 archive with its original 148-byte config.
     * All remaining payload bytes and their CRC still follow the old layout. */
    backup_header_t *header = (void *)archive->bytes;
    size_t old_data_offset = sizeof(*header) + sizeof(original);
    memmove(archive->bytes + sizeof(*header) + sizeof(released_config_t),
        archive->bytes + old_data_offset, archive->length - old_data_offset);
    archive->length -= sizeof(original) - sizeof(released_config_t);
    header->config_size = sizeof(released_config_t); recalc(archive);
    /* Legacy data must pass the same integrity checks before activation. */
    uint32_t imported_space = badge_storage_namespace();
    archive->bytes[archive->length - 1] ^= 1; reset_transfer(archive);
    assert(!badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)));
    assert(badge_config_load(&loaded) && loaded.screen_timeout_min == 30 &&
        badge_storage_namespace() == imported_space);
    archive->bytes[archive->length - 1] ^= 1;
    header->config_size = sizeof(released_config_t) + 1; recalc(archive); reset_transfer(archive);
    assert(!badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)));
    assert(badge_storage_namespace() == imported_space);
    header->config_size = sizeof(released_config_t); recalc(archive); reset_transfer(archive);
    /* Reloaded NVS may change the namespace after an uncertain prior write;
     * retirement must use that reloaded value, never the stale RAM value. */
    char unrelated[64]; assert(badge_cache_path_at(777, original.username, "a", unrelated, sizeof(unrelated)));
    assert(badge_file_write(unrelated, "unrelated-cache", 15));
    s_cache_namespace = 777;
    assert(badge_backup_restore(receive_bytes, archive, archive->length, error, sizeof(error)) && ended_restored);
    assert(badge_storage_namespace() == imported_space + 1); assert_file(unrelated, "unrelated-cache");
    assert(badge_config_load(&loaded) && loaded.screen_timeout_min == 10);
    original.screen_timeout_min = 10;
    assert(!memcmp(&original, &loaded, sizeof(original)));
    assert(badge_data_load(original.username, &restored) && restored.followers == 321 && restored.days[0].events == 7);
    assert(badge_cache_path(original.username, "events", path, sizeof(path)) && badge_sync_events_valid(path));
    /* A fresh backup after migration uses the extended config. */
    archive->length = 0; reset_transfer(archive);
    assert(badge_backup_export(send_bytes, archive, error, sizeof(error)));
    assert(((backup_header_t *)(void *)archive->bytes)->config_size == sizeof(badge_config_t));
    free(archive);
}
static void backup_empty_snapshot_tests(void) {
    badge_config_t config, loaded; badge_config_defaults(&config);
    strcpy(config.username, "empty-cache"); strcpy(config.ssid, "test");
    assert(badge_storage_activate(&config, 900));
    transport_t *archive = calloc(1, sizeof(*archive)); assert(archive);
    reset_transfer(archive); char error[120];
    /* No snapshots is a supported first-use backup, not an unsupported
     * access() error. A real stat I/O error must still stop export. */
    assert(badge_backup_export(send_bytes, archive, error, sizeof(error)));
    archive->length = 0; stat_failure_errno = EIO;
    assert(!badge_backup_export(send_bytes, archive, error, sizeof(error)));
    stat_failure_errno = 0;
    assert(archive->length == 0 && !busy);
    assert(badge_config_load(&loaded) && !memcmp(&config, &loaded, sizeof(config)) &&
        badge_storage_namespace() == 900);
    char path[64]; assert(badge_cache_path(config.username, "a", path, sizeof(path)));
    assert(badge_file_write(path, "corrupt snapshot", 16));
    assert(!badge_backup_export(send_bytes, archive, error, sizeof(error)));
    assert(archive->length == 0 && !busy); assert_file(path, "corrupt snapshot");
    free(archive);
}
int main(void) {
    snprintf(test_directory, sizeof(test_directory), "/tmp/github-badge-backup-XXXXXX"); assert(mkdtemp(test_directory));
    mount_tests(); config_tests(); replacement_tests(); snapshot_tests(); retired_namespace_tests(); backup_tests(); backup_empty_snapshot_tests();
    DIR *directory = opendir(test_directory); assert(directory); struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char path[PATH_MAX]; int n = snprintf(path, sizeof(path), "%s/%s", test_directory, entry->d_name);
        assert(n > 0 && (size_t)n < sizeof(path)); assert(unlink(path) == 0);
    }
    closedir(directory); assert(rmdir(test_directory) == 0);
    assert(unsupported_access_calls == 0);
    puts("Badge backup restore transaction/fault tests: PASS (SPIFFS stat compatibility)"); return 0;
}
