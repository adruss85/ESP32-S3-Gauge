// =====================================================================
// drive_ui.cpp - the Haldex face
//
// Mode, lock request, lock actual. That is the whole screen: the drivetrain
// drawing it replaced (chassis, driveshafts, wheel treads, diff dots) could
// not be read on the glass in a car, and what the driver was actually
// looking for were these three numbers.
//
// The vocabulary matches OpenHaldex-S3-Dash's HALDEX panel - the same six
// mode names, the coupling state as the line under the mode, the request in
// the scheme's text colour and the actual engagement in cyan - so the two
// screens on the same car cannot disagree about what a word means.
//
// A tap here asks the controller for the next mode, which is the only thing
// this firmware transmits. The request goes out over ESP-NOW, and the only
// verdict is the controller's own mode byte in its next status broadcasts:
// the request worked when that byte becomes the mode that was asked for.
// The ack fields the controller also sends are deliberately not read - in
// use they disagreed with the flag, and the flag is what the driver reads.
// No reply, no key, wrong key and the rest are console detail, not words on
// the glass: the glass has exactly three answers, sending, ok and the two
// failures the driver can act on (NO CHANGE, NO CONNECTION).
// =====================================================================
#include "ui/drive_ui.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "app/settings.h"
#include "net/ohx_now.h"
#include "theme.h"

namespace drive_ui {
namespace {

constexpr int kW = 466;
constexpr int kH = 466;

// The mode names and their order are the controller's own (OHX_C_MODE arg0,
// openhaldex_mode_t). S3-Dash draws the same six strings.
constexpr uint8_t kModeCount = 6;
const char *const kModeNames[kModeCount] = {"STOCK", "FWD",     "50:50",
                                           "60:40", "75:25", "EXPERT"};

constexpr uint32_t kConfirmMs = 1500;     // wait for the mode flag to move
constexpr uint32_t kResultHoldMs = 5000;  // how long the verdict stays up

// Absolute layout for the 466 px round glass. Everything is a plain label:
// there is no canvas and nothing animates, so a value change invalidates one
// small label and nothing else.
//
// The value boxes sit above their captions because the 48 px face has a deeper
// baseline than the 20 px one: montserrat_48 is 52/9 and montserrat_20 is 22/4
// (line height / baseline from the bottom), so the value box goes 25 px above
// the caption box to put the number and "LOCK REQ" on one line.
constexpr int kCaptionY = 74;
constexpr int kModeY = 100;
constexpr int kSubY = 164;
constexpr int kRuleY = 190;
constexpr int kReqValueY = 202;
constexpr int kReqLabelY = 227;
constexpr int kActValueY = 280;
constexpr int kActLabelY = 305;
constexpr int kHintY = 384;

lv_obj_t *g_root = nullptr;
lv_obj_t *g_caption = nullptr;
lv_obj_t *g_mode = nullptr;
lv_obj_t *g_sub = nullptr;
lv_obj_t *g_rule = nullptr;
lv_obj_t *g_reqCap = nullptr;
lv_obj_t *g_reqVal = nullptr;
lv_obj_t *g_actCap = nullptr;
lv_obj_t *g_actVal = nullptr;
lv_obj_t *g_hint = nullptr;

uint8_t g_masterOpa = 255;
bool g_ready = false;
int g_slideX = 0;

// What the last update() was told, so a tap steps from the mode on the glass
// rather than from a fresh read of the radio.
int g_modeShown = -1;
bool g_standalone = false;

// The verdict is the controller's mode byte, so the state is only what the
// glass needs: one request in flight, and the last outcome. `sentPackets` is
// the status counter at the send, which is how a status that could carry the
// verdict is told from one that predates it (and how a tap knows the nonce is
// fresh again).
enum class Outcome : uint8_t { None, Ok, NoChange, NoConnection };

struct Cmd {
  bool pending = false;
  uint8_t want = 0;
  uint32_t sentMs = 0;
  uint32_t sentPackets = 0;
  Outcome outcome = Outcome::None;
  uint32_t outcomeMs = 0;
};
Cmd g_cmd;

// Text caches. LVGL invalidates a label when its text is set, and this face is
// refreshed at 20 Hz, so a steady reading must cost no repaint.
char g_lastMode[12] = "";
char g_lastReq[8] = "";
char g_lastAct[8] = "";
char g_lastSub[28] = "";
uint32_t g_reqCol = 0;
uint32_t g_actCol = 0;
uint32_t g_lastSubCol = 0;

lv_color_t col_cyan() { return lv_color_hex(0x22D3EE); }
lv_color_t col_amber() { return lv_color_hex(0xF59E0B); }
lv_color_t col_green() { return lv_color_hex(0x22C55E); }

void plain(lv_obj_t *obj) {
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour,
                    const char *text, int letterSpace) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, colour, 0);
  if (letterSpace != 0) lv_obj_set_style_text_letter_space(l, letterSpace, 0);
  return l;
}

