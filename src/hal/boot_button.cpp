// =====================================================================
// boot_button.cpp - the board's BOOT key as a two-action user input
//
// GPIO0 is a strapping pin only while the chip resets. Once the firmware
// is running it is a normal input:
//
//   short press  -> next accent colour
//   long press   -> boost gauge <-> G-meter
//
// A long press fires at the 700 ms threshold, not on release, so the mode
// changes while the finger is still down. Its release is swallowed.
//
// This is polled, not interrupt-driven. The old ISR existed because a
// disconnected USB CDC console could hold loop() for seconds; 0.3.4 made
// those writes non-blocking, and the panel flush is asynchronous. Polling
// every loop with a 30 ms debounce catches a deliberate tap and cannot
// mistake contact bounce on the way into a long press for a short one.
// =====================================================================
#include "hal/boot_button.h"

#include <Arduino.h>

#include "board_config.h"

namespace boot_button {
namespace {

constexpr uint32_t kCooldownMs = 200;  // ignore contact bounce and double taps
constexpr uint32_t kLongMs = 700;      // deliberate hold, not a fat-finger

bool g_raw = false;        // last sample, true = pressed
bool g_stable = false;     // debounced state
uint32_t g_changedMs = 0;  // when g_raw last changed
bool g_down = false;       // a press is being timed
uint32_t g_downMs = 0;     // first raw falling edge
bool g_longSent = false;   // this press already produced Long
bool g_shortPending = false;
uint32_t g_lastAcceptMs = 0;

bool sample() { return digitalRead(BOARD_PIN_BOOT) == LOW; }

}  // namespace

void begin() {
  pinMode(BOARD_PIN_BOOT, INPUT_PULLUP);

  g_raw = sample();
  g_stable = g_raw;
  g_changedMs = millis();
  g_down = g_stable;
  g_downMs = g_changedMs;
  g_longSent = false;
  g_shortPending = false;
  g_lastAcceptMs = millis() - kCooldownMs;
}

Event poll() {
  const uint32_t now = millis();
  const bool raw = sample();

  if (raw != g_raw) {
    g_raw = raw;
    g_changedMs = now;
  }

  // 30 ms debounce. The timer starts at the first raw edge, so a 700 ms
  // hold is measured from the real press, not from the end of debounce.
  if ((uint32_t)(now - g_changedMs) >= BOARD_BOOT_DEBOUNCE_MS &&
      g_stable != g_raw) {
    g_stable = g_raw;
    if (g_stable) {
      if (!g_down) {
        g_down = true;
        g_downMs = g_changedMs;
        g_longSent = false;
      }
    } else {
      g_down = false;
      if (!g_longSent) g_shortPending = true;
    }
  }

  if (g_down && !g_longSent && (uint32_t)(now - g_downMs) >= kLongMs) {
    g_longSent = true;
    g_shortPending = false;
    g_lastAcceptMs = now;
    return Event::Long;
  }

  if (g_shortPending &&
      (uint32_t)(now - g_lastAcceptMs) >= kCooldownMs) {
    g_shortPending = false;
    g_lastAcceptMs = now;
    return Event::Short;
  }

  return Event::None;
}

}  // namespace boot_button
