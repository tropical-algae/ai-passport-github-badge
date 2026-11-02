#include "badge_sync_parse.h"
#include <string.h>

void badge_json_stream_init(badge_json_stream_t *s, char *buffer, size_t size,
                            bool array, badge_json_item_fn emit, void *context)
{
    *s = (badge_json_stream_t){ .buffer = buffer, .capacity = size,
        .array = array, .emit = emit, .context = context };
}

static bool whitespace(char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

bool badge_json_stream_feed(badge_json_stream_t *s, const char *bytes, size_t size)
{
    if (s->failed) return false;
    for (size_t i = 0; i < size; ++i) {
        char c = bytes[i];
        if (!s->depth) {
            if (whitespace(c)) continue;
            if (s->finished) goto fail;
            if (!s->started) {
                s->started = true;
                if (s->array) { if (c != '[') goto fail; continue; }
            }
            if (s->array && c == ']') {
                if (s->items && !s->need_comma) goto fail;
                s->finished = true;
                continue;
            }
            if (s->array && s->need_comma) {
                if (c != ',') goto fail;
                s->need_comma = false;
                continue;
            }
            if (c != '{') goto fail;
            s->used = 0;
            s->skipping = false;
            s->depth = 1;
            if (s->capacity < 2) goto fail;
            s->buffer[s->used++] = c;
            continue;
        }
        if (!s->skipping && s->used + 1 >= s->capacity) {
            if (!s->array || !s->skip_large_objects) goto fail;
            s->skipping = true;
        }
        if (!s->skipping) s->buffer[s->used++] = c;
        if (s->string) {
            if (s->escape) s->escape = false;
            else if (c == '\\') s->escape = true;
            else if (c == '"') s->string = false;
        } else if (c == '"') s->string = true;
        else if (c == '{' || c == '[') {
            if (++s->depth > 64) goto fail;
        } else if (c == '}' || c == ']') {
            if (--s->depth == 0) {
                if (s->skipping) ++s->dropped;
                else {
                    s->buffer[s->used] = 0;
                    if (!s->emit(s->buffer, s->used, s->context)) goto fail;
                }
                ++s->items;
                s->need_comma = true;
                if (!s->array) s->finished = true;
            }
        }
    }
    return true;
fail:
    s->failed = true;
    return false;
}

bool badge_json_stream_done(const badge_json_stream_t *s)
{ return s->started && s->finished && !s->failed && !s->depth && !s->string; }

bool badge_iso_day(const char *iso, int32_t *day)
{
    if (!iso || strlen(iso) < 10 || iso[4] != '-' || iso[7] != '-') return false;
    const unsigned positions[] = {0,1,2,3,5,6,8,9};
    for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i)
        if (iso[positions[i]] < '0' || iso[positions[i]] > '9') return false;
    int y = (iso[0]-'0')*1000 + (iso[1]-'0')*100 + (iso[2]-'0')*10 + iso[3]-'0';
    unsigned m = (unsigned)((iso[5]-'0')*10 + iso[6]-'0');
    unsigned d = (unsigned)((iso[8]-'0')*10 + iso[9]-'0');
    static const unsigned month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (y < 1970 || y > 2200 || m < 1 || m > 12 || d < 1) return false;
    unsigned max = month_days[m-1] + (m == 2 && y % 4 == 0 && (y % 100 || y % 400 == 0));
    if (d > max) return false;
    y -= m <= 2;
    int era = y / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    *day = era * 146097 + (int32_t)(yoe * 365 + yoe/4 - yoe/100 + doy) - 719468;
    return true;
}

