// =====================================================================
// display.cpp - Waveshare ESP32-S3-Touch-AMOLED-1.75 panel + LVGL 9
//
// The panel is a CO5300 AMOLED on a quad-SPI bus, 466x466. Waveshare's own
// driver is vendored (src/hal/esp_lcd_co5300.c) with their board's register
// sequence (src/hal/co5300_init.c); this file owns the bus, the reset line,
// the link to LVGL and the emission brightness.
//
// Three things differ from the old 1.85" ST77916 build in ways that are easy
// to get wrong:
//   * the visible area starts at controller column 6, so every draw is
//     offset by LCD_GAP_X;
//   * there is no backlight. Brightness is register 0x51, 0-255, and 0 is
//     the only "off" there is, which is what gates the boot noise;
//   * the invalidated area is snapped to even pixel edges, which is what
//     Waveshare's BSP does for this controller.
// =====================================================================
#include "hal/display.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

#include "board_config.h"
#include "hal/co5300_init.h"
#include "hal/esp_lcd_co5300.h"

namespace display {
namespace {

constexpr int kDrawLines = 48;  // partial buffer: 466 x 48 px = 44,736 B
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

// The CO5300's TE output (GPIO13) pulses once per panel frame. Starting a
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

// Probe once after the panel is on. Two edges in 30 ms means a ~60 Hz TE.
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

// Waveshare's BSP snaps every invalidated area to even edges for this
// controller. Odd edges cost nothing on an even-width panel and the mismatch
// shows up as a one-column seam, so do the same here.
void rounderCb(lv_event_t *e) {
  lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
  if (area == nullptr) return;
  if (area->x1 & 1) area->x1--;
  if (area->y1 & 1) area->y1--;
  if (!(area->x2 & 1)) area->x2++;
  if (!(area->y2 & 1)) area->y2++;
  if (area->x2 >= LCD_H_RES) area->x2 = LCD_H_RES - 1;
  if (area->y2 >= LCD_V_RES) area->y2 = LCD_V_RES - 1;
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

  // LVGL renders in the ESP32's native little-endian RGB565; the CO5300
  // latches the high byte first, like the ST77916 did. Waveshare's own QSPI
  // bus (Arduino_ESP32QSPI::writePixels -> MSB_32_16_16_SET) swaps every
  // 16-bit value on the way out for the same reason.
  //
  // Skipping this is not subtle: white text comes out light green, the green
  // arc comes out dark green and the near-black arc track comes out pink
  // (0x10C3 -> 0xC310). The first AMOLED build did exactly that.
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
  ioCfg.pclk_hz = LCD_QSPI_HZ;
  ioCfg.trans_queue_depth = kTransQueueDepth;
  ioCfg.lcd_cmd_bits = 32;
  ioCfg.lcd_param_bits = 8;
  ioCfg.flags.quad_mode = true;
  ioCfg.on_color_trans_done = onColorDone;
  ioCfg.user_ctx = nullptr;

  esp_lcd_panel_io_handle_t io = nullptr;
  err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioCfg,
                                 &io);
  if (err != ESP_OK) {
    Serial.printf("[lcd] QSPI io failed: %s\n", esp_err_to_name(err));
    return false;
  }

  co5300_vendor_config_t vendor = {};
  vendor.flags.use_qspi_interface = 1;
  vendor.init_cmds = co5300_vendor_init;
  vendor.init_cmds_size = co5300_vendor_init_size;

  esp_lcd_panel_dev_config_t panelCfg = {};
  panelCfg.reset_gpio_num = LCD_PIN_RST;
  panelCfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  // The SPI panel drivers ignore this field; flushCb does the real swap.
  // It says BIG because that is the order the panel receives.
  panelCfg.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
  panelCfg.bits_per_pixel = 16;
  panelCfg.flags.reset_active_high = 0;
  panelCfg.vendor_config = &vendor;

  err = esp_lcd_new_panel_co5300(io, &panelCfg, &g_panel);
  if (err != ESP_OK) {
    Serial.printf("[lcd] panel create failed: %s\n", esp_err_to_name(err));
    return false;
  }

  esp_lcd_panel_reset(g_panel);
  esp_lcd_panel_init(g_panel);
  // Visible glass starts at controller column 6.
  esp_lcd_panel_set_gap(g_panel, LCD_GAP_X, LCD_GAP_Y);
  esp_lcd_panel_disp_on_off(g_panel, true);
  Serial.printf("[lcd] CO5300 init (%dx%d, gap %d, %d MHz QSPI)\n", LCD_H_RES,
                LCD_V_RES, LCD_GAP_X, LCD_QSPI_HZ / 1000000);
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
  // and ~45 KB of RAM; it removes that overlap.
  alignas(LV_DRAW_BUF_ALIGN) static lv_color_t buf1[LCD_H_RES * kDrawLines];

  lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(g_disp, flushCb);
  lv_display_set_buffers(g_disp, buf1, nullptr, sizeof(buf1),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_default(g_disp);
  lv_display_add_event_cb(g_disp, onRenderReady, LV_EVENT_RENDER_READY, nullptr);
  lv_display_add_event_cb(g_disp, rounderCb, LV_EVENT_INVALIDATE_AREA, nullptr);
  return true;
}

}  // namespace

bool begin() {
  if (g_ready) return true;

  Serial.println("[lcd] bringing up the 1.75\" CO5300 AMOLED");

  // One I2C bus for the expander, PMIC, RTC, IMU and touch. Nothing else may
  // call Wire.begin() afterwards with different pins.
  Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_HZ);

  // Emission at zero while the panel is configured, so the first frames are
  // not visible as noise. main() raises it after the first frame is queued.
  if (!initBusAndPanel()) return false;
  esp_lcd_panel_co5300_set_brightness(g_panel, 0);

  pinMode(LCD_PIN_TE, INPUT_PULLUP);
  teIsPulsing();
  if (!initLvgl()) return false;

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
  esp_lcd_panel_co5300_set_brightness(g_panel, percent);
}

void applyDimmer(int term58s) {
  // -1 = no Cluster_2 frame heard; hold the current brightness rather than
  // jumping to day when the relay is briefly quiet.
  if (term58s < 0) return;

  int pct;
  if (term58s == 0) {
    pct = LCD_BRIGHTNESS_DAY_PCT;
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
