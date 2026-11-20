# AGENTS.md

Guidance for AI coding agents working on **ESP32-S3-Gauge**. Read this first;
detail lives in `README.md` and the files it points to.

## First thing on every task: is this tree current?

Before reading, editing, building or releasing anything, check that the working
directory matches GitHub. A stale checkout silently undoes work: 0.9.0 of the
OpenHaldex S3 Dash was published from another machine while an agent was working
on a two-commit-old tree, and the whole change had to be redone against the real
code.

```bash
git fetch origin
git status -sb                  # the branch line must not end in [behind N]
git log --oneline HEAD..origin/main
```

If it is not current, **stop and ask before touching anything** - do not pull on
your own. Use this shape, with the real status filled in:

```
Tree isn't current - behind 2 commits on main - do you want to pull the latest?
```

* **Behind** — ask; pull only after the user says yes (`git merge --ff-only
  origin/main`). If the fast-forward refuses, say why and let them decide.
* **Ahead** — unpushed local commits: say so; do not push unless asked.
* **Diverged** — stop and ask, with both counts (ahead/behind). Never rebase or
  rewrite published history without being told to.
* A dirty working tree is normal mid-task; it is not the same as stale.

Never decide a version or cut a release before this check has passed.

## What this is

A standalone **digital boost gauge** on a **Waveshare
ESP32-S3-Touch-AMOLED-1.75** (ESP32-S3R8, 16 MB flash, 8 MB OPI PSRAM,
466x466 round CO5300 AMOLED over QSPI, CST9217 touch).
It is a **receive-only display client** for the OpenHaldex family:

* boost, rpm, speed, throttle, mode from the controller's `OHX_STATUS`
  ESP-NOW broadcast (`include/ohx_proto.h`);
* intake air temperature from `Engine_3` (`0x380`) in the CANN relay
  (`include/cannow_proto.h`);
* the AP name `OpenHaldex-C6` (preferred), or the dash's own `CANv-dash`
  (formerly `ohx-dash`), supplies the WiFi channel. Nothing is transmitted.

Stack: PlatformIO, Arduino-ESP32, LVGL 9.3, Espressif `esp_lcd`, and a
trimmed vendor CO5300 driver. C++ in `src/`, headers in `include/`.

The QMI8658 on the control I2C bus adds a **G-meter** screen: a 2D dot
(vertical acceleration/braking, horizontal lateral), a fading 1 s tail and a
1 s two-axis magnitude readout. It is local to the board; the car link
stays receive-only.

The CST9217 touch panel is the second input: a **tap steps the colour
scheme**, a **horizontal swipe slides to the other screen**. Also local.

The gauge also runs its own always-on open AP, **CANv-gauge**, serving a
settings page (colour scheme, boost scale, display mode, G-meter mount) and OTA
firmware upload. The AP shares the ESP-NOW radio, so it follows the receiver's
channel and is the only RF the firmware originates.

`env:esp32s3` is the real display target. `env:esp32dev` is a headless CANN
listener for bench work and builds without the display code.

**The 1.85 unit's panel failed**, and 0.6.0 moved the build to the AMOLED
board. The old pin map, the TCA9554 LCD reset and the 72 px value face live in
git history. `docs/board-port-amoled-175.md` records what the move involved
and which assumptions the bench corrected.

## Out of scope

The **OpenHaldex-C6 controller firmware** and the **bodycan sender** are
separate repositories. Report upstream problems, do not patch them here.
The gauge never sends ESP-NOW or CAN traffic and never actuates the car.

## Ignore these directories

Do not read, grep, index or edit: `.pio/`, `.venv/`. Build output and local
tooling. Scope searches to `src/`, `include/`, `tools/`, `assets/` and the
project `*.md` files. `compile_commands.json` is not present here.

## Build, flash, verify

```bash
# one-off
python3 -m venv .venv && ./.venv/bin/pip install platformio pyserial

# display board (native USB CDC)
./.venv/bin/pio run -e esp32s3
./.venv/bin/pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0

# OTA over the gauge's own AP (join CANv-gauge first)
curl -F "firmware=@.pio/build/esp32s3/firmware.bin" http://192.168.4.1/update

# headless WROOM listener (CH340)
./.venv/bin/pio run -e esp32dev
./.venv/bin/pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0
```

