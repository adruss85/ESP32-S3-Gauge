// =====================================================================
// drive_ui.cpp - drivetrain visualisation face
//
// Adapted from the HTML prototype for 466x466 round display.
// Shows: chassis, front/rear drive lines, engine RPM, diffs, Haldex pack
// with fill (actual) and request line, four wheels with speed + slip %,
// ABS/TC intervention chips.
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

// Layout coordinates (matching HTML prototype)
constexpr int kEngineBoxX = 178, kEngineBoxY = 30, kEngineBoxW = 110, kEngineBoxH = 46;
constexpr int kFrontDiffCX = 233, kFrontDiffCY = 108, kFrontDiffR = 9;
constexpr int kRearDiffCX = 233, kRearDiffCY = 358, kRearDiffR = 11;
constexpr int kHaldexBoxX = 199, kHaldexBoxY = 208, kHaldexBoxW = 68, kHaldexBoxH = 50;
constexpr int kHaldexFillX = 204, kHaldexFillY = 208, kHaldexFillW = 58, kHaldexFillHMax = 40;
constexpr int kHaldexReqLineY = 253;  // baseline, moves with req

// Wheel positions (center of each wheel rect)
constexpr int kWheelW = 30, kWheelH = 60;
struct WheelPos { int x, y; };
const WheelPos kWheelPos[4] = {
    {130, 78},   // FL
    {306, 78},   // FR
    {130, 328},  // RL
    {306, 328},  // RR
};

// Wheel speed text positions
constexpr int kWheelSpeedX[4] = {120, 346, 120, 346};
constexpr int kWheelSpeedY[4] = {108, 108, 358, 358};
constexpr int kWheelSlipY[4] = {126, 126, 376, 376};
constexpr int kWheelSpeedAnchor[4] = {LV_TEXT_ALIGN_RIGHT, LV_TEXT_ALIGN_LEFT,
                                       LV_TEXT_ALIGN_RIGHT, LV_TEXT_ALIGN_LEFT};

