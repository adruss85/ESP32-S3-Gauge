// =====================================================================
// gesture.cpp - press tracking, gesture classification
//
// Header-only logic would be fine, but this keeps the build's one-file-per-
// module rule and puts the timing in a .cpp the host test can link.
// =====================================================================
#include "hal/gesture.h"

namespace gesture {

Gesture Tracker::update(bool down, int x, int y, uint32_t nowMs) {
  if (down && !armed_) {
    // A press that starts within the release guard is the tail of the last
    // gesture, not a new one.
    if ((uint32_t)(nowMs - lastReleaseMs_) < cfg_.releaseGuardMs) {
      return Gesture::None;
    }
    armed_ = true;
    holdSent_ = false;
    maxMove2_ = 0;
    startX_ = x;
    startY_ = y;
    startMs_ = nowMs;
    return Gesture::None;
  }

  if (down) {
    // Track the furthest the finger has been from where it landed, so a
    // press that moved away and came back is not a tap.
    const int dx = x - startX_;
    const int dy = y - startY_;
    const int d2 = dx * dx + dy * dy;
    if (d2 > maxMove2_) maxMove2_ = d2;

    // Fire the hold while the finger is still down, and only if it has
    // stayed put: a swipe never reaches this line.
    if (!holdSent_ && maxMove2_ <= slop2() &&
        (uint32_t)(nowMs - startMs_) >= cfg_.holdMs) {
      holdSent_ = true;
      return Gesture::Hold;
    }
    return Gesture::None;
  }

  if (!armed_) return Gesture::None;
  armed_ = false;
  lastReleaseMs_ = nowMs;
  // The hold already did its work; its release must not also be a tap.
  if (holdSent_) return Gesture::None;
  return classify(x, y, nowMs);
}

Gesture Tracker::classify(int x, int y, uint32_t nowMs) const {
  const int dx = x - startX_;
  const int dy = y - startY_;
  const int adx = dx < 0 ? -dx : dx;
  const int ady = dy < 0 ? -dy : dy;

  // Net displacement decides a swipe: the finger has to have travelled, and
  // horizontally.
  if (adx >= cfg_.swipeMinPx && adx > ady && maxMove2_ > slop2()) {
    return dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight;
  }
  if ((uint32_t)(nowMs - startMs_) <= cfg_.tapMaxMs &&
      maxMove2_ <= slop2()) {
    return Gesture::Tap;
  }
  return Gesture::None;
}

}  // namespace gesture
