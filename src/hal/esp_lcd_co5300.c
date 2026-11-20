/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CO5300 panel driver for the ESP32-S3-Gauge, in the shape of the vendored
 * ST77916 driver beside it: the bus and the IO handle are built by
 * display.cpp, this file is only the panel object.
 *
 * Derived from esp-iot-solution's esp_lcd_co5300_spi.c. The parts this gauge
 * does not use (sleep / deep standby, MIPI-DSI) are removed rather than
 * compiled out, because their state machine is the only reason the original
 * needs a context struct shared with the public API.
 */
#include <stdlib.h>
#include <sys/cdefs.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_log.h"

#include "hal/esp_lcd_co5300.h"

#define LCD_OPCODE_WRITE_CMD ((uint32_t)0x02)
#define LCD_OPCODE_WRITE_COLOR ((uint32_t)0x32)

// Emission register. CO5300 calls it WDBRIGHTNESSVALNOR; 0x00 is off.
#define CO5300_CMD_BRIGHTNESS (0x51)

static const char *TAG = "co5300";

static esp_err_t panel_co5300_del(esp_lcd_panel_t *panel);
static esp_err_t panel_co5300_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_co5300_init(esp_lcd_panel_t *panel);
static esp_err_t panel_co5300_draw_bitmap(esp_lcd_panel_t *panel, int x_start,
                                          int y_start, int x_end, int y_end,
                                          const void *color_data);
static esp_err_t panel_co5300_invert_color(esp_lcd_panel_t *panel, bool invert);
static esp_err_t panel_co5300_mirror(esp_lcd_panel_t *panel, bool mirror_x,
                                     bool mirror_y);
static esp_err_t panel_co5300_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_co5300_set_gap(esp_lcd_panel_t *panel, int x_gap,
                                      int y_gap);
static esp_err_t panel_co5300_disp_on_off(esp_lcd_panel_t *panel, bool on_off);

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val;  // current value of LCD_CMD_MADCTL
    uint8_t colmod_val;  // current value of LCD_CMD_COLMOD
    const co5300_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    struct {
        unsigned int use_qspi_interface : 1;
        unsigned int reset_level : 1;
    } flags;
} co5300_panel_t;

// QSPI wraps a command in a 32-bit word: opcode in the top byte, register in
// bits 15:8. A bare 8-bit command is silently ignored by the controller.
static esp_err_t tx_param(co5300_panel_t *co5300,
                          esp_lcd_panel_io_handle_t io, int lcd_cmd,
                          const void *param, size_t param_size) {
  if (co5300->flags.use_qspi_interface) {
    lcd_cmd &= 0xff;
    lcd_cmd <<= 8;
    lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;
  }
  return esp_lcd_panel_io_tx_param(io, lcd_cmd, param, param_size);
}

static esp_err_t tx_color(co5300_panel_t *co5300,
                          esp_lcd_panel_io_handle_t io, int lcd_cmd,
                          const void *param, size_t param_size) {
  if (co5300->flags.use_qspi_interface) {
    lcd_cmd &= 0xff;
    lcd_cmd <<= 8;
    lcd_cmd |= LCD_OPCODE_WRITE_COLOR << 24;
  }
  return esp_lcd_panel_io_tx_color(io, lcd_cmd, param, param_size);
}

// The panel is brought up by the vendor table; there is no default table
// here on purpose. A generic CO5300 sequence can come up blank on this glass,
// and silently falling back to one would look like a broken board.
static const co5300_lcd_init_cmd_t vendor_specific_init_default[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x63, (uint8_t[]){0xFF}, 1, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 10},
    {0x35, (uint8_t[]){0x00}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 0},
    {0x29, (uint8_t[]){0x00}, 0, 20},
};