* Display serial console: 115200 baud on `/dev/ttyACM0`. Opening it resets
  the board; `tools/monitor.py` opens once and captures the boot log.
* Upload speed is 921600 for the S3; the WROOM's CH340 is pinned to 460800.
* **There is no hardware test suite.** A successful `pio run` is the only
  check you can do without hardware. Anything touching the panel, the IMU,
  ESP-NOW, the BOOT key or IAT decoding needs the board or a car. Say clearly
  what was and was not verified on hardware.
* Host-only checks live in `tools/`: `gmeter_test.cpp` (filter, axes, average,
  speed gate, zero) and `boot_button_test.cpp` (short/long/bounce). Build
  commands are at the top of each file. They do not touch hardware.
* After a build, report the RAM and flash lines (see Budgets).

### Capturing the glass without a camera

`-DGAUGE_CAPTURE=1` makes the firmware render a fixed reading and stream each
scheme's 466x466 RGB565 framebuffer over USB. Use it to check layout changes:

```bash
PLATFORMIO_BUILD_FLAGS="-DGAUGE_CAPTURE=1" \
  ./.venv/bin/pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0
./.venv/bin/python tools/capture_frame.py /dev/ttyACM0 /tmp/gauge
```

This produces one PPM per scheme (midnight, orange, green, blue, purple,
white, red). The capture code is compiled out of normal builds and does not
write NVS. Add `-DGAUGE_CAPTURE_BOOST=-2.5f` to photograph a vacuum reading; a
custom scale is read from NVS, so set it from the web page first if needed.

A capture is LVGL's framebuffer, not the panel: it cannot show a wrong byte
order, a wrong gap or a wrong brightness. Those still need an eye on the glass.

`-DGAUGE_CAPTURE_GMETER=1` (with `GAUGE_CAPTURE=1`) captures the G-meter
instead, feeding a synthetic 0.5 Hz sweep so the blob, tail and readout all
appear. `-DGAUGE_CAPTURE_SPLASH=1` captures the boot screen at 2 s, with the
loading rule part-filled. Use a distinct output prefix, e.g. `/tmp/gmeter`.

## Hard rules

1. **Never edit `include/ohx_proto.h` or `include/cannow_proto.h`.** They are
   verbatim shared copies of the controller's headers. Refresh from upstream
   only; bump `CANN_PROTO` in every copy together.
2. **The gauge is receive-only for the car.** Do not add ESP-NOW sends, CAN
   transmits or any control path. It observes; the controller acts. The
   setup AP, its HTTP responses and OTA flash writes are local to the gauge
   and must never become a way to control the car.
3. **Unknown is `NaN`, drawn as `--`. Never substitute a fake zero.** A
   packet "no data" sentinel (e.g. `OHX_NONE16`, IAT `0xFF`) travels to the
   screen as unknown. A genuine `0` draws `0`. The web status JSON follows
   the same rule with `null`, and only when the reading is fresh.
4. **The channel comes from an AP, not a compiled-in constant.** Scan for
   `OpenHaldex-C6` first, then `CANv-dash` (then the legacy `ohx-dash`); fall
   back to channel 1 and hop only
   while quiet. Do not hard-code a per-rig channel. The setup AP shares the
   radio, so while it is up the channel walk is disabled and the periodic
   full scan is the discovery path.
5. **Layout geometry and the palette live in `include/theme.h`.** The seven
   runtime schemes are in `src/ui/theme.cpp`; `gauge_ui.cpp` only applies
   them. Nothing scales: all values are absolute pixels for 466x466. The
   boost scale (min/max) is a stored setting applied by
   `gauge_ui::applyScale()`, not a compile-time constant.
6. **LVGL is configured for exactly what the gauge draws** (`include/lv_conf.h`).
   Every unused widget, layout and colour-format path is pinned to 0 with the
   reason beside it. Do not enable a widget without removing another, and do
   not add a full-screen redraw path.
7. **Fonts are deliberate.** The 96 px boost face is a Montserrat Bold subset
   (digits, `.` and `-`, U+002D..U+0039) generated from `OpenHaldex-S3-Dash`'s
   face; the labels and IAT use stock Montserrat faces. The value decimal point
   is a `.` glyph from that subset, not a drawn circle — regenerating the face
   without `.` renders the dot as a hollow box. The `-` is why a negative scale
   minimum can be drawn.
