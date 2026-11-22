// =====================================================================
// sim_support.cpp - what the UI modules expect from the board, on the host
//
// The renderer compiles the real UI sources (theme, gauge_ui, oil_ui,
// oil_press_ui, coolant_ui, drive_ui, gmeter_ui, splash) against real LVGL.
// Four board-side dependencies are left:
//
//   * Arduino.h        - the harness clock and the console (host/Arduino.h)
//   * settings         - NVS. Nothing is stored; setScheme() accepts and
//                        remembers in RAM so a persist=true call is not a crash
//   * ohx_now          - the radio. stats() reports nothing heard, sendMode()
//                        refuses: the renderer never builds a packet, which is
//                        the same guarantee GAUGE_CAPTURE gives on the board
//   * splashimg        - the mask ROM's TJpgDec. The JPEG is decoded to raw
//                        RGB565 by tools/sim/splash_raw.py at build time, so
//                        this only maps the file
//
// Keeping the shims here rather than #ifdef-ing the real files is deliberate:
// the renderer photographs the same code the firmware runs.
// =====================================================================
#include <Arduino.h>
#include <lvgl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/settings.h"
#include "net/ohx_now.h"
#include "ui/splash_img.h"

// ---------------------------------------------------------------- clock
namespace {
uint32_t g_millis = 0;
}

uint32_t millis() { return g_millis; }
void simSetMillis(uint32_t ms) { g_millis = ms; }

// -------------------------------------------------------------- console
SimSerial Serial;

void SimSerial::print(const char *s) { fputs(s, stderr); }

void SimSerial::println(const char *s) {
  fputs(s, stderr);
  fputc('\n', stderr);
}

void SimSerial::printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}

// LVGL 9 aborts through this instead of hanging. On the host it is a real
// abort: a renderer that quietly continued after an assert would produce a
// bad image and no explanation.
void ohx_lv_assert_handler(const char *file, int line) {
  fprintf(stderr, "[sim] LVGL assert: %s:%d\n", file, line);
  abort();
}

// ------------------------------------------------------------- settings
namespace settings {
namespace {
Values g_values = {};
uint8_t g_scheme = 0;
}  // namespace

void begin() { g_values.scheme = g_scheme; }

const Values &get() { return g_values; }

bool setScheme(uint8_t scheme) {
  if (scheme >= kSchemeCount) return false;
  g_scheme = scheme;
  return true;
}
}  // namespace settings

// -------------------------------------------------------------- ohx_now
namespace ohxnow {
namespace {
Stats g_stats = {};
}  // namespace

Stats stats() { return g_stats; }

SendResult sendMode(uint8_t) { return SendResult::NoStatus; }
}  // namespace ohxnow

// ------------------------------------------------------------- splashimg
namespace splashimg {
namespace {
constexpr int kWidth = 466;
constexpr int kHeight = 466;
constexpr size_t kImageBytes = (size_t)kWidth * (size_t)kHeight * 2;

uint8_t *g_pixels = nullptr;
lv_image_dsc_t g_dsc = {};
}  // namespace

bool decode() {
  // SIM_SPLASH_RAW is the same asset the firmware embeds, expanded to
  // little-endian RGB565 by tools/sim/splash_raw.py. TJpgDec hands the ROM
  // the JPEG and converts per MCU band; the bytes that arrive here are the
  // same buffer the panel would see.
  FILE *f = fopen(SIM_SPLASH_RAW, "rb");
  if (f == nullptr) {
    fprintf(stderr, "[sim] cannot open %s\n", SIM_SPLASH_RAW);
    return false;
  }
  g_pixels = (uint8_t *)malloc(kImageBytes);
  if (g_pixels == nullptr) {
    fclose(f);
    return false;
  }
  const size_t got = fread(g_pixels, 1, kImageBytes, f);
  fclose(f);
  if (got != kImageBytes) {
    fprintf(stderr, "[sim] %s is %zu bytes, want %zu\n", SIM_SPLASH_RAW, got,
            kImageBytes);
    free(g_pixels);
    g_pixels = nullptr;
    return false;
  }

  g_dsc = {};
  g_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  g_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  g_dsc.header.w = kWidth;
  g_dsc.header.h = kHeight;
  g_dsc.header.stride = kWidth * 2;
  g_dsc.data_size = kImageBytes;
  g_dsc.data = g_pixels;
  return true;
}

const lv_image_dsc_t *image() { return g_pixels != nullptr ? &g_dsc : nullptr; }

void release() {
  free(g_pixels);
  g_pixels = nullptr;
}
}  // namespace splashimg
