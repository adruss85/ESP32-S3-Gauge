// =====================================================================
// ohx_now.cpp - the gauge's ESP-NOW receiver
//
// Two broadcasts share one radio channel:
//
//   OHX_STATUS  OpenHaldex-C6 -> 10 Hz, carries boost (mbar above ambient)
//   CANN        bodycan/C6    -> the car's CAN frames, carries Engine_3
//                                (0x380) intake air temperature, Cluster_2
//                                (0x420) coolant and engine-oil temperature,
//                                and Engine_7 (0x588) oil pressure on a
//                                ForbesAutomotive-patched ME7
//
// The controller's access point is the anchor. Scanning for it gives the
// channel without joining it, which is all ESP-NOW needs. If the anchor
// disappears, the listener falls back to channel 1 and hops.
//
// The gauge's own setup AP (CANv-gauge) shares this radio: AP and STA must
// be on one channel, so the AP follows this file's channel and the channel
// is frozen while a page client is connected.
//
// No ESP-NOW or CAN frame is transmitted here: the gauge observes. The AP's
// 802.11 beacons are the only RF the firmware originates.
// =====================================================================
#include "net/ohx_now.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <math.h>
#include <string.h>

#include "app/config.h"
#include "cannow_proto.h"
#include "ohx_proto.h"

#ifndef GAUGE_HAS_DISPLAY
#define GAUGE_HAS_DISPLAY 0
#endif

namespace ohxnow {
namespace {

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

constexpr int kMaxSenders = 4;
constexpr int kMaxLogFrames = 8;

constexpr float kMbarPerPsi = 68.94757f;

// A frame older than this is treated as gone rather than as the current
// reading. The controller broadcasts at 10 Hz; 1.5 s rides out packet loss.
constexpr uint32_t kStatusFreshMs = 1500;
constexpr uint32_t kIatFreshMs = 2000;

struct Sender {
  uint8_t mac[6];
  bool seen;
};

Sender g_senders[kMaxSenders];
int g_senderCount = 0;

struct LogFrame {
  uint32_t id;
  uint8_t len;
  bool ext;
  uint8_t data[8];
};

struct LogPacket {
  bool valid;
  uint8_t src[6];
  uint8_t kind;
  uint8_t seq;
  uint8_t count;
  uint8_t lost;
  uint8_t channel;
  int16_t rssi;
  uint8_t nFrames;
  LogFrame frames[kMaxLogFrames];
};

struct RejectLog {
  uint8_t src[6];
  uint8_t channel;
  int16_t rssi;
  int len;
  uint8_t data[16];
};

Stats g_stats = {};
Telemetry g_tel = {};

LogPacket g_log = {};
bool g_logNew = false;
uint32_t g_logCount = 0;  // accepted CANN packets, used to throttle the console

RejectLog g_reject = {};
bool g_rejectNew = false;

volatile uint32_t g_lastAnyMs = 0;
volatile uint32_t g_lastStatusMs = 0;
volatile uint32_t g_lastCanMs = 0;
volatile uint32_t g_lastIatMs = 0;
volatile float g_iatC = NAN;
volatile int g_term58s = -1;  // Terminal 58s, 0..99; -1 = not heard
volatile uint32_t g_lastOilMs = 0;
volatile float g_oilC = NAN;

volatile uint32_t g_lastCoolantMs = 0;
volatile float g_coolantC = NAN;

volatile uint32_t g_lastOilPressMs = 0;
volatile float g_oilPressPsi = NAN;
volatile bool g_schemeEvent = false;
volatile bool g_modeEvent = false;
bool g_cruiseCombo = false;
bool g_cruiseMode = false;
uint32_t g_lastCruiseEventMs = 0;
uint32_t g_lastCruiseModeMs = 0;

volatile uint8_t g_channel = config::kFallbackChannel;
volatile bool g_forceScan = false;
bool g_enabled = false;

// Upper bound for hopping. 13 with no AP; the softAP can lower it.
uint8_t g_maxChannel = config::kMaxChannel;
bool g_apUp = false;

bool g_hopping = false;
uint8_t g_hopChannel = config::kFallbackChannel;
uint32_t g_hopMs = 0;
uint32_t g_forceScanUntil = 0;
uint8_t g_printedChannel = 0;
uint32_t g_lastScanMs = 0;
bool g_anchorFound = false;
bool g_pageHold = false;  // a web client is pinning the channel

// Async WiFi scan state machine
enum class ScanState { Idle, Scanning, Processing };
ScanState g_scanState = ScanState::Idle;

uint32_t g_lastStatusPrintMs = 0;
uint32_t g_printedStatusPackets = 0;
bool g_printedFirstStatus = false;
bool g_printedFirstIat = false;

bool setChannel(uint8_t ch) {
  const esp_err_t e = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  if (e != ESP_OK) {
    Serial.printf("[now] set_channel(%u) failed: %s\n", (unsigned)ch,
                  esp_err_to_name(e));
    return false;
  }
  return true;
}

int senderSlot(const uint8_t *mac) {
  for (int i = 0; i < g_senderCount; i++) {
    if (memcmp(g_senders[i].mac, mac, 6) == 0) return i;
  }
  if (g_senderCount < kMaxSenders) {
    memcpy(g_senders[g_senderCount].mac, mac, 6);
    g_senders[g_senderCount].seen = true;
    return g_senderCount++;
  }
  return 0;  // more than four senders: fold into the first slot
}

void noteSender(const uint8_t *mac) {
  senderSlot(mac);
  g_stats.senders = (uint32_t)g_senderCount;
}

void noteReject(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  taskENTER_CRITICAL(&g_mux);
  g_stats.rejects++;
  if (!g_rejectNew) {
    memcpy(g_reject.src, info->src_addr, 6);
    g_reject.channel = info->rx_ctrl ? info->rx_ctrl->channel : g_channel;
    g_reject.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
    g_reject.len = len < (int)sizeof(g_reject.data) ? len : (int)sizeof(g_reject.data);
    if (g_reject.len > 0) memcpy(g_reject.data, data, (size_t)g_reject.len);
    g_rejectNew = true;
  }
  taskEXIT_CRITICAL(&g_mux);
}

// ---- OHX_STATUS ------------------------------------------------------
void handleStatus(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != (int)sizeof(OhxStatus)) {
    noteReject(info, data, len);
    return;
  }

