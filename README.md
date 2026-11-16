# ESP32-S3-Gauge

Digital boost gauge for a **Waveshare ESP32-S3-LCD-1.85** (360×360 round
ST77916 panel). Boost comes from the OpenHaldex controller's `OHX_STATUS`
broadcast; intake air temperature is decoded from `Engine_3` (`0x380`) in the
CANN relay.

The plain ESP32-WROOM build (`env:esp32dev`) stays as a headless CANN
listener for bench work.

## Hardware

- Display target: Waveshare ESP32-S3-LCD-1.85, ESP32-S3R8, 16 MB flash,
  8 MB OPI PSRAM (`env:esp32s3`)
- Bench proxy: plain ESP32-WROOM devkit (`env:esp32dev`)
- Sender: the ESP32-bodycan device / OpenHaldex-C6, broadcasting CANN and
  `OHX_STATUS` on its WiFi channel

The display is driven with Waveshare's vendor ST77916 QSPI driver. LVGL 9.3
runs with its pool in PSRAM, and the gauge uses the Montserrat faces from
`OpenHaldex-S3-Dash` (56 px for the boost value).

## Build / flash

```bash
python3 -m venv .venv
./.venv/bin/pip install platformio pyserial esptool

# Display board (native USB CDC, /dev/ttyACM0)
./.venv/bin/pio run -e esp32s3
./.venv/bin/pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0

# WROOM bench listener (CH340, /dev/ttyUSB0)
./.venv/bin/pio run -e esp32dev
./.venv/bin/pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0
```

## Monitor

```bash
./.venv/bin/pio device monitor -p /dev/ttyACM0 -b 115200
```

Non-interactive (also pulses RTS to capture the boot log):

```bash
./.venv/bin/python tools/monitor.py /dev/ttyACM0
```

Serial commands while it is running:

- `i` — print raw/packet/frame/reject counters
- `s` or `r` — force a channel scan

## Gauge layout

- 270° arc, 0–25 PSI, green below 16 PSI, amber above, red above 22
- Large white boost value with a centred decimal dot
- `BOOST PSI` caption, `IAT` readout under a thin rule
- Link lamp: green for fresh `OHX_STATUS`, amber for CANN only, grey for none

Absent values are drawn as `--`, never as zero.

## What good output looks like

```text
ESP32-S3-Gauge 0.2.0-gauge
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
`OpenHaldex-C6` or `ohx-dash` AP, and hops through 1..13 only if it has been
quiet. Once packets arrive it locks to their channel.
