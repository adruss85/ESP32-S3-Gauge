#pragma once

#include "net/ohx_now.h"
#include "theme.h"

namespace gauge_ui {

// Build the gauge on the active LVGL screen.
void create();

// Push the latest telemetry + link stats into the widgets. Safe to call every ~50 ms.
void update(const ohxnow::Telemetry &t, const ohxnow::Stats &s);

// Colour scheme, applied to this module only. The cycling and the NVS write
// live in dashboard::nextScheme(); this is the leaf that repaints, so there is
// one place that steps the scheme and one place that persists it.
// persist=false is the capture sweep, which must not change settings.
void setScheme(theme::Scheme s, bool persist = true);

// Show/hide the whole gauge and fade it. The dashboard uses these during
// the G-meter transition; opacity is per object because a container layer
// would not fit the LVGL pool.
void setVisible(bool visible);
void setOpacity(uint8_t opacity);

// Horizontal offset in pixels, used to slide the gauge in and out during a
// swipe. 0 is the resting position.
void setSlide(int dx);
int slide();

// Readout scale in tenths of a PSI, e.g. 0..250 for the stock 0-25 PSI arc.
// Re-labels the six ticks, re-ranges the arc and redraws the value on the
// next update(). Returns false (and changes nothing) if the range is not
// usable: a minimum at or above the maximum, or a span under 5 PSI.
bool applyScale(int16_t min10, int16_t max10);

}  // namespace gauge_ui
