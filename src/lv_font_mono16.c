/*******************************************************************************
 * Size: 16 px
 * Bpp: 1
 * Opts: --bpp 1 --size 16 --font /Users/christian/.claude/jobs/35a3674d/tmp/menlo-regular.ttf --range 0x20-0x7E --format lvgl --lv-include lvgl.h -o src/lv_font_mono16.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef LV_FONT_MONO16
#define LV_FONT_MONO16 1
#endif

#if LV_FONT_MONO16

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xff, 0xfc, 0xf,

    /* U+0022 "\"" */
    0xcf, 0x3c, 0xf3, 0xcc,

    /* U+0023 "#" */
    0x9, 0x82, 0x40, 0x91, 0xff, 0x13, 0x4, 0x81,
    0x20, 0xc8, 0xff, 0x89, 0x82, 0x41, 0x90,

    /* U+0024 "$" */
    0x10, 0x21, 0xf6, 0x8d, 0x1a, 0x1e, 0xe, 0x16,
    0x2e, 0x5f, 0xe1, 0x2, 0x4, 0x0,

    /* U+0025 "%" */
    0x70, 0x44, 0x22, 0x11, 0x17, 0x38, 0xe3, 0x9c,
    0x11, 0x8, 0x84, 0x41, 0xc0,

    /* U+0026 "&" */
    0x1c, 0x10, 0x8, 0x4, 0x3, 0x3, 0x81, 0xa7,
    0x9b, 0xc5, 0xe3, 0x99, 0xc7, 0xb0,

    /* U+0027 "'" */
    0xff, 0xc0,

    /* U+0028 "(" */
    0x13, 0x66, 0x4c, 0xcc, 0xcc, 0xc6, 0x62, 0x30,

    /* U+0029 ")" */
    0x8c, 0x46, 0x23, 0x33, 0x33, 0x36, 0x64, 0xc0,

    /* U+002A "*" */
    0x8, 0x4, 0x32, 0x67, 0xc0, 0x81, 0xf3, 0x26,
    0x10, 0x8, 0x0,

    /* U+002B "+" */
    0x18, 0x18, 0x18, 0xff, 0x18, 0x18, 0x18, 0x18,

    /* U+002C "," */
    0x6d, 0xac,

    /* U+002D "-" */
    0xff,

    /* U+002E "." */
    0xff, 0x80,

    /* U+002F "/" */
    0x2, 0xc, 0x10, 0x60, 0x83, 0x4, 0x18, 0x20,
    0xc1, 0x6, 0xc, 0x0,

    /* U+0030 "0" */
    0x3c, 0x66, 0x42, 0xc7, 0xcb, 0xcb, 0xd3, 0xe3,
    0xe3, 0x42, 0x66, 0x3c,

    /* U+0031 "1" */
    0x39, 0xf3, 0x60, 0xc1, 0x83, 0x6, 0xc, 0x18,
    0x30, 0x63, 0xf0,

    /* U+0032 "2" */
    0xf9, 0x18, 0x18, 0x30, 0x61, 0x83, 0xc, 0x30,
    0xc3, 0x7, 0xf0,

    /* U+0033 "3" */
    0x7c, 0x7, 0x3, 0x3, 0x7, 0x1c, 0x6, 0x3,
    0x3, 0x3, 0x86, 0xfc,

    /* U+0034 "4" */
    0xe, 0x7, 0x5, 0x82, 0xc2, 0x63, 0x31, 0x19,
    0x8c, 0xff, 0x83, 0x1, 0x80, 0xc0,

    /* U+0035 "5" */
    0x7e, 0x60, 0x60, 0x60, 0x7c, 0x6, 0x3, 0x3,
    0x3, 0x3, 0x86, 0xfc,

    /* U+0036 "6" */
    0x3e, 0x60, 0x60, 0xc0, 0xdc, 0xe6, 0xc3, 0xc3,
    0xc3, 0xc3, 0x66, 0x3c,

    /* U+0037 "7" */
    0xfe, 0xc, 0x10, 0x60, 0xc1, 0x6, 0xc, 0x10,
    0x60, 0xc1, 0x0,

    /* U+0038 "8" */
    0x3c, 0xe7, 0xc3, 0xc3, 0xe6, 0x3c, 0x66, 0xc3,
    0xc3, 0xc3, 0xe6, 0x3c,

    /* U+0039 "9" */
    0x3c, 0x66, 0xc3, 0xc3, 0xc3, 0xc7, 0x67, 0x3b,
    0x3, 0x6, 0x6, 0x7c,

    /* U+003A ":" */
    0xfc, 0xf, 0xc0,

    /* U+003B ";" */
    0x6d, 0x80, 0x1b, 0x6b, 0x0,

    /* U+003C "<" */
    0x1, 0x83, 0x8f, 0x1c, 0xe, 0x1, 0xe0, 0x1e,
    0x3,

    /* U+003D "=" */
    0xff, 0x0, 0x0, 0x0, 0xff,

    /* U+003E ">" */
    0x80, 0x78, 0x7, 0x0, 0x70, 0x78, 0xe3, 0xc1,
    0x0,

    /* U+003F "?" */
    0xfa, 0x30, 0xc3, 0x1c, 0xe3, 0xc, 0x30, 0x3,
    0xc,

    /* U+0040 "@" */
    0x1e, 0x10, 0x90, 0x39, 0xf9, 0x9c, 0x86, 0x43,
    0x33, 0x8f, 0xe0, 0x10, 0x6, 0x1, 0xf8,

    /* U+0041 "A" */
    0xc, 0xe, 0x7, 0x82, 0xc1, 0x21, 0x90, 0xcc,
    0x46, 0x7f, 0x30, 0xd0, 0x78, 0x10,

    /* U+0042 "B" */
    0xfc, 0xc6, 0xc6, 0xc6, 0xc6, 0xf8, 0xc6, 0xc3,
    0xc3, 0xc3, 0xc7, 0xfc,

    /* U+0043 "C" */
    0x1f, 0x61, 0x60, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
    0xc0, 0x60, 0x61, 0x1f,

    /* U+0044 "D" */
    0xf8, 0xc6, 0xc6, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc6, 0xc6, 0xf8,

    /* U+0045 "E" */
    0xff, 0x83, 0x6, 0xc, 0x1f, 0xf0, 0x60, 0xc1,
    0x83, 0x7, 0xf0,

    /* U+0046 "F" */
    0xff, 0x83, 0x6, 0xc, 0x1f, 0xf0, 0x60, 0xc1,
    0x83, 0x6, 0x0,

    /* U+0047 "G" */
    0x1e, 0x62, 0x60, 0xc0, 0xc0, 0xc0, 0xc7, 0xc3,
    0xc3, 0x43, 0x63, 0x1e,

    /* U+0048 "H" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xff, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3, 0xc3,

    /* U+0049 "I" */
    0xfc, 0xc3, 0xc, 0x30, 0xc3, 0xc, 0x30, 0xc3,
    0x3f,

    /* U+004A "J" */
    0x3e, 0xc, 0x18, 0x30, 0x60, 0xc1, 0x83, 0x6,
    0xe, 0x3f, 0xe0,

    /* U+004B "K" */
    0xc3, 0x61, 0x31, 0x19, 0x8d, 0x87, 0xc3, 0xa1,
    0x98, 0xc6, 0x63, 0x30, 0xd8, 0x30,

    /* U+004C "L" */
    0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
    0xc0, 0xc0, 0xc0, 0xff,

    /* U+004D "M" */
    0xe3, 0xf1, 0xf8, 0xfa, 0xbd, 0x5e, 0xaf, 0x27,
    0x93, 0xc1, 0xe0, 0xf0, 0x78, 0x30,

    /* U+004E "N" */
    0xe1, 0xf0, 0xfc, 0x7a, 0x3d, 0x9e, 0x4f, 0x27,
    0x9b, 0xc5, 0xe3, 0xf0, 0xf8, 0x70,

    /* U+004F "O" */
    0x3c, 0x66, 0xc2, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc3, 0x66, 0x3c,

    /* U+0050 "P" */
    0xfc, 0xc6, 0xc3, 0xc3, 0xc3, 0xc6, 0xfc, 0xc0,
    0xc0, 0xc0, 0xc0, 0xc0,

    /* U+0051 "Q" */
    0x3c, 0x66, 0xc2, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc2, 0x66, 0x3c, 0x6, 0x2,

    /* U+0052 "R" */
    0xf8, 0xce, 0xc6, 0xc6, 0xc6, 0xcc, 0xf8, 0xcc,
    0xc6, 0xc6, 0xc3, 0xc3,

    /* U+0053 "S" */
    0x3e, 0x62, 0xc0, 0xc0, 0xe0, 0x7c, 0x3e, 0x7,
    0x3, 0x3, 0x86, 0xfc,

    /* U+0054 "T" */
    0xff, 0x8c, 0x6, 0x3, 0x1, 0x80, 0xc0, 0x60,
    0x30, 0x18, 0xc, 0x6, 0x3, 0x0,

    /* U+0055 "U" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc3, 0x66, 0x3c,

    /* U+0056 "V" */
    0x41, 0xa0, 0xd8, 0x4c, 0x22, 0x31, 0x98, 0xc8,
    0x24, 0x16, 0xf, 0x7, 0x1, 0x80,

    /* U+0057 "W" */
    0xc0, 0xf0, 0x2c, 0x9, 0x32, 0x4c, 0x97, 0x65,
    0x59, 0x5e, 0x73, 0x1c, 0xc3, 0x30, 0x8c,

    /* U+0058 "X" */
    0x61, 0x90, 0x8c, 0xc2, 0x41, 0xc0, 0x60, 0x30,
    0x3c, 0x32, 0x11, 0x98, 0x78, 0x10,

    /* U+0059 "Y" */
    0x40, 0x98, 0x62, 0x10, 0xcc, 0x1e, 0x7, 0x80,
    0xc0, 0x30, 0xc, 0x3, 0x0, 0xc0, 0x30,

    /* U+005A "Z" */
    0xff, 0x3, 0x6, 0x4, 0xc, 0x18, 0x18, 0x30,
    0x20, 0x60, 0xc0, 0xff,

    /* U+005B "[" */
    0xfc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcf,

    /* U+005C "\\" */
    0xc1, 0x81, 0x3, 0x2, 0x6, 0x4, 0xc, 0x8,
    0x18, 0x10, 0x30, 0x20,

    /* U+005D "]" */
    0xf3, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3f,

    /* U+005E "^" */
    0xc, 0xe, 0xd, 0x84, 0x64, 0x18,

    /* U+005F "_" */
    0xff, 0xc0,

    /* U+0060 "`" */
    0x46, 0x30,

    /* U+0061 "a" */
    0x7c, 0x9c, 0x18, 0x37, 0xf8, 0xf1, 0xe7, 0x76,

    /* U+0062 "b" */
    0xc1, 0x83, 0x6, 0xee, 0xd8, 0xf1, 0xe3, 0xc7,
    0x8f, 0xb6, 0xe0,

    /* U+0063 "c" */
    0x3e, 0xc7, 0x6, 0xc, 0x18, 0x30, 0x31, 0x3e,

    /* U+0064 "d" */
    0x6, 0xc, 0x1b, 0xb6, 0xf8, 0xf1, 0xe3, 0xc7,
    0x8d, 0xbb, 0xb0,

    /* U+0065 "e" */
    0x3c, 0x62, 0xc3, 0xff, 0xc0, 0xc0, 0xc0, 0x61,
    0x3e,

    /* U+0066 "f" */
    0x1e, 0x60, 0xc7, 0xf3, 0x6, 0xc, 0x18, 0x30,
    0x60, 0xc1, 0x80,

    /* U+0067 "g" */
    0x76, 0xdf, 0x1e, 0x3c, 0x78, 0xf1, 0xb7, 0x76,
    0xd, 0x33, 0xe0,

    /* U+0068 "h" */
    0xc1, 0x83, 0x6, 0xee, 0x78, 0xf1, 0xe3, 0xc7,
    0x8f, 0x1e, 0x30,

    /* U+0069 "i" */
    0x18, 0x30, 0x3, 0xc1, 0x83, 0x6, 0xc, 0x18,
    0x30, 0x67, 0xf0,

    /* U+006A "j" */
    0x18, 0xc0, 0xf1, 0x8c, 0x63, 0x18, 0xc6, 0x31,
    0x8f, 0xc0,

    /* U+006B "k" */
    0xc0, 0xc0, 0xc0, 0xc6, 0xcc, 0xd8, 0xf0, 0xf8,
    0xcc, 0xc4, 0xc6, 0xc3,

    /* U+006C "l" */
    0xf0, 0x60, 0xc1, 0x83, 0x6, 0xc, 0x18, 0x30,
    0x60, 0xc0, 0xf0,

    /* U+006D "m" */
    0xf6, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb,
    0xdb,

    /* U+006E "n" */
    0xdd, 0xcf, 0x1e, 0x3c, 0x78, 0xf1, 0xe3, 0xc6,

    /* U+006F "o" */
    0x3c, 0x66, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x66,
    0x3c,

    /* U+0070 "p" */
    0xdd, 0xdb, 0x1e, 0x3c, 0x78, 0xf1, 0xf6, 0xdd,
    0x83, 0x6, 0x0,

    /* U+0071 "q" */
    0x76, 0xdf, 0x1e, 0x3c, 0x78, 0xf1, 0xb7, 0x76,
    0xc, 0x18, 0x30,

    /* U+0072 "r" */
    0xdf, 0x8c, 0x30, 0xc3, 0xc, 0x30, 0xc0,

    /* U+0073 "s" */
    0x7d, 0x83, 0x7, 0x7, 0xc1, 0xc1, 0xc3, 0xfc,

    /* U+0074 "t" */
    0x30, 0x60, 0xc7, 0xf3, 0x6, 0xc, 0x18, 0x30,
    0x60, 0xc0, 0xf0,

    /* U+0075 "u" */
    0xc7, 0x8f, 0x1e, 0x3c, 0x78, 0xf1, 0xe7, 0x76,

    /* U+0076 "v" */
    0x83, 0xc2, 0x46, 0x46, 0x64, 0x2c, 0x2c, 0x38,
    0x18,

    /* U+0077 "w" */
    0xc0, 0xf0, 0x24, 0x9, 0x36, 0x4d, 0x9d, 0x47,
    0x50, 0xcc, 0x33, 0x0,

    /* U+0078 "x" */
    0xc2, 0x66, 0x2c, 0x38, 0x18, 0x38, 0x64, 0x46,
    0xc3,

    /* U+0079 "y" */
    0x41, 0x21, 0x98, 0xc4, 0x43, 0x60, 0xa0, 0x50,
    0x38, 0x8, 0xc, 0x4, 0xe, 0x0,

    /* U+007A "z" */
    0xfe, 0xc, 0x30, 0xc1, 0x6, 0x18, 0x60, 0xfe,

    /* U+007B "{" */
    0xe, 0x30, 0x60, 0xc1, 0x83, 0xe, 0x60, 0x38,
    0x30, 0x60, 0xc1, 0x83, 0x3, 0x80,

    /* U+007C "|" */
    0xff, 0xff, 0xff, 0xff,

    /* U+007D "}" */
    0xf0, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7,
    0x1c, 0x18, 0x18, 0x18, 0x18, 0x18, 0xf0,

    /* U+007E "~" */
    0x70, 0x12, 0x38, 0xc8, 0x1c
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 154, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 154, .box_w = 2, .box_h = 12, .ofs_x = 4, .ofs_y = 0},
    {.bitmap_index = 4, .adv_w = 154, .box_w = 6, .box_h = 5, .ofs_x = 1, .ofs_y = 7},
    {.bitmap_index = 8, .adv_w = 154, .box_w = 10, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 23, .adv_w = 154, .box_w = 7, .box_h = 15, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 37, .adv_w = 154, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 50, .adv_w = 154, .box_w = 9, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 64, .adv_w = 154, .box_w = 2, .box_h = 5, .ofs_x = 4, .ofs_y = 7},
    {.bitmap_index = 66, .adv_w = 154, .box_w = 4, .box_h = 15, .ofs_x = 3, .ofs_y = -1},
    {.bitmap_index = 74, .adv_w = 154, .box_w = 4, .box_h = 15, .ofs_x = 3, .ofs_y = -1},
    {.bitmap_index = 82, .adv_w = 154, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 93, .adv_w = 154, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 101, .adv_w = 154, .box_w = 3, .box_h = 5, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 103, .adv_w = 154, .box_w = 8, .box_h = 1, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 104, .adv_w = 154, .box_w = 3, .box_h = 3, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 106, .adv_w = 154, .box_w = 7, .box_h = 13, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 118, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 130, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 141, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 152, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 164, .adv_w = 154, .box_w = 9, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 178, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 190, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 202, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 213, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 225, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 237, .adv_w = 154, .box_w = 2, .box_h = 9, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 240, .adv_w = 154, .box_w = 3, .box_h = 11, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 245, .adv_w = 154, .box_w = 9, .box_h = 8, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 254, .adv_w = 154, .box_w = 8, .box_h = 5, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 259, .adv_w = 154, .box_w = 9, .box_h = 8, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 268, .adv_w = 154, .box_w = 6, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 277, .adv_w = 154, .box_w = 9, .box_h = 13, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 292, .adv_w = 154, .box_w = 9, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 306, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 318, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 330, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 342, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 353, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 364, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 376, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 388, .adv_w = 154, .box_w = 6, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 397, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 408, .adv_w = 154, .box_w = 9, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 422, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 434, .adv_w = 154, .box_w = 9, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 448, .adv_w = 154, .box_w = 9, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 462, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 474, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 486, .adv_w = 154, .box_w = 8, .box_h = 14, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 500, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 512, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 524, .adv_w = 154, .box_w = 9, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 538, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 550, .adv_w = 154, .box_w = 9, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 564, .adv_w = 154, .box_w = 10, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 579, .adv_w = 154, .box_w = 9, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 593, .adv_w = 154, .box_w = 10, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 608, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 620, .adv_w = 154, .box_w = 4, .box_h = 14, .ofs_x = 4, .ofs_y = -1},
    {.bitmap_index = 627, .adv_w = 154, .box_w = 7, .box_h = 13, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 639, .adv_w = 154, .box_w = 4, .box_h = 14, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 646, .adv_w = 154, .box_w = 9, .box_h = 5, .ofs_x = 0, .ofs_y = 7},
    {.bitmap_index = 652, .adv_w = 154, .box_w = 10, .box_h = 1, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 654, .adv_w = 154, .box_w = 4, .box_h = 3, .ofs_x = 2, .ofs_y = 10},
    {.bitmap_index = 656, .adv_w = 154, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 664, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 675, .adv_w = 154, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 683, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 694, .adv_w = 154, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 703, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 714, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 725, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 736, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 747, .adv_w = 154, .box_w = 5, .box_h = 15, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 757, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 769, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 780, .adv_w = 154, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 789, .adv_w = 154, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 797, .adv_w = 154, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 806, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 817, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 828, .adv_w = 154, .box_w = 6, .box_h = 9, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 835, .adv_w = 154, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 843, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 854, .adv_w = 154, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 862, .adv_w = 154, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 871, .adv_w = 154, .box_w = 10, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 883, .adv_w = 154, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 892, .adv_w = 154, .box_w = 9, .box_h = 12, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 906, .adv_w = 154, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 914, .adv_w = 154, .box_w = 7, .box_h = 15, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 928, .adv_w = 154, .box_w = 2, .box_h = 16, .ofs_x = 4, .ofs_y = -4},
    {.bitmap_index = 932, .adv_w = 154, .box_w = 8, .box_h = 15, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 947, .adv_w = 154, .box_w = 10, .box_h = 4, .ofs_x = 0, .ofs_y = 3}
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



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t lv_font_mono16 = {
#else
lv_font_t lv_font_mono16 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 18,          /*The maximum line height required by the font*/
    .base_line = 4,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if LV_FONT_MONO16*/

