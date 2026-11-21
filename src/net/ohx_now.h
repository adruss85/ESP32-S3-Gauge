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

  // Terminal 58s from Cluster_2 (0x420): 0 = lights off/day, 7..99 = the
  // dash dimmer at night, -1 = never heard. The S3-dash rule uses 58s for
  // day/night and brightness; 58d is non-zero with the lights off, so it is
  // deliberately not used.
  int term58s;
  bool oilTempValid;  // a fresh Cluster_2 (0x420) frame has arrived
  float oilTempC;     // engine oil temperature, deg C

  // Coolant temperature from Cluster_2 (0x420) byte 4.
  bool coolantValid;    // a fresh Cluster_2 (0x420) frame has arrived
  float coolantC;       // engine coolant temperature, deg C

  // Oil pressure from Engine_7 (0x588) bytes 5-6 on a patched ME7 (PSI,
  // 0.1 PSI resolution).
  bool oilPressValid;    // a fresh Engine_7 (0x588) frame has arrived
  float oilPressPsi;     // oil pressure in PSI (tenths * 0.1)

  // Wheel slip from OHX_STATUS: FL, FR, RL, RR in percent.
  // -128 (OHX_NONE8) means no value. Positive = wheel faster than reference (TC),
  // negative = wheel slower (ABS).
  bool slipValid;
  int8_t slip[4];

  // Haldex lock actual from OHX_STATUS (0-100%, OHX_NONE8 = no value).
  bool lockActualValid;
  uint8_t lockActual;

  // Haldex lock target from OHX_STATUS (0-100%, requested lock).
  uint8_t lockTarget;
};

// Initialise WiFi + ESP-NOW and lock onto the controller's AP channel.
bool begin();

// Call from loop(): retries begin(), drains diagnostics, scans channels,
// prints the link summary.
void poll();

bool enabled();
Stats stats();
Telemetry telemetry();

// Channel the radio is on. The setup AP is started on this channel because
// AP+STA share one radio; there is no second channel to give the page.
uint8_t channel();

// Highest channel the radio may tune to. The softAP's country policy caps
// this (the default world-safe setting allows 1-11, so 12/13 fail once the
// AP is up). gauge_web reports the real limit after starting the AP.
void setChannelLimit(uint8_t maxChannel);

// The setup AP is up. A softAP has to stay on one channel to be joinable,
// so this disables the channel walk; the periodic full scan still finds the
// controller's AP.
void setSetupAp(bool up);

// One-shot: GRA_Cancel + GRA_Down on the PQ cruise stalk (GRA_New, 0x38A)
// were pressed together. Consumed by loop() to advance the accent scheme.
bool takeSchemeEvent();

// One-shot: GRA_Down_Long on the PQ cruise stalk without GRA_Cancel. This is
// the cruise-stalk equivalent of the BOOT 700 ms hold and toggles the screen.
bool takeModeEvent();

}  // namespace ohxnow
