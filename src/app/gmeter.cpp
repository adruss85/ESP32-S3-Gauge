// =====================================================================
// gmeter.cpp - gravity removal, axis mapping and the 1 s average
//
// Why not simply subtract a fixed zero captured at boot?  A car changes
// attitude on its suspension, and the gauge may be powered up on a slope.
// A fixed zero would then show a permanent false acceleration.  The
// tracker below follows gravity whenever the car is genuinely still:
//
//   | |a| - 1 g | < 0.06 g   and   |gyro| < 0.2 rad/s
//   and (controller speed absent or < 1 km/h)
//
// Note that a tilt changes which axis gravity is on but not |a|.  An
// earlier version also required the dynamic vector to be small, so a large
// re-mount was never learned and the offset stayed forever.  The magnitude
// window catches real acceleration instead, and the controller's speed is
// the authoritative "moving" gate in the car.
//
// A 2 s time constant then absorbs slow pitch/roll changes, not a launch.
//
// The 2-axis magnitude is sqrt(vertical^2 + lateral^2), not
// (|vertical| + |lateral|) / 2: the latter understates a diagonal 1 g
// vector by about 29 percent.  It is averaged over a full second because
// the display answers "how hard was that corner/brake", not "what is the
// vibration at this instant".
// =====================================================================
#include "app/gmeter.h"

#include <math.h>
#include <string.h>

