/*******************************************************************************
 * Size: 14 px
 * Bpp: 1
 * Opts: --font ai-passport/assets/fonts/github-badge/Silkscreen-Regular.ttf --range 0x20-0x7E --size 14 --bpp 1 --format lvgl --no-compress --no-kerning --lv-font-name badge_font_14 --lv-fallback badge_font_cjk_10 --lv-include lvgl.h --output ai-passport/assets/fonts/github-badge/badge_font_14.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef BADGE_FONT_14
#define BADGE_FONT_14 1
#endif

#if BADGE_FONT_14

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xff, 0xc3, 0xc0,

    /* U+0022 "\"" */
    0xde, 0xf6,

    /* U+0023 "#" */
    0x33, 0x19, 0xbf, 0xff, 0xf3, 0x37, 0xff, 0xfe,
    0x66, 0x33, 0x0,

    /* U+0024 "$" */
    0x8, 0x10, 0xf9, 0xfc, 0x18, 0xe, 0x3, 0x7,
    0xf3, 0xe1, 0x83, 0x0,

    /* U+0025 "%" */
    0xf6, 0x7b, 0x3d, 0x82, 0x0, 0x80, 0x40, 0xde,
    0x6f, 0x37, 0x80,

    /* U+0026 "&" */
    0x18, 0x30, 0xf9, 0xfc, 0x18, 0xe, 0x60, 0xc0,
    0x7c, 0xf8, 0xc1, 0x80,

    /* U+0027 "'" */
    0xfc,

    /* U+0028 "(" */
    0x33, 0xcc, 0xcc, 0xc3, 0x30,

    /* U+0029 ")" */
    0xcc, 0x33, 0x33, 0x3c, 0xc0,

    /* U+002A "*" */
    0x18, 0xc, 0x36, 0x7b, 0x33, 0xe6, 0xcf, 0x66,
    0x30, 0x18, 0x0,

    /* U+002B "+" */
    0x18, 0xc, 0x6, 0x3, 0xf, 0xff, 0xfc, 0x60,
    0x30, 0x18, 0x0,

    /* U+002C "," */
    0x33, 0xcc,

    /* U+002D "-" */
    0xff, 0xc0,

    /* U+002E "." */
    0xf0,

    /* U+002F "/" */
    0x8, 0x42, 0x3, 0x1b, 0x18, 0xc0,

    /* U+0030 "0" */
    0x38, 0x73, 0x1e, 0x3c, 0x78, 0xf1, 0x9c, 0x38,

    /* U+0031 "1" */
    0xf7, 0x8c, 0x63, 0x18, 0xdf, 0xf8,

    /* U+0032 "2" */
    0xf9, 0xf0, 0x18, 0x33, 0x98, 0x30, 0x7f, 0xfe,

    /* U+0033 "3" */
    0xf9, 0xf0, 0x18, 0x37, 0x80, 0xc1, 0xfc, 0xf8,

    /* U+0034 "4" */
    0xd9, 0xb3, 0x66, 0xcf, 0xff, 0xc6, 0xc, 0x18,

    /* U+0035 "5" */
    0xff, 0xff, 0x6, 0xf, 0x80, 0xc1, 0xfc, 0xf8,

    /* U+0036 "6" */
    0x38, 0x73, 0x6, 0xf, 0x9f, 0xf1, 0x9c, 0x38,

    /* U+0037 "7" */
    0xff, 0xfc, 0x8, 0x10, 0xc1, 0x8c, 0x18, 0x30,

    /* U+0038 "8" */
    0x38, 0x73, 0x1e, 0x33, 0x9f, 0xf1, 0x9c, 0x38,

    /* U+0039 "9" */
    0x38, 0x73, 0x1f, 0xf3, 0xe0, 0xc1, 0x9c, 0x38,

    /* U+003A ":" */
    0xf3, 0xc0,

    /* U+003B ";" */
    0x33, 0x3, 0x3c, 0xc0,

    /* U+003C "<" */
    0x8, 0x4c, 0x6c, 0x18, 0xc1, 0x8,

    /* U+003D "=" */
    0xff, 0xc1, 0xff, 0x80,

    /* U+003E ">" */
    0xc6, 0xc, 0x60, 0x98, 0xd8, 0xc0,

    /* U+003F "?" */
    0xfd, 0xf8, 0x8, 0x13, 0xc7, 0x80, 0x18, 0x30,

    /* U+0040 "@" */
    0x3e, 0x1f, 0x36, 0x7b, 0x3d, 0xe6, 0xf3, 0x0,
    0x7c, 0x3e, 0x0,

    /* U+0041 "A" */
    0x38, 0x73, 0x1e, 0x3f, 0xff, 0xf1, 0xe3, 0xc6,

    /* U+0042 "B" */
    0xf9, 0xf3, 0x1e, 0x3f, 0xff, 0xf1, 0xfc, 0xf8,

    /* U+0043 "C" */
    0x38, 0x73, 0x1e, 0x3c, 0x18, 0xf1, 0x9c, 0x38,

    /* U+0044 "D" */
    0xf9, 0xf3, 0x1e, 0x3c, 0x78, 0xf1, 0xfc, 0xf8,

    /* U+0045 "E" */
    0xff, 0xfc, 0x30, 0xff, 0xfc, 0x3f, 0xfc,

    /* U+0046 "F" */
    0xff, 0xfc, 0x30, 0xff, 0xfc, 0x30, 0xc0,

    /* U+0047 "G" */
    0x3e, 0x7f, 0x6, 0xd, 0xfb, 0xf1, 0x9c, 0x38,

    /* U+0048 "H" */
    0xc7, 0x8f, 0x1e, 0x3f, 0xff, 0xf1, 0xe3, 0xc6,

    /* U+0049 "I" */
    0xff, 0xff, 0xc0,

    /* U+004A "J" */
    0x6, 0xc, 0x18, 0x30, 0x78, 0xf1, 0x9c, 0x38,

    /* U+004B "K" */
    0xc7, 0x8f, 0x66, 0xce, 0x1b, 0x36, 0x63, 0xc6,

    /* U+004C "L" */
    0xc3, 0xc, 0x30, 0xc3, 0xc, 0x3f, 0xfc,

    /* U+004D "M" */
    0xc1, 0xe0, 0xf9, 0xfc, 0xfd, 0x9e, 0xcf, 0x7,
    0x83, 0xc1, 0x80,

    /* U+004E "N" */
    0xc1, 0xe0, 0xf8, 0x7c, 0x3d, 0x9e, 0x3f, 0x1f,
    0x83, 0xc1, 0x80,

    /* U+004F "O" */
    0x38, 0x73, 0x1e, 0x3c, 0x78, 0xf1, 0x9c, 0x38,

    /* U+0050 "P" */
    0xf9, 0xf3, 0x1e, 0x3f, 0x9f, 0x30, 0x60, 0xc0,

    /* U+0051 "Q" */
    0x38, 0x73, 0x1e, 0x3c, 0x78, 0xf1, 0x9c, 0x38,
    0xc, 0x18,

    /* U+0052 "R" */
    0xf9, 0xf3, 0x1e, 0x3f, 0x9f, 0x36, 0x63, 0xc6,

    /* U+0053 "S" */
    0x3e, 0x7f, 0x6, 0x3, 0x80, 0xc1, 0xfc, 0xf8,

    /* U+0054 "T" */
    0xff, 0xcc, 0x63, 0x18, 0xc6, 0x30,

    /* U+0055 "U" */
    0xc7, 0x8f, 0x1e, 0x3c, 0x78, 0xf1, 0x9c, 0x38,

    /* U+0056 "V" */
    0xc1, 0xe0, 0xf0, 0x68, 0x3, 0x61, 0xb0, 0xd8,
    0x10, 0x8, 0x0,

    /* U+0057 "W" */
    0xc1, 0xe0, 0xf2, 0x79, 0x3c, 0x9e, 0x4f, 0x26,
    0x6c, 0x36, 0x0,

    /* U+0058 "X" */
    0xc1, 0xe0, 0xcd, 0x86, 0xc0, 0x81, 0xb0, 0xd9,
    0x83, 0xc1, 0x80,

    /* U+0059 "Y" */
    0xc1, 0xe0, 0xcd, 0x86, 0xc0, 0x80, 0x40, 0x20,
    0x10, 0x8, 0x0,

    /* U+005A "Z" */
    0xff, 0xc2, 0x13, 0x63, 0x1f, 0xf8,

    /* U+005B "[" */
    0xff, 0xcc, 0xcc, 0xcf, 0xf0,

    /* U+005C "\\" */
    0xc6, 0x30, 0x83, 0x18, 0x21, 0x8,

    /* U+005D "]" */
    0xff, 0x33, 0x33, 0x3f, 0xf0,

    /* U+005E "^" */
    0x31, 0xb3, 0x90,

    /* U+005F "_" */
    0xff, 0xfc,

    /* U+0060 "`" */
    0xcc, 0x33,

    /* U+0061 "a" */
    0x38, 0x73, 0x1e, 0x3f, 0xff, 0xf1, 0xe3, 0xc6,

    /* U+0062 "b" */
    0xf9, 0xf3, 0x1e, 0x3f, 0xff, 0xf1, 0xfc, 0xf8,

    /* U+0063 "c" */
    0x38, 0x73, 0x1e, 0x3c, 0x18, 0xf1, 0x9c, 0x38,

    /* U+0064 "d" */
    0xf9, 0xf3, 0x1e, 0x3c, 0x78, 0xf1, 0xfc, 0xf8,

    /* U+0065 "e" */
    0xff, 0xfc, 0x30, 0xff, 0xfc, 0x3f, 0xfc,

    /* U+0066 "f" */
    0xff, 0xfc, 0x30, 0xff, 0xfc, 0x30, 0xc0,

    /* U+0067 "g" */
    0x3e, 0x7f, 0x6, 0xd, 0xfb, 0xf1, 0x9c, 0x38,

    /* U+0068 "h" */
    0xc7, 0x8f, 0x1e, 0x3f, 0xff, 0xf1, 0xe3, 0xc6,

    /* U+0069 "i" */
    0xff, 0xff, 0xc0,

    /* U+006A "j" */
    0x6, 0xc, 0x18, 0x30, 0x78, 0xf1, 0x9c, 0x38,

    /* U+006B "k" */
    0xc7, 0x8f, 0x66, 0xce, 0x1b, 0x36, 0x63, 0xc6,

    /* U+006C "l" */
    0xc3, 0xc, 0x30, 0xc3, 0xc, 0x3f, 0xfc,

    /* U+006D "m" */
    0xc1, 0xe0, 0xf9, 0xfc, 0xfd, 0x9e, 0xcf, 0x7,
    0x83, 0xc1, 0x80,

    /* U+006E "n" */
    0xc1, 0xe0, 0xf8, 0x7c, 0x3d, 0x9e, 0x3f, 0x1f,
    0x83, 0xc1, 0x80,

    /* U+006F "o" */
    0x38, 0x73, 0x1e, 0x3c, 0x78, 0xf1, 0x9c, 0x38,

    /* U+0070 "p" */
    0xf9, 0xf3, 0x1e, 0x3f, 0x9f, 0x30, 0x60, 0xc0,

    /* U+0071 "q" */
    0x38, 0x73, 0x1e, 0x3c, 0x78, 0xf1, 0x9c, 0x38,
    0xc, 0x18,

    /* U+0072 "r" */
    0xf9, 0xf3, 0x1e, 0x3f, 0x9f, 0x36, 0x63, 0xc6,

    /* U+0073 "s" */
    0x3e, 0x7f, 0x6, 0x3, 0x80, 0xc1, 0xfc, 0xf8,

    /* U+0074 "t" */
    0xff, 0xcc, 0x63, 0x18, 0xc6, 0x30,

    /* U+0075 "u" */
    0xc7, 0x8f, 0x1e, 0x3c, 0x78, 0xf1, 0x9c, 0x38,

    /* U+0076 "v" */
    0xc1, 0xe0, 0xf0, 0x68, 0x3, 0x61, 0xb0, 0xd8,
    0x10, 0x8, 0x0,

    /* U+0077 "w" */
    0xc1, 0xe0, 0xf2, 0x79, 0x3c, 0x9e, 0x4f, 0x26,
    0x6c, 0x36, 0x0,

    /* U+0078 "x" */
    0xc1, 0xe0, 0xcd, 0x86, 0xc0, 0x81, 0xb0, 0xd9,
    0x83, 0xc1, 0x80,

    /* U+0079 "y" */
    0xc1, 0xe0, 0xcd, 0x86, 0xc0, 0x80, 0x40, 0x20,
    0x10, 0x8, 0x0,

    /* U+007A "z" */
    0xff, 0xc2, 0x13, 0x63, 0x1f, 0xf8,

    /* U+007B "{" */
    0x3c, 0xf3, 0xc, 0xc0, 0xc3, 0xf, 0x3c,

    /* U+007C "|" */
    0xff, 0xff, 0xff,

    /* U+007D "}" */
    0xf3, 0xc3, 0xc, 0xc, 0xc3, 0x3c, 0xf0,

    /* U+007E "~" */
    0x36, 0x6f, 0x26, 0x40
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 112, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 84, .box_w = 2, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 4, .adv_w = 140, .box_w = 5, .box_h = 3, .ofs_x = 2, .ofs_y = 6},
    {.bitmap_index = 6, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 17, .adv_w = 168, .box_w = 7, .box_h = 13, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 29, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 40, .adv_w = 168, .box_w = 7, .box_h = 13, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 52, .adv_w = 84, .box_w = 2, .box_h = 3, .ofs_x = 2, .ofs_y = 6},
    {.bitmap_index = 53, .adv_w = 112, .box_w = 4, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 58, .adv_w = 112, .box_w = 4, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 63, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 74, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 85, .adv_w = 112, .box_w = 4, .box_h = 4, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 87, .adv_w = 140, .box_w = 5, .box_h = 2, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 89, .adv_w = 84, .box_w = 2, .box_h = 2, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 90, .adv_w = 140, .box_w = 5, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 96, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 104, .adv_w = 140, .box_w = 5, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 110, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 118, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 126, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 134, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 142, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 150, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 158, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 166, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 174, .adv_w = 84, .box_w = 2, .box_h = 5, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 176, .adv_w = 112, .box_w = 4, .box_h = 7, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 180, .adv_w = 140, .box_w = 5, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 186, .adv_w = 140, .box_w = 5, .box_h = 5, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 190, .adv_w = 140, .box_w = 5, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 196, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 204, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 215, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 223, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 231, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 239, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 247, .adv_w = 140, .box_w = 6, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 254, .adv_w = 140, .box_w = 6, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 261, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 269, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 277, .adv_w = 84, .box_w = 2, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 280, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 288, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 296, .adv_w = 140, .box_w = 6, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 303, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 314, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 325, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 333, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 341, .adv_w = 168, .box_w = 7, .box_h = 11, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 351, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 359, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 367, .adv_w = 140, .box_w = 5, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 373, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 381, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 392, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 403, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 414, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 425, .adv_w = 140, .box_w = 5, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 431, .adv_w = 112, .box_w = 4, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 436, .adv_w = 140, .box_w = 5, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 442, .adv_w = 112, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 447, .adv_w = 140, .box_w = 5, .box_h = 4, .ofs_x = 2, .ofs_y = 7},
    {.bitmap_index = 450, .adv_w = 168, .box_w = 7, .box_h = 2, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 452, .adv_w = 112, .box_w = 4, .box_h = 4, .ofs_x = 2, .ofs_y = 11},
    {.bitmap_index = 454, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 462, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 470, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 478, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 486, .adv_w = 140, .box_w = 6, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 493, .adv_w = 140, .box_w = 6, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 500, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 508, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 516, .adv_w = 84, .box_w = 2, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 519, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 527, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 535, .adv_w = 140, .box_w = 6, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 542, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 553, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 564, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 572, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 580, .adv_w = 168, .box_w = 7, .box_h = 11, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 590, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 598, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 606, .adv_w = 140, .box_w = 5, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 612, .adv_w = 168, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 620, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 631, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 642, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 653, .adv_w = 196, .box_w = 9, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 664, .adv_w = 140, .box_w = 5, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 670, .adv_w = 140, .box_w = 6, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 677, .adv_w = 84, .box_w = 2, .box_h = 12, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 680, .adv_w = 140, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 687, .adv_w = 168, .box_w = 7, .box_h = 4, .ofs_x = 2, .ofs_y = 5}
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
const lv_font_t badge_font_14 = {
#else
lv_font_t badge_font_14 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 17,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
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



#endif /*#if BADGE_FONT_14*/

