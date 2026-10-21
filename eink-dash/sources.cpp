#include "sources.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <memory>

namespace {

struct SourceState {
  SourceDef def;
  JsonDocument doc;
  bool have = false;      // doc holds data from a successful fetch
  bool lastOk = false;
  bool tried = false;
  String err;
  uint32_t lastAttempt = 0;
  uint32_t lastSuccess = 0;
};

std::vector<std::unique_ptr<SourceState>> states;

const size_t MAX_BODY = 160 * 1024;

bool fetch(SourceState &s) {
  s.tried = true;
  s.lastAttempt = millis();
  if (WiFi.status() != WL_CONNECTED) {
    s.err = "no wifi";
    return false;
  }

  WiFiClientSecure secure;
  WiFiClient plain;
  HTTPClient http;
  bool https = s.def.url.startsWith("https://");
  if (https) secure.setInsecure();  // no CA bundle; data is display-only
  if (!(https ? http.begin(secure, s.def.url) : http.begin(plain, s.def.url))) {
    s.err = "bad url";
    return false;
  }
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("eink-dash/1.0 (ESP32)");
  for (auto &h : s.def.headers) http.addHeader(h.key, h.value);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    s.err = code < 0 ? HTTPClient::errorToString(code) : "HTTP " + String(code);
    http.end();
    return false;
  }
  int size = http.getSize();
  if (size > (int)MAX_BODY) {
    s.err = "response too big (" + String(size / 1024) + " KB)";
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  if (body.length() == 0) {
    s.err = "empty response";
    return false;
  }

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);
  if (e) {
    // Not JSON: keep it as plain text so ${name} still shows it.
    doc.clear();
    body.trim();
    doc.set(body);
  } else if (doc.overflowed()) {
    s.err = "out of memory parsing JSON";
    return false;
  }
  s.doc = std::move(doc);
  s.have = true;
  s.err = "";
  s.lastSuccess = millis();
  return true;
}

SourceState *find(const String &name) {
  for (auto &s : states)
    if (s->def.name == name) return s.get();
  return nullptr;
}

String htmlEscape(const String &in) {
  String o;
  for (char c : in) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else o += c;
  }
  return o;
}

}  // namespace

void sourcesConfigure(const std::vector<SourceDef> &defs) {
  std::vector<std::unique_ptr<SourceState>> next;
  for (auto &d : defs) {
    std::unique_ptr<SourceState> st;
    for (auto &old : states) {
      if (old && old->def.name == d.name && old->def.url == d.url) {
        st = std::move(old);
        break;
      }
    }
    if (!st) st.reset(new SourceState());
    st->def = d;
    next.push_back(std::move(st));
  }
  states = std::move(next);
}

bool sourcesPoll(bool force) {
  bool changed = false;
  uint32_t now = millis();
  for (auto &sp : states) {
    SourceState &s = *sp;
    // Failed sources retry sooner (after at most a minute).
    uint32_t wait = s.lastOk ? s.def.everyMs : min(s.def.everyMs, (uint32_t)60000);
    if (!force && s.tried && now - s.lastAttempt < wait) continue;
    bool hadData = s.have;
    bool wasOk = s.lastOk;
    s.lastOk = fetch(s);
    Serial.printf("[source] %s: %s\n", s.def.name.c_str(), s.lastOk ? "ok" : s.err.c_str());
    if (!hadData && s.have) changed = true;  // first data arrived
    if (wasOk != s.lastOk) changed = true;   // error marker appears/disappears
  }
  return changed;
}

JsonVariantConst sourceRoot(const String &name) {
  SourceState *s = find(name);
  if (!s || !s->have) return JsonVariantConst();
  return s->doc.as<JsonVariantConst>();
}

String sourcesFailing() {
  String out;
  for (auto &s : states) {
    if (s->tried && !s->lastOk) {
      if (out.length()) out += ' ';
      out += s->def.name;
    }
  }
  return out;
}

String sourcesStatusHtml() {
  String out;
  uint32_t now = millis();
  for (auto &s : states) {
    out += "<tr><td>" + htmlEscape(s->def.name) + "</td><td>";
    if (!s->tried) out += "pending";
    else if (s->lastOk) out += "ok";
    else out += "<b>error:</b> " + htmlEscape(s->err);
    out += "</td><td>";
    out += s->have ? String((now - s->lastSuccess) / 1000) + "s ago" : String("never");
    out += "</td><td>";
    if (s->have) {
      String preview;
      serializeJson(s->doc, preview);
      if (preview.length() > 300) preview = preview.substring(0, 300) + "...";
      out += "<code>" + htmlEscape(preview) + "</code>";
    }
    out += "</td></tr>";
  }
  return out;
}