  OhxStatus st;
  memcpy(&st, data, sizeof(st));
  if (st.h.type != OHX_T_STATUS || st.h.proto != OHX_PROTO) {
    noteReject(info, data, len);
    return;
  }

  const uint32_t now = millis();
  const int8_t rssi = info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : 0;
  const uint8_t ch = info->rx_ctrl ? (uint8_t)info->rx_ctrl->channel : g_channel;

  Telemetry t = {};
  t.statusValid = true;
  t.seq = st.seq;
  t.mode = st.mode;
  t.haldexState = st.haldexState;
  t.rpm = st.rpm == OHX_NONE16 ? NAN : (float)st.rpm;
  t.speed = st.speed == OHX_NONE16 ? NAN : (float)st.speed;
  t.throttle = st.throttle == OHX_NONE8 ? NAN : (float)st.throttle;
  if (st.boost == OHX_NONE16) {
    t.boostPsi = NAN;
    t.boostBar = NAN;
  } else {
    t.boostPsi = (float)st.boost / kMbarPerPsi;
    t.boostBar = (float)st.boost / 1000.0f;
  }
  memcpy(t.name, st.name, sizeof(st.name));
  t.name[sizeof(t.name) - 1] = '\0';

  // Wheel slip: FL, FR, RL, RR (%). OHX_NONE8 (-128) = no value.
  bool anySlip = false;
  for (int i = 0; i < 4; i++) {
    t.slip[i] = st.slip[i];
    if (st.slip[i] != OHX_NONE8) anySlip = true;
  }
  t.slipValid = anySlip;

  // Haldex lock actual
  t.lockActualValid = (st.lockActual != OHX_NONE8);
  t.lockActual = t.lockActualValid ? st.lockActual : 0;

  // Haldex lock target (requested)
  t.lockTarget = st.lockTarget;

