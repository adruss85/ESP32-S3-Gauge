# TODO

Open items for the gauge. Finished work lives in `include/version.h` and
`docs/status.md`, not here.

## 1. Hardware and car

- [ ] **Touch on the glass.** No finger has been on the panel through this
      firmware. 0.7.0 made the scheme a 550 ms hold and gave the tap a job on
      the Haldex screen only, but the two thresholds (12 px of movement for a
      tap, 46 px of travel for a swipe) are geometry, not measurements.
      Confirm: a hold steps the scheme once and its release adds nothing; a
      swipe never does; a tap on the Haldex screen sends one mode request and
      a tap elsewhere does nothing but log `[ui] touch tap (ignored)`; and `t`
      logs the corner you touched (that is the mirror-flag check). Decide
      whether a tap should wake a dimmed screen and whether the round edge
      should reject touches outside the circle.
- [ ] **The mode request on the air.** Nothing in 0.7.0 has been sent from
      this board. Confirm a tap produces `[haldex] mode -> <name> (sent)` and
      then `<name>: ok (ack after N ms)`, that the controller's own page shows
      the same mode, and that the Haldex face updates on the next packet.
      Then try: a wrong key (`wrong key`), no stored key on a passworded AP
      (`no key`, and nothing on the air), the controller with ESP-NOW control
      disabled (`disabled`), and both this gauge and `OpenHaldex-S3-Dash`
      stepping at once (one of them should report `stale` and retry once).
- [ ] **Slide animation, watched.** The 280 ms swipe push ran without a crash
      but was not watched frame by frame; check it does not tear on the glass.
- [ ] **AMOLED burn-in.** A fixed arc, ticks and captions are lit for hours.
      Watch for ghosting after long sessions; if it appears, add an idle
      screen-off or a pixel shift after a period without `OHX_STATUS`.
- [ ] **AMOLED brightness curve.** 85% day and `5 + 60 x 58s/100` night are a
      ported starting point, not measured on an emissive panel. Check night
      levels for flicker/banding and that the dash dimmer still maps sensibly.
- [ ] **In-car test.** Boot from the car, confirm boost follows the manifold
      (not just `0`/`--`) and IAT follows the intake sensor. Bench != car.
      Nothing has been in a car on the AMOLED board.
- [ ] **First G-meter drive.** Confirm the blob moves **up** under
      acceleration and **down** under braking, and that the average
      reacts to a known pull. If the direction is reversed, tick
      **Flip accel/brake** on the web page; **Mount Auto** should pick
      upright or flat from gravity, with the explicit setting for an angled
      mount. The QMI8658 sits differently on this PCB, so the axis defaults
      are inherited rather than re-derived.
- [ ] **G-meter accuracy.** Compare the 1 s average with a phone/dragy over a
      few 0.3-0.8 g pulls. The filter and axis signs are only host-tested so
      far; a known reference would validate the scale.
- [ ] **BOOT long press, physically.** Hold BOOT for 700 ms and confirm
      `[ui] mode g-meter` appears exactly once and the fade runs; release
      must not also step the scheme. The state machine is host-tested, but
      the key has not been held on this board.
- [ ] **G-meter zero drift over a drive.** Park on a slope, drive, return,
      and check the readout returns to 0.00 without pressing Zero.
- [ ] **Splash on the glass.** The framebuffer capture is clean; confirm the
      3 s rule and version label read well at the panel's real brightness.
- [ ] **Haldex face layout.** Capture with `-DGAUGE_CAPTURE_DRIVE=1` and check
      the two value rows sit on their captions' baseline, that `60:40` and
      `EXPERT` are both comfortable at 48 px on the round glass, and that a
      five-second answer line (`60:40: wrong key`) fits without clipping.
- [ ] **Cruise-stalk accent combo.** Press Cancel + Down together on the PQ
      stalk and confirm it changes the scheme exactly once
      (`[ui] cruise combo`).
- [ ] **Cruise-stalk mode switch.** Hold Down alone; confirm
      `[ui] cruise mode` and one screen switch. The gauge relies on the
      stalk's GRA_Down_Long bit; verify that bit in a car capture.
- [ ] **Soak test** for at least an hour with ESP-NOW traffic, the display and
      the G-meter running; watch free internal RAM and PSRAM, and the panel at
      80 MHz QSPI.
- [x] **Board move to the ESP32-S3-Touch-AMOLED-1.75.** Done in 0.6.0: CO5300
      panel, GPIO39 reset, emission brightness, 466 px relayout, CST9217
      touch, seven schemes. See `docs/board-port-amoled-175.md` for what the
      bench corrected (the RGB565 byte order above all).

## 2. Firmware behaviour

- [x] **Status bar semantics.** Red=never, green=streaming, orange=stale,
      amber=IAT-only. Async WiFi scan no longer blocks main loop. BOOT key
      reports one event per press, with a 700 ms hold for the mode switch.
- [x] **Persist the accent scheme.** Scheme, boost scale, display mode and
      G-meter mount now live in NVS (`src/app/settings.cpp`) and are editable
      from the web page.
- [x] **Boot splash.** The asset JPEG is decoded by the mask ROM and the
      green rule fills over 3 s; the panel stays dark until the first frame.
