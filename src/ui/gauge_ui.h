#pragma once

#include "net/ohx_now.h"
#include "theme.h"

namespace gauge_ui {

// Build the gauge on the active LVGL screen.
void create();

// Push the latest telemetry + link stats into the widgets. Safe to call every ~50 ms.
void update(const ohxnow::Telemetry &t, const ohxnow::Stats &s);

// Accent colour control, driven by the BOOT key, the cruise stalk or the
// web page. Both entry points persist the new scheme in NVS; setScheme()
// takes persist=false for the capture sweep, which must not change settings.
void nextScheme();
void setScheme(theme::Scheme s, bool persist = true);

// Show/hide the whole gauge and fade it. The dashboard uses these during
// the G-meter transition; opacity is per object because a container layer
// would not fit the LVGL pool.
void setVisible(bool visible);
void setOpacity(uint8_t opacity);

// Readout scale in tenths of a PSI, e.g. 0..250 for the stock 0-25 PSI arc.
// Re-labels the six ticks, re-ranges the arc and redraws the value on the
// next update(). Returns false (and changes nothing) if the range is not
// usable: a minimum at or above the maximum, or a span under 5 PSI.
bool applyScale(int16_t min10, int16_t max10);

}  // namespace gauge_ui
