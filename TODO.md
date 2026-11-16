# TODO

Open items for the gauge. Finished work lives in `include/version.h` and
`docs/status.md`, not here.

## 1. Hardware and car

- [ ] **In-car test.** Boot from the car, confirm boost follows the manifold
      (not just `0`/`--`) and IAT follows the intake sensor. Bench != car.
      The boost display sweep itself is clean and responsive in the car.
- [ ] **Cruise-stalk accent combo.** Press Cancel + Down together on the PQ
      stalk and confirm it changes the accent exactly once (`[ui] cruise combo`).
- [ ] **Soak test** for at least an hour with ESP-NOW traffic and the display
      running; watch free internal RAM and PSRAM.
- [ ] Decide whether the touch panel (separate I2C on GPIO1/3) is ever used.
      It is wired on the board but this firmware ignores it.

## 2. Firmware behaviour

- [x] **Status bar semantics.** Red=never, green=streaming, orange=stale,
      amber=IAT-only. Async WiFi scan no longer blocks main loop. BOOT key
      reports one event per press.
- [ ] **Persist the accent scheme.** It currently resets to green on every
      boot. NVS would survive a power cycle, but adds a settings API; decide
      whether it is worth it.
- [ ] **Confirm IAT against a known source.** `0x380` byte 1 only exists when
      the engine ECU broadcasts `Engine_3`; the PQ patch copies it into
      `Motor_7`, which is a separate signature. A car capture would settle it.
- [ ] **Boost scale above 25 PSI.** The value is shown but the arc clamps at
      the 25 PSI scale end. Decide whether the arc should rescale or the
      gauge should warn.
- [ ] **No OTA path.** Flashing is USB only. Not needed for the bench, but a
      car install may want it.

## 3. Repository

- [ ] Add a CI build (GitHub Actions `pio run` for both environments) once the
      hardware workflow settles.
- [ ] The capture build is a manual flag; `tools/capture_frame.py` explains
      it. Consider a `pio` extra env if it is used often.
