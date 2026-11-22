// Host-side check of the touch gesture classifier. Build:
//   g++ -std=c++17 -Isrc -Iinclude -o /tmp/gesture_test tools/gesture_test.cpp \
//       src/hal/gesture.cpp
//
// No hardware, no panel, no LVGL: this drives Tracker::update() with synthetic
// samples at the poll rate touch.cpp uses (15 ms) and asserts what the driver
// would have emitted. The case it exists for is the last group: a flicked
// swipe that never travelled 46 px used to be read as a tap, and a tap used to
// step the colour scheme.
#include <cstdio>

#include "hal/gesture.h"

namespace {

int g_fail = 0;

const char *name(gesture::Gesture g) {
  switch (g) {
    case gesture::Gesture::None: return "None";
    case gesture::Gesture::Tap: return "Tap";
    case gesture::Gesture::Hold: return "Hold";
    case gesture::Gesture::SwipeLeft: return "SwipeLeft";
    case gesture::Gesture::SwipeRight: return "SwipeRight";
  }
  return "?";
}

void expect(const char *what, gesture::Gesture got, gesture::Gesture want) {
  const bool ok = got == want;
  if (!ok) g_fail++;
  std::printf("%-52s %-10s %s\n", what, name(got), ok ? "ok" : "FAIL");
}

// One press: down at (x0,y0), a path of points every 15 ms, then release.
// Returns every gesture the tracker emitted, checking there is at most one.
struct Run {
  gesture::Gesture g = gesture::Gesture::None;
  int count = 0;
};

Run press(gesture::Tracker &t, int x0, int y0, uint32_t &ms, uint32_t holdForMs,
          int dxPerStep = 0, int dyPerStep = 0) {
  Run r;
  // Land.
  r.g = t.update(true, x0, y0, ms);
  if (r.g != gesture::Gesture::None) r.count++;
  int x = x0;
  int y = y0;
  uint32_t elapsed = 0;
  while (elapsed < holdForMs) {
    ms += 15;
    elapsed += 15;
    x += dxPerStep;
    y += dyPerStep;
    const gesture::Gesture g = t.update(true, x, y, ms);
    if (g != gesture::Gesture::None) {
      r.g = g;
      r.count++;
    }
  }
  // Lift, from wherever the path got to.
  ms += 15;
  const gesture::Gesture g = t.update(false, x, y, ms);
  if (g != gesture::Gesture::None) {
    r.g = g;
    r.count++;
  }
  return r;
}

}  // namespace

