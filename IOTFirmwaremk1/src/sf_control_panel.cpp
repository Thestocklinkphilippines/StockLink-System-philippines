#include "sf_control_panel.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

#include "sf_actuators.h"
#include "sf_config.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_sensors.h"
#include "sf_storage.h"
#include "sf_utils.h"

namespace {

LiquidCrystal_I2C* gLcd = nullptr;
ControlPanelState gState = STATE_HOME;
ControlPanelState gPrevState = STATE_HOME;

enum NumericEntryTarget {
  ENTRY_NONE,
  ENTRY_MANUAL_FEED,
  ENTRY_SCHEDULE_AMOUNT,
};

unsigned long gLastDrawMs = 0;
unsigned long gFeedingStartedMs = 0;
unsigned long gFeedingDurationMs = 0;
float gFeedInputKg = 0.10f;

NumericEntryTarget gEntryTarget = ENTRY_NONE;
char gEntryBuffer[16] = {'\0'};
int gEntryLength = 0;
float gEntryMinKg = 0.05f;
float gEntryMaxKg = DEFAULT_MAX_SINGLE_FEED_KG;
int gEntryDecimals = 2;

int gMenuIndex = 0;
int gScheduleIndex = 0;
int gSelectedScheduleId = -1;
bool gSelectedScheduleEnabled = false;
float gSelectedScheduleAmountKg = 0.0f;

int gSettingsIndex = 0;
int gThresholdIndex = 0;

int gTimeEditHour = 0;
int gTimeEditMinute = 0;

bool gAckLowFeed = false;
bool gAckLowWater = false;
bool gAckPowerOut = false;

static const char* kMenuItems[] = {
    "Manual Feed",
    "Schedules",
    "Status",
    "Alerts",
    "Settings",
};

static const int kMenuCount = (int)(sizeof(kMenuItems) / sizeof(kMenuItems[0]));

float getCfg(JsonVariant cfg, const char* key, float fallback) {
  if (cfg.isNull() || !cfg.containsKey(key)) return fallback;
  return cfg[key] | fallback;
}

void clearAndPrint4(const String& l1, const String& l2, const String& l3, const String& l4) {
  if (!gLcd) return;
  gLcd->clear();
  gLcd->setCursor(0, 0);
  gLcd->print(l1.substring(0, 20));
  gLcd->setCursor(0, 1);
  gLcd->print(l2.substring(0, 20));
  gLcd->setCursor(0, 2);
  gLcd->print(l3.substring(0, 20));
  gLcd->setCursor(0, 3);
  gLcd->print(l4.substring(0, 20));
}

String fmtPct(const char* label, float v) {
  char b[24];
  snprintf(b, sizeof(b), "%s:%3d%%", label, (int)clampf(v, 0.0f, 100.0f));
  return String(b);
}

String nowHm() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char b[8];
  strftime(b, sizeof(b), "%H:%M", &t);
  return String(b);
}

String fmtKg(float value, int decimals = 2) {
  char b[24];
  snprintf(b, sizeof(b), "%.*fkg", decimals, value);
  return String(b);
}

void trimEntryTrailingZeroes() {
  while (gEntryLength > 0 && gEntryBuffer[gEntryLength - 1] == '0') {
    gEntryLength--;
    gEntryBuffer[gEntryLength] = '\0';
  }
  if (gEntryLength > 0 && gEntryBuffer[gEntryLength - 1] == '.') {
    gEntryLength--;
    gEntryBuffer[gEntryLength] = '\0';
  }
  if (gEntryLength == 0) {
    gEntryBuffer[gEntryLength++] = '0';
    gEntryBuffer[gEntryLength] = '\0';
  }
}

void setEntryFromValue(float value, int decimals = 2) {
  gEntryDecimals = decimals;
  snprintf(gEntryBuffer, sizeof(gEntryBuffer), "%.*f", decimals, value);
  gEntryLength = (int)strlen(gEntryBuffer);
  trimEntryTrailingZeroes();
}

