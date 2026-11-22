// =====================================================================
// settings.h - the few things the owner can change without a rebuild
//
// The accent scheme, boost scale and display mode are editable from the web
// page and from the BOOT key, and must survive a power cycle, so they live
// in NVS.  The G-meter mount is also stored: a gauge that is flipped 180
// degrees during installation should not need a rebuild.
//
// The controller's access-point password lives here too: it is what signs a
// Haldex mode request from the drive face. It is the one value that is a
// secret, so it has its own accessors and never travels in Values.
//
// Values are plain integers: this module deliberately does not include
// theme.h, LVGL or the IMU driver, so the headless esp32dev build can use it
// too. The caller maps scheme to theme::Scheme and mount to gmeter::Mount.
// =====================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace settings {

struct Values {
  uint8_t scheme;   // index into theme::Scheme, 0 = Midnight
  int16_t min10;    // scale minimum, tenths of a PSI
  int16_t max10;    // scale maximum, tenths of a PSI
  uint8_t mode;     // 0 = boost gauge, 1 = oil temperature, 2 = G-meter
  uint8_t mount;    // 0 = upright, 1 = flat, 2 = auto from gravity
  uint8_t gflip;    // 1 = invert the accel/brake direction
  int16_t omin10;   // oil temperature scale minimum, tenths of a degree C
  int16_t omax10;   // oil temperature scale maximum, tenths of a degree C
  int16_t omin10p;  // oil pressure scale minimum, tenths of a PSI
  int16_t omax10p;  // oil pressure scale maximum, tenths of a PSI
  int16_t cmin10;   // coolant scale minimum, tenths of a degree C
  int16_t cmax10;   // coolant scale maximum, tenths of a degree C
};

// Load from NVS and validate. Safe to call once from setup(); a missing or
// corrupt record falls back to Midnight / 0-25 PSI / boost gauge / upright.
// A record written by the 1.85 build is migrated to the seven-scheme order.
void begin();

const Values &get();

// How many schemes theme.h defines. Updated with the palette table; kept here
// so this module and the headless build do not have to include LVGL.
inline constexpr uint8_t kSchemeCount = 7;

// Persist a scheme index. Out-of-range indexes are rejected.
bool setScheme(uint8_t scheme);

// Persist a scale. Rejected unless it is inside the limits below and the
// span is wide enough for six readable labels.
bool setScale(int16_t min10, int16_t max10);

// Persist the oil temperature scale, in tenths of a degree C. The accepted
// window is a realistic oil-temperature band; six readable labels need a
// span of at least 2 degrees.
bool setOilScale(int16_t min10, int16_t max10);

// Persist the display mode (0 gauge, 1 G-meter).
bool setMode(uint8_t mode);

// Persist the G-meter mount (0 upright, 1 flat, 2 auto) and direction flip.
bool setMount(uint8_t mount, uint8_t flip);

// The accepted window. A gauge that reads boost only needs 0-25, but the
// controller reports vacuum as a negative mbar value, so the minimum may go
// below zero and the readout has to draw it.
inline constexpr int16_t kMinLimit10 = -150;  // -15.0 PSI
inline constexpr int16_t kMaxLimit10 = 500;   //  50.0 PSI
inline constexpr int16_t kSpanMin10 = 50;     //   5.0 PSI
inline constexpr int16_t kSpanMax10 = 600;    //  60.0 PSI

// Oil temperature span minimums, tenths of a degree C.
inline constexpr int16_t kOilSpanMin10 = 20;  //   2.0 °C

// Oil pressure span minimums, tenths of a PSI.
inline constexpr int16_t kOilPressSpanMin10 = 10;  //   1.0 PSI

// Coolant temperature span minimums, tenths of a degree C.
inline constexpr int16_t kCoolantSpanMin10 = 100;  //  10.0 °C

// Persist the oil pressure scale, in tenths of a PSI. The accepted
// window is a realistic oil-pressure band; six readable labels need a
// span of at least 1 PSI.
bool setOilPressScale(int16_t min10, int16_t max10);

// Persist the coolant scale, in tenths of a degree C.
bool setCoolantScale(int16_t min10, int16_t max10);

// The OpenHaldex controller's access-point password. It exists for one
// reason: a Haldex mode request is signed with HMAC-SHA256 keyed on it, so
// the gauge cannot ask for a mode without knowing it. An empty string is the
// key for an open access point, which is a key and not the absence of one.
//
// It is stored in NVS beside the display settings, never printed and never
// included in the status JSON. Set it on the setup page.
inline constexpr size_t kControllerKeyMax = 63;  // WPA2 passphrase limit
const char *controllerKey();
bool setControllerKey(const char *key);

}  // namespace settings
