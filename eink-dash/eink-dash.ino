// eink-dash: a conky-style, config-driven dashboard for the Waveshare 7.5"
// e-paper (GDEY075T7, 800x480) on the Waveshare ESP32 driver board.
//
// The layout and data sources live in /config.txt on the ESP32's flash and
// are edited from a web page the board serves (http://eink.local/).
// See README.md for the config syntax.

#include <LittleFS.h>
#include <WiFi.h>
#include <time.h>

#include "app.h"
#include "default_config.h"
#include "render.h"
#include "sources.h"
#include "tmpl.h"
#include "web.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

static const char *CONFIG_PATH = "/config.txt";
static const char *AP_NAME = "eink-setup";

Config gConfig;
String gConfigText;
String gConfigErrors;

static bool apMode = false;
static bool refreshRequested = true;
static bool fullRequested = true;
static bool restartPending = false;
static uint32_t restartAt = 0;
static int partialCount = 0;
static time_t nextDrawEpoch = 0;
static uint32_t lastDrawMs = 0;

static bool calActive = false, calDirty = false, calShown = false;
static uint32_t calStart = 0;
static int calM[4] = {0, 0, 0, 0};
static const uint32_t CAL_TIMEOUT_MS = 10 * 60 * 1000;

// ---------------------------------------------------------------- config ---

static String readConfigFile() {
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return String();
  String s = f.readString();
  f.close();
  return s;
}

static bool writeConfigFile(const String &text) {
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) return false;
  size_t n = f.print(text);
  f.close();
  return n == text.length();
}

static void applyTime() {
  configTzTime(gConfig.tz.c_str(), gConfig.ntp.c_str(), "time.nist.gov");
}

bool applyConfigText(const String &text, String &errors) {
  Config next;
  if (!parseConfig(text, next, errors)) return false;
  if (!writeConfigFile(text)) {
    errors = "could not write to flash";
    return false;
  }
  bool wifiChanged = next.wifiSsid != gConfig.wifiSsid || next.wifiPass != gConfig.wifiPass;
  bool tzChanged = next.tz != gConfig.tz || next.ntp != gConfig.ntp;
  gConfig = next;
  gConfigText = text;
  gConfigErrors = "";
  calActive = false;
  sourcesConfigure(gConfig.sources);
  if (tzChanged) applyTime();
  if (wifiChanged) {
    Serial.println("[config] wifi changed, restarting");
    restartPending = true;
    restartAt = millis();
  }
  requestRefresh(true);
  Serial.println("[config] applied new config");
  return true;
}

void requestRefresh(bool full) {
  refreshRequested = true;
  fullRequested = fullRequested || full;
}

void calibrationShow(const int m[4]) {
  for (int i = 0; i < 4; i++) calM[i] = m[i];
  if (!calActive) calShown = false;  // first calibration draw is a full refresh
  calActive = true;
  calDirty = true;
  calStart = millis();
}

void calibrationEnd() {
  if (!calActive) return;
  calActive = false;
  requestRefresh(true);
}

bool calibrationActive() { return calActive; }
const int *calibrationMargins() { return calActive ? calM : gConfig.margin; }

static void loadConfig() {
  gConfigText = readConfigFile();
  if (gConfigText.length() == 0) {
    Serial.println("[config] no config on flash, writing default");
    gConfigText = DEFAULT_CONFIG;
    writeConfigFile(gConfigText);
  }
  // A stored config with errors still runs; bad lines are skipped and listed.
  parseConfig(gConfigText, gConfig, gConfigErrors);
  if (gConfigErrors.length()) Serial.print("[config] errors:\n" + gConfigErrors);
}

// ------------------------------------------------------------------ wifi ---

static void startWifi() {
  String ssid = gConfig.wifiSsid.length() ? gConfig.wifiSsid : String(WIFI_SSID);
  String pass = gConfig.wifiSsid.length() ? gConfig.wifiPass : String(WIFI_PASS);

  if (ssid.length()) {
    Serial.printf("[wifi] connecting to %s", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("eink");
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setSleep(false);  // power saving makes the web editor lag by seconds
      Serial.printf("[wifi] connected, IP %s  ->  http://%s/  or  http://eink.local/\n",
                    WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());
      return;
    }
    Serial.println("[wifi] could not connect");
  }

  // Fall back to our own access point so the config can still be edited.
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_NAME);
  Serial.printf("[wifi] setup mode: join Wi-Fi '%s' and open http://%s/\n", AP_NAME,
                WiFi.softAPIP().toString().c_str());
  String why = ssid.length() ? "Could not connect to \"" + ssid + "\".\n\n" : String("No Wi-Fi configured.\n\n");
  renderMessage(gConfig, "Wi-Fi setup",
                why +
                "1. Join the Wi-Fi network \"" + AP_NAME + "\"\n"
                "2. Open http://192.168.4.1/\n"
                "3. Add a line:  wifi \"your-network\" \"password\"\n"
                "4. Press Save. The display restarts and connects.");
}

static String footerNotice() {
  String n;
  if (apMode) n = "Setup mode: join Wi-Fi \"" + String(AP_NAME) + "\", open http://192.168.4.1/\n";
  if (gConfigErrors.length()) n += "Config errors (see web editor):\n" + gConfigErrors;
  n.trim();
  return n;
}

// ------------------------------------------------------------ main loop ---

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[eink-dash] starting");

  displayInit();
  if (!LittleFS.begin(true)) Serial.println("[fs] LittleFS mount failed");
  loadConfig();
  startWifi();
  applyTime();
  sourcesConfigure(gConfig.sources);
  webBegin();

  if (!apMode) {
    Serial.print("[time] waiting for NTP");
    for (int i = 0; i < 20 && !timeValid(); i++) {
      delay(500);
      Serial.print('.');
    }
    Serial.println(timeValid() ? " ok" : " not yet");
  }
}

void loop() {
  webLoop();

  if (restartPending && millis() - restartAt > 1500) ESP.restart();

  if (calActive) {
    if (millis() - calStart > CAL_TIMEOUT_MS) {
      Serial.println("[calibrate] timed out");
      calibrationEnd();
    } else {
      if (calDirty) {
        renderCalibration(gConfig, calM, !calShown);
        calShown = true;
        calDirty = false;
      }
      return;  // dashboard paused while calibrating
    }
  }

  bool force = refreshRequested;
  if (!apMode && sourcesPoll(force)) refreshRequested = true;
  // In setup mode, keep the setup instructions on screen until Wi-Fi is configured.
  if (apMode && !gConfig.wifiSsid.length()) {
    refreshRequested = false;
    return;
  }

  // Redraw on wall-clock multiples of `redraw` (e.g. on the minute) once the
  // clock is set; before that, every `redraw` interval.
  time_t now = time(nullptr);
  bool due = refreshRequested;
  if (timeValid()) due = due || now >= nextDrawEpoch;
  else due = due || millis() - lastDrawMs >= gConfig.redrawMs;
  if (!due) return;

  bool full = fullRequested || partialCount >= gConfig.fullRefreshEvery;
  renderAll(gConfig, footerNotice(), full);
  partialCount = full ? 0 : partialCount + 1;
  refreshRequested = false;
  fullRequested = false;
  lastDrawMs = millis();
  uint32_t step = max<uint32_t>(10, gConfig.redrawMs / 1000);
  now = time(nullptr);
  nextDrawEpoch = (now / step + 1) * step;
}
