# Status

Last updated: 18 Nov 2026, firmware **0.5.2**.

> **Hardware state.** The ESP32-S3-LCD-1.85 that this firmware was developed
> against has a **failed panel**, so nothing new has been measured on glass
> since 0.5.2. The replacement board is a Waveshare **ESP32-S3-Touch-AMOLED-1.75**;
> moving the firmware to it is **researched only** — see
> [`docs/board-port-amoled-175.md`](board-port-amoled-175.md). No code has been
> changed for it, and nothing in that note has been measured on hardware. The
> firmware in this tree still targets the 1.85.

## Verified on the bench

* The **Waveshare ESP32-S3-LCD-1.85** panel initialises over QSPI, reads ID
  `00 02 7F 7F`, selects the vendor init sequence, and reaches
  `[lcd] Waveshare ESP32-S3-LCD-1.85 ready (360x360)`.
* The gauge renders through LVGL 9.3 in partial mode with byte-swapped
  RGB565 flushes. The six accent schemes were captured from the real
  framebuffer (`tools/capture_frame.py`) and inspected: green, orange, red,
  blue, white, purple.
* **QMI8658 and G-meter (0.5.0).** The IMU answers at I2C `0x6B` and the boot
  log reports `[imu] QMI8658 at 0x6B, accel +/-4 g 250 Hz, gyro 256 dps`.
  With the board at rest, the console `g` key and `/api/status` both read
  zero (`avg=0.00 valid=1`, `g100:0`) after the 1 s average filled. A boot
  into the stored G-meter mode shows `-- g` until that window is full, then
  the blob and `0.00 g`. Mode, mount and flip survived an OTA reboot
  (`mode:1, mount:0, flip:0` came back).
* **G-meter layout (0.5.0).** A capture build with
  `-DGAUGE_CAPTURE_GMETER=1` fed a synthetic 0.5 Hz sweep and produced six
  360x360 frames, one per accent. They show the 50 px accent blob, the
  fading tail of smaller circles, and the white `0.58 g` / `0.75 g` readout
  at the bottom. The synthetic lateral component also moved the blob
  horizontally, so the 2D dot is exercised. The gauge capture was repeated
  after both screens moved to their own full-screen root: the arc, value row,
  ticks and IAT are unchanged.
* **G-meter math (host, 0.5.0).** `tools/gmeter_test.cpp` passes: vertical
  +0.50 g under acceleration and -0.50 g under braking (upright and flat
  mounts), a diagonal 0.5/0.5 vector averages **0.707 g** (not 0.5), a
  15 s 0.05 g pull is not learned as gravity when the controller reports
  speed, a 20 degree stationary tilt is re-learned, and Zero leaves the
  reading at 0.00 g. Zero with no sample stays invalid.
* **BOOT key state machine (host, 0.5.0).** `tools/boot_button_test.cpp`
  passes: one short event, one long event at 700 ms, the long release
  swallowed, and contact bounce on the way into a long press emits no
  short event. The physical long press has not been exercised yet.
* **TCA9554 IMU INT pins (0.5.0).** The expander's P3/P4 (QMI INT2/INT1)
  are now inputs while P0/P1/P2 stay outputs. The panel still resets and
  boots (`[tca9554] ready at 0x20`, `[lcd] ... ready`), so the LCD reset path
  is unaffected.
* **Boot splash (0.5.1).** The embedded 360x360 JPEG decoded in **94 ms**
  with the S3 mask ROM (`[splash] picture 360x360 in 94 ms (19112 B flash,
  259200 B PSRAM)`). The splash was captured at two seconds with the green
  rule about 43% across the artwork's red track (captured at 0.5.1; the label follows the build, now 0.5.2);
  `[ui] splash up`/`[ui] splash down` bracket the three seconds. The panel
  was dark until the first frame was queued, so no init noise was shown.
* **TE-synced flush (0.5.1).** The boot log reports
  `[lcd] TE sync on (4 edges, high 1029 low 20495, rising edge)`: the ST77916
  TE output on GPIO18 is pulsing and the flush waits for its V-blank edge.
  The panel IO queue depth is 64. This is the fix for the moving G-meter
  dot; the framebuffer captures were already clean, so the glass still needs
  a human look while the dot moves.
