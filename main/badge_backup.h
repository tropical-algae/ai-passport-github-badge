#pragma once
#include <stdbool.h>
#include <stddef.h>

#define BADGE_BACKUP_MAX_SIZE (128U * 1024U)
typedef bool (*badge_backup_write_fn)(const void *bytes, size_t size, void *context);
/* Return positive bytes read, zero for EOF, negative for transport failure. */
typedef int (*badge_backup_read_fn)(void *bytes, size_t size, void *context);
typedef bool (*badge_backup_begin_fn)(char *error, size_t size);
typedef void (*badge_backup_end_fn)(bool restored);
void badge_backup_init(badge_backup_begin_fn begin, badge_backup_end_fn end);
bool badge_backup_export(badge_backup_write_fn write, void *context, char *error, size_t size);
bool badge_backup_restore(badge_backup_read_fn read, void *context, size_t content_length,
                          char *error, size_t size);
