#include <Arduino.h>

#include "app/config.h"

#ifndef GAUGE_HAS_DISPLAY
#define GAUGE_HAS_DISPLAY 0
#endif

#ifndef GAUGE_CAPTURE
#define GAUGE_CAPTURE 0
#endif

#if GAUGE_HAS_DISPLAY
#include <lvgl.h>

#include "hal/display.h"
#include "ui/gauge_ui.h"
#endif

#include "net/ohx_now.h"

#if GAUGE_CAPTURE && GAUGE_HAS_DISPLAY
// Verification aid: render the gauge with a fixed reading and stream the
// 360x360 RGB565 framebuffer over the USB CDC port as raw bytes after a
// one-line header. Never compiled into a normal build.
namespace {

bool g_captured = false;

void dumpSnapshot() {
  lv_draw_buf_t *buf =
      lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
  if (buf == nullptr) {
    Serial.println("[shot] snapshot failed");
    return;
  }

  const uint32_t w = buf->header.w;
  const uint32_t h = buf->header.h;
  const uint32_t stride = buf->header.stride;
  Serial.printf("[shot] %lu %lu %lu %lu\n", (unsigned long)w, (unsigned long)h,
                (unsigned long)stride, (unsigned long)buf->data_size);
  Serial.flush();

  const uint8_t *p = buf->data;
  for (uint32_t y = 0; y < h; y++) {
    Serial.write(p + (size_t)y * stride, w * 2);
  }
  Serial.flush();
  lv_draw_buf_destroy(buf);
}

}  // namespace
#endif

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.printf("ESP32-S3-Gauge %s\n", config::kVersion);
  Serial.println("[gauge] boost from OHX_STATUS, IAT from Engine_3 (0x380)");

#if GAUGE_HAS_DISPLAY
  if (!display::begin()) {
    Serial.println("[lcd] display init failed - continuing headless");
  } else {
    gauge_ui::create();
  }
#endif

  if (!ohxnow::begin()) {
    Serial.println("[now] begin failed - poll() will retry");
  }
}

void loop() {
  ohxnow::poll();

#if GAUGE_HAS_DISPLAY
  display::loop();

  static uint32_t lastUi = 0;
  const uint32_t now = millis();
  if ((uint32_t)(now - lastUi) >= 50) {
    lastUi = now;
    ohxnow::Telemetry t = ohxnow::telemetry();
#if GAUGE_CAPTURE
    // Fixed values so the snapshot always exercises the full value row.
    t.statusValid = true;
    t.boostPsi = 18.4f;
    t.iatValid = true;
    t.iatC = 42.0f;
#endif
    gauge_ui::update(t);
  }

#if GAUGE_CAPTURE
  if (!g_captured && millis() > 6000) {
    g_captured = true;
    dumpSnapshot();
  }
#endif
#endif

  delay(5);
}
