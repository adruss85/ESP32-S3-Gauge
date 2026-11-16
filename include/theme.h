// =====================================================================
// theme.h - palette and fonts for the boost gauge
//
// The look follows the reference image: black glass, a dim track, a green
// boost arc that turns amber/red near the top of the scale, white values,
// and small grey labels.
// =====================================================================
#pragma once

#include <lvgl.h>

#define COL_BG lv_color_hex(0x000000)
#define COL_TRACK lv_color_hex(0x25282c)
#define COL_FG lv_color_hex(0xf2f5f8)
#define COL_DIM lv_color_hex(0x7c8894)
#define COL_LINE lv_color_hex(0x2c3138)
#define COL_GREEN lv_color_hex(0x22c55e)
#define COL_AMBER lv_color_hex(0xf59e0b)
#define COL_RED lv_color_hex(0xef4444)

// 56 px Montserrat Bold, digits + '.', copied from OpenHaldex-S3-Dash.
LV_FONT_DECLARE(lv_font_dash_num_56);

#define FONT_VALUE (&lv_font_dash_num_56)
#define FONT_IAT (&lv_font_montserrat_24)
#define FONT_LABEL (&lv_font_montserrat_20)
#define FONT_SMALL (&lv_font_montserrat_16)
#define FONT_TINY (&lv_font_montserrat_12)