static unsigned be16(const uint8_t *p) { return ((unsigned)p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p)
{ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

bool badge_avatar_dimensions(const void *bytes, size_t size, unsigned *width, unsigned *height)
{
    const uint8_t *p = bytes;
    if (!p || !width || !height || size > 24 * 1024 || size < 24) return false;
    if (!memcmp(p, "\x89PNG\r\n\x1a\n", 8)) {
        if (be32(p + 8) != 13 || memcmp(p + 12, "IHDR", 4)) return false;
        *width = be32(p + 16); *height = be32(p + 20);
    } else if (p[0] == 0xff && p[1] == 0xd8) {
        size_t at = 2;
        bool found = false;
        while (at + 4 <= size) {
            if (p[at++] != 0xff) return false;
            while (at < size && p[at] == 0xff) ++at;
            if (at >= size) return false;
            unsigned marker = p[at++];
            if (marker == 0xda || marker == 0xd9) break;
            if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
            if (at + 2 > size) return false;
            unsigned length = be16(p + at);
            if (length < 2 || at + length > size) return false;
            if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
                /* Tiny JPEG decoder supports baseline sequential JPEG only. */
                if (marker != 0xc0 || length < 8 || p[at + 2] != 8) return false;
                *height = be16(p + at + 3); *width = be16(p + at + 5);
                found = true; break;
            }
            at += length;
        }
        if (!found) return false;
    } else return false;
    return *width > 0 && *height > 0 && *width <= 64 && *height <= 64;
}

enum { SEARCH_START, SEARCH_KEY, SEARCH_KEY_STRING, SEARCH_COLON, SEARCH_VALUE,
       SEARCH_PRIMITIVE, SEARCH_NESTED, SEARCH_COMMA, SEARCH_STRING_VALUE, SEARCH_DONE };

void badge_search_stream_init(badge_search_stream_t *stream)
{ memset(stream, 0, sizeof(*stream)); }

static bool search_primitive(badge_search_stream_t *s)
{
    if (!s->field) return s->value_length > 0;
    if (s->value_length >= sizeof(s->value)) return false;
    s->value[s->value_length] = 0;
    if (s->field == 1) {
        if (!s->value_length || (s->value_length > 1 && s->value[0] == '0') || (s->found & 1)) return false;
        uint32_t number = 0;
        for (unsigned i = 0; i < s->value_length; ++i) {
            if (s->value[i] < '0' || s->value[i] > '9') return false;
            unsigned digit = (unsigned)(s->value[i] - '0');
            if (number > (UINT32_MAX - digit) / 10) return false;
            number = number * 10 + digit;
        }
        s->count = number; s->found |= 1;
    } else {
        if (s->found & 2) return false;
        if (!strcmp(s->value, "true")) s->incomplete = true;
        else if (!strcmp(s->value, "false")) s->incomplete = false;
        else return false;
        s->found |= 2;
    }
    return true;
}

bool badge_search_stream_feed(badge_search_stream_t *s, const char *bytes, size_t size)
{
    if (s->failed) return false;
    for (size_t i = 0; i < size; ++i) {
        char c = bytes[i];
        switch (s->phase) {
        case SEARCH_START:
            if (whitespace(c)) break;
            if (c != '{') goto fail;
            s->phase = SEARCH_KEY; break;
        case SEARCH_KEY:
            if (whitespace(c)) break;
            if (c != '"') goto fail;
            s->key_length = 0; s->escape = false; s->phase = SEARCH_KEY_STRING; break;
        case SEARCH_KEY_STRING:
            if (s->escape) { s->escape = false; c = '?'; }
            else if (c == '\\') { s->escape = true; break; }
            else if (c == '"') {
                s->key[s->key_length < sizeof(s->key) ? s->key_length : sizeof(s->key) - 1] = 0;
                s->field = !strcmp(s->key, "total_count") ? 1 : !strcmp(s->key, "incomplete_results") ? 2 : 0;
                s->phase = SEARCH_COLON; break;
            }
            if ((unsigned char)c < 32) goto fail;
            if (s->key_length + 1 < sizeof(s->key)) s->key[s->key_length] = c;
            if (s->key_length < sizeof(s->key)) ++s->key_length;
            break;
        case SEARCH_COLON:
            if (whitespace(c)) break;
            if (c != ':') goto fail;
            s->phase = SEARCH_VALUE; break;
        case SEARCH_VALUE:
            if (whitespace(c)) break;
            s->value_length = 0;
            if (c == '[' || c == '{') {
                if (s->field) goto fail;
                s->depth = 1; s->brackets[0] = c; s->string = s->escape = false; s->phase = SEARCH_NESTED;
            } else if (c == '"') {
                if (s->field) goto fail;
                s->escape = false; s->phase = SEARCH_STRING_VALUE;
            } else {
                if (c == ',' || c == '}') goto fail;
                s->value[0] = c; s->value_length = 1; s->phase = SEARCH_PRIMITIVE;
            }
            break;
        case SEARCH_PRIMITIVE:
            if (whitespace(c) || c == ',' || c == '}') {
                if (!search_primitive(s)) goto fail;
                s->phase = SEARCH_COMMA;
                if (c == ',') s->phase = SEARCH_KEY;
                else if (c == '}') s->phase = SEARCH_DONE;
            } else {
                if (s->field && s->value_length + 1 >= sizeof(s->value)) goto fail;
                if (s->value_length < sizeof(s->value)) s->value[s->value_length++] = c;
            }
            break;
        case SEARCH_NESTED:
            if (s->string) {
                if (s->escape) s->escape = false;
                else if (c == '\\') s->escape = true;
                else if (c == '"') s->string = false;
                else if ((unsigned char)c < 32) goto fail;
            } else if (c == '"') s->string = true;
            else if (c == '[' || c == '{') {
                if (s->depth == sizeof(s->brackets)) goto fail;
                s->brackets[s->depth++] = c;
            } else if (c == ']' || c == '}') {
                if (!s->depth || (c == ']' ? s->brackets[s->depth - 1] != '[' : s->brackets[s->depth - 1] != '{')) goto fail;
                if (!--s->depth) s->phase = SEARCH_COMMA;
            }
            break;
        case SEARCH_STRING_VALUE:
            if (s->escape) s->escape = false;
            else if (c == '\\') s->escape = true;
            else if (c == '"') s->phase = SEARCH_COMMA;
            else if ((unsigned char)c < 32) goto fail;
            break;
        case SEARCH_COMMA:
            if (whitespace(c)) break;
            if (c == ',') s->phase = SEARCH_KEY;
            else if (c == '}') s->phase = SEARCH_DONE;
            else goto fail;
            break;
        case SEARCH_DONE:
            if (!whitespace(c)) goto fail;
            break;
        default: goto fail;
        }
    }
    return true;
fail:
    s->failed = true; return false;
}

bool badge_search_stream_done(const badge_search_stream_t *s)
{ return !s->failed && s->phase == SEARCH_DONE && s->found == 3; }
