// =====================================================================
// splash_img.cpp - decoding the splash picture with the mask ROM
//
// The S3 has no JPEG peripheral (the esp_driver_jpeg path is gated on
// CONFIG_SOC_JPEG_CODEC_SUPPORTED, which the S3 does not set), but the mask
// ROM ships TJpgDec. esp32s3.rom.ld exports jd_prepare at 0x40000858 and
// jd_decomp at 0x40000864, so the decoder is free; this file is only the
// two callbacks it drives.
//
// The ROM's TJpgDec uses RGB888 output and hands the picture over one MCU
// band at a time. Each band is converted to RGB565 as it arrives, so a
// full-size RGB888 image is never materialised. The whole job is roughly a
// fifth of a second of CPU on the S3, paid once while the panel would
// otherwise be blank.
// =====================================================================
#include "ui/splash_img.h"

#include <Arduino.h>

#include <esp_heap_caps.h>

#include "esp32s3/rom/tjpgd.h"

// board_build.embed_files in platformio.ini puts the JPEG in flash and
// defines these. The linker drops the data again unless something refers to
// it, which is why the aliases live here and nowhere else.
extern const uint8_t splash_jpg_start[]
    asm("_binary_assets_canvas_gauge_splash_466_base_jpg_start");
extern const uint8_t splash_jpg_end[]
    asm("_binary_assets_canvas_gauge_splash_466_base_jpg_end");

namespace splashimg {
namespace {

constexpr int kWidth = 466;
constexpr int kHeight = 466;
constexpr size_t kImageBytes = (size_t)kWidth * (size_t)kHeight * 2;

// TJpgDec's pool: Huffman and quantisation tables plus the IDCT / colour
// conversion work buffer. It is internal RAM because the IDCT hits it hard,
// and it is freed the moment the decode returns.
constexpr size_t kWorkPoolBytes = 32 * 1024;

uint16_t *g_pixels = nullptr;
lv_image_dsc_t g_dsc = {};

// The file is memory-mapped flash, so the input callback is a cursor.
struct Reader {
  const uint8_t *p;
  const uint8_t *end;
};

UINT readFromFlash(JDEC *jd, BYTE *buff, UINT nbytes) {
  Reader *r = (Reader *)jd->device;
  const size_t left = (size_t)(r->end - r->p);
  const UINT take = ((size_t)nbytes < left) ? nbytes : (UINT)left;
  if (buff != nullptr && take > 0) memcpy(buff, r->p, take);
  r->p += take;
  return take;  // fewer than asked for means end of file
}

UINT writeBand(JDEC *, void *band, JRECT *rect) {
  const uint8_t *src = (const uint8_t *)band;  // RGB888, one band
  for (int y = (int)rect->top; y <= (int)rect->bottom; y++) {
    uint16_t *dst = g_pixels + (size_t)y * (size_t)kWidth + (size_t)rect->left;
    for (int x = (int)rect->left; x <= (int)rect->right; x++) {
      const uint8_t r = src[0];
      const uint8_t g = src[1];
      const uint8_t b = src[2];
      src += 3;
      *dst++ = (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
    }
  }
  return 1;  // non-zero: keep going
}

}  // namespace

bool decode() {
  release();

  g_pixels = (uint16_t *)heap_caps_malloc(kImageBytes,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (g_pixels == nullptr) {
    Serial.printf("[splash] no PSRAM for the picture (%u B) - flat field\n",
                  (unsigned)kImageBytes);
    return false;
  }

  void *pool = heap_caps_malloc(kWorkPoolBytes,
                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (pool == nullptr) {
    Serial.println("[splash] no internal RAM for the decoder pool - flat field");
    release();
    return false;
  }

  Reader reader = {splash_jpg_start, splash_jpg_end};
  JDEC jd = {};
  const uint32_t started = millis();

  JRESULT r = jd_prepare(&jd, readFromFlash, pool, (UINT)kWorkPoolBytes,
                         &reader);
  if (r == JDR_OK && (jd.width != (UINT)kWidth || jd.height != (UINT)kHeight)) {
    Serial.printf("[splash] picture is %ux%u, not %dx%d - flat field\n",
                  (unsigned)jd.width, (unsigned)jd.height, kWidth, kHeight);
    r = JDR_PAR;
  }
  if (r == JDR_OK) r = jd_decomp(&jd, writeBand, 0);  // full size, no descale

  heap_caps_free(pool);
  const uint32_t ms = millis() - started;

  if (r != JDR_OK) {
    Serial.printf("[splash] JPEG decode failed (%d) after %lu ms - flat field\n",
                  (int)r, (unsigned long)ms);
    release();
    return false;
  }

  g_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  g_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  g_dsc.header.w = kWidth;
  g_dsc.header.h = kHeight;
  g_dsc.header.stride = kWidth * 2;
  g_dsc.data_size = (uint32_t)kImageBytes;
  g_dsc.data = (const uint8_t *)g_pixels;

  Serial.printf("[splash] picture %dx%d in %lu ms (%u B flash, %u B PSRAM)\n",
                kWidth, kHeight, (unsigned long)ms,
                (unsigned)(splash_jpg_end - splash_jpg_start),
                (unsigned)kImageBytes);
  return true;
}

const lv_image_dsc_t *image() { return (g_pixels != nullptr) ? &g_dsc : nullptr; }

void release() {
  if (g_pixels != nullptr) {
    heap_caps_free(g_pixels);
    g_pixels = nullptr;
  }
  g_dsc = {};
}

}  // namespace splashimg
