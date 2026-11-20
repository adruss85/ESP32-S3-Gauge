// =====================================================================
// gauge_web.cpp - setup AP, settings page, OTA upload
//
// Everything here runs from loop() on the same thread as LVGL (WebServer is
// polled, not an interrupt), so the handlers may touch the gauge widgets
// directly. The two costs to know about:
//
//   * handleClient() processes an upload one 2 KB chunk at a time, but each
//     flash write stalls the CPU for tens of milliseconds, so the arc can
//     hitch for a second or two while an image is being written. Nothing is
//     lost: the panel DMA and the ESP-NOW receive callback are independent.
//   * The page's requests add to the loop's work. The JSON is built once a
//     second when the page is open and not at all when it is closed.
//
// No request can change the wire protocol or start a transmission; the only
// actions are the stored display settings, the G-meter zero and the OTA slot
// that is not running.
// =====================================================================
#include "net/gauge_web.h"

#include <Arduino.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <math.h>
#include <stdlib.h>

#include "app/config.h"
#include "app/gmeter.h"
#include "app/settings.h"
#include "net/ohx_now.h"
#include "version.h"

#ifndef GAUGE_HAS_DISPLAY
#define GAUGE_HAS_DISPLAY 0
#endif

#if GAUGE_HAS_DISPLAY
#include "theme.h"
#include "ui/dashboard.h"
#endif