  taskENTER_CRITICAL(&g_mux);
  noteSender(info->src_addr);
  g_tel = t;
  g_stats.statusPackets++;
  g_stats.rssi = rssi;
  g_stats.channel = ch;
  memcpy(g_stats.src, info->src_addr, 6);
  g_lastStatusMs = now;
  g_lastAnyMs = now;
  g_forceScan = false;
  g_channel = ch;
  taskEXIT_CRITICAL(&g_mux);
}

// ---- CANN ------------------------------------------------------------
const char *kindName(uint8_t kind) {
  switch (kind) {
    case CANN_K_OPENHALDEX:
      return "OpenHaldex";
    case CANN_K_ADC2CAN:
      return "ADC2CAN";
    default:
      return "?";
  }
}

void handleCann(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < (int)sizeof(CannHdr)) {
    noteReject(info, data, len);
    return;
  }

  CannHdr hdr;
  memcpy(&hdr, data, sizeof(hdr));
  if (hdr.magic[0] != CANN_MAGIC0 || hdr.magic[1] != CANN_MAGIC1 ||
      hdr.proto != CANN_PROTO) {
    noteReject(info, data, len);
    return;
  }

  const uint32_t now = millis();
  LogPacket lp = {};
  lp.valid = true;
  memcpy(lp.src, info->src_addr, 6);
  lp.kind = hdr.kind;
  lp.seq = hdr.seq;
  lp.count = hdr.count;
  lp.lost = hdr.lost;
  lp.channel = info->rx_ctrl ? info->rx_ctrl->channel : g_channel;
  lp.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;

  bool haveIat = false;
  float iat = NAN;
  bool haveOilTemp = false;
  float oilTemp = NAN;
  bool haveDimmer = false;
  int dimmer = 0;
  bool haveCruise = false;
  bool cruiseCombo = false;
  bool cruiseMode = false;
  bool haveCoolant = false;
  float coolant = NAN;
  bool haveOilPress = false;
  float oilPress = NAN;

