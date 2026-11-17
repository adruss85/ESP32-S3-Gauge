// =====================================================================
// gmeter_ui.cpp - one canvas for the dot, tail and g readout
//
// The first versions used an LVGL object for the blob, one for each of the
// 20 tail dots, and a label for the g readout. That produced a separate
// flush area per object. The panel showed intermittent artifacts inside
// exactly those boxes even with TE sync, a whole-band redraw and one draw
// buffer. The fix is structural: draw everything into one RGB565 canvas and
// invalidate only that canvas. LVGL then flushes one object in top-to-bottom
// tiles, with no overlapping per-object areas.
//
// The canvas starts at y=60 and is 360x300. The top 60 px of the glass are
// the black screen; the dot/tail and the readout at the bottom are inside.
// The 216,000 B buffer is in PSRAM and allocated once.
// =====================================================================
#include "ui/gmeter_ui.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <esp_heap_caps.h>

#include "theme.h"

namespace gmeter_ui {
namespace {

constexpr int kScreen = 360;
constexpr int kCanvasTop = 60;
constexpr int kCanvasH = 300;
constexpr int kBlobSize = 50;
constexpr int kBlobHalf = kBlobSize / 2;
constexpr int kBaseX = 180;
constexpr int kBaseY = 180;
constexpr int kTravelPx = 72;
constexpr float kFullScaleG = 1.2f;
constexpr size_t kTailCount = 20;
constexpr uint32_t kTailStepMs = 50;

constexpr uint8_t kTailAgeOpa[kTailCount] = {
    150, 136, 123, 110, 98, 87, 76, 66, 57, 48,
    40,  33,  27,  21,  16, 12, 9,  6,  4,  2,
};

lv_obj_t *g_canvas = nullptr;
uint8_t *g_canvasBuf = nullptr;

uint8_t g_masterOpa = 255;
bool g_visible = false;
bool g_forceRedraw = true;
bool g_lastValid = false;
bool g_tailVisible = false;
bool g_primeHistory = false;
int g_lastX = kBaseX;
int g_lastY = kBaseY;
int16_t g_histX[kTailCount] = {};
int16_t g_histY[kTailCount] = {};
uint32_t g_lastStepMs = 0;
char g_lastText[12] = "";

int tailSize(size_t i) {
  // 24 px at the newest end down to 6 px at the oldest.
  return 24 - (int)((18 * i) / (kTailCount - 1));
}

void drawCanvas(int x, int y, bool valid, bool tail, const char *text) {
  if (g_canvas == nullptr) return;

  lv_canvas_fill_bg(g_canvas, COL_BG, LV_OPA_COVER);

  lv_layer_t layer;
  lv_canvas_init_layer(g_canvas, &layer);

  const int cy = y - kCanvasTop;

  if (valid && tail) {
    for (size_t i = 0; i < kTailCount; i++) {
      const int size = tailSize(i);
      lv_draw_rect_dsc_t d;
      lv_draw_rect_dsc_init(&d);
      d.radius = LV_RADIUS_CIRCLE;
      d.bg_color = theme::accent();
      d.bg_opa = kTailAgeOpa[i];
      lv_area_t a = {g_histX[i] - size / 2, g_histY[i] - kCanvasTop - size / 2,
                     g_histX[i] + size / 2, g_histY[i] - kCanvasTop + size / 2};
      lv_draw_rect(&layer, &d, &a);
    }
  }

  if (valid) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = theme::accent();
    d.bg_opa = LV_OPA_COVER;
    lv_area_t a = {x - kBlobHalf, cy - kBlobHalf, x + kBlobHalf,
                   cy + kBlobHalf};
    lv_draw_rect(&layer, &d, &a);
  }

  const int lineH = lv_font_get_line_height(FONT_G);
  const int bottom = kCanvasH - 12;
  lv_area_t textArea = {0, bottom - lineH, kScreen - 1, bottom};
  lv_draw_label_dsc_t ld;
  lv_draw_label_dsc_init(&ld);
  ld.text = text;
  ld.font = FONT_G;
  ld.color = COL_FG;
  ld.opa = LV_OPA_COVER;
  ld.align = LV_TEXT_ALIGN_CENTER;
  lv_draw_label(&layer, &ld, &textArea);

  lv_canvas_finish_layer(g_canvas, &layer);
  lv_obj_invalidate(g_canvas);
}

}  // namespace

