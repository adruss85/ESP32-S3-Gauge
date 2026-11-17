// =====================================================================
// splash.h - the three-second boot screen
// =====================================================================
#pragma once

namespace splash {

// Build the splash on top of the current screen and decode its picture.
// Call after dashboard::begin() so the splash is the foreground root.
void begin();

// Advance the green loading rule and remove the splash at three seconds.
// Call every loop before display::loop().
void update();

// True while the splash covers the gauge; the dashboard skips widget
// updates then, because drawing underneath it only costs flushes.
bool active();

}  // namespace splash
