#include "render.h"

#include <algorithm>
#include <vector>

#define ENABLE_GxEPD2_GFX 0
#include <GxEPD2_BW.h>
#include <SPI.h>

#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMono18pt7b.h>
#include <Fonts/FreeMono24pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerif18pt7b.h>
#include <Fonts/FreeSerif24pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifBold18pt7b.h>
#include <Fonts/FreeSerifBold24pt7b.h>

#include "sources.h"
#include "tmpl.h"

// ---- hardware (same wiring as rk-test) ----
static SPIClass hspi(HSPI);
static GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT> display(
    GxEPD2_750_GDEY075T7(/*CS=*/15, /*DC=*/27, /*RST=*/26, /*BUSY=*/25));

// ---- fonts ----
struct FontEntry {
  const char *name;
  const GFXfont *font;
};
static const FontEntry FONTS[] = {
    {"mono9", &FreeMono9pt7b},         {"mono12", &FreeMono12pt7b},
    {"mono18", &FreeMono18pt7b},       {"mono24", &FreeMono24pt7b},
    {"monobold9", &FreeMonoBold9pt7b}, {"monobold12", &FreeMonoBold12pt7b},
    {"monobold18", &FreeMonoBold18pt7b}, {"monobold24", &FreeMonoBold24pt7b},
    {"sans9", &FreeSans9pt7b},         {"sans12", &FreeSans12pt7b},
    {"sans18", &FreeSans18pt7b},       {"sans24", &FreeSans24pt7b},
    {"sansbold9", &FreeSansBold9pt7b}, {"sansbold12", &FreeSansBold12pt7b},
    {"sansbold18", &FreeSansBold18pt7b}, {"sansbold24", &FreeSansBold24pt7b},
    {"serif9", &FreeSerif9pt7b},       {"serif12", &FreeSerif12pt7b},
    {"serif18", &FreeSerif18pt7b},     {"serif24", &FreeSerif24pt7b},
    {"serifbold9", &FreeSerifBold9pt7b}, {"serifbold12", &FreeSerifBold12pt7b},
    {"serifbold18", &FreeSerifBold18pt7b}, {"serifbold24", &FreeSerifBold24pt7b},
};

static const GFXfont *findFont(const String &name) {
  for (auto &f : FONTS)
    if (name == f.name) return f.font;
  return &FreeSans12pt7b;
}

bool fontExists(const String &name) {
  for (auto &f : FONTS)
    if (name == f.name) return true;
  return false;
}

// ---- text drawing ----
// The GFX fonts only cover printable ASCII. We convert UTF-8 to that, mapping
// the degree sign to '\x01' (drawn as a small circle) and a few lookalikes.
static const char DEGREE = '\x01';

static String toFontText(const String &in) {
  String out;
  int n = in.length();
  for (int i = 0; i < n; i++) {
    uint8_t c = in[i];
    if (c < 0x80) {
      if (c == '\n' || (c >= 0x20 && c < 0x7F)) out += (char)c;
      continue;
    }
    // multi-byte UTF-8 sequence
    int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
    uint32_t cp = c & (0xFF >> (len + 1));
    for (int k = 1; k < len && i + k < n; k++) cp = (cp << 6) | (in[i + k] & 0x3F);
    i += len - 1;
    switch (cp) {
      case 0xB0: out += DEGREE; break;                    // °
      case 0x2013: case 0x2014: case 0x2212: out += '-'; break;  // – — −
      case 0x2018: case 0x2019: out += '\''; break;       // ‘ ’
      case 0x201C: case 0x201D: out += '"'; break;        // “ ”
      case 0x2022: case 0xB7: out += '*'; break;          // • ·
      case 0xA0: out += ' '; break;
      default:
        if (cp >= 0xC0 && cp <= 0xC5) out += 'A';
        else if (cp >= 0xE0 && cp <= 0xE5) out += 'a';
        else if (cp >= 0xE8 && cp <= 0xEB) out += 'e';
        else if (cp >= 0xC8 && cp <= 0xCB) out += 'E';
        else if (cp >= 0xEC && cp <= 0xEF) out += 'i';
        else if (cp >= 0xF2 && cp <= 0xF6) out += 'o';
        else if (cp >= 0xF9 && cp <= 0xFC) out += 'u';
        else if (cp == 0xF1) out += 'n';
        else if (cp == 0xDF) out += "ss";
        else out += '?';
    }
  }
  return out;
}

static int degreeRadius(const GFXfont *f, int size) {
  return max(2, (int)f->yAdvance / 10) * size;
}

