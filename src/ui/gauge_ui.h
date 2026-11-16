#pragma once

#include "net/ohx_now.h"

namespace gauge_ui {

// Build the gauge on the active LVGL screen.
void create();

// Push the latest telemetry into the widgets. Safe to call every ~50 ms.
void update(const ohxnow::Telemetry &t);

}  // namespace gauge_ui