  size_t off = sizeof(CannHdr);
  for (uint8_t i = 0; i < hdr.count; i++) {
    if (off + 2 > (size_t)len) {
      lp.valid = false;
      break;
    }
    uint16_t tag;
    memcpy(&tag, data + off, 2);
    off += 2;

    const bool ext = (tag & CANN_TAG_EXT) != 0;
    const uint8_t frameLen = (uint8_t)((tag >> CANN_TAG_LEN_SHIFT) & 0x0F);
    uint32_t id = tag & CANN_TAG_ID;
    if (ext) {
      if (off + 4 > (size_t)len) {
        lp.valid = false;
        break;
      }
      uint32_t id32;
      memcpy(&id32, data + off, 4);
      off += 4;
      id = id32 & 0x1FFFFFFF;
    }
    if (frameLen > 8 || off + frameLen > (size_t)len) {
      lp.valid = false;
      break;
    }

    // Engine_3 (0x380): byte 1 is MO3_Offsentemp, 0.75 C/count with a
    // -48 C offset. 0xFF is outside the DBC range, so it means "no value".
    if (id == 0x380 && frameLen >= 2 && data[off + 1] != 0xFF) {
      iat = (float)data[off + 1] * 0.75f - 48.0f;
      haveIat = true;
    }

    // Cluster_2 (0x420): byte 4 is CoolantTemp__4_1__Cluster_2_,
    // 0.75 C/count with a -48 C offset (vw_pq.dbc). 0xFF means no value.
    // Engine_1 (0x280) byte 1 is Inner_Engine_Torque, not coolant, so the
    // 0.6.2 decode read an engine-torque byte and subtracted 40 from it.
    if (id == 0x420 && frameLen >= 5 && data[off + 4] != 0xFF) {
      coolant = (float)data[off + 4] * 0.75f - 48.0f;
      haveCoolant = true;
    }

    // Engine_7 / Motor_7 (0x588): bytes 5-6 are the raw 10-bit ADC of the
    // rear-O2 input, now carrying a PST-F 1 oil-pressure sensor. Only the
    // ForbesAutomotive ME7 patch lengthens the stock three-byte frame to
    // eight, so the length is the gate: reading past a stock frame is how a
    // missing sensor becomes a confident 0.00. Little-endian, sent unscaled
    // so the gauge owns the transfer: 0.5 V at 0 bar, 4.5 V at 10 bar, so
    // volts = raw * 5/1024 and bar = (volts - 0.5) / 0.4. Deliberately not
    // clamped at zero - a reading below the sensor's own zero is an unwired
    // plug, and a fake 0.0 bar on an oil gauge is worse than a negative one.
    if (id == 0x588 && frameLen >= 8) {
      const uint16_t raw =
          (uint16_t)((data[off + 5] | (data[off + 6] << 8)) & 0x03FF);
      const float volts = (float)raw * 5.0f / 1024.0f;
      oilPress = ((volts - 0.5f) / 0.4f) * 14.5038f;
      haveOilPress = true;
    }

    // Cluster_2 (0x420): Terminal 58s is byte 6 bits 0-6; bit 7 is its error
    // flag and 0x7F is the no-value sentinel. This is the same field the
    // S3-dash uses for auto day/night. Terminal 58d (byte 5) is not used:
    // it reads 24% with the lights off there.
    if (id == 0x420 && frameLen >= 7) {
      const uint8_t b = data[off + 6];
      const int value = b & 0x7F;
      if (((b >> 7) & 0x01) == 0 && value != 0x7F) {
        dimmer = value;
        haveDimmer = true;
      }
    }

    // Cluster_2 (0x420) byte 3: OilTemperature_4_1, 1 C/count with -60 C
    // offset. 0xFF means no value.
    if (id == 0x420 && frameLen >= 4 && data[off + 3] != 0xFF) {
      oilTemp = (float)data[off + 3] - 60.0f;
      haveOilTemp = true;
    }

    // GRA_New (0x38A, PQ cruise stalk): byte 1 bit 1 is GRA_Cancel, bit 2
    // is GRA_Down_Short and bit 4 is GRA_Down_Long. The column can set Cancel
    // and Down together; treat either down bit as the switch.
    if (id == 0x38A && frameLen >= 2) {
      const uint8_t b = data[off + 1];
      const bool cancel = (b & 0x02) != 0;
      const bool downShort = (b & 0x04) != 0;
      const bool downLong = (b & 0x10) != 0;
      // Cancel + Down (either length) keeps the documented accent cycle.
      // Down long on its own is the cruise-stalk equivalent of the BOOT
      // hold: it switches the gauge/G-meter screen. The stalk generates the
      // long bit; the gauge does not have to time the hold itself.
      cruiseCombo = cancel && (downShort || downLong);
      cruiseMode = downLong && !cancel;
      haveCruise = true;
    }

    if (lp.nFrames < kMaxLogFrames) {
      LogFrame &f = lp.frames[lp.nFrames++];
      f.id = id;
      f.len = frameLen;
      f.ext = ext;
      memcpy(f.data, data + off, frameLen);
    }
    off += frameLen;
  }

  if (!lp.valid) {
    noteReject(info, data, len);
    return;
  }

  taskENTER_CRITICAL(&g_mux);
  noteSender(info->src_addr);
  g_stats.canPackets++;
  g_stats.frames += hdr.count;
  g_stats.rssi = lp.rssi;
  g_stats.channel = lp.channel;
  memcpy(g_stats.src, info->src_addr, 6);
  g_lastCanMs = now;
  g_lastAnyMs = now;
  g_forceScan = false;
  g_channel = lp.channel;
  if (haveIat) {
    g_iatC = iat;
    g_lastIatMs = now;
  }
  if (haveCoolant) {
    g_coolantC = coolant;
    g_lastCoolantMs = now;
  }
  if (haveOilPress) {
    g_oilPressPsi = oilPress;
    g_lastOilPressMs = now;
  }
  if (haveOilTemp) {
    g_oilC = oilTemp;
    g_lastOilMs = now;
  }
  if (haveDimmer) g_term58s = dimmer;
  if (haveCruise) {
    if (cruiseCombo && !g_cruiseCombo &&
        (uint32_t)(now - g_lastCruiseEventMs) >= 250) {
      g_schemeEvent = true;
      g_lastCruiseEventMs = now;
    }
    if (cruiseMode && !g_cruiseMode &&
        (uint32_t)(now - g_lastCruiseModeMs) >= 250) {
      g_modeEvent = true;
      g_lastCruiseModeMs = now;
    }
    g_cruiseCombo = cruiseCombo;
    g_cruiseMode = cruiseMode;
  }
  g_log = lp;
  g_logNew = true;
  g_logCount++;
  taskEXIT_CRITICAL(&g_mux);
}

