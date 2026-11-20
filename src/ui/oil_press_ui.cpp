// =====================================================================
// oil_press_ui.cpp - the digital oil pressure face
//
// Same layout as the boost gauge: 270 degree arc with six scale labels,
// a large value with decimal point on the baseline, an OIL PRESSURE bar
// caption. The arc scale is configurable (0.1 bar steps) and persisted in NVS.
// =====================================================================
#include "ui/oil_press_ui.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "app/settings.h"
#include "theme.h"

namespace oil_press_ui {
namespace {

int16_t g_min10 = 40;   // 4.0 bar * 10
int16_t g_max10 = 200;  // 20.0 bar * 10
int g_arcRange = 160;   // (200 - 40) * 10

int32_t g_lastDrawn = INT32_MIN;
bool g_scaleDirty = true;

constexpr int kArcSize = 466;
constexpr int kArcWidth = 27;
constexpr int kTickRadius = kArcSize / 2 - kArcWidth - 18;

lv_obj_t *g_root = nullptr;
lv_obj_t *g_arc = nullptr;
lv_obj_t *g_whole = nullptr;
lv_obj_t *g_dot = nullptr;
lv_obj_t *g_frac = nullptr;
lv_obj_t *g_unit = nullptr;
lv_obj_t *g_status = nullptr;
lv_obj_t *g_ticks[6] = {};
uint8_t g_masterOpa = 255;
bool g_ready = false;
int g_slideX = 0;

struct Tick {
  int16_t angle;  // LVGL arc angle, 135 = scale minimum, 405 = maximum
};

constexpr size_t kTickCount = 6;
const Tick kTicks[kTickCount] = {
    {135}, {189}, {243}, {297}, {351}, {405},
};

void formatTenths(int16_t tenths, char *buf, size_t len) {
  const int a = abs((int)tenths);
  if (a % 10 == 0) {
    snprintf(buf, len, "%s%d", tenths < 0 ? "-" : "", a / 10);
  } else {
    snprintf(buf, len, "%s%d.%d", tenths < 0 ? "-" : "", a / 10, a % 10);
  }
}

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
  lv_obj_set_style_bg_color(g_status, theme::line(), 0);
}

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
  lv_obj_set_style_bg_opa(g_status, g_masterOpa, 0);
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

  g_root = lv_obj_create(scr);
  lv_obj_set_size(g_root, kArcSize, kArcSize);
  lv_obj_set_pos(g_root, 0, 0);
  plain(g_root);  // Ensure black background, no padding/border, not scrollable/clickable

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

  addTicks(g_root);
  addValueRow(g_root);

  g_unit = lv_label_create(g_root);
  lv_label_set_text(g_unit, "OIL PRESSURE BAR");
  lv_obj_set_style_text_font(g_unit, FONT_LABEL, 0);
  lv_obj_set_style_text_color(g_unit, theme::accent(), 0);
  lv_obj_set_style_text_letter_space(g_unit, 5, 0);
  lv_obj_align(g_unit, LV_ALIGN_TOP_MID, 0, 246);

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

  static float shown = NAN;
  const float target = t.oilPressValid ? t.oilPressBar : NAN;

  if (isnan(target)) {
    shown = NAN;
  } else if (isnan(shown)) {
    shown = target;
  } else {
    shown += (target - shown) * 0.30f;
  }

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

      const int a = abs(tenths);
      char buf[8];
      snprintf(buf, sizeof(buf), "%s%d", tenths < 0 ? "-" : "", a / 10);
      lv_label_set_text(g_whole, buf);
      snprintf(buf, sizeof(buf), "%d", a % 10);
      lv_label_set_text(g_frac, buf);

      int arc = tenths - (int)g_min10;
      if (arc < 0) arc = 0;
      if (arc > g_arcRange) arc = g_arcRange;
      lv_arc_set_value(g_arc, arc);
    }

    lv_obj_invalidate(g_arc);
  }

  lv_color_t statusColor;
  if (s.raw == 0) {
    statusColor = COL_STATUS_IDLE;
  } else if (t.statusValid) {
    statusColor = COL_STATUS_OK;
  } else if (s.statusPackets > 0) {
    statusColor = COL_STATUS_STALE;
  } else if (s.canPackets > 0) {
    statusColor = COL_STATUS_STALE;
  } else {
    statusColor = COL_STATUS_IDLE;
  }

  static lv_color_t lastStatusColor = lv_color_hex(0xFFFFFFFF);
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
  // Oil pressure: 10 tenths min span (1.0 bar) - typical resolution is 0.1 bar
  if ((int16_t)(max10 - min10) < 10) return false;

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

void captureRender(float oilPressBar, float iatC) {
  // This is a placeholder - the actual capture uses gauge_ui's captureRender
  // which handles the scheme sweep. The oil press face would need its own capture
  // if we want to photograph it separately.
}

}  // namespace oil_press_ui
