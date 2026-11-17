// =====================================================================
// theme.h - palette, colour schemes and fonts for the boost gauge
//
// The base look is black glass, a dim track, a single accent colour for
// the whole arc and the small captions, and white values.
//
// The accent is switchable at runtime with the board's BOOT button, the PQ
// cruise stalk, or the CANv-gauge web page:
// green -> orange -> red -> blue -> white -> purple -> green.
// =====================================================================
#pragma once

#include <lvgl.h>

#define COL_BG lv_color_hex(0x000000)
#define COL_TRACK lv_color_hex(0x25282c)
#define COL_FG lv_color_hex(0xf2f5f8)
#define COL_DIM lv_color_hex(0x7c8894)
#define COL_LINE lv_color_hex(0x2c3138)

// Link state: fresh OHX_STATUS (green), stale/lost OHX_STATUS (orange),
// CANN only / IAT only (amber), never received anything (red).
// Independent of the accent scheme so the indicator means the same in every colour.
#define COL_STATUS_OK lv_color_hex(0x22c55e)
#define COL_STATUS_STALE lv_color_hex(0xfb923c)  // orange - had data, now stale
#define COL_STATUS_WARN lv_color_hex(0xf59e0b)   // amber - IAT only
#define COL_STATUS_IDLE lv_color_hex(0xef4444)   // red - never received anything

// 72 px Montserrat Bold, digits + '.', generated from the face used by
// OpenHaldex-S3-Dash. The larger face fills the space opened by the edge arc.
LV_FONT_DECLARE(lv_font_dash_num_72);

#define FONT_VALUE (&lv_font_dash_num_72)
#define FONT_IAT (&lv_font_montserrat_24)
#define FONT_LABEL (&lv_font_montserrat_20)
#define FONT_SMALL (&lv_font_montserrat_16)
#define FONT_TINY (&lv_font_montserrat_12)
#define FONT_G (&lv_font_montserrat_40)

namespace theme {

// The order is the button cycle order.
enum class Scheme : uint8_t {
  Green = 0,
  Orange,
  Red,
  Blue,
  White,
  Purple,
  Count,
};

Scheme scheme();
void setScheme(Scheme s);
void nextScheme();

// Current accent, used for the arc, the scale labels and the captions.
lv_color_t accent();

// Stable name for the console log.
const char *schemeName();

}  // namespace theme