// ---- receive callback ------------------------------------------------
void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  taskENTER_CRITICAL(&g_mux);
  g_stats.raw++;
  taskEXIT_CRITICAL(&g_mux);

  if (len >= 2 && data[0] == OHX_MAGIC0 && data[1] == OHX_MAGIC1) {
    handleStatus(info, data, len);
    return;
  }
  if (len >= 2 && data[0] == CANN_MAGIC0 && data[1] == CANN_MAGIC1) {
    handleCann(info, data, len);
    return;
  }
  noteReject(info, data, len);
}

// ---- console diagnostics ---------------------------------------------
void printFrame(const LogFrame &f) {
  Serial.printf("        id=0x%03lX%s len=%u data:", (unsigned long)f.id,
                f.ext ? " ext" : "", f.len);
  for (int i = 0; i < f.len; i++) Serial.printf(" %02X", f.data[i]);
  Serial.println();
}

void drainReject() {
  RejectLog r;
  taskENTER_CRITICAL(&g_mux);
  const bool have = g_rejectNew;
  if (have) {
    r = g_reject;
    g_rejectNew = false;
  }
  taskEXIT_CRITICAL(&g_mux);
  if (!have) return;

  Serial.printf("[now] rejected %d bytes from %02X:%02X:%02X:%02X:%02X:%02X ch=%u rssi=%d:",
                r.len, r.src[0], r.src[1], r.src[2], r.src[3], r.src[4], r.src[5],
                r.channel, r.rssi);
  for (int i = 0; i < r.len; i++) Serial.printf(" %02X", r.data[i]);
  Serial.println();
}

void drainLog() {
  LogPacket lp;
  uint32_t n;
  taskENTER_CRITICAL(&g_mux);
  const bool have = g_logNew;
  if (have) {
    lp = g_log;
    n = g_logCount;
    g_logNew = false;
  }
  taskEXIT_CRITICAL(&g_mux);
  if (!have) return;

  // First few packets in full, then a once-every-100 heartbeat so a real
  // bus cannot swamp the console.
  if (n <= 2) {
    Serial.printf("[cann] #%lu %s seq=%u frames=%u lost=%u ch=%u rssi=%d\n",
                  (unsigned long)n, kindName(lp.kind), lp.seq, lp.count, lp.lost,
                  lp.channel, lp.rssi);
    for (int i = 0; i < lp.nFrames; i++) printFrame(lp.frames[i]);
  } else if ((n % 100) == 0) {
    Serial.printf("[cann] #%lu %s seq=%u frames=%u lost=%u ch=%u rssi=%d\n",
                  (unsigned long)n, kindName(lp.kind), lp.seq, lp.count, lp.lost,
                  lp.channel, lp.rssi);
  }
}

void handleSerial() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == 's' || c == 'r') {
      g_forceScan = true;
      g_forceScanUntil = millis() + 15000;
      g_hopMs = 0;
      g_hopChannel = g_channel;
      Serial.println("[now] channel scan requested");
    } else if (c == 'i') {
      const Stats s = stats();
      Serial.printf("[now] ch=%u raw=%lu status=%lu cann=%lu frames=%lu rejects=%lu senders=%lu rssi=%d\n",
                    s.channel, (unsigned long)s.raw, (unsigned long)s.statusPackets,
                    (unsigned long)s.canPackets, (unsigned long)s.frames,
                    (unsigned long)s.rejects, (unsigned long)s.senders, s.rssi);
    }
  }
}

// The family rule is that the channel comes from an AP, not a compiled-in
// constant. The C6 is preferred because it is the boost source.
// Async version: starts scan, returns immediately. Results processed in hopTick().
void discoverChannel() {
  if (g_scanState != ScanState::Idle) return;  // scan already in progress

  WiFi.scanNetworks(true);  // async mode
  g_scanState = ScanState::Scanning;
  Serial.println("[now] async scan started");
}