namespace gauge_web {
namespace {

WebServer g_server(80);

bool g_started = false;

// Packet rate shown on the page, refreshed every 5 s like the console line.
uint32_t g_rateMs = 0;
uint32_t g_ratePackets = 0;
float g_pps = 0.0f;

uint8_t g_clients = 0;
uint32_t g_clientCheckMs = 0;

// OTA state. g_otaOk is what the final handler reports; the restart is
// delayed so the browser gets the response before the radio goes away.
bool g_otaOk = false;
bool g_otaWriting = false;
uint32_t g_otaBytes = 0;
uint32_t g_restartAt = 0;

// ---------------------------------------------------------------------
// Page. Everything dynamic comes from /api/status, so the HTML itself is
// constant and the inputs are not rebuilt under the user's fingers.
// ---------------------------------------------------------------------
const char kPage[] PROGMEM = R"HTML(<!doctype html>
<html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>CANv-gauge</title>
<style>
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0 auto;padding:18px;max-width:520px;background:#0a0c0f;color:#e6ebf1;
font:16px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
h1{font-size:20px;margin:0}
h2{font-size:12px;letter-spacing:.14em;text-transform:uppercase;color:#8b97a5;
margin:24px 0 8px;font-weight:600}
.sub{color:#8b97a5;font-size:13px;margin:4px 0 0}
.card{background:#14181d;border:1px solid #252c35;border-radius:12px;padding:12px}
label{display:flex;align-items:center;gap:10px;padding:6px 0;cursor:pointer}
.dot{width:18px;height:18px;border-radius:50%;border:1px solid #0008;flex:none}
.row{display:flex;flex-wrap:wrap;gap:12px;align-items:center}
input[type=number]{width:92px;padding:9px;border-radius:8px;border:1px solid #333c47;
background:#0a0c0f;color:#e6ebf1;font:inherit}
select{padding:9px;border-radius:8px;border:1px solid #333c47;background:#0a0c0f;
color:#e6ebf1;font:inherit}
button{padding:9px 16px;border:0;border-radius:8px;background:#22c55e;color:#04120a;
font:600 15px system-ui;margin-top:10px;cursor:pointer}
button.b{background:#3b82f6;color:#03101f}
button.v{background:#a855f7;color:#160322}
pre{margin:0;font:12px/1.6 ui-monospace,Menlo,monospace;color:#b9c4d0;white-space:pre-wrap}
input[type=file]{font-size:13px;color:#b9c4d0;max-width:100%}
.ok{color:#22c55e}.bad{color:#ef4444}code{font-size:12px}
</style></head><body>
<h1>CANv-gauge</h1><p class=sub id=sub>reading&hellip;</p>

<h2>Colour scheme</h2>
<div class=card id=schemes></div>

<h2>Scale</h2>
<div class=card><form id=scale class=row>
<label>Min <input id=min type=number step=.1 required> PSI</label>
<label>Max <input id=max type=number step=.1 required> PSI</label>
<button>Apply</button></form>
<p class=sub id=scalemsg></p></div>

<div class=card><form id=os class=row>
<label>Min <input id=omin type=number step=.1 required> °C</label>
<label>Max <input id=omax type=number step=.1 required> °C</label>
<button>Apply</button></form>
<p class=sub id=ocmmsg></p></div>

<h2>Display</h2>
<div class=card>
<div class=row>
<label><input type=radio name=mode value=0> Boost gauge</label>
<label><input type=radio name=mode value=1> Oil temp</label>
<label><input type=radio name=mode value=2> G-meter</label>
</div>
<div class=row style="margin-top:10px">
<label>Mount <select id=mount>
<option value=0>Upright, facing driver</option>
<option value=1>Flat, top forward</option>
<option value=2>Auto (from gravity)</option>
</select></label>
<label><input type=checkbox id=flip> Flip accel/brake</label>
<button class=b id=applyg>Apply</button>
</div>
<button class=v id=zero>Zero G-meter</button>
<p class=sub id=gmsg></p>
</div>

<h2>Firmware</h2>
<div class=card><form id=ota method=post action=/update enctype=multipart/form-data>
<input type=file name=firmware accept=.bin required><br>
<button class=b>Upload and reboot</button></form>
<p class=sub>PlatformIO <code>firmware.bin</code>. The gauge writes the inactive
slot and reboots; the image it is running is never overwritten.</p></div>

<h2>Live</h2><div class=card><pre id=live>&mdash;</pre></div>

<script>
var N=["Midnight","Orange","Green","Blue","Purple","White","Red"],
    C=["#22c55e","#f59e0b","#22c55e","#3b82f6","#a855f7","#7c8894","#ef4444"],
    busy=false,mountTouched=false;
function $(id){return document.getElementById(id)}
function msg(id,ok,text){$(id).innerHTML='<span class='+(ok?"ok":"bad")+'>'+text+'</span>'}
function val(x){return x===null?"--":(x/10).toFixed(1)}
function valg(x){return x===null?"--":(x/100).toFixed(2)}
async function send(url,box){
  busy=true;
  try{
    var r=await fetch(url,{method:"POST"}),j=await r.json();
    msg(box,j.ok,j.ok?"saved":(j.error||"rejected"));
  }catch(e){msg(box,false,"request failed")}
  busy=false;tick();
}
document.addEventListener("DOMContentLoaded",function(){
  N.forEach(function(n,i){
    var l=document.createElement("label");
    l.innerHTML='<input type=radio name=s value='+i+
      '><span class=dot style="background:'+C[i]+'"></span>'+n;
    l.firstChild.onchange=function(){send("/scheme?s="+i,"scalemsg")};
    $("schemes").appendChild(l);
  });
  $("scale").onsubmit=function(e){
    e.preventDefault();
    send("/scale?min="+encodeURIComponent($("min").value)+
         "&max="+encodeURIComponent($("max").value),"scalemsg");
  };
  document.querySelectorAll("input[name=mode]").forEach(function(r){
    r.onchange=function(){send("/mode?m="+r.value,"gmsg")};
  });
  $("mount").onchange=function(){mountTouched=true;};
  $("flip").onchange=function(){mountTouched=true;};
  $("applyg").onclick=function(e){
    e.preventDefault();
    mountTouched=false;
    send("/gmount?m="+$("mount").value+"&flip="+($("flip").checked?1:0),"gmsg");
  };
  $("zero").onclick=function(e){
    e.preventDefault();
    send("/gzero","gmsg");
  };
  tick();
});
async function tick(){
  if(busy)return;
  try{
    var s=await (await fetch("/api/status")).json();
    $("sub").textContent="firmware "+s.fw+" \u00b7 channel "+s.ch+
      (s.rssi?" \u00b7 "+s.rssi+" dBm":"")+" \u00b7 "+s.pps.toFixed(1)+" OHX/s";
    document.querySelectorAll("input[name=s]").forEach(function(r){
      r.checked=+r.value===s.scheme;
    });
    if(document.activeElement!==$("min"))$("min").value=(s.min10/10).toFixed(1);
    if(document.activeElement!==$("max"))$("max").value=(s.max10/10).toFixed(1);
    if(document.activeElement!==$("omin"))$("omin").value=(s.omin10/10).toFixed(1);
    if(document.activeElement!==$("omax"))$("omax").value=(s.omax10/10).toFixed(1);
    document.querySelectorAll("input[name=mode]").forEach(function(r){
      r.checked=+r.value===s.mode;
    });
    if(document.activeElement!==$("mount")&&!mountTouched)$("mount").value=s.mount;
    if(document.activeElement!==$("flip")&&!mountTouched)$("flip").checked=!!s.flip;
    $("live").textContent=
      "boost   "+val(s.boost10)+" psi"+(s.boost10===null?"  (no OHX_STATUS)":"")+
      "\nIAT     "+val(s.iat10)+" \u00b0C"+(s.iat10===null?"  (no Engine_3)":"")+
      "\nOil    "+val(s.oil10)+" \u00b0C"+(s.oil10===null?"  (no Cluster_2)":"")+
      "\nOil scale "+(s.omin10/10).toFixed(1)+".."+(s.omax10/10).toFixed(1)+" \u00b0C"+
      "\ng avg   "+valg(s.g100)+" g"+(s.g100===null?"  (no IMU)":"")+
      "\nG long  "+valg(s.gv100)+" g \u00b7 lat "+valg(s.gl100)+" g"+
      "\nOHX     "+s.ohx+" packets \u00b7 CANN "+s.cann+" packets"+
      "\nchannel "+s.ch+" \u00b7 "+s.clients+" page client(s)"+
      "\nheap    "+(s.heap/1024).toFixed(1)+" KB free";
  }catch(e){$("sub").textContent="gauge unreachable"}
}
setInterval(tick,1000);
</script></body></html>)HTML";

const char kOtaOk[] PROGMEM = R"HTML(<!doctype html><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Update accepted</title>
<body style="font:16px system-ui;background:#0a0c0f;color:#e6ebf1;padding:24px">
<h1 style="font-size:20px">Update accepted</h1>
<p>The gauge is rebooting into the new image.</p>
<p style="color:#8b97a5">Rejoin <b>CANv-gauge</b> and reload this page in a few seconds.</p>
</body>)HTML";

const char kOtaFail[] PROGMEM = R"HTML(<!doctype html><meta charset=utf-8>
<title>Update failed</title>
<body style="font:16px system-ui;background:#0a0c0f;color:#e6ebf1;padding:24px">
<h1 style="font-size:20px;color:#ef4444">Update failed</h1>
<p>Nothing was switched. Check the serial console for the reason and try again.</p>
</body>)HTML";

// ---------------------------------------------------------------------

void sendJson(int code, const String &json) {
  g_server.sendHeader("Cache-Control", "no-store");
  g_server.send(code, "application/json", json);
}

void reject(const char *why) {
  sendJson(400, String("{\"ok\":false,\"error\":\"") + why + "\"}");
}

void handleRoot() {
  g_server.sendHeader("Cache-Control", "no-store");
  g_server.send_P(200, "text/html", kPage);
}

void handleStatus() {
  const ohxnow::Telemetry t = ohxnow::telemetry();
  const ohxnow::Stats s = ohxnow::stats();
  const settings::Values &v = settings::get();

  // Tenths keep the JSON free of NaN, which is not a JSON value. null means
  // "no reading", exactly like "--" on the glass. statusValid matters: a
  // stale boostPsi still holds the last number, which must not be reported
  // as a live reading.
  char boost[8] = "null";
  char iat[8] = "null";
  char oil[8] = "null";
  if (t.statusValid && !isnan(t.boostPsi)) {
    snprintf(boost, sizeof(boost), "%d", (int)lroundf(t.boostPsi * 10.0f));
  }
  if (t.iatValid && !isnan(t.iatC)) {
    snprintf(iat, sizeof(iat), "%d", (int)lroundf(t.iatC * 10.0f));
  }
  if (t.oilTempValid && !isnan(t.oilTempC)) {
    snprintf(oil, sizeof(oil), "%d", (int)lroundf(t.oilTempC * 10.0f));
  }

  // The G-meter reading is already filtered and averaged. Null until a full
  // 1 s window exists, matching the "--" on the glass.
  char gavg[8] = "null";
  char gvert[8] = "null";
  char glat[8] = "null";
  const gmeter::Reading &g = gmeter::reading();
  if (g.valid) {
    snprintf(gavg, sizeof(gavg), "%d", (int)lroundf(g.average * 100.0f));
    snprintf(gvert, sizeof(gvert), "%d", (int)lroundf(g.vertical * 100.0f));
    snprintf(glat, sizeof(glat), "%d", (int)lroundf(g.lateral * 100.0f));
  }

  char buf[416];
  snprintf(buf, sizeof(buf),
           "{\"fw\":\"%s\",\"scheme\":%u,\"min10\":%d,\"max10\":%d,"
           "\"mode\":%u,\"mount\":%u,\"mres\":%u,\"flip\":%u,"
           "\"boost10\":%s,\"iat10\":%s,\"oil10\":%s,\"g100\":%s,\"gv100\":%s,\"gl100\":%s,"
           "\"omin10\":%d,\"omax10\":%d,"
           "\"ch\":%u,\"rssi\":%d,"
           "\"pps\":%.1f,\"ohx\":%lu,\"cann\":%lu,\"clients\":%u,"
           "\"heap\":%lu}",
           config::kVersion, (unsigned)v.scheme, (int)v.min10, (int)v.max10,
           (unsigned)v.mode, (unsigned)v.mount, (unsigned)gmeter::mount(),
           (unsigned)v.gflip, boost, iat, oil, gavg, gvert, glat,
           (int)v.omin10, (int)v.omax10,
           (unsigned)s.channel, (int)s.rssi, (double)g_pps,
           (unsigned long)s.statusPackets, (unsigned long)s.canPackets,
           (unsigned)WiFi.softAPgetStationNum(),
           (unsigned long)ESP.getFreeHeap());
  sendJson(200, buf);
}

void handleOilScale() {
  if (!g_server.hasArg("min") || !g_server.hasArg("max")) {
    reject("missing min or max");
    return;
  }

  const String mn = g_server.arg("min");
  const String mx = g_server.arg("max");
  char *end = nullptr;
  const float fmin = strtof(mn.c_str(), &end);
  if (end == mn.c_str() || *end != '\0') {
    reject("min is not a number");
    return;
  }
  const float fmax = strtof(mx.c_str(), &end);
  if (end == mx.c_str() || *end != '\0') {
    reject("max is not a number");
    return;
  }
  if (!isfinite(fmin) || !isfinite(fmax)) {
    reject("scale is not finite");
    return;
  }

  const int32_t min10 = lroundf(fmin * 10.0f);
  const int32_t max10 = lroundf(fmax * 10.0f);
  // Oil temperature scale: realistic engine-oil band, tenths of a degree C.
  // Accept -60..200 (the offset band plus headroom); the display layer
  // validates the span itself.
  if (min10 < -600 || min10 > 2000 || max10 < -600 || max10 > 2000) {
    reject("oil scale outside -60.0 .. 200.0 °C");
    return;
  }
  if (!settings::setOilScale((int16_t)min10, (int16_t)max10)) {
    reject("min must be below max and the span at least 2.0 °C");
    return;
  }

#if GAUGE_HAS_DISPLAY
  dashboard::applyOilScale((int16_t)min10, (int16_t)max10);
#endif
  Serial.printf("[ui] oil scale %.1f..%.1f °C (web)\n", (double)min10 / 10.0,
                (double)max10 / 10.0);

  char buf[96];
  snprintf(buf, sizeof(buf), "{\"ok\":true,\"min10\":%ld,\"max10\":%ld}",
           (long)min10, (long)max10);
  sendJson(200, buf);
}

void handleScheme() {
#if GAUGE_HAS_DISPLAY
  if (!g_server.hasArg("s")) {
    reject("missing s");
    return;
  }
  // String::toInt() maps anything non-numeric to 0, which would silently
  // select Midnight. Accept exactly one digit.
  const String arg = g_server.arg("s");
  if (arg.length() != 1 || arg[0] < '0' ||
      arg[0] >= '0' + (char)theme::Scheme::Count) {
    reject("scheme must be 0-6");
    return;
  }
  const int n = arg[0] - '0';
  dashboard::setScheme((theme::Scheme)n);  // also persists in NVS
  Serial.printf("[ui] accent %s (web)\n", theme::schemeName());
  sendJson(200, "{\"ok\":true}");
#else
  reject("no display on this build");
#endif
}

void handleScale() {
  if (!g_server.hasArg("min") || !g_server.hasArg("max")) {
    reject("missing min or max");
    return;
  }

  const String mn = g_server.arg("min");
  const String mx = g_server.arg("max");
  char *end = nullptr;
  const float fmin = strtof(mn.c_str(), &end);
  if (end == mn.c_str() || *end != '\0') {
    reject("min is not a number");
    return;
  }
  const float fmax = strtof(mx.c_str(), &end);
  if (end == mx.c_str() || *end != '\0') {
    reject("max is not a number");
    return;
  }
  if (!isfinite(fmin) || !isfinite(fmax)) {
    reject("scale is not finite");
    return;
  }

  const int32_t min10 = lroundf(fmin * 10.0f);
  const int32_t max10 = lroundf(fmax * 10.0f);
  if (min10 < settings::kMinLimit10 || min10 > settings::kMaxLimit10 ||
      max10 < settings::kMinLimit10 || max10 > settings::kMaxLimit10) {
    reject("scale outside -15.0 .. 50.0 PSI");
    return;
  }
  if (!settings::setScale((int16_t)min10, (int16_t)max10)) {
    reject("min must be below max and the span 5 .. 60 PSI");
    return;
  }

#if GAUGE_HAS_DISPLAY
  dashboard::applyScale((int16_t)min10, (int16_t)max10);
#endif
  Serial.printf("[ui] scale %.1f..%.1f PSI (web)\n", (double)min10 / 10.0,
                (double)max10 / 10.0);

  char buf[96];
  snprintf(buf, sizeof(buf), "{\"ok\":true,\"min10\":%ld,\"max10\":%ld}",
           (long)min10, (long)max10);
  sendJson(200, buf);
}

// ---- display mode and G-meter ---------------------------------------
void handleMode() {
#if GAUGE_HAS_DISPLAY
  if (!g_server.hasArg("m")) {
    reject("missing m");
    return;
  }
  const String arg = g_server.arg("m");
  if (arg.length() != 1 || arg[0] < '0' || arg[0] > '2') {
    reject("mode must be 0, 1 or 2");
    return;
  }
  dashboard::setMode((dashboard::Mode)(arg[0] - '0'), true, "web");
  sendJson(200, "{\"ok\":true}");
#else
  reject("no display on this build");
#endif
}

void handleGMount() {
#if GAUGE_HAS_DISPLAY
  if (!g_server.hasArg("m") || !g_server.hasArg("flip")) {
    reject("missing m or flip");
    return;
  }
  const String m = g_server.arg("m");
  const String f = g_server.arg("flip");
  if (m.length() != 1 || m[0] < '0' || m[0] > '2') {
    reject("mount must be 0, 1 or 2");
    return;
  }
  if (f.length() != 1 || f[0] < '0' || f[0] > '1') {
    reject("flip must be 0 or 1");
    return;
  }
  dashboard::setMount((gmeter::Mount)(m[0] - '0'), f[0] == '1');
  sendJson(200, "{\"ok\":true}");
#else
  reject("no display on this build");
#endif
}

void handleGZero() {
#if GAUGE_HAS_DISPLAY
  dashboard::zeroG();
  sendJson(200, "{\"ok\":true}");
#else
  reject("no display on this build");
#endif
}

// ---- OTA -------------------------------------------------------------
void handleOtaUpload() {
  HTTPUpload &up = g_server.upload();

  if (up.status == UPLOAD_FILE_START) {
    g_otaOk = false;
    g_otaWriting = false;
    g_otaBytes = 0;
    Serial.printf("[web] OTA start: \"%s\"\n", up.filename.c_str());
    // UPDATE_SIZE_UNKNOWN: the browser sends a length, but a chunked upload
    // may not, and the partition bounds are what actually matter.
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Serial.printf("[web] OTA begin failed: %s\n", Update.errorString());
      return;
    }
    g_otaWriting = true;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!g_otaWriting) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      Serial.printf("[web] OTA write failed: %s\n", Update.errorString());
      Update.abort();
      g_otaWriting = false;
      return;
    }
    g_otaBytes += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (!g_otaWriting) return;
    g_otaWriting = false;
    if (!Update.end(true)) {
      Serial.printf("[web] OTA end failed: %s\n", Update.errorString());
      return;
    }
    g_otaOk = true;
    Serial.printf("[web] OTA image accepted (%lu bytes)\n",
                  (unsigned long)g_otaBytes);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    g_otaWriting = false;
    Serial.println("[web] OTA aborted");
  }
}

void handleOtaDone() {
  if (!g_otaOk) {
    g_server.send_P(500, "text/html", kOtaFail);
    return;
  }
  // Give the response time to leave the socket before the radio restarts.
  g_restartAt = millis() + 1200;
  g_server.send_P(200, "text/html", kOtaOk);
}

void startServer() {
  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/api/status", HTTP_GET, handleStatus);
  g_server.on("/scheme", HTTP_POST, handleScheme);
  g_server.on("/scale", HTTP_POST, handleScale);
  g_server.on("/mode", HTTP_POST, handleMode);
  g_server.on("/gmount", HTTP_POST, handleGMount);
  g_server.on("/gzero", HTTP_POST, handleGZero);
  g_server.on("/oscale", HTTP_POST, handleOilScale);
  g_server.on("/update", HTTP_POST, handleOtaDone, handleOtaUpload);
  g_server.on("/favicon.ico", HTTP_GET, []() { g_server.send(204, "text/plain", ""); });
  g_server.onNotFound([]() { g_server.send(404, "text/plain", "not found"); });
  g_server.begin();
}

}  // namespace

void begin() {
  if (g_started) return;

  // AP+STA is already set by ohxnow::begin(). The softAP must sit on the
  // receiver's channel; the first scan may move both, which is why the AP
  // is started on the channel the receiver is on right now.
  const uint8_t channel = ohxnow::channel();
  if (!WiFi.softAP(config::kApSsid, config::kApPassword, channel, 0,
                   config::kApMaxClients)) {
    Serial.println("[web] softAP failed; the page is unavailable");
    return;
  }

  startServer();
  g_started = true;
  g_rateMs = millis();
  g_clientCheckMs = millis();
  g_ratePackets = ohxnow::stats().statusPackets;

  // The softAP's country policy now caps the channels the shared radio can
  // tune to (world-safe is 1-11). Tell the hopper so it does not keep asking
  // for 12/13, which fail once the AP is up.
  wifi_country_t country = {};
  if (esp_wifi_get_country(&country) == ESP_OK && country.nchan >= 1) {
    ohxnow::setChannelLimit((uint8_t)country.nchan);
  }

  // One channel only from here on: the scan finds the controller's AP.
  ohxnow::setSetupAp(true);

  Serial.printf("[web] AP \"%s\" (%s) channel %u, http://%s/\n",
                config::kApSsid, config::kApPassword[0] ? "WPA2" : "open",
                (unsigned)channel, WiFi.softAPIP().toString().c_str());
}

void poll() {
  if (!g_started) return;

  g_server.handleClient();

  const uint32_t now = millis();
  if ((uint32_t)(now - g_rateMs) >= 5000) {
    const ohxnow::Stats s = ohxnow::stats();
    const uint32_t dt = now - g_rateMs;
    g_pps = (float)(s.statusPackets - g_ratePackets) * 1000.0f / (float)dt;
    g_ratePackets = s.statusPackets;
    g_rateMs = now;
  }

  // Querying the association count twice a second is enough to log a join,
  // and keeps the WiFi driver out of the 200 Hz loop path.
  if ((uint32_t)(now - g_clientCheckMs) >= 500) {
    g_clientCheckMs = now;
    const uint8_t n = (uint8_t)WiFi.softAPgetStationNum();
    if (n != g_clients) {
      g_clients = n;
      Serial.printf("[web] %u page client(s)\n", (unsigned)n);
    }
  }

  if (g_restartAt != 0 && (int32_t)(now - g_restartAt) >= 0) {
    Serial.println("[web] rebooting into the uploaded image");
    Serial.flush();
    ESP.restart();
  }
}

bool started() { return g_started; }

uint8_t clients() { return g_clients; }

}  // namespace gauge_web
