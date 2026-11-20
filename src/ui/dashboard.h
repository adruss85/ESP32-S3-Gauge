// =====================================================================
// dashboard.h - which screen is on the glass, and the switch between them
//
// The boost gauge, the oil temperature face and the G-meter are three
// roots on the same LVGL screen. The dashboard owns the IMU sampling, the
// mode setting and the short cross-fade. It keeps the G-meter math running
// even while a face is displayed, so switching screens shows a warm 1 s
// average rather than a blank dot.
// =====================================================================
#pragma once

#include "app/gmeter.h"
#include "net/ohx_now.h"
#include "theme.h"

namespace dashboard {

enum class Mode : uint8_t {
  Gauge = 0,      // boost psi face (270 deg arc, BOOST PSI)
  Oil = 1,        // engine oil temp face (270 deg arc, OIL °C)
  OilPress = 2,   // oil pressure face (270 deg arc, OIL PRESSURE bar)
  Coolant = 3,    // coolant temp face (270 deg arc, COOLANT °C)
  GMeter = 4,     // accelerometer dot + tail + readout
};

// Which transition draws a mode change. A key or web change cross-fades; a
// swipe slides, because that is the motion the finger just made.
enum class Anim : uint8_t {
  Fade = 0,
  Slide = 1,
};

void begin();

Mode mode();

// source is only for the console line: "web", "BOOT key", "swipe" or
// "stored".
void setMode(Mode m, bool persist = true, const char *source = "BOOT key",
             Anim anim = Anim::Fade);
void toggleMode(const char *source = "BOOT key");

// True while a fade or slide is in flight; gestures are ignored then.
bool moving();

void nextScheme();
void setScheme(theme::Scheme s, bool persist = true);
bool applyScale(int16_t min10, int16_t max10);
bool applyOilScale(int16_t min10, int16_t max10);
bool applyOilPressScale(int16_t min10, int16_t max10);
bool applyCoolantScale(int16_t min10, int16_t max10);

void setMount(gmeter::Mount m, bool flip, bool persist = true);
void zeroG();

// 20 Hz boost-gauge refresh. The G-meter has its own 30 Hz path in poll().
void update(const ohxnow::Telemetry &t, const ohxnow::Stats &s);

// Call every loop: samples the IMU at ~100 Hz, advances the fade and
// repaints the blob at ~50 Hz.
void poll();

}  // namespace dashboard
