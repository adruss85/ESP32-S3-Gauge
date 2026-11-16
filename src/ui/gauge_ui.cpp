// =====================================================================
// gauge_ui.cpp - the digital boost gauge
//
// Reference layout: a 270 degree arc with 0/5/10/15/20/25 PSI labels, a
// large white boost value with a decimal point on the baseline, a BOOST
// PSI caption in the active accent, and the intake-air temperature under
// a thin rule.
//
// The value is built from three flex items (integer, dot, fraction) so it
// stays centred at every width. The dot is a real '.' glyph from the same
// font, not a circle, so it sits on the text baseline.
//
// The arc keeps one colour across the whole sweep; the BOOT key changes
// that accent (green/orange/red/blue/white/purple).
// =====================================================================
#include "ui/gauge_ui.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

#include "theme.h"

namespace gauge_ui {
namespace {

constexpr int kScaleMaxPsi = 25;
constexpr int kArcRange = kScaleMaxPsi * 10;  // tenths of a PSI

// Outer radius 157 px, stroke 21 px: the inner edge stays at 136 px, the
// same as the original 300 px / 14 px arc, so it grows outwards only.
constexpr int kArcSize = 314;
constexpr int kArcWidth = 21;

lv_obj_t *g_arc = nullptr;
lv_obj_t *g_whole = nullptr;
lv_obj_t *g_dot = nullptr;
lv_obj_t *g_frac = nullptr;
lv_obj_t *g_unit = nullptr;
lv_obj_t *g_iat = nullptr;
lv_obj_t *g_iatCap = nullptr;
lv_obj_t *g_status = nullptr;
lv_obj_t *g_ticks[6] = {};
bool g_ready = false;

struct Tick {
  int16_t x;
  int16_t y;
  const char *text;
};

// 0 .. 25 PSI placed on a radius-112 circle inside the arc.
const Tick kTicks[] = {
    {101, 259, "0"},  {69, 163, "5"},   {129, 80, "10"},
    {231, 80, "15"},  {291, 163, "20"}, {259, 259, "25"},
};

void plain(lv_obj_t *obj) {
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

void applyScheme() {
  if (!g_ready) return;

  const lv_color_t accent = theme::accent();
  lv_obj_set_style_arc_color(g_arc, accent, LV_PART_INDICATOR);
  lv_obj_set_style_text_color(g_unit, accent, 0);
  for (lv_obj_t *tick : g_ticks) {
    if (tick != nullptr) lv_obj_set_style_text_color(tick, accent, 0);
  }
  lv_obj_set_style_text_color(g_iatCap, accent, 0);
}

void addTicks(lv_obj_t *scr) {
  size_t i = 0;
  for (const Tick &t : kTicks) {
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, 40, 22);
    lv_obj_set_pos(box, t.x - 20, t.y - 11);
    plain(box);

    lv_obj_t *label = lv_label_create(box);
    lv_label_set_text(label, t.text);
    lv_obj_set_style_text_font(label, FONT_SMALL, 0);
    lv_obj_set_style_text_color(label, COL_DIM, 0);
    lv_obj_set_style_text_opa(label, LV_OPA_60, 0);
    lv_obj_center(label);

    if (i < sizeof(g_ticks) / sizeof(g_ticks[0])) g_ticks[i] = label;
    i++;
  }
}

void addValueRow(lv_obj_t *scr) {
  lv_obj_t *row = lv_obj_create(scr);
  lv_obj_set_size(row, 300, 88);
  lv_obj_set_pos(row, 30, 96);
  plain(row);

  lv_obj_set_layout(row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 0, 0);

  g_whole = lv_label_create(row);
  lv_label_set_text(g_whole, "--");
  lv_obj_set_style_text_font(g_whole, FONT_VALUE, 0);
  lv_obj_set_style_text_color(g_whole, COL_FG, 0);

  // A real period glyph shares the digits' baseline, so it lands at the
  // bottom of the value instead of floating in the middle.
  g_dot = lv_label_create(row);
  lv_label_set_text(g_dot, ".");
  lv_obj_set_style_text_font(g_dot, FONT_VALUE, 0);
  lv_obj_set_style_text_color(g_dot, COL_FG, 0);

  g_frac = lv_label_create(row);
  lv_label_set_text(g_frac, "0");
  lv_obj_set_style_text_font(g_frac, FONT_VALUE, 0);
  lv_obj_set_style_text_color(g_frac, COL_FG, 0);
}

}  // namespace

void create() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // A thin rim just inside the glass edge.
  lv_obj_t *bezel = lv_obj_create(scr);
  lv_obj_set_size(bezel, 352, 352);
  lv_obj_center(bezel);
  plain(bezel);
  lv_obj_set_style_radius(bezel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(bezel, 1, 0);
  lv_obj_set_style_border_color(bezel, COL_LINE, 0);

  // The scale: 135 -> 45 degrees, clockwise, leaving the bottom open.
  g_arc = lv_arc_create(scr);
  lv_obj_set_size(g_arc, kArcSize, kArcSize);
  lv_obj_center(g_arc);
  lv_arc_set_bg_angles(g_arc, 135, 45);
  lv_arc_set_range(g_arc, 0, kArcRange);
  lv_arc_set_value(g_arc, 0);
  lv_obj_remove_flag(g_arc, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_set_style_bg_opa(g_arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_arc, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(g_arc, 0, LV_PART_KNOB);

  lv_obj_set_style_arc_width(g_arc, kArcWidth, LV_PART_MAIN);
  lv_obj_set_style_arc_color(g_arc, COL_TRACK, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(g_arc, true, LV_PART_MAIN);

  lv_obj_set_style_arc_width(g_arc, kArcWidth, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(g_arc, theme::accent(), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(g_arc, true, LV_PART_INDICATOR);

  addTicks(scr);
  addValueRow(scr);

  g_unit = lv_label_create(scr);
  lv_label_set_text(g_unit, "BOOST PSI");
  lv_obj_set_style_text_font(g_unit, FONT_LABEL, 0);
  lv_obj_set_style_text_color(g_unit, theme::accent(), 0);
  lv_obj_set_style_text_letter_space(g_unit, 4, 0);
  lv_obj_align(g_unit, LV_ALIGN_TOP_MID, 0, 190);

  lv_obj_t *rule = lv_obj_create(scr);
  lv_obj_set_size(rule, 130, 1);
  lv_obj_set_pos(rule, 115, 216);
  lv_obj_set_style_bg_color(rule, COL_LINE, 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rule, 0, 0);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);

  g_iatCap = lv_label_create(scr);
  lv_label_set_text(g_iatCap, "IAT");
  lv_obj_set_style_text_font(g_iatCap, FONT_SMALL, 0);
  lv_obj_set_style_text_color(g_iatCap, theme::accent(), 0);
  lv_obj_set_style_text_opa(g_iatCap, LV_OPA_60, 0);
  lv_obj_align(g_iatCap, LV_ALIGN_TOP_MID, 0, 220);

  g_iat = lv_label_create(scr);
  lv_label_set_text(g_iat, "--");
  lv_obj_set_style_text_font(g_iat, FONT_IAT, 0);
  lv_obj_set_style_text_color(g_iat, COL_FG, 0);
  lv_obj_align(g_iat, LV_ALIGN_TOP_MID, 0, 242);

  // Link state as a small line at the bottom edge: a few pixels tall, wide
  // enough to read at a glance without becoming a lamp.
  g_status = lv_obj_create(scr);
  lv_obj_set_size(g_status, 20, 4);
  lv_obj_set_style_radius(g_status, 2, 0);
  lv_obj_set_style_bg_color(g_status, COL_STATUS_IDLE, 0);
  lv_obj_set_style_bg_opa(g_status, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_status, 0, 0);
  lv_obj_remove_flag(g_status, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(g_status, LV_ALIGN_BOTTOM_MID, 0, -18);

  g_ready = true;
  applyScheme();
}

void update(const ohxnow::Telemetry &t, const ohxnow::Stats &s) {
  if (!g_ready) return;

  static float shown = NAN;
  const float target = t.statusValid ? t.boostPsi : NAN;

  if (isnan(target)) {
    shown = NAN;
  } else if (isnan(shown)) {
    shown = target;
  } else {
    shown += (target - shown) * 0.30f;
  }

  // Only touch the widgets when the rounded value changes. The smoothing
  // loop runs at 20 Hz, but repainting the 314 px arc on every iteration
  // is what made the loop sluggish and the value look unsettled.
  int tenths = -1;
  if (!isnan(shown)) {
    tenths = (int)(shown * 10.0f + 0.5f);
    if (tenths < 0) tenths = 0;
  }

  static int lastTenths = -2;
  if (tenths != lastTenths) {
    lastTenths = tenths;
    if (tenths < 0) {
      lv_label_set_text(g_whole, "--");
      lv_obj_add_flag(g_dot, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(g_frac, LV_OBJ_FLAG_HIDDEN);
      lv_arc_set_value(g_arc, 0);
    } else {
      lv_obj_remove_flag(g_dot, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(g_frac, LV_OBJ_FLAG_HIDDEN);

      char buf[8];
      snprintf(buf, sizeof(buf), "%d", tenths / 10);
      lv_label_set_text(g_whole, buf);
      snprintf(buf, sizeof(buf), "%d", tenths % 10);
      lv_label_set_text(g_frac, buf);
      lv_arc_set_value(g_arc, tenths > kArcRange ? kArcRange : tenths);
    }
  }

  const bool iatOk = t.iatValid && !isnan(t.iatC);
  const int iat = iatOk ? (int)lroundf(t.iatC) : 0;
  static bool lastIatOk = false;
  static int lastIat = 0;
  if (iatOk != lastIatOk || (iatOk && iat != lastIat)) {
    lastIatOk = iatOk;
    lastIat = iat;
    if (iatOk) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%d°C", iat);
      lv_label_set_text(g_iat, buf);
    } else {
      lv_label_set_text(g_iat, "--");
    }
  }

  // Status bar: red=never received, green=streaming, orange=stale, amber=IAT only
  // Priority: never > streaming > stale > IAT-only > idle
  lv_color_t statusColor;
  if (s.raw == 0) {
    statusColor = COL_STATUS_IDLE;  // red - never received anything
  } else if (t.statusValid) {
    statusColor = COL_STATUS_OK;    // green - fresh OHX_STATUS
  } else if (s.statusPackets > 0) {
    statusColor = COL_STATUS_STALE; // orange - had OHX_STATUS, now stale
  } else if (t.iatValid) {
    statusColor = COL_STATUS_WARN;  // amber - IAT only (fresh CANN)
  } else if (s.canPackets > 0) {
    statusColor = COL_STATUS_STALE; // orange - had CANN, now stale
  } else {
    statusColor = COL_STATUS_IDLE;  // red - packets received but no valid data (shouldn't happen)
  }

  static lv_color_t lastStatusColor = lv_color_hex(0xFFFFFFFF);  // invalid sentinel
  if (!lv_color_eq(statusColor, lastStatusColor)) {
    lastStatusColor = statusColor;
    lv_obj_set_style_bg_color(g_status, statusColor, 0);
  }
}

void nextScheme() {
  theme::nextScheme();
  applyScheme();
  Serial.printf("[ui] accent %s\n", theme::schemeName());
}

void setScheme(theme::Scheme s) {
  theme::setScheme(s);
  applyScheme();
}

}  // namespace gauge_ui