static int textWidth(const String &s, const GFXfont *f, int size) {
  int w = 0;
  for (char c : s) {
    if (c == DEGREE) w += degreeRadius(f, size) * 2 + 3 * size;
    else if ((uint8_t)c >= f->first && (uint8_t)c <= f->last)
      w += f->glyph[(uint8_t)c - f->first].xAdvance * size;
  }
  return w;
}

// Draws one line with its baseline at y, anchored at x by `align`.
static void drawLine(String s, int x, int y, const GFXfont *f, int size, char align, uint16_t color) {
  int w = textWidth(s, f, size);
  if (align == 'c') x -= w / 2;
  else if (align == 'r') x -= w;
  display.setFont(f);
  display.setTextSize(size);
  display.setTextColor(color);
  display.setTextWrap(false);
  display.setCursor(x, y);
  for (char c : s) {
    if (c == DEGREE) {
      int r = degreeRadius(f, size);
      int cap = f->glyph['0' - f->first].height * size;  // digit height
      int cx = display.getCursorX() + r + size;
      int cy = y - cap + r;
      for (int t = 0; t < max(1, r / 3); t++) display.drawCircle(cx, cy, r - t, color);
      display.setCursor(cx + r + 2 * size, y);
    } else {
      display.write(c);
    }
  }
}

static void drawText(const String &raw, int x, int y, const Widget &w) {
  const GFXfont *f = findFont(w.font);
  uint16_t color = w.white ? GxEPD_WHITE : GxEPD_BLACK;
  String s = toFontText(raw);
  int lineH = f->yAdvance * w.size;
  int start = 0;
  while (true) {
    int nl = s.indexOf('\n', start);
    drawLine(s.substring(start, nl < 0 ? s.length() : nl), x, y, f, w.size, w.align, color);
    if (nl < 0) break;
    start = nl + 1;
    y += lineH;
  }
}

// ---- visible area ----
// Widget coordinates are relative to the part of the screen not hidden by a
// frame (the `margin` setting). Negative values count from the right/bottom.
static int areaX = 0, areaY = 0, areaW = 800, areaH = 480;

static void setArea(const Config &cfg) {
  areaX = cfg.margin[0];
  areaY = cfg.margin[1];
  areaW = visibleW(cfg);
  areaH = visibleH(cfg);
}

static int relX(int x) { return x < 0 ? areaW + x : x; }
static int relY(int y) { return y < 0 ? areaH + y : y; }
static int px(int x) { return areaX + relX(x); }
static int py(int y) { return areaY + relY(y); }

// Truthy = present and not false / empty string / empty array / empty object.
static bool truthy(JsonVariantConst v) {
  if (v.isNull()) return false;
  if (v.is<bool>()) return v.as<bool>();
  if (v.is<const char *>()) return strlen(v.as<const char *>()) > 0;
  if (v.is<JsonArrayConst>() || v.is<JsonObjectConst>()) return v.size() > 0;
  return true;
}

// Splits "a+b+c" into paths.
static std::vector<String> splitPlus(const String &s) {
  std::vector<String> out;
  int start = 0;
  while (true) {
    int p = s.indexOf('+', start);
    String part = s.substring(start, p < 0 ? s.length() : p);
    part.trim();
    if (part.length()) out.push_back(part);
    if (p < 0) break;
    start = p + 1;
  }
  return out;
}

// if=a+b is true when any of the paths has data.
static bool condTrue(const String &cond) {
  for (auto &p : splitPlus(cond))
    if (truthy(resolvePath(p, JsonVariantConst()))) return true;
  return false;
}

// Compares two values numerically when both are numbers, else as text.
static bool lessValue(JsonVariantConst a, JsonVariantConst b) {
  if (a.isNull()) return false;
  if (b.isNull()) return true;
  bool an = a.is<double>(), bn = b.is<double>();
  if (an && bn) return a.as<double>() < b.as<double>();
  String sa = a.is<const char *>() ? String(a.as<const char *>()) : String(a.as<double>());
  String sb = b.is<const char *>() ? String(b.as<const char *>()) : String(b.as<double>());
  return sa < sb;
}

// Collects list items from one or more arrays (items=a+b), optionally sorted.
static std::vector<JsonVariantConst> collectItems(const Widget &w) {
  std::vector<JsonVariantConst> items;
  for (auto &p : splitPlus(w.items)) {
    JsonVariantConst v = resolvePath(p, JsonVariantConst());
    if (v.is<JsonArrayConst>())
      for (JsonVariantConst e : v.as<JsonArrayConst>()) items.push_back(e);
    else if (v.is<JsonObjectConst>())
      items.push_back(v);  // some APIs return one object instead of a list of one
  }
  if (w.sortKey.length()) {
    String key = w.sortKey;
    std::stable_sort(items.begin(), items.end(), [&](JsonVariantConst a, JsonVariantConst b) {
      return lessValue(resolvePath("item." + key, a), resolvePath("item." + key, b));
    });
  }
  return items;
}

