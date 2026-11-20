/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Trimmed to what this gauge uses: create, init, draw, gap, invert and
 * emission brightness. Taken from esp-iot-solution's esp_lcd_co5300 (SPI
 * variant); the sleep/deep-standby path is left out because the gauge never
 * sleeps the panel.
 */
#pragma once

#include <stdint.h>

#include "esp_lcd_panel_vendor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCD panel initialisation commands.
 */
typedef struct {
    int cmd;                /*<! The specific LCD command */
    const void *data;       /*<! Buffer that holds the command specific data */
    size_t data_bytes;      /*<! Size of `data` in memory, in bytes */
    unsigned int delay_ms;  /*<! Delay in milliseconds after this command */
} co5300_lcd_init_cmd_t;

/**
 * @brief Panel vendor configuration, passed in esp_lcd_panel_dev_config_t.
 */
typedef struct {
    const co5300_lcd_init_cmd_t *init_cmds;  /*!< Init commands; NULL for the
                                                 driver's own default table */
    uint16_t init_cmds_size;                 /*<! Number of commands */
    struct {
        unsigned int use_qspi_interface : 1; /*<! 1 for QSPI, 0 for plain SPI */
    } flags;
} co5300_vendor_config_t;

/**
 * @brief Create a CO5300 panel.
 */
esp_err_t esp_lcd_new_panel_co5300(const esp_lcd_panel_io_handle_t io,
                                   const esp_lcd_panel_dev_config_t *panel_dev_config,
                                   esp_lcd_panel_handle_t *ret_panel);

/**
 * @brief Set panel emission (register 0x51), 0-100 percent.
 *
 * An AMOLED has no backlight; this is the only brightness control there is.
 * 100% writes 0xFF. The register is not linear in perceived brightness.
 */
esp_err_t esp_lcd_panel_co5300_set_brightness(esp_lcd_panel_handle_t panel,
                                              uint8_t percent);

/**
 * @brief Send an already-encoded QSPI command (opcode in the top byte).
 *
 * display.cpp uses this for the vendor page commands that are not part of the
 * copied init table.
 */
esp_err_t esp_lcd_panel_co5300_tx_param(esp_lcd_panel_handle_t panel, int cmd,
                                        const void *param, size_t param_size);

#ifdef __cplusplus
}
#endif
