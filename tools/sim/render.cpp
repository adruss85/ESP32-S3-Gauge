// =====================================================================
// render.cpp - render the gauge's screens to a framebuffer on the host
//
// The firmware's own GAUGE_CAPTURE build photographs the glass, but it needs
// the board. This does the same job with no hardware: it compiles the real UI
// sources against the real LVGL 9.3 configuration, drives them with a fixed
// telemetry sample, and writes each 466x466 RGB565 framebuffer as a PPM -
// the same format tools/capture_frame.py produces over USB.
//
// It is a render, not a photograph. It cannot show a wrong byte order, a
// wrong column gap or a wrong brightness: those are the panel's, not LVGL's.
//
// Build and run:
//   make -C tools/sim
//   ./tools/sim/build/render /tmp/gauge-render
//   ./tools/sim/preview.py /tmp/gauge-render assets
//
// Output, one PPM per image:
//   boost_<scheme>.ppm        the boost face in all seven schemes
//   oil-temp.ppm              OIL °C
//   oil-pressure.ppm          OIL PRESSURE PSI
//   coolant.ppm               COOLANT °C
//   gmeter.ppm                the 1 s tail and readout, 2 s into a sweep
//   haldex.ppm                60:40 with a 45% request against 38% actual
//   splash.ppm                the boot screen at 1.4 s
// =====================================================================
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app/settings.h"
#include "net/ohx_now.h"
#include "theme.h"
#include "ui/coolant_ui.h"
#include "ui/drive_ui.h"
#include "ui/gauge_ui.h"
#include "ui/gmeter_ui.h"
#include "ui/oil_press_ui.h"
#include "ui/oil_ui.h"
#include "ui/splash.h"

