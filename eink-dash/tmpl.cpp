#include "tmpl.h"
#include <WiFi.h>
#include <time.h>
#include "sources.h"

bool timeValid() { return time(nullptr) > 1700000000; }

JsonVariantConst resolvePath(const String &path, JsonVariantConst item) {
  int i = 0, n = path.length();
  auto readKey = [&]() {
    String k;
    while (i < n && path[i] != '.' && path[i] != '[') k += path[i++];
    return k;
  };

  String head = readKey();
  JsonVariantConst v = (head == "item") ? item : sourceRoot(head);

  while (i < n && !v.isNull()) {
    if (path[i] == '.') {
      i++;
      String k = readKey();
      // Numeric segment on an array means index: a.0.b
      if (v.is<JsonArrayConst>() && k.length() && isDigit(k[0])) v = v[k.toInt()];
      else v = v[k];
    } else if (path[i] == '[') {
      int close = path.indexOf(']', i);
      if (close < 0) return JsonVariantConst();
      String idx = path.substring(i + 1, close);
      i = close + 1;
      if (idx.length() && (isDigit(idx[0]) || idx[0] == '-')) {
        int k = idx.toInt();
        if (k < 0 && v.is<JsonArrayConst>()) k += v.size();
        v = v[k];
      } else {
        // ["key with.dots"]
        if (idx.length() >= 2 && (idx[0] == '\'' || idx[0] == '"')) idx = idx.substring(1, idx.length() - 1);
        v = v[idx];
      }
    } else {
      return JsonVariantConst();
    }
  }
  return v;
}

namespace {

String valueToString(JsonVariantConst v) {
  if (v.is<const char *>()) return v.as<const char *>();
  if (v.is<bool>()) return v.as<bool>() ? "true" : "false";
  if (v.is<long long>()) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%lld", v.as<long long>());
    return buf;
  }
  if (v.is<double>()) {
    String s = String(v.as<double>(), 2);
    // trim trailing zeros: 12.50 -> 12.5, 3.00 -> 3
    while (s.endsWith("0")) s.remove(s.length() - 1);
    if (s.endsWith(".")) s.remove(s.length() - 1);
    return s;
  }
  String s;
  serializeJson(v, s);
  return s;
}

int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

// Accepts unix seconds/milliseconds, ISO-8601 ("2026-10-20T14:32:00Z",
// "...+02:00", or no zone = local time), "20261020 14:32" (CTA style), or a
// bare "14:32" (today/tomorrow, local). Returns -1 if unrecognised.
time_t parseTimestamp(String s) {
  s.trim();
  if (s.length() == 0) return -1;

  bool digits = true;
  for (char c : s) if (!isDigit(c)) { digits = false; break; }
  if (digits && s.length() >= 9) {
    long long v = atoll(s.c_str());
    if (v > 100000000000LL) v /= 1000;
    return (time_t)v;
  }

  int Y = 0, M = 0, D = 0, h = 0, m = 0, sec = 0;
  struct tm lt = {};
  bool haveDate = false;
  int pos = 0;

  if (sscanf(s.c_str(), "%4d-%2d-%2d%*c%2d:%2d", &Y, &M, &D, &h, &m) == 5) {
    haveDate = true;
    pos = 16;
  } else if (sscanf(s.c_str(), "%4d%2d%2d %2d:%2d", &Y, &M, &D, &h, &m) == 5) {
    haveDate = true;
    pos = 14;
  } else if (s.length() == 10 && sscanf(s.c_str(), "%4d-%2d-%2d", &Y, &M, &D) == 3) {
    haveDate = true;  // date only: local midnight
    pos = 10;
  } else if (sscanf(s.c_str(), "%2d:%2d", &h, &m) == 2 && s.length() <= 8) {
    time_t now = time(nullptr);
    localtime_r(&now, &lt);
    lt.tm_hour = h;
    lt.tm_min = m;
    lt.tm_sec = 0;
    lt.tm_isdst = -1;
    time_t t = mktime(&lt);
    if (t < now - 12 * 3600) t += 24 * 3600;
    return t;
  } else {
    return -1;
  }

  if (haveDate && pos < (int)s.length() && s[pos] == ':') {
    sec = atoi(s.c_str() + pos + 1);
    pos += 3;
  }
  while (pos < (int)s.length() && (s[pos] == '.' || isDigit(s[pos]))) pos++;  // fractions

  if (pos < (int)s.length()) {
    char z = s[pos];
    long offset = 0;
    if (z == '+' || z == '-') {
      int oh = 0, om = 0;
      sscanf(s.c_str() + pos + 1, "%2d:%2d", &oh, &om);
      if (om == 0 && s.length() >= (unsigned)pos + 5 && s[pos + 3] != ':') om = atoi(s.c_str() + pos + 3);
      offset = (oh * 3600L + om * 60L) * (z == '-' ? -1 : 1);
    } else if (z != 'Z' && z != 'z') {
      return -1;
    }
    int64_t t = daysFromCivil(Y, M, D) * 86400 + h * 3600 + m * 60 + sec;
    return (time_t)(t - offset);
  }

  // No zone: local time.
  lt.tm_year = Y - 1900;
  lt.tm_mon = M - 1;
  lt.tm_mday = D;
  lt.tm_hour = h;
  lt.tm_min = m;
  lt.tm_sec = sec;
  lt.tm_isdst = -1;
  return mktime(&lt);
}

