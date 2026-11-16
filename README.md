# ESP32-S3-Gauge

A standalone **digital boost gauge** for the
**[OpenHaldex](https://github.com/Forbes-Automotive/OpenHaldex-C6)** Haldex
controller, running on a **Waveshare ESP32-S3-LCD-1.85** (360x360 round IPS
panel, ST77916 over QSPI).

The gauge is **receive-only**. It listens for the controller's `OHX_STATUS`
ESP-NOW broadcast for boost (and rpm/speed/throttle/mode), and decodes intake
air temperature from `Engine_3` (`0x380`) in the CANN relay. It never
transmits, never touches the car's CAN bus, and the controller firmware is
not modified.

> This repository is AI-generated and human-tested on one board. It is not
> security-audited. Read the code before you flash it.

![The gauge in the green scheme](assets/gauge-preview.png)

## What it shows

- Edge-to-edge 270-degree arc, `0 / 5 / 10 / 15 / 20 / 25` PSI, one
  consistent accent colour across the whole sweep (no warning colours).
- Large 72 px white boost value with the decimal point on the text baseline.
- `BOOST PSI` caption, and the intake air temperature under a thin rule.
- Automatic day/night dimming from the car's Terminal 58s dash-dimmer
  reading (`Cluster_2`, `0x420`) in the CANN relay, using the same curve as
  `OpenHaldex-S3-Dash`. Terminal 58d is not used: it reads non-zero with the
  lights off. Without a 58s reading the backlight stays at the day level.
- Six accent schemes: **green, orange, red, blue, white, purple**. Press the
  board's **BOOT** key (GPIO0) to step through them. The key is
  interrupt-driven, so quick taps land even while the arc is repainting.
  With the CANN relay on, pressing **Cancel + Down** together on the PQ
  cruise stalk does the same; each source logs `[ui] accent <name>` (the
  stalk adds `[ui] cruise combo`).
- A **20x4 px link line** at the bottom edge: green when `OHX_STATUS` is
  fresh, orange when it is stale, amber when only CANN is arriving, red when
  nothing has been heard.
- Unknown values draw `--`, never a fake zero. A genuine `0` draws `0`.

![All six accent schemes](assets/gauge-schemes.png)

## Hardware

| Piece | Notes |
|---|---|
| Display board | Waveshare ESP32-S3-LCD-1.85, ESP32-S3R8, 16 MB flash, 8 MB OPI PSRAM |
| Panel | 360x360 ST77916, QSPI, vendor init sequence when ID reads `00 02 7F 7F` |
| LCD reset | TCA9554 I/O expander at `0x20`, bit 1 (I2C SDA 11 / SCL 10) |
| Backlight | GPIO5, LEDC PWM |
| BOOT key | GPIO0, active low, internal pull-up |
| Bench listener | Plain ESP32-WROOM devkit (`env:esp32dev`) |

Touch is wired on a separate bus (GPIO1/3) and is not used.

## Build and flash

```bash
python3 -m venv .venv
./.venv/bin/pip install platformio pyserial

# Display board, native USB CDC on /dev/ttyACM0
./.venv/bin/pio run -e esp32s3
./.venv/bin/pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0

# Headless WROOM CANN listener, CH340 on /dev/ttyUSB0
./.venv/bin/pio run -e esp32dev
./.venv/bin/pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0
```

Build sizes for the current version are recorded in
[`docs/status.md`](docs/status.md).

## Monitor

```bash
./.venv/bin/python tools/monitor.py /dev/ttyACM0
```

Console keys while it is running:

- `i` — raw/packet/frame/reject counters and the current channel
- `s` or `r` — force a channel scan
- `b` — step the accent scheme (same action as the BOOT key)

A healthy link looks like:

```text
ESP32-S3-Gauge 0.3.1
[gauge] boost from OHX_STATUS, IAT from Engine_3 (0x380)
[lcd] panel id: 00 02 7F 7F
[lcd] using vendor init sequence
[lcd] Waveshare ESP32-S3-LCD-1.85 ready (360x360)
[now] scan: 7 APs; anchor "OpenHaldex-C6" ch 1 (-74 dBm)
[now] listening on channel 1
[can] IAT 4.5 C
[gauge] ch=1 OHX=9.8/s boost=ok IAT=ok cann=184 rssi=-74
```

The receiver starts on the family fallback channel 1, scans for the
`OpenHaldex-C6` AP first and `ohx-dash` second, and hops through 1..13 only
while it has been quiet.

## Capturing the glass

Layout and colour changes can be checked without a camera. The capture build
renders one frame per scheme and streams each 360x360 RGB565 framebuffer over
USB:

```bash
PLATFORMIO_BUILD_FLAGS="-DGAUGE_CAPTURE=1" \
  ./.venv/bin/pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0
./.venv/bin/python tools/capture_frame.py /dev/ttyACM0 /tmp/gauge
```

This writes `gauge_<n>_<scheme>.ppm`. The capture code and its 512 KB LVGL
pool are compiled out of normal firmware.

## Repository layout

```text
include/     board pins, LVGL config, theme, version, shared wire headers
src/hal/     QSPI panel, TCA9554 reset, BOOT key
src/net/     ESP-NOW receive, channel discovery, IAT decode
src/ui/      gauge layout, accent schemes, value font
tools/       serial monitor, framebuffer capture
docs/        status and verification notes
AGENTS.md    guidance for AI coding agents
TODO.md      open hardware and firmware items
```

## Verification and status

- [`docs/status.md`](docs/status.md) — what was measured on the bench, what is
  still unverified, and the current build size.
- [`TODO.md`](TODO.md) — open items, including the first in-car test and the
  physical BOOT-key check.
- [`AGENTS.md`](AGENTS.md) — conventions, hard rules and where to change what.

## Credits

The controller, its CAN decoding and the ESP-NOW protocol are the work of
**[Forbes Automotive](https://github.com/Forbes-Automotive/OpenHaldex-C6)**.
`include/ohx_proto.h` and `include/cannow_proto.h` are verbatim copies of the
shared wire-format headers; refresh them from upstream, never hand-edit them.

The 72 px Montserrat Bold subset is generated from the face used by
[`OpenHaldex-S3-Dash`](https://github.com/adruss85/OpenHaldex-S3-Dash).

## Licence

GPL-3.0-or-later. See [`LICENSE`](LICENSE).
