# Status

Last updated: 20 Nov 2026, firmware **0.6.2**.

> **Hardware state.** The gauge runs on a **Waveshare ESP32-S3-Touch-AMOLED-1.75**
> (466x466 CO5300 AMOLED, CST9217 touch). The 1.85 unit this firmware was
> developed on has a failed panel; 0.6.0 moved the build to the AMOLED board
> and the old pin map, TCA9554 LCD reset and 72 px face are in git history.
> The move, and the three assumptions the bench corrected, is recorded in
> [`docs/board-port-amoled-175.md`](board-port-amoled-175.md).

## Verified on the bench

* **The AMOLED board boots and drives its panel (0.6.0).** The boot log reads
  `[lcd] CO5300 init (466x466, gap 6, 80 MHz QSPI)` and
  `[lcd] Waveshare ESP32-S3-Touch-AMOLED-1.75 ready (466x466)`. The visible
  area starts at controller column 6; a full-width draw lands at 6..471 with
  no strip at the left edge and no torn column at the right.
* **RGB565 byte order (0.6.0).** The first AMOLED build skipped the swap the
  ST77916 needed and the glass showed white text as light green, the green arc
  as dark green and the near-black arc track as pink (`0x10C3` → `0xC310`).
  With the swap back in `flushCb` a human looked at the glass and confirmed
  the value is white, the arc green and the track a dim grey ring. This is the
  check a framebuffer capture cannot do — the captures were clean throughout.
* **Seven schemes on the glass (0.6.0).** Midnight, Orange, Green, Blue,
  Purple, White and Red were captured from the real framebuffer
  (`tools/capture_frame.py`, 466x466) and inspected, and the Midnight default
  was confirmed on the panel.
* **The 96 px value face (0.6.0).** Copied from the dash's 96 px face and
  regenerated to include `.`; without it the decimal point drew as a hollow
  box, which the capture showed immediately. The value row now reads `18.4`
  with a real period on the digits' baseline.
* **The touch controller answers (0.6.0).** `[touch] CST9217 at 0x5A,
  466x466, tap = scheme, swipe = screen` — command mode, resolution and chip
  type all read back from the controller itself. **The gesture classifier has
  not been exercised by a human finger**: tap and swipe are reasoned from the
  driver, not measured.
* **The splash (0.6.0).** The 466x466 JPEG decoded in **147 ms**
  (`[splash] picture 466x466 in 147 ms (14414 B flash, 434312 B PSRAM)`) and
  the capture shows the green rule on the artwork's track at 2 s.
* **The G-meter on the larger canvas (0.6.0).** A synthetic 0.5 Hz sweep
  capture shows the 65 px blob, the fading tail and the `0.58 g` readout
  centred at the bottom of the 466x388 canvas.
* **Scheme migration (0.6.0).** A board updating in place keeps its colour:
  the stored 1.85 index is mapped once through `{2,1,6,3,5,4}` and logged as
  `[set] scheme <old> -> <new> (1.85 order to dash order)`. A fresh board has
  no `scheme` key and comes up Midnight.
* **TE-synced flush (0.6.0).** `[lcd] TE sync on (4 edges, high 916 low
  20604, rising edge)` — the CO5300 TE output on GPIO13 pulses, and it is
  short-high, so the flush waits for the rising edge. The panel IO queue depth
  is 64.
* **QMI8658 and G-meter (0.5.0, re-checked 0.6.0).** The IMU answers at I2C
  `0x6B` on the new board's bus and the boot log reports
  `[imu] QMI8658 at 0x6B, accel +/-4 g 250 Hz, gyro 256 dps`. With the board
  at rest the console `g` key reads `avg=0.00 valid=1`.
* **G-meter math (host).** `tools/gmeter_test.cpp` passes: vertical
  +0.50 g under acceleration and -0.50 g under braking (upright and flat
  mounts), a diagonal 0.5/0.5 vector averages **0.707 g** (not 0.5), a
  15 s 0.05 g pull is not learned as gravity when the controller reports
  speed, a 20 degree stationary tilt is re-learned, and Zero leaves the
  reading at 0.00 g. Zero with no sample stays invalid.
* **BOOT key state machine (host).** `tools/boot_button_test.cpp` passes: one
  short event, one long event at 700 ms, the long release swallowed, and
  contact bounce on the way into a long press emits no short event. The
  physical long press has not been exercised yet.