esp_err_t esp_lcd_new_panel_co5300(
    const esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t *panel_dev_config,
    esp_lcd_panel_handle_t *ret_panel) {
  ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel,
                      ESP_ERR_INVALID_ARG, TAG, "invalid argument");

  esp_err_t ret = ESP_OK;
  co5300_panel_t *co5300 = calloc(1, sizeof(co5300_panel_t));
  ESP_GOTO_ON_FALSE(co5300, ESP_ERR_NO_MEM, err, TAG,
                    "no mem for co5300 panel");

  if (panel_dev_config->reset_gpio_num >= 0) {
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
    };
    ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG,
                      "configure GPIO for RST line failed");
  }

  switch (panel_dev_config->rgb_ele_order) {
    case LCD_RGB_ELEMENT_ORDER_RGB:
      co5300->madctl_val = 0;
      break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
      co5300->madctl_val |= LCD_CMD_BGR_BIT;
      break;
    default:
      ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG,
                        "unsupported color element order");
      break;
  }

  if (panel_dev_config->bits_per_pixel != 16) {
    ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG,
                      "only RGB565 is wired up");
  }
  co5300->colmod_val = 0x55;
  co5300->fb_bits_per_pixel = 16;

  co5300->io = io;
  co5300->reset_gpio_num = panel_dev_config->reset_gpio_num;
  co5300_vendor_config_t *vendor_config =
      (co5300_vendor_config_t *)panel_dev_config->vendor_config;
  if (vendor_config) {
    co5300->init_cmds = vendor_config->init_cmds;
    co5300->init_cmds_size = vendor_config->init_cmds_size;
    co5300->flags.use_qspi_interface = vendor_config->flags.use_qspi_interface;
  }

  co5300->flags.reset_level = panel_dev_config->flags.reset_active_high;
  co5300->base.del = panel_co5300_del;
  co5300->base.reset = panel_co5300_reset;
  co5300->base.init = panel_co5300_init;
  co5300->base.draw_bitmap = panel_co5300_draw_bitmap;
  co5300->base.invert_color = panel_co5300_invert_color;
  co5300->base.set_gap = panel_co5300_set_gap;
  co5300->base.mirror = panel_co5300_mirror;
  co5300->base.swap_xy = panel_co5300_swap_xy;
  co5300->base.disp_on_off = panel_co5300_disp_on_off;
  *ret_panel = &(co5300->base);

  ESP_LOGD(TAG, "new co5300 panel @%p", co5300);
  return ESP_OK;

err:
  if (co5300) {
    if (panel_dev_config->reset_gpio_num >= 0) {
      gpio_reset_pin(panel_dev_config->reset_gpio_num);
    }
    free(co5300);
  }
  return ret;
}

static esp_err_t panel_co5300_del(esp_lcd_panel_t *panel) {
  co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
  if (co5300->reset_gpio_num >= 0) {
    gpio_reset_pin(co5300->reset_gpio_num);
  }
  free(co5300);
  return ESP_OK;
}

static esp_err_t panel_co5300_reset(esp_lcd_panel_t *panel) {
  co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
  if (co5300->reset_gpio_num >= 0) {
    ESP_RETURN_ON_ERROR(gpio_set_level(co5300->reset_gpio_num,
                                       co5300->flags.reset_level),
                        TAG, "set reset GPIO active failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(co5300->reset_gpio_num,
                                       !co5300->flags.reset_level),
                        TAG, "set reset GPIO inactive failed");
    // The vendor driver waits 200 ms after reset before touching the bus.
    vTaskDelay(pdMS_TO_TICKS(150));
  } else {
    ESP_RETURN_ON_ERROR(tx_param(co5300, co5300->io, LCD_CMD_SWRESET, NULL, 0),
                        TAG, "send command failed");
    vTaskDelay(pdMS_TO_TICKS(80));
  }
  return ESP_OK;
}

static esp_err_t panel_co5300_init(esp_lcd_panel_t *panel) {
  co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
  esp_lcd_panel_io_handle_t io = co5300->io;

  ESP_RETURN_ON_ERROR(tx_param(co5300, io, LCD_CMD_MADCTL,
                               (uint8_t[]){co5300->madctl_val}, 1),
                      TAG, "send MADCTL failed");
  ESP_RETURN_ON_ERROR(tx_param(co5300, io, LCD_CMD_COLMOD,
                               (uint8_t[]){co5300->colmod_val}, 1),
                      TAG, "send COLMOD failed");

  const co5300_lcd_init_cmd_t *cmds = co5300->init_cmds;
  uint16_t count = co5300->init_cmds_size;
  if (cmds == NULL || count == 0) {
    cmds = vendor_specific_init_default;
    count = sizeof(vendor_specific_init_default) / sizeof(cmds[0]);
  }

  for (int i = 0; i < count; i++) {
    ESP_RETURN_ON_FALSE(cmds[i].data || cmds[i].data_bytes == 0,
                        ESP_ERR_INVALID_ARG, TAG, "cmd %02X has no data",
                        cmds[i].cmd);
    // A vendor table that also sets MADCTL/COLMOD wins over the two above.
    if (cmds[i].data_bytes > 0) {
      if (cmds[i].cmd == LCD_CMD_MADCTL) {
        co5300->madctl_val = ((const uint8_t *)cmds[i].data)[0];
      } else if (cmds[i].cmd == LCD_CMD_COLMOD) {
        co5300->colmod_val = ((const uint8_t *)cmds[i].data)[0];
      }
    }
    ESP_RETURN_ON_ERROR(
        tx_param(co5300, io, cmds[i].cmd, cmds[i].data, cmds[i].data_bytes),
        TAG, "cmd %02X failed", cmds[i].cmd);
    vTaskDelay(pdMS_TO_TICKS(cmds[i].delay_ms));
  }
  return ESP_OK;
}

