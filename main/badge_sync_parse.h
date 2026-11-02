#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*badge_json_item_fn)(const char *json, size_t size, void *context);
/* A bounded framing layer. Each framed object is subsequently validated by
 * cJSON; no complete GitHub array is ever allocated in RAM. */
typedef struct {
    char *buffer;
    size_t capacity, used;
    unsigned depth, items, dropped;
    bool array, started, finished, string, escape, failed, need_comma;
    bool skip_large_objects, skipping;
    badge_json_item_fn emit;
    void *context;
} badge_json_stream_t;
void badge_json_stream_init(badge_json_stream_t *stream, char *buffer, size_t size,
                            bool array, badge_json_item_fn emit, void *context);
bool badge_json_stream_feed(badge_json_stream_t *stream, const char *bytes, size_t size);
bool badge_json_stream_done(const badge_json_stream_t *stream);
bool badge_iso_day(const char *iso, int32_t *day);
bool badge_avatar_dimensions(const void *bytes, size_t size, unsigned *width, unsigned *height);

/* Search metadata projection: skips the potentially very large `items` value
 * without retaining issue bodies. Metadata may appear before or after items. */
typedef struct {
    char key[32], value[32], brackets[64];
    unsigned phase, key_length, value_length, depth, field, found;
    uint32_t count;
    bool incomplete, escape, string, failed;
} badge_search_stream_t;
void badge_search_stream_init(badge_search_stream_t *stream);
bool badge_search_stream_feed(badge_search_stream_t *stream, const char *bytes, size_t size);
bool badge_search_stream_done(const badge_search_stream_t *stream);
