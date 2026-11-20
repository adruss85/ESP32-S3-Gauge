// =====================================================================
// drive_ui.h - drivetrain visualisation face
//
// Shows: RPM, wheel speeds + slip %, Haldex target/actual, ABS/TC flags.
// Layout adapted from the HTML prototype for 466x466 round.
// =====================================================================
#pragma once

#include <lvgl.h>
#include "theme.h"
#include "net/ohx_now.h"

namespace drive_ui {

void begin();
void update(const ohxnow::Telemetry &t, const ohxnow::Stats &s);
void setScheme(theme::Scheme s, bool persist);
void setVisible(bool visible);
void setOpacity(uint8_t opacity);
void setSlide(int dx);
int slide();
lv_obj_t *root();

}  // namespace drive_ui