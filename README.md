# ESP32-S3-Gauge

A standalone **digital boost gauge** for the
**[OpenHaldex](https://github.com/Forbes-Automotive/OpenHaldex-C6)** Haldex
controller, running on a **Waveshare ESP32-S3-LCD-1.85** (360x360 round IPS
panel, ST77916 over QSPI).

The gauge is **receive-only** with respect to the car: it listens for the
controller's `OHX_STATUS` ESP-NOW broadcast for boost (and rpm/speed/throttle/
mode), and decodes intake air temperature from `Engine_3` (`0x380`) in the
CANN relay. It never sends ESP-NOW or CAN traffic, never touches the car's CAN
bus, and the controller firmware is not modified. The only RF it originates is
its own **CANv-gauge** setup AP, which serves a settings page and OTA uploads.

It also has a local **G-meter** screen driven by the board's QMI8658 IMU:
acceleration/braking on the vertical axis, a fading 1 s tail, and a white
1 s average readout. The IMU is on the gauge's own I2C bus, so the G-meter
does not touch the car's network.

> This repository is AI-generated and human-tested on one board. It is not
> security-audited. Read the code before you flash it.

![The gauge in the green scheme](assets/gauge-preview.png)

## What it shows

- Edge-to-edge 270-degree arc with six evenly spaced scale labels, one
  consistent accent colour across the whole sweep (no warning colours). The
  default scale is `0 / 5 / 10 / 15 / 20 / 25` PSI; both ends are editable
  from the web page (0.1 PSI steps, minimum may be negative for vacuum).
- Large 72 px white boost value with the decimal point on the text baseline.
- `BOOST PSI` caption, and the intake air temperature under a thin rule.
- Automatic day/night dimming from the car's Terminal 58s dash-dimmer
  reading (`Cluster_2`, `0x420`) in the CANN relay, using the same curve as
  `OpenHaldex-S3-Dash`. Terminal 58d is not used: it reads non-zero with the
  lights off. Without a 58s reading the backlight stays at the day level.
- Six accent schemes: **green, orange, red, blue, white, purple**. A **short
  press** of the board's **BOOT** key (GPIO0) steps through them; a **700 ms
  hold** switches to the G-meter and back. The key is polled with a 30 ms
  debounce, so a deliberate tap lands even while the arc is repainting. With
  the CANN relay on, pressing **Cancel + Down** together on the PQ cruise stalk
  does the same; each source logs `[ui] accent <name>` (the stalk adds
  `[ui] cruise combo`). The scheme is stored in NVS and survives a power cycle.
