# Status

Last updated: 16 Nov 2026, firmware **0.3.7**.

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
* The edge-to-edge 360 px arc, the scale labels at radius 145, the 72 px
  value row, `BOOST PSI` caption and IAT row fit without overlap. The decimal
  point is a baseline `.` glyph; the empty arc background is one consistent
  colour. All six schemes were captured and inspected after the 0.3.6 layout
  change.
* The link state renders as a **20x4 px rounded line** at the bottom edge:
  **green** = streaming OHX_STATUS, **orange** = stale/lost OHX_STATUS,
  **amber** = IAT only (CANN), **red** = never received anything.
* LVGL buffers are released from the panel IO's `on_color_trans_done`
  callback, after the QSPI DMA transfer finishes.
* Async WiFi scan runs in background; main loop (LVGL, BOOT key) is not
  blocked during channel hopping. With no anchor and the USB console
  draining, `ohxnow::poll()` measured at most about **0.85 ms** while
  hopping every 400 ms and rescanning every 15 s.
* The physical **BOOT key** was exercised on the bench: a normal press, a
  quick tap and a 1 s hold each produce exactly **one** scheme change. With
  the console draining, ISR-to-handler measured **0.7-4.6 ms**; the scheme
  repaint takes about **40 ms** including the last DMA flush. Before 0.3.3,
  a press held past the 30 ms debounce made the poll fallback report the
  same press a second time 203 ms later.
* **USB console writes no longer block the loop** (0.3.4). With no host
  draining `/dev/ttyACM0`, the disconnected-state `[scan] channel` lines
  could hold `loop()` for seconds; one press measured **6.9 s** from ISR to
  handler. `Serial.setTxTimeoutMs(0)` makes normal-build writes non-blocking;
  the same no-monitor press then measured **~40 ms** ISR-to-flush and felt
  immediate. The capture build keeps the blocking timeout so framebuffer
  dumps are not shortened.
* **Live boost sweep (0.3.5).** A synthetic 0-25 PSI sweep reproduced the
  in-car corruption on the bench: the thick arc left a trail of old
  indicator pixels and the value row showed stale white blocks. A forced
  full-screen redraw was clean; invalidating only the whole arc was also
  clean. The fix invalidates the whole arc on each displayed value change,
  because LVGL's incremental arc clean-up is not sufficient for this
  270-degree rounded indicator. The synthetic bench sweep and a car re-test
  are clean, and the gauge still feels responsive.
* **Terminal 58s auto-dimming (0.3.6).** `Cluster_2` (`0x420`) byte 6 is
  decoded as Terminal 58s, using the same field and `5 + 60 × 58s/100` night
  curve as `OpenHaldex-S3-Dash` (0 = day, 7..99 = night dimmer; 0x7F and the
  error bit are ignored). The gauge uses **85%** as the day level. Terminal
  58d is deliberately not used: the S3-dash measured it non-zero with the
  lights off. Verified in the car: the backlight follows the dash dimmer
  wheel and returns to day brightness with the lights off.
* **Cruise-stalk accent cycle (0.3.7).** PQ `GRA_New` (`0x38A`) byte 1 is
  decoded: `GRA_Cancel` is bit 1 and `GRA_Down_Short`/`GRA_Down_Long` are
  bits 2/4. Cancel + either down bit is edge-triggered with a 250 ms cooldown
  and logs `[ui] cruise combo` before the accent change. This is new and has
  not yet been tried in the car.
* Partial draw tiles increased to 60 lines (6 tiles/frame vs 9) to reduce rolling-shutter tearing.
* The headless `esp32dev` configuration builds; its receiver path is
  unchanged from the earlier WROOM bench run.

## Not verified yet

* **In a car.** Most readings above are from the bench rig and a CANN
  sender. The live boost display has been exercised in the car for the
  distortion fix, but numeric accuracy in the car is still unverified.
* **Boost above ambient.** On the bench the controller sent `0` or the
  absent sentinel; the gauge showed `0.0` or `--`. The car showed a rising
  boost, but it has not been compared against a known manifold pressure.
* **Cruise-stalk combo in the car.** The PQ `0x38A` decode is implemented,
  but it has not been pressed in the car yet. Confirm one accent step per
  Cancel + Down press.
* **Long-run stability.** No soak test past a few minutes; PSRAM pool and
  internal RAM have not been watched over hours.

## Build sizes, 0.3.7

Measured with `./.venv/bin/pio run -e esp32s3`:

| Resource | Used | Capacity | Note |
|---|---|---|---|
| Flash | 1,208,934 B (18.4%) | 6,553,600 B | one of two 6.25 MB OTA app slots |
| Static RAM | 179,112 B (54.7%) | 327,680 B | 60-line draw buffers (2×360×60) |
| LVGL pool | 128 KB | — | allocated from 8 MB PSRAM |
| Draw buffers | 86,400 B | — | 2 x 360x60 RGB565, internal RAM |

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
