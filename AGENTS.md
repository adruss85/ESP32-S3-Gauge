# AGENTS.md

Guidance for AI coding agents working on **ESP32-S3-Gauge**. Read this first;
detail lives in `README.md` and the files it points to.

## What this is

A standalone **digital boost gauge** on a **Waveshare ESP32-S3-LCD-1.85**
(ESP32-S3R8, 16 MB flash, 8 MB OPI PSRAM, 360x360 round ST77916 over QSPI).
It is a **receive-only display client** for the OpenHaldex family:

* boost, rpm, speed, throttle, mode from the controller's `OHX_STATUS`
  ESP-NOW broadcast (`include/ohx_proto.h`);
* intake air temperature from `Engine_3` (`0x380`) in the CANN relay
  (`include/cannow_proto.h`);
* the AP name `OpenHaldex-C6` (preferred) or `ohx-dash` supplies the WiFi
  channel. Nothing is transmitted.

Stack: PlatformIO, Arduino-ESP32, LVGL 9.3, Espressif `esp_lcd`, and
Waveshare's vendor ST77916 driver. C++ in `src/`, headers in `include/`.

`env:esp32s3` is the real display target. `env:esp32dev` is a headless CANN
listener for bench work and builds without the display code.

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

# headless WROOM listener (CH340)
./.venv/bin/pio run -e esp32dev
./.venv/bin/pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0
```

* Display serial console: 115200 baud on `/dev/ttyACM0`. Opening it resets
  the board; `tools/monitor.py` opens once and captures the boot log.
* Upload speed is 921600 for the S3; the WROOM's CH340 is pinned to 460800.
* **There is no test suite.** A successful `pio run` is the only check you
  can do without hardware. Anything touching the panel, ESP-NOW, the BOOT
  key or IAT decoding needs the board or a car. Say clearly what was and was
  not verified on hardware.
* After a build, report the RAM and flash lines (see Budgets).

### Capturing the glass without a camera

`-DGAUGE_CAPTURE=1` makes the firmware render a fixed reading and stream each
scheme's 360x360 RGB565 framebuffer over USB. Use it to check layout changes:

```bash
PLATFORMIO_BUILD_FLAGS="-DGAUGE_CAPTURE=1" \
  ./.venv/bin/pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0
