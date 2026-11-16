// =====================================================================
// display.cpp - Waveshare ESP32-S3-LCD-1.85 panel + LVGL 9
//
// The panel is an ST77916 on a quad-SPI bus. Waveshare's driver is used
// unmodified (src/hal/esp_lcd_st77916.c); this file owns the bus, the
// reset expander, the backlight and the LVGL display driver.
// =====================================================================
#include "hal/display.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

#include "board_config.h"
#include "hal/esp_lcd_st77916.h"
#include "hal/st77916_init.h"
#include "hal/tca9554.h"

namespace display {
namespace {

constexpr int kDrawLines = 60;  // partial draw buffer: 360 x 60 px (6 tiles, less tearing)
constexpr int kProbeHz = 5 * 1000 * 1000;

esp_lcd_panel_handle_t g_panel = nullptr;
lv_display_t *g_disp = nullptr;
bool g_ready = false;
volatile uint32_t g_flushDone = 0;
bool g_firstFlushLogged = false;

uint32_t tickCb() { return (uint32_t)millis(); }

// The SPI panel IO queues the transfer and returns immediately: the buffer
// is still being read by DMA. LVGL must not reuse it until this callback
// fires, or the next render lands in a buffer that is halfway out of the
// panel and the readout looks stretched (rolling-shutter tearing).
bool IRAM_ATTR onColorDone(esp_lcd_panel_io_handle_t,
                           esp_lcd_panel_io_event_data_t *, void *) {
  g_flushDone++;
  if (g_disp != nullptr) lv_display_flush_ready(g_disp);
  return false;
}

void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  if (w <= 0 || h <= 0 || px_map == nullptr) {
    lv_display_flush_ready(disp);
    return;
  }

  // LVGL renders in the ESP32's native little-endian RGB565; the ST77916
  // wants the high byte first. The vendor example swaps here too.
  uint16_t *p = (uint16_t *)px_map;
  const uint32_t pixels = (uint32_t)w * (uint32_t)h;
  for (uint32_t i = 0; i < pixels; i++) {
    const uint16_t v = p[i];
    p[i] = (uint16_t)((v << 8) | (v >> 8));
  }

  if (esp_lcd_panel_draw_bitmap(g_panel, area->x1, area->y1, area->x2 + 1,
                                area->y2 + 1, px_map) != ESP_OK) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      Serial.println("[lcd] draw_bitmap failed");
    }
    // No transfer will complete, so release the buffer now.
    lv_display_flush_ready(disp);
  }
}

bool initBusAndPanel() {
  spi_bus_config_t bus = {};
  bus.data0_io_num = LCD_PIN_D0;
  bus.data1_io_num = LCD_PIN_D1;
  bus.data2_io_num = LCD_PIN_D2;
  bus.data3_io_num = LCD_PIN_D3;
  bus.sclk_io_num = LCD_PIN_SCK;
  bus.max_transfer_sz = 64 * 1024;
  bus.flags = SPICOMMON_BUSFLAG_MASTER;

  esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    Serial.printf("[lcd] spi_bus_initialize failed: %s\n", esp_err_to_name(err));
    return false;
  }

  esp_lcd_panel_io_spi_config_t ioCfg = {};
  ioCfg.cs_gpio_num = LCD_PIN_CS;
  ioCfg.dc_gpio_num = -1;
  ioCfg.spi_mode = 0;
  ioCfg.pclk_hz = kProbeHz;
  ioCfg.trans_queue_depth = 10;
  ioCfg.lcd_cmd_bits = 32;
  ioCfg.lcd_param_bits = 8;
  ioCfg.flags.quad_mode = true;
  ioCfg.on_color_trans_done = onColorDone;
  ioCfg.user_ctx = nullptr;

  // Probe the panel's identification register. Waveshare's demo uses the
  // answer to choose between two init sequences; a failed read falls back
  // to the driver's default table.
  esp_lcd_panel_io_handle_t probe = nullptr;
  uint8_t reg[4] = {0, 0, 0, 0};
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioCfg,
                               &probe) == ESP_OK) {
    uint32_t cmd = ((uint32_t)0x04 << 8) | ((uint32_t)0x0B << 24);
    const esp_err_t rerr = esp_lcd_panel_io_rx_param(probe, cmd, reg, sizeof(reg));
    if (rerr == ESP_OK) {
      Serial.printf("[lcd] panel id: %02X %02X %02X %02X\n", reg[0], reg[1], reg[2],
                    reg[3]);
    } else {
      Serial.printf("[lcd] panel id read failed: %s\n", esp_err_to_name(rerr));
    }
    esp_lcd_panel_io_del(probe);
  }

  ioCfg.pclk_hz = LCD_QSPI_HZ;
  esp_lcd_panel_io_handle_t io = nullptr;
  err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioCfg, &io);
  if (err != ESP_OK) {
    Serial.printf("[lcd] QSPI io failed: %s\n", esp_err_to_name(err));
    return false;
  }

  st77916_vendor_config_t vendor = {};
  vendor.flags.use_qspi_interface = 1;
  if (reg[0] == 0x00 && reg[1] == 0x02 && reg[2] == 0x7F && reg[3] == 0x7F) {
    vendor.init_cmds = st77916_vendor_init;
    vendor.init_cmds_size = st77916_vendor_init_size;
    Serial.println("[lcd] using vendor init sequence");
  }

  esp_lcd_panel_dev_config_t panelCfg = {};
  panelCfg.reset_gpio_num = -1;  // reset is on the TCA9554
  panelCfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panelCfg.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
  panelCfg.bits_per_pixel = 16;
  panelCfg.flags.reset_active_high = 0;
  panelCfg.vendor_config = &vendor;

  err = esp_lcd_new_panel_st77916(io, &panelCfg, &g_panel);
  if (err != ESP_OK) {
    Serial.printf("[lcd] panel create failed: %s\n", esp_err_to_name(err));
    return false;
  }

  esp_lcd_panel_reset(g_panel);
  esp_lcd_panel_init(g_panel);
  esp_lcd_panel_disp_on_off(g_panel, true);
  return true;
}

