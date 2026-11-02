#include "badge_sync_parse.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool item(const char *json, size_t length, void *ctx)
{
    unsigned *count = ctx;
    assert(length && json[0] == '{' && json[length - 1] == '}');
    ++*count;
    return true;
}

int main(void)
{
    const char *json = "[ {\"x\":\"}\\\"[\",\"y\":[{},3]}, {\"z\":2} ]";
    for (size_t chunk = 1; chunk <= strlen(json); ++chunk) {
        char buffer[128]; unsigned count = 0; badge_json_stream_t s;
        badge_json_stream_init(&s, buffer, sizeof(buffer), true, item, &count);
        for (size_t i = 0; i < strlen(json); i += chunk) {
            size_t n = strlen(json) - i; if (n > chunk) n = chunk;
            assert(badge_json_stream_feed(&s, json + i, n));
        }
        assert(badge_json_stream_done(&s) && count == 2);
    }
    const char *bad[] = {"[{},]", "[{}{}]", "[]{}", "[{\"x\":\"broken", "42"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        char buffer[128]; unsigned count = 0; badge_json_stream_t s;
        badge_json_stream_init(&s, buffer, sizeof(buffer), true, item, &count);
        badge_json_stream_feed(&s, bad[i], strlen(bad[i]));
        assert(!badge_json_stream_done(&s));
    }
    char small[4]; unsigned count = 0; badge_json_stream_t s;
    badge_json_stream_init(&s, small, sizeof(small), true, item, &count);
    assert(!badge_json_stream_feed(&s, "[{\"long\":1}]", 12));
    badge_json_stream_init(&s, small, sizeof(small), true, item, &count);
    s.skip_large_objects = true;
    const char *large = "[{\"long\":\"escaped \\\" } [ \",\"deep\":[{}]},{}]";
    assert(badge_json_stream_feed(&s, large, strlen(large)));
    assert(badge_json_stream_done(&s) && s.dropped == 1 && s.items == 2 && count == 1);
    int32_t day;
    assert(badge_iso_day("1970-01-01T00:00:00Z", &day) && day == 0);
    assert(badge_iso_day("2024-02-29", &day) && day == 19782);
    assert(!badge_iso_day("2023-02-29", &day));
    assert(!badge_iso_day("2024-13-01", &day));
    unsigned w, h;
    unsigned char png[33] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,54,0,0,0,54,8,6};
    assert(badge_avatar_dimensions(png, sizeof(png), &w, &h) && w == 54 && h == 54);
    for (size_t length = 24; length < sizeof(png); ++length)
        assert(!badge_avatar_dimensions(png, length, &w, &h));
    /* The gate accepts only the PNG specification's <=8-bit combinations.
     * In particular RGB/RGBA16 must never reach the LVGL decoder's undersized
     * source buffer, including through backup/cache/direct UI callers. */
    for (unsigned depth = 0; depth <= 32; ++depth) for (unsigned color = 0; color <= 7; ++color) {
        bool valid = (depth == 1 || depth == 2 || depth == 4 || depth == 8) &&
            (color == 0 || color == 3 || (depth == 8 && (color == 2 || color == 4 || color == 6)));
        png[24] = (unsigned char)depth; png[25] = (unsigned char)color;
        assert(badge_avatar_dimensions(png, sizeof(png), &w, &h) == valid);
    }
    png[24] = 8; png[25] = 6;
    png[28] = 1; /* Adam7 is supported. */
    assert(badge_avatar_dimensions(png, sizeof(png), &w, &h));
    png[28] = 0;
    for (unsigned at = 26; at <= 28; ++at) {
        png[at] = 2;
        assert(!badge_avatar_dimensions(png, sizeof(png), &w, &h));
        png[at] = 0;
    }
    png[19] = 255;
    assert(!badge_avatar_dimensions(png, sizeof(png), &w, &h));
    const unsigned char jpg[] = {255,216,255,224,0,4,0,0,255,192,0,17,8,0,54,0,54,3,1,0x11,0,2,0x11,0,3,0x11,0,255,217};
    assert(badge_avatar_dimensions(jpg, sizeof(jpg), &w, &h) && w == 54 && h == 54);
    assert(!badge_avatar_dimensions(jpg, 13, &w, &h));
    const char *search = "{\"items\":[{\"body\":\"braces } [ and \\\"quoted\\\"\"}],\"incomplete_results\":false,\"total_count\":123}";
    for (size_t chunk = 1; chunk <= strlen(search); ++chunk) {
        badge_search_stream_t meta; badge_search_stream_init(&meta);
        for (size_t i = 0; i < strlen(search); i += chunk) {
            size_t n = strlen(search) - i; if (n > chunk) n = chunk;
            assert(badge_search_stream_feed(&meta, search + i, n));
        }
        assert(badge_search_stream_done(&meta) && meta.count == 123 && !meta.incomplete);
    }
    const char *invalid_search[] = {
        "{\"total_count\":4294967296,\"incomplete_results\":false}",
        "{\"total_count\":12,\"incomplete_results\":false,}",
        "{\"total_count\":12,\"incomplete_results\":\"false\"}",
        "{\"total_count\":12,\"items\":[}",
        "{\"total_count\":12,\"total_count\":13,\"incomplete_results\":false}"
    };
    for (unsigned i = 0; i < sizeof(invalid_search) / sizeof(invalid_search[0]); ++i) {
        badge_search_stream_t meta; badge_search_stream_init(&meta);
        badge_search_stream_feed(&meta, invalid_search[i], strlen(invalid_search[i]));
        assert(!badge_search_stream_done(&meta));
    }
    puts("badge sync framing/date/avatar tests passed");
    return 0;
}
