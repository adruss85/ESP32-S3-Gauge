# Status

Last updated: 16 Nov 2026, firmware **0.3.0**.

## Verified on the bench

* The **Waveshare ESP32-S3-LCD-1.85** panel initialises over QSPI, reads ID
  `00 02 7F 7F`, selects the vendor init sequence, and reaches
  `[lcd] Waveshare ESP32-S3-LCD-1.85 ready (360x360)`.
* The gauge renders through LVGL 9.3 in partial mode with byte-swapped
  RGB565 flushes. The six accent schemes were captured from the real
  framebuffer (`tools/capture_frame.py`) and inspected: green, orange, red,
  blue, white, purple.
* `OHX_STATUS` is received from the `OpenHaldex-C6` AP on channel 1 at about
  **9.8 packets/s** with `rssi` around -75 dBm.
* IAT decodes from CANN `Engine_3` (`0x380`) and displayed **4.5 C** on the
  bench, matching `0.75 * byte1 - 48`.
* The arc, ticks, value row, `BOOST PSI` caption and IAT row fit without
  overlap. The decimal point is a baseline `.` glyph; the empty arc
  background is one consistent colour.
* The headless `esp32dev` configuration builds; its receiver path is
  unchanged from the earlier WROOM bench run.

## Not verified yet

* **In a car.** Every reading above is from the bench rig and a CANN sender.
* **Boost above ambient.** The controller sent `0` or the absent sentinel;
  the gauge showed `0.0` or `--` accordingly. A positive boost sweep has not
  been exercised.
* **The BOOT key on the physical board.** The GPIO0 input, debounce and
  six-scheme cycle build and the schemes render, but the key was not pressed
  on hardware in this pass. `TODO.md` tracks it.
* **Long-run stability.** No soak test past a few minutes; PSRAM pool and
  internal RAM have not been watched over hours.

## Build sizes, 0.3.0

Measured with `./.venv/bin/pio run -e esp32s3`:

| Resource | Used | Capacity | Note |
|---|---|---|---|
| Flash | 1,198,418 B (18.3%) | 6,553,600 B | one of two 6.25 MB OTA app slots |
| Static RAM | 135,240 B (41.3%) | 327,680 B | before the WiFi task stacks grow |
| LVGL pool | 128 KB | — | allocated from 8 MB PSRAM |
| Draw buffers | 57,600 B | — | 2 x 360x40 RGB565, internal RAM |

The capture build (`-DGAUGE_CAPTURE=1`) temporarily raises the LVGL pool to
512 KB because a snapshot is a full-screen RGB565 image. That build is only
for UI verification; normal firmware keeps 128 KB.

## How this was checked

```bash
./.venv/bin/pio run -e esp32s3
./.venv/bin/pio run -e esp32dev
PLATFORMIO_BUILD_FLAGS="-DGAUGE_CAPTURE=1" \
  ./.venv/bin/pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0
./.venv/bin/python tools/capture_frame.py /dev/ttyACM0 /tmp/gauge
```

Console evidence for the live link:

```text
[now] scan: 7 APs; anchor "OpenHaldex-C6" ch 1 (-74 dBm)
[can] IAT 4.5 C
[gauge] ch=1 OHX=9.8/s boost=ok IAT=ok cann=184 rssi=-74
```
