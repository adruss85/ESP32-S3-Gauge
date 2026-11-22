// Arduino shim for the host screen renderer.
//
// Only this directory is on the include path when tools/sim builds, so
// PlatformIO never sees it. It provides the two things the UI modules touch
// outside LVGL: the console they log to, and millis().
//
// millis() is not a real clock here: the harness sets it by hand, because the
// timed paths being photographed (the G-meter's 1 s tail, the splash's 3 s
// rule) would otherwise need the tool to sleep. tools/host/Arduino.h stays the
// shim for the key tests, which need the pin API instead.
#pragma once

#include <stddef.h>
#include <stdint.h>

uint32_t millis();

struct SimSerial {
  void begin(unsigned long) {}
  void flush() {}
  int available() { return 0; }
  int read() { return -1; }
  int peek() { return -1; }
  size_t write(const uint8_t *, size_t n) { return n; }
  void print(const char *s);
  void println(const char *s = "");
  void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};

extern SimSerial Serial;

// Advance the harness clock. Defined with millis() in sim_support.cpp.
void simSetMillis(uint32_t ms);
