// =====================================================================
// oil_ui.cpp - the digital oil temperature face
//
// Same layout as the boost gauge: 270 degree arc with six scale labels,
// a large value with decimal point on the baseline, an OIL °C caption,
// and the intake-air temperature under a thin rule.
// The arc scale is configurable (0.1 °C steps) and persisted in NVS.
// =====================================================================
#include "ui/oil_ui.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "app/settings.h"
#include "theme.h"

namespace oil_ui {
namespace {

// Runtime scale, in tenths of a degree C. These defaults are replaced at boot by
// settings::begin() and whenever the web page applies a new range.
int16_t g_min10 = 400;  // 40.0 °C * 10
int16_t g_max10 = 1200; // 120.0 °C * 10
int g_arcRange = 800;   // (1200 - 400) * 10

// 0 = no value has been drawn yet; forces the first update() to paint.
int32_t g_lastDrawn = INT32_MIN;

// Set when the scale changed under an unchanged value, so update() repaints
// the arc against the new range even if the reading has not moved.
bool g_scaleDirty = true;

// Outer radius 233 px fills the round glass; the 27 px stroke sits inside
// the edge. The scale labels sit 18 px inside the inner edge (radius 188).
constexpr int kArcSize = 466;
constexpr int kArcWidth = 27;
constexpr int kTickRadius = kArcSize / 2 - kArcWidth - 18;

lv_obj_t *g_root = nullptr;
lv_obj_t *g_arc = nullptr;
lv_obj_t *g_whole = nullptr;
lv_obj_t *g_dot = nullptr;
lv_obj_t *g_frac = nullptr;
lv_obj_t *g_unit = nullptr;
lv_obj_t *g_rule = nullptr;
lv_obj_t *g_iat = nullptr;
lv_obj_t *g_iatCap = nullptr;
lv_obj_t *g_status = nullptr;
lv_obj_t *g_ticks[6] = {};
uint8_t g_masterOpa = 255;
bool g_ready = false;
int g_slideX = 0;

struct Tick {
  int16_t angle;  // LVGL arc angle, 135 = scale minimum, 405 = maximum
};

constexpr size_t kTickCount = 6;

// Even 54-degree steps from the 135-degree arc start. Radius 188 puts the
// labels close to the edge arc but still inside its 206 px inner edge. The
// text is rebuilt by updateScaleVisuals() whenever the range changes.
const Tick kTicks[kTickCount] = {
    {135}, {189}, {243}, {297}, {351}, {405},
};

// -2.5 -> "-2.5", -2.0 -> "-2", 12.5 -> "12.5". The 96 px face is only used
// for the value row; the labels are Montserrat 20.
void formatTenths(int16_t tenths, char *buf, size_t len) {
  const int a = abs((int)tenths);
  if (a % 10 == 0) {
    snprintf(buf, len, "%s%d", tenths < 0 ? "-" : "", a / 10);
  } else {
    snprintf(buf, len, "%s%d.%d", tenths < 0 ? "-" : "", a / 10, a % 10);
  }
}

// Repaint the arc range and the six labels from g_min10/g_max10. Called from
// create() (defaults) and from applyScale() (web page).
void updateScaleVisuals() {
  if (!g_ready) return;

  lv_arc_set_range(g_arc, 0, g_arcRange);

  for (size_t i = 0; i < kTickCount; i++) {
    if (g_ticks[i] == nullptr) continue;
    const int t = (int)g_min10 + (g_arcRange * (int)i) / (int)(kTickCount - 1);
    char buf[8];
    formatTenths((int16_t)t, buf, sizeof(buf));
    lv_label_set_text(g_ticks[i], buf);
  }
}

void plain(lv_obj_t *obj) {
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

// A scheme change repaints the whole gauge: the arc, the value, the ticks
// and the captions all take a role from the palette, and on the non-Midnight
// schemes the text itself is the scheme's hue, not a shared white.
void applyScheme() {
  if (!g_ready) return;

  const lv_color_t accent = theme::accent();
  lv_obj_set_style_arc_color(g_arc, accent, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(g_arc, theme::track(), LV_PART_MAIN);
  lv_obj_set_style_text_color(g_unit, accent, 0);
  for (lv_obj_t *tick : g_ticks) {
    if (tick != nullptr) lv_obj_set_style_text_color(tick, theme::dim(), 0);
  }

  const lv_color_t fg = theme::fg();
  lv_obj_set_style_text_color(g_whole, fg, 0);
  lv_obj_set_style_text_color(g_dot, fg, 0);
  lv_obj_set_style_text_color(g_frac, fg, 0);
  lv_obj_set_style_bg_color(g_rule, theme::line(), 0);
}

// The gauge is faded out while the G-meter fades in, and slid sideways during
// a swipe.  LVGL can only keep per-object opacity cheap; a container opacity
// would allocate a 466x466 layer (434 KB), which does not fit the 128 KB
// pool.  So every visible object gets its own scale, and the two
// deliberately dim captions keep their 60% base opacity.
void applyOpacity() {
  if (!g_ready) return;

  lv_obj_set_style_arc_opa(g_arc, g_masterOpa, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(g_arc, g_masterOpa, LV_PART_INDICATOR);

  const uint8_t dim = (uint8_t)(((unsigned)LV_OPA_60 * g_masterOpa) / 255u);
  for (lv_obj_t *tick : g_ticks) {
    if (tick != nullptr) lv_obj_set_style_text_opa(tick, dim, 0);
  }
  lv_obj_set_style_text_opa(g_whole, g_masterOpa, 0);
  lv_obj_set_style_text_opa(g_dot, g_masterOpa, 0);
  lv_obj_set_style_text_opa(g_frac, g_masterOpa, 0);
  lv_obj_set_style_text_opa(g_unit, g_masterOpa, 0);
  lv_obj_set_style_bg_opa(g_status, g_masterOpa, 0);
  lv_obj_set_style_bg_opa(g_rule, g_masterOpa, 0);
}

void addTicks(lv_obj_t *scr) {
  size_t i = 0;
  for (const Tick &t : kTicks) {
    const float rad = (float)t.angle * 0.0174532925f;
    const int cx = kArcSize / 2 + (int)lroundf((float)kTickRadius * cosf(rad));
    const int cy = kArcSize / 2 + (int)lroundf((float)kTickRadius * sinf(rad));

    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, 52, 28);
    lv_obj_set_pos(box, cx - 26, cy - 14);
    plain(box);

    lv_obj_t *label = lv_label_create(box);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_font(label, FONT_SMALL, 0);
    lv_obj_set_style_text_color(label, theme::dim(), 0);
    lv_obj_set_style_text_opa(label, LV_OPA_60, 0);
    lv_obj_center(label);

    if (i < sizeof(g_ticks) / sizeof(g_ticks[0])) g_ticks[i] = label;
    i++;
  }
}

void addValueRow(lv_obj_t *scr) {
  lv_obj_t *row = lv_obj_create(scr);
  lv_obj_set_size(row, 388, 124);
  lv_obj_set_pos(row, 39, 114);
  plain(row);

  lv_obj_set_layout(row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 0, 0);

  g_whole = lv_label_create(row);
  lv_label_set_text(g_whole, "--");
  lv_obj_set_style_text_font(g_whole, FONT_VALUE, 0);
  lv_obj_set_style_text_color(g_whole, theme::fg(), 0);

  // A real period glyph shares the digits' baseline, so it lands at the
  // bottom of the value instead of floating in the middle.
  g_dot = lv_label_create(row);
  lv_label_set_text(g_dot, ".");
  lv_obj_set_style_text_font(g_dot, FONT_VALUE, 0);
  lv_obj_set_style_text_color(g_dot, theme::fg(), 0);

  g_frac = lv_label_create(row);
  lv_label_set_text(g_frac, "0");
  lv_obj_set_style_text_font(g_frac, FONT_VALUE, 0);
  lv_obj_set_style_text_color(g_frac, theme::fg(), 0);
}

}  // namespace

void begin() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // A full-screen transparent container owns every oil face object. The
  // dashboard hides this one container when the G-meter is up, fades its
  // children individually during the transition, and slides the container
  // itself during a swipe.
  g_root = lv_obj_create(scr);
  lv_obj_set_size(g_root, kArcSize, kArcSize);
  lv_obj_set_pos(g_root, 0, 0);
  plain(g_root);

  // No rim ring here: a full-circle 1 px border is only visible in the 90
  // degree gap the arc leaves at the bottom, where it reads as a stray line
  // between the two arc ends. The arc itself is the edge.

  // The scale: 135 -> 45 degrees, clockwise, leaving the bottom open.
  g_arc = lv_arc_create(g_root);
  lv_obj_set_size(g_arc, kArcSize, kArcSize);
  lv_obj_center(g_arc);
  lv_arc_set_bg_angles(g_arc, 135, 45);
  lv_arc_set_range(g_arc, 0, g_arcRange);
  lv_arc_set_value(g_arc, 0);
  lv_obj_remove_flag(g_arc, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_set_style_bg_opa(g_arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_arc, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(g_arc, 0, LV_PART_KNOB);

  lv_obj_set_style_arc_width(g_arc, kArcWidth, LV_PART_MAIN);
  lv_obj_set_style_arc_color(g_arc, theme::track(), LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(g_arc, true, LV_PART_MAIN);

  lv_obj_set_style_arc_width(g_arc, kArcWidth, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(g_arc, theme::accent(), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(g_arc, true, LV_PART_INDICATOR);

  // Ticks
  addTicks(g_root);

  // Value row (integer . fraction)
  addValueRow(g_root);

  g_unit = lv_label_create(g_root);
  lv_label_set_text(g_unit, "OIL °C");
  lv_obj_set_style_text_font(g_unit, FONT_LABEL, 0);
  lv_obj_set_style_text_color(g_unit, theme::accent(), 0);
  lv_obj_set_style_text_letter_space(g_unit, 5, 0);
  lv_obj_align(g_unit, LV_ALIGN_TOP_MID, 0, 246);

  // 2 px rather than 1: on an emissive panel at this density a single row of
  // lit pixels reads as a faint seam, and the rule is meant to separate.
  g_rule = lv_obj_create(g_root);
  lv_obj_set_size(g_rule, 168, 2);
  lv_obj_set_pos(g_rule, 149, 280);
  lv_obj_set_style_bg_color(g_rule, theme::line(), 0);
  lv_obj_set_style_bg_opa(g_rule, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_rule, 0, 0);
  lv_obj_remove_flag(g_rule, LV_OBJ_FLAG_SCROLLABLE);

  // No IAT display in oil view

  // Link state as a small line at the bottom edge: a few pixels tall, wide
  // enough to read at a glance without becoming a lamp.
  g_status = lv_obj_create(g_root);
  lv_obj_set_size(g_status, 26, 5);
  lv_obj_set_style_radius(g_status, 3, 0);
  lv_obj_set_style_bg_color(g_status, COL_STATUS_IDLE, 0);
  lv_obj_set_style_bg_opa(g_status, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_status, 0, 0);
  lv_obj_remove_flag(g_status, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(g_status, LV_ALIGN_BOTTOM_MID, 0, -23);

  g_ready = true;
  applyScheme();
  applyOpacity();
  updateScaleVisuals();
}

void update(const ohxnow::Telemetry &t, const ohxnow::Stats &s) {
  if (!g_ready) return;

  // Oil temperature from Cluster_2 (0x420): byte 3 is OilTemperature_4_1,
  // 1 °C/count with -60 °C offset. 0xFF means absent.
  // The telemetry already gives us the value in °C.
  static float shown = NAN;
  const float target = t.oilTempValid ? t.oilTempC : NAN;

  if (isnan(target)) {
    shown = NAN;
  } else if (isnan(shown)) {
    shown = target;
  } else {
    shown += (target - shown) * 0.30f;
  }

  // Only touch the widgets when the rounded value changes. The smoothing
  // loop runs at 20 Hz, but repainting the thick arc on every iteration is
  // what made the loop sluggish and the value look unsettled.
  const bool have = !isnan(shown);
  const int tenths = have ? (int)lroundf(shown * 10.0f) : 0;
  const int32_t drawn = have ? (int32_t)tenths : INT32_MIN;

  if (g_scaleDirty || drawn != g_lastDrawn) {
    g_scaleDirty = false;
    g_lastDrawn = drawn;
    if (!have) {
      lv_label_set_text(g_whole, "--");
      lv_obj_add_flag(g_dot, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(g_frac, LV_OBJ_FLAG_HIDDEN);
      lv_arc_set_value(g_arc, 0);
    } else {
      lv_obj_remove_flag(g_dot, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(g_frac, LV_OBJ_FLAG_HIDDEN);

      // abs() so a negative value renders as "-2.5" rather than "-2.-5".
      const int a = abs(tenths);
      char buf[8];
      snprintf(buf, sizeof(buf), "%s%d", tenths < 0 ? "-" : "", a / 10);
      lv_label_set_text(g_whole, buf);
      snprintf(buf, sizeof(buf), "%d", a % 10);
      lv_label_set_text(g_frac, buf);

      // The arc spans min..max; the numeric row still shows the true value
      // when it is outside the scale (the arc just sits at an end).
      int arc = tenths - (int)g_min10;
      if (arc < 0) arc = 0;
      if (arc > g_arcRange) arc = g_arcRange;
      lv_arc_set_value(g_arc, arc);
    }

    // LVGL's incremental clean-up of this thick rounded 270-degree arc left
    // a trail of old indicator pixels and could paint over the overlapping
    // value row. A synthetic sweep was clean only when the whole
    // arc was repainted; incremental invalidation produced the green trail
    // and a white block in the row. The larger 466 px arc keeps that habit.
    lv_obj_invalidate(g_arc);
  }

  // Status bar: red=never received, green=streaming, orange=stale, amber=IAT only
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
    statusColor = COL_STATUS_IDLE;  // red - packets received but no valid data
  }

  static lv_color_t lastStatusColor = lv_color_hex(0xFFFFFFFF);  // invalid sentinel
  if (!lv_color_eq(statusColor, lastStatusColor)) {
    lastStatusColor = statusColor;
    lv_obj_set_style_bg_color(g_status, statusColor, 0);
  }
  lv_obj_clear_flag(g_status, LV_OBJ_FLAG_HIDDEN);
}

void setScheme(theme::Scheme s, bool persist) {
  theme::setScheme(s);
  if (persist) settings::setScheme((uint8_t)theme::scheme());
  applyScheme();
}

bool applyScale(int16_t min10, int16_t max10) {
  if (min10 >= max10) return false;
  if ((int16_t)(max10 - min10) < settings::kOilSpanMin10) return false;

  g_min10 = min10;
  g_max10 = max10;
  g_arcRange = (int)(max10 - min10);
  g_scaleDirty = true;

  if (g_ready) {
    updateScaleVisuals();
    lv_obj_invalidate(g_arc);
  }
  return true;
}

void setVisible(bool visible) {
  if (!g_ready) return;
  if (visible) {
    lv_obj_remove_flag(g_root, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_root, LV_OBJ_FLAG_HIDDEN);
  }
}

void setOpacity(uint8_t opacity) {
  if (!g_ready || opacity == g_masterOpa) return;
  g_masterOpa = opacity;
  applyOpacity();
}

void setSlide(int dx) {
  if (!g_ready || dx == g_slideX) return;
  g_slideX = dx;
  lv_obj_set_pos(g_root, dx, 0);
}

int slide() { return g_slideX; }

lv_obj_t *root() { return g_root; }

// Capture build: render a fixed reading and dump the framebuffer
void captureRender(float oilTempC, float iatC) {
  // This is a placeholder - the actual capture uses gauge_ui's captureRender
  // which handles the scheme sweep. The oil face would need its own capture
  // if we want to photograph it separately.
}

}  // namespace oil_ui