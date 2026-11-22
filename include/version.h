#pragma once

// The version string is the changelog. Bump it for built changes, not for
// docs-only commits.
//
// 0.7.1 - Judge the mode request by the mode flag, not by the ack. In use the
// request worked and the controller changed mode, but the ack echo
// (ackSrc/ackSeq/ackResult) never matched, so the Haldex face said "no reply"
// after a change that had already happened. The ack fields are no longer read:
// the verdict is the mode byte in the controller's next status. One tap is at
// most one frame (the STALE retry is gone, which is also what rule 2 wants);
// a second tap waits for a fresh status, because that is what refills the
// nonce. The sub-line has three answers: sending, ok (green) when the flag
// becomes the requested mode, NO CHANGE (amber) when the controller is
// broadcasting but the flag does not move, and NO CONNECTION (dim) when
// nothing is being broadcast. No ack vocabulary on the glass; a refused send
// (no key, no fresh status, radio) is console detail.
// Build sizes: flash 1,466,647 B (22.4%), RAM 119,088 B (36.3%); esp32dev
// 956,788 B (73.0%) flash / 48,520 B (14.8%) RAM.
// 0.7.0 - The Haldex face, and the gauge's first and only transmission. The
// drive view's drivetrain drawing could not be read on the glass in a car, so
// it is now four lines of type: mode (STOCK/FWD/50:50/60:40/75:25/EXPERT),
// LOCK REQ, LOCK ACT, and the coupling state under the mode. One tap asks the
// controller for the next mode: OHX_C_MODE over ESP-NOW, unicast to the MAC
// that broadcasts OHX_STATUS, HMAC-SHA256 signed with the controller's AP
// password (stored in NVS as ohxkey, entered on the web page, never logged or
// echoed). The answer arrives in the next status's ackSrc/ackSeq/ackResult
// and is drawn in the sub-line for 5 s; one STALE retry, no other retries.
// There is no lock-duty opcode in v1, so LOCK REQ/ACT are display only. The
// gauge is no longer receive-only: AGENTS.md rule 2 is now the single-write-
// path rule. Touch tap -> hold (550 ms, motionless) for the colour scheme;
// a tap now means the Haldex mode and only on that screen, because a flicked
// swipe was being read as a tap. The press's furthest excursion is tracked,
// so a press that wanders more than 12 px is never a tap. Drive is now a
// storable mode (settings accepted 0..4, so the face could not survive a
// reboot). mbedtls HMAC keeps the signature on the gauge.
// Build sizes: flash 1,467,259 B (22.4%), RAM 119,096 B (36.3%); esp32dev
// 956,968 B (73.0%) flash / 48,536 B (14.8%) RAM.
// 0.6.5 - Match the readouts to what the bus can send. Oil temperature is
// Cluster_2 (0x420) byte 3, a whole-degree count, so the oil face drops the
// decimal point and the tenths digit (the web Live line prints whole degrees
// too); the arc still scales in tenths, so a 0.1 °C range set from /oscale
// keeps working. Oil temp and coolant both draw the last packet straight: an
// eased value would sit at a temperature the bus can never send and make the
// last digit wander (coolant's count is 0.75 °C/count, three steps of the DBC
// 0.25 °C grid). Boost and oil pressure keep their 0.30 easing.
// Build sizes: flash 1,463,835 B (22.3%), RAM 118,960 B (36.3%); esp32dev
// 955,412 B (72.9%) flash / 48,432 B (14.8%) RAM.
// 0.6.4 - Correct the coolant and oil-pressure sources. Coolant is
// Cluster_2 (0x420) byte 4, 0.75 °C/count - 48 (0.6.2 read Engine_1 byte 1,
// which is Inner_Engine_Torque). Oil pressure is Engine_7 (0x588) bytes 5-6,
// the raw 10-bit ADC of the rear-O2 input now carrying a PST-F 1 sensor:
// (volts - 0.5) / 0.4 bar, x 14.5038 PSI (0.6.2/0.6.3 read Cluster_2 byte 4,
// which is coolant). Oil-pressure scale default 0..145 PSI (0..10 bar).
// Build sizes: flash 1,462,979 B (22.3%), RAM 119,008 B (36.3%); esp32dev
// 955,400 B (72.9%) flash / 48,432 B (14.8%) RAM.
// 0.6.3 - Drive screen (drivetrain visualisation): RPM, wheel speeds + slip
// %, Haldex target/actual, ABS/TC chips. Wheel slip from OHX_STATUS slip[4].
// Oil pressure now in PSI (was bar): Cluster_2 (0x420) byte 4 converted at
// 14.5038x. Oil pressure scale default 58..290 PSI (approx 4..20 bar). Six-mode
// cycle: Gauge -> Oil -> OilPress -> Coolant -> G-meter -> Drive -> Gauge.
// Fixed oil temperature mapping bug (variable shadowing in CANN Cluster_2
// byte 3 decode). Build sizes: flash 1,462,887 B (22.3%), RAM 119,000 B (36.3%).
// 0.6.2 - Coolant and oil-pressure pages: a fifth and sixth face around
// the same 270° arc and value row as the boost gauge. Coolant from
// Engine_1 (0x280) byte 1 (1 °C/count, -40 °C offset; 0xFF absent -> "--"
// °C"). Oil pressure from Cluster_2 (0x420) byte 4 (0.1 bar/count; 0xFF
// absent -> "-- bar"). Five-mode cycle now Gauge -> Oil -> OilPress ->
// Coolant -> G-meter -> Gauge. Coolant scale (40..120 °C default, min span
// 10 °C) and oil-pressure scale (4..20 bar default, min span 1 bar) added;
// persisted in NVS, configurable from the web page in 0.1 steps through
// the new /cscale and /opscale endpoints, reported in /api/status.
// Mode migration: the two-screen-to-three-screen change is superseded - old
// mode 1 (G-meter) now maps to new mode 4. Build sizes: flash 1,440,087 B
// (22.0%), RAM 118,808 B (36.3%).
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
#define OHX_GAUGE_VERSION "0.7.1"