int main() {
  std::printf("gesture classifier (slop 12 px, hold 550 ms, swipe 46 px)\n\n");

  {
    // A quick press that never moves: a tap.
    gesture::Tracker t;
    uint32_t ms = 10000;
    const Run r = press(t, 230, 230, ms, 120);
    expect("still for 120 ms, release", r.g, gesture::Gesture::Tap);
    if (r.count != 1) {
      g_fail++;
      std::printf("%-52s %d events (want 1)\n", "tap fires exactly once",
                  r.count);
    }
  }

  {
    // Real touch panels jitter. 8 px of wander over the press is still a tap.
    gesture::Tracker t;
    uint32_t ms = 10000;
    const Run r = press(t, 200, 200, ms, 120, 1, 0);  // 8 steps -> 8 px
    expect("8 px of jitter, release", r.g, gesture::Gesture::Tap);
  }

  {
    // Held still: the hold fires at the threshold, while the finger is down,
    // and the release adds nothing.
    gesture::Tracker t;
    uint32_t ms = 10000;
    const Run r = press(t, 233, 233, ms, 900);
    expect("still for 900 ms, release", r.g, gesture::Gesture::Hold);
    if (r.count != 1) {
      g_fail++;
      std::printf("%-52s %d events (want 1: the release is swallowed)\n",
                  "hold fires once and not again on release", r.count);
    }
    if (!t.holdSent()) {
      g_fail++;
      std::printf("%-52s FAIL\n", "tracker reports the hold as sent");
    }
  }

  {
    // A hold fires early in a long press, not at the end.
    gesture::Tracker t;
    uint32_t ms = 10000;
    gesture::Gesture got = gesture::Gesture::None;
    uint32_t firedAt = 0;
    const uint32_t start = ms;
    t.update(true, 233, 233, ms);
    for (int i = 0; i < 60; i++) {
      ms += 15;
      const gesture::Gesture g = t.update(true, 233, 233, ms);
      if (g == gesture::Gesture::Hold && firedAt == 0) {
        got = g;
        firedAt = ms - start;
      }
    }
    expect("hold fires while the finger is down", got, gesture::Gesture::Hold);
    const bool soon = firedAt >= 540 && firedAt <= 570;
    if (!soon) g_fail++;
    std::printf("%-52s %lu ms  %s\n", "hold fires at the 550 ms threshold",
                (unsigned long)firedAt, soon ? "ok" : "FAIL");
  }

  {
    // A real swipe: 60 px left in 4 steps, then lift.
    gesture::Tracker t;
    uint32_t ms = 10000;
    const Run r = press(t, 300, 230, ms, 60, -20, 0);
    expect("60 px left, release", r.g, gesture::Gesture::SwipeLeft);
  }

  {
    gesture::Tracker t;
    uint32_t ms = 10000;
    const Run r = press(t, 150, 230, ms, 60, 20, 0);
    expect("60 px right, release", r.g, gesture::Gesture::SwipeRight);
  }

  {
    // Vertical drag: not a swipe, and not a tap either (it moved).
    gesture::Tracker t;
    uint32_t ms = 10000;
    const Run r = press(t, 233, 120, ms, 90, 0, 30);
    expect("90 px down, release", r.g, gesture::Gesture::None);
  }

  {
    // The false-tap case this classifier was rewritten for: a flick that
    // travels 30 px and comes back, then lifts quickly.
    gesture::Tracker t;
    uint32_t ms = 10000;
    t.update(true, 233, 233, ms);
    ms += 15;
    t.update(true, 263, 233, ms);  // out 30 px
    ms += 15;
    t.update(true, 233, 233, ms);  // back to where it landed
    ms += 15;
    const gesture::Gesture g = t.update(false, 233, 233, ms);
    expect("flick out 30 px and back (net 0)", g, gesture::Gesture::None);
  }

  {
    // A short flick that never reaches the swipe distance: no gesture at all,
    // because it wandered more than the tap slop.
    gesture::Tracker t;
    uint32_t ms = 10000;
    const Run r = press(t, 233, 233, ms, 45, 10, 0);  // 30 px right
    expect("short flick, 30 px right", r.g, gesture::Gesture::None);
    if (r.count != 0) {
      g_fail++;
      std::printf("%-52s %d events (want 0)\n", "a short flick is silent",
                  r.count);
    }
  }

  {
    // A slow press that wanders before the hold threshold and then rests:
    // the hold is refused because it moved, and it is not a tap either.
    gesture::Tracker t;
    uint32_t ms = 10000;
    t.update(true, 200, 200, ms);
    ms += 15;
    t.update(true, 220, 200, ms);  // 20 px
    gesture::Gesture got = gesture::Gesture::None;
    for (int i = 0; i < 60; i++) {
      ms += 15;
      const gesture::Gesture g = t.update(true, 220, 200, ms);
      if (g != gesture::Gesture::None) got = g;
    }
    ms += 15;
    const gesture::Gesture up = t.update(false, 220, 200, ms);
    if (up != gesture::Gesture::None) got = up;
    expect("moved 20 px then rested 900 ms", got, gesture::Gesture::None);
  }

  {
    // A second report arriving straight after a release is the tail of the
    // first press, not a new one.
    gesture::Tracker t;
    uint32_t ms = 10000;
    const Run r = press(t, 233, 233, ms, 90);
    if (r.g != gesture::Gesture::Tap) {
      g_fail++;
      std::printf("%-52s FAIL (setup)\n", "sequence start is a tap");
    }
    // 60 ms later, well inside the 120 ms release guard.
    const gesture::Gesture g1 = t.update(true, 233, 233, ms + 60);
    const gesture::Gesture g2 = t.update(false, 233, 233, ms + 75);
    expect("double report 60 ms after a tap", g1, gesture::Gesture::None);
    expect("... its release", g2, gesture::Gesture::None);
  }

  {
    // After the guard, the next tap works.
    gesture::Tracker t;
    uint32_t ms = 10000;
    press(t, 233, 233, ms, 90);
    ms += 200;
    const Run r = press(t, 233, 233, ms, 90);
    expect("a tap 200 ms after the last release", r.g, gesture::Gesture::Tap);
  }

  std::printf("\n%s\n", g_fail == 0 ? "all checks passed" : "FAILURES");
  return g_fail == 0 ? 0 : 1;
}
