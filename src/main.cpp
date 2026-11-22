#include <Arduino.h>

#include "app/config.h"
#include "app/gmeter.h"
#include "app/settings.h"

#ifndef GAUGE_HAS_DISPLAY
#define GAUGE_HAS_DISPLAY 0
#endif

#ifndef GAUGE_CAPTURE
#define GAUGE_CAPTURE 0
#endif

// Capture the G-meter instead of the boost gauge. The synthetic reading
// below moves the blob so the tail and the readout are exercised too.
#ifndef GAUGE_CAPTURE_GMETER
#define GAUGE_CAPTURE_GMETER 0
#endif

// Capture the Haldex face instead of the boost gauge: a fixed mode and a
// fixed lock request/actual, so the whole screen is exercised in one shot.
// A capture build still cannot send anything - a tap is what transmits, not a
// render, and nothing here calls onTap().
#ifndef GAUGE_CAPTURE_DRIVE
#define GAUGE_CAPTURE_DRIVE 0
#endif

// Capture the boot splash (at 1.5 s, half-filled) instead of the gauge.
#ifndef GAUGE_CAPTURE_SPLASH
#define GAUGE_CAPTURE_SPLASH 0
#endif

// The capture build's fixed reading. Override it to photograph a vacuum
// value or a custom scale:
//   PLATFORMIO_BUILD_FLAGS="-DGAUGE_CAPTURE=1 -DGAUGE_CAPTURE_BOOST=-2.5f"
#ifndef GAUGE_CAPTURE_BOOST
#define GAUGE_CAPTURE_BOOST 18.4f
#endif

#if GAUGE_HAS_DISPLAY
#include <lvgl.h>

#include "board_config.h"
#include "hal/boot_button.h"
#include "hal/display.h"
#include "hal/imu.h"
#include "hal/touch.h"
#include "theme.h"
#include "ui/dashboard.h"
#include "ui/drive_ui.h"
#include "ui/gauge_ui.h"
#include "ui/splash.h"
#endif

#include "net/gauge_web.h"
#include "net/ohx_now.h"

#if GAUGE_CAPTURE && GAUGE_HAS_DISPLAY
// Verification aid: render the gauge with a fixed reading, once per colour
// scheme, and stream each 466x466 RGB565 framebuffer over USB as raw bytes
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
  // 400 ms) can hold loop() for seconds: one press was measured at 6.9 s
  // from edge to handler. Drop console output instead of blocking.
  // The capture build keeps the blocking timeout so framebuffer dumps are
  // not shortened.
  Serial.setTxTimeoutMs(0);
#endif
  delay(250);
  Serial.println();
  Serial.printf("ESP32-S3-Gauge %s\n", config::kVersion);
  Serial.println("[gauge] boost from OHX_STATUS, IAT from Engine_3 (0x380)");

  // Before the display: the stored scheme, scale, mode and mount are what
  // create() and the first update() must draw.
  settings::begin();

#if GAUGE_HAS_DISPLAY
  if (!display::begin()) {
    Serial.println("[lcd] display init failed - continuing headless");
  } else {
    // display::begin() owns Wire(), so the IMU and the touch controller are
    // probed after it - they share that bus.
    imu::begin();
    touch::begin();
    dashboard::begin();
#if GAUGE_CAPTURE
    // The capture sweep photographs the boost gauge, never a stored screen.
    // The G-meter and Haldex builds force their own face.
    dashboard::setMode(GAUGE_CAPTURE_GMETER ? dashboard::Mode::GMeter
                       : GAUGE_CAPTURE_DRIVE ? dashboard::Mode::Drive
                                             : dashboard::Mode::Gauge,
                       false, "capture");
#endif
    // After the dashboard roots, so the splash is the topmost object. It
    // covers the JPEG decode and the first WiFi/ESP-NOW setup.
    splash::begin();
    boot_button::begin();
    Serial.printf("[ui] accent %s (BOOT: short = scheme, long = screen; touch: hold = scheme, tap = haldex mode, swipe = screen)\n",
                  theme::schemeName());
  }
#endif

  if (!ohxnow::begin()) {
    Serial.println("[now] begin failed - poll() will retry");
  }

#if GAUGE_HAS_DISPLAY
  // After ohxnow::begin(): the AP has to start on the receiver's channel.
  // The headless listener keeps its radio free of the AP and page.
  gauge_web::begin();
#endif
}