static void drawWidget(const Widget &w) {
  if (w.cond.length() && condTrue(w.cond) == w.condNegate) return;
  uint16_t color = w.white ? GxEPD_WHITE : GxEPD_BLACK;
  int x = px(w.n[0]), y = py(w.n[1]);
  switch (w.type) {
    case W_TEXT:
      drawText(expandTemplate(w.tmpl, JsonVariantConst()), x, y, w);
      break;
    case W_LINE:
      display.drawLine(x, y, px(w.n[2]), py(w.n[3]), color);
      break;
    case W_RECT: {
      // Width/height of 0 or less reach to that many px before the far edge.
      int rw = w.n[2] > 0 ? w.n[2] : areaW + w.n[2] - relX(w.n[0]);
      int rh = w.n[3] > 0 ? w.n[3] : areaH + w.n[3] - relY(w.n[1]);
      if (rw <= 0 || rh <= 0) break;
      if (w.fill) display.fillRoundRect(x, y, rw, rh, w.radius, color);
      else display.drawRoundRect(x, y, rw, rh, w.radius, color);
      break;
    }
    case W_LIST: {
      // No items = draw nothing (pair with an `unless=` text for a message).
      int step = w.spacing ? w.spacing : findFont(w.font)->yAdvance * w.size;
      int i = 0;
      for (JsonVariantConst item : collectItems(w)) {
        if (i >= w.maxItems) break;
        drawText(expandTemplate(w.tmpl, item), x, y + i * step, w);
        i++;
      }
      break;
    }
  }
}

// Small inverted bar along the bottom with a notice (errors, setup hints).
static void drawFooter(const String &notice) {
  if (notice.length() == 0) return;
  const GFXfont *f = &FreeSans9pt7b;
  int lines = 1;
  for (char c : notice) if (c == '\n') lines++;
  lines = min(lines, 4);
  int h = lines * f->yAdvance + 8;
  int y0 = areaY + areaH - h;
  display.fillRect(areaX, y0, areaW, h, GxEPD_BLACK);
  String s = toFontText(notice);
  int start = 0, y = y0 + f->yAdvance;
  for (int i = 0; i < lines; i++) {
    int nl = s.indexOf('\n', start);
    drawLine(s.substring(start, nl < 0 ? s.length() : nl), areaX + 6, y, f, 1, 'l', GxEPD_WHITE);
    if (nl < 0) break;
    start = nl + 1;
    y += f->yAdvance;
  }
}

static void drawSourceErrors() {
  String failing = sourcesFailing();
  if (failing.length() == 0) return;
  String s = "!" + failing;
  s.replace(" ", " !");
  drawLine(s, areaX + areaW - 4, areaY + 14, &FreeSans9pt7b, 1, 'r', GxEPD_BLACK);
}

void displayInit() {
  hspi.begin(13, 12, 14, 15);  // Waveshare ESP32 driver board: SCK=13, MISO=12, MOSI=14, SS=15
  display.epd2.selectSPI(hspi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(115200);
}

void renderAll(const Config &cfg, const String &notice, bool full) {
  uint32_t t0 = millis();
  display.setRotation(cfg.rotation);
  setArea(cfg);
  if (full) display.setFullWindow();
  else display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    for (auto &w : cfg.widgets) drawWidget(w);
    drawSourceErrors();
    drawFooter(notice);
  } while (display.nextPage());
  display.powerOff();
  Serial.printf("[display] %s refresh in %lu ms\n", full ? "full" : "partial", millis() - t0);
}

void renderMessage(const Config &cfg, const String &title, const String &body) {
  display.setRotation(cfg.rotation);
  setArea(cfg);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawLine(toFontText(title), areaX + 20, areaY + 50, &FreeSansBold18pt7b, 1, 'l', GxEPD_BLACK);
    String s = toFontText(body);
    int start = 0, y = areaY + 100;
    while (true) {
      int nl = s.indexOf('\n', start);
      drawLine(s.substring(start, nl < 0 ? s.length() : nl), areaX + 20, y, &FreeSans12pt7b, 1, 'l', GxEPD_BLACK);
      if (nl < 0) break;
      start = nl + 1;
      y += FreeSans12pt7b.yAdvance;
    }
  } while (display.nextPage());
  display.powerOff();
}

// ---- calibration screen ----
// Rulers measure inward from each physical edge; a box shows the margins
// currently being tried. Labels use the built-in 6x8 font at size 2 (12x16).