* **Auto mount (0.5.1).** On the flat bench, `POST /gmount?m=2&flip=0`
  returned `mount:2` and the next status reported `mres:1` (flat), as
  expected from gravity on chip Z. Restored to explicit upright afterwards.
  `tools/gmeter_test.cpp` covers both auto resolutions.
* **G-meter redraw load (0.5.0).** The first G-meter build applied the mode
  fade's opacity at loop rate (up to 200 Hz), and the tail re-applied 20
  opacity styles on every 50 ms position step. That is far above the 60 Hz
  panel and overflows LVGL's 64-slot invalid-area buffer, which turns a busy
  frame into a full 360x360 repaint; it is the likely cause of the visible
  distortion around the mode switch. The fade now steps once per display
  frame (16 ms), `LV_INV_BUF_SIZE` is 128, the tail only reapplies opacity
  when the master changes, the blob is 30 Hz and the tail/readout 20 Hz.
  The framebuffer captures are clean; the panel itself needs a human look
  while the blob is moving. This alone did not cure the user-visible tearing
  on 0.5.0; 0.5.1 adds the TE sync and the 64-deep panel queue described
  above, plus a single 360x240 movement-band invalidation (y 60..299). The
  twenty tail dots were being flushed as separate 6-24 px areas in arbitrary
  order; the band is redrawn top-to-bottom in four 60-line tiles instead.
  0.5.2 also serialises the flushes with one partial draw buffer and draws
  the dot, tail and readout into one 360x300 canvas. Only that canvas is
  invalidated, so there are no per-object dot/text boxes for the panel to
  catch separately; the earlier two-buffer path let the band, dot and text
  areas overlap on the wire.
* `OHX_STATUS` is received from the `OpenHaldex-C6` AP on channel 1 at about
  **9.8 packets/s** with `rssi` around -75 dBm.
* IAT decodes from CANN `Engine_3` (`0x380`) and displayed **4.5 C** on the
  bench, matching `0.75 * byte1 - 48`.
* The edge-to-edge 360 px arc, the scale labels at radius 145, the 72 px
  value row, `BOOST PSI` caption and IAT row fit without overlap. The decimal
  point is a baseline `.` glyph; the empty arc background is one consistent
  colour. All six schemes were captured and inspected after the 0.3.6 layout
  change. The 1 px full-circle rim was removed in 0.4.0: it was hidden behind
  the arc everywhere except the 90 degree gap at the bottom, where it read as
  a stray line between the arc ends.
* **Setup AP and settings page (0.4.0, extended 0.5.0).** `CANv-gauge`, open,
  on `http://192.168.4.1/`, started on the receiver's channel. The page was
  loaded from a laptop over the AP (4,345 bytes in 0.4.0; about **6.1 KB**
  with the Display/auto-mount section). `GET /api/status` returns `fw`, scheme, scale, mode,
  mount/`mres` (setting/resolved), flip, boost/IAT, `g100`/`gv100`/`gl100`, channel, RSSI, packet rate
  and heap. With no IMU reading the G fields are `null`, exactly like `--` on
  the glass. `POST /mode?m=0|1`, `/gmount?m=0|1&flip=0|1` and `/gzero` all
  returned `{"ok":true}` and were reflected on the next status read; invalid
  values returned HTTP 400 (`mode must be 0 or 1`, `flip must be 0 or 1`).
  Unknown paths 404. The scheme and scale endpoints from 0.4.0 still work.
* **Settings persist (0.4.0, extended 0.5.0).** Scheme 3 and a 0..25 PSI scale
  survived an OTA reboot: the next boot logged
  `[set] scheme 3, scale 0.0..25.0 PSI` and `[ui] accent blue`. The G-meter
  mode, mount and flip also survived the 0.5.0 OTA (`[set] mode g-meter,
  mount upright, flip 0`). A first attempt lost the scheme because
  `Preferences::getUChar()` rejects the four-byte `putInt()` record; all keys
  are now `getInt`.
