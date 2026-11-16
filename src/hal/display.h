#pragma once

#include <stdint.h>

namespace display {

// Bring up the TCA9554, reset the panel, start QSPI, and attach LVGL to it.
bool begin();

// Call from loop(): runs LVGL's timer handler.
void loop();

void setBacklight(uint8_t percent);

bool ready();

}  // namespace display