static void label(int value, int x, int y, char align) {
  String s(value);
  int w = s.length() * 12;
  if (align == 'c') x -= w / 2;
  else if (align == 'r') x -= w;
  display.setFont(nullptr);
  display.setTextSize(2);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(x, y);  // top-left for the built-in font
  display.print(s);
}

void renderCalibration(const Config &cfg, const int m[4], bool full) {
  display.setRotation(cfg.rotation);
  int W = display.width(), H = display.height();
  int hLen = min(240, W / 2 - 10);  // left/right rulers
  int vLen = min(160, H / 3);       // top/bottom rulers
  int rulerY = H / 4;               // row for the left/right rulers
  int rulerX = W / 2;               // column for the top/bottom rulers

  if (full) display.setFullWindow();
  else display.setPartialWindow(0, 0, W, H);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Left and right: ticks hang below the ruler line, labels sit above in
    // two staggered rows so 3-digit numbers don't collide.
    for (int side = 0; side < 2; side++) {
      auto X = [&](int d) { return side == 0 ? d : W - 1 - d; };
      display.drawFastHLine(min(X(0), X(hLen)), rulerY, hLen, GxEPD_BLACK);
      for (int d = 0; d <= hLen; d += 10) {
        int len = d % 50 == 0 ? 26 : 12;
        display.fillRect(X(d) - (side ? 1 : 0), rulerY, 2, len, GxEPD_BLACK);
        if (d % 20 == 0) {
          int row = (d % 40 == 0) ? 0 : 1;
          int ly = rulerY - 22 - row * 20;
          display.drawFastVLine(X(d), ly + 16, rulerY - ly - 16, GxEPD_BLACK);
          label(d, X(d), ly, 'c');
        }
      }
    }

    // Top and bottom: ticks go right of the ruler line, labels beside them.
    for (int side = 0; side < 2; side++) {
      auto Y = [&](int d) { return side == 0 ? d : H - 1 - d; };
      display.drawFastVLine(rulerX, min(Y(0), Y(vLen)), vLen, GxEPD_BLACK);
      for (int d = 0; d <= vLen; d += 10) {
        int len = d % 50 == 0 ? 26 : 12;
        display.fillRect(rulerX, Y(d) - (side ? 1 : 0), len, 2, GxEPD_BLACK);
        if (d % 20 == 0) label(d, rulerX + 32, Y(d) - 8, 'l');
      }
    }

    // Box at the margins being tried: 2 px border plus thick corner marks.
    int bx = m[0], by = m[1], bw = W - m[0] - m[2], bh = H - m[1] - m[3];
    if (bw > 10 && bh > 10) {
      display.drawRect(bx, by, bw, bh, GxEPD_BLACK);
      display.drawRect(bx + 1, by + 1, bw - 2, bh - 2, GxEPD_BLACK);
      const int L = 40, T = 5;
      display.fillRect(bx, by, L, T, GxEPD_BLACK);
      display.fillRect(bx, by, T, L, GxEPD_BLACK);
      display.fillRect(bx + bw - L, by, L, T, GxEPD_BLACK);
      display.fillRect(bx + bw - T, by, T, L, GxEPD_BLACK);
      display.fillRect(bx, by + bh - T, L, T, GxEPD_BLACK);
      display.fillRect(bx, by + bh - L, T, L, GxEPD_BLACK);
      display.fillRect(bx + bw - L, by + bh - T, L, T, GxEPD_BLACK);
      display.fillRect(bx + bw - T, by + bh - L, T, L, GxEPD_BLACK);
    }

    // Instructions in the middle band.
    const GFXfont *f = &FreeSans12pt7b;
    int y = H / 2 - 30;
    int cx = W / 2;
    drawLine("Calibrating visible area", cx, y, &FreeSansBold12pt7b, 1, 'c', GxEPD_BLACK);
    y += f->yAdvance + 4;
    drawLine("Each ruler counts pixels in from its edge.", cx, y, &FreeSans9pt7b, 1, 'c', GxEPD_BLACK);
    y += 22;
    drawLine("Adjust until all 4 sides of the box just show.", cx, y, &FreeSans9pt7b, 1, 'c', GxEPD_BLACK);
    y += 22;
    drawLine("http://eink.local/calibrate", cx, y, &FreeSans9pt7b, 1, 'c', GxEPD_BLACK);
    y += 30;
    String now = "left " + String(m[0]) + "  top " + String(m[1]) + "  right " + String(m[2]) + "  bottom " + String(m[3]);
    drawLine(now, cx, y, &FreeSansBold9pt7b, 1, 'c', GxEPD_BLACK);
  } while (display.nextPage());
  display.powerOff();
  Serial.printf("[display] calibration %d %d %d %d\n", m[0], m[1], m[2], m[3]);
}