namespace {

constexpr int kW = 466;
constexpr int kH = 466;

// The whole glass in one buffer: one render pass, no partial redraw, which is
// what a snapshot wants (the panel itself uses a 466x48 partial buffer).
uint16_t g_fb[kW * kH];
char g_outDir[512];

const char *const kSchemeNames[(int)theme::Scheme::Count] = {
    "midnight", "orange", "green", "blue", "purple", "white", "red"};

struct Path {
  char buf[600];
};

Path outPath(const char *name) {
  Path p;
  snprintf(p.buf, sizeof(p.buf), "%s/%s.ppm", g_outDir, name);
  return p;
}

void flushCb(lv_display_t *disp, const lv_area_t *, uint8_t *) {
  // The buffer already holds the frame; there is no panel to send it to.
  lv_display_flush_ready(disp);
}

// RGB565 little-endian to P6, expanded exactly as tools/capture_frame.py
// expands a frame from the board, so a host render and a capture are
// comparable pixel for pixel.
void writePpm(const char *path, const uint16_t *px, int w, int h) {
  FILE *f = fopen(path, "wb");
  if (f == nullptr) {
    fprintf(stderr, "[sim] cannot write %s\n", path);
    exit(1);
  }
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; i++) {
    const uint16_t v = px[i];
    const uint8_t rgb[3] = {uint8_t(((v >> 11) & 0x1F) << 3),
                            uint8_t(((v >> 5) & 0x3F) << 2),
                            uint8_t((v & 0x1F) << 3)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  fprintf(stderr, "[sim] %s\n", path);
}

void capture(const char *name) {
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(nullptr);
  const Path p = outPath(name);
  writePpm(p.buf, g_fb, kW, kH);
}

// The six faces, as dashboard::begin() leaves them: every module builds its
// own full-screen root on the one active screen and the dashboard shows one
// at a time. Building each face on a separate screen instead would give it a
// background from LVGL's default theme, which the panel never shows.
enum class Face { Boost, OilTemp, OilPressure, Coolant, GMeter, Drive };

void showOnly(Face face) {
  gauge_ui::setVisible(face == Face::Boost);
  oil_ui::setVisible(face == Face::OilTemp);
  oil_press_ui::setVisible(face == Face::OilPressure);
  coolant_ui::setVisible(face == Face::Coolant);
  gmeter_ui::setVisible(face == Face::GMeter);
  drive_ui::setVisible(face == Face::Drive);
}

// The fixed sample the firmware's capture build uses (src/main.cpp), so a
// render and a capture of the same build show the same numbers.
ohxnow::Telemetry captureTelemetry() {
  ohxnow::Telemetry t = {};
  t.statusValid = true;
  t.boostPsi = 18.4f;
  t.iatValid = true;
  t.iatC = 42.0f;
  t.oilTempValid = true;
  t.oilTempC = 98.0f;
  t.coolantValid = true;
  t.coolantC = 90.0f;
  t.oilPressValid = true;
  t.oilPressPsi = 52.3f;

  // The Haldex face: a mid-range request against a lower actual, which is the
  // pair a driver actually reads and the case where the two must not look
  // alike.
  t.mode = 3;  // 60:40
  t.lockTargetValid = true;
  t.lockTarget = 45;
  t.lockActualValid = true;
  t.lockActual = 38;
  t.haldexCan = true;
  t.ctrlAccept = true;
  t.standalone = false;
  t.couplingOpen = false;
  t.tempProt = false;
  return t;
}

ohxnow::Stats captureStats() {
  ohxnow::Stats s = {};
  s.raw = 1;
  s.statusPackets = 1;
  s.canPackets = 1;
  s.statusAgeMs = 40;
  s.channel = 6;
  return s;
}

void buildFaces(const ohxnow::Telemetry &t, const ohxnow::Stats &s) {
  theme::setScheme(theme::Scheme::Midnight);

  gauge_ui::create();
  gauge_ui::update(t, s);

  oil_ui::begin();
  oil_ui::update(t, s);

  oil_press_ui::begin();
  oil_press_ui::update(t, s);

  coolant_ui::begin();
  coolant_ui::update(t, s);

  drive_ui::begin();
  drive_ui::update(t, s);

  gmeter_ui::create();
}

void sweepGmeter() {
  gmeter_ui::setVisible(true);

  // The same 0.5 Hz sweep the GAUGE_CAPTURE_GMETER build feeds, stepped on
  // the harness clock. It runs for two seconds and stops on the zero crossing
  // (blob at 0 g vertically, full 0.25 g laterally): the trailing second then
  // spans the whole vertical travel, which is the widest the tail ever gets.
  // Capture it anywhere else and the tail is a short smear.
  constexpr int kSteps = 125;  // 125 x 16 ms = 2 s
  uint32_t now = 10000;
  for (int i = 0; i < kSteps; i++) {
    now += 16;
    simSetMillis(now);
    const float phase = (float)(now % 2000) * (2.0f * 3.14159265f / 2000.0f);
    gmeter::Reading fake;
    fake.vertical = 0.75f * sinf(phase);
    fake.lateral = 0.25f * cosf(phase);
    fake.average = 0.55f + 0.25f * fabsf(sinf(phase));
    fake.valid = true;
    gmeter_ui::update(fake);
  }
}

// The scheme sweep, laid out the way the board's capture does it: one face,
// seven palettes, persist=false because a renderer must not write settings.
void captureSchemes() {
  showOnly(Face::Boost);
  for (int i = 0; i < (int)theme::Scheme::Count; i++) {
    gauge_ui::setScheme((theme::Scheme)i, false);
    char name[32];
    snprintf(name, sizeof(name), "boost_%s", kSchemeNames[i]);
    capture(name);
  }
  gauge_ui::setScheme(theme::Scheme::Midnight, false);
}

void captureFaces() {
  showOnly(Face::OilTemp);
  capture("oil-temp");

  showOnly(Face::OilPressure);
  capture("oil-pressure");

  showOnly(Face::Coolant);
  capture("coolant");

  showOnly(Face::GMeter);
  capture("gmeter");

  showOnly(Face::Drive);
  capture("haldex");
}

// The splash covers whatever is on the glass, as it does on the board, so it
// goes on last. The 3 s rule is stepped on the harness clock rather than
// waited for.
void captureSplash() {
  simSetMillis(20000);
  splash::begin();
  simSetMillis(21400);
  splash::update();
  capture("splash");
}

}  // namespace

int main(int argc, char **argv) {
  snprintf(g_outDir, sizeof(g_outDir), "%s",
           argc > 1 ? argv[1] : "/tmp/gauge-render");
  mkdir(g_outDir, 0755);

  lv_init();

  lv_display_t *disp = lv_display_create(kW, kH);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, g_fb, nullptr, sizeof(g_fb),
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(disp, flushCb);

  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_screen_load(scr);

  const ohxnow::Telemetry t = captureTelemetry();
  const ohxnow::Stats s = captureStats();

  buildFaces(t, s);
  sweepGmeter();

  captureSchemes();
  captureFaces();
  captureSplash();

  fprintf(stderr, "[sim] done, %s\n", g_outDir);
  return 0;
}
