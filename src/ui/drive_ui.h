#pragma once
// =====================================================================
// drive_ui.h - the Haldex face: mode, lock request, lock actual
//
// Four lines of type on the round glass and one gesture: a tap asks the
// controller for the next mode. The drivetrain drawing this replaced was
// unreadable in the car, and the numbers it was made of are the three the
// driver actually uses.
// =====================================================================
#include <stdint.h>

#include "lvgl.h"
#include "net/ohx_now.h"
#include "theme.h"

namespace drive_ui {

void begin();
void update(const ohxnow::Telemetry &t, const ohxnow::Stats &s);

// A tap on this face: ask the controller for the next Haldex mode. Sends
// nothing unless a fresh OHX_STATUS has been heard and the controller says it
// accepts commands; the result is drawn in the sub-line.
void onTap();

void setScheme(theme::Scheme s, bool persist);
void setVisible(bool visible);
void setOpacity(uint8_t opacity);
void setSlide(int dx);
int slide();
lv_obj_t *root();

}  // namespace drive_ui