void beginManualFeedEntry() {
  gEntryTarget = ENTRY_MANUAL_FEED;
  gEntryMinKg = 0.05f;
  gEntryMaxKg = DEFAULT_MAX_SINGLE_FEED_KG;
  setEntryFromValue(gFeedInputKg, 2);
}

float parseEntryValue() {
  if (gEntryLength <= 0) return 0.0f;
  return strtof(gEntryBuffer, nullptr);
}

void appendEntryChar(char key) {
  if (gEntryLength >= (int)sizeof(gEntryBuffer) - 1) return;

  if (key >= '0' && key <= '9') {
    if (gEntryLength == 1 && gEntryBuffer[0] == '0' && strchr(gEntryBuffer, '.') == nullptr) {
      gEntryLength = 0;
    }
    gEntryBuffer[gEntryLength++] = key;
    gEntryBuffer[gEntryLength] = '\0';
    return;
  }

  if (key == '*') {
    if (strchr(gEntryBuffer, '.') == nullptr) {
      if (gEntryLength == 0) {
        gEntryBuffer[gEntryLength++] = '0';
      }
      gEntryBuffer[gEntryLength++] = '.';
      gEntryBuffer[gEntryLength] = '\0';
    }
    return;
  }
}

void deleteEntryChar() {
  if (gEntryLength <= 0) return;
  gEntryLength--;
  gEntryBuffer[gEntryLength] = '\0';
  if (gEntryLength == 0) {
    gEntryBuffer[gEntryLength++] = '0';
    gEntryBuffer[gEntryLength] = '\0';
  }
}

void clearEntry() {
  gEntryLength = 0;
  gEntryBuffer[0] = '\0';
}

bool setScheduleAmountByIndex(int wantedIndex, float amountKg) {
  if (wantedIndex < 0) return false;
  String cfgStr = loadLocalConfig();
  DynamicJsonDocument d(8192);
  DeserializationError err = deserializeJson(d, cfgStr);
  if (err) return false;

  JsonArray arr = d["schedules"].as<JsonArray>();
  if (arr.isNull()) return false;

  int idx = 0;
  for (JsonVariant s : arr) {
    if (idx == wantedIndex) {
      s["feeding_amount_kg"] = amountKg;
      d["last_updated"] = getUtcIsoNow();
      d["updated_by"] = "esp32";
      String out;
      serializeJson(d, out);
      saveLocalConfig(out);
      return true;
    }
    idx++;
  }

  return false;
}

float getScheduleAmountLimitKg(JsonVariant cfg) {
  float maxCap = getCfg(cfg, "max_feeds_capacity_kg", DEFAULT_MAX_FEEDS_CAPACITY_KG);
  if (maxCap <= 0.0f) maxCap = DEFAULT_MAX_FEEDS_CAPACITY_KG;
  return maxCap;
}

void beginScheduleAmountEntry(JsonVariant cfg) {
  gEntryTarget = ENTRY_SCHEDULE_AMOUNT;
  gEntryMinKg = 0.05f;
  gEntryMaxKg = getScheduleAmountLimitKg(cfg);
  setEntryFromValue(gSelectedScheduleAmountKg > 0.0f ? gSelectedScheduleAmountKg : 0.10f, 2);
}

int getActiveAlerts(JsonVariant cfg, bool* lowFeed, bool* lowWater, bool* powerOut) {
  float feedPct = getFeederLevelPct(cfg);
  float waterPct = getWaterLevelPct(cfg);
  float feedLow = getCfg(cfg, "feeder_low_threshold_pct", DEFAULT_FEEDER_LOW_THRESHOLD_PCT);
  float waterLow = getCfg(cfg, "water_low_threshold_pct", DEFAULT_WATER_LOW_THRESHOLD_PCT);

  bool lf = feedPct <= feedLow;
  bool lw = waterPct <= waterLow;
  bool po = !state.mainsPowerPresent;

  if (!lf) gAckLowFeed = false;
  if (!lw) gAckLowWater = false;
  if (!po) gAckPowerOut = false;

  if (lowFeed) *lowFeed = lf;
  if (lowWater) *lowWater = lw;
  if (powerOut) *powerOut = po;

  return (lf ? 1 : 0) + (lw ? 1 : 0) + (po ? 1 : 0);
}