* `OHX_STATUS` is received from the `OpenHaldex-C6` AP on channel 1 at about
  **9.8 packets/s** with `rssi` around -75 dBm.
* IAT decodes from CANN `Engine_3` (`0x380`) and displayed **4.5 C** on the
  bench, matching `0.75 * byte1 - 48`. Engine **oil temperature** is decoded
  from the same CANN relay on `Cluster_2` (`0x420`) byte 3 as
  `value - 60 °C` (DBC `OilTemperature_4_1`); `0xFF` means absent and draws
  `-- °C` on the glass and `null` in `/api/status`. Needs a CANN sender to
  confirm the decode on the bench.
* **Setup AP and settings page.** `CANv-gauge`, open, on
  `http://192.168.4.1/`, started on the receiver's channel. `GET /api/status`
  returns `fw`, scheme, scale, mode, mount/`mres`, flip, boost/IAT/oil,
  `g100`/`gv100`/`gl100`, channel, RSSI, packet rate and heap. With no IMU
  reading the G fields are `null`, exactly like `--` on the glass. Invalid
  values return HTTP 400; the scheme endpoint accepts `0-6`.
* **Settings persist.** Scheme, scale, display mode, G-meter mount and flip
  survive an OTA reboot. A first attempt lost the scheme because
  `Preferences::getUChar()` rejects the four-byte `putInt()` record; all keys
  are now `getInt`.
* **OTA over the AP.** Uploads of the 1.30-1.39 MB images (HTTP POST to
  `/update`) returned the "Update accepted" page in about 5-7 s, rebooted, and
  booted the new image (proven by the changed `/api/status` JSON). The upload
  writes the inactive slot.
* **Channel policy with the AP.** With a page client connected the console
  logs `[now] page client: holding channel 1` and channel hopping stops. The
  AP's country policy reports `1-11`; the hop limit is lowered to match. With
  the AP up, the gauge stays on one channel and discovery is the 15 s full
  scan.
* The link state renders as a **26x5 px rounded line** at the bottom edge:
  **green** = streaming OHX_STATUS, **orange** = stale/lost OHX_STATUS,
  **amber** = IAT only (CANN), **red** = never received anything.
* LVGL buffers are released from the panel IO's `on_color_trans_done`
  callback, after the QSPI DMA transfer finishes.
* Async WiFi scan runs in background; main loop (LVGL, BOOT key) is not
  blocked during channel hopping. With no anchor and the USB console
  draining, `ohxnow::poll()` measured at most about **0.85 ms** while
  hopping every 400 ms and rescanning every 15 s.
* **Cruise Down long.** The GRA_New decode separates Cancel+Down (scheme,
  `[ui] cruise combo`) from Down-long alone (`[ui] cruise mode`, screen
  switch). The stalk supplies GRA_Down_Long; this has not been pressed in the
  car yet.
* **USB console writes do not block the loop.** With no host draining
  `/dev/ttyACM0`, the disconnected-state `[scan] channel` lines could hold
  `loop()` for seconds; one press measured **6.9 s** from ISR to handler.
  `Serial.setTxTimeoutMs(0)` makes normal-build writes non-blocking; the same
  no-monitor press then measured **~40 ms** and felt immediate. The capture
  build keeps the blocking timeout so framebuffer dumps are not shortened.
* **Live boost sweep.** A synthetic 0-25 PSI sweep reproduced in-car
  corruption on the bench: the thick arc left a trail of old indicator pixels
  and the value row showed stale blocks. The fix invalidates the whole arc on
  each displayed value change; the sweep and a car re-test are clean.
* **Terminal 58s auto-dimming.** `Cluster_2` (`0x420`) byte 6 is decoded as
  Terminal 58s, using the same field and `5 + 60 × 58s/100` night curve as
  `OpenHaldex-S3-Dash`. On the 1.85 the LEDC backlight was verified in the
  car; the AMOLED emission curve is a bench starting point (85% day) and has
  **not** been checked against a dash dimmer.
* The headless `esp32dev` configuration builds; its receiver path is
  unchanged from the earlier WROOM bench run.

* **Oil temperature page (0.6.1).** The dedicated 270° oil arc face is wired
  to the same decode and now has its own screen in the three-mode cycle
  (boost → oil → G-meter → boost). The arc default scale is 40–120 °C, the
  stored setting is editable from the web page via `/oscale` (min/max, 0.1 °C
  steps), and the scale plus mode are persisted in NVS with the mode
  migration (old mode 1 → new mode 2). Build sizes for 0.6.1:
  flash **1,429,043 B (21.8%)**, static RAM **118,608 B (36.2%)**; headless
  `esp32dev` 954,044 B (72.8%) flash / 48,384 B (14.8%) RAM. Both builds pass.

