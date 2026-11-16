// =====================================================================
// boot_button.cpp - the board's BOOT key as a user input
//
// GPIO0 is a strapping pin only while the chip resets. Once the firmware
// is running it is a normal input, so the same key that enters download
// mode also cycles the gauge's colour scheme.
//
// The key is interrupt-driven: one anti-aliased arc repaint can hold
// loop() for longer than a quick press, so polling alone misses taps.
// A slow poll still runs as a fallback and shares the same cooldown.
// =====================================================================
#include "hal/boot_button.h"

#include <Arduino.h>

#include "board_config.h"

namespace boot_button {
namespace {

constexpr uint32_t kCooldownMs = 200;  // ignore contact bounce and double taps

volatile bool g_isrFlag = false;

bool g_raw = false;        // last sample, true = pressed
bool g_stable = false;     // debounced state
uint32_t g_changedMs = 0;  // when g_raw last changed
bool g_event = false;      // a press is waiting to be reported
uint32_t g_lastAcceptMs = 0;

bool sample() { return digitalRead(BOARD_PIN_BOOT) == LOW; }

// Keep the ISR to one store: millis() and debounce live in pressed().
void IRAM_ATTR onFall() { g_isrFlag = true; }

}  // namespace

void begin() {
  pinMode(BOARD_PIN_BOOT, INPUT_PULLUP);

  g_raw = sample();
  g_stable = g_raw;
  g_changedMs = millis();
  g_event = false;
  g_lastAcceptMs = millis() - kCooldownMs;

  attachInterrupt(digitalPinToInterrupt(BOARD_PIN_BOOT), onFall, FALLING);
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

  bool irq;
  noInterrupts();
  irq = g_isrFlag;
  interrupts();

  const bool viaIsr = irq;
  const bool edge = irq || g_event;
  if (!edge) return false;

  // One report per press: bounce edges and a fast second tap share the
  // cooldown, which is shorter than a deliberate double press.
  if ((uint32_t)(now - g_lastAcceptMs) < kCooldownMs) return false;

  // Only consume the flags now that we've accepted the press.
  noInterrupts();
  g_isrFlag = false;
  interrupts();
  g_event = false;

  // The ISR has already reported this press. Move the debounced state to
  // "pressed" as well; otherwise the fallback poll sees the same still-held
  // press become stable, sets g_event, and reports it a second time when the
  // 200 ms cooldown expires. That looked like a second colour change on the
  // same press. The fallback still works when no ISR arrived.
  if (viaIsr) g_stable = true;

  g_lastAcceptMs = now;
  return true;
}

}  // namespace boot_button