// Process async scan results when complete. Call from hopTick().
void processScanResults() {
  int n = WiFi.scanComplete();
  if (n == -1) return;   // still scanning
  if (n == -2) {         // scan failed or not started
    g_anchorFound = false;
    g_channel = config::kFallbackChannel;
    Serial.printf("[now] scan failed; fallback ch %u\n", (unsigned)g_channel);
    g_scanState = ScanState::Idle;
    return;
  }

  // Scan complete, process results
  int c6 = -1, c6Rssi = -1000, dash = -1, dashRssi = -1000;
  int old = -1, oldRssi = -1000;
  for (int i = 0; i < n; i++) {
    const String ssid = WiFi.SSID(i);
    const int rssi = WiFi.RSSI(i);
    if (ssid == config::kAnchorSSIDAlt) {
      if (rssi > c6Rssi) {
        c6 = i;
        c6Rssi = rssi;
      }
    } else if (ssid == config::kAnchorSSID && rssi > dashRssi) {
      dash = i;
      dashRssi = rssi;
    } else if (ssid == config::kAnchorSSIDOld && rssi > oldRssi) {
      old = i;
      oldRssi = rssi;
    }
  }

  const int pick = c6 >= 0 ? c6 : (dash >= 0 ? dash : old);
  if (pick >= 0) {
    const uint8_t anchorCh = (uint8_t)WiFi.channel(pick);
    // A channel the softAP cannot use is not a channel this radio can
    // listen on: AP+STA share one channel and the AP's country policy wins.
    if (anchorCh > g_maxChannel) {
      g_anchorFound = false;
      Serial.printf("[now] scan: anchor \"%s\" ch %u above the AP limit %u\n",
                    WiFi.SSID(pick).c_str(), (unsigned)anchorCh,
                    (unsigned)g_maxChannel);
    } else {
      g_channel = anchorCh;
      g_anchorFound = true;
      Serial.printf("[now] scan: %d APs; anchor \"%s\" ch %u (%d dBm)\n", n,
                    WiFi.SSID(pick).c_str(), (unsigned)g_channel,
                    WiFi.RSSI(pick));
    }
  } else {
    g_channel = config::kFallbackChannel;
    g_anchorFound = false;
    Serial.printf("[now] scan: %d APs; no anchor - fallback ch %u\n", n,
                  (unsigned)g_channel);
  }
  WiFi.scanDelete();
  g_scanState = ScanState::Idle;
}

void hopTick() {
  const uint32_t now = millis();
  const uint32_t last = g_lastAnyMs;

  // Process async scan results if scanning
  if (g_scanState == ScanState::Scanning) {
    processScanResults();
    if (g_scanState == ScanState::Idle) {
      // Scan just completed, apply the channel
      setChannel(g_channel);
      g_hopChannel = g_channel;
      g_hopMs = now;
    }
    return;  // don't do other hop logic while scan completes
  }

  // A connected page client owns the radio. AP+STA share one channel, so a
  // hop would drag the softAP with it and drop the page mid-edit. A gauge
  // already locked on the anchor keeps receiving; an unlocked one waits for
  // the page to close before tracking channels again.
  if (WiFi.softAPgetStationNum() > 0) {
    if (!g_pageHold) {
      g_pageHold = true;
      Serial.printf("[now] page client: holding channel %u\n",
                    (unsigned)g_channel);
    }
    return;
  }
  if (g_pageHold) {
    g_pageHold = false;
    Serial.println("[now] page client gone; channel tracking resumes");
  }

  bool force = g_forceScan;
  if (force && (int32_t)(now - g_forceScanUntil) >= 0) {
    g_forceScan = false;
    force = false;
    Serial.println("[now] scan timed out; back to normal listening");
  }

  const bool quiet =
      (last == 0) || ((uint32_t)(now - last) >= config::kQuietBeforeRescanMs);
  if (!quiet && !force) {
    if (g_hopping) {
      g_hopping = false;
      Serial.printf("[now] locked on channel %u\n", (unsigned)g_channel);
    }
    return;
  }

  if (!g_hopping) {
    if (last == 0 && !force && (uint32_t)(now - g_hopMs) < config::kInitialDwellMs) {
      return;
    }
    g_hopping = true;
    g_hopChannel = g_channel;
    g_hopMs = now;
    g_lastScanMs = 0;
    Serial.println("[now] no packets; looking for the anchor");
  }

  // Re-scan for an access point first: if it is there, its channel is the
  // answer and no hopping is needed. Otherwise walk 1..13.
  if ((uint32_t)(now - g_lastScanMs) >= config::kRescanMs) {
    g_lastScanMs = now;
    discoverChannel();  // starts async scan
    return;  // wait for next hopTick to process results
  }

  if (g_anchorFound) return;

  // A joinable softAP cannot move every 400 ms: a client's scan would miss
  // it. With the AP up, discovery is the periodic full scan above rather
  // than the channel walk. The cost: an anchor whose SSID is hidden cannot
  // be found this way (see TODO.md).
  if (g_apUp) return;

  if ((uint32_t)(now - g_hopMs) < config::kChannelDwellMs) return;
  g_hopMs = now;
  g_hopChannel = (uint8_t)((g_hopChannel % g_maxChannel) + 1);
  // Log only channels the radio actually tuned to; with the setup AP up the
  // country policy rejects 12/13 and a "[scan] channel 12" line that did
  // not happen reads like a working hop.
  if (setChannel(g_hopChannel)) {
    Serial.printf("[scan] channel %u\n", (unsigned)g_hopChannel);
  }
}

