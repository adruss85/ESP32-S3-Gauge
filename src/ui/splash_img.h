// =====================================================================
// splash_img.h - the boot splash's background picture
//
// The gradient field, the CANvas wordmark and the empty loading track are
// one pre-rendered 360x360 baseline JPEG. Drawing those gradients with
// LVGL's software renderer would band and would be slow enough for the
// progress fill to outrun the paint, so the picture is decoded once at boot
// and shown as an image.
//
// Nothing is linked to decode it: the ESP32-S3 mask ROM contains TJpgDec
// (jd_prepare / jd_decomp, exported by esp32s3.rom.ld), so this is a thin
// wrapper and costs about 1 KB of flash. The RGB565 result is in PSRAM
// (259,200 B) only for the three seconds the splash is up.
// =====================================================================
#pragma once

#include <lvgl.h>

namespace splashimg {

// Decode the embedded JPEG into an RGB565 PSRAM buffer. Returns false,
// having logged why, if the image is not baseline 360x360 or the buffers
// cannot be had. The splash then falls back to a flat field with the same
// bar and version.
bool decode();

// The decoded picture for lv_image_set_src(). nullptr until decode()
// succeeds, and again after release().
const lv_image_dsc_t *image();

// Free the picture. Called when the splash comes down so the dashboard
// gets the PSRAM back.
void release();

}  // namespace splashimg
