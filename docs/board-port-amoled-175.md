# Board port: ESP32-S3-Touch-AMOLED-1.75

Status: **research note, no code changed.** The firmware in this tree still
targets the Waveshare ESP32-S3-LCD-1.85. This document records what a move to
the **ESP32-S3-Touch-AMOLED-1.75** would involve, so the work can start when
the unit arrives.

Written 18 Nov 2026 from Waveshare's product wiki, hardware reference,
schematic and BSP sources, and Espressif's `esp_lcd_co5300` driver. **Nothing
in this document has been measured on the AMOLED board.** The 1.85 unit that
this repository was developed against has a failed panel.

## Decisions

| Decision | State |
|---|---|
| Target board | ESP32-S3-Touch-AMOLED-1.75 (standard, not `-B` case, not `-G` GPS) |
| Touch | **Enabled** — the CST9217 will be used as an input device |
| Keep the 1.85 build target | **Open.** Recommendation: replace it, leave the old pin map in git history |
| Timing | Do not start until the board is in hand |

Touch is a new input path, so it needs its own UI decision (what a tap does)
before implementation. It must stay local to the gauge: the car link remains
receive-only (hard rule 2).

## What the board is

| Piece | ESP32-S3-LCD-1.85 (current) | ESP32-S3-Touch-AMOLED-1.75 |
|---|---|---|
| SoC | ESP32-S3R8, 16 MB flash, 8 MB OPI PSRAM | same |
| Panel | 360x360 IPS, ST77916, QSPI | 466x466 **AMOLED**, CO5300, QSPI |
| Panel reset | TCA9554 bit 1 | GPIO39, direct |
| Backlight | GPIO5, LEDC PWM | none — emission register `0x51` |
| TE | GPIO18 | GPIO13, already enabled by the vendor init |
| I2C | SDA 11 / SCL 10 | **SDA 15 / SCL 14** |
| IMU | QMI8658 `0x6B` | QMI8658 `0x6B` on the shared bus |
| Expander | TCA9554 `0x20` | TCA9554 `0x20`, **all pin roles different** |
| Touch | separate I2C on GPIO1/3, unused | CST9217 `0x5A`, INT 11, RST 40 |
| Power | USB-C only | AXP2101 `0x34` PMIC, 5 V on the 8-pin header, battery connector |
| USB | native CDC, GPIO19/20 | same |
| BOOT | GPIO0 | GPIO0 |
| PWR button | battery latch | AXP2101 power key (not a GPIO) |
| Extra | — | RTC PCF85063, ES8311/ES7210 audio, TF slot, 3 GPIOs + UART0 on a header |

Mechanical: Ø46.00 mm PCB, Ø48.96 mm glass bezel, 6.90 mm PCB depth
(10.40 mm including the USB-C shell), 4x M2 holes on 34 mm centres, 2.54 mm
8-pin header on the back bottom edge, test points for SDA/SCL/EX0-EX2, PWR on
the left edge, BOOT bottom-left, USB-C right edge. The 1.75" active area is
slightly **smaller** than the current 1.85" panel, at higher pixel density
(466 px over ~43.8 mm vs 360 px over ~47 mm), so the pod needs a new bezel and
clearance behind for the header.

## What carries over

Unchanged: ESP-NOW receive and channel scan, `OHX_STATUS`/CANN decodes, NVS
settings, web page and OTA, BOOT key handling, the G-meter filter/gravity/zero
maths, the LVGL 9 + `esp_lcd` structure, the 16 MB partition table with two
6.25 MB OTA slots, and the headless `esp32dev` environment.

The port is concentrated in the panel driver, the 360→466 relayout, the board
pin map, and re-validating the IMU axes.

## The panel: CO5300 specifics

1. **Column gap.** The visible 466 columns start at column 6 of a 480-wide
   controller RAM. Call `esp_lcd_panel_set_gap(panel, 6, 0)`; the vendor init
   window is `0x2A = 00 06 01 D7`, `0x2B = 00 00 01 D1`.
