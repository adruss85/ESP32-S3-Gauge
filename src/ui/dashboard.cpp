// =====================================================================
// dashboard.cpp - mode ownership, IMU sampling, transitions and gestures
//
// Two things can change the screen: the BOOT key / cruise stalk / web page,
// which cross-fade, and a horizontal swipe, which slides. Both end in the
// same place; the difference is what the eye sees.
//
// Fade design: LVGL opacity on a container allocates a layer buffer sized to
// the container. A 466x466 RGB565 layer is 434 KB, which cannot fit the
// 128 KB pool, so each module scales the opacity of its own leaf objects
// instead. At ~60 Hz the arc costs a few extra repaints for 260 ms; after
// that the hidden root stops drawing entirely.
//
// Slide design: both roots translate instead of fading. The invalidated area
// is already the whole glass during a transition (both screens are visible),
// so moving costs no more than the fade and reads as a push. Each root's
// offset is owned by its own module (setSlide), because the gauge's root and
// the G-meter's canvas are different objects at different y origins.
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
  gauge_ui::setSlide(0);
  gmeter_ui::setSlide(0);
  applyOpacity(g ? 255 : 0, g ? 0 : 255);
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

  // The screen being left travels out one way; the screen arriving comes in
  // from the other side.
  const int outTo = -g_slideDir * w;
  const int inFrom = g_slideDir * w;

  if (toGauge) {
    gauge_ui::setSlide(slideOffset(inFrom, 0, p));
    gmeter_ui::setSlide(slideOffset(0, outTo, p));
  } else {
    gauge_ui::setSlide(slideOffset(0, outTo, p));
    gmeter_ui::setSlide(slideOffset(inFrom, 0, p));
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
  setMode(g_mode == Mode::Gauge ? Mode::GMeter : Mode::Gauge, true, "swipe",
          Anim::Slide);
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

  touch::setHandler(onGesture);

  const char *mountSetting = v.mount == 2   ? "auto"
                             : v.mount == 1 ? "flat"
                                            : "upright";
  Serial.printf("[ui] mode %s, mount %s, flip %u\n",
                g_mode == Mode::GMeter ? "g-meter" : "gauge", mountSetting,
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
  g_startMs = millis();
  g_lastStepMs = g_startMs - kStepMs;  // first step immediately
  g_moving = true;
  g_anim = anim;
  g_mode = m;

  // Both roots must draw during the transition.
  gauge_ui::setVisible(true);
  gmeter_ui::setVisible(true);
  gauge_ui::setSlide(0);
  gmeter_ui::setSlide(0);

  if (anim == Anim::Slide) {
    // A slide never fades: the two screens are opaque and pass each other.
    applyOpacity(255, 255);
  }

  if (persist) settings::setMode((uint8_t)m);
  Serial.printf("[ui] mode %s (%s, %s)\n",
                m == Mode::GMeter ? "g-meter" : "gauge", source,
                anim == Anim::Slide ? "slide" : "fade");
}

void toggleMode(const char *source) {
  setMode(g_mode == Mode::Gauge ? Mode::GMeter : Mode::Gauge, true, source,
          Anim::Fade);
}

bool moving() { return g_moving; }

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
  // keeps receiving data until its opacity reaches zero. A slide does not
  // need that: the gauge is opaque and leaving, so redrawing its arc would
  // only add work. While the splash covers the glass there is no point
  // drawing either screen underneath it.
  const bool gaugeLive = g_mode == Mode::Gauge || (g_moving && g_anim == Anim::Fade);
  if (gaugeLive && !splash::active()) gauge_ui::update(t, s);
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
    showOnly(g_mode);
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
    const bool toGauge = g_mode == Mode::Gauge;
    const uint8_t toGaugeOpa = toGauge ? 255 : 0;
    const uint8_t toGmOpa = toGauge ? 0 : 255;
    const int gaugeOpa =
        (int)g_fromGauge + (int)(((int)toGaugeOpa - (int)g_fromGauge) * p);
    const int gmOpa = (int)g_fromGm + (int)(((int)toGmOpa - (int)g_fromGm) * p);
    applyOpacity((uint8_t)gaugeOpa, (uint8_t)gmOpa);
  }
}

}  // namespace dashboard