// ABS/TC chips
constexpr int kChipW = 68, kChipH = 26;
constexpr int kAbsChipX = 161, kAbsChipY = 408;
constexpr int kTcChipX = 237, kTcChipY = 408;

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
lv_obj_t *g_haldexReqLabel = nullptr;
lv_obj_t *g_haldexActLabel = nullptr;
lv_obj_t *g_wheelRect[4] = {nullptr};
lv_obj_t *g_wheelRing[4] = {nullptr};
lv_obj_t *g_wheelTread[4] = {nullptr};  // container for tread animation
lv_obj_t *g_wheelSpeedLabel[4] = {nullptr};
lv_obj_t *g_wheelSlipLabel[4] = {nullptr};
lv_obj_t *g_absChip = nullptr;
lv_obj_t *g_absLabel = nullptr;
lv_obj_t *g_tcChip = nullptr;
lv_obj_t *g_tcLabel = nullptr;
lv_obj_t *g_liveLabel = nullptr;

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
  const lv_color_t accent = theme::accent();
  // Could tint some elements with accent if desired
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
  if (g_haldexReqLabel) lv_obj_set_style_text_opa(g_haldexReqLabel, g_masterOpa, 0);
  if (g_haldexActLabel) lv_obj_set_style_text_opa(g_haldexActLabel, g_masterOpa, 0);
  for (int i = 0; i < 4; i++) {
    if (g_wheelRect[i]) lv_obj_set_style_opa(g_wheelRect[i], g_masterOpa, 0);
    if (g_wheelRing[i]) lv_obj_set_style_opa(g_wheelRing[i], g_masterOpa, 0);
    if (g_wheelTread[i]) lv_obj_set_style_opa(g_wheelTread[i], g_masterOpa, 0);
    if (g_wheelSpeedLabel[i]) lv_obj_set_style_text_opa(g_wheelSpeedLabel[i], g_masterOpa, 0);
    if (g_wheelSlipLabel[i]) lv_obj_set_style_text_opa(g_wheelSlipLabel[i], dim, 0);
  }
  if (g_absChip) lv_obj_set_style_opa(g_absChip, g_masterOpa, 0);
  if (g_absLabel) lv_obj_set_style_text_opa(g_absLabel, g_masterOpa, 0);
  if (g_tcChip) lv_obj_set_style_opa(g_tcChip, g_masterOpa, 0);
  if (g_tcLabel) lv_obj_set_style_text_opa(g_tcLabel, g_masterOpa, 0);
  if (g_liveLabel) lv_obj_set_style_text_opa(g_liveLabel, g_masterOpa, 0);
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

  // Use thin rectangles as lines (LV_USE_LINE is 0)
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

  // Request line (horizontal) - use thin rectangle
  g_haldexReqLine = lv_obj_create(g_haldexBox);
  lv_obj_set_size(g_haldexReqLine, kHaldexBoxW + 4, 2);
  lv_obj_set_pos(g_haldexReqLine, -2, kHaldexReqLineY - kHaldexBoxY);
  lv_obj_set_style_bg_color(g_haldexReqLine, col_fg(), 0);
  lv_obj_set_style_bg_opa(g_haldexReqLine, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_haldexReqLine, 0, 0);
  plain(g_haldexReqLine);

  // HALDEX label
  lv_obj_t *hLabel = lv_label_create(g_haldexBox);
  lv_label_set_text(hLabel, "HALDEX");
  lv_obj_set_style_text_font(hLabel, FONT_SMALL, 0);
  lv_obj_set_style_text_color(hLabel, col_dim(), 0);
  lv_obj_set_style_text_letter_space(hLabel, 3, 0);
  lv_obj_align(hLabel, LV_ALIGN_TOP_MID, 0, -8);

  // REQ label
  lv_obj_t *reqLabel = lv_label_create(g_haldexBox);
  lv_label_set_text(reqLabel, "REQ");
  lv_obj_set_style_text_font(reqLabel, FONT_SMALL, 0);
  lv_obj_set_style_text_color(reqLabel, col_dim(), 0);
  lv_obj_set_style_text_letter_space(reqLabel, 2, 0);
  lv_obj_align(reqLabel, LV_ALIGN_TOP_LEFT, -8, 20);

  // ACT label
  lv_obj_t *actLabel = lv_label_create(g_haldexBox);
  lv_label_set_text(actLabel, "ACT");
  lv_obj_set_style_text_font(actLabel, FONT_SMALL, 0);
  lv_obj_set_style_text_color(actLabel, col_dim(), 0);
  lv_obj_set_style_text_letter_space(actLabel, 2, 0);
  lv_obj_align(actLabel, LV_ALIGN_TOP_RIGHT, 8, 20);

  // REQ value
  g_haldexReqLabel = lv_label_create(g_haldexBox);
  lv_label_set_text(g_haldexReqLabel, "0%");
  lv_obj_set_style_text_font(g_haldexReqLabel, FONT_LABEL, 0);  // 26px
  lv_obj_set_style_text_color(g_haldexReqLabel, col_fg(), 0);
  lv_obj_align(g_haldexReqLabel, LV_ALIGN_TOP_LEFT, -8, 42);

  // ACT value
  g_haldexActLabel = lv_label_create(g_haldexBox);
  lv_label_set_text(g_haldexActLabel, "0%");
  lv_obj_set_style_text_font(g_haldexActLabel, FONT_LABEL, 0);
  lv_obj_set_style_text_color(g_haldexActLabel, col_cyan(), 0);
  lv_obj_align(g_haldexActLabel, LV_ALIGN_TOP_RIGHT, 8, 42);
}

