// =====================================================================
// gmeter_ui.h - the G-meter screen: blob, 1 s tail, average readout
//
// The blob is a 50 px circle in the active accent colour.  It moves only
// vertically: acceleration up, braking down.  A 1 s history of positions
// is drawn as a fading tail of smaller circles.  The white number at the
// bottom is the 1 s average of sqrt(vertical^2 + lateral^2).
//
// The dashboard owns visibility and the mode fade; this module only draws
// the screen and its own master opacity.
// =====================================================================
#pragma once

#include "app/gmeter.h"

namespace gmeter_ui {

// Build the screen on the active LVGL display. The root starts hidden.
void create();

void setVisible(bool visible);

// 0..255. Scales the blob, tail and readout together for the mode fade.
void setOpacity(uint8_t opacity);

// Repaint the blob and, at 20 Hz, the tail and the readout.
void update(const gmeter::Reading &r);

// Re-colour the blob and tail after a scheme change.
void applyScheme();

}  // namespace gmeter_ui
