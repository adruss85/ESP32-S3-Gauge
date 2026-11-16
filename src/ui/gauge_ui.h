#pragma once

#include "net/ohx_now.h"
#include "theme.h"

namespace gauge_ui {

// Build the gauge on the active LVGL screen.
void create();

// Push the latest telemetry into the widgets. Safe to call every ~50 ms.
void update(const ohxnow::Telemetry &t);

// Accent colour control, driven by the BOOT key.
void nextScheme();
void setScheme(theme::Scheme s);

}  // namespace gauge_ui
