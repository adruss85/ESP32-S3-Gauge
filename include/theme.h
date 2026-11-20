// =====================================================================
// theme.h - palette, colour schemes and fonts for the boost gauge
//
// The base look is black glass, a dim track, a single accent family for the
// arc and the captions, and the scheme's own text colour for values.
//
// The seven schemes are copied from OpenHaldex-S3-Dash (src/ui/theme.cpp
// there), which is the sibling dashboard on the same car. That table is what
// its driver already reads all day, so the gauge and the dash show the same
// colours in the same order, and a scheme is a palette rather than one
// accent swapped into a fixed black-and-white frame.
//
// The order is the cycle order for BOOT, a tap and the web page:
//   Midnight -> Orange -> Green -> Blue -> Purple -> White -> Red -> Midnight
//
// Midnight is what every build before this one drew and stays the default.
// =====================================================================
#pragma once

#include <lvgl.h>

// Fixed, scheme-independent neutrals that have to mean the same thing on
// every scheme.
#define COL_BG lv_color_hex(0x000000)
// The splash is fixed art and does not follow the scheme, so it uses these
// rather than theme::fg()/theme::dim().
#define COL_FG lv_color_hex(0xe8edf2)
#define COL_DIM lv_color_hex(0x7c8894)
#define COL_STATUS_OK lv_color_hex(0x22c55e)
#define COL_STATUS_STALE lv_color_hex(0xfb923c)  // had data, now stale
#define COL_STATUS_WARN lv_color_hex(0xf59e0b)   // IAT only
#define COL_STATUS_IDLE lv_color_hex(0xef4444)   // never received anything

// 96 px Montserrat Bold, digits + '.' + '-', generated from the same face
// OpenHaldex-S3-Dash uses for its 96 px rev digits. The 1.85 build's face
// was 72 px; 466/360 x 72 = 93.2, so the existing 96 px face is the right
// size for the larger glass and is copied rather than regenerated.
LV_FONT_DECLARE(lv_font_dash_num_96);

#define FONT_VALUE (&lv_font_dash_num_96)
#define FONT_IAT (&lv_font_montserrat_30)
#define FONT_LABEL (&lv_font_montserrat_26)
#define FONT_SMALL (&lv_font_montserrat_20)
#define FONT_TINY (&lv_font_montserrat_16)
#define FONT_G (&lv_font_montserrat_48)

namespace theme {

// Copy of OpenHaldex-S3-Dash's list, in its order. A stored scheme is a byte
// in NVS, so this order is a wire format: Red is last because it was added
// last there, and reordering would silently repaint every car that has a
// scheme stored.
enum class Scheme : uint8_t {
  Midnight = 0,
  Orange,
  Green,
  Blue,
  Purple,
  White,
  Red,
  Count,
};

// One scheme. The neutrals are exact copies of the dash's defaults; a scheme
// only names the accent family and (for the non-Midnight ones) the text and
// caption hues.
//
// Roles the gauge does not draw yet (warn, crit, cyan, shift, ...) are kept
// so this table can be diffed against the dash's and re-copied whole.
struct Palette {
  uint32_t bg = 0x000000;
  uint32_t panel = 0x0b0d10;
  uint32_t line = 0x1c2128;
  uint32_t fg = 0xe8edf2;
  uint32_t dim = 0x7c8894;
  uint32_t white = 0xf2f5f8;
  uint32_t track = 0x14181d;

  // --- the accent family ---
  uint32_t accent;
  uint32_t accentDim;
  uint32_t accentDimmer;
  uint32_t warn;
  uint32_t crit;
  uint32_t critText;
  uint32_t accentText = 0x000000;
  uint32_t cyan;
  uint32_t shift;
};

Scheme scheme();
bool setScheme(Scheme s);
void nextScheme();

const Palette &palette();
const char *schemeName(Scheme s);
const char *schemeName();

// The colours the gauge draws, as colours rather than indices.
lv_color_t accent();
lv_color_t accentDim();
lv_color_t fg();
lv_color_t dim();
lv_color_t line();
lv_color_t track();
lv_color_t white();

// Midnight / Orange / Green / Blue / Purple / White / Red, index 0..6, for
// the web page and the serial log.
constexpr uint8_t kSchemeCount = (uint8_t)Scheme::Count;

}  // namespace theme
