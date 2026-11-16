// =====================================================================
// gauge_ui.cpp - the digital boost gauge
//
// Reference layout: a 270 degree arc with 0/5/10/15/20/25 PSI labels, a
// large white boost value with a centred decimal dot, a green BOOST PSI
// caption, and the intake-air temperature under a thin rule.
//
// The value is built from three flex items (integer, dot, fraction) so the
// decimal point stays in the middle of the row at every width.
// =====================================================================
#include "ui/gauge_ui.h"

#include <math.h>
#include <stdio.h>

#include "theme.h"

namespace gauge_ui {
namespace {

constexpr int kScaleMaxPsi = 25;
constexpr int kArcRange = kScaleMaxPsi * 10;  // tenths of a PSI
constexpr int kArcSize = 300;
constexpr int kArcWidth = 14;

lv_obj_t *g_arc = nullptr;
lv_obj_t *g_whole = nullptr;
lv_obj_t *g_dot = nullptr;
lv_obj_t *g_frac = nullptr;
lv_obj_t *g_iat = nullptr;
lv_obj_t *g_link = nullptr;
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

void addTicks(lv_obj_t *scr) {
  for (const Tick &t : kTicks) {
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, 40, 22);
    lv_obj_set_pos(box, t.x - 20, t.y - 11);
    plain(box);

    lv_obj_t *label = lv_label_create(box);
    lv_label_set_text(label, t.text);
    lv_obj_set_style_text_font(label, FONT_SMALL, 0);
    lv_obj_set_style_text_color(label, COL_DIM, 0);
    lv_obj_center(label);
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

  g_dot = lv_obj_create(row);
  lv_obj_set_size(g_dot, 12, 12);
  lv_obj_set_style_radius(g_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_dot, COL_FG, 0);
  lv_obj_set_style_bg_opa(g_dot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_dot, 0, 0);
  lv_obj_set_style_pad_all(g_dot, 0, 0);
  lv_obj_set_style_margin_left(g_dot, 5, 0);
  lv_obj_set_style_margin_right(g_dot, 5, 0);
  lv_obj_remove_flag(g_dot, LV_OBJ_FLAG_SCROLLABLE);

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
  lv_obj_set_style_arc_color(g_arc, COL_GREEN, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(g_arc, true, LV_PART_INDICATOR);

  addTicks(scr);
  addValueRow(scr);

  lv_obj_t *unit = lv_label_create(scr);
  lv_label_set_text(unit, "BOOST PSI");
  lv_obj_set_style_text_font(unit, FONT_LABEL, 0);
  lv_obj_set_style_text_color(unit, COL_GREEN, 0);
  lv_obj_set_style_text_letter_space(unit, 4, 0);
  lv_obj_align(unit, LV_ALIGN_TOP_MID, 0, 190);

  lv_obj_t *rule = lv_obj_create(scr);
  lv_obj_set_size(rule, 130, 1);
  lv_obj_set_pos(rule, 115, 216);
  lv_obj_set_style_bg_color(rule, COL_LINE, 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rule, 0, 0);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *iatCap = lv_label_create(scr);
  lv_label_set_text(iatCap, "IAT");
  lv_obj_set_style_text_font(iatCap, FONT_SMALL, 0);
  lv_obj_set_style_text_color(iatCap, COL_DIM, 0);
  lv_obj_align(iatCap, LV_ALIGN_TOP_MID, 0, 220);

  g_iat = lv_label_create(scr);
  lv_label_set_text(g_iat, "--");
  lv_obj_set_style_text_font(g_iat, FONT_IAT, 0);
  lv_obj_set_style_text_color(g_iat, COL_FG, 0);
  lv_obj_align(g_iat, LV_ALIGN_TOP_MID, 0, 242);

  // Subtle link lamp: green with a fresh controller packet, amber with only
  // CAN data, grey with nothing.
  g_link = lv_obj_create(scr);
  lv_obj_set_size(g_link, 7, 7);
  lv_obj_set_style_radius(g_link, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_link, COL_DIM, 0);
  lv_obj_set_style_bg_opa(g_link, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_link, 0, 0);
  lv_obj_remove_flag(g_link, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(g_link, LV_ALIGN_BOTTOM_MID, 0, -20);

  g_ready = true;
}

void update(const ohxnow::Telemetry &t) {
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

  if (isnan(shown)) {
    lv_label_set_text(g_whole, "--");
    lv_obj_add_flag(g_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_frac, LV_OBJ_FLAG_HIDDEN);
    lv_arc_set_value(g_arc, 0);
  } else {
    lv_obj_remove_flag(g_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_frac, LV_OBJ_FLAG_HIDDEN);

    int tenths = (int)(shown * 10.0f + 0.5f);
    if (tenths < 0) tenths = 0;

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", tenths / 10);
    lv_label_set_text(g_whole, buf);
    snprintf(buf, sizeof(buf), "%d", tenths % 10);
    lv_label_set_text(g_frac, buf);

    lv_arc_set_value(g_arc, tenths > kArcRange ? kArcRange : tenths);
    const lv_color_t col =
        shown >= 22.0f ? COL_RED : (shown >= 16.0f ? COL_AMBER : COL_GREEN);
    lv_obj_set_style_arc_color(g_arc, col, LV_PART_INDICATOR);
  }

  if (t.iatValid && !isnan(t.iatC)) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f°C", (double)t.iatC);
    lv_label_set_text(g_iat, buf);
  } else {
    lv_label_set_text(g_iat, "--");
  }

  lv_color_t link = COL_DIM;
  if (t.statusValid) {
    link = COL_GREEN;
  } else if (t.iatValid) {
    link = COL_AMBER;
  }
  lv_obj_set_style_bg_color(g_link, link, 0);
}

}  // namespace gauge_ui
