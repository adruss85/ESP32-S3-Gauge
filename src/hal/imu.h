// =====================================================================
// imu.h - QMI8658 accelerometer + gyroscope on the control I2C bus
//
// The QMI8658 sits on the same SDA=11 / SCL=10 bus as the TCA9554 that
// resets the panel.  It is a 3.3 V part with two possible addresses; the
// board wires one of them, so begin() probes both.
//
// Only the accelerometer is used by the G-meter.  The gyro is read in the
// same burst because it costs no extra bus transaction and its magnitude
// is a useful "is the car actually still?" test for the gravity tracker.
// =====================================================================
#pragma once

#include <stdint.h>

namespace imu {

struct Sample {
  float ax, ay, az;  // g, chip axes
  float gx, gy, gz;  // rad/s, chip axes
};

// Probe, reset and configure the chip.  Wire must already be running
// (display::begin() starts it).  Returns false when no QMI8658 answers.
bool begin();

// True once begin() has found and configured a sensor.
bool ready();

// Read one sample.  The caller owns the timing; the sensor runs at 250 Hz
// and this is a single 12-byte burst, so 100 Hz is comfortably inside it.
bool read(Sample &out);

// 7-bit address that answered, or 0 when the probe failed.
uint8_t address();

// One-line description for the boot log, e.g. "QMI8658 at 0x6B".
const char *name();

}  // namespace imu