// Set the text only when it actually changed; returns true if it did.
bool setCached(lv_obj_t *obj, char *cache, size_t len, const char *text) {
  if (strncmp(cache, text, len) == 0) return false;
  strncpy(cache, text, len - 1);
  cache[len - 1] = '\0';
  lv_label_set_text(obj, text);
  return true;
}

void setColour(lv_obj_t *obj, lv_color_t c, uint32_t &cache) {
  const uint32_t u = lv_color_to_u32(c);
  if (u == cache) return;
  cache = u;
  lv_obj_set_style_text_color(obj, c, 0);
}

const char *outcomeText(Outcome o) {
  switch (o) {
    case Outcome::Ok: return "ok";
    case Outcome::NoChange: return "NO CHANGE";
    default: return "NO CONNECTION";
  }
}

lv_color_t outcomeColour(Outcome o) {
  if (o == Outcome::Ok) return col_green();
  if (o == Outcome::NoChange) return col_amber();
  return COL_DIM;
}

// The line under the mode: the controller's answer for a few seconds after a
// request lands, otherwise what the coupling is doing.
void subLine(const char *text) {
  setCached(g_sub, g_lastSub, sizeof(g_lastSub), text);
}

void subColour(lv_color_t c) { setColour(g_sub, c, g_lastSubCol); }

void applyScheme() {
  if (!g_ready) return;

  lv_obj_set_style_text_color(g_caption, theme::dim(), 0);
  lv_obj_set_style_text_color(g_reqCap, theme::dim(), 0);
  lv_obj_set_style_text_color(g_actCap, theme::dim(), 0);
  lv_obj_set_style_text_color(g_hint, theme::dim(), 0);
  lv_obj_set_style_bg_color(g_rule, theme::line(), 0);

  // The three values carry a state colour as well as a scheme colour, so they
  // are repainted here from the state the last update() left behind, and the
  // caches are dropped so the next pass can set them again.
  lv_obj_set_style_text_color(
      g_mode, g_modeShown >= 0 ? theme::accent() : theme::dim(), 0);
  lv_obj_set_style_text_color(g_reqVal, theme::fg(), 0);
  lv_obj_set_style_text_color(g_actVal, col_cyan(), 0);
  g_reqCol = 0;
  g_actCol = 0;
  g_lastSubCol = 0;
  g_lastSub[0] = '\0';
}

void applyOpacity() {
  if (!g_ready) return;
  lv_obj_t *wid[] = {g_caption, g_mode, g_sub, g_rule, g_reqCap,
                     g_reqVal,   g_actCap, g_actVal, g_hint};
  for (lv_obj_t *o : wid) {
    if (o == nullptr) continue;
    if (o == g_rule) lv_obj_set_style_bg_opa(o, g_masterOpa, 0);
    else lv_obj_set_style_text_opa(o, g_masterOpa, 0);
  }
}

// Has the mode flag moved? A fresh status is also what refills the nonce, so
// it doubles as the signal that another tap may send. The window only decides
// when to stop waiting: the flag is the verdict, and a status that arrives
// without it is NO CHANGE rather than a failure of the link.
void resolveCommand(const ohxnow::Telemetry &t, const ohxnow::Stats &s,
                    uint32_t now) {
  if (!g_cmd.pending) return;

  const bool live = s.raw > 0 && t.statusValid;
  const bool fresh = s.statusPackets != g_cmd.sentPackets;
  const uint32_t age = (uint32_t)(now - g_cmd.sentMs);

  if (live && fresh && t.mode == g_cmd.want) {
    g_cmd.pending = false;
    g_cmd.outcome = Outcome::Ok;
    g_cmd.outcomeMs = now;
    Serial.printf("[haldex] mode %s: ok (mode flag after %lu ms)\n",
                  kModeNames[g_cmd.want], (unsigned long)age);
    return;
  }

  // Nothing is being broadcast, so there is no verdict to wait for.
  if (!live) {
    g_cmd.pending = false;
    g_cmd.outcome = Outcome::NoConnection;
    g_cmd.outcomeMs = now;
    Serial.printf("[haldex] mode %s: no connection\n", kModeNames[g_cmd.want]);
    return;
  }

  if (age > kConfirmMs) {
    g_cmd.pending = false;
    g_cmd.outcome = fresh ? Outcome::NoChange : Outcome::NoConnection;
    g_cmd.outcomeMs = now;
    if (g_cmd.outcome == Outcome::NoChange) {
      const char *still = t.mode < kModeCount ? kModeNames[t.mode] : "--";
      Serial.printf("[haldex] mode %s: not confirmed, flag still %s\n",
                    kModeNames[g_cmd.want], still);
    } else {
      Serial.printf("[haldex] mode %s: no connection\n", kModeNames[g_cmd.want]);
    }
  }
}

}  // namespace

