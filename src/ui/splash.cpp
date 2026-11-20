// =====================================================================
// splash.cpp - the boot screen: CANvas splash art plus a 3 s loading rule
//
// The artwork is a pre-rendered 466x466 JPEG (assets/canvas-gauge-splash-
// 466-base.jpg, the 360 px original upscaled by 466/360 when the gauge moved
// to the AMOLED panel). Its underline is the empty loading track; the green
// lv_bar sits exactly on it with a transparent track, so the fill advances
// over the artwork without repainting the picture. The 258x4 rule is at
// x=104, y=262, measured from the asset.
//
// The splash owns the glass for three seconds and is then deleted, not
// hidden: its widgets go back to the LVGL pool and its 434 KB RGB565 PSRAM
// picture is freed for the dashboard.
// =====================================================================
#include "ui/splash.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

#include "board_config.h"
#include "theme.h"
#include "ui/splash_img.h"
#include "version.h"

namespace splash {
namespace {

constexpr uint32_t kSplashMs = 3000;
constexpr int kBarX = 104;
constexpr int kBarY = 262;
constexpr int kBarW = 258;
constexpr int kBarH = 4;

lv_obj_t *g_root = nullptr;
lv_obj_t *g_bar = nullptr;
uint32_t g_startMs = 0;
int g_lastPct = -1;
bool g_active = false;

void buildFallback(lv_obj_t *root) {
  // The decode is rare to fail, but a black field with a green rule and the
  // version is better than a blank panel that looks dead.
  lv_obj_t *word = lv_label_create(root);
  lv_label_set_text(word, "CANvas");
  lv_obj_set_style_text_font(word, FONT_G, 0);
  lv_obj_set_style_text_color(word, COL_FG, 0);
  lv_obj_align(word, LV_ALIGN_CENTER, 0, -44);

  lv_obj_t *tag = lv_label_create(root);
  lv_label_set_text(tag, "gauge");
  lv_obj_set_style_text_font(tag, FONT_LABEL, 0);
  lv_obj_set_style_text_color(tag, COL_DIM, 0);
  lv_obj_set_style_text_letter_space(tag, 6, 0);
  lv_obj_align(tag, LV_ALIGN_CENTER, 0, 18);
}

}  // namespace

void begin() {
  if (g_active) return;

  lv_obj_t *scr = lv_screen_active();
  g_root = lv_obj_create(scr);
  lv_obj_set_size(g_root, LCD_H_RES, LCD_V_RES);
  lv_obj_set_pos(g_root, 0, 0);
  lv_obj_set_style_bg_color(g_root, COL_BG, 0);
  lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_root, 0, 0);
  lv_obj_set_style_pad_all(g_root, 0, 0);
  lv_obj_set_style_radius(g_root, 0, 0);
  lv_obj_remove_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(g_root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_foreground(g_root);

  if (splashimg::decode()) {
    lv_obj_t *pic = lv_image_create(g_root);
    lv_obj_remove_style_all(pic);
    lv_obj_remove_flag(pic, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(pic, splashimg::image());
    lv_obj_set_pos(pic, 0, 0);
  } else {
    buildFallback(g_root);
  }

  // The artwork's empty track is red; the green indicator covers it. The
  // bar's main part is transparent so the art shows through at 0%.
  g_bar = lv_bar_create(g_root);
  lv_obj_remove_style_all(g_bar);
  lv_obj_remove_flag(g_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(g_bar, kBarX, kBarY);
  lv_obj_set_size(g_bar, kBarW, kBarH);
  lv_bar_set_range(g_bar, 0, 100);
  lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_opa(g_bar, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(g_bar, lv_color_hex(0x22c55e), LV_PART_INDICATOR);
  lv_obj_set_style_radius(g_bar, 0, LV_PART_INDICATOR);
  lv_obj_move_foreground(g_bar);

  char vbuf[24];
  snprintf(vbuf, sizeof(vbuf), "v%s", OHX_GAUGE_VERSION);
  lv_obj_t *ver = lv_label_create(g_root);
  lv_label_set_text(ver, vbuf);
  lv_obj_set_style_text_font(ver, FONT_SMALL, 0);
  lv_obj_set_style_text_color(ver, COL_DIM, 0);
  lv_obj_align(ver, LV_ALIGN_BOTTOM_RIGHT, -16, -13);

  g_startMs = millis();
  g_lastPct = 0;
  g_active = true;
  Serial.println("[ui] splash up (3 s)");
}

void update() {
  if (!g_active) return;

  const uint32_t elapsed = millis() - g_startMs;
  if (elapsed >= kSplashMs) {
    g_active = false;
    g_bar = nullptr;
    if (g_root != nullptr) {
      lv_obj_delete(g_root);
      g_root = nullptr;
    }
    // The image widget is gone; the picture buffer is ours to free.
    splashimg::release();
    Serial.println("[ui] splash down");
    return;
  }

  const int pct = (int)((uint64_t)100 * elapsed / kSplashMs);
  if (pct != g_lastPct) {
    g_lastPct = pct;
    lv_bar_set_value(g_bar, pct, LV_ANIM_OFF);
  }
}

bool active() { return g_active; }

}  // namespace splash