void addWheels(lv_obj_t *scr) {
  const char *wheelNames[4] = {"FL", "FR", "RL", "RR"};
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

    // Tread rows (8 rows)
    for (int row = -1; row < 8; row++) {
      lv_obj_t *tread = lv_obj_create(g_wheelTread[i]);
      lv_obj_set_size(tread, 24, 3);
      lv_obj_set_pos(tread, 3, y + row * 10);  // 10px gap
      lv_obj_set_style_radius(tread, 1, 0);
      lv_obj_set_style_bg_color(tread, col_tread(), 0);
      plain(tread);
    }

    // Ring (slip indicator)
    g_wheelRing[i] = lv_obj_create(scr);
    lv_obj_set_size(g_wheelRing[i], kWheelW, kWheelH);
    lv_obj_set_pos(g_wheelRing[i], x, y);
    lv_obj_set_style_radius(g_wheelRing[i], 6, 0);
    lv_obj_set_style_bg_opa(g_wheelRing[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_wheelRing[i], 3, 0);
    lv_obj_set_style_border_color(g_wheelRing[i], col_ring(), 0);
    plain(g_wheelRing[i]);

    // Speed label
    g_wheelSpeedLabel[i] = lv_label_create(scr);
    lv_label_set_text(g_wheelSpeedLabel[i], "0");
    lv_obj_set_style_text_font(g_wheelSpeedLabel[i], FONT_LABEL, 0);  // 26px
    lv_obj_set_style_text_color(g_wheelSpeedLabel[i], col_fg(), 0);
    lv_obj_set_style_text_align(g_wheelSpeedLabel[i], (lv_text_align_t)kWheelSpeedAnchor[i], 0);
    lv_obj_set_pos(g_wheelSpeedLabel[i], kWheelSpeedX[i] - (kWheelSpeedAnchor[i] == LV_TEXT_ALIGN_RIGHT ? 0 : 0), kWheelSpeedY[i]);
    if (kWheelSpeedAnchor[i] == LV_TEXT_ALIGN_RIGHT) {
      lv_obj_align(g_wheelSpeedLabel[i], LV_ALIGN_TOP_RIGHT, -kWheelSpeedX[i] + (kW - 10), kWheelSpeedY[i]);
    } else {
      lv_obj_set_pos(g_wheelSpeedLabel[i], kWheelSpeedX[i], kWheelSpeedY[i]);
    }

    // Slip label
    g_wheelSlipLabel[i] = lv_label_create(scr);
    lv_label_set_text(g_wheelSlipLabel[i], "km/h");
    lv_obj_set_style_text_font(g_wheelSlipLabel[i], FONT_TINY, 0);  // 16px
    lv_obj_set_style_text_color(g_wheelSlipLabel[i], col_dim(), 0);
    lv_obj_set_style_text_align(g_wheelSlipLabel[i], (lv_text_align_t)kWheelSpeedAnchor[i], 0);
    if (kWheelSpeedAnchor[i] == LV_TEXT_ALIGN_RIGHT) {
      lv_obj_align(g_wheelSlipLabel[i], LV_ALIGN_TOP_RIGHT, -kWheelSpeedX[i] + (kW - 10), kWheelSlipY[i]);
    } else {
      lv_obj_set_pos(g_wheelSlipLabel[i], kWheelSpeedX[i], kWheelSlipY[i]);
    }
  }
}

