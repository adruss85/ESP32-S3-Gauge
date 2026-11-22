// =====================================================================
// touch.cpp - CST9217 driver, LVGL pointer and the gauge's gestures
//
// Protocol and geometry follow Waveshare's SensorLib TouchDrvCST92xx and
// their BSP's touch configuration for this board. The short version:
//
//   0xD1/0x01   enter command mode
//   0xD1/0xF8   read the controller's resolution (466 x 466 here)
//   0xD2/0x04   read the chip type (0x9217)
//   0xD0/0x00   read a point report; then 0xD0/0x00/0xAB acknowledges it
//
// A report is five bytes per finger: id<<4 | event, x high, y low nibbles
// packed across three bytes. Event 0x06 is "down", anything else is release.
//
// The INT line is not used: SensorLib notes the CST9217 raises it once per
// second rather than per touch, so the controller is polled instead. Three
// short I2C transfers at up to 66 Hz is about 0.4 ms each - cheaper than a
// mis-timed interrupt, and the IMU shares the same bus without contention
// because both run from loop().
//
// The gestures are told apart by how still the finger was, not by how long
// it was down. That comparison lives in gesture.cpp, which the host test
// drives; this file only samples the controller and feeds it.
// =====================================================================
#include "hal/touch.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "board_config.h"

namespace touch {
namespace {

constexpr uint8_t kCmdRead = 0xD0;
constexpr uint8_t kCmdInfo = 0xD1;
constexpr uint8_t kCmdType = 0xD2;
constexpr uint8_t kAck = 0xAB;
constexpr size_t kReportBytes = 5 * 2 + 5;  // two fingers + header + ack

constexpr uint32_t kPollMs = 15;

bool g_ready = false;
uint16_t g_resX = LCD_H_RES;
uint16_t g_resY = LCD_V_RES;
lv_indev_t *g_indev = nullptr;
void (*g_handler)(Gesture) = nullptr;

uint32_t g_lastPollMs = 0;
bool g_pressed = false;
uint16_t g_x = 0;
uint16_t g_y = 0;
uint16_t g_rawX = 0;
uint16_t g_rawY = 0;

// Press tracking for the gesture classifier.
gesture::Tracker g_tracker;

bool writeBytes(const uint8_t *buf, size_t len) {
  Wire.beginTransmission(BOARD_TOUCH_ADDR);
  for (size_t i = 0; i < len; i++) Wire.write(buf[i]);
  return Wire.endTransmission() == 0;
}

bool readAfter(const uint8_t *buf, size_t len, uint8_t *out, size_t outLen) {
  Wire.beginTransmission(BOARD_TOUCH_ADDR);
  for (size_t i = 0; i < len; i++) Wire.write(buf[i]);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)BOARD_TOUCH_ADDR, (int)outLen) != (int)outLen) {
    return false;
  }
  for (size_t i = 0; i < outLen; i++) out[i] = (uint8_t)Wire.read();
  return true;
}

// Panel coordinates -> LVGL coordinates. Waveshare's BSP for this board uses
// swap_xy = 0, mirror_x = 1, mirror_y = 1. The swap, when it is needed, is
// done by the caller because it exchanges the two raw axes before mapping.
uint16_t mapX(uint16_t x) {
  return BOARD_TOUCH_MIRROR_X ? (uint16_t)(LCD_H_RES - 1 - x) : x;
}

uint16_t mapY(uint16_t y) {
  return BOARD_TOUCH_MIRROR_Y ? (uint16_t)(LCD_V_RES - 1 - y) : y;
}

