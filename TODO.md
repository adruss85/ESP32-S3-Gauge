# TODO

Open items for the gauge. Finished work lives in `include/version.h` and
`docs/status.md`, not here.

## 1. Hardware and car

- [ ] **In-car test.** Boot from the car, confirm boost follows the manifold
      (not just `0`/`--`) and IAT follows the intake sensor. Bench != car.
      The boost display sweep itself is clean and responsive in the car.
- [ ] **First G-meter drive.** Confirm the blob moves **up** under
      acceleration and **down** under braking, and that the white average
      reacts to a known pull. If the direction is reversed, tick
      **Flip accel/brake** on the web page; **Mount Auto** should pick
      upright or flat from gravity, with the explicit setting for an angled
      mount. No real acceleration has been applied on the bench.
- [ ] **G-meter accuracy.** Compare the 1 s average with a phone/dragy over a
      few 0.3-0.8 g pulls. The filter and axis signs are only host-tested so
      far; a known reference would validate the scale.
- [ ] **BOOT long press, physically.** Hold BOOT for 700 ms and confirm
      `[ui] mode g-meter` appears exactly once and the fade runs; release
      must not also step the accent. The state machine is host-tested, but
      the key has not been held on the board since the ISR was removed.
- [ ] **G-meter zero drift over a drive.** Park on a slope, drive, return,
      and check the readout returns to 0.00 without pressing Zero. The
      gravity tracker now trusts controller speed 0 at standstill; verify
      that engine vibration no longer freezes the zero.
- [ ] **TE-synced G-meter on the glass.** The boot log confirms the TE
      signal, but watch the dot move and confirm the tearing/ghosting is
      gone. If not, the remaining step is a full-frame/TE-locked flush.
- [ ] **Splash on the glass.** The framebuffer capture is clean; confirm the
      3 s rule and version label read well at the panel's real brightness.
- [ ] **Cruise-stalk accent combo.** Press Cancel + Down together on the PQ
      stalk and confirm it changes the accent exactly once (`[ui] cruise combo`).
- [ ] **Cruise-stalk mode switch.** Hold Down alone; confirm
      `[ui] cruise mode` and one screen switch. The gauge relies on the
      stalk's GRA_Down_Long bit; verify that bit in a car capture.
- [ ] **Soak test** for at least an hour with ESP-NOW traffic, the display and
      the G-meter running; watch free internal RAM and PSRAM.
- [ ] **Board move to the ESP32-S3-Touch-AMOLED-1.75.** The 1.85 unit's panel
      has failed. The replacement is the 466x466 CO5300 AMOLED board (AXP2101
      PMIC, CST9217 touch). The research note is
      [`docs/board-port-amoled-175.md`](docs/board-port-amoled-175.md); no code
      has moved and nothing will until the unit is in hand. Nothing in that
      note has been measured on hardware.
- [x] **Touch is used.** Decided: the CST9217 on the AMOLED board becomes an
      LVGL input device. What a tap does is still open — see the port note and
      the touch item under "Firmware behaviour". The 1.85's separate touch bus
      on GPIO1/3 was never wired into this firmware.

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
- [ ] **Touch tap behaviour.** Enabled on the AMOLED board, but a tap has no
      defined action yet. Decide what it does without duplicating BOOT short
      (accent) / long (screen), whether it wakes a dimmed screen, and how to
      avoid an accidental tap changing a stored setting. Keep it local: the
      car link stays receive-only.
- [ ] **G-meter filter tuning.** Revisit the 8 Hz low-pass, the 0.06 g /
      0.2 rad/s fallback gate and the 2 s gravity constant after real road
      data. The speed gate and auto mount are host/bench-tested only.
- [ ] **Confirm IAT against a known source.** `0x380` byte 1 only exists when
      the engine ECU broadcasts `Engine_3`; the PQ patch copies it into
      `Motor_7`, which is a separate signature. A car capture would settle it.
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
