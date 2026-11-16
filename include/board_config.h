// =====================================================================
// board_config.h - Waveshare ESP32-S3-LCD-1.85 (N16R8)
//
// 1.85" round IPS panel, 360 x 360, ST77916 driven over QSPI.
// The LCD reset line is not a GPIO: it comes from the TCA9554 I/O
// expander on the board's I2C bus.
//
// Pin map from Waveshare's own LVGL_Arduino example and the board wiki.
// =====================================================================
#pragma once

#include <stdint.h>

#define BOARD_NAME "Waveshare ESP32-S3-LCD-1.85"

// ---------------------------------------------------------------- panel
#define LCD_H_RES 360
#define LCD_V_RES 360

// ST77916 QSPI: one clock and four data lines.
#define LCD_PIN_SCK 40
#define LCD_PIN_D0 46
#define LCD_PIN_D1 45
#define LCD_PIN_D2 42
#define LCD_PIN_D3 41
#define LCD_PIN_CS 21
#define LCD_PIN_TE 18  // tearing-effect output, not used by the SPI path

// QSPI clock. The vendor example asks for 80 MHz.
#define LCD_QSPI_HZ (80 * 1000 * 1000)

// Backlight: plain GPIO, dimmable with LEDC.
#define LCD_PIN_BACKLIGHT 5
#define LCD_BACKLIGHT_HZ 20000
#define LCD_BACKLIGHT_BITS 10

// ---------------------------------------------------- I2C control bus
// TCA9554 I/O expander, RTC and IMU share this bus. The touch controller
// has its own bus on GPIO1/3, which this gauge does not use.
#define BOARD_I2C_SDA 11
#define BOARD_I2C_SCL 10
#define BOARD_I2C_HZ 400000

// TCA9554: address 0x20 (A2:A0 all low).
//   P0 = touch reset
//   P1 = LCD reset
//   P2 = SD card chip select
//   P3 = IMU interrupt 2
//   P4 = IMU interrupt 1
#define TCA9554_ADDR 0x20
#define TCA9554_PIN_TOUCH_RST 0
#define TCA9554_PIN_LCD_RST 1
