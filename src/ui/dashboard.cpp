// =====================================================================
// dashboard.cpp - mode ownership, IMU sampling, transitions and gestures
//
// Three screens on one LVGL display: boost gauge, oil temperature face,
// and G-meter. The dashboard owns IMU sampling, mode setting and the short
// cross-fade. It keeps G-meter math running even while other faces are
// displayed, so switching screens shows a warm 1 s average rather than a
// blank dot.
// =====================================================================
#include "ui/dashboard.h"

#include <Arduino.h>
#include <math.h>

#include "app/settings.h"
#include "board_config.h"
#include "hal/imu.h"
#include "hal/touch.h"
#include "ui/gauge_ui.h"
#include "ui/gmeter_ui.h"
#include "ui/oil_ui.h"
#include "ui/splash.h"

#ifndef GAUGE_CAPTURE_GMETER
#define GAUGE_CAPTURE_GMETER 0
#endif

namespace dashboard {
namespace {

constexpr uint32_t kSampleIntervalMs = 10;   // 100 Hz
constexpr uint32_t kGmUiIntervalMs = 33;     // 30 Hz blob
constexpr uint32_t kFadeMs = 260;
constexpr uint32_t kSlideMs = 280;
constexpr uint32_t kStepMs = 16;             // about one display frame
constexpr uint32_t kImuTimeoutMs = 1000;

Mode g_mode = Mode::Gauge;
bool g_moving = false;
Anim g_anim = Anim::Fade;
int8_t g_slideDir = 1;  // +1 = content travels left (advance)
uint32_t g_startMs = 0;
uint32_t g_lastStepMs = 0;
uint8_t g_gaugeOpa = 255;
uint8_t g_gmOpa = 0;
uint8_t g_oilOpa = 255;
uint8_t g_fromGauge = 255;
uint8_t g_fromGm = 0;
uint32_t g_lastSampleMs = 0;
uint32_t g_lastGoodMs = 0;
bool g_imuOffline = false;
gmeter::Mount g_lastMount = gmeter::Mount::Upright;

const char *mountName(gmeter::Mount m) {
  return m == gmeter::Mount::Flat ? "flat" : "upright";
}

void applyOpacity() {
  gauge_ui::setOpacity(g_gaugeOpa);
  gmeter_ui::setOpacity(g_gmOpa);
  oil_ui::setOpacity(g_oilOpa);
}

void setVisible() {
  gauge_ui::setVisible(g_mode == Mode::Gauge || g_mode == Mode::Oil);
  gmeter_ui::setVisible(g_mode == Mode::GMeter);
  oil_ui::setVisible(g_mode == Mode::Oil);
}

// Offset of a screen part-way through a slide. `from` is where it started,
// `to` where it ends; easing is cubic ease-out, which is what makes a swipe
// feel like it has weight instead of stopping dead.
int slideOffset(int from, int to, float p) {
  const float k = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
  return from + (int)lroundf((float)(to - from) * k);
}

void applySlide(float p) {
  const int w = LCD_H_RES;
  const bool toGauge = g_mode == Mode::Gauge;
  const bool toOil = g_mode == Mode::Oil;
  const bool toGMeter = g_mode == Mode::GMeter;

  // The screen being left travels out one way; the screen arriving comes in
  // from the other side.
  const int outTo = -g_slideDir * w;
  const int inFrom = g_slideDir * w;

  if (toGauge) {
    gauge_ui::setSlide(slideOffset(inFrom, 0, p));
    gmeter_ui::setSlide(slideOffset(0, outTo, p));
    oil_ui::setSlide(slideOffset(0, outTo, p));
  } else if (toOil) {
    oil_ui::setSlide(slideOffset(inFrom, 0, p));
    gauge_ui::setSlide(slideOffset(0, outTo, p));
    gmeter_ui::setSlide(slideOffset(0, outTo, p));
  } else {  // toGMeter
    gmeter_ui::setSlide(slideOffset(inFrom, 0, p));
    gauge_ui::setSlide(slideOffset(0, outTo, p));
    oil_ui::setSlide(slideOffset(0, outTo, p));
  }
}

// Taps and swipes. Kept here because this module already owns scheme and
// mode, and because a gesture must not fight the BOOT key: both end in the
// same two functions.
void onGesture(touch::Gesture g) {
  if (g == touch::Gesture::None) return;
  // The splash owns the glass, and a transition in flight must not be
  // interrupted - a second swipe mid-slide would leave a root at a stale
  // offset.
  if (splash::active() || g_moving) return;

  if (g == touch::Gesture::Tap) {
    nextScheme();
    Serial.println("[ui] touch tap");
    return;
  }

  g_slideDir = (g == touch::Gesture::SwipeLeft) ? 1 : -1;
  Serial.printf("[ui] touch swipe %s\n", g_slideDir > 0 ? "left" : "right");

  // Swipe order: boost -> oil -> G-meter -> boost. SwipeLeft advances,
  // SwipeRight goes back.
  if (g_mode == Mode::Gauge && g_slideDir > 0) {
    setMode(Mode::Oil, true, "swipe", Anim::Slide);
  } else if (g_mode == Mode::Oil && g_slideDir > 0) {
    setMode(Mode::GMeter, true, "swipe", Anim::Slide);
  } else if (g_mode == Mode::GMeter && g_slideDir > 0) {
    setMode(Mode::Gauge, true, "swipe", Anim::Slide);
  } else if (g_mode == Mode::Gauge && g_slideDir < 0) {
    setMode(Mode::GMeter, true, "swipe", Anim::Slide);
  } else if (g_mode == Mode::Oil && g_slideDir < 0) {
    setMode(Mode::Gauge, true, "swipe", Anim::Slide);
  } else if (g_mode == Mode::GMeter && g_slideDir < 0) {
    setMode(Mode::Oil, true, "swipe", Anim::Slide);
  }
}

}  // namespace

void begin() {
  const settings::Values &v = settings::get();
  g_mode = (Mode)v.mode;

  gauge_ui::create();
  gauge_ui::setScheme((theme::Scheme)v.scheme);
  gauge_ui::applyScale(v.min10, v.max10);

  oil_ui::begin();
  oil_ui::setScheme((theme::Scheme)v.scheme);
  applyOilScale(v.omin10, v.omax10);

  gmeter_ui::create();
  gmeter::begin((gmeter::Mount)v.mount, v.gflip != 0);
  g_lastMount = gmeter::mount();
  gmeter_ui::applyScheme();

  setVisible();
  g_lastSampleMs = millis();
  g_lastGoodMs = millis();

  touch::setHandler(onGesture);

  const char *mountSetting = v.mount == 2   ? "auto"
                             : v.mount == 1 ? "flat"
                                            : "upright";
  Serial.printf("[ui] mode %s, mount %s, flip %u\n",
                g_mode == Mode::GMeter ? "g-meter"
                : g_mode == Mode::Oil ? "oil"
                                     : "gauge", mountSetting,
                (unsigned)v.gflip);
}

Mode mode() { return g_mode; }

void setMode(Mode m, bool persist, const char *source, Anim anim) {
  if (m == g_mode && !g_moving) {
    if (persist) settings::setMode((uint8_t)m);
    return;
  }

  // Start from wherever the previous transition got to, so a quick second
  // press reverses smoothly instead of jumping back to full opacity.
  g_fromGauge = g_gaugeOpa;
  g_fromGm = g_gmOpa;
  g_oilOpa = 255;  // oil starts fully opaque
  g_startMs = millis();
  g_lastStepMs = g_startMs - kStepMs;  // first step immediately
  g_moving = true;
  g_anim = anim;
  g_mode = m;

  // All three roots must draw during the transition.
  gauge_ui::setVisible(true);
  gmeter_ui::setVisible(true);
  oil_ui::setVisible(true);
  gauge_ui::setSlide(0);
  gmeter_ui::setSlide(0);
  oil_ui::setSlide(0);

  if (anim == Anim::Slide) {
    // A slide never fades: the three screens are opaque and pass each other.
    g_gaugeOpa = 255;
    g_gmOpa = 255;
    g_oilOpa = 255;
    applyOpacity();
  }

  if (persist) settings::setMode((uint8_t)m);
  Serial.printf("[ui] mode %s (%s, %s)\n",
                g_mode == Mode::GMeter ? "g-meter"
                : g_mode == Mode::Oil ? "oil"
                                      : "gauge", source,
                anim == Anim::Slide ? "slide" : "fade");
}

void toggleMode(const char *source) {
  if (g_mode == Mode::Gauge) {
    setMode(Mode::Oil, true, source, Anim::Fade);
  } else if (g_mode == Mode::Oil) {
    setMode(Mode::GMeter, true, source, Anim::Fade);
  } else {
    setMode(Mode::Gauge, true, source, Anim::Fade);
  }
}

bool moving() { return g_moving; }

void nextScheme() {
  theme::nextScheme();
  settings::setScheme((uint8_t)theme::scheme());
  gauge_ui::setScheme(theme::scheme(), false);
  oil_ui::setScheme(theme::scheme(), false);
  gmeter_ui::applyScheme();
  Serial.printf("[ui] accent %s\n", theme::schemeName());
}

void setScheme(theme::Scheme s, bool persist) {
  gauge_ui::setScheme(s, persist);
  oil_ui::setScheme(s, persist);
  gmeter_ui::applyScheme();
}

bool applyScale(int16_t min10, int16_t max10) {
  bool ok = gauge_ui::applyScale(min10, max10);
  // The boost scale is PSI-based; the oil face owns its own °C range and
  // must not inherit the boost scale (the units differ and so does the
  // meaning of min/max).
  return ok;
}

bool applyOilScale(int16_t min10, int16_t max10) {
  const bool ok = oil_ui::applyScale(min10, max10);
  if (ok) settings::setOilScale(min10, max10);
  return ok;
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

  // During a fade to another screen the gauge/oil face is still partly
  // visible, so it keeps receiving data until its opacity reaches zero.
  // While the splash covers the glass there is no point drawing either screen
  // underneath it.
  const bool gaugeLive =
      (g_mode == Mode::Gauge || g_mode == Mode::Oil) ||
      (g_moving && g_anim == Anim::Fade);
  if (!splash::active()) {
    if (g_mode == Mode::Gauge && (gaugeLive || g_gaugeOpa > 0))
      gauge_ui::update(t, s);
    if (g_mode == Mode::Oil && (gaugeLive || g_oilOpa > 0))
      oil_ui::update(t, s);
  }
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

  // ---- transition ---------------------------------------------------
  if (!g_moving || splash::active()) return;

  const uint32_t t = now - g_startMs;
  const uint32_t duration = g_anim == Anim::Slide ? kSlideMs : kFadeMs;

  if (t >= duration) {
    g_moving = false;
    g_anim = Anim::Fade;
    setVisible();
    return;
  }

  // Opacity and position both invalidate their objects, and loop() runs far
  // faster than the ~60 Hz panel. Applying every millisecond of a transition
  // at 200 Hz filled the invalidation buffer and forced full-screen repaints;
  // one step per display frame is both smoother and cheaper.
  if ((uint32_t)(now - g_lastStepMs) < kStepMs) return;
  g_lastStepMs = now;

  // Smoothstep for the fade, cubic ease-out for the slide.
  float p = (float)t / (float)duration;
  if (g_anim == Anim::Slide) {
    applySlide(p);
  } else {
    p = p * p * (3.0f - 2.0f * p);
    // Fade only gauge <-> gmeter; oil stays fully opaque during cross-fades
    if (g_mode == Mode::Gauge || g_mode == Mode::GMeter) {
      const uint8_t toGaugeOpa = g_mode == Mode::Gauge ? 255 : 0;
      const uint8_t toGmOpa = g_mode == Mode::GMeter ? 255 : 0;
      const int gaugeOpa =
          (int)g_fromGauge + (int)(((int)toGaugeOpa - (int)g_fromGauge) * p);
      const int gmOpa = (int)g_fromGm + (int)(((int)toGmOpa - (int)g_fromGm) * p);
      g_gaugeOpa = gaugeOpa;
      g_gmOpa = gmOpa;
      applyOpacity();
    } else if (g_mode == Mode::Oil) {
      // Oil is always visible during a fade - just make sure opacity stays 255
      g_oilOpa = 255;
      applyOpacity();
    }
  }
}

}  // namespace dashboard