// =====================================================================
// imu.cpp - QMI8658 accelerometer + gyroscope driver
//
// Register map and sequence follow the QMI8658A datasheet and the
// ESPHome QMI8658 component:
//
//   0x00 WHO_AM_I   0x05
//   0x02 CTRL1      0x40 = register auto-increment, little-endian
//   0x03 CTRL2      accel range/ODR
//   0x04 CTRL3      gyro range/ODR
//   0x06 CTRL5      0x00 = no chip low-pass, filtering is in software
//   0x08 CTRL7      0x03 = accelerometer + gyroscope on
//   0x35 AX_L       ... 0x40 GZ_H, 12 bytes little-endian
//   0x60 RESET      0xB0
//
// The chip's internal low-pass is left off on purpose: the G-meter wants
// one predictable filter it can tune, not a hidden second one.  The
// accelerometer runs at 250 Hz / +/-4 g, which covers a road car (hard
// braking is about 1 g, a kerb strike is the reason for the 4 g range)
// with 0.12 mg per LSB.
// =====================================================================
#include "hal/imu.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "board_config.h"

namespace imu {
namespace {

constexpr uint8_t kRegWhoAmI = 0x00;
constexpr uint8_t kRegCtrl1 = 0x02;
constexpr uint8_t kRegCtrl2 = 0x03;
constexpr uint8_t kRegCtrl3 = 0x04;
constexpr uint8_t kRegCtrl5 = 0x06;
constexpr uint8_t kRegCtrl7 = 0x08;
constexpr uint8_t kRegAxL = 0x35;
constexpr uint8_t kRegReset = 0x60;

constexpr uint8_t kWhoAmIValue = 0x05;
constexpr uint8_t kResetCommand = 0xB0;

// CTRL2: +/-4 g (bits 6:4 = 001) and 250 Hz (bits 3:0 = 0101).
constexpr uint8_t kAccel4g250 = 0x10 | 0x05;
// CTRL3: +/-256 dps (bits 6:4 = 100) and 250 Hz.
constexpr uint8_t kGyro256dps250 = 0x40 | 0x05;

// 4 g across a signed 16-bit word.
constexpr float kAccelGPerLsb = 4.0f / 32768.0f;
// 256 dps across a signed 16-bit word, converted to rad/s.
constexpr float kGyroRadPerLsb = (256.0f / 32768.0f) * 0.01745329252f;

uint8_t g_addr = 0;
bool g_ready = false;
uint32_t g_errors = 0;

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(g_addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(g_addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)g_addr, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)Wire.read();
  return true;
}

bool probe(uint8_t addr) {
  g_addr = addr;
  uint8_t id = 0;
  if (!readRegs(kRegWhoAmI, &id, 1)) return false;
  if (id != kWhoAmIValue) {
    Serial.printf("[imu] 0x%02X answered 0x%02X (not a QMI8658)\n", addr, id);
    return false;
  }
  return true;
}

}  // namespace

bool begin() {
  g_ready = false;
  g_addr = 0;

  // 0x6B is what both this AMOLED board and the 1.85 board answer on; 0x6A
  // is the SA0-low alternative. Try the one that is fitted first: probing a
  // missing address makes the I2C driver log an error before the fallback,
  // which reads like a fault in a boot log that is otherwise clean.
  if (!probe(BOARD_IMU_ADDR_B) && !probe(BOARD_IMU_ADDR_A)) {
    g_addr = 0;
    Serial.println("[imu] no QMI8658 at 0x6A or 0x6B");
    return false;
  }

  if (!writeReg(kRegReset, kResetCommand)) {
    Serial.printf("[imu] reset write failed at 0x%02X\n", g_addr);
    g_addr = 0;
    return false;
  }
  delay(20);

  if (!writeReg(kRegCtrl1, 0x40) || !writeReg(kRegCtrl2, kAccel4g250) ||
      !writeReg(kRegCtrl3, kGyro256dps250) ||
      !writeReg(kRegCtrl5, 0x00) || !writeReg(kRegCtrl7, 0x03)) {
    Serial.printf("[imu] configuration failed at 0x%02X\n", g_addr);
    g_addr = 0;
    return false;
  }

  // The first sample after enable is not valid; give the chip a few ODRs.
  delay(20);
  g_errors = 0;
  g_ready = true;
  Serial.printf("[imu] QMI8658 at 0x%02X, accel +/-4 g 250 Hz, gyro 256 dps\n",
                g_addr);
  return true;
}

bool ready() { return g_ready; }
uint8_t address() { return g_addr; }
const char *name() { return "QMI8658"; }

bool read(Sample &out) {
  if (!g_ready) return false;

  uint8_t raw[12];
  if (!readRegs(kRegAxL, raw, sizeof(raw))) {
    g_errors++;
    if (g_errors == 1) Serial.println("[imu] read failed");
    return false;
  }
  g_errors = 0;

  const int16_t ax = (int16_t)((uint16_t)raw[1] << 8 | raw[0]);
  const int16_t ay = (int16_t)((uint16_t)raw[3] << 8 | raw[2]);
  const int16_t az = (int16_t)((uint16_t)raw[5] << 8 | raw[4]);
  const int16_t gx = (int16_t)((uint16_t)raw[7] << 8 | raw[6]);
  const int16_t gy = (int16_t)((uint16_t)raw[9] << 8 | raw[8]);
  const int16_t gz = (int16_t)((uint16_t)raw[11] << 8 | raw[10]);

  out.ax = (float)ax * kAccelGPerLsb;
  out.ay = (float)ay * kAccelGPerLsb;
  out.az = (float)az * kAccelGPerLsb;
  out.gx = (float)gx * kGyroRadPerLsb;
  out.gy = (float)gy * kGyroRadPerLsb;
  out.gz = (float)gz * kGyroRadPerLsb;
  return true;
}

}  // namespace imu
