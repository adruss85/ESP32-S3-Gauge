// =====================================================================
// gmeter.h - acceleration math for the G-meter screen
//
// The display shows one vertical axis (acceleration / braking) and a
// 1-second average of the magnitude of the two horizontal axes.  Gravity
// is not "assumed zero": a slow tracker learns the gravity vector while the
// car is still, and only freezes it while the car is accelerating.  That is
// the standard complementary-filter approach and avoids the two common
// errors: showing a constant offset on a slope, and letting a sustained
// pull decay away as a high-pass filter would.
//
// The two axes are selected by mount:
//   Upright: screen faces the driver.  Longitudinal = chip Z (screen
//            normal), lateral = chip X.  Gravity sits on chip Y.
//   Flat:    screen faces up, top of the glass points forward.
//            Longitudinal = chip Y, lateral = chip X.  Gravity sits on Z.
//
// This module is pure math: it does not include the IMU or LVGL, so the
// headless esp32dev build compiles it too.
// =====================================================================
#pragma once

#include <stdint.h>

namespace gmeter {

enum class Mount : uint8_t {
  Upright = 0,
  Flat = 1,
  Auto = 2,  // resolve Upright/Flat from the learned gravity vector
};

struct Reading {
  float vertical;  // g, positive = acceleration (blob moves up)
  float lateral;   // g, second monitored axis
  float average;   // 1 s mean of sqrt(vertical^2 + lateral^2), g
  bool valid;      // a full average window has been collected
};

// Reset the filters. mount/flip normally come from NVS.
void begin(Mount mount, bool flip);

// Change the axis map without losing the gravity estimate. Auto waits for
// the next gravity prime and then chooses Upright or Flat.
void setMount(Mount mount, bool flip);

// The resolved mount (never Auto). Useful for the boot log and status.
Mount mount();

// One 100 Hz sample: accelerometer in g, gyro in rad/s, dt in seconds.
void update(float ax, float ay, float az, float gx, float gy, float gz,
            float dt);

const Reading &reading();

// The controller's road speed, when available. A G-meter must not learn
// gravity while the car is moving: a long 0.05 g pull would otherwise be
// slowly absorbed into the gravity vector. With a valid speed above
// 1 km/h the tracker freezes; with no speed data the IMU "still" test is
// the fallback (bench use, controller off).
void setVehicleSpeed(float kph, bool valid);

// Treat the current sample as zero. Press this with the car stationary.
void zero();

// The sensor stopped answering; hide the blob and the average until it
// returns. The learned gravity vector is kept.
void invalidate();

}  // namespace gmeter
