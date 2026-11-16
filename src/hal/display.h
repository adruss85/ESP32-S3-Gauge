#pragma once

#include <stdint.h>

namespace display {

// Bring up the TCA9554, reset the panel, start QSPI, and attach LVGL to it.
bool begin();

// Call from loop(): runs LVGL's timer handler.
void loop();

void setBacklight(uint8_t percent);

// Follow the car's dash dimmer: Terminal 58s, 0 = day, 7..99 = night dimmer.
// -1 means not heard, so the last brightness is held.
void applyDimmer(int term58s);

bool ready();

}  // namespace display