void addChips(lv_obj_t *scr) {
  // ABS chip
  g_absChip = lv_obj_create(scr);
  lv_obj_set_size(g_absChip, kChipW, kChipH);
  lv_obj_set_pos(g_absChip, kAbsChipX, kAbsChipY);
  lv_obj_set_style_radius(g_absChip, 6, 0);
  lv_obj_set_style_bg_color(g_absChip, col_chipBg(), 0);
  plain(g_absChip);

  g_absLabel = lv_label_create(g_absChip);
  lv_label_set_text(g_absLabel, "ABS");
  lv_obj_set_style_text_font(g_absLabel, FONT_TINY, 0);  // 16px
  lv_obj_set_style_text_color(g_absLabel, col_dim(), 0);
  lv_obj_center(g_absLabel);

  // TC chip
  g_tcChip = lv_obj_create(scr);
  lv_obj_set_size(g_tcChip, kChipW, kChipH);
  lv_obj_set_pos(g_tcChip, kTcChipX, kTcChipY);
  lv_obj_set_style_radius(g_tcChip, 6, 0);
  lv_obj_set_style_bg_color(g_tcChip, col_chipBg(), 0);
  plain(g_tcChip);

  g_tcLabel = lv_label_create(g_tcChip);
  lv_label_set_text(g_tcLabel, "TC");
  lv_obj_set_style_text_font(g_tcLabel, FONT_TINY, 0);
  lv_obj_set_style_text_color(g_tcLabel, col_dim(), 0);
  lv_obj_center(g_tcLabel);

  // LIVE label
  g_liveLabel = lv_label_create(scr);
  lv_label_set_text(g_liveLabel, "DEMO");
  lv_obj_set_style_text_font(g_liveLabel, FONT_SMALL, 0);
  lv_obj_set_style_text_color(g_liveLabel, col_dim(), 0);
  lv_obj_set_style_text_letter_space(g_liveLabel, 3, 0);
  lv_obj_align(g_liveLabel, LV_ALIGN_BOTTOM_MID, 0, -14);
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
  addChips(g_root);

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

  // Haldex actual fill (0-100% -> 0-40px height)
  if (g_haldexFill && !isnan(t.speed)) {
    float actPct = 0;
    if (t.statusValid) {
      // lockActual is 0-100 in OHX_STATUS
      actPct = t.haldexState & 0xFF;  // placeholder - need actual field
      // Actually lockActual is separate field in OhxStatus
    }
    // For now use a smoothed value from telemetry if available
    // We'll add lockActual to Telemetry next
  }

  // Wheel speeds and slip
  if (t.slipValid) {
    // Reference speed = average of 4 wheels (or vehicle speed)
    float ref = t.speed;
    if (isnan(ref)) ref = 0;

    for (int i = 0; i < 4; i++) {
      int8_t slip = t.slip[i];
      bool hasSlip = (slip != OHX_NONE8);

      // Speed label - we don't have individual wheel speeds yet
      // For now show vehicle speed or --
      if (g_wheelSpeedLabel[i]) {
        if (stale || isnan(ref)) {
          lv_label_set_text(g_wheelSpeedLabel[i], "--");
          lv_obj_set_style_text_color(g_wheelSpeedLabel[i], lv_color_hex(0x555555), 0);
        } else {
          char buf[16];
          snprintf(buf, sizeof(buf), "%d", (int)ref);
          lv_label_set_text(g_wheelSpeedLabel[i], buf);
          lv_obj_set_style_text_color(g_wheelSpeedLabel[i], col_fg(), 0);
        }
      }

      // Slip label and ring color
      if (g_wheelSlipLabel[i] && g_wheelRing[i]) {
        if (!hasSlip || stale) {
          lv_label_set_text(g_wheelSlipLabel[i], "km/h");
          lv_obj_set_style_text_color(g_wheelSlipLabel[i], col_dim(), 0);
          lv_obj_set_style_border_color(g_wheelRing[i], col_ring(), 0);
        } else {
          char buf[16];
          snprintf(buf, sizeof(buf), "%s%d%%", slip >= 0 ? "+" : "", slip);
          lv_label_set_text(g_wheelSlipLabel[i], buf);
          // Color based on slip: negative (ABS) = amber, positive (TC) = red
          lv_color_t slipColor = slip < 0 ? col_amber() : col_red();
          lv_obj_set_style_text_color(g_wheelSlipLabel[i], slipColor, 0);
          lv_obj_set_style_border_color(g_wheelRing[i], slipColor, 0);
        }
      }
    }
  } else {
    // No slip data
    for (int i = 0; i < 4; i++) {
      if (g_wheelSpeedLabel[i]) {
        lv_label_set_text(g_wheelSpeedLabel[i], "--");
        lv_obj_set_style_text_color(g_wheelSpeedLabel[i], lv_color_hex(0x555555), 0);
      }
      if (g_wheelSlipLabel[i]) {
        lv_label_set_text(g_wheelSlipLabel[i], "km/h");
        lv_obj_set_style_text_color(g_wheelSlipLabel[i], col_dim(), 0);
      }
      if (g_wheelRing[i]) {
        lv_obj_set_style_border_color(g_wheelRing[i], col_ring(), 0);
      }
    }
  }

  // ABS/TC chips (infer from slip for now)
  bool absActive = false, tcActive = false;
  if (t.slipValid && !stale) {
    for (int i = 0; i < 4; i++) {
      if (t.slip[i] != OHX_NONE8) {
        if (t.slip[i] < -4) absActive = true;  // threshold
        if (t.slip[i] > 4) tcActive = true;
      }
    }
  }

  static uint32_t pulseToggle = 0;
  pulseToggle = (pulseToggle + 1) % 2;

  if (g_absChip && g_absLabel) {
    if (absActive && live) {
      lv_obj_set_style_bg_color(g_absChip, pulseToggle ? col_amber() : lv_color_hex(0x7A5206), 0);
      lv_obj_set_style_text_color(g_absLabel, lv_color_black(), 0);
    } else {
      lv_obj_set_style_bg_color(g_absChip, col_chipBg(), 0);
      lv_obj_set_style_text_color(g_absLabel, col_dim(), 0);
    }
  }
  if (g_tcChip && g_tcLabel) {
    if (tcActive && live) {
      lv_obj_set_style_bg_color(g_tcChip, pulseToggle ? col_red() : lv_color_hex(0x7A2020), 0);
      lv_obj_set_style_text_color(g_tcLabel, lv_color_white(), 0);
    } else {
      lv_obj_set_style_bg_color(g_tcChip, col_chipBg(), 0);
      lv_obj_set_style_text_color(g_tcLabel, col_dim(), 0);
    }
  }

  // LIVE label
  if (g_liveLabel) {
    lv_label_set_text(g_liveLabel, live ? "LIVE" : "DEMO");
  }
}

void setScheme(theme::Scheme s, bool persist) {
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