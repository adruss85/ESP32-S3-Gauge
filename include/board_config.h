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

// Normal (day) brightness, then the S3-dash night curve:
//   night = LCD_NIGHT_MIN_PCT + LCD_NIGHT_SPAN_PCT * 58s / 100
// 5% + 60% of the dash dimmer gives 9% at 58s=7 and 64% at 58s=99.
#define LCD_BACKLIGHT_DAY_PCT 85
#define LCD_NIGHT_MIN_PCT 5
#define LCD_NIGHT_SPAN_PCT 60

// BOOT button, active low. Holding it at reset still enters download mode;
// after boot it is a plain user input.
#define BOARD_PIN_BOOT 0
#define BOARD_BOOT_DEBOUNCE_MS 30

// ---------------------------------------------------- I2C control bus
// TCA9554 I/O expander, RTC and IMU share this bus. The touch controller
// has its own bus on GPIO1/3, which this gauge does not use.
#define BOARD_I2C_SDA 11
#define BOARD_I2C_SCL 10
#define BOARD_I2C_HZ 400000

// QMI8658 6-axis IMU. The SA0 strap selects 0x6A or 0x6B; this board
// answers at 0x6B, but the driver probes 0x6A first so a different strap
// does not need a rebuild.
#define BOARD_IMU_ADDR_A 0x6A
#define BOARD_IMU_ADDR_B 0x6B

// TCA9554: address 0x20 (A2:A0 all low).
//   P0 = touch reset
//   P1 = LCD reset
//   P2 = SD card chip select
//   P3 = IMU interrupt 2
//   P4 = IMU interrupt 1
#define TCA9554_ADDR 0x20
#define TCA9554_PIN_TOUCH_RST 0
#define TCA9554_PIN_LCD_RST 1
