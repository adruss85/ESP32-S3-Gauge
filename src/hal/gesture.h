// =====================================================================
// gesture.h - the press-tracking classifier behind the touch driver
//
// Kept out of touch.cpp (and free of Arduino and LVGL) so it can be driven
// from a host test: the difference between "the scheme changed" and "the
// screen changed" is entirely in these thresholds, and a gesture cannot be
// checked on a desk.
//
// The rules, in the order they matter:
//
//   * A Hold fires at its threshold while the finger is still down, like the
//     BOOT key, and swallows its release.
//   * A press that wanders further than tapSlopPx from where it landed is
//     never a Tap, even if it comes back to the start before lifting. This is
//     the case that made a flicked swipe step the colour scheme.
//   * A swipe needs net horizontal travel of swipeMinPx and has to have
//     moved: a press that ends where it began is not a swipe.
//   * Nothing at all is emitted for anything else.
// =====================================================================
#pragma once

#include <stdint.h>

namespace gesture {

enum class Gesture : uint8_t {
  None = 0,
  Tap,        // quick, motionless press and release
  Hold,       // motionless press held past holdMs (fires while still down)
  SwipeLeft,  // net travel left past swipeMinPx: advance
  SwipeRight, // net travel right past swipeMinPx: go back
};

struct Config {
  uint32_t tapMaxMs = 450;       // a tap is short...
  int tapSlopPx = 12;            // ...and never wanders this far
  uint32_t holdMs = 550;         // deliberate dwell, not a swipe
  int swipeMinPx = 46;           // 10% of the 466 px glass
  uint32_t releaseGuardMs = 120; // swallow a double report
};

class Tracker {
 public:
  explicit Tracker(const Config &cfg = Config()) : cfg_(cfg) {}

  // Feed one sample. Returns the gesture that completed (or fired) on it.
  Gesture update(bool down, int x, int y, uint32_t nowMs);

  // True once this press has already produced a Hold, so its release can be
  // swallowed. Exposed for the log line, not for control flow.
  bool holdSent() const { return holdSent_; }

 private:
  Gesture classify(int x, int y, uint32_t nowMs) const;

  int slop2() const { return cfg_.tapSlopPx * cfg_.tapSlopPx; }

  Config cfg_;
  bool armed_ = false;
  bool holdSent_ = false;
  int startX_ = 0;
  int startY_ = 0;
  uint32_t startMs_ = 0;
  int maxMove2_ = 0;  // furthest squared excursion from the landing point
  uint32_t lastReleaseMs_ = 0;
};

}  // namespace gesture
