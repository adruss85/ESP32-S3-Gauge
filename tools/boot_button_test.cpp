// Host-side check of the BOOT key short/long state machine. Build:
//   g++ -std=c++17 -Itools/host -Isrc -Iinclude -o /tmp/boot_button_test tools/boot_button_test.cpp src/hal/boot_button.cpp
// tools/host/Arduino.h is a four-symbol shim; no hardware is touched.
#include <cstdio>

#include <Arduino.h>

#include "hal/boot_button.h"

namespace {
uint32_t g_ms = 0;
int g_pin = HIGH;
int g_fail = 0;

boot_button::Event pollAt(uint32_t ms, int pin) {
  g_ms = ms;
  g_pin = pin;
  return boot_button::poll();
}

void expect(const char *what, boot_button::Event got,
            boot_button::Event want) {
  const bool ok = got == want;
  if (!ok) g_fail++;
  std::printf("%-40s %s\n", what, ok ? "ok" : "FAIL");
}

}  // namespace

// Arduino stubs used by boot_button.cpp.
void pinMode(int, int) {}
int digitalRead(int) { return g_pin; }
uint32_t millis() { return g_ms; }

int main() {
  boot_button::begin();

  // Short press: 80 ms held, released, then debounced.
  pollAt(100, LOW);
  pollAt(140, LOW);
  pollAt(180, LOW);
  pollAt(215, HIGH);
  expect("short press", pollAt(250, HIGH), boot_button::Event::Short);

  // Long press: reported at 700 ms from the raw edge, release swallowed.
  pollAt(1000, LOW);
  pollAt(1040, LOW);
  expect("long press at 700 ms", pollAt(1700, LOW),
         boot_button::Event::Long);
  pollAt(1800, HIGH);
  expect("long release is swallowed", pollAt(1840, HIGH),
         boot_button::Event::None);

  // Bounce on the way into a long press must not emit a short first.
  pollAt(2000, LOW);
  expect("bounce high emits nothing", pollAt(2005, HIGH),
         boot_button::Event::None);
  pollAt(2010, LOW);
  expect("bounce settled emits nothing", pollAt(2050, LOW),
         boot_button::Event::None);
  expect("bounce then long", pollAt(2710, LOW), boot_button::Event::Long);
  pollAt(2800, HIGH);
  expect("bounce release swallowed", pollAt(2840, HIGH),
         boot_button::Event::None);

  std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail,
              g_fail == 1 ? "" : "s");
  return g_fail ? 1 : 0;
}
