#pragma once
#include <stdint.h>

namespace config {

inline constexpr char kVersion[] = "0.2.0-gauge";

// The family fallback channel (bodycan uses the same one when no AP is up).
inline constexpr uint8_t kFallbackChannel = 1;
inline constexpr uint8_t kMaxChannel = 13;

// The controller is the preferred anchor; the dash AP is the backup.
inline constexpr char kAnchorSSID[] = "ohx-dash";
inline constexpr char kAnchorSSIDAlt[] = "OpenHaldex-C6";

// Listen on the current channel for a moment before looking around.
inline constexpr uint32_t kInitialDwellMs = 2000;
inline constexpr uint32_t kChannelDwellMs = 400;

// If nothing arrives for this long, look for the anchor again.
inline constexpr uint32_t kQuietBeforeRescanMs = 15000;
inline constexpr uint32_t kRescanMs = 15000;

// Console link summary period.
inline constexpr uint32_t kStatusIntervalMs = 5000;

}  // namespace config
