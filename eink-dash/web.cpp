#include "web.h"
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include "app.h"
#include "sources.h"

static WebServer server(80);

static String esc(const String &in) {
  String o;
  o.reserve(in.length() + 32);
  for (char c : in) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>eink-dash</title>
<style>
body{font:15px system-ui,sans-serif;margin:0 auto;max-width:1000px;padding:16px;background:#fafafa;color:#222}
h1{font-size:20px;margin:0 0 12px}
textarea{width:100%;box-sizing:border-box;height:60vh;font:13px/1.45 ui-monospace,monospace;padding:10px;border:1px solid #bbb;border-radius:6px}
button{font:inherit;padding:8px 16px;margin:8px 8px 0 0;border-radius:6px;border:1px solid #333;background:#222;color:#fff;cursor:pointer}
button.alt{background:#fff;color:#222}
.msg{padding:10px 12px;border-radius:6px;margin:0 0 12px;white-space:pre-wrap;font-family:ui-monospace,monospace;font-size:13px}
.err{background:#fde8e8;border:1px solid #e0a0a0}.ok{background:#e6f6e6;border:1px solid #9c9}
table{border-collapse:collapse;width:100%;margin-top:16px;font-size:13px}
td,th{border-bottom:1px solid #ddd;padding:6px;text-align:left;vertical-align:top}
code{word-break:break-all;font-size:12px;color:#555}
details{margin-top:16px}pre{background:#fff;border:1px solid #ddd;padding:10px;overflow:auto;font-size:12px}
</style></head><body><h1>eink-dash</h1>
)HTML";

static const char PAGE_HELP[] PROGMEM = R"HTML(
<details><summary>Config reference</summary><pre>
# Globals
wifi "ssid" "password"         # overrides secrets.h (device restarts on change)
timezone CST6CDT,M3.2.0,M11.1.0 # POSIX TZ string
ntp pool.ntp.org
redraw 60s                     # s / m / h
full_refresh_every 10          # partial refreshes between full ones
rotation 2                     # 0 upright, 2 upside down, 1/3 portrait (480x800)
                               # (or use the Flip / Rotate buttons above)
margin 80 10 20 15             # px hidden by a frame: left top right bottom
                               # (use /calibrate to measure). 0,0 = top-left
                               # visible pixel; negative x/y count from the
                               # right/bottom edge; rect W/H &lt;= 0 stretch to it

# Data sources (JSON, or plain text)
source NAME "URL" every 15m [header "Name: value"]...

# Widgets  (x,y of text = left end of the baseline)
text X Y [font=sans12] [size=1] [align=left|center|right] [color=black|white] "template"
list X Y items=SOURCE.path.to.array [max=10] [spacing=PX] [font=..] "template using ${item.field}"
line X1 Y1 X2 Y2 [color=..]
any widget: if=PATH (draw only when PATH has data) / unless=PATH (only when it doesn't)
rect X Y W H [fill] [radius=R] [color=..]

# Templates
${time %H:%M}  ${time %A %d %B}  (strftime)   ${ip}  ${rssi}
${weather.current.temperature_2m}   ${src.list[0].name}   ${src.list[-1]}
filters: |round |round:1 |int |upper |lower |trunc:N |lpad:N |rpad:N |unit: min
         |wmo (weather code to text) |mins (timestamp to minutes from now)
         |time:%H:%M (format a timestamp) |default:text

# Fonts
sans, sansbold, serif, serifbold, mono, monobold  x  9, 12, 18, 24   (e.g. sansbold24)
</pre></details>
)HTML";

static String orientationHtml();

static void sendPage(const String &text, const String &msg, bool isError) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(isError ? 400 : 200, "text/html; charset=utf-8", "");
  server.sendContent_P(PAGE_HEAD);

  if (msg.length()) server.sendContent("<div class=\"msg " + String(isError ? "err" : "ok") + "\">" + esc(msg) + "</div>");
  if (gConfigErrors.length() && !isError)
    server.sendContent("<div class=\"msg err\">Running config has errors (those lines are skipped):\n" + esc(gConfigErrors) + "</div>");

  server.sendContent("<form method=post action=/save><textarea name=config spellcheck=false>");
  server.sendContent(esc(text));
  server.sendContent("</textarea><br><button type=submit>Save &amp; apply</button>"
                     "<button class=alt formaction=/refresh>Refetch &amp; redraw</button>"
                     "<a href=/config.txt download=config.txt>download</a> &middot; "
                     "<a href=/calibrate>calibrate visible area</a> (now " +
                     String(visibleW(gConfig)) + " &times; " + String(visibleH(gConfig)) + " px)</form>");
  server.sendContent(orientationHtml());

  server.sendContent("<table><tr><th>source</th><th>status</th><th>updated</th><th>data</th></tr>");
  server.sendContent(sourcesStatusHtml());
  server.sendContent("</table>");
  server.sendContent_P(PAGE_HELP);
  server.sendContent("<p style=\"color:#888;font-size:12px\">IP " +
                     (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP() : WiFi.localIP()).toString() +
                     " &middot; free heap " + String(ESP.getFreeHeap() / 1024) + " KB &middot; up " +
                     String(millis() / 60000) + " min</p></body></html>");
  server.sendContent("");
}

static void handleRoot() {
  String msg;
  if (server.hasArg("saved")) msg = "Saved. The display is updating.";
  if (server.hasArg("refreshed")) msg = "Refetching and redrawing.";
  sendPage(gConfigText, msg, false);
}

static void handleSave() {
  String text = server.arg("config");
  text.replace("\r\n", "\n");
  String errors;
  if (!applyConfigText(text, errors)) {
    sendPage(text, "Not saved - fix these first:\n" + errors, true);
    return;
  }
  server.sendHeader("Location", "/?saved=1");
  server.send(303);
}

static void handleRefresh() {
  requestRefresh(true);
  server.sendHeader("Location", "/?refreshed=1");
  server.send(303);
}

// ---- orientation and margins ----
// Margins are stored as the viewer sees them (left, top, right, bottom).
// With GxEPD2, viewed edge i (in L,T,R,B order) at rotation r is physical
// edge (i + r) % 4, so turning by k quarter turns maps new[i] = old[(i+k)%4].

static void remapMargins(int m[4], int k) {
  int old[4] = {m[0], m[1], m[2], m[3]};
  for (int i = 0; i < 4; i++) m[i] = old[(i + k) % 4];
}

// Starting guess before calibrating: the left edge (as seen at rotation 0)
// hides about 80 px.
static void defaultMargins(int m[4]) {
  int phys[4] = {80, 0, 0, 0};
  for (int i = 0; i < 4; i++) m[i] = phys[(i + gConfig.rotation) % 4];
}

static const char *orientationName(int r) {
  static const char *names[4] = {"upright", "rotated 90&deg;", "upside down", "rotated 270&deg;"};
  return names[r & 3];
}

static String orientationHtml() {
  return "<form method=post action=/rotate style=\"margin-top:12px\">Orientation: <b>" +
         String(orientationName(gConfig.rotation)) + "</b> (rotation " + String(gConfig.rotation) +
         ") <button name=by value=2 class=alt>Flip 180&deg;</button>"
         "<button name=by value=1 class=alt>&#8635; Rotate 90&deg;</button></form>";
}

// Replaces any `key ...` lines in the config with `line`, placed just before
// the first non-comment line.
static String withSetting(const String &text, const String &key, const String &line) {
  String out;
  bool inserted = line.length() == 0;  // empty line = just remove the setting
  int start = 0, len = text.length();
  while (start < len) {
    int end = text.indexOf('\n', start);
    if (end < 0) end = len;
    String l = text.substring(start, end);
    start = end + 1;
    String t = l;
    t.trim();
    if (t == key || t.startsWith(key + " ") || t.startsWith(key + "\t")) continue;
    if (!inserted && t.length() && t[0] != '#') {
      out += line + "\n";
      inserted = true;
    }
    out += l + "\n";
  }
  if (!inserted) out += line + "\n";
  return out;
}

static String withMargin(const String &text, const int m[4]) {
  bool none = !m[0] && !m[1] && !m[2] && !m[3];
  if (none) return withSetting(text, "margin", "");  // drop the line
  return withSetting(text, "margin",
                     "margin " + String(m[0]) + " " + String(m[1]) + " " + String(m[2]) + " " + String(m[3]) +
                         "   # left top right bottom (set by /calibrate)");
}

static void handleRotate() {
  int k = constrain((int)server.arg("by").toInt(), 1, 3);
  bool calibrating = calibrationActive();
  int cal[4];
  memcpy(cal, calibrationMargins(), sizeof(cal));

  int m[4] = {gConfig.margin[0], gConfig.margin[1], gConfig.margin[2], gConfig.margin[3]};
  remapMargins(m, k);
  int r = (gConfig.rotation + k) % 4;
  String text = withSetting(gConfigText, "rotation", "rotation " + String(r) + "   # " +
                            String(r == 0 ? "upright" : r == 2 ? "upside down" : r == 1 ? "rotated 90" : "rotated 270"));
  text = withMargin(text, m);

  String errors;
  if (!applyConfigText(text, errors)) {
    server.send(400, "text/plain", "Could not rotate:\n" + errors);
    return;
  }
  if (calibrating) {
    remapMargins(cal, k);
    calibrationShow(cal);
  }
  server.sendHeader("Location", calibrating ? "/calibrate" : "/?saved=1");
  server.send(303);
}

// ---- calibration ----

static void sendCalibratePage(const String &msg) {
  const int *m = calibrationMargins();
  int v[4] = {m[0], m[1], m[2], m[3]};
  if (!calibrationActive() && !v[0] && !v[1] && !v[2] && !v[3]) defaultMargins(v);
  static const char *names[4] = {"left", "top", "right", "bottom"};

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent_P(PAGE_HEAD);
  server.sendContent(R"HTML(<style>
.cal{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;margin:12px 0}
.cal label{display:block;font-weight:600;margin-bottom:4px}
.cal input{width:70px;font:inherit;padding:6px;text-align:center}
.cal button{margin:0 2px;padding:6px 10px}
</style>
<p>The display now shows a ruler on each edge. Each ruler counts pixels in from its edge.
For each side, enter the <b>first number or tick you can see</b>, then press <b>Preview</b>.
A box is drawn at those margins: adjust until all four sides of the box are <b>just</b> visible
inside the frame, then <b>Save</b>. Each preview takes a second or two to show.</p>)HTML");
  if (msg.length()) server.sendContent("<div class=\"msg ok\">" + esc(msg) + "</div>");
  server.sendContent("<form method=post action=/calibrate id=f><div class=cal>");
  for (int i = 0; i < 4; i++) {
    String n = names[i];
    server.sendContent("<div><label for=" + n + ">" + n + "</label>"
                       "<button type=button class=alt onclick=\"s('" + n + "',-5)\">&minus;5</button>"
                       "<input type=number min=0 max=400 id=" + n + " name=" + n + " value=" + String(v[i]) + ">"
                       "<button type=button class=alt onclick=\"s('" + n + "',5)\">+5</button></div>");
  }
  server.sendContent(R"HTML(</div>
<button type=submit>Preview on display</button>
<button type=submit formaction=/calibrate/save>Save margins</button>
<button type=submit class=alt formaction=/calibrate/cancel>Cancel</button></form>
<script>function s(n,d){var e=document.getElementById(n);e.value=Math.max(0,(+e.value||0)+d);document.getElementById('f').submit()}</script>
<p><a href=/>&larr; back to editor</a></p>)HTML");
  server.sendContent(orientationHtml());
  server.sendContent("<p style=\"font-size:13px;color:#666\">Get the orientation right first: the rulers and box turn with it, and saved margins follow their physical edges.</p></body></html>");
  server.sendContent("");
}

static bool readMargins(int m[4]) {
  static const char *names[4] = {"left", "top", "right", "bottom"};
  for (int i = 0; i < 4; i++) {
    if (!server.hasArg(names[i])) return false;
    m[i] = constrain((int)server.arg(names[i]).toInt(), 0, 400);
  }
  return true;
}

static void handleCalibrateGet() {
  if (!calibrationActive()) {
    const int *cur = calibrationMargins();
    int m[4] = {cur[0], cur[1], cur[2], cur[3]};
    if (!m[0] && !m[1] && !m[2] && !m[3]) defaultMargins(m);
    calibrationShow(m);
  }
  sendCalibratePage("");
}

static void handleCalibratePreview() {
  int m[4];
  if (!readMargins(m)) { server.send(400, "text/plain", "need left, top, right, bottom"); return; }
  calibrationShow(m);
  sendCalibratePage("Showing left " + String(m[0]) + ", top " + String(m[1]) + ", right " + String(m[2]) +
                    ", bottom " + String(m[3]) + " on the display.");
}

static void handleCalibrateSave() {
  int m[4];
  if (!readMargins(m)) { server.send(400, "text/plain", "need left, top, right, bottom"); return; }
  String errors;
  if (!applyConfigText(withMargin(gConfigText, m), errors)) {
    sendCalibratePage("Could not save: " + errors);
    return;
  }
  server.sendHeader("Location", "/?saved=1");
  server.send(303);
}

static void handleCalibrateCancel() {
  calibrationEnd();
  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleRawGet() { server.send(200, "text/plain; charset=utf-8", gConfigText); }

static void handleRawPost() {
  String text = server.arg("plain");
  text.replace("\r\n", "\n");
  String errors;
  if (!applyConfigText(text, errors)) {
    server.send(400, "text/plain", "Not saved:\n" + errors);
    return;
  }
  server.send(200, "text/plain", "ok\n");
}

void webBegin() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/refresh", HTTP_POST, handleRefresh);
  server.on("/rotate", HTTP_POST, handleRotate);
  server.on("/calibrate", HTTP_GET, handleCalibrateGet);
  server.on("/calibrate", HTTP_POST, handleCalibratePreview);
  server.on("/calibrate/save", HTTP_POST, handleCalibrateSave);
  server.on("/calibrate/cancel", HTTP_POST, handleCalibrateCancel);
  server.on("/config.txt", HTTP_GET, handleRawGet);
  server.on("/config.txt", HTTP_POST, handleRawPost);
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });
  server.begin();
  if (MDNS.begin("eink")) MDNS.addService("http", "tcp", 80);
}

void webLoop() { server.handleClient(); }