void begin() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  g_root = lv_obj_create(scr);
  lv_obj_set_size(g_root, kW, kH);
  lv_obj_set_pos(g_root, 0, 0);
  plain(g_root);

  g_caption = makeLabel(g_root, FONT_TINY, theme::dim(), "HALDEX", 4);
  lv_obj_align(g_caption, LV_ALIGN_TOP_MID, 0, kCaptionY);

  g_mode = makeLabel(g_root, FONT_G, theme::accent(), "--", 0);
  lv_obj_align(g_mode, LV_ALIGN_TOP_MID, 0, kModeY);

  g_sub = makeLabel(g_root, FONT_TINY, theme::dim(), "", 0);
  lv_obj_align(g_sub, LV_ALIGN_TOP_MID, 0, kSubY);

  g_rule = lv_obj_create(g_root);
  lv_obj_set_size(g_rule, 140, 2);
  lv_obj_align(g_rule, LV_ALIGN_TOP_MID, 0, kRuleY);
  lv_obj_set_style_bg_color(g_rule, theme::line(), 0);
  lv_obj_set_style_bg_opa(g_rule, LV_OPA_COVER, 0);
  plain(g_rule);

  g_reqCap = makeLabel(g_root, FONT_SMALL, theme::dim(), "LOCK REQ", 2);
  lv_obj_set_pos(g_reqCap, 96, kReqLabelY);
  g_reqVal = makeLabel(g_root, FONT_G, theme::fg(), "--", 0);
  lv_obj_align(g_reqVal, LV_ALIGN_TOP_RIGHT, -96, kReqValueY);

  g_actCap = makeLabel(g_root, FONT_SMALL, theme::dim(), "LOCK ACT", 2);
  lv_obj_set_pos(g_actCap, 96, kActLabelY);
  g_actVal = makeLabel(g_root, FONT_G, col_cyan(), "--", 0);
  lv_obj_align(g_actVal, LV_ALIGN_TOP_RIGHT, -96, kActValueY);

  g_hint = makeLabel(g_root, FONT_TINY, theme::dim(), "TAP = NEXT MODE", 2);
  lv_obj_align(g_hint, LV_ALIGN_TOP_MID, 0, kHintY);

  g_ready = true;
  applyScheme();
  applyOpacity();
}

void update(const ohxnow::Telemetry &t, const ohxnow::Stats &s) {
  if (!g_ready) return;

  const uint32_t now = millis();
  const bool live = s.raw > 0 && t.statusValid;

  resolveCommand(t, s, now);

  g_standalone = t.standalone;
  g_modeShown = (live && t.mode < kModeCount) ? (int)t.mode : -1;

  // ---- mode ---------------------------------------------------------
  const char *mode = g_modeShown >= 0 ? kModeNames[g_modeShown] : "--";
  if (setCached(g_mode, g_lastMode, sizeof(g_lastMode), mode)) {
    lv_obj_set_style_text_color(
        g_mode, g_modeShown >= 0 ? theme::accent() : theme::dim(), 0);
  }

  // ---- lock request and actual --------------------------------------
  // Unknown is "--", never 0: a Haldex with no CAN data reports lockActual as
  // OHX_NONE8, and a request of 0 is a real request that must still read 0%.
  char req[8];
  char act[8];
  if (live && t.lockTargetValid) snprintf(req, sizeof(req), "%u%%", (unsigned)t.lockTarget);
  else snprintf(req, sizeof(req), "--");
  if (live && t.lockActualValid) snprintf(act, sizeof(act), "%u%%", (unsigned)t.lockActual);
  else snprintf(act, sizeof(act), "--");

  if (setCached(g_reqVal, g_lastReq, sizeof(g_lastReq), req)) g_reqCol = 0;
  if (setCached(g_actVal, g_lastAct, sizeof(g_lastAct), act)) g_actCol = 0;
  // setColour() is a comparison as well as a setter, so an unchanged value
  // and an unchanged scheme both cost nothing.
  setColour(g_reqVal, live ? theme::fg() : theme::dim(), g_reqCol);
  setColour(g_actVal, (live && t.lockActualValid) ? col_cyan() : theme::dim(),
            g_actCol);

  // ---- the line under the mode --------------------------------------
  if (g_cmd.pending) {
    char buf[28];
    snprintf(buf, sizeof(buf), "%s: sending", kModeNames[g_cmd.want]);
    subLine(buf);
    subColour(col_amber());
  } else if (g_cmd.outcome != Outcome::None &&
             (uint32_t)(now - g_cmd.outcomeMs) <= kResultHoldMs) {
    char buf[28];
    if (g_cmd.outcome == Outcome::NoConnection) {
      snprintf(buf, sizeof(buf), "NO CONNECTION");
    } else {
      snprintf(buf, sizeof(buf), "%s: %s", kModeNames[g_cmd.want],
               outcomeText(g_cmd.outcome));
    }
    subLine(buf);
    subColour(outcomeColour(g_cmd.outcome));
  } else if (!live) {
    subLine("NO CONNECTION");
    subColour(theme::dim());
  } else if (!t.ctrlAccept) {
    subLine("CONTROL OFF");
    subColour(col_amber());
  } else if (!t.haldexCan) {
    subLine("HALDEX CAN --");
    subColour(theme::dim());
  } else if (t.tempProt) {
    subLine("TEMP PROTECTION");
    subColour(col_amber());
  } else if (t.couplingOpen) {
    subLine("COUPLING OPEN");
    subColour(theme::dim());
  } else {
    subLine("COUPLING CLOSED");
    subColour(theme::dim());
  }
}

