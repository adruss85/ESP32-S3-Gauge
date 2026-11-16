// =====================================================================
// theme.cpp - the six runtime colour schemes
//
// Only the accent changes. Background, track, rule and value stay fixed
// so the gauge keeps its contrast in every scheme.
// =====================================================================
#include "theme.h"

namespace theme {
namespace {

// Saturated enough to read on the black panel, but not neon.
constexpr uint32_t kAccent[] = {
    0x22c55e,  // green
    0xf59e0b,  // orange
    0xef4444,  // red
    0x3b82f6,  // blue
    0xf2f5f8,  // white
    0xa855f7,  // purple
};

constexpr const char *kName[] = {
    "green", "orange", "red", "blue", "white", "purple",
};

static_assert(sizeof(kAccent) / sizeof(kAccent[0]) ==
                  (size_t)Scheme::Count,
              "accent table and Scheme::Count disagree");
static_assert(sizeof(kName) / sizeof(kName[0]) == (size_t)Scheme::Count,
              "name table and Scheme::Count disagree");

Scheme g_scheme = Scheme::Green;

}  // namespace

Scheme scheme() { return g_scheme; }

void setScheme(Scheme s) {
  if ((uint8_t)s >= (uint8_t)Scheme::Count) s = Scheme::Green;
  g_scheme = s;
}

void nextScheme() {
  const uint8_t next = ((uint8_t)g_scheme + 1) % (uint8_t)Scheme::Count;
  g_scheme = (Scheme)next;
}

lv_color_t accent() { return lv_color_hex(kAccent[(uint8_t)g_scheme]); }

const char *schemeName() { return kName[(uint8_t)g_scheme]; }

}  // namespace theme