8. **No credentials in git.** The controller's AP password is not stored
   here; the gauge only listens.
9. **Honesty about verification.** Bench != car. Boost has only been seen as
   `0` or the absent sentinel; IAT has only been seen on the bench with a
   CANN sender present. Do not describe those as verified in a car.
10. **The QSPI flush is asynchronous.** LVGL must not get its buffer back
    before the QSPI DMA transfer completes; the panel would show torn or
    stretched content. Keep the `on_color_done` callback wired to
    `lv_display_flush_ready`.
11. **The G-meter zero is learned, never faked.** Gravity is removed by a
    tracker that updates only when the car is stationary (controller speed
    under 1 km/h when available, otherwise `| |a| - 1 g | < 0.06 g` plus a
    quiet gyro). Do not replace it with a fixed boot offset. A Zero with no
    IMU sample must leave the readout at `--`. The two-axis magnitude is
    `sqrt(v² + l²)` and its 1 s mean, not `(|v| + |l|) / 2`.
12. **The G-meter mount is a setting, not an assumption in pixels.** The
    screen uses the chip's Z axis for an upright gauge and Y for a flat one;
    `Auto` resolves that from the learned gravity vector, and `Flip
    accel/brake` handles a 180 degree mount. Keep the setting in NVS and
    apply it through `gmeter::setMount()`.
13. **BOOT owns two actions.** A short press steps the scheme, a 700 ms hold
    switches screens. Keep the long press firing at the threshold while the
    key is still down, and swallow its release. Do not reintroduce the old
    ISR quick-tap path: a bounce on the way into a long press would emit a
    spurious accent change.
14. **Touch is a local input, and the two gestures are the whole vocabulary.**
    A tap steps the scheme, a horizontal swipe slides to the other screen;
    nothing else. Gestures are classified in `src/hal/touch.cpp` and delivered
    through `dashboard::onGesture()`, which refuses them while the splash owns
    the glass or a transition is in flight — a second swipe mid-slide would
    leave a root at a stale offset. The touch controller shares the control
    I2C bus; never re-`Wire.begin()` it with different pins.
15. **RGB565 goes to the panel high byte first.** LVGL renders little-endian;
    `flushCb` swaps every 16-bit pixel before `esp_lcd_panel_draw_bitmap`.
    Removing that loop is not a subtle failure: white text draws light green,
    the green arc dark green, and the near-black arc track pink.
16. **A stored scheme index is a wire format.** `theme::Scheme` order is the
    order `OpenHaldex-S3-Dash` uses (Red last, because it was added last
    there). Reordering repaints every car that has a scheme in NVS; add a new
    scheme at the end and bump `kSchemeVersion` in `settings.cpp` with a
    migration if the meaning of an existing number changes.

## Budgets: do not regress these

| Resource | Limit / baseline | Why it matters |
|---|---|---|
| App flash | OTA slot is **6,553,600 B** (0x640000). 0.6.0 used **1,422,999 B (21.7%)** | Two OTA slots plus SPIFFS share 16 MB; re-measure after your change |
| Internal RAM | **118,464 B (36.2%)** static at 0.6.0 | WiFi/ESP-NOW/AP stacks and DMA buffers need the rest |
| LVGL pool | **128 KB** (`LV_MEM_SIZE`), allocated from PSRAM | Keep the pool out of internal RAM |
| Draw buffers | 1 x 466x48 RGB565 = **44,736 B** in internal RAM | One partial buffer, serialised flushes; `LV_DRAW_BUF_ALIGN` aligned |
| G-meter canvas | **361,616 B** in PSRAM | 466x388 RGB565, one object; not static RAM |
| Splash picture | **434,312 B** in PSRAM, freed on delete | 466x466 RGB565, only while the splash is up |
| QSPI clock | 80 MHz | The vendor panel sequence assumes it |

Rules of thumb: no unconditional `lv_obj_invalidate`; style changes already
invalidate only the affected objects; keep the arc as the only large
anti-aliased shape; do not add scrolling containers. OTA and the page are
allowed to allocate at runtime (they are not always-on hot paths), but a
page request must not add a full-screen repaint. For the mode fade, scale
leaf-object opacity; a container opacity would allocate a 434 KB layer and
fail. The G-meter's dot, tail and readout are one 466x388 canvas; its
361,616 B buffer is PSRAM, not the LVGL pool, and only the canvas is
invalidated when something changes. Do not go back to per-dot objects.