void saveNumericConfig(const char* key, float value) {
  String cfgStr = loadLocalConfig();
  DynamicJsonDocument d(8192);
  DeserializationError err = deserializeJson(d, cfgStr);
  if (err) return;
  d[key] = value;
  d["last_updated"] = getUtcIsoNow();
  d["updated_by"] = "esp32";
  String out;
  serializeJson(d, out);
  saveLocalConfig(out);
}

void setScheduleEnabledByIndex(int wantedIndex, bool enabled) {
  if (wantedIndex < 0) return;
  String cfgStr = loadLocalConfig();
  DynamicJsonDocument d(8192);
  DeserializationError err = deserializeJson(d, cfgStr);
  if (err) return;

  JsonArray arr = d["schedules"].as<JsonArray>();
  if (arr.isNull()) return;

  int idx = 0;
  for (JsonVariant s : arr) {
    if (idx == wantedIndex) {
      s["enabled"] = enabled;
      d["last_updated"] = getUtcIsoNow();
      d["updated_by"] = "esp32";
      String out;
      serializeJson(d, out);
      saveLocalConfig(out);
      return;
    }
    idx++;
  }
}

void drawHome(JsonVariant cfg) {
  String line1 = "Smart Feeder  " + nowHm();

  String nextSched = "Next: --:--";
  if (cfg.containsKey("schedules")) {
    JsonArray arr = cfg["schedules"].as<JsonArray>();
    if (!arr.isNull()) {
      for (JsonVariant s : arr) {
        bool enabled = s["enabled"] | false;
        if (enabled) {
          const char* t = s["time"] | "--:--";
          nextSched = String("Next: ") + t;
          break;
        }
      }
    }
  }

  float feed = getFeederLevelPct(cfg);
  String line3 = fmtPct("Feed", feed);
  String line4 = "A=Menu C=Feed";
  clearAndPrint4(line1, nextSched, line3, line4);
}

void drawMenu() {
  int base = gMenuIndex;
  if (base > kMenuCount - 3) base = kMenuCount - 3;
  if (base < 0) base = 0;

  String l1 = "Menu";
  String l2 = String(gMenuIndex == base ? ">" : " ") + kMenuItems[base];
  String l3 = String(gMenuIndex == base + 1 ? ">" : " ") + kMenuItems[base + 1];
  String l4 = String(gMenuIndex == base + 2 ? ">" : " ") + kMenuItems[base + 2];
  clearAndPrint4(l1, l2, l3, l4);
}

void drawNumericEntry(const char* title, const char* hintLine4) {
  String line2 = String("[") + gEntryBuffer + " kg]";
  clearAndPrint4(title, line2, "0-9 *=. #=Del", hintLine4);
}

void drawManualInput() {
  drawNumericEntry("Manual Feed", "C=Next D=Back");
}

void drawManualConfirm() {
  char b[24];
  snprintf(b, sizeof(b), "Dispense %.2fkg?", gFeedInputKg);
  clearAndPrint4("Confirm Feed", String(b), "C=Yes", "D=Back");
}

void drawFeedingProgress() {
  unsigned long elapsed = millis() - gFeedingStartedMs;
  float pct = (gFeedingDurationMs > 0) ? ((100.0f * elapsed) / (float)gFeedingDurationMs) : 100.0f;
  int bars = (int)clampf(pct / 12.5f, 0.0f, 8.0f);
  String bar = "[";
  for (int i = 0; i < 8; i++) {
    bar += (i < bars) ? "=" : " ";
  }
  bar += "]";
  clearAndPrint4("Feeding...", "Please wait", bar, "D=Back");
}

