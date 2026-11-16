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

#include "hal/boot_button.h"
#include "hal/display.h"
#include "theme.h"
#include "ui/gauge_ui.h"
#endif

#include "net/ohx_now.h"

#if GAUGE_CAPTURE && GAUGE_HAS_DISPLAY
// Verification aid: render the gauge with a fixed reading, once per colour
// scheme, and stream each 360x360 RGB565 framebuffer over USB as raw bytes
// after a one-line header. Never compiled into a normal build.
namespace {

uint8_t g_shotIndex = 0;
bool g_captured = false;
uint32_t g_lastShotMs = 0;

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
  Serial.printf("[shot] %u %lu %lu %lu %lu\n", (unsigned)g_shotIndex,
                (unsigned long)w, (unsigned long)h, (unsigned long)stride,
                (unsigned long)buf->data_size);
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
#if !GAUGE_CAPTURE && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  // USBCDC's 250 ms TX timeout is applied per write. With no host draining
  // the CDC port, the disconnected-state [scan] channel lines (one every
  // 400 ms) can hold loop() for seconds: a bench press was measured at
  // 6.9 s from ISR to handler. Drop console output instead of blocking.
  // The capture build keeps the blocking timeout so framebuffer dumps are
  // not shortened.
  Serial.setTxTimeoutMs(0);
#endif
  delay(250);
  Serial.println();
  Serial.printf("ESP32-S3-Gauge %s\n", config::kVersion);
  Serial.println("[gauge] boost from OHX_STATUS, IAT from Engine_3 (0x380)");

#if GAUGE_HAS_DISPLAY
  if (!display::begin()) {
    Serial.println("[lcd] display init failed - continuing headless");
  } else {
    gauge_ui::create();
    boot_button::begin();
    Serial.printf("[ui] accent %s (BOOT key cycles)\n", theme::schemeName());
  }
#endif

  if (!ohxnow::begin()) {
    Serial.println("[now] begin failed - poll() will retry");
  }
}

void loop() {
#if GAUGE_HAS_DISPLAY
  // Console fallback for the accent cycle, same action as the BOOT key.
  if (Serial.available() > 0 && Serial.peek() == 'b') {
    Serial.read();
    gauge_ui::nextScheme();
  }
#endif

  ohxnow::poll();

#if GAUGE_HAS_DISPLAY
  display::loop();

  const bool button = boot_button::pressed();
  const bool cruise = ohxnow::takeSchemeEvent();
  if (cruise) Serial.println("[ui] cruise combo");
  if (button || cruise) gauge_ui::nextScheme();

  static uint32_t lastUi = 0;
  const uint32_t now = millis();
  if ((uint32_t)(now - lastUi) >= 50) {
    lastUi = now;
    ohxnow::Telemetry t = ohxnow::telemetry();
    ohxnow::Stats s = ohxnow::stats();
#if GAUGE_CAPTURE
    // Fixed values so every snapshot exercises the full value row.
    t.statusValid = true;
    t.boostPsi = 18.4f;
    t.iatValid = true;
    t.iatC = 42.0f;
    s.raw = 1;
    s.statusPackets = 1;
#endif
    display::applyDimmer(t.term58s);
    gauge_ui::update(t, s);
  }

#if GAUGE_CAPTURE
  if (!g_captured && millis() > 6000 &&
      (uint32_t)(now - g_lastShotMs) >= 400) {
    g_lastShotMs = now;
    gauge_ui::setScheme((theme::Scheme)g_shotIndex);
    dumpSnapshot();
    if (++g_shotIndex >= (uint8_t)theme::Scheme::Count) g_captured = true;
  }
#endif
#endif

  delay(5);
}
