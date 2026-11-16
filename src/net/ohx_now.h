#pragma once
#include <stdint.h>

namespace ohxnow {

struct Stats {
  uint32_t raw;            // every ESP-NOW packet, before validation
  uint32_t statusPackets;  // OHX_STATUS accepted (boost source)
  uint32_t canPackets;     // CANN packets accepted (IAT source)
  uint32_t frames;         // CANN frames inside those packets
  uint32_t rejects;        // bad magic / type / proto / layout
  uint32_t senders;        // distinct source MACs heard
  uint32_t statusAgeMs;    // since the last OHX_STATUS (UINT32_MAX = never)
  uint32_t canAgeMs;       // since the last CANN packet (UINT32_MAX = never)
  int16_t rssi;            // of the last accepted packet
  uint8_t channel;         // channel the radio is on
  uint8_t src[6];          // last accepted sender
  bool enabled;
};

// What the gauge draws. Absent values are NaN, never zero.
struct Telemetry {
  bool statusValid;   // a fresh OHX_STATUS has arrived
  float boostPsi;     // mbar above ambient -> psi
  float boostBar;     // mbar above ambient -> bar
  float rpm;
  float speed;
  float throttle;
  uint8_t mode;
  uint8_t haldexState;
  uint8_t seq;
  char name[25];      // controller's AP SSID

  bool iatValid;      // a fresh Engine_3 (0x380) frame has arrived
  float iatC;         // intake air temperature, degrees C
};

// Initialise WiFi + ESP-NOW and lock onto the controller's AP channel.
bool begin();

// Call from loop(): retries begin(), drains diagnostics, scans channels,
// prints the link summary.
void poll();

bool enabled();
Stats stats();
Telemetry telemetry();

}  // namespace ohxnow
