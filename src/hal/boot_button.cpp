// =====================================================================
// boot_button.cpp - the board's BOOT key as a user input
//
// GPIO0 is a strapping pin only while the chip resets. Once the firmware
// is running it is a normal input, so the same key that enters download
// mode also cycles the gauge's colour scheme.
// =====================================================================
#include "hal/boot_button.h"

#include <Arduino.h>

#include "board_config.h"

namespace boot_button {
namespace {

bool g_raw = false;       // last sample, true = pressed
bool g_stable = false;    // debounced state
uint32_t g_changedMs = 0; // when g_raw last changed
bool g_event = false;     // a press is waiting to be reported

bool sample() { return digitalRead(BOARD_PIN_BOOT) == LOW; }

}  // namespace

void begin() {
  pinMode(BOARD_PIN_BOOT, INPUT_PULLUP);
  g_raw = sample();
  g_stable = g_raw;
  g_changedMs = millis();
  g_event = false;
}

bool pressed() {
  const uint32_t now = millis();
  const bool raw = sample();

  if (raw != g_raw) {
    g_raw = raw;
    g_changedMs = now;
  }

  if ((uint32_t)(now - g_changedMs) >= BOARD_BOOT_DEBOUNCE_MS &&
      g_stable != g_raw) {
    g_stable = g_raw;
    if (g_stable) g_event = true;  // report on the falling edge only
  }

  if (!g_event) return false;
  g_event = false;
  return true;
}

}  // namespace boot_button
