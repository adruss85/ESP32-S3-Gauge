// esp_heap_caps shim for the host screen renderer.
//
// include/lv_conf.h points LVGL's pool allocator at heap_caps_malloc so the
// pool sits in PSRAM on the board, and the G-meter canvas is allocated the
// same way. On the host there is one heap and no capability flags, so the
// flags are ignored and the allocator is plain malloc/posix_memalign.
//
// The point of the shim is that the renderer compiles include/lv_conf.h
// verbatim: a second LVGL configuration for the host would drift from the
// firmware's, and a render of the wrong configuration is worse than none.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0

static inline void *heap_caps_malloc(size_t size, uint32_t caps) {
  (void)caps;
  return malloc(size);
}

static inline void *heap_caps_aligned_alloc(size_t align, size_t size,
                                            uint32_t caps) {
  (void)caps;
  void *p = NULL;
  if (align < sizeof(void *)) align = sizeof(void *);
  if (posix_memalign(&p, align, size) != 0) return NULL;
  return p;
}
