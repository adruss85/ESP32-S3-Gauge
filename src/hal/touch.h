// =====================================================================
// touch.h - CST9217 capacitive touch on the AMOLED board
//
// The controller shares the I2C bus with the IMU, RTC and PMIC. It speaks a
// small command protocol rather than a register map:
//
//   0xD0 0x00              read the current point report
//   <15 bytes>            ... whose byte 6 is an ACK (0xAB in nibbles)
//   0xD0 0x00 0xAB        acknowledge, which re-arms the next report
//
// One finger is used. The point is fed to LVGL as a pointer indev so future
// widgets work, and the same stream is classified by hal/gesture.cpp into the
// three gestures the gauge has: a motionless hold (next colour scheme), a
// quick motionless tap (Haldex mode, on the Haldex screen only) and a
// horizontal swipe (next screen).
// =====================================================================
#pragma once

#include <stdint.h>

#include "hal/gesture.h"

namespace touch {

using Gesture = gesture::Gesture;

// Reset the controller, read its resolution and register the LVGL pointer.
// False (with a log line) if it does not answer; the gauge still runs.
bool begin();

// Poll the controller at up to ~66 Hz. The classified gesture, if any, is
// delivered to the handler set with setHandler().
void poll();

// One call per gesture. The handler runs from poll(), i.e. from loop().
void setHandler(void (*fn)(Gesture));

bool ready();

// Last mapped point, for the console 't' debug line.
void lastPoint(uint16_t &x, uint16_t &y, bool &pressed);

}  // namespace touch