void statusTick() {
  const uint32_t now = millis();
  if ((uint32_t)(now - g_lastStatusPrintMs) < config::kStatusIntervalMs) return;
  g_lastStatusPrintMs = now;

  const Stats s = stats();
  const Telemetry t = telemetry();

  static int printed58 = -2;
  if (t.term58s != printed58) {
    printed58 = t.term58s;
    if (t.term58s < 0) {
      Serial.println("[now] 58s --");
    } else {
      Serial.printf("[now] 58s %d%%\n", t.term58s);
    }
  }

  if (s.statusPackets == 0 && s.canPackets == 0) {
    Serial.printf("[gauge] ch=%u waiting (raw=%lu rejects=%lu)\n", s.channel,
                  (unsigned long)s.raw, (unsigned long)s.rejects);
    return;
  }

  if (!g_printedFirstStatus && t.statusValid) {
    g_printedFirstStatus = true;
    Serial.printf("[ohx] controller \"%s\" seq=%u boost=%.2f psi\n", t.name, t.seq,
                  (double)t.boostPsi);
  }
  if (!g_printedFirstIat && t.iatValid) {
    g_printedFirstIat = true;
    Serial.printf("[can] IAT %.1f C\n", (double)t.iatC);
  }

  const uint32_t dp = s.statusPackets - g_printedStatusPackets;
  g_printedStatusPackets = s.statusPackets;
  const double rate =
      (double)dp * 1000.0 / (double)config::kStatusIntervalMs;

  Serial.printf("[gauge] ch=%u OHX=%.1f/s boost=%s IAT=%s cann=%lu rssi=%d\n",
                s.channel, rate,
                t.statusValid ? "ok" : "--", t.iatValid ? "ok" : "--",
                (unsigned long)s.canPackets, s.rssi);
}

}  // namespace

bool begin() {
  if (g_enabled) return true;

  Serial.println("[now] initialising WiFi + ESP-NOW");
  // AP+STA on the display board: the web page's softAP is started later by
  // gauge_web::begin(), and switching modes after esp_now_init() is not
  // worth the risk. The headless listener keeps plain STA.
#if GAUGE_HAS_DISPLAY
  WiFi.mode(WIFI_AP_STA);
#else
  WiFi.mode(WIFI_STA);
#endif
  WiFi.persistent(false);
  WiFi.disconnect(false, false);
  WiFi.setSleep(false);

  // Start on the family fallback channel, then look for the anchor. The
  // channel has to be set before the async scan starts: esp_wifi_set_channel
  // returns ESP_FAIL while a scan is running.
  setChannel(g_channel);
  discoverChannel();

  const esp_err_t err = esp_now_init();
  if (err != ESP_OK) {
    Serial.printf("[now] esp_now_init failed: %s\n", esp_err_to_name(err));
    return false;
  }
  esp_now_register_recv_cb(onRecv);

  // Receiving does not require a peer, but the broadcast entry keeps the
  // door open for a future send path.
  esp_now_peer_info_t peer = {};
  memset(peer.peer_addr, 0xFF, 6);
  peer.channel = 0;
  peer.encrypt = false;
  peer.ifidx = WIFI_IF_STA;
  const esp_err_t perr = esp_now_add_peer(&peer);
  if (perr != ESP_OK && perr != ESP_ERR_ESPNOW_EXIST) {
    Serial.printf("[now] broadcast peer add failed: %s (receive still works)\n",
                  esp_err_to_name(perr));
  }

  g_enabled = true;
  g_hopMs = millis();
  g_lastStatusPrintMs = millis();
  g_printedChannel = 0;
  Serial.printf("[now] listening on channel %u (scan in progress)\n", (unsigned)g_channel);
  return true;
}