## Where things are (task to files)

| Task | Touch |
|---|---|
| Gauge layout, value row, ticks | `src/ui/gauge_ui.cpp`, geometry in `include/theme.h` |
| Boost scale (min/max), tick labels | `gauge_ui::applyScale()` in `src/ui/gauge_ui.cpp` |
| Colour schemes / BOOT short press | `src/ui/theme.cpp`, `src/hal/boot_button.cpp` (poll + 700 ms long press) |
| Touch tap / swipe classification | `src/hal/touch.cpp`, `onGesture()` in `src/ui/dashboard.cpp` |
| Cruise stalk: Cancel+Down accent, Down-long mode | `takeSchemeEvent()` / `takeModeEvent()` in `src/net/ohx_now.cpp` |
| Mode switch: 260 ms fade, 280 ms slide | `src/ui/dashboard.cpp`, `setMode(..., Anim)` |
| Boot splash + 3 s loading rule | `src/ui/splash.cpp`, `src/ui/splash_img.cpp`, `assets/canvas-gauge-splash-466-base.jpg` |
| G-meter math (gravity, axes, 1 s average) | `src/app/gmeter.cpp` |
| QMI8658 init + burst read | `src/hal/imu.cpp` |
| G-meter dot, tail, readout (one canvas) | `src/ui/gmeter_ui.cpp`, `include/theme.h` |
| Persisted scheme/scale/mode/mount (NVS) | `src/app/settings.cpp` |
| Web page, settings API, OTA, G zero | `src/net/gauge_web.cpp` |
| Setup AP channel policy | `hopTick()` and `setSetupAp()` in `src/net/ohx_now.cpp` |
| Panel, QSPI, LVGL display, TE sync, pixel swap | `src/hal/display.cpp`; vendor driver `esp_lcd_co5300.c`, sequence `co5300_init.c` |
| Touch controller (CST9217) | `src/hal/touch.cpp` |
| ESP-NOW receive, channel scan | `src/net/ohx_now.cpp` |
| IAT decode (`0x380` byte 1) | `handleCann()` in `src/net/ohx_now.cpp` |
| Console keys | `handleSerial()` in `src/net/ohx_now.cpp`; `src/main.cpp` for `b`/`m`/`z`/`t`/`g`/`a` |
| Boot banner / version | `include/version.h` |
| Board pin map | `include/board_config.h` |
| Frame capture | `GAUGE_CAPTURE` in `src/main.cpp`, `tools/capture_frame.py` |
| Host math/button checks | `tools/gmeter_test.cpp`, `tools/boot_button_test.cpp`, `tools/host/` |

## How to verify a change

| Change type | Check |
|---|---|
| Any | `pio run -e esp32s3` and `pio run -e esp32dev` succeed; report size lines |
| Layout or colour | Capture the seven schemes (above) and inspect the PPMs; repeat with `GAUGE_CAPTURE_GMETER` for the G-meter and `GAUGE_CAPTURE_SPLASH` for the boot screen. A capture is LVGL's framebuffer, not the glass — colour order and brightness still need an eye on the panel |
| Data path | Console `[gauge] ch=... OHX=.../s boost=ok IAT=ok cann=...` |
| Web page | Join `CANv-gauge`, check `/`, `/api/status`, scheme/scale/mode/mount, Zero, and that all survive a reboot |
| OTA | `curl -F firmware=@.pio/build/esp32s3/firmware.bin http://192.168.4.1/update`, then confirm the boot log and the new build |
| BOOT key | Short press: `[ui] accent <name>` once. 700 ms hold: `[ui] mode g-meter`; release must not also step the scheme |
| Touch | Tap: `[ui] touch tap` + one scheme step. Swipe left/right: `[ui] touch swipe left|right` + one 280 ms slide. `t` logs the mapped point, which is how the mirror flags are checked: touch a known corner and read it |
| Cruise stalk | Cancel+Down: `[ui] cruise combo` + one scheme step. Down long alone: `[ui] cruise mode` + one screen switch |
| G-meter | At rest `g` prints `avg=0.00 valid=1` and `/api/status` has `g100:0`; tilt the board to see the blob move; acceleration must move it up, braking down. In a car, use Flip if reversed |
| G-meter math | `tools/gmeter_test.cpp` passes: 0.5 g signs, diagonal 0.707, speed gate, tilt re-learn, auto mount, Zero |
| Channel behaviour | Console scan lines; `s`/`r` forces a rescan, `i` prints counters |
| Absent values | Power the controller off: boost/IAT must become `--` on the glass and `null` in `/api/status`. With no IMU, the G readout stays `-- g` and Zero does not fake 0.00 |
| Scheme migration | Boot a board that had a 1.85 scheme stored: `[set] scheme <old> -> <new> (1.85 order to dash order)` once, then the same colour. A fresh board has no `scheme` key and must come up Midnight, not Green |
| TE sync | Boot log must say `[lcd] TE sync on` with a plausible edge count. If it says `TE not pulsing`, the flush is unsynced and the G-meter may tear |
| Splash | `[splash] picture 466x466 in ... ms`, `[ui] splash up`, then `[ui] splash down` after 3 s; capture with `GAUGE_CAPTURE_SPLASH` |