* **Scale and negative values (0.4.0).** The capture build with a stored
  -5..30 PSI scale and `-DGAUGE_CAPTURE_BOOST=-2.5f` rendered six labels
  `-5, 2, 9, 16, 23, 30`, `-2.5` in the value row with a real minus glyph,
  and the arc indicator just above the minimum. No rim line at the bottom.
* **OTA over the AP (0.4.0, re-tested 0.5.0).** Three uploads of the 1.30 MB
  0.4.0 `firmware.bin` (HTTP POST to `/update`) each returned the "Update
  accepted" page in about 5-7 s, rebooted, and booted the new image (proven by
  the changed `/api/status` JSON). The 1.39 MB 0.5.2 image took about **7 s**;
  settings and mode survived. The upload writes the inactive slot.
* **Channel policy with the AP (0.4.0).** With a page client connected the
  console logged `[now] page client: holding channel 1` and channel hopping
  stopped. The AP's country policy reports `1-11`; the hop limit is lowered
  to match. With the AP up, the gauge stays on one channel and discovery is
  the 15 s full scan (`scan: 6 APs` with the AP already serving).
* The link state renders as a **20x4 px rounded line** at the bottom edge:
  **green** = streaming OHX_STATUS, **orange** = stale/lost OHX_STATUS,
  **amber** = IAT only (CANN), **red** = never received anything.
* LVGL buffers are released from the panel IO's `on_color_trans_done`
  callback, after the QSPI DMA transfer finishes.
* Async WiFi scan runs in background; main loop (LVGL, BOOT key) is not
  blocked during channel hopping. With no anchor and the USB console
  draining, `ohxnow::poll()` measured at most about **0.85 ms** while
  hopping every 400 ms and rescanning every 15 s.
* **Cruise Down long (0.5.2).** The GRA_New decode now separates
  Cancel+Down (accent, `[ui] cruise combo`) from Down-long alone
  (`[ui] cruise mode`, screen switch). The stalk supplies GRA_Down_Long;
  this has not been pressed in the car yet.
* **BOOT key (0.3.x, changed 0.5.0).** Earlier firmware used an ISR and was
  exercised on the bench: a normal press, a quick tap and a 1 s hold each
  produced exactly one scheme change, with ISR-to-handler 0.7-4.6 ms. 0.5.0
  replaced the ISR with a 30 ms poll and a 700 ms long-press timer, because a
  bounce on the way into a long press could otherwise emit a spurious accent
  change. The new state machine passes `tools/boot_button_test.cpp` (short,
  long, long release, bounce), but the physical long press has not yet been
  pressed on the board.
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

* **G-meter in a car.** Only the at-rest zero was measured (board on a desk,
  `avg=0.00`). No real acceleration, braking or cornering has been applied,
  so the axis signs, the 1 s average against a known G value, the gravity
  tracker with controller speed, and the 8 Hz filter on a rough road are all
  unverified. The first drive should confirm the blob moves **up** under
  acceleration and **down** under braking; tick **Flip accel/brake** if it is
  reversed. Mount **Auto** should pick upright or flat from gravity;
  choose the explicit setting if the gauge is at an angle.
* **G-meter mode switch and fade on the physical BOOT key.** The short/long
  state machine passes its host test, and the mode switch was exercised via
  the console `m` key and the web page. The 700 ms hold itself was not
  physically pressed, and the 260 ms cross-fade was not captured frame by
  frame; it ran without a crash.
* **G-meter tearing on the glass after TE sync.** The TE signal is confirmed
  pulsing and the flush now waits for it, but the panel was not photographed
  while the dot moved. The user still reported redraw trouble before this
  change; a visual check with the new build is the remaining test.
* **In-car gravity/auto-mount.** The speed gate and auto mount are host- and
  bench-tested; a real launch and a slope change are still needed.
* **TCA9554 P3/P4 as inputs.** The LCD still resets and boots after the
  change, but the IMU INT lines were not scoped. No interrupt is enabled in
  `CTRL8`, so the pins are not expected to toggle.
