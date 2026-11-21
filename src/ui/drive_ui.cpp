// =====================================================================
// drive_ui.cpp - drivetrain visualisation face
//
// Simplified graphical view for 466x466 round display.
// Shows: chassis outline, front/rear drive lines, engine RPM,
// front/rear diffs, Haldex pack with fill (actual %) and request line,
// four wheels with animated treads.
// Only text: RPM and Haldex actual %.
// =====================================================================
#include "ui/drive_ui.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

#include "app/settings.h"
#include "theme.h"
#include "ohx_proto.h"

namespace drive_ui {
namespace {

constexpr int kW = 466;
constexpr int kH = 466;
constexpr int kCenterX = kW / 2;
constexpr int kCenterY = kH / 2;

// Layout coordinates
constexpr int kEngineBoxX = 178, kEngineBoxY = 30, kEngineBoxW = 110, kEngineBoxH = 46;
constexpr int kFrontDiffCX = 233, kFrontDiffCY = 108, kFrontDiffR = 9;
constexpr int kRearDiffCX = 233, kRearDiffCY = 358, kRearDiffR = 11;
constexpr int kHaldexBoxX = 199, kHaldexBoxY = 208, kHaldexBoxW = 68, kHaldexBoxH = 50;
constexpr int kHaldexFillW = 58;
constexpr int kHaldexFillHMax = 40;
constexpr int kHaldexReqLineY = 253;

// Wheel positions
constexpr int kWheelW = 30, kWheelH = 60;
struct WheelPos { int x, y; };
const WheelPos kWheelPos[4] = {
    {130, 78},   // FL
    {306, 78},   // FR
    {130, 328},  // RL
    {306, 328},  // RR
};

lv_obj_t *g_root = nullptr;
lv_obj_t *g_chassis = nullptr;
lv_obj_t *g_frontLines[3] = {nullptr};
lv_obj_t *g_rearLines[4] = {nullptr};
lv_obj_t *g_engineBox = nullptr;
lv_obj_t *g_rpmLabel = nullptr;
lv_obj_t *g_frontDiff = nullptr;
lv_obj_t *g_rearDiff = nullptr;
lv_obj_t *g_haldexBox = nullptr;
lv_obj_t *g_haldexFill = nullptr;
lv_obj_t *g_haldexReqLine = nullptr;
lv_obj_t *g_haldexPctLabel = nullptr;  // actual % inside haldex box
lv_obj_t *g_wheelRect[4] = {nullptr};
lv_obj_t *g_wheelRing[4] = {nullptr};
lv_obj_t *g_wheelTread[4] = {nullptr};

uint8_t g_masterOpa = 255;
bool g_ready = false;
int g_slideX = 0;

// Tread animation state
float g_treadPhase[4] = {0};
uint32_t g_lastTreadUpdate = 0;

// Colors
lv_color_t col_fg() { return theme::fg(); }
lv_color_t col_dim() { return theme::dim(); }
lv_color_t col_green() { return lv_color_hex(0x22C55E); }
lv_color_t col_cyan() { return lv_color_hex(0x22C0F0); }
lv_color_t col_amber() { return lv_color_hex(0xF59E0B); }
lv_color_t col_red() { return lv_color_hex(0xEF4444); }
lv_color_t col_bg() { return lv_color_hex(0x000000); }
lv_color_t col_chassis() { return lv_color_hex(0x14171C); }
lv_color_t col_struct() { return lv_color_hex(0x23272E); }
lv_color_t col_panel() { return lv_color_hex(0x101318); }
lv_color_t col_panelStroke() { return lv_color_hex(0x2C323A); }
lv_color_t col_tread() { return lv_color_hex(0x3F454D); }
lv_color_t col_ring() { return lv_color_hex(0x3A4048); }
lv_color_t col_chipBg() { return lv_color_hex(0x14171C); }

void plain(lv_obj_t *obj) {
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

void applyScheme() {
  if (!g_ready) return;

  const lv_color_t fg = col_fg();
  const lv_color_t dim = col_dim();
  const lv_color_t accent = theme::accent();

  // Update text colors to follow the current scheme
  if (g_engineBox) {
    lv_obj_t *engLabel = lv_obj_get_child(g_engineBox, 0);
    if (engLabel) lv_obj_set_style_text_color(engLabel, dim, 0);
  }
  if (g_rpmLabel) lv_obj_set_style_text_color(g_rpmLabel, fg, 0);
  if (g_haldexPctLabel) lv_obj_set_style_text_color(g_haldexPctLabel, fg, 0);

  // Request line uses accent
  if (g_haldexReqLine) lv_obj_set_style_bg_color(g_haldexReqLine, accent, 0);
}

void applyOpacity() {
  if (!g_ready) return;
  const uint8_t dim = (uint8_t)(((unsigned)LV_OPA_60 * g_masterOpa) / 255u);
  if (g_chassis) lv_obj_set_style_opa(g_chassis, g_masterOpa, 0);
  for (int i = 0; i < 3; i++) if (g_frontLines[i]) lv_obj_set_style_opa(g_frontLines[i], g_masterOpa, 0);
  for (int i = 0; i < 4; i++) if (g_rearLines[i]) lv_obj_set_style_opa(g_rearLines[i], g_masterOpa, 0);
  if (g_engineBox) lv_obj_set_style_opa(g_engineBox, g_masterOpa, 0);
  if (g_rpmLabel) lv_obj_set_style_text_opa(g_rpmLabel, g_masterOpa, 0);
  if (g_frontDiff) lv_obj_set_style_opa(g_frontDiff, g_masterOpa, 0);
  if (g_rearDiff) lv_obj_set_style_opa(g_rearDiff, g_masterOpa, 0);
  if (g_haldexBox) lv_obj_set_style_opa(g_haldexBox, g_masterOpa, 0);
  if (g_haldexFill) lv_obj_set_style_opa(g_haldexFill, g_masterOpa, 0);
  if (g_haldexReqLine) lv_obj_set_style_opa(g_haldexReqLine, g_masterOpa, 0);
  if (g_haldexPctLabel) lv_obj_set_style_text_opa(g_haldexPctLabel, g_masterOpa, 0);
  for (int i = 0; i < 4; i++) {
    if (g_wheelRect[i]) lv_obj_set_style_opa(g_wheelRect[i], g_masterOpa, 0);
    if (g_wheelRing[i]) lv_obj_set_style_opa(g_wheelRing[i], g_masterOpa, 0);
    if (g_wheelTread[i]) lv_obj_set_style_opa(g_wheelTread[i], g_masterOpa, 0);
  }
}

void addChassis(lv_obj_t *scr) {
  // Chassis outline
  g_chassis = lv_obj_create(scr);
  lv_obj_set_size(g_chassis, 152, 418);
  lv_obj_set_pos(g_chassis, 157, 24);
  lv_obj_set_style_radius(g_chassis, 60, 0);
  lv_obj_set_style_bg_opa(g_chassis, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_chassis, 3, 0);
  lv_obj_set_style_border_color(g_chassis, col_chassis(), 0);
  plain(g_chassis);

  // Front drive (green, always on)
  // Vertical: 233, 76 -> 233, 108 (height 32, width 6)
  g_frontLines[0] = lv_obj_create(scr);
  lv_obj_set_size(g_frontLines[0], 6, 32);
  lv_obj_set_pos(g_frontLines[0], 230, 76);
  lv_obj_set_style_bg_color(g_frontLines[0], col_green(), 0);
  lv_obj_set_style_bg_opa(g_frontLines[0], LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_frontLines[0], 0, 0);
  plain(g_frontLines[0]);

  // Horizontal left: 233, 108 -> 162, 108 (width 71, height 6)
  g_frontLines[1] = lv_obj_create(scr);
  lv_obj_set_size(g_frontLines[1], 71, 6);
  lv_obj_set_pos(g_frontLines[1], 162, 105);
  lv_obj_set_style_bg_color(g_frontLines[1], col_green(), 0);
  lv_obj_set_style_bg_opa(g_frontLines[1], LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_frontLines[1], 0, 0);
  plain(g_frontLines[1]);

  // Horizontal right: 233, 108 -> 304, 108 (width 71, height 6)
  g_frontLines[2] = lv_obj_create(scr);
  lv_obj_set_size(g_frontLines[2], 71, 6);
  lv_obj_set_pos(g_frontLines[2], 233, 105);
  lv_obj_set_style_bg_color(g_frontLines[2], col_green(), 0);
  lv_obj_set_style_bg_opa(g_frontLines[2], LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_frontLines[2], 0, 0);
  plain(g_frontLines[2]);

  // Rear drive (cyan, scales with Haldex lock)
  // Vertical upper: 233, 117 -> 233, 208 (height 91, width 3)
  g_rearLines[0] = lv_obj_create(scr);
  lv_obj_set_size(g_rearLines[0], 3, 91);
  lv_obj_set_pos(g_rearLines[0], 231, 117);
  lv_obj_set_style_bg_color(g_rearLines[0], col_cyan(), 0);
  lv_obj_set_style_bg_opa(g_rearLines[0], LV_OPA_20, 0);
  lv_obj_set_style_radius(g_rearLines[0], 0, 0);
  plain(g_rearLines[0]);

  // Vertical lower: 233, 258 -> 233, 347 (height 89, width 3)
  g_rearLines[1] = lv_obj_create(scr);
  lv_obj_set_size(g_rearLines[1], 3, 89);
  lv_obj_set_pos(g_rearLines[1], 231, 258);
  lv_obj_set_style_bg_color(g_rearLines[1], col_cyan(), 0);
  lv_obj_set_style_bg_opa(g_rearLines[1], LV_OPA_20, 0);
  lv_obj_set_style_radius(g_rearLines[1], 0, 0);
  plain(g_rearLines[1]);

  // Horizontal left: 233, 358 -> 162, 358 (width 71, height 3)
  g_rearLines[2] = lv_obj_create(scr);
  lv_obj_set_size(g_rearLines[2], 71, 3);
  lv_obj_set_pos(g_rearLines[2], 162, 356);
  lv_obj_set_style_bg_color(g_rearLines[2], col_cyan(), 0);
  lv_obj_set_style_bg_opa(g_rearLines[2], LV_OPA_20, 0);
  lv_obj_set_style_radius(g_rearLines[2], 0, 0);
  plain(g_rearLines[2]);

  // Horizontal right: 233, 358 -> 304, 358 (width 71, height 3)
  g_rearLines[3] = lv_obj_create(scr);
  lv_obj_set_size(g_rearLines[3], 71, 3);
  lv_obj_set_pos(g_rearLines[3], 233, 356);
  lv_obj_set_style_bg_color(g_rearLines[3], col_cyan(), 0);
  lv_obj_set_style_bg_opa(g_rearLines[3], LV_OPA_20, 0);
  lv_obj_set_style_radius(g_rearLines[3], 0, 0);
  plain(g_rearLines[3]);
}

void addEngine(lv_obj_t *scr) {
  g_engineBox = lv_obj_create(scr);
  lv_obj_set_size(g_engineBox, kEngineBoxW, kEngineBoxH);
  lv_obj_set_pos(g_engineBox, kEngineBoxX, kEngineBoxY);
  lv_obj_set_style_radius(g_engineBox, 9, 0);
  lv_obj_set_style_bg_color(g_engineBox, col_panel(), 0);
  lv_obj_set_style_border_width(g_engineBox, 2, 0);
  lv_obj_set_style_border_color(g_engineBox, col_panelStroke(), 0);
  plain(g_engineBox);

  lv_obj_t *engLabel = lv_label_create(g_engineBox);
  lv_label_set_text(engLabel, "ENGINE");
  lv_obj_set_style_text_font(engLabel, FONT_SMALL, 0);
  lv_obj_set_style_text_color(engLabel, col_dim(), 0);
  lv_obj_set_style_text_letter_space(engLabel, 3, 0);
  lv_obj_align(engLabel, LV_ALIGN_TOP_MID, 0, 4);

  g_rpmLabel = lv_label_create(g_engineBox);
  lv_label_set_text(g_rpmLabel, "----");
  lv_obj_set_style_text_font(g_rpmLabel, FONT_SMALL, 0);  // 20px
  lv_obj_set_style_text_color(g_rpmLabel, col_fg(), 0);
  lv_obj_align(g_rpmLabel, LV_ALIGN_CENTER, 0, 8);
}

void addDiffs(lv_obj_t *scr) {
  // Front diff
  g_frontDiff = lv_obj_create(scr);
  lv_obj_set_size(g_frontDiff, kFrontDiffR * 2, kFrontDiffR * 2);
  lv_obj_set_pos(g_frontDiff, kFrontDiffCX - kFrontDiffR, kFrontDiffCY - kFrontDiffR);
  lv_obj_set_style_radius(g_frontDiff, kFrontDiffR, 0);
  lv_obj_set_style_bg_color(g_frontDiff, col_panel(), 0);
  lv_obj_set_style_border_width(g_frontDiff, 2, 0);
  lv_obj_set_style_border_color(g_frontDiff, col_panelStroke(), 0);
  plain(g_frontDiff);

  // Rear diff
  g_rearDiff = lv_obj_create(scr);
  lv_obj_set_size(g_rearDiff, kRearDiffR * 2, kRearDiffR * 2);
  lv_obj_set_pos(g_rearDiff, kRearDiffCX - kRearDiffR, kRearDiffCY - kRearDiffR);
  lv_obj_set_style_radius(g_rearDiff, kRearDiffR, 0);
  lv_obj_set_style_bg_color(g_rearDiff, col_panel(), 0);
  lv_obj_set_style_border_width(g_rearDiff, 2, 0);
  lv_obj_set_style_border_color(g_rearDiff, col_panelStroke(), 0);
  plain(g_rearDiff);
}

void addHaldex(lv_obj_t *scr) {
  // Haldex pack box
  g_haldexBox = lv_obj_create(scr);
  lv_obj_set_size(g_haldexBox, kHaldexBoxW, kHaldexBoxH);
  lv_obj_set_pos(g_haldexBox, kHaldexBoxX, kHaldexBoxY);
  lv_obj_set_style_radius(g_haldexBox, 7, 0);
  lv_obj_set_style_bg_color(g_haldexBox, col_panel(), 0);
  lv_obj_set_style_border_width(g_haldexBox, 2, 0);
  lv_obj_set_style_border_color(g_haldexBox, col_panelStroke(), 0);
  plain(g_haldexBox);

  // Fill (actual lock) - grows from bottom
  g_haldexFill = lv_obj_create(g_haldexBox);
  lv_obj_set_size(g_haldexFill, kHaldexFillW, 0);
  lv_obj_set_pos(g_haldexFill, 5, kHaldexBoxH - 5);  // bottom-left inside
  lv_obj_set_style_radius(g_haldexFill, 5, 0);
  lv_obj_set_style_bg_color(g_haldexFill, col_cyan(), 0);
  lv_obj_set_style_bg_opa(g_haldexFill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_haldexFill, 0, 0);
  plain(g_haldexFill);

  // Request line (horizontal) - thin rectangle at request position
  g_haldexReqLine = lv_obj_create(g_haldexBox);
  lv_obj_set_size(g_haldexReqLine, kHaldexBoxW + 4, 2);
  lv_obj_set_pos(g_haldexReqLine, -2, kHaldexReqLineY - kHaldexBoxY);
  lv_obj_set_style_bg_color(g_haldexReqLine, theme::accent(), 0);
  lv_obj_set_style_bg_opa(g_haldexReqLine, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_haldexReqLine, 0, 0);
  plain(g_haldexReqLine);

  // HALDEX label (top)
  lv_obj_t *hLabel = lv_label_create(g_haldexBox);
  lv_label_set_text(hLabel, "HALDEX");
  lv_obj_set_style_text_font(hLabel, FONT_SMALL, 0);
  lv_obj_set_style_text_color(hLabel, col_dim(), 0);
  lv_obj_set_style_text_letter_space(hLabel, 3, 0);
  lv_obj_align(hLabel, LV_ALIGN_TOP_MID, 0, -8);

  // Actual % label (centered in the fill area)
  g_haldexPctLabel = lv_label_create(g_haldexBox);
  lv_label_set_text(g_haldexPctLabel, "0%");
  lv_obj_set_style_text_font(g_haldexPctLabel, FONT_LABEL, 0);  // 26px
  lv_obj_set_style_text_color(g_haldexPctLabel, col_fg(), 0);
  lv_obj_align(g_haldexPctLabel, LV_ALIGN_CENTER, 0, -4);
}

void addWheels(lv_obj_t *scr) {
  for (int i = 0; i < 4; i++) {
    int x = kWheelPos[i].x;
    int y = kWheelPos[i].y;

    // Wheel rect background
    g_wheelRect[i] = lv_obj_create(scr);
    lv_obj_set_size(g_wheelRect[i], kWheelW, kWheelH);
    lv_obj_set_pos(g_wheelRect[i], x, y);
    lv_obj_set_style_radius(g_wheelRect[i], 6, 0);
    lv_obj_set_style_bg_color(g_wheelRect[i], lv_color_hex(0x12151A), 0);
    plain(g_wheelRect[i]);

    // Tread container (clipped)
    g_wheelTread[i] = lv_obj_create(g_wheelRect[i]);
    lv_obj_set_size(g_wheelTread[i], kWheelW, kWheelH);
    lv_obj_set_pos(g_wheelTread[i], 0, 0);
    lv_obj_set_style_radius(g_wheelTread[i], 6, 0);
    lv_obj_set_style_clip_corner(g_wheelTread[i], true, 0);
    plain(g_wheelTread[i]);

    // Tread rows (8 rows) - positioned relative to container
    for (int row = -1; row < 8; row++) {
      lv_obj_t *tread = lv_obj_create(g_wheelTread[i]);
      lv_obj_set_size(tread, 24, 3);
      lv_obj_set_pos(tread, 3, row * 10);  // 10px gap
      lv_obj_set_style_radius(tread, 1, 0);
      lv_obj_set_style_bg_color(tread, col_tread(), 0);
      plain(tread);
    }

    // Ring (slip indicator - subtle border)
    g_wheelRing[i] = lv_obj_create(scr);
    lv_obj_set_size(g_wheelRing[i], kWheelW, kWheelH);
    lv_obj_set_pos(g_wheelRing[i], x, y);
    lv_obj_set_style_radius(g_wheelRing[i], 6, 0);
    lv_obj_set_style_bg_opa(g_wheelRing[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_wheelRing[i], 3, 0);
    lv_obj_set_style_border_color(g_wheelRing[i], col_ring(), 0);
    plain(g_wheelRing[i]);
  }
}

}  // namespace

void begin() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, col_bg(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  g_root = lv_obj_create(scr);
  lv_obj_set_size(g_root, kW, kH);
  lv_obj_set_pos(g_root, 0, 0);
  plain(g_root);

  addChassis(g_root);
  addEngine(g_root);
  addDiffs(g_root);
  addHaldex(g_root);
  addWheels(g_root);

  g_ready = true;
  applyScheme();
  applyOpacity();
}

void update(const ohxnow::Telemetry &t, const ohxnow::Stats &s) {
  if (!g_ready) return;

  const bool live = s.raw > 0 && t.statusValid;
  const bool stale = !live;

  // RPM
  if (g_rpmLabel) {
    if (stale || isnan(t.rpm)) {
      lv_label_set_text(g_rpmLabel, "----");
      lv_obj_set_style_text_color(g_rpmLabel, lv_color_hex(0x555555), 0);
    } else {
      char buf[16];
      snprintf(buf, sizeof(buf), "%d", (int)(t.rpm / 10) * 10);
      lv_label_set_text(g_rpmLabel, buf);
      lv_obj_set_style_text_color(g_rpmLabel, col_fg(), 0);
    }
  }

  // Haldex actual fill (0-100% -> 0-40px height) and % label
  if (g_haldexFill && g_haldexPctLabel && t.statusValid) {
    int lockActual = t.lockActual;  // 0-100 from OHX_STATUS
    if (lockActual < 0) lockActual = 0;
    if (lockActual > 100) lockActual = 100;

    // Fill height
    int fillH = (lockActual * kHaldexFillHMax) / 100;
    lv_obj_set_size(g_haldexFill, kHaldexFillW, fillH);
    lv_obj_set_pos(g_haldexFill, 5, kHaldexBoxH - 5 - fillH);  // bottom-aligned

    // % label
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", lockActual);
    lv_label_set_text(g_haldexPctLabel, buf);
    lv_obj_set_style_text_color(g_haldexPctLabel, col_fg(), 0);
  }

  // Haldex request line position (0-100% maps to box height)
  if (g_haldexReqLine && t.statusValid) {
    int lockTarget = t.lockTarget;  // 0-100 from OHX_STATUS
    if (lockTarget < 0) lockTarget = 0;
    if (lockTarget > 100) lockTarget = 100;

    // Y position in box coordinates (0 at top, kHaldexBoxH at bottom)
    // Request line should be at the fill level: bottom - (pct * height)
    int reqY = kHaldexBoxH - (lockTarget * kHaldexBoxH) / 100;
    lv_obj_set_pos(g_haldexReqLine, -2, reqY);
  }

  // Rear drive line opacity scales with lockActual
  if (t.statusValid) {
    int lockActual = t.lockActual;
    if (lockActual < 0) lockActual = 0;
    if (lockActual > 100) lockActual = 100;
    uint8_t opa = (uint8_t)(20 + (lockActual * 200) / 100);  // 20..220
    for (int i = 0; i < 4; i++) {
      if (g_rearLines[i]) lv_obj_set_style_bg_opa(g_rearLines[i], opa, 0);
    }
  } else {
    for (int i = 0; i < 4; i++) {
      if (g_rearLines[i]) lv_obj_set_style_bg_opa(g_rearLines[i], LV_OPA_20, 0);
    }
  }

  // Wheel tread animation (when moving)
  if (live && !isnan(t.speed) && t.speed > 5) {
    uint32_t now = millis();
    if (now - g_lastTreadUpdate >= 50) {  // 20 Hz
      g_lastTreadUpdate = now;
      float speedFactor = t.speed / 200.0f;  // normalize
      if (speedFactor > 1.0f) speedFactor = 1.0f;
      for (int i = 0; i < 4; i++) {
        g_treadPhase[i] += speedFactor * 0.5f;
        if (g_treadPhase[i] >= 10.0f) g_treadPhase[i] -= 10.0f;  // wrap
        // Move tread container vertically to animate
        lv_obj_set_pos(g_wheelTread[i], 0, (int)(g_treadPhase[i] * 10.0f));
      }
    }
  }
}

void setScheme(theme::Scheme s, bool persist = true) {
  theme::setScheme(s);
  if (persist) settings::setScheme((uint8_t)theme::scheme());
  applyScheme();
}

void setVisible(bool visible) {
  if (!g_ready) return;
  if (visible) lv_obj_remove_flag(g_root, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(g_root, LV_OBJ_FLAG_HIDDEN);
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

}  // namespace drive_ui