./.venv/bin/python tools/capture_frame.py /dev/ttyACM0 /tmp/gauge
```

This produces one PPM per scheme (green, orange, red, blue, white, purple).
The capture code is compiled out of normal builds.

## Hard rules

1. **Never edit `include/ohx_proto.h` or `include/cannow_proto.h`.** They are
   verbatim shared copies of the controller's headers. Refresh from upstream
   only; bump `CANN_PROTO` in every copy together.
2. **The gauge is receive-only.** Do not add ESP-NOW sends, CAN transmits or
   any control path. It observes; the controller acts.
3. **Unknown is `NaN`, drawn as `--`. Never substitute a fake zero.** A
   packet "no data" sentinel (e.g. `OHX_NONE16`, IAT `0xFF`) travels to the
   screen as unknown. A genuine `0` draws `0`.
4. **The channel comes from an AP, not a compiled-in constant.** Scan for
   `OpenHaldex-C6` first, then `ohx-dash`; fall back to channel 1 and hop only
   while quiet. Do not hard-code a per-rig channel.
5. **Layout geometry and the palette live in `include/theme.h`.** The six
   runtime schemes are in `src/ui/theme.cpp`; `gauge_ui.cpp` only applies
   them. Nothing scales: all values are absolute pixels for 360x360.
6. **LVGL is configured for exactly what the gauge draws** (`include/lv_conf.h`).
   Every unused widget, layout and colour-format path is pinned to 0 with the
   reason beside it. Do not enable a widget without removing another, and do
   not add a full-screen redraw path.
7. **Fonts are deliberate.** The 56 px boost face is a Montserrat Bold subset
   (digits and `.`) copied from `OpenHaldex-S3-Dash`; the labels and IAT use
   stock Montserrat faces. The value decimal point is a `.` glyph from that
   subset, not a drawn circle.
8. **No credentials in git.** The controller's AP password is not stored
   here; the gauge only listens.
9. **Honesty about verification.** Bench != car. Boost has only been seen as
   `0` or the absent sentinel; IAT has only been seen on the bench with a
   CANN sender present. Do not describe those as verified in a car.

## Budgets: do not regress these

| Resource | Limit / baseline | Why it matters |
|---|---|---|
| App flash | OTA slot is **6,553,600 B** (0x640000). 0.3.0 used **1,198,418 B (18.3%)** | Two OTA slots plus SPIFFS share 16 MB; re-measure after your change |
| Internal RAM | **135,240 B (41.3%)** static at 0.3.0 | WiFi/ESP-NOW stacks and DMA buffers need the rest |
| LVGL pool | **128 KB** (`LV_MEM_SIZE`), allocated from PSRAM | Keep the pool out of internal RAM |
| Draw buffers | 2 x 360x40 RGB565 = **57,600 B** in internal RAM | Partial render mode; both must be `LV_DRAW_BUF_ALIGN` aligned |
| QSPI clock | 80 MHz | The vendor panel sequence assumes it |

Rules of thumb: no unconditional `lv_obj_invalidate`; style changes already
invalidate only the affected objects; keep the arc as the only large
anti-aliased shape; do not add scrolling containers.

## Where things are (task to files)

| Task | Touch |
|---|---|
| Gauge layout, value row, ticks | `src/ui/gauge_ui.cpp`, geometry in `include/theme.h` |
| Accent schemes / BOOT cycle | `src/ui/theme.cpp`, `src/hal/boot_button.cpp` |
| Panel, QSPI, LVGL display | `src/hal/display.cpp`; vendor driver `esp_lcd_st77916.c`, sequence `st77916_init.c` |
| LCD reset expander | `src/hal/tca9554.cpp` (TCA9554 bit 1) |
| ESP-NOW receive, channel scan | `src/net/ohx_now.cpp` |
| IAT decode (`0x380` byte 1) | `handleCann()` in `src/net/ohx_now.cpp` |
| Console keys | `handleSerial()` in `src/net/ohx_now.cpp` |
| Boot banner / version | `include/version.h` |
| Board pin map | `include/board_config.h` |
| Frame capture | `GAUGE_CAPTURE` in `src/main.cpp`, `tools/capture_frame.py` |

## How to verify a change

| Change type | Check |
|---|---|
| Any | `pio run -e esp32s3` and `pio run -e esp32dev` succeed; report size lines |
| Layout or colour | Capture the six schemes (above) and inspect the PPMs |
| Data path | Console `[gauge] ch=... OHX=.../s boost=ok IAT=ok cann=...` |
| BOOT key | Press it on the board: `[ui] accent <name>` once per press |
| Channel behaviour | Console scan lines; `s`/`r` forces a rescan, `i` prints counters |
| Absent values | Power the controller off: boost/IAT must become `--`, not `0` |

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
| `AGENTS.md` | This file |

## Known hardware and environment facts

* The panel reports ID `00 02 7F 7F`; that selects Waveshare's vendor init
  sequence. The other ID path in their demo is not present on this board.
* The LCD reset is on TCA9554 bit 1; the expander is at `0x20` on I2C
  `SDA=11/SCL=10`.
* The BOOT key is **GPIO0, active low**, with the internal pull-up. It is a
  strapping pin at reset only.
* IAT is `0.75 * byte1 - 48` from `Engine_3` (`0x380`), and `0xFF` means absent.
  Without a CANN sender the value stays `--`.
* Boost is `mbar / 68.94757` PSI. On the bench the controller often sends the
  absent sentinel; that is not a zero.
* Touch (separate I2C on GPIO1/3) is not used by this firmware.
* This repository is AI-generated and human-tested on one board. Qualify
  verification claims accordingly.