String formatTime(time_t t, const String &fmt) {
  struct tm lt;
  localtime_r(&t, &lt);
  char buf[96];
  size_t n = strftime(buf, sizeof(buf), fmt.c_str(), &lt);
  return String(buf).substring(0, n);
}

const char *wmoText(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunder + hail";
    default: return "?";
  }
}

String applyFilter(String v, bool &missing, const String &spec) {
  int colon = spec.indexOf(':');
  String name = colon < 0 ? spec : spec.substring(0, colon);
  String arg = colon < 0 ? String() : spec.substring(colon + 1);
  name.trim();

  if (name == "default") {
    if (missing || v.length() == 0) { missing = false; return arg; }
    return v;
  }
  if (missing) return v;

  if (name == "round") {
    int d = arg.length() ? arg.toInt() : 0;
    double x = v.toDouble();
    String r = d <= 0 ? String((long)lround(x)) : String(x, d);
    return r == "-0" ? "0" : r;
  }
  if (name == "int") return String(v.toInt());
  if (name == "unit") {
    // Append arg only to numbers: "5" -> "5 min", "DUE" stays "DUE".
    String t = v;
    t.trim();
    bool num = t.length() > 0;
    for (size_t i = 0; i < t.length(); i++)
      if (!isDigit(t[i]) && !(i == 0 && t[i] == '-') && t[i] != '.') num = false;
    return num ? v + arg : v;
  }
  if (name == "upper") { v.toUpperCase(); return v; }
  if (name == "lower") { v.toLowerCase(); return v; }
  if (name == "wmo") return wmoText(v.toInt());
  if (name == "trunc") {
    int n = arg.toInt();
    return (int)v.length() > n ? v.substring(0, n) : v;
  }
  if (name == "lpad" || name == "rpad") {
    int n = arg.toInt();
    while ((int)v.length() < n) v = (name == "lpad") ? " " + v : v + " ";
    return v;
  }
  if (name == "mins") {
    time_t t = parseTimestamp(v);
    if (t < 0 || !timeValid()) { missing = true; return "?"; }
    long m = (long)((t - time(nullptr)) / 60);
    return String(m < 0 ? 0 : m);
  }
  if (name == "time") {
    time_t t = parseTimestamp(v);
    if (t < 0) { missing = true; return "?"; }
    return formatTime(t, arg.length() ? arg : String("%H:%M"));
  }
  return v + "[?" + name + "]";
}

String evalExpr(const String &expr, JsonVariantConst item) {
  // Split "head|filter|filter:arg"
  std::vector<String> parts;
  int start = 0;
  while (true) {
    int bar = expr.indexOf('|', start);
    parts.push_back(expr.substring(start, bar < 0 ? expr.length() : bar));
    if (bar < 0) break;
    start = bar + 1;
  }
  String head = parts[0];
  head.trim();

  String v;
  bool missing = false;
  if (head == "time" || head.startsWith("time ")) {
    String fmt = head.length() > 5 ? head.substring(5) : String("%H:%M");
    if (timeValid()) v = formatTime(time(nullptr), fmt);
    else missing = true;
  } else if (head == "ip") {
    v = WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  } else if (head == "rssi") {
    v = String(WiFi.RSSI());
  } else {
    JsonVariantConst jv = resolvePath(head, item);
    if (jv.isNull()) missing = true;
    else v = valueToString(jv);
  }

  for (size_t i = 1; i < parts.size(); i++) v = applyFilter(v, missing, parts[i]);
  return missing ? String("?") : v;
}

}  // namespace

String expandTemplate(const String &t, JsonVariantConst item) {
  String out;
  int i = 0, n = t.length();
  while (i < n) {
    int s = t.indexOf("${", i);
    if (s < 0) { out += t.substring(i); break; }
    out += t.substring(i, s);
    int e = t.indexOf('}', s + 2);
    if (e < 0) { out += t.substring(s); break; }
    out += evalExpr(t.substring(s + 2, e), item);
    i = e + 1;
  }
  return out;
}