void poll() {
  if (!g_enabled) {
    static uint32_t lastTry = 0;
    const uint32_t now = millis();
    if ((uint32_t)(now - lastTry) < 1000) return;
    lastTry = now;
    if (!begin()) return;
  }

  drainReject();
  drainLog();
  handleSerial();
  hopTick();

  if (g_channel != g_printedChannel) {
    g_printedChannel = g_channel;
    Serial.printf("[now] channel %u\n", (unsigned)g_channel);
  }

  statusTick();
}

bool enabled() { return g_enabled; }

uint8_t channel() { return g_channel; }

void setChannelLimit(uint8_t maxChannel) {
  if (maxChannel < 1) return;
  if (maxChannel > config::kMaxChannel) maxChannel = config::kMaxChannel;
  if (maxChannel == g_maxChannel) return;
  g_maxChannel = maxChannel;
  if (g_hopChannel > g_maxChannel) g_hopChannel = g_maxChannel;
  Serial.printf("[now] hop limit is now 1-%u\n", (unsigned)g_maxChannel);
}

void setSetupAp(bool up) {
  if (g_apUp == up) return;
  g_apUp = up;
  if (up) {
    Serial.printf("[now] setup AP up: holding channel %u (scan-only discovery)\n",
                  (unsigned)g_channel);
  } else {
    Serial.println("[now] setup AP down: channel walk allowed");
  }
}

bool takeSchemeEvent() {
  taskENTER_CRITICAL(&g_mux);
  const bool event = g_schemeEvent;
  g_schemeEvent = false;
  taskEXIT_CRITICAL(&g_mux);
  return event;
}

bool takeModeEvent() {
  taskENTER_CRITICAL(&g_mux);
  const bool event = g_modeEvent;
  g_modeEvent = false;
  taskEXIT_CRITICAL(&g_mux);
  return event;
}

Stats stats() {
  Stats s;
  const uint32_t now = millis();
  taskENTER_CRITICAL(&g_mux);
  s = g_stats;
  s.enabled = g_enabled;
  s.channel = g_channel;
  s.statusAgeMs = g_lastStatusMs ? (uint32_t)(now - g_lastStatusMs) : UINT32_MAX;
  s.canAgeMs = g_lastCanMs ? (uint32_t)(now - g_lastCanMs) : UINT32_MAX;
  taskEXIT_CRITICAL(&g_mux);
  return s;
}

Telemetry telemetry() {
  Telemetry t;
  const uint32_t now = millis();
  taskENTER_CRITICAL(&g_mux);
  t = g_tel;
  if (!g_lastStatusMs || (uint32_t)(now - g_lastStatusMs) > kStatusFreshMs) {
    t.statusValid = false;
    t.slipValid = false;
    t.lockActualValid = false;
  }
  t.iatValid = g_lastIatMs && (uint32_t)(now - g_lastIatMs) <= kIatFreshMs;
  t.iatC = t.iatValid ? (float)g_iatC : NAN;
  t.coolantValid = g_lastCoolantMs && (uint32_t)(now - g_lastCoolantMs) <= kIatFreshMs;
  t.coolantC = t.coolantValid ? (float)g_coolantC : NAN;
  t.oilTempValid = g_lastOilMs && (uint32_t)(now - g_lastOilMs) <= kIatFreshMs;
  t.oilTempC = t.oilTempValid ? (float)g_oilC : NAN;
  t.oilPressValid = g_lastOilPressMs && (uint32_t)(now - g_lastOilPressMs) <= kIatFreshMs;
  t.oilPressPsi = t.oilPressValid ? (float)g_oilPressPsi : NAN;
  t.term58s = g_term58s;
  t.lockTarget = g_tel.lockTarget;
  taskEXIT_CRITICAL(&g_mux);
  return t;
}

}  // namespace ohxnow