2. **Use Waveshare's init table, not a generic one.** It begins with
   `0xFE,0x20 / 0x19,0x10 / 0x1C,0xA0` (page select and panel tuning) before
   the standard `0xC4 0x80`, `0x3A 0x55`, `0x35 0x00`, `0x53 0x20`,
   `0x51 0xFF`, `0x63 0xFF`, `0x2A/0x2B` windows, `0x11`, `0x29`. A generic
   CO5300 sequence can come up blank on this glass.
3. **Brightness is `0x51`, 0-255.** Upstream `esp_lcd_co5300` exposes
   `esp_lcd_panel_co5300_set_brightness(panel, percent)`. This replaces the
   LEDC backlight in `display.cpp` and `applyDimmer()`. The 8-bit emission
   register is not the same perceptual scale as the current 10-bit LEDC duty,
   so the night curve (`5 + 60 * 58s/100` percent, 85% day) must be
   re-measured, not assumed.
4. **No backlight gate.** "Hold the backlight at 0 until the first frame" maps
   to "set brightness 0, then the day level after the first flush".
5. **TE is available** at GPIO13 and enabled by `0x35,0x00` in the vendor
   init. The existing `onRenderReady`/`teIsPulsing` logic ports with a new
   pin, and the G-meter needs it.
6. **Even-pixel rounding.** Waveshare's BSP snaps invalidated areas to even
   x/y edges. Add an `LV_EVENT_INVALIDATE_AREA` rounder and expect the flush
   area to grow by up to one pixel/row.
7. **Byte order is unverified for this panel.** The current flush callback
   byte-swaps every RGB565 word for the ST77916. Waveshare's CO5300 path does
   **not** swap, and the IDF driver ignores `data_endian`. The first glass
   test must be a colour bar; wrong order shows as swapped or scrambled
   colour. Do not copy the swap loop blindly.
8. **Rotation is flips only** in the Arduino-style CO5300 class; the IDF BSP
   uses MADCTL `MV` for 90/270. Irrelevant for a fixed-orientation gauge, but
   do not plan on rotating.
9. **Driver source.** Arduino-ESP32 does not ship CO5300. Vendor
   `espressif/esp-lcd-co5300` (Apache-2.0, `espressif/esp-iot-solution`) into
   `src/hal/` the way `esp_lcd_st77916.c` is vendored today, and confirm the
   Arduino core's IDF version meets the driver's requirement (>= 5.4; the
   2026 2.x driver wants the newer `esp_lcd_panel_disp_sleep` API).

## QSPI bandwidth

Waveshare's BSP runs CO5300 at the driver default **40 MHz**; the ST77916
currently runs at **80 MHz**. Payload sizes and transfer time at 4-bit QSPI:

| Surface | 360x360 @ 80 MHz | 466x466 @ 80 MHz | 466x466 @ 40 MHz |
|---|---|---|---|
| Full frame | 259 KB, ~6.5 ms | 434 KB, ~10.9 ms | 434 KB, ~21.7 ms |
| G-meter canvas | 216 KB, ~5.4 ms | ~363 KB, ~9.1 ms | ~363 KB, ~18.2 ms |

The G-meter invalidates its whole canvas every 50 ms tail step, so at 40 MHz
that one object would occupy roughly a third of the bus. Try 80 MHz early
with a solid-colour plus moving-blob soak and keep 40 MHz as the fallback.
If 40 MHz is the ceiling, shrink the canvas or invalidate only the dirty
bounding box instead of the whole canvas.

## Layout: 360 -> 466

Everything is absolute pixels for 360x360, so the scale factor is
466/360 = **1.294**.