static esp_err_t panel_co5300_draw_bitmap(esp_lcd_panel_t *panel, int x_start,
                                          int y_start, int x_end, int y_end,
                                          const void *color_data) {
  co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
  esp_lcd_panel_io_handle_t io = co5300->io;
  assert((x_start < x_end) && (y_start < y_end) &&
         "start position must be smaller than end position");

  x_start += co5300->x_gap;
  x_end += co5300->x_gap;
  y_start += co5300->y_gap;
  y_end += co5300->y_gap;

  ESP_RETURN_ON_ERROR(tx_param(co5300, io, LCD_CMD_CASET,
                               (uint8_t[]){
                                   (uint8_t)((x_start >> 8) & 0xFF),
                                   (uint8_t)(x_start & 0xFF),
                                   (uint8_t)(((x_end - 1) >> 8) & 0xFF),
                                   (uint8_t)((x_end - 1) & 0xFF),
                               },
                               4),
                      TAG, "send CASET failed");
  ESP_RETURN_ON_ERROR(tx_param(co5300, io, LCD_CMD_RASET,
                               (uint8_t[]){
                                   (uint8_t)((y_start >> 8) & 0xFF),
                                   (uint8_t)(y_start & 0xFF),
                                   (uint8_t)(((y_end - 1) >> 8) & 0xFF),
                                   (uint8_t)((y_end - 1) & 0xFF),
                               },
                               4),
                      TAG, "send RASET failed");

  const size_t len = (size_t)(x_end - x_start) * (size_t)(y_end - y_start) *
                     co5300->fb_bits_per_pixel / 8;
  ESP_RETURN_ON_ERROR(tx_color(co5300, io, LCD_CMD_RAMWR, color_data, len), TAG,
                      "send color data failed");
  return ESP_OK;
}

static esp_err_t panel_co5300_invert_color(esp_lcd_panel_t *panel, bool invert) {
  co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
  const int command = invert ? LCD_CMD_INVON : LCD_CMD_INVOFF;
  return tx_param(co5300, co5300->io, command, NULL, 0);
}

static esp_err_t panel_co5300_mirror(esp_lcd_panel_t *panel, bool mirror_x,
                                     bool mirror_y) {
  co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
  if (mirror_x) {
    co5300->madctl_val |= BIT(6);
  } else {
    co5300->madctl_val &= ~BIT(6);
  }
  if (mirror_y) {
    co5300->madctl_val |= BIT(7);
  } else {
    co5300->madctl_val &= ~BIT(7);
  }
  return tx_param(co5300, co5300->io, LCD_CMD_MADCTL,
                  (uint8_t[]){co5300->madctl_val}, 1);
}

static esp_err_t panel_co5300_swap_xy(esp_lcd_panel_t *panel, bool swap_axes) {
  co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
  if (swap_axes) {
    co5300->madctl_val |= LCD_CMD_MV_BIT;
  } else {
    co5300->madctl_val &= ~LCD_CMD_MV_BIT;
  }
  return tx_param(co5300, co5300->io, LCD_CMD_MADCTL,
                  (uint8_t[]){co5300->madctl_val}, 1);
}

static esp_err_t panel_co5300_set_gap(esp_lcd_panel_t *panel, int x_gap,
                                      int y_gap) {
  co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
  co5300->x_gap = x_gap;
  co5300->y_gap = y_gap;
  return ESP_OK;
}

static esp_err_t panel_co5300_disp_on_off(esp_lcd_panel_t *panel, bool on_off) {
  co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
  const int command = on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF;
  return tx_param(co5300, co5300->io, command, NULL, 0);
}

esp_err_t esp_lcd_panel_co5300_set_brightness(esp_lcd_panel_handle_t panel,
                                              uint8_t percent) {
  ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG, "invalid panel");
  co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
  ESP_RETURN_ON_FALSE(co5300->io, ESP_ERR_INVALID_STATE, TAG,
                      "panel IO not initialized");
  if (percent > 100) percent = 100;
  // Register is 0..255, not a percent, and not linear in perceived light.
  const uint8_t value = (uint8_t)(((uint16_t)percent * 255u) / 100u);
  return tx_param(co5300, co5300->io, CO5300_CMD_BRIGHTNESS, &value, 1);
}

esp_err_t esp_lcd_panel_co5300_tx_param(esp_lcd_panel_handle_t panel, int cmd,
                                        const void *param, size_t param_size) {
  ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG, "invalid panel");
  co5300_panel_t *co5300 = __containerof(panel, co5300_panel_t, base);
  return tx_param(co5300, co5300->io, cmd, param, param_size);
}
