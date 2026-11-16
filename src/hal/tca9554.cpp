#include "hal/tca9554.h"

#include <Arduino.h>
#include <Wire.h>

namespace tca9554 {
namespace {

constexpr uint8_t kRegInput = 0x00;
constexpr uint8_t kRegOutput = 0x01;
constexpr uint8_t kRegConfig = 0x03;

uint8_t g_addr = 0;
bool g_ready = false;

bool writeReg(uint8_t reg, uint8_t value) {
  if (!g_addr) return false;
  Wire.beginTransmission(g_addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readReg(uint8_t reg, uint8_t &value) {
  if (!g_addr) return false;
  Wire.beginTransmission(g_addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)g_addr, 1) != 1) return false;
  value = (uint8_t)Wire.read();
  return true;
}

}  // namespace

bool begin(uint8_t addr) {
  g_addr = addr;
  g_ready = false;

  Wire.beginTransmission(addr);
  if (Wire.endTransmission() != 0) {
    Serial.printf("[tca9554] no device at 0x%02X\n", addr);
    g_addr = 0;
    return false;
  }

  // All pins outputs, all low. The vendor example does the same at boot.
  if (!writeReg(kRegConfig, 0x00) || !writeReg(kRegOutput, 0x00)) {
    Serial.println("[tca9554] register write failed");
    g_addr = 0;
    return false;
  }

  g_ready = true;
  Serial.printf("[tca9554] ready at 0x%02X\n", addr);
  return true;
}

bool write(uint8_t pin, bool high) {
  if (!g_ready || pin > 7) return false;
  uint8_t output = 0;
  if (!readReg(kRegOutput, output)) return false;
  if (high) {
    output |= (uint8_t)(1u << pin);
  } else {
    output &= (uint8_t)~(1u << pin);
  }
  return writeReg(kRegOutput, output);
}

bool read(uint8_t pin) {
  if (!g_ready || pin > 7) return false;
  uint8_t input = 0;
  if (!readReg(kRegInput, input)) return false;
  return (input >> pin) & 1;
}

}  // namespace tca9554
