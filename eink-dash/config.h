#pragma once
#include <Arduino.h>
#include <vector>

// Parsed form of the conky-style config file (see README.md for the syntax).

struct Header {
  String key, value;
};

struct SourceDef {
  String name;
  String url;
  uint32_t everyMs = 10 * 60 * 1000;
  std::vector<Header> headers;
};

enum WidgetType { W_TEXT, W_LINE, W_RECT, W_LIST };

struct Widget {
  WidgetType type;
  int line = 0;        // config line number, for error messages
  int n[4] = {0, 0, 0, 0};  // positional numbers (x, y, w/x2, h/y2)
  String font = "sans12";
  uint8_t size = 1;
  char align = 'l';    // l, c, r
  bool white = false;  // draw in white instead of black
  bool fill = false;   // rect: filled
  int radius = 0;      // rect: rounded corners
  int spacing = 0;     // list: pixels between rows (0 = font line height)
  int maxItems = 10;   // list: max rows
  String items;        // list: path to a JSON array
  String tmpl;         // text/list: template string
  String cond;         // if=/unless= path; empty = always draw
  bool condNegate = false;  // true for unless=
};

struct Config {
  String wifiSsid, wifiPass;  // optional, overrides secrets.h
  String tz = "UTC0";
  String ntp = "pool.ntp.org";
  uint32_t redrawMs = 60 * 1000;
  int fullRefreshEvery = 10;
  int rotation = 0;
  int margin[4] = {0, 0, 0, 0};  // left, top, right, bottom: pixels hidden by a frame
  int marginLine = 0;
  std::vector<SourceDef> sources;
  std::vector<Widget> widgets;
};

static const int SCREEN_W = 800, SCREEN_H = 480;

// Screen size for the config's rotation, and the visible part inside the margins.
inline int screenW(const Config &c) { return (c.rotation & 1) ? SCREEN_H : SCREEN_W; }
inline int screenH(const Config &c) { return (c.rotation & 1) ? SCREEN_W : SCREEN_H; }
inline int visibleW(const Config &c) { return screenW(c) - c.margin[0] - c.margin[2]; }
inline int visibleH(const Config &c) { return screenH(c) - c.margin[1] - c.margin[3]; }

// Parses `text` into `out`. Bad lines are skipped and described in `errors`
// (one per line, "line N: ..."). Returns true when there were no errors.
bool parseConfig(const String &text, Config &out, String &errors);
