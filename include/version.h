#pragma once

// The version string is the changelog. Bump it for built changes, not for
// docs-only commits.
//
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
#define OHX_GAUGE_VERSION "0.3.7"
