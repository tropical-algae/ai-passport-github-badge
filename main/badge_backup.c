#include "badge_backup.h"
#include "badge_storage.h"
#include "badge_sync.h"
#include "badge_sync_parse.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* v1 is a little-endian, fixed-schema local archive. Config sizes distinguish
 * the original config from the screen-timeout extension. Explicit sizes prevent
 * another firmware's ABI from being interpreted as this model. CRC detects
 * transfer/corruption, not authenticity. Never use filenames from the archive. */
typedef struct {
    char magic[8];
    uint32_t config_size, data_size, events_size, avatar_size, crc;
} backup_header_t;
static badge_backup_begin_fn s_begin;
static badge_backup_end_fn s_end;
void badge_backup_init(badge_backup_begin_fn begin, badge_backup_end_fn end) {
    s_begin = begin; s_end = end;
}
static void error_text(char *out, size_t size, const char *text) {
    if (out && size) snprintf(out, size, "%s", text);
}
static uint32_t update_crc(uint32_t crc, const void *bytes, size_t size) {
    const uint8_t *p = bytes;
    while (size--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc;
}
static bool file_size(const char *path, uint32_t *size, uint32_t maximum) {
    FILE *f = fopen(path, "rb");
    if (!f) { *size = 0; return errno == ENOENT; }
    bool ok = fseek(f, 0, SEEK_END) == 0;
    long n = ok ? ftell(f) : -1;
    fclose(f);
    if (n <= 0 || (unsigned long)n > maximum) return false;
    *size = (uint32_t)n;
    return true;
}
static bool stream_file(const char *path, uint32_t size, uint32_t *crc,
                        badge_backup_write_fn write, void *context) {
    if (!size) return true;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t block[768]; bool ok = true;
    while (size && ok) {
        size_t n = size < sizeof(block) ? size : sizeof(block);
        ok = fread(block, 1, n, f) == n;
        if (ok && crc) *crc = update_crc(*crc, block, n);
        if (ok && write) ok = write(block, n, context);
        size -= (uint32_t)n;
    }
    if (ok) ok = fgetc(f) == EOF;
    fclose(f);
    return ok;
}
static bool avatar_valid(const char *path, uint32_t size) {
    if (!size) return true;
    void *bytes = malloc(size);
    if (!bytes) return false;
    FILE *f = fopen(path, "rb");
    unsigned w = 0, h = 0;
    bool ok = f && fread(bytes, 1, size, f) == size && fgetc(f) == EOF &&
        badge_avatar_dimensions(bytes, size, &w, &h) && w && h && w <= 64 && h <= 64;
    if (f) fclose(f);
    free(bytes); return ok;
}
static uint32_t header_crc(const backup_header_t *h) {
    return update_crc(~0U, h, offsetof(backup_header_t, crc));
}
bool badge_backup_export(badge_backup_write_fn write, void *context, char *error, size_t size) {
    if (!write || !s_begin || !s_end) { error_text(error, size, "BACKUP NOT READY"); return false; }
    if (!s_begin(error, size)) return false;
    bool ok = false;
    badge_config_t config;
    badge_data_t *data = malloc(sizeof(*data));
    char events[64], avatar[64];
    backup_header_t h = {.magic = {'G','H','B','K','0','0','0','1'},
        .config_size = sizeof(config), .data_size = sizeof(*data)};
    error_text(error, size, "BACKUP READ FAILED");
    if (!data) { error_text(error, size, "NOT ENOUGH MEMORY"); goto done; }
    if (!badge_config_load(&config)) { error_text(error, size, "SAVE YOUR CONFIG FIRST"); goto done; }
    badge_data_reset(data, config.username);
    if (!badge_data_load(config.username, data)) {
        char snapshot[64];
        struct stat info;
        const char *slots[] = {"a", "b"};
        for (unsigned i = 0; i < 2; ++i) {
            if (!badge_cache_path(config.username, slots[i], snapshot, sizeof(snapshot)) ||
                stat(snapshot, &info) == 0 || errno != ENOENT) {
                error_text(error, size, "SNAPSHOT UNREADABLE / BACKUP CANCELLED"); goto done;
            }
        }
    }
    if (!badge_data_valid(data) ||
        !badge_cache_path(config.username, "events", events, sizeof(events)) ||
        !badge_cache_path(config.username, "avatar", avatar, sizeof(avatar)) ||
        !file_size(events, &h.events_size, 32768) || !file_size(avatar, &h.avatar_size, 24576)) goto done;
    if (((data->valid & BADGE_ACTIVITY) && !h.events_size) ||
        ((data->valid & BADGE_AVATAR) && !h.avatar_size) ||
        (h.events_size && !badge_sync_events_valid(events)) || !avatar_valid(avatar, h.avatar_size)) {
        error_text(error, size, "CACHE INCOMPLETE OR CORRUPT"); goto done;
    }
    uint32_t crc = update_crc(header_crc(&h), &config, sizeof(config));
    crc = update_crc(crc, data, sizeof(*data));
    if (!stream_file(events, h.events_size, &crc, NULL, NULL) ||
        !stream_file(avatar, h.avatar_size, &crc, NULL, NULL)) goto done;
    h.crc = ~crc;
    ok = write(&h, sizeof(h), context) && write(&config, sizeof(config), context) &&
        write(data, sizeof(*data), context) && stream_file(events, h.events_size, NULL, write, context) &&
        stream_file(avatar, h.avatar_size, NULL, write, context);
done:
    free(data);
    s_end(false);
    if (ok) error_text(error, size, "BACKUP EXPORTED");
    return ok;
}
static bool read_exact(badge_backup_read_fn read, void *context, void *bytes, size_t size, uint32_t *crc) {
    uint8_t *p = bytes;
    while (size) {
        int got = read(p, size, context);
        if (got <= 0 || (size_t)got > size) return false;
        if (crc) *crc = update_crc(*crc, p, (size_t)got);
        p += got; size -= (size_t)got;
    }
    return true;
}
static bool receive_file(badge_backup_read_fn read, void *context, const char *path,
                         uint32_t size, uint32_t *crc) {
    if (!size) return true;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint8_t block[768]; bool ok = true;
    while (size && ok) {
        size_t n = size < sizeof(block) ? size : sizeof(block);
        ok = read_exact(read, context, block, n, crc) && fwrite(block, 1, n, f) == n;
        size -= (uint32_t)n;
    }
    if (fflush(f) != 0) ok = false;
    if (ok && fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    return ok;
}
bool badge_backup_restore(badge_backup_read_fn read, void *context, size_t content_length,
                          char *error, size_t size) {
    if (!read || !s_begin || !s_end) { error_text(error, size, "RESTORE NOT READY"); return false; }
    if (content_length > BADGE_BACKUP_MAX_SIZE || content_length < sizeof(backup_header_t)) {
        error_text(error, size, "INVALID BACKUP SIZE"); return false;
    }
    if (!s_begin(error, size)) return false;
    bool ok = false, staged = false, activation_attempted = false;
    badge_config_t old_config, config;
    uint8_t config_bytes[sizeof(badge_config_t)];
    bool old_valid = badge_config_load(&old_config);
    uint32_t old_space = badge_storage_namespace();
    uint32_t next_space = old_space == UINT32_MAX ? 1 : old_space + 1;
    backup_header_t h;
    badge_data_t *data = malloc(sizeof(*data));
    char events[64], avatar[64];
    error_text(error, size, "INVALID OR INCOMPATIBLE BACKUP");
    if (!data) { error_text(error, size, "NOT ENOUGH MEMORY"); goto done; }
    if (!read_exact(read, context, &h, sizeof(h), NULL) || memcmp(h.magic, "GHBK0001", 8) ||
        (h.config_size != sizeof(config) && h.config_size != BADGE_LEGACY_CONFIG_SIZE) ||
        h.data_size != sizeof(*data) ||
        h.events_size > 32768 || h.avatar_size > 24576 ||
        content_length != sizeof(h) + h.config_size + sizeof(*data) + h.events_size + h.avatar_size) goto done;
    uint32_t crc = header_crc(&h);
    if (!read_exact(read, context, config_bytes, h.config_size, &crc) ||
        !badge_config_decode(config_bytes, h.config_size, &config) ||
        !read_exact(read, context, data, sizeof(*data), &crc) || !badge_data_valid(data) ||
        strcasecmp(config.username, data->username) ||
        ((data->valid & BADGE_ACTIVITY) && !h.events_size) ||
        ((data->valid & BADGE_AVATAR) && !h.avatar_size)) goto done;
    /* Never overwrite an existing namespace, including orphaned files from an
     * interrupted activation or a configuration that could not be read. */
    unsigned attempts = 0;
    while (!badge_storage_namespace_empty(next_space, config.username) && attempts++ < 100)
        next_space = next_space == UINT32_MAX ? 1 : next_space + 1;
    if (attempts >= 100 || next_space == old_space) {
        error_text(error, size, "NO UNUSED CACHE NAMESPACE"); goto done;
    }
    if (!badge_cache_path_at(next_space, config.username, "events", events, sizeof(events)) ||
        !badge_cache_path_at(next_space, config.username, "avatar", avatar, sizeof(avatar))) goto done;
    staged = true;
    error_text(error, size, "UPLOAD INTERRUPTED OR CACHE FULL");
    if (!receive_file(read, context, events, h.events_size, &crc) ||
        !receive_file(read, context, avatar, h.avatar_size, &crc)) goto done;
    error_text(error, size, "BACKUP CHECKSUM OR CONTENT INVALID");
    if (~crc != h.crc || (h.events_size && !badge_sync_events_valid(events)) ||
        !avatar_valid(avatar, h.avatar_size)) goto done;
    /* A sync cursor is intentionally not restored. A fresh collection can use
     * the journal's event IDs without double counting the imported heatmap. */
    data->retry_after = 0;
    snprintf(data->sync_message, sizeof(data->sync_message), "BACKUP RESTORED");
    error_text(error, size, "RESTORE SAVE FAILED / OLD DATA KEPT");
    if (!badge_data_save_at(next_space, data)) goto done;
    activation_attempted = true;
    if (!badge_storage_activate(&config, next_space)) goto done;
    ok = true;
    if (old_valid) badge_storage_remove_account(old_space, old_config.username);
    error_text(error, size, "RESTORED / RESTARTING");
done:
    /* NVS commit errors can have an uncertain persistence outcome. Retain both
     * namespaces after any activation attempt so either boot state is usable. */
    if (!ok && staged && !activation_attempted) badge_storage_remove_account(next_space, config.username);
    free(data);
    s_end(ok);
    return ok;
}