void loop() {
#if GAUGE_HAS_DISPLAY
  // Console fallbacks for the same actions as the BOOT key.
  static bool touchDebug = false;
  if (Serial.available() > 0) {
    const int key = Serial.peek();
    if (key == 'b') {
      Serial.read();
      dashboard::nextScheme();
    } else if (key == 'm') {
      Serial.read();
      dashboard::toggleMode("console");
    } else if (key == 'h') {
      Serial.read();
      // The same function the touch tap calls, so a mode request and its
      // verdict can be exercised from the console with no finger on the glass.
      if (dashboard::mode() != dashboard::Mode::Drive) {
        dashboard::setMode(dashboard::Mode::Drive, false, "console");
      }
      drive_ui::onTap();
    } else if (key == 'z') {
      Serial.read();
      dashboard::zeroG();
    } else if (key == 't') {
      Serial.read();
      touchDebug = !touchDebug;
      Serial.printf("[touch] point log %s\n", touchDebug ? "on" : "off");
    } else if (key == 'g') {
      Serial.read();
      const gmeter::Reading &r = gmeter::reading();
      Serial.printf("[g] v=%+.2f lat=%+.2f avg=%.2f valid=%u\n",
                    (double)r.vertical, (double)r.lateral,
                    (double)r.average, r.valid ? 1u : 0u);
    }
  }

  // 't' logs the touch point after the board's mirror settings have been
  // applied, which is the check that mirror_x/mirror_y in board_config.h are
  // right: tap a known corner and read the coordinate.
  if (touchDebug && touch::ready()) {
    static uint32_t lastTouchLog = 0;
    const uint32_t nowTouch = millis();
    if ((uint32_t)(nowTouch - lastTouchLog) >= 200) {
      lastTouchLog = nowTouch;
      uint16_t tx = 0;
      uint16_t ty = 0;
      bool pressed = false;
      touch::lastPoint(tx, ty, pressed);
      if (pressed) {
        Serial.printf("[touch] mapped %u,%u\n", (unsigned)tx, (unsigned)ty);
      }
    }
  }

  // 'a' toggles a slow raw-IMU dump, useful when checking which chip axis
  // points along the screen and which way gravity falls on a mount.
  static bool imuDumpOn = false;
  if (Serial.available() > 0 && Serial.peek() == 'a') {
    Serial.read();
    imuDumpOn = !imuDumpOn;
    Serial.printf("[imu] dump %s\n", imuDumpOn ? "on" : "off");
  }
  if (imuDumpOn) {
    static uint32_t lastImuDump = 0;
    const uint32_t nowDump = millis();
    if ((uint32_t)(nowDump - lastImuDump) >= 500) {
      lastImuDump = nowDump;
      imu::Sample s;
      if (imu::read(s)) {
        Serial.printf("[imu] a=(%+.3f %+.3f %+.3f) g  w=(%+.2f %+.2f %+.2f) rad/s\n",
                      (double)s.ax, (double)s.ay, (double)s.az,
                      (double)s.gx, (double)s.gy, (double)s.gz);
      } else {
        Serial.println("[imu] no sample");
      }
    }
  }
#endif

  ohxnow::poll();
#if GAUGE_HAS_DISPLAY
  gauge_web::poll();
  touch::poll();
#endif

#if GAUGE_HAS_DISPLAY
  // IMU sampling, G-meter and the mode fade are all independent of LVGL's
  // timer; run them before the render pump so a new blob position is
  // included in this pass.
  dashboard::poll();
  splash::update();
  display::loop();

  // The panel is kept dark through init and the splash JPEG decode. Raise
  // the AMOLED emission after the first frame has been queued, so boot noise
  // in GRAM is not shown.
  static bool backlightOn = false;
  if (!backlightOn && display::ready()) {
    backlightOn = true;
    display::setBacklight(LCD_BRIGHTNESS_DAY_PCT);
  }

  const boot_button::Event button = boot_button::poll();
  const bool cruise = ohxnow::takeSchemeEvent();
  const bool cruiseMode = ohxnow::takeModeEvent();
  if (cruise) Serial.println("[ui] cruise combo");
  if (cruiseMode) {
    Serial.println("[ui] cruise mode");
    dashboard::toggleMode("cruise");
  }
  if (button == boot_button::Event::Short || cruise) {
    dashboard::nextScheme();
  } else if (button == boot_button::Event::Long) {
    dashboard::toggleMode();
  }

  static uint32_t lastUi = 0;
  const uint32_t now = millis();
  if ((uint32_t)(now - lastUi) >= 50) {
    lastUi = now;
    ohxnow::Telemetry t = ohxnow::telemetry();
    ohxnow::Stats s = ohxnow::stats();
#if GAUGE_CAPTURE
    // Fixed values so every snapshot exercises the full value row.
    t.statusValid = true;
    t.boostPsi = GAUGE_CAPTURE_BOOST;
    t.iatValid = true;
    t.iatC = 42.0f;
    t.oilTempValid = true;
    t.oilTempC = 98.0f;
    s.raw = 1;
    s.statusPackets = 1;
#if GAUGE_CAPTURE_DRIVE
    // A mid-range request against a lower actual, which is the pair a driver
    // actually reads (and the case where the two must not look alike).
    t.mode = 3;  // 60:40
    t.lockTargetValid = true;
    t.lockTarget = 45;
    t.lockActualValid = true;
    t.lockActual = 38;
    t.haldexCan = true;
    t.ctrlAccept = true;
#endif
#endif
    display::applyDimmer(t.term58s);
    dashboard::update(t, s);
  }

#if GAUGE_CAPTURE
#if GAUGE_CAPTURE_SPLASH
  if (!g_captured && millis() > 2000) {
    g_captured = true;
    dumpSnapshot();
  }
#else
  if (!g_captured && millis() > 6000 &&
      (uint32_t)(now - g_lastShotMs) >= 400) {
    g_lastShotMs = now;
    dashboard::setScheme((theme::Scheme)g_shotIndex, false);
    dumpSnapshot();
    if (++g_shotIndex >= (uint8_t)theme::Scheme::Count) g_captured = true;
  }
#endif
#endif
#endif

  delay(5);
}
