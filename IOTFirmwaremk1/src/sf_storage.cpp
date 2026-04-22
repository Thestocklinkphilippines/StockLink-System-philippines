#include "sf_storage.h"

#include <ArduinoJson.h>

#include "sf_config.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_utils.h"

static String getLocalDateYmd() {
  time_t now = time(nullptr);
  struct tm tmNow;
  localtime_r(&now, &tmNow);
  char buf[16];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &tmNow);
  return String(buf);
}

static bool loadConfigDoc(DynamicJsonDocument& d) {
  String cfg = loadLocalConfig();
  d.clear();
  DeserializationError err = deserializeJson(d, cfg);
  if (err) {
    LOG_ERROR("Local config parse failed in storage helpers: %s bytes=%u", err.c_str(), (unsigned int)cfg.length());
    return false;
  }
  return true;
}

static void stampConfigOwnerFields(JsonVariant cfgObj) {
  cfgObj["last_updated"] = getUtcIsoNow();
  cfgObj["updated_by"] = "esp32";
}

String loadLocalConfig() {
  if (!prefs.begin(PREF_NAMESPACE, true)) {
    LOG_ERROR("Preferences begin(readonly) failed");
    return "{}";
  }
  String cfg = prefs.getString(PREF_CONFIG_KEY, "{}");
  prefs.end();
  LOG_DEBUG("Loaded local config bytes=%u", (unsigned int)cfg.length());
  return cfg;
}

void saveLocalConfig(const String& jsonCfg) {
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    LOG_ERROR("Preferences begin(write) failed");
    return;
  }
  prefs.putString(PREF_CONFIG_KEY, jsonCfg);
  prefs.end();
  cachedCfgStr = jsonCfg;
  LOG_INFO("Saved local config bytes=%u", (unsigned int)jsonCfg.length());
}

float readRemainingKg() {
  if (!prefs.begin(PREF_NAMESPACE, true)) {
    LOG_ERROR("Preferences begin(read remaining) failed");
    return DEFAULT_MAX_FEEDS_CAPACITY_KG;
  }
  float v = prefs.getFloat(PREF_REMAINING_KG, DEFAULT_MAX_FEEDS_CAPACITY_KG);
  prefs.end();
  return v;
}

void writeRemainingKg(float v) {
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    LOG_ERROR("Preferences begin(write remaining) failed");
    return;
  }
  prefs.putFloat(PREF_REMAINING_KG, v);
  prefs.end();
  LOG_DEBUG("Remaining kg updated: %.3f", v);
}

uint32_t readLastFeedNowCommandId() {
  if (!prefs.begin(PREF_NAMESPACE, true)) {
    LOG_ERROR("Preferences begin(read feed cmd id) failed");
    return 0;
  }
  uint32_t v = prefs.getULong(PREF_LAST_FEED_CMD_ID, 0UL);
  prefs.end();
  return v;
}

void writeLastFeedNowCommandId(uint32_t id) {
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    LOG_ERROR("Preferences begin(write feed cmd id) failed");
    return;
  }
  prefs.putULong(PREF_LAST_FEED_CMD_ID, id);
  prefs.end();
  LOG_INFO("Last feed_now command id updated: %lu", (unsigned long)id);
}

