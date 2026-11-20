// =====================================================================
// co5300_init.c - the register sequence for this exact AMOLED glass
//
// Copied from Waveshare's own CO5300 driver for the ESP32-S3-Touch-AMOLED-1.75
// (GFX_Library_for_Arduino, Arduino_CO5300::tftInit). It is not a generic
// CO5300 table: esp-iot-solution's default sends a different column window
// (00 06 01 DD) and no page-select, and that combination comes up shifted on
// this panel.
//
// The two window commands bake in the 6-column gap of the visible area
// (controller RAM is 480 wide, the glass is 466 starting at column 6):
//   0x2A 00 06 01 D7 -> columns 6..471   (466)
//   0x2B 00 00 01 D1 -> rows    0..465   (466)
// display() also calls esp_lcd_panel_set_gap(6, 0), which is what the draw
// path offsets by; the window here is the same geometry from the other side.
//
// Keep this table verbatim. It is replaced as a whole, never edited.
// =====================================================================
#include "hal/co5300_init.h"

const co5300_lcd_init_cmd_t co5300_vendor_init[] = {
  {0x11, (uint8_t []){0x00}, 0, 120},  // sleep out
  {0xFE, (uint8_t []){0x00}, 1, 0},    // page select 0
  {0xC4, (uint8_t []){0x80}, 1, 0},    // SPI mode control: QSPI
  {0x3A, (uint8_t []){0x55}, 1, 0},    // 16 bit/pixel
  {0x53, (uint8_t []){0x20}, 1, 10},   // CTRLD1: BCTRL + brightness dimming
  {0x63, (uint8_t []){0xFF}, 1, 10},   // high-brightness mode value
  {0x51, (uint8_t []){0xFF}, 1, 10},   // normal brightness (display.cpp owns it)
  {0x58, (uint8_t []){0x00}, 1, 0},    // contrast enhancement off
  {0x35, (uint8_t []){0x00}, 1, 10},   // TEON: V-blank only
  {0x2A, (uint8_t []){0x00, 0x06, 0x01, 0xD7}, 4, 0},
  {0x2B, (uint8_t []){0x00, 0x00, 0x01, 0xD1}, 4, 0},
  {0x29, (uint8_t []){0x00}, 0, 20},   // display on
};

const uint16_t co5300_vendor_init_size =
    (uint16_t)(sizeof(co5300_vendor_init) / sizeof(co5300_vendor_init[0]));
