// Host-side check of the G-meter filter and axis map. Build:
//   g++ -std=c++17 -Isrc -Iinclude -o /tmp/gmeter_test tools/gmeter_test.cpp src/app/gmeter.cpp -lm
#include <cmath>
#include <cstdio>

#include "app/gmeter.h"

namespace {

int g_fail = 0;

void check(const char *what, float got, float want, float tol) {
  const bool ok = std::isfinite(got) && std::fabs(got - want) <= tol;
  if (!ok) g_fail++;
  std::printf("%-42s got %+.3f want %+.3f  %s\n", what, got, want,
              ok ? "ok" : "FAIL");
}

// dt = 10 ms, chip axes in g.
void feed(int samples, float ax, float ay, float az) {
  for (int i = 0; i < samples; i++) {
    gmeter::update(ax, ay, az, 0.0f, 0.0f, 0.0f, 0.01f);
  }
}

void rest(int samples) { feed(samples, 0.0f, 1.0f, 0.0f); }

}  // namespace

int main() {
  // ---- upright: gravity on +Y, acceleration/braking on Z -------------
  gmeter::begin(gmeter::Mount::Upright, false);
  rest(400);
  check("upright rest vertical", gmeter::reading().vertical, 0.0f, 0.02f);
  check("upright rest average", gmeter::reading().average, 0.0f, 0.02f);
  check("upright rest valid", gmeter::reading().valid ? 1.0f : 0.0f, 1.0f,
        0.0f);

  // +0.5 g forward -> chip Z -0.5 g (the pseudo-force points rearward).
  for (int i = 0; i < 150; i++) {
    gmeter::update(0.0f, 1.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.01f);
  }
  check("upright accel vertical", gmeter::reading().vertical, 0.5f, 0.04f);
  check("upright accel average", gmeter::reading().average, 0.5f, 0.04f);

  // Braking is the opposite sign.
  for (int i = 0; i < 150; i++) {
    gmeter::update(0.0f, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.01f);
  }
  check("upright brake vertical", gmeter::reading().vertical, -0.5f, 0.04f);

  // ---- diagonal: sqrt(0.5^2 + 0.5^2), not the mean of the absolutes --
  for (int i = 0; i < 150; i++) {
    gmeter::update(0.5f, 1.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.01f);
  }
  check("diagonal average", gmeter::reading().average, 0.7071f, 0.05f);

  // ---- speed gate: a long gentle pull must not be learned as gravity --
  rest(300);
  gmeter::setVehicleSpeed(60.0f, true);
  for (int i = 0; i < 1500; i++) {  // 15 s at 0.05 g
    gmeter::update(0.0f, 1.0f, -0.05f, 0.0f, 0.0f, 0.0f, 0.01f);
  }
  check("15 s gentle pull (speed gate)", gmeter::reading().vertical, 0.05f,
        0.01f);
  gmeter::setVehicleSpeed(0.0f, true);
  rest(300);

  // ---- re-learning a new mount attitude ------------------------------
  gmeter::begin(gmeter::Mount::Upright, false);
  rest(400);
  // Gravity moves from +Y to a 20 degree tilt (0, 0.94, 0.34) while the
  // car is stationary. A tilt changes |a| only by rounding, so the tracker
  // must follow it; the old dynamic-vector gate could never recover.
  for (int i = 0; i < 500; i++) {
    gmeter::update(0.0f, 0.94f, 0.34f, 0.0f, 0.0f, 0.0f, 0.01f);
  }
  check("re-learned tilt vertical", gmeter::reading().vertical, 0.0f, 0.05f);

  // ---- zero ----------------------------------------------------------
  gmeter::zero();
  check("zero vertical", gmeter::reading().vertical, 0.0f, 0.001f);
  check("zero average", gmeter::reading().average, 0.0f, 0.001f);

  // ---- auto mount from gravity ---------------------------------------
  gmeter::begin(gmeter::Mount::Auto, false);
  feed(400, 0.0f, 0.0f, -1.0f);  // flat: gravity on Z
  check("auto mount chose flat",
        gmeter::mount() == gmeter::Mount::Flat ? 1.0f : 0.0f, 1.0f, 0.0f);
  gmeter::begin(gmeter::Mount::Auto, false);
  rest(400);  // upright: gravity on Y
  check("auto mount chose upright",
        gmeter::mount() == gmeter::Mount::Upright ? 1.0f : 0.0f, 1.0f, 0.0f);

  // ---- zero without a sample must not fabricate a reading ------------
  gmeter::begin(gmeter::Mount::Upright, false);
  gmeter::zero();
  check("zero without a sample stays invalid",
        gmeter::reading().valid ? 1.0f : 0.0f, 0.0f, 0.0f);

  // ---- mount change: flat uses +Y as longitudinal --------------------
  gmeter::begin(gmeter::Mount::Flat, false);
  feed(400, 0.0f, 0.0f, -1.0f);  // screen up, chip +Z down
  check("flat rest vertical", gmeter::reading().vertical, 0.0f, 0.02f);
  for (int i = 0; i < 150; i++) {
    gmeter::update(0.0f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 0.01f);
  }
  check("flat accel vertical", gmeter::reading().vertical, 0.5f, 0.04f);

  // ---- flip ----------------------------------------------------------
  gmeter::begin(gmeter::Mount::Upright, true);
  rest(400);
  for (int i = 0; i < 150; i++) {
    gmeter::update(0.0f, 1.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.01f);
  }
  check("flipped accel vertical", gmeter::reading().vertical, -0.5f, 0.04f);

  std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail,
              g_fail == 1 ? "" : "s");
  return g_fail ? 1 : 0;
}
