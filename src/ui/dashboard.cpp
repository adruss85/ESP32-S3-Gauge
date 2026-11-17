// =====================================================================
// dashboard.cpp - mode ownership, IMU sampling and the cross-fade
//
// Fade design: LVGL opacity on a container allocates a layer buffer sized
// to the container. A 360x360 RGB565 layer is 259 KB, which cannot fit the
// 128 KB pool, so each module scales the opacity of its own leaf objects
// instead. At 50 Hz the arc costs a few extra repaints for 260 ms; after
// that the hidden root stops drawing entirely.
// =====================================================================
#include "ui/dashboard.h"

#include <Arduino.h>
#include <math.h>

#include "app/settings.h"
#include "hal/imu.h"
#include "ui/gauge_ui.h"
#include "ui/gmeter_ui.h"
#include "ui/splash.h"

#ifndef GAUGE_CAPTURE_GMETER
#define GAUGE_CAPTURE_GMETER 0
#endif

namespace dashboard {
namespace {

constexpr uint32_t kSampleIntervalMs = 10;   // 100 Hz
constexpr uint32_t kGmUiIntervalMs = 33;     // 30 Hz blob
constexpr uint32_t kFadeMs = 260;
constexpr uint32_t kFadeStepMs = 16;         // about one display frame
constexpr uint32_t kImuTimeoutMs = 1000;

Mode g_mode = Mode::Gauge;
bool g_fading = false;
uint32_t g_fadeStartMs = 0;
uint32_t g_lastFadeStepMs = 0;
uint8_t g_gaugeOpa = 255;
uint8_t g_gmOpa = 0;
uint8_t g_fromGauge = 255;
uint8_t g_fromGm = 0;

uint32_t g_lastSampleMs = 0;
uint32_t g_lastGoodMs = 0;
bool g_imuOffline = false;
gmeter::Mount g_lastMount = gmeter::Mount::Upright;

const char *mountName(gmeter::Mount m) {
  return m == gmeter::Mount::Flat ? "flat" : "upright";
}

void applyOpacity(uint8_t gaugeOpa, uint8_t gmOpa) {
  g_gaugeOpa = gaugeOpa;
  g_gmOpa = gmOpa;
  gauge_ui::setOpacity(gaugeOpa);
  gmeter_ui::setOpacity(gmOpa);
}

void showOnly(Mode m) {
  const bool g = m == Mode::Gauge;
  gauge_ui::setVisible(g);
  gmeter_ui::setVisible(!g);
  applyOpacity(g ? 255 : 0, g ? 0 : 255);
}

}  // namespace

void begin() {
  const settings::Values &v = settings::get();
  g_mode = (Mode)v.mode;

  gauge_ui::create();
  gauge_ui::setScheme((theme::Scheme)v.scheme);
  gauge_ui::applyScale(v.min10, v.max10);

  gmeter_ui::create();
  gmeter::begin((gmeter::Mount)v.mount, v.gflip != 0);
  g_lastMount = gmeter::mount();
  gmeter_ui::applyScheme();

  showOnly(g_mode);
  g_lastSampleMs = millis();
  g_lastGoodMs = millis();

  const char *mountSetting = v.mount == 2   ? "auto"
                             : v.mount == 1 ? "flat"
                                           : "upright";
  Serial.printf("[ui] mode %s, mount %s, flip %u\n",
                g_mode == Mode::GMeter ? "g-meter" : "gauge", mountSetting,
                (unsigned)v.gflip);
}

Mode mode() { return g_mode; }

void setMode(Mode m, bool persist, const char *source) {
  if (m == g_mode && !g_fading) {
    if (persist) settings::setMode((uint8_t)m);
    return;
  }

  // Start from wherever the previous fade got to, so a quick second press
  // reverses smoothly instead of jumping back to full opacity.
  g_fromGauge = g_gaugeOpa;
  g_fromGm = g_gmOpa;
  g_fadeStartMs = millis();
  g_lastFadeStepMs = g_fadeStartMs - kFadeStepMs;  // first step immediately
  g_fading = true;
  g_mode = m;

  // Both roots must draw during the fade.
  gauge_ui::setVisible(true);
  gmeter_ui::setVisible(true);

  if (persist) settings::setMode((uint8_t)m);
  Serial.printf("[ui] mode %s (%s)\n",
                m == Mode::GMeter ? "g-meter" : "gauge", source);
}

void toggleMode(const char *source) {
  setMode(g_mode == Mode::Gauge ? Mode::GMeter : Mode::Gauge, true, source);
}

void nextScheme() {
  theme::nextScheme();
  settings::setScheme((uint8_t)theme::scheme());
  gauge_ui::setScheme(theme::scheme(), false);
  gmeter_ui::applyScheme();
  Serial.printf("[ui] accent %s\n", theme::schemeName());
}

void setScheme(theme::Scheme s, bool persist) {
  gauge_ui::setScheme(s, persist);
  gmeter_ui::applyScheme();
}

bool applyScale(int16_t min10, int16_t max10) {
  return gauge_ui::applyScale(min10, max10);
}

void setMount(gmeter::Mount m, bool flip, bool persist) {
  if (persist) settings::setMount((uint8_t)m, flip ? 1 : 0);
  gmeter::setMount(m, flip);
  g_lastMount = gmeter::mount();
  gmeter_ui::applyScheme();
  const char *name =
      m == gmeter::Mount::Auto ? "auto" : mountName(m);
  Serial.printf("[ui] g-meter mount %s, flip %u\n", name, flip ? 1u : 0u);
}

void zeroG() {
  gmeter::zero();
  Serial.println("[ui] g-meter zeroed");
}

void update(const ohxnow::Telemetry &t, const ohxnow::Stats &s) {
  // A fresh controller speed is the best "is the car moving?" gate for the
  // gravity tracker. When it is absent the IMU still test is the fallback.
  gmeter::setVehicleSpeed(t.speed, t.statusValid && !isnan(t.speed));

  // During a fade to the G-meter the gauge is still partly visible, so it
  // keeps receiving data until its opacity reaches zero. While the splash
  // covers the glass there is no point drawing either screen underneath it.
  if (g_gaugeOpa > 0 && !splash::active()) gauge_ui::update(t, s);
}

void poll() {
  const uint32_t now = millis();

  // ---- IMU: one 12-byte I2C burst at 100 Hz -------------------------
  const uint32_t elapsed = now - g_lastSampleMs;
  if (elapsed >= kSampleIntervalMs) {
    g_lastSampleMs = now;
    float dt = (float)elapsed * 0.001f;
    if (dt > 0.05f) dt = 0.05f;

    imu::Sample sample;
    if (imu::read(sample)) {
      g_lastGoodMs = now;
      if (g_imuOffline) {
        g_imuOffline = false;
        Serial.println("[imu] samples back");
      }
      gmeter::update(sample.ax, sample.ay, sample.az, sample.gx, sample.gy,
                     sample.gz, dt);
      if (g_lastMount != gmeter::mount()) {
        g_lastMount = gmeter::mount();
        Serial.printf("[g] mount auto -> %s\n", mountName(g_lastMount));
      }
    } else if (!g_imuOffline &&
               (uint32_t)(now - g_lastGoodMs) >= kImuTimeoutMs) {
      g_imuOffline = true;
      gmeter::invalidate();
      Serial.println("[imu] no samples; G-meter shows --");
    }
  }

  // ---- G-meter widget update ----------------------------------------
  static uint32_t lastGmUi = 0;
  if (g_gmOpa > 0 && !splash::active() &&
      (uint32_t)(now - lastGmUi) >= kGmUiIntervalMs) {
    lastGmUi = now;
#if GAUGE_CAPTURE_GMETER
    // Layout capture only: a synthetic 0.5 Hz sweep moves the blob so the
    // 1 s tail and the bottom readout are both in the snapshot. The real
    // IMU path below is unaffected in normal builds.
    const float phase =
        (float)(now % 2000) * (2.0f * 3.14159265f / 2000.0f);
    gmeter::Reading fake;
    fake.vertical = 0.75f * sinf(phase);
    fake.lateral = 0.25f * cosf(phase);
    fake.average = 0.55f + 0.25f * fabsf(sinf(phase));
    fake.valid = true;
    gmeter_ui::update(fake);
#else
    gmeter_ui::update(gmeter::reading());
#endif
  }

  // ---- cross-fade ---------------------------------------------------
  if (!g_fading || splash::active()) return;

  const uint32_t t = now - g_fadeStartMs;
  const bool toGauge = g_mode == Mode::Gauge;
  const uint8_t toGaugeOpa = toGauge ? 255 : 0;
  const uint8_t toGmOpa = toGauge ? 0 : 255;

  if (t >= kFadeMs) {
    g_fading = false;
    showOnly(g_mode);
    return;
  }

  // Opacity invalidates its object, and loop() runs far faster than the
  // 60 Hz panel. Applying every millisecond of the fade at 200 Hz filled
  // the invalidation buffer and forced full-screen repaints; one step per
  // display frame is both smoother and cheaper.
  if ((uint32_t)(now - g_lastFadeStepMs) < kFadeStepMs) return;
  g_lastFadeStepMs = now;

  // Smoothstep, so the fade starts and ends gently without an LVGL path.
  float p = (float)t / (float)kFadeMs;
  p = p * p * (3.0f - 2.0f * p);
  const int gaugeOpa =
      (int)g_fromGauge + (int)(((int)toGaugeOpa - (int)g_fromGauge) * p);
  const int gmOpa = (int)g_fromGm + (int)(((int)toGmOpa - (int)g_fromGm) * p);
  applyOpacity((uint8_t)gaugeOpa, (uint8_t)gmOpa);
}

}  // namespace dashboard
