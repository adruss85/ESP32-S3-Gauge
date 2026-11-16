#pragma once

#include <stdint.h>

#include "hal/esp_lcd_st77916.h"

#ifdef __cplusplus
extern "C" {
#endif

// Vendor register sequence used by the Waveshare 1.85" ST77916 panel.
extern const st77916_lcd_init_cmd_t st77916_vendor_init[];
extern const uint16_t st77916_vendor_init_size;

#ifdef __cplusplus
}
#endif
