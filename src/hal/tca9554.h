// =====================================================================
// tca9554 - minimal driver for the board's I2C I/O expander
//
// The Waveshare 1.85" board does not wire the LCD reset to a GPIO: it is
// bit 1 of a TCA9554 at 0x20. Only the output register and the direction
// register are needed here.
// =====================================================================
#pragma once

#include <stdint.h>

namespace tca9554 {

// Probe the expander and set every pin to output, all low.
bool begin(uint8_t addr);

// Drive one pin high or low. Pin is 0..7.
bool write(uint8_t pin, bool high);

// Read the input register bit.
bool read(uint8_t pin);

}  // namespace tca9554