## Conventions

* Match the existing style: comments explain **why** and record measured
  evidence. Keep the one-line-per-state-change console habit
  (`[lcd] ...`, `[now] ...`, `[ui] accent ...`).
* Prefer a small, honest change over a wide one. Update `docs/status.md` and
  `TODO.md` in the same change as the behaviour.
* Commit style: short imperative subject, body explaining the reason.
* Version lives in `include/version.h`; its history comment is the changelog.

## Documentation map

| File | Read when |
|---|---|
| `README.md` | Overview, hardware, build/flash, gauge layout, repository layout, licence |
| `docs/status.md` | What is verified where, current build size |
| `TODO.md` | What is deliberately unbuilt or waiting on hardware |
| `docs/board-port-amoled-175.md` | The ESP32-S3-Touch-AMOLED-1.75 move: pin map, corrected assumptions, remaining checks |
| `AGENTS.md` | This file |

## Known hardware and environment facts

* The panel is a **466x466 CO5300 AMOLED** over QSPI (SCLK 38, D0-D3 4-7,
  CS 12, reset GPIO39). Its controller RAM is 480 wide and the visible area
  starts at column 6, so every draw is offset by `LCD_GAP_X` (6) and the
  init table bakes the same window (`0x2A 00 06 01 D7`).
* There is **no backlight**. Brightness is emission register `0x51`, 0-255,
  written through `esp_lcd_panel_co5300_set_brightness()`; 0 is the only
  "off". `LCD_BRIGHTNESS_DAY_PCT` is 85 and the night curve is
  `5 + 60 x 58s/100`.
* **RGB565 must be byte-swapped** before `esp_lcd_panel_draw_bitmap`. The
  first AMOLED build skipped it and the glass showed white as light green,
  the green arc as dark green and the near-black track as pink.
* The init sequence is Waveshare's own (`co5300_init.c`), not
  esp-iot-solution's default: `0xFE 0x00`, `0xC4 0x80`, `0x3A 0x55`,
  `0x53 0x20`, `0x63 0xFF`, `0x51`, `0x58 0x00`, `0x35 0x00` (TEON), the two
  window commands, `0x29`. The generic table's column window comes up shifted
  on this glass.
* The QMI8658 answers at I2C `0x6B` (not `0x6A` on this board). It is
  configured for ±4 g / 250 Hz accelerometer and ±256 dps / 250 Hz gyro.
  The 12-byte burst starts at `0x35`; the chip's internal LPF is off.
* The **CST9217 touch** is at `0x5A` on the control bus (SDA 15 / SCL 14 on
  this board; the 1.85 used 11/10), reset GPIO40, interrupt GPIO11.
  It is polled at ~66 Hz rather than interrupt-driven: SensorLib notes the
  INT line fires about once a second on this controller, not per touch.
  Protocol: `0xD1/0x01` command mode, `0xD1/0xF8` resolution, `0xD2/0x04`
  chip type, `0xD0/0x00` read a report then `0xD0/0x00/0xAB` to acknowledge.
  `mirror_x=1, mirror_y=1, swap_xy=0` matches Waveshare's BSP; the `t`
  console key logs the mapped point so a wrong corner is obvious.
* The TCA9554 expander at `0x20` is **not driven**. All of its pins belong to
  other chips on this board, so it is left at its power-on default (every pin
  an input). The LCD reset is a plain GPIO here.
