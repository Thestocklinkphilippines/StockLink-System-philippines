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
#include "sf_pins.h"

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

// Backlight idle management
unsigned long gLastKeyActivityMs = 0;
bool gLcdBacklightOn = true;
static const unsigned long LCD_BACKLIGHT_TIMEOUT_MS = 5UL * 60UL * 1000UL; // 5 minutes

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
int gGrainIndex = 0;

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

static const char* kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

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

void setLcdBacklight(bool on) {
  gLcdBacklightOn = on;
  if (!gLcd) return;
  if (on) gLcd->backlight();
  else gLcd->noBacklight();
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

bool scheduleHasDay(JsonArrayConst days, const char* dayName) {
  if (days.isNull()) return false;
  for (JsonVariantConst day : days) {
    const char* value = day | "";
    if (String(value).equalsIgnoreCase(dayName)) return true;
  }
  return false;
}

String formatScheduleDays(JsonVariantConst schedule) {
  JsonArrayConst days = schedule["days"].as<JsonArrayConst>();

  String pattern = "[";
  for (const char* weekday : kWeekdays) {
    pattern += scheduleHasDay(days, weekday) ? '#' : ' ';
  }
  pattern += "]";
  return pattern;
}

String formatScheduleRow(JsonVariantConst schedule, bool selected) {
  static const int kLcdColumns = 20;

  const char* t = schedule["time"] | "--:--";
  bool en = schedule["enabled"] | false;

  String row = String(selected ? ">" : " ") + t + (en ? " ON" : " OFF");
  String pattern = formatScheduleDays(schedule);

  int pad = kLcdColumns - (int)row.length() - (int)pattern.length();
  if (pad < 1) pad = 1;
  for (int i = 0; i < pad; i++) {
    row += ' ';
  }
  row += pattern;
  return row;
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

void beginManualFeedEntry(JsonVariant cfg) {
  gEntryTarget = ENTRY_MANUAL_FEED;
  gEntryMinKg = 0.05f;
  gEntryMaxKg = getMaxSingleFeedKg(cfg);
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

  float batteryVoltage = getBatteryVoltageV(cfg);
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
  String line1 = "StockLink  " + nowHm();

  // Find the next scheduled feed time
  String nextSched = "Next: --:--";
  
  // Get current time
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int nowMin = (t.tm_hour * 60) + t.tm_min;
  int nowWday = t.tm_wday;
  
  if (cfg.containsKey("schedules")) {
    JsonArray arr = cfg["schedules"].as<JsonArray>();
    if (!arr.isNull()) {
      const int kMinPerDay = 24 * 60;
      int bestOffsetMin = 8 * kMinPerDay;
      int bestSchedMin = -1;

      for (JsonVariant s : arr) {
        bool enabled = s["enabled"] | false;
        if (!enabled) continue;

        const char* timeStr = s["time"] | "--:--";
        if (strlen(timeStr) != 5 || timeStr[2] != ':') continue;
        if (timeStr[0] < '0' || timeStr[0] > '9' ||
            timeStr[1] < '0' || timeStr[1] > '9' ||
            timeStr[3] < '0' || timeStr[3] > '9' ||
            timeStr[4] < '0' || timeStr[4] > '9') {
          continue;
        }

        int schedHour = ((timeStr[0] - '0') * 10) + (timeStr[1] - '0');
        int schedMinute = ((timeStr[3] - '0') * 10) + (timeStr[4] - '0');
        if (schedHour < 0 || schedHour >= 24 || schedMinute < 0 || schedMinute >= 60) continue;

        int schedMinOfDay = (schedHour * 60) + schedMinute;
        JsonArrayConst days = s["days"].as<JsonArrayConst>();
        bool hasDays = !days.isNull() && days.size() > 0;

        if (!hasDays) {
          int offset = schedMinOfDay - nowMin;
          if (offset < 0) offset += kMinPerDay;
          if (offset < bestOffsetMin) {
            bestOffsetMin = offset;
            bestSchedMin = schedMinOfDay;
          }
          continue;
        }

        for (int dayOffset = 0; dayOffset < 7; dayOffset++) {
          int weekdayIdx = (nowWday + dayOffset) % 7;
          if (!scheduleHasDay(days, kWeekdays[weekdayIdx])) continue;

          int offset = (dayOffset * kMinPerDay) + (schedMinOfDay - nowMin);
          if (offset < 0) {
            // Same day but earlier time is not upcoming.
            continue;
          }

          if (offset < bestOffsetMin) {
            bestOffsetMin = offset;
            bestSchedMin = schedMinOfDay;
          }
          break;
        }
      }

      if (bestSchedMin >= 0) {
        int chh = bestSchedMin / 60;
        int cmm = bestSchedMin % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "Next:%02d:%02d", chh, cmm);
        nextSched = String(buf);
      }
    }
  }

  float feedPct = getFeederLevelPct(cfg);
  float waterPct = getWaterLevelPct(cfg);

  float maxCapKg = getCfg(cfg, "max_feeds_capacity_kg", DEFAULT_MAX_FEEDS_CAPACITY_KG);
  if (maxCapKg <= 0.0f) maxCapKg = DEFAULT_MAX_FEEDS_CAPACITY_KG;

  float feedKg = clampf((feedPct / 100.0f) * maxCapKg, 0.0f, maxCapKg);

  char line3Buf[32];
  snprintf(line3Buf, sizeof(line3Buf), "F:%gkg W:%d%%", feedKg, (int)clampf(waterPct, 0.0f, 100.0f));
  String line3 = String(line3Buf);
  // Main overview: show battery value like other status indicators, controls on same line
  float batteryVoltage = getBatteryVoltageV(cfg);
  char battBuf[16];
  if (batteryVoltage >= 0.0f) {
    snprintf(battBuf, sizeof(battBuf), "B:%.1fV", batteryVoltage);
  } else {
    snprintf(battBuf, sizeof(battBuf), "B:-");
  }
  char line4buf[32];
  snprintf(line4buf, sizeof(line4buf), "%s A=Menu C=Feed", battBuf);
  String line4 = String(line4buf);
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
  char limitBuf[24];
  snprintf(limitBuf, sizeof(limitBuf), "Max %.2fkg", gEntryMaxKg);
  drawNumericEntry("Manual Feed", limitBuf);
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
    rows[i] = formatScheduleRow(s, idx == gScheduleIndex);
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
  float batteryVoltage = getBatteryVoltageV(cfg);
  char b3[32];
  if (batteryVoltage >= 0.0f) {
    snprintf(b3, sizeof(b3), "Power:%s B:%.1fV", pwr.c_str(), batteryVoltage);
  } else {
    snprintf(b3, sizeof(b3), "Power:%s B:-", pwr.c_str());
  }
  clearAndPrint4(fmtPct("Feed", feed), fmtPct("Drink", water), String(b3), "D=Back");
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
  String l3 = String(gSettingsIndex == 1 ? ">" : " ") + "Grain Type";
  String l4 = String(gSettingsIndex == 2 ? ">" : " ") + "Time Setup";
  clearAndPrint4("Settings", l2, l3, l4);
}

void drawGrainEditor(JsonVariant cfg) {
  int count = getGrainTypeCount(cfg);
  if (count <= 0) count = DEFAULT_GRAIN_TYPE_COUNT;
  if (count <= 0) {
    clearAndPrint4("Grain Type", "No options", "", "D=Back");
    return;
  }

  if (gGrainIndex < 0) gGrainIndex = 0;
  if (gGrainIndex >= count) gGrainIndex %= count;

  const char* selectedType = getGrainTypeNameByIndex(cfg, gGrainIndex);
  float selectedRate = getGrainTypeMsPerKgByIndex(cfg, gGrainIndex);

  String line2 = String(">") + selectedType;
  char rateBuf[24];
  snprintf(rateBuf, sizeof(rateBuf), "%.1f ms/kg", selectedRate);
  clearAndPrint4("Grain Type", line2, String(rateBuf), "A/B=Change D=Back");
}

void drawThresholdEditor(JsonVariant cfg) {
  float feedLow = getCfg(cfg, "feeder_low_threshold_pct", DEFAULT_FEEDER_LOW_THRESHOLD_PCT);
  float waterLow = getCfg(cfg, "water_low_threshold_pct", DEFAULT_WATER_LOW_THRESHOLD_PCT);
  float batteryShutdown = getCfg(cfg, "low_battery_shutdown_v", DEFAULT_LOW_BATTERY_SHUTDOWN_V);

  char b2[24];
  char b3[24];
  char b4[24];
  snprintf(b2, sizeof(b2), "%cF:%2d%%", gThresholdIndex == 0 ? '>' : ' ', (int)feedLow);
  snprintf(b3, sizeof(b3), "%cW:%2d%%", gThresholdIndex == 1 ? '>' : ' ', (int)waterLow);
  snprintf(b4, sizeof(b4), "%cB:%4.1fV", gThresholdIndex == 2 ? '>' : ' ', batteryShutdown);

  // Try to place feeder and water on the same line if they fit
  String combined = String(b2) + " " + String(b3);
  if (combined.length() <= 20) {
    clearAndPrint4("Thresholds D=Back", combined, "", String(b4) + " C=Next");
  } else {
    clearAndPrint4("Thresholds D=Back", String(b2), String(b3), String(b4) + " C=Next");
  }
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

void handleHomeKey(char key, JsonVariant cfg) {
  if (key == 'A') transition(STATE_MENU);
  if (key == 'C') {
    beginManualFeedEntry(cfg);
    transition(STATE_MANUAL_FEED_INPUT);
  }
}

void handleMenuKey(char key, JsonVariant cfg) {
  if (key == 'A') gMenuIndex = (gMenuIndex - 1 + kMenuCount) % kMenuCount;
  if (key == 'B') gMenuIndex = (gMenuIndex + 1) % kMenuCount;
  if (key == 'D') transition(STATE_HOME);

  if (key == 'C') {
    if (gMenuIndex == 0) {
      beginManualFeedEntry(cfg);
      transition(STATE_MANUAL_FEED_INPUT);
    }
    if (gMenuIndex == 1) transition(STATE_SCHEDULE_LIST);
    if (gMenuIndex == 2) transition(STATE_STATUS_VIEW);
    if (gMenuIndex == 3) transition(STATE_ALERTS_LIST);
    if (gMenuIndex == 4) transition(STATE_SETTINGS_MENU);
  }
}

void handleManualInputKey(char key, JsonVariant cfg) {
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
    gEntryMaxKg = getMaxSingleFeedKg(cfg);
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
  if (key == 'A') gSettingsIndex = (gSettingsIndex - 1 + 3) % 3;
  if (key == 'B') gSettingsIndex = (gSettingsIndex + 1) % 3;
  if (key == 'D') transition(STATE_MENU);
  if (key == 'C') {
    if (gSettingsIndex == 0) transition(STATE_SETTINGS_THRESHOLD);
    if (gSettingsIndex == 1) transition(STATE_SETTINGS_GRAIN);
    if (gSettingsIndex == 2) {
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
  float batteryShutdown = getCfg(cfg, "low_battery_shutdown_v", DEFAULT_LOW_BATTERY_SHUTDOWN_V);

  if (key == 'C') {
    gThresholdIndex = (gThresholdIndex + 1) % 3;
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
    if (gThresholdIndex == 2) saveNumericConfig("low_battery_shutdown_v", clampf(batteryShutdown + 0.1f, 9.0f, 14.5f));
  }
  if (key == 'B') {
    if (gThresholdIndex == 0) saveNumericConfig("feeder_low_threshold_pct", clampf(feedLow - step, 1.0f, 99.0f));
    if (gThresholdIndex == 1) saveNumericConfig("water_low_threshold_pct", clampf(waterLow - step, 1.0f, 99.0f));
    if (gThresholdIndex == 2) saveNumericConfig("low_battery_shutdown_v", clampf(batteryShutdown - 0.1f, 9.0f, 14.5f));
  }
}

void handleGrainKey(char key, JsonVariant cfg) {
  int count = getGrainTypeCount(cfg);
  if (count <= 0) count = DEFAULT_GRAIN_TYPE_COUNT;
  if (count <= 0) {
    if (key == 'D') transition(STATE_SETTINGS_MENU);
    return;
  }

  if (key == 'D') {
    transition(STATE_SETTINGS_MENU);
    return;
  }
  if (key == 'A') {
    gGrainIndex = (gGrainIndex - 1 + count) % count;
    saveGrainTypeSelection(cfg, gGrainIndex);
    return;
  }
  if (key == 'B') {
    gGrainIndex = (gGrainIndex + 1) % count;
    saveGrainTypeSelection(cfg, gGrainIndex);
    return;
  }
  if (key == 'C') {
    saveGrainTypeSelection(cfg, gGrainIndex);
    transition(STATE_SETTINGS_MENU);
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
    case STATE_SETTINGS_GRAIN:
      drawGrainEditor(cfg);
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
      handleHomeKey(key, cfg);
      break;
    case STATE_MENU:
      handleMenuKey(key, cfg);
      break;
    case STATE_MANUAL_FEED_INPUT:
      handleManualInputKey(key, cfg);
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
      if (key == 'C' && gSettingsIndex == 1) {
        gGrainIndex = getSelectedGrainTypeIndex(cfg);
      }
      handleSettingsMenuKey(key);
      break;
    case STATE_SETTINGS_THRESHOLD:
      handleThresholdKey(key, cfg);
      break;
    case STATE_SETTINGS_GRAIN:
      handleGrainKey(key, cfg);
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

  // Update last activity and wake LCD if needed
  if (key != '\0') {
    gLastKeyActivityMs = millis();
    if (!gLcdBacklightOn) {
      setLcdBacklight(true);
    }
  }

  // If backlight is on and idle timeout exceeded, turn it off
  if (gLcdBacklightOn && (millis() - gLastKeyActivityMs >= LCD_BACKLIGHT_TIMEOUT_MS)) {
    setLcdBacklight(false);
  }

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
