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

  // Controller flags (OHX_STATUS.flags[]). The drive face needs them: whether
  // the coupling is doing anything, and whether the controller would accept a
  // mode request at all.
  bool standalone;    // OHX_F0_STANDALONE
  bool haldexCan;     // OHX_F0_HALDEX_CAN: lockActual is a real reading
  bool ctrlAccept;    // OHX_F2_CONTROL: commands over ESP-NOW are accepted
  bool keyed;         // OHX_F2_KEY: its AP has a password, so commands sign
  bool couplingOpen;  // OHX_F1_COUPLING_OPEN
  bool tempProt;      // OHX_F1_TEMP_PROT

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

  // Haldex lock target from OHX_STATUS (0-100%, requested lock). The header
  // documents no sentinel for this field, but 0xFF would be 255% on a bar
  // that tops out at 100, so it is treated as absent like lockActual.
  bool lockTargetValid;
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

// ---- the one thing this firmware transmits ---------------------------
//
// A Haldex mode request (OHX_C_MODE) to the controller that is broadcasting
// OHX_STATUS. Everything else the gauge sends is the setup AP's own 802.11
// beacons; nothing else on the wire originates here.
//
// The command is signed with HMAC-SHA256 keyed on the controller's AP
// password, which is why that password is a stored setting. The controller
// never answers directly: the verdict arrives in the ack fields of its next
// status broadcast, so sendMode() succeeding only means "on the air".

enum class SendResult : uint8_t {
  Ok = 0,
  NoStatus,  // no fresh OHX_STATUS: nothing to sign against, no MAC to send to
  NoPeer,    // ESP-NOW refused the unicast peer
  NoKey,     // the controller's AP is passworded and no key is stored
  Radio,     // esp_now_send() failed
  Invalid,   // not a mode: the caller is broken, nothing was sent
};

// Ask the controller for a Haldex mode (0 Stock .. 5 Expert).
SendResult sendMode(uint8_t mode);

// Sequence number of the last command this build put on the air (0 = none).
uint8_t lastSentSeq();

// Result codes that are ours, not the controller's.
inline constexpr uint8_t kAckNoLink = 0xFE;  // nothing was sent
inline constexpr uint8_t kAckNoKey = 0xFD;   // a key is needed and not stored
inline constexpr uint8_t kAckNoReply = 0xFF; // sent, never answered

struct Ack {
  bool valid;       // a verdict for one of our commands has arrived
  uint8_t result;   // ohxResult_t, or kAckNoLink / kAckNoKey / kAckNoReply
  uint8_t seq;      // the command it is about
  uint32_t ageMs;   // since the verdict arrived
};

// The last verdict heard. valid stays true once set, so the caller decides how
// long to show it; a new command does not clear it.
Ack lastAck();

}  // namespace ohxnow
