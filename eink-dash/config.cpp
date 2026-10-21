#include "config.h"
#include "render.h"

namespace {

struct Token {
  String s;
  bool quoted;
};

// Splits a line into tokens. Double quotes group words (with \" \\ \n escapes).
// A '#' at the start of an unquoted token begins a comment.
bool tokenize(const String &line, std::vector<Token> &out, String &err) {
  int i = 0, n = line.length();
  while (i < n) {
    char c = line[i];
    if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
    if (c == '#') break;
    if (c == '"') {
      String s;
      i++;
      bool closed = false;
      while (i < n) {
        char d = line[i++];
        if (d == '\\' && i < n) {
          char e = line[i++];
          s += (e == 'n') ? '\n' : e;
        } else if (d == '"') {
          closed = true;
          break;
        } else {
          s += d;
        }
      }
      if (!closed) { err = "missing closing quote"; return false; }
      out.push_back({s, true});
    } else {
      String s;
      while (i < n && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') s += line[i++];
      out.push_back({s, false});
    }
  }
  return true;
}

bool isInt(const String &s) {
  if (s.length() == 0) return false;
  int i = (s[0] == '-') ? 1 : 0;
  if (i == (int)s.length()) return false;
  for (; i < (int)s.length(); i++)
    if (!isDigit(s[i])) return false;
  return true;
}

// "90", "90s", "15m", "1h" -> milliseconds. 0 on error.
uint32_t parseDuration(const String &s) {
  if (s.length() == 0) return 0;
  char unit = s[s.length() - 1];
  String num = isDigit(unit) ? s : s.substring(0, s.length() - 1);
  if (!isInt(num)) return 0;
  long v = num.toInt();
  if (v <= 0) return 0;
  switch (unit) {
    case 'h': return v * 3600000UL;
    case 'm': return v * 60000UL;
    case 's': return v * 1000UL;
    default: return isDigit(unit) ? v * 1000UL : 0;
  }
}

bool validName(const String &s) {
  if (s.length() == 0) return false;
  for (char c : s)
    if (!isAlphaNumeric(c) && c != '_') return false;
  return s != "time" && s != "item" && s != "ip" && s != "rssi";
}

}  // namespace

bool parseConfig(const String &text, Config &out, String &errors) {
  out = Config();
  errors = "";
  int lineNo = 0;
  int start = 0;
  int len = text.length();

  while (start <= len) {
    int end = text.indexOf('\n', start);
    if (end < 0) end = len;
    String line = text.substring(start, end);
    start = end + 1;
    lineNo++;

    std::vector<Token> t;
    String err;
    auto fail = [&](const String &msg) {
      errors += "line " + String(lineNo) + ": " + msg + "\n";
    };

    if (!tokenize(line, t, err)) { fail(err); continue; }
    if (t.empty()) continue;

    String kw = t[0].s;
    kw.toLowerCase();

    // ---- globals ----
    if (kw == "timezone" || kw == "ntp") {
      if (t.size() != 2) { fail(kw + " needs one value"); continue; }
      (kw == "timezone" ? out.tz : out.ntp) = t[1].s;
      continue;
    }
    if (kw == "wifi") {
      if (t.size() != 3) { fail("usage: wifi \"ssid\" \"password\""); continue; }
      out.wifiSsid = t[1].s;
      out.wifiPass = t[2].s;
      continue;
    }
    if (kw == "redraw") {
      uint32_t ms = (t.size() == 2) ? parseDuration(t[1].s) : 0;
      if (ms < 10000) { fail("redraw needs a duration of at least 10s, e.g. 60s or 5m"); continue; }
      out.redrawMs = ms;
      continue;
    }
    if (kw == "margin") {
      // margin ALL  |  margin LEFT TOP RIGHT BOTTOM
      bool ok = (t.size() == 2 || t.size() == 5);
      for (size_t i = 1; ok && i < t.size(); i++) ok = isInt(t[i].s) && t[i].s.toInt() >= 0;
      if (!ok) { fail("usage: margin LEFT TOP RIGHT BOTTOM (or one number for all four)"); continue; }
      for (int i = 0; i < 4; i++) out.margin[i] = t[t.size() == 2 ? 1 : i + 1].s.toInt();
      out.marginLine = lineNo;
      continue;
    }
    if (kw == "full_refresh_every" || kw == "rotation") {
      if (t.size() != 2 || !isInt(t[1].s)) { fail(kw + " needs a number"); continue; }
      int v = t[1].s.toInt();
      if (kw == "rotation") {
        if (v < 0 || v > 3) { fail("rotation must be 0-3"); continue; }
        out.rotation = v;
      } else {
        out.fullRefreshEvery = max(1, v);
      }
      continue;
    }

    // ---- data sources ----
    if (kw == "source") {
      if (t.size() < 3) { fail("usage: source <name> \"<url>\" every <interval>"); continue; }
      SourceDef s;
      s.name = t[1].s;
      s.url = t[2].s;
      if (!validName(s.name)) { fail("bad source name '" + s.name + "' (letters, digits, _ only; not time/item/ip/rssi)"); continue; }
      if (!s.url.startsWith("http://") && !s.url.startsWith("https://")) { fail("url must start with http:// or https://"); continue; }
      bool ok = true;
      for (size_t i = 3; i < t.size() && ok; i++) {
        if (t[i].s == "every" && i + 1 < t.size()) {
          s.everyMs = parseDuration(t[++i].s);
          if (s.everyMs < 10000) { fail("'every' needs a duration of at least 10s"); ok = false; }
        } else if (t[i].s == "header" && i + 1 < t.size()) {
          String h = t[++i].s;
          int colon = h.indexOf(':');
          if (colon <= 0) { fail("header must look like \"Name: value\""); ok = false; break; }
          Header hd{h.substring(0, colon), h.substring(colon + 1)};
          hd.key.trim();
          hd.value.trim();
          s.headers.push_back(hd);
        } else {
          fail("unexpected '" + t[i].s + "' in source");
          ok = false;
        }
      }
      for (auto &o : out.sources)
        if (o.name == s.name) { fail("duplicate source '" + s.name + "'"); ok = false; }
      if (ok) out.sources.push_back(s);
      continue;
    }

    // ---- widgets ----
    Widget w;
    w.line = lineNo;
    int needNums;
    if (kw == "text") { w.type = W_TEXT; needNums = 2; }
    else if (kw == "list") { w.type = W_LIST; needNums = 2; }
    else if (kw == "line") { w.type = W_LINE; needNums = 4; }
    else if (kw == "rect") { w.type = W_RECT; needNums = 4; }
    else { fail("unknown keyword '" + t[0].s + "'"); continue; }

    int nums = 0;
    bool haveTmpl = false, ok = true;
    for (size_t i = 1; i < t.size() && ok; i++) {
      const String &s = t[i].s;
      if (t[i].quoted) {
        w.tmpl = s;
        haveTmpl = true;
        continue;
      }
      int eq = s.indexOf('=');
      if (eq < 0) {
        if (isInt(s)) {
          if (nums >= needNums) { fail("too many numbers"); ok = false; break; }
          w.n[nums++] = s.toInt();
        } else if (s == "fill") {
          w.fill = true;
        } else {
          fail("unexpected '" + s + "' (text must be in \"quotes\")");
          ok = false;
        }
        continue;
      }
      String k = s.substring(0, eq), v = s.substring(eq + 1);
      if (k == "font") {
        if (!fontExists(v)) { fail("unknown font '" + v + "' (see README for names)"); ok = false; }
        w.font = v;
      } else if (k == "size") {
        w.size = constrain(v.toInt(), 1, 10);
      } else if (k == "align") {
        if (v != "left" && v != "center" && v != "right") { fail("align must be left, center or right"); ok = false; }
        w.align = v[0] == 'c' ? 'c' : v[0] == 'r' ? 'r' : 'l';
      } else if (k == "color") {
        if (v != "black" && v != "white") { fail("color must be black or white"); ok = false; }
        w.white = (v == "white");
      } else if (k == "radius") {
        w.radius = max(0, (int)v.toInt());
      } else if (k == "spacing") {
        w.spacing = max(0, (int)v.toInt());
      } else if (k == "max") {
        w.maxItems = max(1, (int)v.toInt());
      } else if (k == "items") {
        w.items = v;
      } else if (k == "sort") {
        w.sortKey = v;
      } else if (k == "if" || k == "unless") {
        w.cond = v;
        w.condNegate = (k == "unless");
      } else {
        fail("unknown option '" + k + "'");
        ok = false;
      }
    }
    if (!ok) continue;
    if (nums != needNums) { fail(kw + " needs " + String(needNums) + " numbers"); continue; }
    if ((w.type == W_TEXT || w.type == W_LIST) && !haveTmpl) { fail(kw + " needs a \"template\""); continue; }
    if (w.type == W_LIST && w.items.length() == 0) { fail("list needs items=<path to array>"); continue; }
    out.widgets.push_back(w);
  }

  // Checked at the end because `rotation` may come after `margin`.
  if (visibleW(out) < 100 || visibleH(out) < 100) {
    errors += "line " + String(out.marginLine) + ": margins leave less than 100 px visible (screen is " +
              String(screenW(out)) + " x " + String(screenH(out)) + ")\n";
    for (int &m : out.margin) m = 0;
  }
  return errors.length() == 0;
}