- [x] **Coolant and oil-pressure pages (0.6.2).** New dedicated 270° arc faces:
  Coolant from Engine_1 (0x280) byte 1 (1 °C/count, -40 °C offset; 0xFF →
  "-- °C"). Oil pressure from Cluster_2 (0x420) byte 4 (0.1 bar/count;
  0xFF absent → "-- bar"). Five-mode cycle now Gauge → Oil → OilPress →
  Coolant → GMeter → Gauge with 280 ms slide. Coolant scale (default 40..120
  °C, min span 10 °C) and oil-pressure scale (default 4..20 bar, min span 1
  bar) added to NVS (`src/app/settings.cpp`), configurable from the web page
  via `/cscale` and `/opscale`, reported in /api/status. Swipe order matches
  the touch gestures (tap steps scheme, horizontal swipe slides).

## Not verified yet

* **Touch gestures on the glass.** The controller answers and reports its own
  resolution, and `mirror_x=1, mirror_y=1, swap_xy=0` follows Waveshare's BSP,
  but no finger has been on the panel through this firmware. Confirm a tap
  steps the scheme exactly once (`[ui] touch tap`), that a swipe left and
  right each slide once (`[ui] touch swipe left|right`), and that `t` logs the
  corner you actually touched. Also confirm the round glass edge does not
  accept a touch outside the visible circle.
* **The slide animation.** It is a 280 ms cubic ease-out of both roots and it
  ran without a crash, but it was not watched frame by frame on the glass, and
  no second swipe mid-slide has been tried (it is refused by design).
* **G-meter in a car.** Only the at-rest zero was measured (board on a desk,
  `avg=0.00`). No real acceleration, braking or cornering has been applied,
  so the axis signs, the 1 s average against a known G value, the gravity
  tracker with controller speed, and the 8 Hz filter on a rough road are all
  unverified. The first drive should confirm the blob moves **up** under
  acceleration and **down** under braking; tick **Flip accel/brake** if it is
  reversed.
* **AMOLED brightness and the 58s curve.** Day is 85% of register `0x51` and
  night is `5 + 60 x 58s/100`; that curve was ported, not measured on an
  emissive panel. Check night levels for flicker/banding and confirm the dash
  dimmer still maps sensibly.
* **AMOLED burn-in.** A fixed arc, tick labels and captions are lit for hours.
  No idle screen-off or pixel shift exists yet; watch for ghosting after long
  bench sessions. See `TODO.md`.
* **G-meter mode switch and fade on the physical BOOT key.** The short/long
  state machine passes its host test, and the mode switch was exercised via
  the console `m` key and the web page. The 700 ms hold itself was not
  physically pressed.
* **In-car gravity/auto-mount.** The speed gate and auto mount are host- and
  bench-tested; a real launch and a slope change are still needed.
* **G-meter long-run behaviour.** No multi-hour drive; the gravity tracker,
  zero drift over temperature, and the tail's redraw cost at sustained 100 Hz
  have not been watched over time.
* **In a car.** Most readings above are from the bench rig and a CANN sender.
  The live boost display has been exercised in the car on the 1.85 build;
  numeric accuracy in the car is still unverified, and nothing has been in a
  car on the AMOLED board.
* **Boost above ambient.** On the bench the controller sent `0` or the
  absent sentinel; the gauge showed `0.0` or `--`. The car showed a rising
  boost on the 1.85 build, but it has not been compared against a known
  manifold pressure.
* **Cruise-stalk combo in the car.** The PQ `0x38A` decode is implemented,
  but it has not been pressed in the car yet. Confirm one scheme step per
  Cancel + Down press.
* **ESP-NOW while a page client is connected.** No controller was on the
  bench during the web tests, so reception with an associated client (and with
  the AP up in general) is reasoned from AP+STA, not measured. Check
  `[gauge] OHX=.../s` while the page is open.
* **AP channel move onto the anchor.** The AP starts on channel 1 and is
  expected to follow the first scan's channel; the bench had no controller, so
  the move was not observed. In the car the controller's AP is channel 1.
