// =====================================================================
// gauge_web.h - the CANv-gauge setup AP: settings page and OTA upload
//
// The page is served from the gauge itself. It can change the accent scheme,
// boost scale, display mode and G-meter mount (all persist in NVS), zero the
// G-meter, and accept a PlatformIO firmware.bin for the inactive OTA slot.
//
// It shares the single radio with ESP-NOW: the softAP is started on the
// channel the receiver is locked to, and ohxnow freezes channel hopping
// while a client is connected.
// =====================================================================
#pragma once
#include <stdint.h>

namespace gauge_web {

// Start the AP (open unless GAUGE_AP_PASSWORD is defined) and the HTTP
// server. Call after ohxnow::begin(), which sets the radio mode and channel.
void begin();

// Call from loop(): serve requests, refresh the page's packet rate, and
// reboot after a successful OTA upload.
void poll();

bool started();
uint8_t clients();

}  // namespace gauge_web