| File | Change |
|---|---|
| `include/board_config.h` | new pin map, `LCD_H_RES/V_RES = 466` |
| `src/ui/gauge_ui.cpp` | `kArcSize 360→466`, stroke 21→27, tick radius, value row `300x96 @ 30,88`, rule `130x1 @ 115,216`, all align offsets |
| `src/ui/gmeter_ui.cpp` | screen 360→466, canvas `360x300 @ y=60` → `466x390 @ y=60`, base 180→233, travel 72→93, blob 50→65 |
| `src/hal/display.cpp` | draw buffer 360x60 → 466x60 = **55,920 B**; use 466x48 (44.7 KB) to hold the current RAM budget |
| `src/ui/font_num_72.c` | regenerate as a ~93 px subset (digits, `.`, `-` only) |
| `include/theme.h`, `include/lv_conf.h` | label faces 20/24/40 → 26/31/52; enable the new stock sizes |
| `src/ui/splash.cpp`, `splash_img.cpp` | root 360x360 → 466x466; new `assets/*-466-base.jpg`; decode buffer 434 KB PSRAM (~150 ms vs 94 ms) |
| `tools/capture_frame.py`, `GAUGE_CAPTURE` | reads/writes 360x360; needs the new geometry |

Budgets that move: static RAM +12.7 KB at 466x60 (from 116,632 B, 35.6%),
G-meter canvas 216 KB → ~363 KB PSRAM, splash buffer 259 KB → 434 KB PSRAM.
Flash grows by roughly 60 KB (driver, larger fonts, larger JPEG) against a
6.25 MB OTA slot, so it is not a risk.

## Pin map (new board)

| Signal | GPIO |
|---|---|
| QSPI D0/D1/D2/D3 | 4 / 5 / 6 / 7 |
| QSPI SCK / CS | 38 / 12 |
| LCD reset | 39 |
| TE | 13 |
| I2C SDA / SCL | 15 / 14 |
| Touch INT / RST | 11 / 40 |
| IMU INT2 | 21 (INT1 on expander P6) |
| SD CMD/CLK/D0/D3 | 1 / 2 / 3 / 41 |
| Audio (unused) | 8, 9, 10, 42, 45, 46 |
| Header | IO18, IO17, IO16, RXD, TXD, 3V3, GND, VBUS |
| UART0 | 43 / 44 (header, also console fallback) |

**The old pin numbers are different functions on this board.** QSPI D0 was 46
(now PA_CTRL), CS was 21 (now IMU INT2), SCK was 40 (now touch reset), the
backlight was 5 (now QSPI data1), and I2C was 11/10 (now touch INT and I2S
DIN). Reusing `board_config.h` values would drive audio, SD and display lines
against each other.

### TCA9554 roles

P0-P2 free test points, P3 `RTC_INT`, P4 `SYS_OUT` (PWR button sense),
P5 `AXP_IRQ`, P6 `QMI_INT1`, P7 `GPS_RST`. The current `kConfig = 0x18` drives
P5/P6/P7 as outputs, which would hold the PMIC IRQ and IMU INT1 low and reset
the GPS. Either configure P3-P7 as inputs (`0xF8`) or drop expander init
entirely: the panel no longer needs it.

## Power and the 5 V input

The 8-pin header is IO18, IO17, IO16, RXD, TXD, 3V3, GND, **VBUS**. `3V3` is
the AXP2101 DCDC1 *output*; power the board through **VBUS + GND**.

- The AXP2101 takes VBUS (~4.5-5.5 V), provides the power path and boots on
  VBUS insertion with no battery and no button press. No PMIC code is needed
  to light the display; Waveshare's BSP never initialises it. AXP2101 support
  is only needed for battery/charge telemetry or a controlled shutdown.
- **Header VBUS is the same rail as USB-C VBUS.** Do not feed the header
  while a USB cable is plugged in unless the revision's protection is
  confirmed. Put a Schottky or ideal diode in the harness, plus a fuse and
  TVS: automotive "5 V" bucks spike and AXP2101 VBUS is not a load-dump
  input.
- The MX1.25 battery connector lets the gauge ride through cranking, but a
  LiPo in a hot cabin is a safety decision, not just a feature.
