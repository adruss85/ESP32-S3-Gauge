# Board port: ESP32-S3-Touch-AMOLED-1.75

Status: **implemented in 0.6.0 and running on the board.** The firmware in this
tree targets the **ESP32-S3-Touch-AMOLED-1.75**; the 1.85 pin map, the
TCA9554 LCD reset and the 72 px value face are in git history.

This document began on 18 Nov 2026 as a research note written from
Waveshare's product wiki, hardware reference, schematic and BSP sources, plus
Espressif's `esp_lcd_co5300` driver — before the board was in hand. It is kept
because the parts that were *wrong* are the useful part: three assumptions had
to be corrected on the bench, and they are recorded below with what the glass
actually did.

## What the bench corrected

| Assumption | Reality |
|---|---|
| "The CO5300 takes RGB565 little-endian, like LVGL renders" | **Wrong.** It latches the high byte first, like the ST77916. Without the swap, white text draws light green, the green arc dark green and the near-black arc track **pink** (`0x10C3` → `0xC310`). The swap loop is back in `flushCb`. A framebuffer capture cannot catch this: it snapshots LVGL's buffer, not the wire. |
| "A 96 px value face is 466/360 of the 72 px one, so copy the dash's" | Right about the size, wrong about the glyph set: the dash's 96 px face is digits and `-` only, so the decimal point rendered as a hollow box. Regenerated with `.` included (`src/ui/font_num_96.c`). |
| "Probe the IMU at 0x6A first, then 0x6B" | Harmless but noisy: the missing address makes the I2C driver log `ESP_ERR_INVALID_STATE` before the fallback answers. Probes 0x6B first now. |

The CO5300 init sequence also had to be Waveshare's own rather than
esp-iot-solution's default: the generic table's column window comes up shifted
on this glass, and this panel's visible area starts at controller column 6.

## Decisions

| Decision | State |
|---|---|
| Target board | ESP32-S3-Touch-AMOLED-1.75 (standard, not `-B` case, not `-G` GPS) |
| Touch | **Enabled** — the CST9217 is an input device: tap = colour scheme, horizontal swipe = other screen |
| Keep the 1.85 build target | **Replaced.** One display target; the old pin map stays in git history |
| Timing | Done: the board arrived and the port is on it |

Touch stayed local to the gauge, so the car link is still receive-only
(hard rule 2).

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
   window is `0x2A = 00 06 01 D7`, `0x2B = 00 00 01 D1`. **Measured:** both
   are needed and they agree — a full-width draw lands at columns 6..471 with
   no strip at the left edge and no torn column at the right.
2. **Use Waveshare's init table, not a generic one.** The table in
   `src/hal/co5300_init.c` is theirs: sleep out, `0xFE 0x00` page select,
   `0xC4 0x80` QSPI, `0x3A 0x55`, `0x53 0x20`, `0x63 0xFF`, `0x51`,
   `0x58 0x00`, `0x35 0x00` (TEON), the two windows, `0x29`. The research note
   originally listed a longer table with `0xFE 0x20 / 0x19 0x10 / 0x1C 0xA0`
   page-tuning writes; the vendor driver this board ships with does not send
   them, and the panel comes up correctly without. Do not invent a longer
   sequence.
3. **Brightness is `0x51`, 0-255.** Upstream `esp_lcd_co5300` exposes
   `esp_lcd_panel_co5300_set_brightness(panel, percent)`. This replaces the
   LEDC backlight in `display.cpp` and `applyDimmer()`. The 8-bit emission
   register is not the same perceptual scale as the current 10-bit LEDC duty,
   so the night curve (`5 + 60 * 58s/100` percent, 85% day) is a **starting
   point measured on the bench, not a port**; it has not been checked against
   a dash dimmer in the car.
4. **No backlight gate.** "Hold the backlight at 0 until the first frame" maps
   to "set brightness 0, then the day level after the first flush". This works:
   the boot log shows no init noise on the glass.
5. **TE is available** at GPIO13 and enabled by `0x35,0x00` in the vendor
   init. **Measured:** `[lcd] TE sync on (4 edges, high 916 low 20604, rising
   edge)` — the pulse is short-high, so the flush waits for the rising edge.
6. **Even-pixel rounding.** Waveshare's BSP snaps invalidated areas to even
   x/y edges. `display.cpp` has an `LV_EVENT_INVALIDATE_AREA` rounder that
   also clamps to 465, because the panel is 466 wide and an even-snapped edge
   can overshoot by one.
7. **Byte order: the swap IS needed.** This note originally said the CO5300
   path does not swap and warned against copying the loop. That was wrong, and
   the glass said so in the first build: white text drew **light green**, the
   green arc **dark green**, and the near-black arc track **pink**. Waveshare's
   own QSPI bus (`Arduino_ESP32QSPI::writePixels` → `MSB_32_16_16_SET`)
   byte-swaps every 16-bit word on the way out. `flushCb` swaps. A capture
   cannot catch this class of bug.
8. **Rotation is flips only** in the Arduino-style CO5300 class; the IDF BSP
   uses MADCTL `MV` for 90/270. Irrelevant for a fixed-orientation gauge, but
   do not plan on rotating.
9. **Driver source.** Arduino-ESP32 does not ship CO5300. `src/hal/`
   `esp_lcd_co5300.c` is a trimmed copy of `espressif/esp-iot-solution`'s SPI
   driver (Apache-2.0): create/init/draw/gap/invert/brightness, with the
   sleep and deep-standby state machine removed because the gauge never
   sleeps the panel.