* The whole board's power button is a **battery latch**, not a GPIO input.
  The local user key is BOOT (GPIO0): short press = scheme, 700 ms hold =
  G-meter. With the CANN relay, the PQ cruise stalk also works: Cancel+Down
  cycles the scheme and Down-long alone switches screens (GRA_Down_Long,
  bit 4 of 0x38A). The stalk generates the long bit; the gauge does not
  time that hold.
* The G-meter's default mount is **upright** (gauge faces the driver): the
  longitudinal axis is chip Z, lateral is chip X. **Flat** (screen up, top
  forward) uses chip Y and X. The default signs assume the QMI8658 is on the
  back of the PCB with +Z away from the glass; `Flip accel/brake` fixes a
  180 degree mount. The in-car sign/axis mapping is not yet verified.
* The G-meter gravity tracker has a 2 s time constant and only learns while
  the car is stationary. A fresh controller speed below 1 km/h is trusted
  by itself (engine vibration does not block it); without speed data the
  IMU gate is | |a| - 1 g | < 0.06 g and gyro < 0.2 rad/s. Mount `Auto`
  picks upright or flat from the learned gravity. The 2-axis magnitude
  is `sqrt(v² + l²)`, averaged over 100 samples (1 s at 100 Hz); the deadband
  is 0.01 g. A Zero with no IMU sample leaves the readout at `--`.
* The mode fade scales leaf-object opacity over 260 ms and a swipe slides
  both roots over 280 ms with a cubic ease-out. Do not use a full-screen
  opacity layer: 466x466 RGB565 is 434 KB and the pool is 128 KB.
  Step opacity and offsets at the display rate, not loop rate: applying them
  every loop (200 Hz) overflowed the invalid-area buffer and caused
  full-screen repaints. `LV_INV_BUF_SIZE` is 128 for the G-meter tail + fade.
* The CO5300 TE output on GPIO13 is enabled by `TEON (0x35, 0x00)` and
  pulses once per panel frame. `display.cpp` waits for the V-blank edge
  before a render's flushes and disables itself if the pin does not pulse.
  The SPI panel IO queue depth is 64. The G-meter draws its dot, tail and g
  readout into one 466x388 RGB565 canvas at y=78 and invalidates only that
  canvas; there are no per-object dot/text boxes to flush separately. The
  canvas buffer is 361,616 B in PSRAM.
* The boot splash is a 466x466 baseline JPEG (the 360 px artwork upscaled by
  466/360 with lanczos) embedded by `board_build.embed_files` and decoded by
  the S3 mask ROM's TJpgDec into 434,312 B of PSRAM (147 ms measured). Its
  green `lv_bar` covers the artwork's rule at x=104, y=262, 258x4 and fills
  over 3 s. The picture is released when the splash is deleted, never kept
  for the dashboard.
* IAT is `0.75 * byte1 - 48` from `Engine_3` (`0x380`), and `0xFF` means absent.
  Without a CANN sender the value stays `--`.
* Boost is `mbar / 68.94757` PSI. On the bench the controller often sends the
  absent sentinel; that is not a zero. A negative mbar value is vacuum and is
  drawn with a leading `-`.
* The setup AP is **open** unless built with `-DGAUGE_AP_PASSWORD`. It is
  started on the receiver's channel; AP+STA share one radio, so the AP's
  world-safe country policy caps the radio at **1-11** and channels 12/13
  fail while the AP is up.
* Channel walking is disabled while the AP is up (a moving softAP cannot be
  joined); the 15 s full scan finds the controller's AP by name. A connected
  page client freezes the channel even against a forced scan.
* Settings live in NVS namespace `gauge`, keys `scheme`, `skver`, `smin`,
  `smax`, `mode`, `gmount`, `gflip`, all written with `putInt` and read with
  `getInt`. `getUChar` rejects the four-byte record and silently returns the
  default. `skver` is 2; a stored `scheme` from the 1.85 build is migrated
  once through `{2,1,6,3,5,4}` (green/orange/red/blue/white/purple to the
  seven-scheme order), and `Preferences::isKey()` is what keeps a fresh board
  on Midnight instead of migrating a default.
* OTA writes the inactive 6.25 MB slot and reboots; a 1.36 MB image took about
  7 s over the AP on the bench.
* This repository is AI-generated and human-tested on one board. Qualify
  verification claims accordingly.