- PWR is a hardware power key (long press removes power), not a GPIO. The
  PMIC exposes it on expander P4 if it is ever wanted as an input.
- A 5 V feed must carry the boot surge and WiFi TX peaks (~0.5 A).

## AMOLED risks

- **Burn-in.** A static arc, fixed tick labels and captions are lit for
  hours; that is the pattern that ghosts on an emissive panel. The IPS did
  not care. Mitigate with a capped day level, the night curve, and possibly
  an idle screen-off after a period without `OHX_STATUS`.
- **Sunlight readability.** AMOLED is typically dimmer than a bright IPS in
  direct sun; check in the car before committing to the pod design.
- **Low-brightness behaviour.** Check the 0-255 emission register for
  flicker/banding at night levels; it may be better or worse than LEDC.
- **Upside:** true black and fast pixel response suit both the night gauge
  and the moving G-meter dot.

## G-meter axes must be re-derived

The QMI8658 is the same part at the same address, so `imu.cpp` needs only the
I2C pins. But the chip sits differently on this PCB, so the defaults recorded
for the 1.85 ("upright uses Z, flat uses Y, +Z away from the glass") do not
carry over. Re-run the `tools/gmeter_test.cpp` cases unchanged, then re-derive
the upright/flat axis and sign on the board and update the documented default
and the mount setting. Flip accel/brake still covers a 180 degree mount.

## Touch (enabled)

CST9217 at `0x5A` on the shared I2C bus, INT GPIO11, RST GPIO40. Waveshare's
BSP sets `swap_xy=0, mirror_x=1, mirror_y=1` and uses the interrupt, with the
controller's `x_max/y_max` at 466. Enable it as an LVGL pointer input.

Open before implementation:

- What a tap does. The gauge currently has BOOT short (accent) and long
  (screen); touch must not duplicate or fight those, and an accidental tap
  must not change a stored setting.
- Whether taps are consumed only on the gauge screen, and whether a tap wakes
  a dimmed screen.
- `TP_RST` (GPIO40) must be driven deliberately at boot; it was the QSPI clock
  on the old board.
- The touch controller, PMIC, RTC and both audio codecs now share the IMU's
  I2C bus at 400 kHz. Initialise the bus once.

## Suggested port order

Each step is verifiable on its own; stop and report at each one.

1. `board_config.h` + `display.cpp`: CO5300 bring-up, gap 6, 40/80 MHz sweep,
   colour bar, orientation, byte order, TE probe, brightness API. Keep the
   gauge UI out of the build until the glass is right.
2. `tca9554` config or removal, `Wire` pins, IMU probe at `0x6B`,
   `/api/status` `g100` at rest.
3. Relayout: geometry constants, 93 px font, 466 splash, capture tool; then
   capture all six schemes plus G-meter and splash and inspect the images.
4. Touch: bring-up, LVGL indev, tap behaviour, `TP_RST` handling.
5. G-meter axes/mount/flip for the new PCB.
6. Update `README.md`, `docs/status.md`, `TODO.md`, `AGENTS.md` board facts
   and the budget table (draw buffer, canvas, QSPI clock, panel ID).

## Checks that cannot be skipped on hardware

- Panel init leaves a blank/white screen unless the vendor `0xFE` page
  commands are sent.
- Colour byte order (colour bar, then a scheme capture).
- 80 MHz QSPI stability under a moving G-meter.
- `[lcd] TE sync on` with a plausible edge count on GPIO13.
- Brightness at 5% and 85%, and the 58s night curve in the car.
- Touch coordinates against the four screen edges and the round mask.
- G-meter direction under a real acceleration and braking pull.
- 5 V feed without USB attached, and a brownout/crank check if a battery is
  not fitted.

## Sources

- Waveshare wiki: <https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75>
- Board examples, hardware reference and BSP:
  <https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75>
- Schematic:
  <https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75/ESP32-S3-Touch-AMOLED-1.75.pdf>
- CO5300 driver:
  <https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd/esp_lcd_co5300>
