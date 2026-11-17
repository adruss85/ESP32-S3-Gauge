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
constexpr size_t kTransQueueDepth = 64;
constexpr uint32_t kTeTimeoutUs = 20000;

esp_lcd_panel_handle_t g_panel = nullptr;
lv_display_t *g_disp = nullptr;
bool g_ready = false;
volatile uint32_t g_flushDone = 0;
bool g_firstFlushLogged = false;
bool g_teDisabled = false;
bool g_teRising = true;
uint32_t g_teWaits = 0;
uint32_t g_teMisses = 0;

uint32_t tickCb() { return (uint32_t)millis(); }

// The ST77916's TE output (GPIO18) pulses once per panel frame. Starting a
// flush on that edge keeps a moving blob from being written while the panel
// is scanning the same rows. Without it the G-meter's fast, high-contrast
// dot visibly tears even though the DMA buffer hand-off is correct.
//
// The wait is capped at one frame and disables itself if the pin never
// pulses (wrong init, floating pin, or a panel that does not drive TE), so
// a missing signal costs one log line rather than 20 ms per frame.
void onRenderReady(lv_event_t *) {
  if (g_teDisabled) return;

  const uint32_t started = micros();
  bool last = digitalRead(LCD_PIN_TE);
  while ((uint32_t)(micros() - started) < kTeTimeoutUs) {
    const bool now = digitalRead(LCD_PIN_TE);
    const bool edge = g_teRising ? (!last && now) : (last && !now);
    if (edge) {
      g_teWaits++;
      g_teMisses = 0;
      return;
    }
    last = now;
  }

  if (++g_teMisses >= 30) {
    g_teDisabled = true;
    Serial.println("[lcd] TE not pulsing; flushing unsynced");
  }
}

// Probe once after the panel is on. Two edges in 30 ms means a 60 Hz TE.
// The level that lasts longer is the active scan; the short level is the
// V-blank, so wait for the edge into the short level.
bool teIsPulsing() {
  uint32_t edges = 0;
  uint32_t high = 0;
  uint32_t low = 0;
  bool last = digitalRead(LCD_PIN_TE);
  const uint32_t started = micros();
  while ((uint32_t)(micros() - started) < 30000) {
    const bool now = digitalRead(LCD_PIN_TE);
    if (now) {
      high++;
    } else {
      low++;
    }
    if (now != last) {
      edges++;
      last = now;
    }
  }
  if (edges < 2) {
    Serial.println("[lcd] TE not pulsing; flushing unsynced");
    g_teDisabled = true;
    return false;
  }

  g_teRising = high <= low;  // short high = V-blank high -> rising edge
  Serial.printf("[lcd] TE sync on (%lu edges, high %lu low %lu, %s edge)\n",
                (unsigned long)edges, (unsigned long)high, (unsigned long)low,
                g_teRising ? "rising" : "falling");
  return true;
}

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
  ioCfg.trans_queue_depth = kTransQueueDepth;
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

  // Enable the tearing-effect output (TEON 0x35, parameter 0x00 = V-blank
  // only). The vendor init table is a verbatim copy and does not include
  // it; doing it here keeps the copied sequence untouched. It must be sent
  // after the vendor table's display-on but before the panel's final
  // disp-on call, matching the order that was measured to start the TE pin.
  // QSPI command encoding: the vendor's tx_param() puts the write opcode
  // in the top byte and the command in bits 15:8. Sending a bare 0x35 would
  // be ignored by the panel.
  const uint8_t teOn = 0x00;
  const int teOnCmd = (0x02 << 24) | (0x35 << 8);  // LCD_OPCODE_WRITE_CMD
  err = esp_lcd_panel_io_tx_param(io, teOnCmd, &teOn, 1);
  if (err != ESP_OK) {
    Serial.printf("[lcd] TEON failed: %s\n", esp_err_to_name(err));
  }
  delay(5);
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

  // One partial buffer, not two. The panel flush is asynchronous, and the
  // G-meter's many small areas exposed an intermittent overlap where the
  // second buffer's command/data sequence could start while the first was
  // still on the wire. Serialising the flushes costs a little throughput
  // and 43 KB of RAM; it removes that overlap.
  alignas(LV_DRAW_BUF_ALIGN) static lv_color_t buf1[LCD_H_RES * kDrawLines];

  lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(g_disp, flushCb);
  lv_display_set_buffers(g_disp, buf1, nullptr, sizeof(buf1),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_default(g_disp);
  lv_display_add_event_cb(g_disp, onRenderReady, LV_EVENT_RENDER_READY, nullptr);
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
  pinMode(LCD_PIN_TE, INPUT_PULLUP);
  teIsPulsing();
  if (!initLvgl()) return false;

  // The backlight stays off until the first frame is queued; main() turns
  // it on after the splash (or gauge) has been rendered once.
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

void applyDimmer(int term58s) {
  // -1 = no Cluster_2 frame heard; hold the current brightness rather than
  // jumping to day when the relay is briefly quiet.
  if (term58s < 0) return;

  int pct;
  if (term58s == 0) {
    pct = LCD_BACKLIGHT_DAY_PCT;
  } else {
    pct = LCD_NIGHT_MIN_PCT + (LCD_NIGHT_SPAN_PCT * term58s) / 100;
    if (pct < LCD_NIGHT_MIN_PCT) pct = LCD_NIGHT_MIN_PCT;
    if (pct > 100) pct = 100;
  }

  static int last = -1;
  if (pct == last) return;
  last = pct;
  setBacklight((uint8_t)pct);
}

bool ready() { return g_ready; }

}  // namespace display
