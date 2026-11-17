#pragma once
#include <stdint.h>

#include "version.h"

namespace config {

inline constexpr char kVersion[] = OHX_GAUGE_VERSION;

// The family fallback channel (bodycan uses the same one when no AP is up).
inline constexpr uint8_t kFallbackChannel = 1;
inline constexpr uint8_t kMaxChannel = 13;

// The controller is the preferred anchor; the dash AP is the backup. The dash
// renamed its AP to "CANv-dash" in 0.9.3; scan for that first and keep the old
// name so a gauge can still find a dash that has not been updated.
inline constexpr char kAnchorSSID[] = "CANv-dash";
inline constexpr char kAnchorSSIDOld[] = "ohx-dash";
inline constexpr char kAnchorSSIDAlt[] = "OpenHaldex-C6";

// The gauge's own setup AP. It shares the single radio with ESP-NOW, so it
// is started on whatever channel the receiver is locked to. Open by default:
// the page is the owner's tool, not a public service, and the AP is only in
// range for a few metres. Define GAUGE_AP_PASSWORD to require WPA2:
//   -DGAUGE_AP_PASSWORD=\"your-password\"
inline constexpr char kApSsid[] = "CANv-gauge";
#ifdef GAUGE_AP_PASSWORD
inline constexpr char kApPassword[] = GAUGE_AP_PASSWORD;
#else
inline constexpr char kApPassword[] = "";
#endif
// Two is enough for a phone and a laptop; each associated station costs RAM
// the ESP-NOW receive path also needs.
inline constexpr uint8_t kApMaxClients = 2;

// Listen on the current channel for a moment before looking around.
inline constexpr uint32_t kInitialDwellMs = 2000;
inline constexpr uint32_t kChannelDwellMs = 400;

// If nothing arrives for this long, look for the anchor again.
inline constexpr uint32_t kQuietBeforeRescanMs = 15000;
inline constexpr uint32_t kRescanMs = 15000;

// Console link summary period.
inline constexpr uint32_t kStatusIntervalMs = 5000;

}  // namespace config