* **G-meter long-run behaviour.** No multi-hour drive; the gravity tracker,
  zero drift over temperature, and the 20-dot tail's redraw cost at sustained
  100 Hz have not been watched over time. Heap with the G-meter mode active
  and a page client was about 121 KB free.
* **In a car.** Most readings above are from the bench rig and a CANN
  sender. The live boost display has been exercised in the car for the
  distortion fix, but numeric accuracy in the car is still unverified.
* **Boost above ambient.** On the bench the controller sent `0` or the
  absent sentinel; the gauge showed `0.0` or `--`. The car showed a rising
  boost, but it has not been compared against a known manifold pressure.
* **Cruise-stalk combo in the car.** The PQ `0x38A` decode is implemented,
  but it has not been pressed in the car yet. Confirm one accent step per
  Cancel + Down press.
* **ESP-NOW while a page client is connected.** No controller was on the
  bench during the 0.4.0 web tests, so reception with an associated client
  (and with the AP up in general) is reasoned from AP+STA, not measured.
  Check `[gauge] OHX=.../s` while the page is open.
* **AP channel move onto the anchor.** The AP starts on channel 1 and is
  expected to follow the first scan's channel; the bench had no anchor, so
  the move was not observed. In the car the controller's AP is channel 1.
* **Hidden anchor SSID.** With the AP up, discovery is by name only, so a
  controller AP with a hidden SSID would not be found (see `TODO.md`).
* **ESP32-S3-Touch-AMOLED-1.75 port.** Nothing has run on that board. The
  panel driver, pin map, 466 px relayout, touch and the IMU axes are all
  untested; the note lists what has to be checked first. The current 1.85
  panel failure means there is no working glass to compare against either.
* **Long-run stability.** No soak test past a few minutes; PSRAM pool and
  internal RAM have not been watched over hours. The page reported about
  121 KB free heap with one client associated.

## Build sizes, 0.5.2

Measured with `./.venv/bin/pio run -e esp32s3`:

| Resource | Used | Capacity | Note |
|---|---|---|---|
| Flash | 1,387,278 B (21.2%) | 6,553,600 B | one of two 6.25 MB OTA app slots |
| Static RAM | 116,632 B (35.6%) | 327,680 B | one 360×60 draw buffer (43,200 B) |
| LVGL pool | 128 KB | — | allocated from 8 MB PSRAM |
| Draw buffers | 43,200 B | — | 1 x 360x60 RGB565, internal RAM |
| G-meter canvas | 216,000 B | — | 360x300 RGB565 in PSRAM, one object |

The G-meter, QMI8658 driver, mode fade, extra web controls and the stock
Montserrat 40 readout face, plus the 19 KB splash JPEG and the TE/image
widget paths, cost **107,420 B** of flash and **saved 64,616 B** of static
RAM over 0.4.0 (1,279,858 B / 181,248 B); the single draw buffer removed
43,200 B and the rest came from the LVGL buffer bookkeeping. The G-meter and
splash picture buffers are
allocated from PSRAM (the splash buffer is external and freed when it
drops), so they do not appear in the static RAM figure. The headless
`esp32dev` build is 951,888 B (72.6%) flash and 48,368 B (14.8%) RAM.

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

# G-meter layout (synthetic 0.5 Hz sweep)
PLATFORMIO_BUILD_FLAGS="-DGAUGE_CAPTURE=1 -DGAUGE_CAPTURE_GMETER=1" \
  ./.venv/bin/pio run -e esp32s3 -t upload --upload-port /dev/ttyACM0
./.venv/bin/python tools/capture_frame.py /dev/ttyACM0 /tmp/gmeter

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

Console evidence for the live link and the IMU:

```text
[now] scan: 7 APs; anchor "OpenHaldex-C6" ch 1 (-74 dBm)
[can] IAT 4.5 C
[gauge] ch=1 OHX=9.8/s boost=ok IAT=ok cann=184 rssi=-74
[imu] QMI8658 at 0x6B, accel +/-4 g 250 Hz, gyro 256 dps
[imu] a=(-0.308 +0.339 -0.923) g  w=(-0.04 -0.11 +0.01) rad/s
[g] v=+0.00 lat=+0.00 avg=0.00 valid=1
```