void onTap() {
  if (!g_ready) return;

  const uint32_t now = millis();
  const ohxnow::Stats s = ohxnow::stats();

  // The controller regenerates its nonce after it accepts a command, so a
  // second tap before the next status would sign against a spent one. One
  // request in flight is the whole guard; in practice it clears in ~100 ms.
  if (g_cmd.pending && s.statusPackets == g_cmd.sentPackets) {
    Serial.println("[haldex] a request is already in flight");
    return;
  }

  // Step from what the glass shows, or from the request in flight if the last
  // one has not been confirmed yet, so three quick taps move three modes.
  const int base = g_cmd.pending ? g_cmd.want : (g_modeShown >= 0 ? g_modeShown : 0);
  uint8_t want = (uint8_t)((base + 1) % kModeCount);
  // A standalone controller has no STOCK to fall back on: it answers a request
  // for STOCK by staying where it is and still reports ok, so step over it.
  if (want == 0 && g_standalone) want = 1;

  g_cmd.want = want;
  g_cmd.pending = true;
  g_cmd.outcome = Outcome::None;
  g_cmd.sentMs = now;
  // Taken before the send, so the status that answers it counts as new.
  g_cmd.sentPackets = s.statusPackets;

  const ohxnow::SendResult r = ohxnow::sendMode(want);
  if (r == ohxnow::SendResult::Ok) {
    Serial.printf("[haldex] mode -> %s (sent)\n", kModeNames[want]);
    return;
  }

  // Nothing reached the air. The controller is still broadcasting, so the
  // observable outcome is that the mode does not change; the reason is
  // console detail, not a word the driver has to decode.
  g_cmd.pending = false;
  g_cmd.outcome = (r == ohxnow::SendResult::NoStatus) ? Outcome::NoConnection
                                                      : Outcome::NoChange;
  g_cmd.outcomeMs = now;
  switch (r) {
    case ohxnow::SendResult::NoKey:
      Serial.printf("[haldex] mode %s: controller needs a key, none stored\n",
                    kModeNames[want]);
      break;
    case ohxnow::SendResult::NoStatus:
      Serial.printf("[haldex] mode %s: no fresh OHX_STATUS to send to\n",
                    kModeNames[want]);
      break;
    default:
      Serial.printf("[haldex] mode %s: send failed\n", kModeNames[want]);
      break;
  }
}

void setScheme(theme::Scheme s, bool persist) {
  theme::setScheme(s);
  if (persist) settings::setScheme((uint8_t)theme::scheme());
  applyScheme();
}

void setVisible(bool visible) {
  if (!g_ready) return;
  if (visible) lv_obj_remove_flag(g_root, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(g_root, LV_OBJ_FLAG_HIDDEN);
}

void setOpacity(uint8_t opacity) {
  if (!g_ready || opacity == g_masterOpa) return;
  g_masterOpa = opacity;
  applyOpacity();
}

void setSlide(int dx) {
  if (!g_ready || dx == g_slideX) return;
  g_slideX = dx;
  lv_obj_set_pos(g_root, dx, 0);
}

int slide() { return g_slideX; }

lv_obj_t *root() { return g_root; }

}  // namespace drive_ui