void ensureLocalDefaults() {
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    LOG_ERROR("Preferences begin(init) failed");
    return;
  }

  if (!prefs.isKey(PREF_CONFIG_KEY)) {
    StaticJsonDocument<1024> d;
    String nowIso = getUtcIsoNow();
    d["last_updated"] = nowIso;
    d["updated_by"] = "esp32";
    d["keypad_input_enabled"] = SF_ENABLE_KEYPAD_INPUT ? true : false;
    d["max_feeds_capacity_kg"] = DEFAULT_MAX_FEEDS_CAPACITY_KG;
    d["feeder_low_threshold_pct"] = DEFAULT_FEEDER_LOW_THRESHOLD_PCT;
    d["feeder_high_threshold_pct"] = DEFAULT_FEEDER_HIGH_THRESHOLD_PCT;
    d["water_low_threshold_pct"] = DEFAULT_WATER_LOW_THRESHOLD_PCT;
    d["water_high_threshold_pct"] = DEFAULT_WATER_HIGH_THRESHOLD_PCT;
    d["feeder_tank_depth_cm"] = FEEDER_TANK_DEPTH_CM;
    d["water_tank_depth_cm"] = WATER_TANK_DEPTH_CM;
    d["max_feeds_capacity_updated_at"] = nowIso;
    d["max_feeds_capacity_updated_by"] = "esp32";
    d.createNestedArray("schedules");
    String s;
    serializeJson(d, s);
    prefs.putString(PREF_CONFIG_KEY, s);
    LOG_INFO("Created default local config");
  }

  if (!prefs.isKey(PREF_REMAINING_KG)) {
    prefs.putFloat(PREF_REMAINING_KG, DEFAULT_MAX_FEEDS_CAPACITY_KG);
    LOG_INFO("Initialized remaining feed kg default");
  }

  if (!prefs.isKey(PREF_LAST_FEED_CMD_ID)) {
    prefs.putULong(PREF_LAST_FEED_CMD_ID, 0UL);
    LOG_INFO("Initialized last feed_now command id default");
  }

  if (prefs.isKey(PREF_CONFIG_KEY)) {
    String cfg = prefs.getString(PREF_CONFIG_KEY, "{}");
    DynamicJsonDocument d(8192);
    DeserializationError err = deserializeJson(d, cfg);
    if (!err) {
      bool changed = false;
      if (!d.containsKey("keypad_input_enabled")) {
        d["keypad_input_enabled"] = SF_ENABLE_KEYPAD_INPUT ? true : false;
        changed = true;
      }
      if (!d.containsKey("total_feeds_today_kg")) {
        d["total_feeds_today_kg"] = 0.0f;
        changed = true;
      }
      if (!d.containsKey("total_feeds_today_date")) {
        d["total_feeds_today_date"] = getLocalDateYmd();
        changed = true;
      }
      if (changed) {
        stampConfigOwnerFields(d.as<JsonVariant>());
        String out;
        serializeJson(d, out);
        prefs.putString(PREF_CONFIG_KEY, out);
        LOG_INFO("Initialized daily total fields in local config");
      }
    }
  }

  prefs.end();
}

void ensureDailyFeedTotalForToday() {
  static DynamicJsonDocument d(8192);
  if (!loadConfigDoc(d)) return;

  JsonVariant cfgObj = d.as<JsonVariant>();
  String today = getLocalDateYmd();
  const char* storedDate = cfgObj["total_feeds_today_date"] | "";
  bool missingTotal = !cfgObj.containsKey("total_feeds_today_kg");
  bool missingDate = strlen(storedDate) == 0;
  bool dayRolled = !missingDate && strcmp(storedDate, today.c_str()) != 0;

  if (!missingTotal && !missingDate && !dayRolled) return;

  cfgObj["total_feeds_today_kg"] = 0.0f;
  cfgObj["total_feeds_today_date"] = today;
  stampConfigOwnerFields(cfgObj);

  String out;
  serializeJson(d, out);
  saveLocalConfig(out);
  LOG_INFO("Daily feed total reset date=%s", today.c_str());
}

void addToDailyFeedTotalKg(float dispensedKg) {
  if (dispensedKg <= 0.0f) return;

  ensureDailyFeedTotalForToday();

  static DynamicJsonDocument d(8192);
  if (!loadConfigDoc(d)) return;

  JsonVariant cfgObj = d.as<JsonVariant>();
  float cur = cfgObj["total_feeds_today_kg"] | 0.0f;
  float next = cur + dispensedKg;
  if (next < 0.0f) next = 0.0f;

  cfgObj["total_feeds_today_kg"] = next;
  cfgObj["total_feeds_today_date"] = getLocalDateYmd();
  stampConfigOwnerFields(cfgObj);

  String out;
  serializeJson(d, out);
  saveLocalConfig(out);
  LOG_INFO("Daily feed total updated current=%.3f add=%.3f next=%.3f",
           cur,
           dispensedKg,
           next);
}