- A **G-meter screen**: a 50 px accent-coloured blob moves vertically for
  acceleration/braking and horizontally for lateral acceleration; a fading
  tail covers the last second. The white `0.00 g` readout at the bottom is
  the 1 s average of the two-axis magnitude. See
  [G-meter mode](#g-meter-mode).
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
| IMU | QMI8658 6-axis at I2C `0x6B` on the same bus, ±4 g / 250 Hz |
| Backlight | GPIO5, LEDC PWM |
| BOOT key | GPIO0, active low, internal pull-up |
| Power button | battery latch, not a GPIO input (see below) |
| Bench listener | Plain ESP32-WROOM devkit (`env:esp32dev`) |

Touch is wired on a separate bus (GPIO1/3) and is not used.

## G-meter mode

The QMI8658 is on the control I2C bus, so the G-meter is entirely local. The
screen is a 50 px blob in the active accent colour, centred at 0 g. Its
vertical position is acceleration/braking; its horizontal position is lateral
acceleration. A 20-dot tail fades over the last second; the white number at the
bottom is the 1 s mean of `sqrt(longitudinal² + lateral²)`.

Switch screens from the web page, the console `m` key, or a **700 ms BOOT
hold**. The mode is stored in NVS. The board's power button is a battery
latch, not a GPIO input, so BOOT is the only physical key; a short press still
steps the accent scheme. The change is a 260 ms cross-fade. Each object fades
its own opacity because a full-screen LVGL opacity layer would need 259 KB
and the pool is 128 KB.

The math follows the usual accelerometer G-meter practice:

- **Gravity is learned, not assumed zero.** A 2 s tracker follows the gravity
  vector while the car is stationary (`| |a| - 1 g | < 0.06 g`, gyro under
  0.2 rad/s, and controller speed under 1 km/h when a fresh speed reading is
  available). A tilt changes which axis gravity is on but not `|a|`, so a new
  mount attitude is re-learned instead of leaving a permanent offset.
- **Dynamic acceleration is low-passed at 8 Hz.** This rejects road vibration
  without eating a launch.
- **The magnitude is `sqrt(v² + l²)`, not `(|v| + |l|) / 2`.** The latter
  understates a diagonal 1 g vector by about 29%. The instantaneous magnitude
  is averaged over 1 s, matching "how hard was that pull/corner" rather than
  "what is the vibration now".
- **A 0.01 g deadband** keeps the blob still at idle.
- **Zero is explicit.** The web page's **Zero G-meter** button stores the
  current attitude as 0 g. Press it with the car stationary. With no IMU
  sample at all, Zero does nothing and the display stays `--`.

Mount and direction are stored settings:

| Mount | Longitudinal axis | Lateral axis | Typical install |
|---|---|---|---|
| **Upright** (default) | chip Z (screen normal) | chip X | Gauge faces the driver |
| **Flat** | chip Y (screen vertical) | chip X | Screen up, top of the glass forward |

**Flip accel/brake** inverts the longitudinal sign for a 180° mount. The
default signs assume the QMI8658 is on the back of the PCB, with +Z pointing
away from the glass. Check the first drive: the blob must move up under
acceleration and down under braking; if it is reversed, tick Flip. Accuracy in
a car has not been verified yet (see [`docs/status.md`](docs/status.md)).

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
- `b` — step the accent scheme (same action as a short BOOT press)
- `m` — boost gauge <-> G-meter (same action as a 700 ms BOOT hold)
- `z` — zero the G-meter (car stationary)
- `g` — print the current G reading once
- `a` — toggle a 2 Hz raw accelerometer/gyro dump, for checking the mount axes

A healthy link looks like:

```text
ESP32-S3-Gauge 0.5.0
[gauge] boost from OHX_STATUS, IAT from Engine_3 (0x380)
[set] scheme 0, scale 0.0..25.0 PSI
[set] mode gauge, mount upright, flip 0
[lcd] panel id: 00 02 7F 7F
[lcd] using vendor init sequence
[lcd] Waveshare ESP32-S3-LCD-1.85 ready (360x360)
[tca9554] ready at 0x20
[imu] QMI8658 at 0x6B, accel +/-4 g 250 Hz, gyro 256 dps
[ui] mode gauge, mount upright, flip 0
[ui] accent green (short press cycles, long press = G-meter)
[now] scan: 7 APs; anchor "OpenHaldex-C6" ch 1 (-74 dBm)
[now] hop limit is now 1-11
[web] AP "CANv-gauge" (open) channel 1, http://192.168.4.1/
[now] setup AP up: holding channel 1 (scan-only discovery)
[now] listening on channel 1
[can] IAT 4.5 C
[gauge] ch=1 OHX=9.8/s boost=ok IAT=ok cann=184 rssi=-74
```

The receiver starts on the family fallback channel 1, scans for the
`OpenHaldex-C6` AP first and the dash AP second (`CANv-dash`, or the older
`ohx-dash`), and hops through 1..13 only
while it has been quiet. With the setup AP up it holds one channel instead
(see below) and relies on the 15 s scan, because AP+STA share a single radio
and a softAP that moves every 400 ms is not joinable.

## Web page, settings and OTA

The gauge always runs its own AP:

| | |
|---|---|
| SSID | `CANv-gauge` |
| Address | <http://192.168.4.1/> |
| Network | open (no password) by default |

Join it from a phone or laptop. The page shows live boost, IAT, the G
average and its two signed axes, channel, RSSI and packet rate. These controls
persist in NVS:

- **Colour scheme** — the same six as the BOOT key.
- **Scale** — minimum and maximum in 0.1 PSI steps, limited to -15.0..50.0
  PSI with a span of 5..60 PSI. The arc re-labels and re-ranges immediately.
  The numeric readout still shows a value outside the scale; only the arc
  clamps. A negative minimum is allowed because the controller reports
  vacuum as a negative mbar value.
- **Display** — choose **Boost gauge** or **G-meter**, the mount
  (**Upright** or **Flat**), and **Flip accel/brake**. **Zero G-meter** stores
  the current attitude as 0 g; use it stationary.

Firmware upload uses the same page or curl:

```bash
./.venv/bin/pio run -e esp32s3
curl -F "firmware=@.pio/build/esp32s3/firmware.bin" http://192.168.4.1/update
```

The image is written to the inactive OTA slot, so a failed upload cannot
overwrite the running one, and the gauge reboots into it. A 1.36 MB image took
about 7 s on the bench.

### The AP shares the radio

ESP32 AP+STA uses one radio, so the AP has no channel of its own:

- it starts on the channel the receiver is on;
- while the AP is up the channel walk is disabled, so the AP stays joinable;
  the periodic full scan still finds the controller's AP;
- a connected page client freezes all channel changes, so the page is not
  dropped mid-edit.

The trade-off: an anchor whose SSID is hidden cannot be found while the AP is
up, because discovery is by name. See [`TODO.md`](TODO.md).

### Security

The AP is **open** and the page can flash firmware, so anyone within WiFi
range can change the settings or upload an image. To require WPA2, add a
build flag:

```ini
build_flags = -DGAUGE_AP_PASSWORD=\"your-password\"
```

The boot log states which mode is active:
`[web] AP "CANv-gauge" (open) channel 1, http://192.168.4.1/`.

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
pool are compiled out of normal firmware. To photograph another reading (for
example a vacuum value with a negative minimum), override the fixed boost:

```bash
PLATFORMIO_BUILD_FLAGS="-DGAUGE_CAPTURE=1 -DGAUGE_CAPTURE_BOOST=-2.5f" \
  ./.venv/bin/pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0
```

The capture sweep does not write settings to NVS. To capture the G-meter
layout instead, add `-DGAUGE_CAPTURE_GMETER=1`. It feeds a synthetic 0.5 Hz
sweep so the blob, tail and bottom readout all appear:

```bash
PLATFORMIO_BUILD_FLAGS="-DGAUGE_CAPTURE=1 -DGAUGE_CAPTURE_GMETER=1" \
  ./.venv/bin/pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0
./.venv/bin/python tools/capture_frame.py /dev/ttyACM0 /tmp/gmeter
```

That writes six frames named by accent, exactly like the gauge sweep.

Host-only checks (no hardware) live in `tools/`:

```bash
g++ -std=c++17 -Isrc -Iinclude -o /tmp/gmeter_test tools/gmeter_test.cpp src/app/gmeter.cpp -lm
/tmp/gmeter_test

g++ -std=c++17 -Itools/host -Isrc -Iinclude -o /tmp/boot_button_test tools/boot_button_test.cpp src/hal/boot_button.cpp
/tmp/boot_button_test
```

`gmeter_test` checks gravity removal, the 0.5 g signs, the diagonal
`sqrt(v² + l²)`, the speed gate, tilt re-learning and Zero. `boot_button_test`
checks short, long and bounce behaviour. Neither touches the panel, the IMU or
the car.

## Repository layout

```text
include/     board pins, LVGL config, theme, version, shared wire headers
src/app/     config, NVS settings (scheme/scale/mode/mount), G-meter math
src/hal/     QSPI panel, TCA9554 reset, BOOT key, QMI8658 IMU
src/net/     ESP-NOW receive, channel discovery, IAT decode, setup AP + OTA
src/ui/      gauge layout, G-meter screen, dashboard/mode fade, accents
tools/       serial monitor, framebuffer capture, host math/button checks
docs/        status and verification notes
AGENTS.md    guidance for AI coding agents
TODO.md      open hardware and firmware items
```

## Verification and status

- [`docs/status.md`](docs/status.md) — what was measured on the bench, what is
  still unverified, and the current build size.
- [`TODO.md`](TODO.md) — open items, including the first in-car G-meter test
  and the physical BOOT long-press check.
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