## QSPI bandwidth

Waveshare's BSP runs CO5300 at the driver default **40 MHz**; the ST77916
currently runs at **80 MHz**. Payload sizes and transfer time at 4-bit QSPI:

| Surface | 360x360 @ 80 MHz | 466x466 @ 80 MHz | 466x466 @ 40 MHz |
|---|---|---|---|
| Full frame | 259 KB, ~6.5 ms | 434 KB, ~10.9 ms | 434 KB, ~21.7 ms |
| G-meter canvas | 216 KB, ~5.4 ms | ~363 KB, ~9.1 ms | ~363 KB, ~18.2 ms |

The G-meter invalidates its whole canvas every 50 ms tail step, so at 40 MHz
that one object would occupy roughly a third of the bus. **Decision on the
bench: 80 MHz**, which works — a full frame is ~10.9 ms of bus and the panel
comes up and runs stably, with the splash, all seven schemes and the moving
G-meter exercised. 40 MHz remains the fallback if a longer soak shows trouble.

## Layout: 360 -> 466

Everything was absolute pixels for 360x360, so the scale factor is
466/360 = **1.294**. What was actually done:

| File | Change |
|---|---|
| `include/board_config.h` | new pin map, `LCD_H_RES/V_RES = 466`, gap 6, brightness percentages |
| `src/ui/gauge_ui.cpp` | `kArcSize` 360→466, stroke 21→27, tick radius 188, value row `388x124 @ 39,114`, rule `168x2 @ 149,280`, IAT at 313 |
| `src/ui/gmeter_ui.cpp` | canvas `360x300 @ y=60` → `466x388 @ y=78`, base 180→233, travel 72→93, blob 50→65, tail 24→31 px |
| `src/hal/display.cpp` | draw buffer 466x48 = **44,736 B** |
| `src/ui/font_num_96.c` | the dash's 96 px face, regenerated with `.` as well as digits and `-` |
| `include/theme.h`, `include/lv_conf.h` | label faces 20/26/30/48; the 12/14/18/22/24/40 stock faces are dropped |
| `src/ui/splash.cpp`, `splash_img.cpp` | root 466x466; `assets/canvas-gauge-splash-466-base.jpg` (lanczos upscale of the 360 art); decode buffer 434,312 B PSRAM, **147 ms measured** |
| `tools/capture_frame.py`, `GAUGE_CAPTURE` | 466x466, seven schemes |

Budgets as built: static RAM 118,464 B (36.2%), flash 1,422,967 B (21.7% of a
6.25 MB OTA slot), G-meter canvas 361,616 B PSRAM, splash buffer 434,312 B
PSRAM while it is up.

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

## G-meter axes

The QMI8658 is the same part at the same address, so `imu.cpp` needed only the
I2C pins. The chip sits differently on this PCB, so the defaults recorded for
the 1.85 ("upright uses Z, flat uses Y, +Z away from the glass") do not
automatically carry over. The defaults were kept as-is and
`tools/gmeter_test.cpp` still passes; the at-rest zero reads `avg=0.00` on the
desk. **The upright/flat axis and sign have not been re-derived on this PCB**:
that needs a real acceleration and a braking pull in the car. Flip accel/brake
covers a 180 degree mount, and Mount Auto resolves upright/flat from gravity.

## Touch (enabled)

CST9217 at `0x5A` on the shared I2C bus, INT GPIO11, RST GPIO40. Waveshare's
BSP sets `swap_xy=0, mirror_x=1, mirror_y=1`; this firmware uses the same
flags and **polls** instead of using the interrupt, because SensorLib notes
the CST9217 raises INT about once a second rather than per touch.

Decided and built:

- **A tap steps the colour scheme.** That is the same action as a BOOT short
  press, which is deliberate: the glass is the natural place to press it, and
  the two paths end in one function.
- **A horizontal swipe slides to the other screen**, with a 280 ms push
  animation. Vertical movement is ignored so a sloppy tap is not a swipe.
- Gestures are refused while the splash owns the glass and while a transition
  is in flight.
- `TP_RST` (GPIO40) is driven deliberately at boot; it was the QSPI clock on
  the old board.
- The touch controller, PMIC, RTC and both audio codecs share the IMU's I2C
  bus at 400 kHz. `display::begin()` initialises it once.

Still open: whether a tap should wake a dimmed screen, and how the round glass
edge should reject touches outside the visible circle.

## What the port actually touched

1. `board_config.h` + `display.cpp` + the vendored CO5300 driver: bring-up,
   gap 6, 80 MHz, byte order, TE probe, brightness API.
2. `tca9554` deleted; `Wire` moved to 15/14; IMU probed at `0x6B` first.
3. Relayout: geometry constants, 96 px font, 466 splash, capture tool; seven
   schemes plus G-meter and splash captured and inspected.
4. Touch: bring-up, LVGL pointer indev, tap and swipe, `TP_RST` handling.
5. Seven schemes copied from `OpenHaldex-S3-Dash`, with an NVS migration.
6. `README.md`, `docs/status.md`, `TODO.md`, `AGENTS.md` board facts and the
   budget table.

## Checks that cannot be skipped on hardware

- Colour byte order. **This is the one that bit**: a capture is LVGL's
  framebuffer, so only the glass can show a swapped panel.
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
