/*******************************************************************************
 * Size: 10 px
 * Bpp: 1
 * Opts: --font ai-passport/assets/fonts/github-badge/Silkscreen-Regular.ttf --range 0x20-0x7E --size 10 --bpp 1 --format lvgl --no-compress --no-kerning --lv-font-name badge_font_10 --lv-fallback badge_font_cjk_10 --lv-include lvgl.h --output ai-passport/assets/fonts/github-badge/badge_font_10.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef BADGE_FONT_10
#define BADGE_FONT_10 1
#endif

#if BADGE_FONT_10

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xe4,

    /* U+0022 "\"" */
    0x99,

    /* U+0023 "#" */
    0x53, 0xf5, 0x14, 0xfd, 0x40,

    /* U+0024 "$" */
    0x13, 0xe0, 0xe1, 0x7, 0xc8,

    /* U+0025 "%" */
    0xc9, 0x90, 0xc0, 0x84, 0xe9, 0xc0,

    /* U+0026 "&" */
    0x13, 0xe0, 0xe8, 0x41, 0xe2,

    /* U+0027 "'" */
    0xc0,

    /* U+0028 "(" */
    0x6a, 0x90,

    /* U+0029 ")" */
    0x95, 0x20,

    /* U+002A "*" */
    0x12, 0x57, 0xa6, 0x94, 0x40,

    /* U+002B "+" */
    0x10, 0x4f, 0xc4, 0x10, 0x40,

    /* U+002C "," */
    0x60,

    /* U+002D "-" */
    0xf0,

    /* U+002E "." */
    0x80,

    /* U+002F "/" */
    0x25, 0x9, 0x0,

    /* U+0030 "0" */
    0x74, 0x63, 0x19, 0x38,

    /* U+0031 "1" */
    0xc4, 0x44, 0x4f,

    /* U+0032 "2" */
    0xf0, 0x5c, 0x8, 0x7c,

    /* U+0033 "3" */
    0xf0, 0x5c, 0x20, 0xf8,

    /* U+0034 "4" */
    0x94, 0xbe, 0x21, 0x8,

    /* U+0035 "5" */
    0xfc, 0x3c, 0x20, 0xf8,

    /* U+0036 "6" */
    0x74, 0x21, 0xe8, 0xb8,

    /* U+0037 "7" */
    0xf1, 0x24, 0x44,

    /* U+0038 "8" */
    0x74, 0x5d, 0x28, 0xb8,

    /* U+0039 "9" */
    0x74, 0x5e, 0x11, 0x38,

    /* U+003A ":" */
    0xa0,

    /* U+003B ";" */
    0x41, 0x80,

    /* U+003C "<" */
    0x2a, 0x44, 0x40,

    /* U+003D "=" */
    0xf0, 0xf0,

    /* U+003E ">" */
    0x88, 0x85, 0x0,

    /* U+003F "?" */
    0xf0, 0x5c, 0x0, 0x20,

    /* U+0040 "@" */
    0x7a, 0x59, 0xa0, 0x81, 0xe0,

    /* U+0041 "A" */
    0x74, 0x7f, 0x18, 0xc4,

    /* U+0042 "B" */
    0xf4, 0x7f, 0x19, 0x78,

    /* U+0043 "C" */
    0x74, 0x61, 0x8, 0xb8,

    /* U+0044 "D" */
    0xf4, 0x63, 0x19, 0x78,

    /* U+0045 "E" */
    0xf8, 0xf8, 0x8f,

    /* U+0046 "F" */
    0xf8, 0xf8, 0x88,

    /* U+0047 "G" */
    0x7c, 0x2f, 0x10, 0x38,

    /* U+0048 "H" */
    0x8c, 0x7f, 0x18, 0xc4,

    /* U+0049 "I" */
    0xfc,

    /* U+004A "J" */
    0x8, 0x42, 0x18, 0xb8,

    /* U+004B "K" */
    0x8c, 0xb9, 0x49, 0x44,

    /* U+004C "L" */
    0x88, 0x88, 0x8f,

    /* U+004D "M" */
    0x83, 0xde, 0x4c, 0x18, 0x30, 0x40,

    /* U+004E "N" */
    0x83, 0xc6, 0x4c, 0x98, 0xf0, 0x40,

    /* U+004F "O" */
    0x74, 0x63, 0x19, 0x38,

    /* U+0050 "P" */
    0xf4, 0x7d, 0x8, 0x40,

    /* U+0051 "Q" */
    0x74, 0x63, 0x19, 0x38, 0x20,

    /* U+0052 "R" */
    0xf4, 0x7d, 0x29, 0x44,

    /* U+0053 "S" */
    0x7c, 0x1c, 0x20, 0xf8,

    /* U+0054 "T" */
    0xf4, 0x44, 0x44,

    /* U+0055 "U" */
    0x8c, 0x63, 0x18, 0xb8,

    /* U+0056 "V" */
    0x86, 0x18, 0x12, 0x48, 0xc0,

    /* U+0057 "W" */
    0x86, 0x19, 0x65, 0x95, 0x20,

    /* U+0058 "X" */
    0x85, 0x23, 0x4, 0x4a, 0x10,

    /* U+0059 "Y" */
    0x85, 0x22, 0x4, 0x10, 0x40,

    /* U+005A "Z" */
    0xe5, 0x9, 0xc0,

    /* U+005B "[" */
    0xea, 0xb0,

    /* U+005C "\\" */
    0x91, 0x22, 0x40,

    /* U+005D "]" */
    0xd5, 0x70,

    /* U+005E "^" */
    0x54,

    /* U+005F "_" */
    0xf8,

    /* U+0060 "`" */
    0x90,

    /* U+0061 "a" */
    0x74, 0x7f, 0x18, 0xc4,

    /* U+0062 "b" */
    0xf4, 0x7f, 0x19, 0x78,

    /* U+0063 "c" */
    0x74, 0x61, 0x8, 0xb8,

    /* U+0064 "d" */
    0xf4, 0x63, 0x19, 0x78,

    /* U+0065 "e" */
    0xf8, 0xf8, 0x8f,

    /* U+0066 "f" */
    0xf8, 0xf8, 0x88,

    /* U+0067 "g" */
    0x7c, 0x2f, 0x10, 0x38,

    /* U+0068 "h" */
    0x8c, 0x7f, 0x18, 0xc4,

    /* U+0069 "i" */
    0xfc,

    /* U+006A "j" */
    0x8, 0x42, 0x18, 0xb8,

    /* U+006B "k" */
    0x8c, 0xb9, 0x49, 0x44,

    /* U+006C "l" */
    0x88, 0x88, 0x8f,

    /* U+006D "m" */
    0x83, 0xde, 0x4c, 0x18, 0x30, 0x40,

    /* U+006E "n" */
    0x83, 0xc6, 0x4c, 0x98, 0xf0, 0x40,

    /* U+006F "o" */
    0x74, 0x63, 0x19, 0x38,

    /* U+0070 "p" */
    0xf4, 0x7d, 0x8, 0x40,

    /* U+0071 "q" */
    0x74, 0x63, 0x19, 0x38, 0x20,

    /* U+0072 "r" */
    0xf4, 0x7d, 0x29, 0x44,

    /* U+0073 "s" */
    0x7c, 0x1c, 0x20, 0xf8,

    /* U+0074 "t" */
    0xf4, 0x44, 0x44,

    /* U+0075 "u" */
    0x8c, 0x63, 0x18, 0xb8,

    /* U+0076 "v" */
    0x86, 0x18, 0x12, 0x48, 0xc0,

    /* U+0077 "w" */
    0x86, 0x19, 0x65, 0x95, 0x20,

    /* U+0078 "x" */
    0x85, 0x23, 0x4, 0x4a, 0x10,

    /* U+0079 "y" */
    0x85, 0x22, 0x4, 0x10, 0x40,

    /* U+007A "z" */
    0xe5, 0x9, 0xc0,

    /* U+007B "{" */
    0x6a, 0x44, 0xc0,

    /* U+007C "|" */
    0xff,

    /* U+007D "}" */
    0xc8, 0xa5, 0x80,

    /* U+007E "~" */
    0x4d, 0x80
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 80, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 60, .box_w = 1, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2, .adv_w = 100, .box_w = 4, .box_h = 2, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 3, .adv_w = 140, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 8, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 13, .adv_w = 140, .box_w = 7, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 19, .adv_w = 120, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 24, .adv_w = 60, .box_w = 1, .box_h = 2, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 25, .adv_w = 80, .box_w = 2, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 27, .adv_w = 80, .box_w = 2, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 29, .adv_w = 140, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 34, .adv_w = 140, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 39, .adv_w = 80, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 40, .adv_w = 100, .box_w = 4, .box_h = 1, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 41, .adv_w = 60, .box_w = 1, .box_h = 1, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 42, .adv_w = 100, .box_w = 3, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 45, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 49, .adv_w = 100, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 52, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 56, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 60, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 64, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 68, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 72, .adv_w = 120, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 75, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 79, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 83, .adv_w = 60, .box_w = 1, .box_h = 3, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 84, .adv_w = 80, .box_w = 2, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 86, .adv_w = 100, .box_w = 3, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 89, .adv_w = 100, .box_w = 4, .box_h = 3, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 91, .adv_w = 100, .box_w = 3, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 94, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 98, .adv_w = 140, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 103, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 107, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 111, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 115, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 119, .adv_w = 100, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 122, .adv_w = 100, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 125, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 129, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 133, .adv_w = 60, .box_w = 1, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 134, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 138, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 142, .adv_w = 100, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 145, .adv_w = 140, .box_w = 7, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 151, .adv_w = 140, .box_w = 7, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 157, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 161, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 165, .adv_w = 120, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 170, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 174, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 178, .adv_w = 100, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 181, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 185, .adv_w = 140, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 190, .adv_w = 140, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 195, .adv_w = 140, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 200, .adv_w = 140, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 205, .adv_w = 100, .box_w = 3, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 208, .adv_w = 80, .box_w = 2, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 210, .adv_w = 100, .box_w = 3, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 213, .adv_w = 80, .box_w = 2, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 215, .adv_w = 100, .box_w = 3, .box_h = 2, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 216, .adv_w = 120, .box_w = 5, .box_h = 1, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 217, .adv_w = 80, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = 7},
    {.bitmap_index = 218, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 222, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 226, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 230, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 234, .adv_w = 100, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 237, .adv_w = 100, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 240, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 244, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 248, .adv_w = 60, .box_w = 1, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 249, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 253, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 257, .adv_w = 100, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 260, .adv_w = 140, .box_w = 7, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 266, .adv_w = 140, .box_w = 7, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 272, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 276, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 280, .adv_w = 120, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 285, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 289, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 293, .adv_w = 100, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 296, .adv_w = 120, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 300, .adv_w = 140, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 305, .adv_w = 140, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 310, .adv_w = 140, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 315, .adv_w = 140, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 320, .adv_w = 100, .box_w = 3, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 323, .adv_w = 100, .box_w = 3, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 326, .adv_w = 60, .box_w = 1, .box_h = 8, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 327, .adv_w = 100, .box_w = 3, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 330, .adv_w = 120, .box_w = 5, .box_h = 2, .ofs_x = 1, .ofs_y = 4}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};

extern const lv_font_t badge_font_cjk_10;


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t badge_font_10 = {
#else
lv_font_t badge_font_10 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 10,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = &badge_font_cjk_10,
#endif
    .user_data = NULL,
};



#endif /*#if BADGE_FONT_10*/