void drawSchedules(JsonArray schedules) {
  clearAndPrint4("Schedules", "(none)", "", "D=Back");
  if (schedules.isNull() || schedules.size() == 0) return;

  int size = (int)schedules.size();
  if (gScheduleIndex < 0) gScheduleIndex = 0;
  if (gScheduleIndex >= size) gScheduleIndex = size - 1;

  int base = gScheduleIndex;
  if (base > size - 3) base = size - 3;
  if (base < 0) base = 0;

  String rows[3] = {"", "", ""};
  for (int i = 0; i < 3; i++) {
    int idx = base + i;
    if (idx >= size) continue;
    JsonVariant s = schedules[idx];
    const char* t = s["time"] | "--:--";
    bool en = s["enabled"] | false;
    rows[i] = String(idx == gScheduleIndex ? ">" : " ") + t + (en ? " ON" : " OFF");
  }

  clearAndPrint4("Schedules", rows[0], rows[1], rows[2]);
}

void drawScheduleDetail() {
  clearAndPrint4("Schedule", String("ID:") + gSelectedScheduleId,
                 String("Now ") + (gSelectedScheduleEnabled ? "ON" : "OFF") + " " + fmtKg(gSelectedScheduleAmountKg),
                 "A=Amt C=Toggle D=Back");
}

void drawScheduleAmountInput() {
  drawNumericEntry("Edit Schedule", "C=Save D=Back");
}

void drawStatus(JsonVariant cfg) {
  float feed = getFeederLevelPct(cfg);
  float water = getWaterLevelPct(cfg);
  String pwr = state.mainsPowerPresent ? "MAIN" : "UPS";
  clearAndPrint4(fmtPct("Feed", feed), fmtPct("Drink", water), String("Power:") + pwr, "D=Back");
}

void drawAlerts(JsonVariant cfg) {
  bool lf = false;
  bool lw = false;
  bool po = false;
  int count = getActiveAlerts(cfg, &lf, &lw, &po);

  if (count == 0) {
    clearAndPrint4("Alerts", "No active alerts", "", "D=Back");
    return;
  }

  String l2 = "";
  String l3 = "";
  if (lf) l2 = String(">Low Feed") + (gAckLowFeed ? " ACK" : "");
  if (lw && l2.length() == 0) l2 = String(">Low Water") + (gAckLowWater ? " ACK" : "");
  if (po && l2.length() == 0) l2 = String(">Power Out") + (gAckPowerOut ? " ACK" : "");

  if (lf && l2.indexOf("Low Feed") < 0) l3 = String(" Low Feed") + (gAckLowFeed ? " ACK" : "");
  if (lw && l2.indexOf("Low Water") < 0 && l3.length() == 0) l3 = String(" Low Water") + (gAckLowWater ? " ACK" : "");
  if (po && l2.indexOf("Power Out") < 0 && l3.length() == 0) l3 = String(" Power Out") + (gAckPowerOut ? " ACK" : "");

  clearAndPrint4("Alerts", l2, l3, "C=Ack D=Back");
}

void drawAlertAck() {
  clearAndPrint4("Alert Ack", "Acknowledged", "", "D=Back");
}

void drawSettingsMenu() {
  String l2 = String(gSettingsIndex == 0 ? ">" : " ") + "Thresholds";
  String l3 = String(gSettingsIndex == 1 ? ">" : " ") + "Time Setup";
  clearAndPrint4("Settings", l2, l3, "D=Back");
}

void drawThresholdEditor(JsonVariant cfg) {
  float feedLow = getCfg(cfg, "feeder_low_threshold_pct", DEFAULT_FEEDER_LOW_THRESHOLD_PCT);
  float waterLow = getCfg(cfg, "water_low_threshold_pct", DEFAULT_WATER_LOW_THRESHOLD_PCT);

  char b2[24];
  char b3[24];
  snprintf(b2, sizeof(b2), "%cFeeder Low: %2d%%", gThresholdIndex == 0 ? '>' : ' ', (int)feedLow);
  snprintf(b3, sizeof(b3), "%cWater  Low: %2d%%", gThresholdIndex == 1 ? '>' : ' ', (int)waterLow);
  clearAndPrint4("Thresholds", String(b2), String(b3), "A/B +/- C=Next D=Back");
}

void drawTimeSetup() {
  char b[24];
  snprintf(b, sizeof(b), "%02d:%02d", gTimeEditHour, gTimeEditMinute);
  clearAndPrint4("Time Setup", String("Set ") + b, "A/B +/-  C=Save", "D=Back");
}

