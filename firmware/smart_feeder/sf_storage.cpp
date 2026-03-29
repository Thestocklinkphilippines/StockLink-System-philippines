#include "sf_storage.h"

#include <ArduinoJson.h>

#include "sf_config.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_utils.h"

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

  prefs.end();
}
