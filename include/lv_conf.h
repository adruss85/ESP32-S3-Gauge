/**
 * @file lv_conf.h
 * LVGL v9 configuration for the ESP32-S3-Gauge.
 *
 * Partial on purpose: lv_conf_internal.h fills in every symbol that is not
 * listed here. Only the settings that differ from stock are written down.
 *
 * Selected by -DLV_CONF_INCLUDE_SIMPLE in platformio.ini.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOUR / RENDERING
 *====================*/
#define LV_COLOR_DEPTH 16
#define LV_DRAW_BUF_STRIDE_ALIGN 4

/* RGB565 is the panel format. A8 is the glyph mask path; the splash is the
   only RGB565 image. The other source formats can stay off. */
#define LV_DRAW_SW_SUPPORT_RGB565 1
#define LV_DRAW_SW_SUPPORT_RGB565A8 1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_DRAW_SW_SUPPORT_ARGB8888 1
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 1
#define LV_DRAW_SW_SUPPORT_A8 1
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_I1 0

/*====================
   MEMORY
 *====================*/
/* The gauge has only a handful of objects. Keeping LVGL's pool in PSRAM
   leaves internal RAM for WiFi/ESP-NOW and the SPI draw buffers. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#if defined(GAUGE_CAPTURE) && GAUGE_CAPTURE
/* Snapshot capture needs a full 360x360 RGB565 image in the pool. */
#define LV_MEM_SIZE (512U * 1024U)
#else
#define LV_MEM_SIZE (128U * 1024U)
#endif
#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

/* An LVGL assert prints file/line and reboots instead of hanging silently.
   src/app/lv_assert_glue.cpp implements the handler. */
#ifndef __ASSEMBLER__
void ohx_lv_assert_handler(const char *file, int line);
#endif
#define LV_ASSERT_HANDLER ohx_lv_assert_handler(__FILE__, __LINE__);

/*====================
   HAL / TICK
 *====================*/
#define LV_USE_OS LV_OS_NONE

/*====================
   LOGGING
 *====================*/
#define LV_USE_LOG 0

/*====================
   FONTS
 *====================*/
/* The big boost value is the 72 px Montserrat Bold subset generated from
   OpenHaldex-S3-Dash's face (digits and the decimal point). The labels and
   the IAT value use LVGL's stock Montserrat faces, same typeface. */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_48 0

#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* The 72 px face is RLE-compressed by lv_font_conv. Without this the
   glyph bitmap lookup returns NULL and text drawing faults. */
#define LV_USE_FONT_COMPRESSED 1

/* Framebuffer capture over USB for layout verification. */
#define LV_USE_SNAPSHOT 1

/*====================
   WIDGETS
 *====================*/
#define LV_USE_LABEL 1
#define LV_USE_ARC 1   /* the 270 degree boost scale */

#define LV_USE_ANIMIMG 0
#define LV_USE_BAR 1   /* the splash loading rule */
#define LV_USE_BUTTON 0 /* not drawn; its slot pays for the splash image */
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 1   /* the G-meter dot, tail and readout are one canvas */
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGE 1   /* boot splash only; no images on the gauge screens */
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LINE 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 0
#define LV_USE_TABVIEW 0
#define LV_USE_TABLE 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

/* The value row is a flex container of [integer][dot][fraction], which is
   what keeps the decimal dot centred as the width changes. */
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/*====================
   TIMING
 *====================*/
#define LV_DEF_REFR_PERIOD 16

#endif /* LV_CONF_H */