void transition(ControlPanelState next) {
  gPrevState = gState;
  gState = next;
}

void handleHomeKey(char key) {
  if (key == 'A') transition(STATE_MENU);
  if (key == 'C') {
    beginManualFeedEntry();
    transition(STATE_MANUAL_FEED_INPUT);
  }
}

void handleMenuKey(char key) {
  if (key == 'A') gMenuIndex = (gMenuIndex - 1 + kMenuCount) % kMenuCount;
  if (key == 'B') gMenuIndex = (gMenuIndex + 1) % kMenuCount;
  if (key == 'D') transition(STATE_HOME);

  if (key == 'C') {
    if (gMenuIndex == 0) {
      beginManualFeedEntry();
      transition(STATE_MANUAL_FEED_INPUT);
    }
    if (gMenuIndex == 1) transition(STATE_SCHEDULE_LIST);
    if (gMenuIndex == 2) transition(STATE_STATUS_VIEW);
    if (gMenuIndex == 3) transition(STATE_ALERTS_LIST);
    if (gMenuIndex == 4) transition(STATE_SETTINGS_MENU);
  }
}

void handleManualInputKey(char key) {
  if (key == 'A') {
    clearEntry();
    return;
  }
  if (key == 'B' || key == '#') {
    deleteEntryChar();
    return;
  }
  if (key == 'D') {
    transition(STATE_HOME);
    return;
  }
  if (key == 'C') {
    float entered = clampf(parseEntryValue(), gEntryMinKg, gEntryMaxKg);
    gFeedInputKg = entered;
    setEntryFromValue(gFeedInputKg, 2);
    transition(STATE_MANUAL_FEED_CONFIRM);
    return;
  }

  appendEntryChar(key);
}

void handleManualConfirmKey(char key, JsonVariant cfg) {
  if (key == 'D') {
    transition(STATE_MANUAL_FEED_INPUT);
    return;
  }
  if (key != 'C') return;

  if (isFeedSufficient(gFeedInputKg, cfg)) {
    gFeedingStartedMs = millis();
    gFeedingDurationMs = computeFeedMotorRunMs(gFeedInputKg, cfg);
    transition(STATE_FEEDING_PROGRESS);
    // Force an immediate LCD update before the blocking motor routine starts.
    drawFeedingProgress();
    gLastDrawMs = millis();
    gPrevState = gState;
    dispenseFeed(gFeedInputKg, cfg);
  } else {
    LOG_WARN("UI manual feed blocked: insufficient feed");
    transition(STATE_MANUAL_FEED_CONFIRM);
  }
}

void handleFeedingProgressKey(char key) {
  if (millis() - gFeedingStartedMs > 1200UL || key == 'D') {
    transition(STATE_HOME);
  }
}

void handleScheduleListKey(char key, JsonArray schedules) {
  int size = schedules.isNull() ? 0 : (int)schedules.size();
  if (size <= 0) {
    if (key == 'D') transition(STATE_MENU);
    return;
  }

  if (key == 'A') gScheduleIndex = (gScheduleIndex - 1 + size) % size;
  if (key == 'B') gScheduleIndex = (gScheduleIndex + 1) % size;
  if (key == 'D') transition(STATE_MENU);

  if (key == 'C') {
    JsonVariant s = schedules[gScheduleIndex];
    gSelectedScheduleId = s["id"] | -1;
    gSelectedScheduleEnabled = s["enabled"] | false;
    gSelectedScheduleAmountKg = s["feeding_amount_kg"] | 0.0f;
    transition(STATE_SCHEDULE_DETAIL);
  }
}

void handleScheduleDetailKey(char key, JsonVariant cfg) {
  if (key == 'D') {
    transition(STATE_SCHEDULE_LIST);
    return;
  }
  if (key == 'A') {
    beginScheduleAmountEntry(cfg);
    transition(STATE_SCHEDULE_AMOUNT_INPUT);
    return;
  }
  if (key == 'C') {
    gSelectedScheduleEnabled = !gSelectedScheduleEnabled;
    setScheduleEnabledByIndex(gScheduleIndex, gSelectedScheduleEnabled);
    transition(STATE_SCHEDULE_LIST);
  }
}