- [x] **TE-synced panel flush.** TEON is in the init sequence; the render
      waits for the V-blank edge and the panel queue is 64 deep.
- [x] **G-meter mount auto.** Gravity on Y/Z chooses upright/flat when the
      setting is Auto; the explicit setting remains for angled mounts.
- [x] **G-meter mode.** QMI8658 at `0x6B`, gravity-tracked 2-axis math
      (`sqrt(v² + l²)`, 1 s mean), 50 px blob, 1 s tail, white readout, web +
      BOOT long-press mode switch with a 260 ms cross-fade. Mount/flip/zero
      are stored or explicit.
- [x] **Touch gestures.** A tap steps the colour scheme, a horizontal swipe
      slides to the other screen with a 280 ms push. Classified in
      `src/hal/touch.cpp`, delivered through `dashboard::onGesture()`, refused
      while the splash is up or a transition is running. The CST9217 is also
      registered as an LVGL pointer indev.
- [x] **Seven colour schemes.** Copied from `OpenHaldex-S3-Dash` in its order
      (Midnight, Orange, Green, Blue, Purple, White, Red) so the gauge and the
      dashboard match. Midnight is the default and the order is a wire format;
      a scheme stored by the 1.85 build is migrated once.
- [ ] **G-meter filter tuning.** Revisit the 8 Hz low-pass, the 0.06 g /
      0.2 rad/s fallback gate and the 2 s gravity constant after real road
      data. The speed gate and auto mount are host/bench-tested only.
- [ ] **Confirm IAT against a known source.** `0x380` byte 1 only exists when
      the engine ECU broadcasts `Engine_3`; the PQ patch copies it into
      `Motor_7`, which is a separate signature. A car capture would settle it.
- [x] **Oil temperature page (0.6.1).** Dedicated 270° arc face (°C value,
      IAT readout, configurable 40–120 °C scale, swipe between boost/oil/
      G-meter). Decode: CANN Cluster_2 (0x420) byte 3, -60 °C offset, 0xFF
      absent → "-- °C". Three-mode cycle: Gauge → Oil → G-meter → Gauge.
      Mode migration: old mode 1 → new mode 2. Oil scale persisted in NVS,
      web /oscale endpoint, JSON status includes omin10/omax10/oil10.
- [x] **Coolant and oil-pressure pages (0.6.2).** Dedicated 270° arc faces:
  Coolant from Engine_1 (0x280) byte 1 (1 °C/count, -40 °C offset; 0xFF →
  "-- °C"). Oil pressure from Cluster_2 (0x420) byte 4 (0.1 bar/count;
  0xFF absent → "-- bar"). Five-mode cycle Gauge → Oil → OilPress → Coolant →
  GMeter → Gauge. Scale defaults 40..120 °C (min span 10 °C) and 4..20 bar
  (min span 1 bar), editable via /cscale /opscale. JSON status reports all.
- [x] **Coolant and oil-pressure sources corrected (0.6.4).** Coolant:
      Cluster_2 (0x420) byte 4, 0.75 °C/count - 48 (0.6.2 had read Engine_1
      byte 1, which is Inner_Engine_Torque). Oil pressure: Engine_7 (0x588)
      bytes 5-6, the rear-O2 ADC through the PST-F 1 transfer, x14.5038 to
      PSI; the 8-byte frame is required and the result is not clamped.
      Default scale 0..145 PSI. Compile-verified only.
- [x] **Oil and coolant readout resolution (0.6.5).** Oil temp is a whole-degree
      count, so the oil face dropped the decimal point and the tenths digit (the
      web Live line prints whole degrees too); the arc scale stays in tenths,
      so a 0.1 °C range from /oscale still applies. Both faces draw the last
      packet straight, no smoothing loop: coolant's count is 0.75 °C/count,
      three steps of the DBC's 0.25 °C grid, and oil temp is whole degrees,
      so nothing in between is ever sent. Boost and oil pressure keep easing.
      Compile-verified only.
- [ ] **Mode migration for five modes.** (Old mode 1 → new mode 4 mapping.)
- [x] **Boost scale above 25 PSI.** The scale is a setting now; the arc
      re-ranges and the numeric value is never clipped.
- [x] **OTA path.** The setup AP serves `/update`; the inactive slot is
      written and the gauge reboots.
- [ ] **ESP-NOW with a page client.** Verify `[gauge] OHX=.../s` stays
      healthy while a phone is associated to `CANv-gauge`, and that a client
      disconnect resumes channel tracking.
- [ ] **Hidden anchor.** With the AP up, discovery is by AP name only. If a
      controller's AP turned out to be hidden, decide between a slower
      "sniff walk" and keeping the AP fixed.
- [ ] **AP password.** The AP ships open. Before a permanent install in a
      shared car, decide whether to build with `-DGAUGE_AP_PASSWORD`.
- [ ] **OTA file sanity.** `/update` accepts any upload that passes
      `Update.begin()`; a size/version report on the page would make a wrong
      file obvious before flashing.

## 3. Repository

- [ ] Add a CI build (GitHub Actions `pio run` for both environments) once the
      hardware workflow settles.
- [ ] The capture build is a manual flag; `tools/capture_frame.py` explains
      it. Consider a `pio` extra env if it is used often.
