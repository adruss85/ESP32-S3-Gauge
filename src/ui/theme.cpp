// =====================================================================
// theme.cpp - the seven runtime palettes
//
// The accent families are copied from OpenHaldex-S3-Dash's theme table
// (src/ui/theme.cpp there), values and order unchanged, so the gauge and the
// dashboard on the same car read the same. That table's own notes explain
// the shape and are worth keeping in view:
//
//   * the single-colour schemes are one hue in three steps - accent (base),
//     warn (lighter), crit (brightest);
//   * their text is the hue itself, not lifted towards white, with a
//     darkened version for captions and units, so the whole face carries the
//     scheme instead of a neutral grey;
//   * White keeps near-white text, because that is what the scheme is called;
//   * Midnight keeps red as its critical colour and white readings.
//
// The gauge draws only the arc, the ticks, the captions, the value and the
// IAT line, so it uses fg / dim / accent / line / track. The remaining roles
// are carried so this table can be re-copied whole rather than merged.
// =====================================================================
#include "theme.h"

namespace theme {
namespace {

// Each entry names the scheme it *is*, not just its position: the dash's
// Red scheme was once added in the wrong slot and the table silently
// disagreed with the enum. The static_assert below turns that into a build
// error here too.
struct Definition {
  Scheme id;
  const char *name;
  Palette palette;
};

constexpr Definition kSchemes[kSchemeCount] = {
    // dash.html's :root, ported. The default, and what every build before
    // the AMOLED move drew.
    {Scheme::Midnight, "Midnight",
     {.accent = 0x22c55e,
      .accentDim = 0x15803d,
      .accentDimmer = 0x072a14,
      .warn = 0xf59e0b,
      .crit = 0xef4444,
      .critText = 0xffffff,
      .cyan = 0x22d3ee,
      .shift = 0x8b5cf6}},

    // Orange - amber base, brightest crit is yellow. Black on the plate:
    // white on amber is the one pairing the dash's panel cannot hold.
    {Scheme::Orange, "Orange",
     {.fg = 0xffc247,
      .dim = 0xb97400,
      .accent = 0xf59e0b,
      .accentDim = 0xb45309,
      .accentDimmer = 0x2a1a05,
      .warn = 0xfbbf24,
      .crit = 0xfde047,
      .critText = 0x000000,
      .accentText = 0x000000,
      .cyan = 0x22d3ee,
      .shift = 0xa78bfa}},

    // Green - Midnight's own hue, but stepped instead of going to amber/red.
    {Scheme::Green, "Green",
     {.fg = 0x3ee87b,
      .dim = 0x1f9e57,
      .accent = 0x22c55e,
      .accentDim = 0x15803d,
      .accentDimmer = 0x072a14,
      .warn = 0x4ade80,
      .crit = 0x86efac,
      .critText = 0x000000,
      .accentText = 0x000000,
      .cyan = 0x22d3ee,
      .shift = 0xa78bfa}},

    {Scheme::Blue, "Blue",
     {.fg = 0x5fa8ff,
      .dim = 0x2f6fbf,
      .accent = 0x3b82f6,
      .accentDim = 0x1d4ed8,
      .accentDimmer = 0x0b1a33,
      .warn = 0x60a5fa,
      .crit = 0x93c5fd,
      .critText = 0x000000,
      .accentText = 0xffffff,
      .cyan = 0x22d3ee,
      .shift = 0xf472b6}},

    {Scheme::Purple, "Purple",
     {.fg = 0xc98dff,
      .dim = 0x8b46d6,
      .accent = 0xa855f7,
      .accentDim = 0x7e22ce,
      .accentDimmer = 0x1f0a33,
      .warn = 0xc084fc,
      .crit = 0xd8b4fe,
      .critText = 0x000000,
      .accentText = 0xffffff,
      .cyan = 0xe879f9,
      .shift = 0xf472b6}},

    // White - dark grey, light grey, white: a ramp, not one flat accent.
    {Scheme::White, "White",
     {.fg = 0xeef2f6,
      .dim = 0x9aa3ad,
      .accent = 0x7c8894,
      .accentDim = 0x4b555f,
      .accentDimmer = 0x1a1e22,
      .warn = 0xc6ced6,
      .crit = 0xffffff,
      .critText = 0x000000,
      .accentText = 0x000000,
      .cyan = 0x8ab4d8,
      .shift = 0xf59e0b}},

    // Red - listed last because a stored scheme is a byte in NVS: the
    // numbers 0..6 are a wire format and may not move.
    {Scheme::Red, "Red",
     {.fg = 0xff5f5f,
      .dim = 0xb03333,
      .accent = 0xef4444,
      .accentDim = 0xb91c1c,
      .accentDimmer = 0x2a0b0b,
      .warn = 0xf87171,
      .crit = 0xfea3a3,
      .critText = 0x000000,
      .accentText = 0xffffff,
      .cyan = 0xfda4af,
      .shift = 0xc084fc}},
};

constexpr bool tableOrderOk() {
  for (int i = 0; i < kSchemeCount; i++) {
    if ((uint8_t)kSchemes[i].id != (uint8_t)i) return false;
  }
  return true;
}
static_assert(tableOrderOk(), "kSchemes order must match theme::Scheme");
static_assert(sizeof(kSchemes) / sizeof(kSchemes[0]) == kSchemeCount,
              "palette table and Scheme::Count disagree");

Scheme g_scheme = Scheme::Midnight;

}  // namespace

Scheme scheme() { return g_scheme; }

bool setScheme(Scheme s) {
  if ((uint8_t)s >= kSchemeCount) return false;
  g_scheme = s;
  return true;
}

void nextScheme() {
  const uint8_t next = ((uint8_t)g_scheme + 1) % kSchemeCount;
  g_scheme = (Scheme)next;
}

const Palette &palette() { return kSchemes[(uint8_t)g_scheme].palette; }

const char *schemeName(Scheme s) {
  return kSchemes[(uint8_t)s < kSchemeCount ? (uint8_t)s : 0].name;
}

const char *schemeName() { return schemeName(g_scheme); }

lv_color_t accent() { return lv_color_hex(palette().accent); }
lv_color_t accentDim() { return lv_color_hex(palette().accentDim); }
lv_color_t fg() { return lv_color_hex(palette().fg); }
lv_color_t dim() { return lv_color_hex(palette().dim); }
lv_color_t line() { return lv_color_hex(palette().line); }
lv_color_t track() { return lv_color_hex(palette().track); }
lv_color_t white() { return lv_color_hex(palette().white); }

}  // namespace theme
