// =====================================================================
// dashboard.h - which screen is on the glass, and the switch between them
//
// The boost gauge and the G-meter are two roots on the same LVGL screen.
// The dashboard owns the IMU sampling, the mode setting and the short
// cross-fade.  It keeps the G-meter math running even while the boost
// gauge is displayed, so switching screens shows a warm 1 s average rather
// than a blank dot.
// =====================================================================
#pragma once

#include "app/gmeter.h"
#include "net/ohx_now.h"
#include "theme.h"

namespace dashboard {

enum class Mode : uint8_t {
  Gauge = 0,
  GMeter = 1,
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

void setMount(gmeter::Mount m, bool flip, bool persist = true);
void zeroG();

// 20 Hz boost-gauge refresh. The G-meter has its own 30 Hz path in poll().
void update(const ohxnow::Telemetry &t, const ohxnow::Stats &s);

// Call every loop: samples the IMU at ~100 Hz, advances the fade and
// repaints the blob at ~50 Hz.
void poll();

}  // namespace dashboard