* **Hidden anchor SSID.** With the AP up, discovery is by name only, so a
  controller AP with a hidden SSID would not be found (see `TODO.md`).
* **Long-run stability.** No soak test past a few minutes; PSRAM pool and
  internal RAM have not been watched over hours.

## Build sizes, 0.6.2

Measured with `./.venv/bin/pio run -e esp32s3`:

| Resource | Used | Capacity | Note |
|---|---|---|---|
| Flash | 1,440,087 B (22.0%) | 6,553,600 B | one of two 6.25 MB OTA app slots |
| Static RAM | 118,808 B (36.3%) | 327,680 B | one 466×48 draw buffer (44,736 B) |
| LVGL pool | 128 KB | — | allocated from 8 MB PSRAM |
| Draw buffers | 44,736 B | — | 1 x 466x48 RGB565, internal RAM |
| G-meter canvas | 361,616 B | — | 466x388 RGB565 in PSRAM, one object |
| Splash picture | 434,312 B | — | 466x466 RGB565 in PSRAM, freed when the splash goes |

The move cost **35,721 B** of flash and **1,832 B** of static RAM over 0.5.2
(1,387,278 B / 116,632 B): the CO5300 driver, the touch driver, the 96 px
face and the larger splash JPEG against the six stock Montserrat faces that
were dropped. The G-meter canvas and the splash picture are allocated from
PSRAM, so they do not appear in the static RAM figure. The headless
`esp32dev` build is 953,408 B (72.7%) flash and 48,368 B (14.8%) RAM.

The capture build (`-DGAUGE_CAPTURE=1`) temporarily raises the LVGL pool to
1 MB because a snapshot is a full 466x466 RGB565 image. That build is only for
UI verification; normal firmware keeps 128 KB.

## How this was checked

```bash
./.venv/bin/pio run -e esp32s3
./.venv/bin/pio run -e esp32dev
PLATFORMIO_BUILD_FLAGS="-DGAUGE_CAPTURE=1" \
  ./.venv/bin/pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0
./.venv/bin/python tools/capture_frame.py /dev/ttyACM0 /tmp/gauge

# G-meter layout (synthetic 0.5 Hz sweep)
PLATFORMIO_BUILD_FLAGS="-DGAUGE_CAPTURE=1 -DGAUGE_CAPTURE_GMETER=1" \
  ./.venv/bin/pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0
./.venv/bin/python tools/capture_frame.py /dev/ttyACM0 /tmp/gmeter 1

# boot splash at 2 s
PLATFORMIO_BUILD_FLAGS="-DGAUGE_CAPTURE=1 -DGAUGE_CAPTURE_SPLASH=1" \
  ./.venv/bin/pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0
./.venv/bin/python tools/capture_frame.py /dev/ttyACM0 /tmp/splash 1

# host-only checks
g++ -std=c++17 -Isrc -Iinclude -o /tmp/gmeter_test tools/gmeter_test.cpp src/app/gmeter.cpp -lm
/tmp/gmeter_test
g++ -std=c++17 -Itools/host -Isrc -Iinclude -o /tmp/boot_button_test tools/boot_button_test.cpp src/hal/boot_button.cpp
/tmp/boot_button_test

# web page, settings and OTA (join CANv-gauge first)
curl -s http://192.168.4.1/api/status
curl -s -X POST "http://192.168.4.1/scheme?s=3"
curl -s -X POST "http://192.168.4.1/scale?min=-5&max=30"
curl -s -X POST "http://192.168.4.1/mode?m=1"
curl -s -X POST "http://192.168.4.1/gmount?m=0&flip=0"
curl -s -X POST "http://192.168.4.1/gzero"
curl -F "firmware=@.pio/build/esp32s3/firmware.bin" http://192.168.4.1/update
```

Console evidence for the boot, the live link and the IMU:

```text
[lcd] CO5300 init (466x466, gap 6, 80 MHz QSPI)
[lcd] TE sync on (4 edges, high 916 low 20604, rising edge)
[imu] QMI8658 at 0x6B, accel +/-4 g 250 Hz, gyro 256 dps
[touch] CST9217 at 0x5A, 466x466, tap = scheme, swipe = screen
[splash] picture 466x466 in 147 ms (14414 B flash, 434312 B PSRAM)
[now] scan: 7 APs; anchor "CANv-dash" ch 2 (-48 dBm)
[gauge] ch=2 waiting (raw=0 rejects=0)
```
