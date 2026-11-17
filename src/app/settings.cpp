// =====================================================================
// settings.cpp - NVS-backed display preferences
//
// One Preferences namespace ("gauge") holds the accent scheme, boost scale,
// display mode and G-meter mount. Writes are rare (a BOOT press or a web
// form), so the roughly 15 ms flash stall they cause is acceptable; reads
// happen once at boot.
// =====================================================================
#include "app/settings.h"

#include <Arduino.h>
#include <Preferences.h>

namespace settings {
namespace {

constexpr char kNamespace[] = "gauge";
constexpr char kKeyScheme[] = "scheme";
constexpr char kKeyMin[] = "smin";
constexpr char kKeyMax[] = "smax";
constexpr char kKeyMode[] = "mode";
constexpr char kKeyMount[] = "gmount";
constexpr char kKeyFlip[] = "gflip";

Values g_values = {0, 0, 250, 0, 0, 0};

Preferences g_prefs;
bool g_open = false;

bool openPrefs() {
  if (g_open) return true;
  g_open = g_prefs.begin(kNamespace, false);
  if (!g_open) Serial.println("[set] NVS open failed; values will not persist");
  return g_open;
}

bool validScale(int16_t min10, int16_t max10) {
  if (min10 < kMinLimit10 || max10 > kMaxLimit10) return false;
  if (min10 >= max10) return false;
  const int16_t span = (int16_t)(max10 - min10);
  return span >= kSpanMin10 && span <= kSpanMax10;
}

// A failed write only costs persistence, so it is logged and ignored rather
// than propagated: the in-RAM value stays authoritative for this boot.
void store(const char *key, int32_t value) {
  if (!openPrefs()) return;
  if (g_prefs.putInt(key, value) == 0) {
    Serial.printf("[set] %s=%ld not persisted (NVS full?)\n", key, (long)value);
  }
}

}  // namespace

void begin() {
  if (!openPrefs()) {
    g_values = Values{0, 0, 250, 0, 0, 0};
    return;
  }

  // Reads must match store()'s putInt(): Preferences::getUChar() rejects a
  // four-byte record and returns the default, which silently reset the
  // scheme to green while the scale survived.
  const int32_t scheme = g_prefs.getInt(kKeyScheme, 0);
  const int32_t min10 = g_prefs.getInt(kKeyMin, 0);
  const int32_t max10 = g_prefs.getInt(kKeyMax, 250);
  const int32_t mode = g_prefs.getInt(kKeyMode, 0);
  const int32_t mount = g_prefs.getInt(kKeyMount, 0);
  const int32_t flip = g_prefs.getInt(kKeyFlip, 0);

  if (scheme >= 0 && scheme <= 7) {
    g_values.scheme = (uint8_t)scheme;
  }
  if (validScale((int16_t)min10, (int16_t)max10)) {
    g_values.min10 = (int16_t)min10;
    g_values.max10 = (int16_t)max10;
  } else if (min10 != 0 || max10 != 250) {
    Serial.printf("[set] stored scale %ld..%ld rejected; using 0..25\n",
                  (long)min10, (long)max10);
  }
  if (mode >= 0 && mode <= 1) g_values.mode = (uint8_t)mode;
  if (mount >= 0 && mount <= 2) g_values.mount = (uint8_t)mount;
  if (flip >= 0 && flip <= 1) g_values.gflip = (uint8_t)flip;

  Serial.printf("[set] scheme %u, scale %.1f..%.1f PSI\n", (unsigned)g_values.scheme,
                (double)g_values.min10 / 10.0, (double)g_values.max10 / 10.0);
  const char *mountName = g_values.mount == 2   ? "auto"
                          : g_values.mount == 1 ? "flat"
                                                : "upright";
  Serial.printf("[set] mode %s, mount %s, flip %u\n",
                g_values.mode ? "g-meter" : "gauge", mountName,
                (unsigned)g_values.gflip);
}

const Values &get() { return g_values; }

bool setScheme(uint8_t scheme) {
  if (scheme > 7) return false;
  if (scheme == g_values.scheme) return true;
  g_values.scheme = scheme;
  store(kKeyScheme, scheme);
  return true;
}

bool setScale(int16_t min10, int16_t max10) {
  if (!validScale(min10, max10)) return false;
  if (min10 == g_values.min10 && max10 == g_values.max10) return true;
  g_values.min10 = min10;
  g_values.max10 = max10;
  store(kKeyMin, min10);
  store(kKeyMax, max10);
  return true;
}

bool setMode(uint8_t mode) {
  if (mode > 1) return false;
  if (mode == g_values.mode) return true;
  g_values.mode = mode;
  store(kKeyMode, mode);
  return true;
}

bool setMount(uint8_t mount, uint8_t flip) {
  if (mount > 2 || flip > 1) return false;
  if (mount == g_values.mount && flip == g_values.gflip) return true;
  g_values.mount = mount;
  g_values.gflip = flip;
  store(kKeyMount, mount);
  store(kKeyFlip, flip);
  return true;
}

}  // namespace settings
