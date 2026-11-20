#pragma once

#include <stdint.h>

#include "hal/esp_lcd_co5300.h"

#ifdef __cplusplus
extern "C" {
#endif

// Register sequence for the Waveshare ESP32-S3-Touch-AMOLED-1.75 glass.
extern const co5300_lcd_init_cmd_t co5300_vendor_init[];
extern const uint16_t co5300_vendor_init_size;

#ifdef __cplusplus
}
#endif