void lvRead(lv_indev_t *, lv_indev_data_t *data) {
  data->point.x = g_x;
  data->point.y = g_y;
  data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void emit(Gesture g) {
  if (g_handler != nullptr && g != Gesture::None) g_handler(g);
}

// One report. Returns true if a finger is down.
bool readPoint() {
  const uint8_t cmd[2] = {kCmdRead, 0x00};
  uint8_t report[kReportBytes] = {0};

  if (!readAfter(cmd, sizeof(cmd), report, sizeof(report))) {
    g_pressed = false;
    return false;
  }

  // Always acknowledge, even when the report is a release: the controller
  // will not produce another one until it has been acked.
  const uint8_t ack[3] = {kCmdRead, 0x00, kAck};
  writeBytes(ack, sizeof(ack));

  if (report[6] != kAck) {
    g_pressed = false;
    return false;
  }

  const uint8_t count = report[5] & 0x7F;
  if (count == 0) {
    g_pressed = false;
    return false;
  }

  const uint8_t event = report[0] & 0x0F;
  if (event != 0x06) {  // 0x06 = finger down
    g_pressed = false;
    return false;
  }

  const uint16_t rawX = (uint16_t)(((uint16_t)report[1] << 4) | (report[3] >> 4));
  const uint16_t rawY = (uint16_t)(((uint16_t)report[2] << 4) | (report[3] & 0x0F));
  g_rawX = rawX;
  g_rawY = rawY;

#if BOARD_TOUCH_SWAP_XY
  g_x = mapX(rawY);
  g_y = mapY(rawX);
#else
  g_x = mapX(rawX);
  g_y = mapY(rawY);
#endif
  g_pressed = true;
  return true;
}

}  // namespace

bool begin() {
  g_ready = false;

  pinMode(BOARD_TOUCH_PIN_RST, OUTPUT);
  digitalWrite(BOARD_TOUCH_PIN_RST, LOW);
  delay(10);
  digitalWrite(BOARD_TOUCH_PIN_RST, HIGH);
  delay(40);

  pinMode(BOARD_TOUCH_PIN_INT, INPUT_PULLUP);

  // Command mode, then the controller's own resolution. Reading it makes a
  // future panel change a two-line edit rather than a rescale by guesswork.
  const uint8_t cmdMode[2] = {kCmdInfo, 0x01};
  writeBytes(cmdMode, sizeof(cmdMode));
  delay(10);

  uint8_t info[4] = {0};
  const uint8_t resCmd[2] = {kCmdInfo, 0xF8};
  if (!readAfter(resCmd, sizeof(resCmd), info, sizeof(info))) {
    Serial.println("[touch] no answer at 0x5A");
    return false;
  }
  const uint16_t resX = (uint16_t)(((uint16_t)info[1] << 8) | info[0]);
  const uint16_t resY = (uint16_t)(((uint16_t)info[3] << 8) | info[2]);

  uint8_t type[4] = {0};
  const uint8_t typeCmd[2] = {kCmdType, 0x04};
  uint16_t chip = 0;
  if (readAfter(typeCmd, sizeof(typeCmd), type, sizeof(type))) {
    chip = (uint16_t)(((uint16_t)type[3] << 8) | type[2]);
  }

  if (resX > 0 && resY > 0) {
    g_resX = resX;
    g_resY = resY;
  }

  g_indev = lv_indev_create();
  if (g_indev == nullptr) {
    Serial.println("[touch] no LVGL indev");
    return false;
  }
  lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(g_indev, lvRead);

  g_lastPollMs = millis();
  g_ready = true;
  Serial.printf("[touch] CST92%02X at 0x%02X, %ux%u, hold = scheme, tap = haldex mode, swipe = screen\n",
                (unsigned)(chip & 0xFF), BOARD_TOUCH_ADDR, (unsigned)g_resX,
                (unsigned)g_resY);
  return true;
}

void poll() {
  if (!g_ready) return;

  const uint32_t now = millis();
  if ((uint32_t)(now - g_lastPollMs) < kPollMs) return;
  g_lastPollMs = now;

  const bool down = readPoint();
  emit(g_tracker.update(down, (int)g_x, (int)g_y, now));
}

void setHandler(void (*fn)(Gesture)) { g_handler = fn; }

bool ready() { return g_ready; }

void lastPoint(uint16_t &x, uint16_t &y, bool &pressed) {
  x = g_x;
  y = g_y;
  pressed = g_pressed;
}

}  // namespace touch
