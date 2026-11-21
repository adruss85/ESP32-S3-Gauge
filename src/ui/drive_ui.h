#pragma once
// =====================================================================
// drive_ui.h - drivetrain visualisation face
//
// Graphical view showing engine RPM, Haldex pack with actual fill %
// and request line, chassis outline with front/rear drive lines,
// diffs, and four wheels with animated treads.
// Only text values: RPM and Haldex actual %.
// =====================================================================
#include <stdint.h>
#include "lvgl.h"
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