bool initLvgl() {
  lv_init();
  lv_tick_set_cb(tickCb);

  g_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
  if (g_disp == nullptr) {
    Serial.println("[lcd] lv_display_create failed");
    return false;
  }

  // Two partial buffers: LVGL fills one while the other is on the wire.
  // LVGL requires them aligned to LV_DRAW_BUF_ALIGN (4 bytes); a plain
  // lv_color_t array is only guaranteed 2-byte alignment and trips the
  // assert in lv_display_set_buffers().
  alignas(LV_DRAW_BUF_ALIGN) static lv_color_t buf1[LCD_H_RES * kDrawLines];
  alignas(LV_DRAW_BUF_ALIGN) static lv_color_t buf2[LCD_H_RES * kDrawLines];

  lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(g_disp, flushCb);
  lv_display_set_buffers(g_disp, buf1, buf2, sizeof(buf1),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_default(g_disp);
  return true;
}

}  // namespace

bool begin() {
  if (g_ready) return true;

  Serial.println("[lcd] bringing up the 1.85\" ST77916");

  Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_HZ);
  if (!tca9554::begin(TCA9554_ADDR)) {
    Serial.println("[lcd] no reset expander - panel may stay dark");
  }

  // Hold the panel in reset through the expander, then release it.
  tca9554::write(TCA9554_PIN_LCD_RST, false);
  delay(10);
  tca9554::write(TCA9554_PIN_LCD_RST, true);
  delay(50);

  // Backlight off while the panel is configured, so the first frames are
  // not visible as noise.
  ledcAttach(LCD_PIN_BACKLIGHT, LCD_BACKLIGHT_HZ, LCD_BACKLIGHT_BITS);
  ledcWrite(LCD_PIN_BACKLIGHT, 0);

  if (!initBusAndPanel()) return false;
  if (!initLvgl()) return false;

  setBacklight(85);
  g_ready = true;
  Serial.printf("[lcd] %s ready (%dx%d)\n", BOARD_NAME, LCD_H_RES, LCD_V_RES);
  return true;
}

void loop() {
  if (!g_ready) return;

  if (!g_firstFlushLogged && g_flushDone > 0) {
    g_firstFlushLogged = true;
    Serial.printf("[lcd] DMA flush callback live (%lu transfers)\n",
                  (unsigned long)g_flushDone);
  }

  lv_timer_handler();
}

void setBacklight(uint8_t percent) {
  if (percent > 100) percent = 100;
  const uint32_t maxDuty = (1u << LCD_BACKLIGHT_BITS) - 1;
  ledcWrite(LCD_PIN_BACKLIGHT, (uint32_t)percent * maxDuty / 100u);
}

bool ready() { return g_ready; }

}  // namespace display