void create() {
  g_canvasBuf = (uint8_t *)heap_caps_aligned_alloc(
      LV_DRAW_BUF_ALIGN, (size_t)kScreen * kCanvasH * 2,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (g_canvasBuf == nullptr) {
    Serial.println("[ui] no PSRAM for the G-meter canvas");
    return;
  }

  g_canvas = lv_canvas_create(lv_screen_active());
  lv_obj_remove_style_all(g_canvas);
  lv_obj_remove_flag(g_canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(g_canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(g_canvas, 0, kCanvasTop);
  lv_obj_set_size(g_canvas, kScreen, kCanvasH);
  lv_canvas_set_buffer(g_canvas, g_canvasBuf, kScreen, kCanvasH,
                       LV_COLOR_FORMAT_RGB565);
  lv_obj_set_style_image_opa(g_canvas, 255, 0);
  lv_canvas_fill_bg(g_canvas, COL_BG, LV_OPA_COVER);
  lv_obj_add_flag(g_canvas, LV_OBJ_FLAG_HIDDEN);
  g_visible = false;
}

void setVisible(bool visible) {
  if (g_canvas == nullptr) return;
  if (visible) {
    g_forceRedraw = true;
    lv_obj_remove_flag(g_canvas, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_canvas, LV_OBJ_FLAG_HIDDEN);
  }
  g_visible = visible;
}

void setOpacity(uint8_t opacity) {
  if (g_canvas == nullptr || opacity == g_masterOpa) return;
  g_masterOpa = opacity;
  lv_obj_set_style_image_opa(g_canvas, opacity, 0);
}

void applyScheme() {
  // The colour is read from the theme on the next canvas draw. Draw it now
  // too, so a capture or a quick BOOT press does not show the previous
  // scheme for one 30 Hz tick.
  g_forceRedraw = false;
  const char *text = g_lastText[0] ? g_lastText : "-- g";
  drawCanvas(g_lastX, g_lastY, g_lastValid, g_tailVisible, text);
}

void update(const gmeter::Reading &r) {
  if (g_canvas == nullptr) return;

  if (!r.valid) {
    if (g_lastValid || g_forceRedraw || strcmp(g_lastText, "-- g") != 0) {
      g_lastValid = false;
      g_forceRedraw = false;
      g_tailVisible = false;
      g_primeHistory = true;
      g_lastX = kBaseX;
      g_lastY = kBaseY;
      g_lastStepMs = millis();
      strcpy(g_lastText, "-- g");
      drawCanvas(kBaseX, kBaseY, false, false, "-- g");
    }
    return;
  }

  float v = r.vertical;
  if (v > kFullScaleG) v = kFullScaleG;
  if (v < -kFullScaleG) v = -kFullScaleG;
  float l = r.lateral;
  if (l > kFullScaleG) l = kFullScaleG;
  if (l < -kFullScaleG) l = -kFullScaleG;

  int y = kBaseY - (int)lroundf(v * (float)kTravelPx / kFullScaleG);
  int x = kBaseX + (int)lroundf(l * (float)kTravelPx / kFullScaleG);
  if (y < kBlobHalf) y = kBlobHalf;
  if (y > kScreen - kBlobHalf) y = kScreen - kBlobHalf;
  if (x < kBlobHalf) x = kBlobHalf;
  if (x > kScreen - kBlobHalf) x = kScreen - kBlobHalf;

  bool changed = g_forceRedraw || !g_lastValid;
  if (x != g_lastX || y != g_lastY) {
    g_lastX = x;
    g_lastY = y;
    changed = true;
  }

  if (g_primeHistory) {
    for (size_t i = 0; i < kTailCount; i++) {
      g_histX[i] = (int16_t)x;
      g_histY[i] = (int16_t)y;
    }
    g_primeHistory = false;
    g_lastStepMs = millis();
  }

  const uint32_t now = millis();
  const uint32_t elapsed = now - g_lastStepMs;
  if (elapsed >= kTailStepMs) {
    g_lastStepMs += kTailStepMs;
    if ((uint32_t)(now - g_lastStepMs) >= 2 * kTailStepMs) g_lastStepMs = now;

    for (size_t i = kTailCount - 1; i > 0; i--) {
      g_histX[i] = g_histX[i - 1];
      g_histY[i] = g_histY[i - 1];
    }
    g_histX[0] = (int16_t)x;
    g_histY[0] = (int16_t)y;

    int loX = g_histX[0], hiX = g_histX[0];
    int loY = g_histY[0], hiY = g_histY[0];
    for (size_t i = 1; i < kTailCount; i++) {
      if (g_histX[i] < loX) loX = g_histX[i];
      if (g_histX[i] > hiX) hiX = g_histX[i];
      if (g_histY[i] < loY) loY = g_histY[i];
      if (g_histY[i] > hiY) hiY = g_histY[i];
    }
    g_tailVisible = !(hiX - loX < 3 && hiY - loY < 3);
    changed = true;
  }

  char text[12];
  snprintf(text, sizeof(text), "%.2f g", (double)r.average);
  if (strcmp(text, g_lastText) != 0) changed = true;

  if (!changed) return;

  g_lastValid = true;
  g_forceRedraw = false;
  strcpy(g_lastText, text);
  drawCanvas(x, y, true, g_tailVisible, text);
}

}  // namespace gmeter_ui