void handleScheduleAmountInputKey(char key, JsonVariant cfg) {
  if (key == 'A') {
    clearEntry();
    return;
  }
  if (key == 'B' || key == '#') {
    deleteEntryChar();
    return;
  }
  if (key == 'D') {
    transition(STATE_SCHEDULE_DETAIL);
    return;
  }
  if (key == 'C') {
    float entered = clampf(parseEntryValue(), gEntryMinKg, gEntryMaxKg);
    if (entered < gEntryMinKg) entered = gEntryMinKg;
    if (setScheduleAmountByIndex(gScheduleIndex, entered)) {
      gSelectedScheduleAmountKg = entered;
    }
    setEntryFromValue(gSelectedScheduleAmountKg, 2);
    transition(STATE_SCHEDULE_DETAIL);
    return;
  }

  (void)cfg;
  appendEntryChar(key);
}

void handleStatusKey(char key) {
  if (key == 'D') transition(STATE_MENU);
}

void handleAlertsKey(char key, JsonVariant cfg) {
  if (key == 'D') {
    transition(STATE_MENU);
    return;
  }
  if (key == 'C') {
    bool lf = false;
    bool lw = false;
    bool po = false;
    getActiveAlerts(cfg, &lf, &lw, &po);
    if (lf) gAckLowFeed = true;
    if (lw) gAckLowWater = true;
    if (po) gAckPowerOut = true;
    transition(STATE_ALERT_ACK);
  }
}

void handleAlertAckKey(char key) {
  if (key == 'D' || key == 'C') transition(STATE_ALERTS_LIST);
}

void handleSettingsMenuKey(char key) {
  if (key == 'A' || key == 'B') {
    gSettingsIndex = (gSettingsIndex == 0) ? 1 : 0;
  }
  if (key == 'D') transition(STATE_MENU);
  if (key == 'C') {
    if (gSettingsIndex == 0) transition(STATE_SETTINGS_THRESHOLD);
    if (gSettingsIndex == 1) {
      time_t now = time(nullptr);
      struct tm t;
      localtime_r(&now, &t);
      gTimeEditHour = t.tm_hour;
      gTimeEditMinute = t.tm_min;
      transition(STATE_SETTINGS_TIME);
    }
  }
}

void handleThresholdKey(char key, JsonVariant cfg) {
  float feedLow = getCfg(cfg, "feeder_low_threshold_pct", DEFAULT_FEEDER_LOW_THRESHOLD_PCT);
  float waterLow = getCfg(cfg, "water_low_threshold_pct", DEFAULT_WATER_LOW_THRESHOLD_PCT);

  if (key == 'C') {
    gThresholdIndex = (gThresholdIndex + 1) % 2;
    return;
  }
  if (key == 'D') {
    transition(STATE_SETTINGS_MENU);
    return;
  }

  float step = 1.0f;
  if (key == 'A') {
    if (gThresholdIndex == 0) saveNumericConfig("feeder_low_threshold_pct", clampf(feedLow + step, 1.0f, 99.0f));
    if (gThresholdIndex == 1) saveNumericConfig("water_low_threshold_pct", clampf(waterLow + step, 1.0f, 99.0f));
  }
  if (key == 'B') {
    if (gThresholdIndex == 0) saveNumericConfig("feeder_low_threshold_pct", clampf(feedLow - step, 1.0f, 99.0f));
    if (gThresholdIndex == 1) saveNumericConfig("water_low_threshold_pct", clampf(waterLow - step, 1.0f, 99.0f));
  }
}

void handleTimeSetupKey(char key) {
  if (key == 'D') {
    transition(STATE_SETTINGS_MENU);
    return;
  }
  if (key == 'A') {
    gTimeEditMinute++;
    if (gTimeEditMinute >= 60) {
      gTimeEditMinute = 0;
      gTimeEditHour = (gTimeEditHour + 1) % 24;
    }
  }
  if (key == 'B') {
    gTimeEditMinute--;
    if (gTimeEditMinute < 0) {
      gTimeEditMinute = 59;
      gTimeEditHour = (gTimeEditHour - 1 + 24) % 24;
    }
  }
  if (key == 'C') {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    t.tm_hour = gTimeEditHour;
    t.tm_min = gTimeEditMinute;
    t.tm_sec = 0;
    time_t edited = mktime(&t);
    struct timeval tv;
    tv.tv_sec = edited;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    transition(STATE_SETTINGS_MENU);
  }
}

