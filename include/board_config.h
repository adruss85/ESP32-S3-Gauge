// =====================================================================
// board_config.h - Waveshare ESP32-S3-Touch-AMOLED-1.75 (N16R8)
//
// 1.75" round AMOLED panel, 466 x 466, CO5300 driven over QSPI. The panel
// reset is a plain GPIO on this board, and the emission register (0x51)
// replaces the old LEDC backlight: an AMOLED has no backlight to dim.
//
// The touch controller (CST9217) is on the same I2C bus as the IMU, RTC and
// PMIC, so the bus is initialised once, here, and shared.
//
// Pin map from Waveshare's pin_config.h, the board wiki and the schematic.
// The 1.85 board's numbers are NOT interchangeable: D0 was 46 (now PA_CTRL),
// CS was 21 (now IMU INT2), SCK was 40 (now touch reset) and the backlight
// was 5 (now QSPI data1).
// =====================================================================
#pragma once

#include <stdint.h>

#define BOARD_NAME "Waveshare ESP32-S3-Touch-AMOLED-1.75"

// ---------------------------------------------------------------- panel
#define LCD_H_RES 466
#define LCD_V_RES 466

// CO5300 QSPI: one clock and four data lines.
#define LCD_PIN_SCK 38
#define LCD_PIN_D0 4
#define LCD_PIN_D1 5
#define LCD_PIN_D2 6
#define LCD_PIN_D3 7
#define LCD_PIN_CS 12
#define LCD_PIN_TE 13  // tearing-effect output, enabled by TEON 0x35 in init
#define LCD_PIN_RST 39

// QSPI clock. Waveshare's BSP runs the CO5300 at 40 MHz; 80 MHz is measured
// stable on this unit (a full frame is ~10.9 ms of bus), so the gauge keeps
// the higher rate and falls back by editing this one line.
#define LCD_QSPI_HZ (80 * 1000 * 1000)

// The controller RAM is 480 wide but the visible glass is 466, starting at
// column 6. Without the gap the picture is shifted and the right edge torn.
#define LCD_GAP_X 6
#define LCD_GAP_Y 0

// AMOLED emission (CO5300 register 0x51), not a backlight PWM. The 8-bit
// register is not the same perceptual scale as the old 10-bit LEDC duty, so
// these percentages are a starting point measured on the bench, not a port
// of the 1.85 curve.
#define LCD_BRIGHTNESS_DAY_PCT 85
#define LCD_NIGHT_MIN_PCT 5
#define LCD_NIGHT_SPAN_PCT 60

// BOOT button, active low. Holding it at reset still enters download mode;
// after boot it is a plain user input.
#define BOARD_PIN_BOOT 0
#define BOARD_BOOT_DEBOUNCE_MS 30

// ---------------------------------------------------- I2C control bus
// TCA9554 expander, AXP2101 PMIC, PCF85063 RTC, QMI8658 IMU and CST9217
// touch share this bus. Initialise it once (display::begin does).
#define BOARD_I2C_SDA 15
#define BOARD_I2C_SCL 14
#define BOARD_I2C_HZ 400000

// QMI8658 6-axis IMU. The SA0 strap selects 0x6A or 0x6B; this board
// answers at 0x6B, but the driver probes 0x6A first so a different strap
// does not need a rebuild.
#define BOARD_IMU_ADDR_A 0x6A
#define BOARD_IMU_ADDR_B 0x6B

// CST9217 capacitive touch. Reset is GPIO40 (the QSPI clock on the old
// board), interrupt GPIO11 (the old I2C SDA), address 0x5A.
#define BOARD_TOUCH_ADDR 0x5A
#define BOARD_TOUCH_PIN_INT 11
#define BOARD_TOUCH_PIN_RST 40

// The controller reports in panel coordinates; the mapping to LVGL's is
// these two flags. Waveshare's BSP uses swap_xy=0, mirror_x=1, mirror_y=1,
// which is what the bench check confirmed. A mount that turns the glass
// around is a bezel question, not a software one.
#define BOARD_TOUCH_SWAP_XY 0
#define BOARD_TOUCH_MIRROR_X 1
#define BOARD_TOUCH_MIRROR_Y 1

// TCA9554: address 0x20 (A2:A0 all low). On this board P0-P2 are free test
// points, P3 RTC_INT, P4 SYS_OUT (power key sense), P5 AXP_IRQ, P6 QMI_INT1
// and P7 GPS_RST. All of them belong to other chips, so the expander is
// left at its power-on default (every pin an input) and never driven.
#define TCA9554_ADDR 0x20
