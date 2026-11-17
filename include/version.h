#pragma once

// The version string is the changelog. Bump it for built changes, not for
// docs-only commits.
//
// 0.5.2 - Serialise the panel flush to one partial draw buffer. The
//         two-buffer path let the band, dot and text areas overlap on the
//         wire, leaving intermittent artifacts inside the dot/text boxes.
//         The dot, tail and readout are now one 360x300 canvas. Also
//         separates the PQ cruise stalk's Cancel+Down (accent) from
//         Down-long alone (screen switch).
// 0.5.1 - Boot splash: the 360x360 CANvas artwork is decoded in 94 ms by
//         the S3 mask ROM's TJpgDec and a green rule fills over 3 s. The
//         panel flush now waits for the ST77916 TE edge, which removes the
//         moving G-meter dot's tearing, and the gravity tracker learns at
//         controller speed 0 even with engine vibration. Mount can be auto.
// 0.5.0 - G-meter mode: the QMI8658 on the control I2C bus (0x6B) drives a
//         50 px accent blob with a 1 s fading tail and a white 1 s average
//         readout. The web page and a 700 ms BOOT hold switch screens with
//         a 260 ms cross-fade; mount (upright/flat) and direction flip are
//         stored in NVS. The TCA9554's P3/P4 are now inputs so the IMU's
//         interrupt pins are not driven by the expander.
// 0.4.0 - Setup AP "CANv-gauge": web page for accent scheme and boost scale
//         (persisted in NVS), plus OTA firmware upload over WiFi. The value
//         row can draw vacuum as a negative number, and the stray rim line
//         in the arc's bottom gap is gone.
// 0.3.7 - Cancel + Down on the PQ cruise stalk cycles the accent scheme.
// 0.3.6 - Edge-to-edge boost arc, a 72 px boost value with the scale labels
//         spaced to match, and S3-dash-style Terminal 58s auto-dimming.
// 0.3.5 - Repaint the whole boost arc when the displayed value changes; LVGL's
//         incremental arc invalidation left a trail and corrupted the value
//         row during a live boost sweep.
// 0.3.4 - USB CDC console writes are non-blocking in normal builds, so the
//         disconnected channel-hop log cannot stall loop()/BOOT key.
// 0.3.3 - BOOT key reports one scheme change per press; the poll fallback no
//         longer emits a duplicate event 200 ms after the ISR event.
// 0.3.2 - Status bar: red=never, green=streaming, orange=stale, amber=IAT-only;
//         async WiFi scan (no main-loop block); BOOT key ISR fix; 60-line tiles
//         to reduce tearing.
// 0.3.1 - Flush LVGL buffers only after the QSPI transfer completes (fixes
//         the stretched readout), interrupt-driven BOOT key for reliable
//         presses, and a small link line back at the bottom edge.
// 0.3.0 - BOOT key cycles six accent schemes, the arc is one colour and
//         50% thicker outwards, the boost decimal point sits on the
//         baseline, and the link lamp is gone.
// 0.2.0 - First gauge build: 270-degree boost arc and IAT readout.
// 0.1.0 - Headless CANN listener.
#define OHX_GAUGE_VERSION "0.5.2"