void drawCurrent(JsonVariant cfg, JsonArray schedules) {
  switch (gState) {
    case STATE_HOME:
      drawHome(cfg);
      break;
    case STATE_MENU:
      drawMenu();
      break;
    case STATE_MANUAL_FEED_INPUT:
      drawManualInput();
      break;
    case STATE_MANUAL_FEED_CONFIRM:
      drawManualConfirm();
      break;
    case STATE_FEEDING_PROGRESS:
      drawFeedingProgress();
      break;
    case STATE_SCHEDULE_LIST:
      drawSchedules(schedules);
      break;
    case STATE_SCHEDULE_DETAIL:
      drawScheduleDetail();
      break;
    case STATE_SCHEDULE_AMOUNT_INPUT:
      drawScheduleAmountInput();
      break;
    case STATE_STATUS_VIEW:
      drawStatus(cfg);
      break;
    case STATE_ALERTS_LIST:
      drawAlerts(cfg);
      break;
    case STATE_ALERT_ACK:
      drawAlertAck();
      break;
    case STATE_SETTINGS_MENU:
      drawSettingsMenu();
      break;
    case STATE_SETTINGS_THRESHOLD:
      drawThresholdEditor(cfg);
      break;
    case STATE_SETTINGS_TIME:
      drawTimeSetup();
      break;
  }
}

void handleStateKey(char key, JsonVariant cfg, JsonArray schedules) {
  if (key == '\0') return;

  switch (gState) {
    case STATE_HOME:
      handleHomeKey(key);
      break;
    case STATE_MENU:
      handleMenuKey(key);
      break;
    case STATE_MANUAL_FEED_INPUT:
      handleManualInputKey(key);
      break;
    case STATE_MANUAL_FEED_CONFIRM:
      handleManualConfirmKey(key, cfg);
      break;
    case STATE_FEEDING_PROGRESS:
      handleFeedingProgressKey(key);
      break;
    case STATE_SCHEDULE_LIST:
      handleScheduleListKey(key, schedules);
      break;
    case STATE_SCHEDULE_DETAIL:
      handleScheduleDetailKey(key, cfg);
      break;
    case STATE_SCHEDULE_AMOUNT_INPUT:
      handleScheduleAmountInputKey(key, cfg);
      break;
    case STATE_STATUS_VIEW:
      handleStatusKey(key);
      break;
    case STATE_ALERTS_LIST:
      handleAlertsKey(key, cfg);
      break;
    case STATE_ALERT_ACK:
      handleAlertAckKey(key);
      break;
    case STATE_SETTINGS_MENU:
      handleSettingsMenuKey(key);
      break;
    case STATE_SETTINGS_THRESHOLD:
      handleThresholdKey(key, cfg);
      break;
    case STATE_SETTINGS_TIME:
      handleTimeSetupKey(key);
      break;
  }
}

}  // namespace

void initControlPanel(LiquidCrystal_I2C* lcd) {
  gLcd = lcd;
  gState = STATE_HOME;
  gPrevState = STATE_HOME;
  gLastDrawMs = 0;
}

void updateControlPanel(JsonVariant cfg, JsonArray schedules) {
  if (!gLcd) return;

  char key = consumeKeypadKeyEvent();
  handleStateKey(key, cfg, schedules);

  bool stateChanged = (gState != gPrevState);
  bool redrawPeriodic = (millis() - gLastDrawMs >= 500UL);
  if (stateChanged || redrawPeriodic) {
    drawCurrent(cfg, schedules);
    gLastDrawMs = millis();
    gPrevState = gState;
  }

  if (gState == STATE_FEEDING_PROGRESS && (millis() - gFeedingStartedMs > 1600UL)) {
    transition(STATE_HOME);
  }
}