namespace gmeter {
namespace {

constexpr float kGravityTauS = 2.0f;    // slow tracker, 2 s
constexpr float kDynCutoffHz = 8.0f;    // vehicle dynamics low-pass
constexpr float kStillAccelG = 0.06f;   // | |a| - 1 g | window
constexpr float kStillGyroRads = 0.2f;  // about 11 deg/s
constexpr uint32_t kStillSamples = 50;  // 0.5 s before trusting "still"
constexpr size_t kPrimeSamples = 30;    // 0.3 s gravity average at boot
constexpr size_t kAverageSamples = 100; // 1.0 s at 100 Hz
constexpr float kDeadbandG = 0.01f;     // keep the blob from shivering

Mount g_mount = Mount::Upright;  // resolved map
bool g_mountAuto = false;
bool g_flip = false;

// Gravity estimate in chip axes.
float g_gravity[3] = {0, 0, 0};
float g_primeSum[3] = {0, 0, 0};
size_t g_primeCount = 0;
bool g_primeValid = false;

// Dynamic acceleration, low-passed, in chip axes.
float g_dyn[3] = {0, 0, 0};

// "Still" counter. Only a stationary car may update the gravity estimate.
uint32_t g_stillCount = 0;

// 1 s ring of magnitudes.
float g_ring[kAverageSamples] = {};
size_t g_ringHead = 0;
size_t g_ringCount = 0;
float g_ringSum = 0.0f;

bool g_offline = false;
bool g_haveSample = false;
float g_lastRaw[3] = {0, 0, 0};
float g_speedKph = 0.0f;
bool g_speedValid = false;
Reading g_reading = {0.0f, 0.0f, 0.0f, false};

float deadband(float v) {
  return fabsf(v) < kDeadbandG ? 0.0f : v;
}

void resetFilters() {
  memset(g_dyn, 0, sizeof(g_dyn));
  g_stillCount = 0;
}

void resetAverage() {
  memset(g_ring, 0, sizeof(g_ring));
  g_ringHead = 0;
  g_ringCount = 0;
  g_ringSum = 0.0f;
}

void publish(bool valid) {
  float vertical = g_mount == Mount::Upright ? -g_dyn[2] : -g_dyn[1];
  if (g_flip) vertical = -vertical;
  g_reading.vertical = deadband(vertical);
  g_reading.lateral = deadband(g_dyn[0]);
  g_reading.average = g_ringCount > 0 ? g_ringSum / (float)g_ringCount : 0.0f;
  g_reading.valid = valid && !g_offline;
}

}  // namespace

void begin(Mount mount, bool flip) {
  g_mountAuto = mount == Mount::Auto;
  g_mount = g_mountAuto ? Mount::Upright : mount;
  g_flip = flip;
  g_primeCount = 0;
  g_primeValid = false;
  memset(g_primeSum, 0, sizeof(g_primeSum));
  memset(g_gravity, 0, sizeof(g_gravity));
  memset(g_lastRaw, 0, sizeof(g_lastRaw));
  g_offline = false;
  g_haveSample = false;
  g_speedValid = false;
  g_speedKph = 0.0f;
  resetFilters();
  resetAverage();
  g_reading = Reading{0.0f, 0.0f, 0.0f, false};
}

void setMount(Mount mount, bool flip) {
  g_mountAuto = mount == Mount::Auto;
  g_mount = g_mountAuto ? Mount::Upright : mount;
  g_flip = flip;
  resetFilters();
  resetAverage();
  g_primeValid = false;
  g_primeCount = 0;
  memset(g_primeSum, 0, sizeof(g_primeSum));
  g_reading.valid = false;
}

Mount mount() { return g_mount; }

const Reading &reading() { return g_reading; }

void setVehicleSpeed(float kph, bool valid) {
  g_speedValid = valid && isfinite(kph);
  g_speedKph = g_speedValid ? kph : 0.0f;
}

void zero() {
  // Without a real sample this would fabricate a valid 0.00 g on a gauge
  // whose IMU never answered - exactly the fake-zero rule this project
  // forbids. Leave the "--" in place instead.
  if (!g_haveSample) {
    g_reading.valid = false;
    return;
  }

  // The current raw vector becomes gravity. This is the "car is level and
  // stationary" calibration; the slow tracker still corrects afterwards.
  memcpy(g_gravity, g_lastRaw, sizeof(g_gravity));
  g_primeValid = true;
  g_primeCount = kPrimeSamples;
  g_offline = false;
  resetFilters();
  resetAverage();

  // Present 0.00 immediately rather than a 1 s blank. A stationary car
  // produces no magnitude to average, so the zero stays a zero.
  g_ringCount = kAverageSamples;
  publish(true);
}

void invalidate() {
  if (g_offline) return;
  g_offline = true;
  g_haveSample = false;
  resetFilters();
  resetAverage();
  g_reading.valid = false;
}

void update(float ax, float ay, float az, float gx, float gy, float gz,
            float dt) {
  if (!isfinite(ax) || !isfinite(ay) || !isfinite(az)) return;
  if (!isfinite(dt) || dt <= 0.0f) dt = 0.01f;
  if (dt > 0.05f) dt = 0.05f;

  g_haveSample = true;
  g_lastRaw[0] = ax;
  g_lastRaw[1] = ay;
  g_lastRaw[2] = az;

  // Boot warm-up: average a few samples before calling anything gravity.
  if (!g_primeValid) {
    g_primeSum[0] += ax;
    g_primeSum[1] += ay;
    g_primeSum[2] += az;
    if (++g_primeCount >= kPrimeSamples) {
      for (size_t i = 0; i < 3; i++) {
        g_gravity[i] = g_primeSum[i] / (float)g_primeCount;
      }
      if (g_mountAuto) {
        // Gravity mostly on Y means the screen is upright; mostly on Z
        // means it is flat. A 45 degree mount is ambiguous and should use
        // the explicit setting instead.
        g_mount = fabsf(g_gravity[1]) > fabsf(g_gravity[2]) ? Mount::Upright
                                                           : Mount::Flat;
        g_mountAuto = false;
      }
      g_primeValid = true;
    }
    publish(false);
    return;
  }

  const float raw[3] = {ax, ay, az};

  // Dynamic acceleration = measurement - gravity, then low-pass.
  float dynRaw[3];
  for (size_t i = 0; i < 3; i++) dynRaw[i] = raw[i] - g_gravity[i];

  const float alphaDyn = 1.0f - expf(-2.0f * 3.14159265f * kDynCutoffHz * dt);
  for (size_t i = 0; i < 3; i++) {
    g_dyn[i] += (dynRaw[i] - g_dyn[i]) * alphaDyn;
  }

  const float total = sqrtf(ax * ax + ay * ay + az * az);
  const float gyroMag = sqrtf(gx * gx + gy * gy + gz * gz);

  // A stationary car can change attitude (a slope, a new mount) without
  // changing |a|: gravity only moves between axes.  The old test also
  // required the dynamic vector to be small, which meant a large tilt was
  // never re-learned - the gauge would keep a false offset forever.  The
  // magnitude window catches real acceleration instead, and the controller
  // speed gate makes that reliable in the car.
  const bool still = fabsf(total - 1.0f) < kStillAccelG &&
                     gyroMag < kStillGyroRads;

  // A valid controller speed is the best stationarity signal. If it says
  // the car is stopped, trust it even when engine vibration makes the IMU
  // "still" test fail; the 2 s average removes the vibration. With no speed
  // data the IMU gates above decide. This matters at boot: a gauge started
  // while the car is moving would otherwise keep the first attitude until
  // the car happened to be smooth enough for the IMU gate.
  const bool stationary = g_speedValid ? (g_speedKph < 1.0f) : still;
  if (stationary) {
    if (g_stillCount < kStillSamples) g_stillCount++;
  } else {
    g_stillCount = 0;
  }

  if (g_stillCount >= kStillSamples) {
    const float alphaG = 1.0f - expf(-dt / kGravityTauS);
    for (size_t i = 0; i < 3; i++) {
      g_gravity[i] += (raw[i] - g_gravity[i]) * alphaG;
    }
  }

  // The two monitored axes. Both current board orientations put the
  // longitudinal axis along the chip's +Z (upright) or +Y (flat), with
  // positive pointing away from the glass/top. Acceleration produces the
  // opposite sign, so the display negates it: blob up = acceleration.
  const float vertical = g_mount == Mount::Upright ? -g_dyn[2] : -g_dyn[1];
  const float lateral = g_dyn[0];
  const float v = deadband(g_flip ? -vertical : vertical);
  const float l = deadband(lateral);
  const float magnitude = sqrtf(v * v + l * l);

  if (g_offline) {
    g_offline = false;
    resetAverage();
  }

  g_ringSum -= g_ring[g_ringHead];
  g_ring[g_ringHead] = magnitude;
  g_ringSum += magnitude;
  g_ringHead = (g_ringHead + 1) % kAverageSamples;
  if (g_ringCount < kAverageSamples) g_ringCount++;

  publish(g_ringCount >= kAverageSamples);
}

}  // namespace gmeter
