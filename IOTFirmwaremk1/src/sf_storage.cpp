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

static const char* PREF_CONFIG_CHUNK_COUNT = "cfg_part_count";
static const char* PREF_CONFIG_CHUNK_PREFIX = "cfg_part_";
static const size_t LOCAL_CONFIG_CHUNK_SIZE = 768;

static String makeConfigChunkKey(uint16_t index) {
  return String(PREF_CONFIG_CHUNK_PREFIX) + String(index);
}

static bool writeLocalConfigChunked(const String& jsonCfg) {
  size_t totalBytes = jsonCfg.length();
  uint16_t chunkCount = (uint16_t)((totalBytes + LOCAL_CONFIG_CHUNK_SIZE - 1U) / LOCAL_CONFIG_CHUNK_SIZE);
  if (chunkCount == 0) {
    chunkCount = 1;
  }

  for (uint16_t index = 0; index < chunkCount; ++index) {
    const size_t start = (size_t)index * LOCAL_CONFIG_CHUNK_SIZE;
    const size_t end = start + LOCAL_CONFIG_CHUNK_SIZE;
    String chunk = jsonCfg.substring(start, end > totalBytes ? totalBytes : end);
    String key = makeConfigChunkKey(index);
    prefs.putString(key.c_str(), chunk);
  }

  prefs.putUInt(PREF_CONFIG_CHUNK_COUNT, chunkCount);
  return true;
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

static String getEmptyOutboxJson() {
  StaticJsonDocument<128> d;
  d["next_seq"] = 1UL;
  d.createNestedArray("events");
  String out;
  serializeJson(d, out);
  return out;
}

static bool nearlyEqualFloat(float a, float b, float epsilon = 0.01f) {
  float diff = a - b;
  if (diff < 0.0f) diff = -diff;
  return diff <= epsilon;
}

static float deriveMaxSingleFeedKg(JsonVariant cfg) {
  float maxCap = DEFAULT_MAX_FEEDS_CAPACITY_KG;
  if (!cfg.isNull() && cfg.containsKey("max_feeds_capacity_kg")) {
    maxCap = cfg["max_feeds_capacity_kg"] | DEFAULT_MAX_FEEDS_CAPACITY_KG;
  }
  if (maxCap <= 0.0f) maxCap = DEFAULT_MAX_FEEDS_CAPACITY_KG;

  float remainingKg = readRemainingKg();
  if (remainingKg < 0.0f) remainingKg = 0.0f;
  if (remainingKg > maxCap) remainingKg = maxCap;
  return remainingKg;
}

static void syncMaxSingleFeedKg(JsonVariant cfgObj) {
  if (cfgObj.isNull()) return;

  float derived = deriveMaxSingleFeedKg(cfgObj);
  float current = cfgObj["max_single_feed_kg"] | -1.0f;
  if (nearlyEqualFloat(current, derived)) return;

  cfgObj["max_single_feed_kg"] = derived;
  stampConfigOwnerFields(cfgObj);

  String out;
  serializeJson(cfgObj.as<JsonVariant>(), out);
  saveLocalConfig(out);
}

float getMaxSingleFeedKg(JsonVariant cfg) {
  return deriveMaxSingleFeedKg(cfg);
}

static void seedDefaultGrainTypes(JsonVariant cfgObj) {
  JsonArray grainTypes = cfgObj.createNestedArray("grain_types");
  for (int i = 0; i < DEFAULT_GRAIN_TYPE_COUNT; i++) {
    JsonObject entry = grainTypes.createNestedObject();
    entry["grain_type"] = DEFAULT_GRAIN_TYPES[i].grain_type;
    entry["feed_ms_per_kg"] = DEFAULT_GRAIN_TYPES[i].feed_ms_per_kg;
  }
}

int getGrainTypeCount(JsonVariant cfg) {
  if (!cfg.isNull() && cfg.containsKey("grain_types")) {
    JsonArray grainTypes = cfg["grain_types"].as<JsonArray>();
    if (!grainTypes.isNull() && grainTypes.size() > 0) {
      return (int)grainTypes.size();
    }
  }
  return DEFAULT_GRAIN_TYPE_COUNT;
}

const char* getGrainTypeNameByIndex(JsonVariant cfg, int index) {
  if (index < 0) return DEFAULT_GRAIN_TYPE;

  if (!cfg.isNull() && cfg.containsKey("grain_types")) {
    JsonArray grainTypes = cfg["grain_types"].as<JsonArray>();
    if (!grainTypes.isNull() && index < (int)grainTypes.size()) {
      int i = 0;
      for (JsonVariant grainTypeCfg : grainTypes) {
        if (i == index) {
          return grainTypeCfg["grain_type"] | DEFAULT_GRAIN_TYPE;
        }
        i++;
      }
    }
  }

  if (index < DEFAULT_GRAIN_TYPE_COUNT) {
    return DEFAULT_GRAIN_TYPES[index].grain_type;
  }
  return DEFAULT_GRAIN_TYPE;
}

float getGrainTypeMsPerKgByIndex(JsonVariant cfg, int index) {
  if (index < 0) return FEED_MS_PER_KG_STANDARD_PELLETS;

  if (!cfg.isNull() && cfg.containsKey("grain_types")) {
    JsonArray grainTypes = cfg["grain_types"].as<JsonArray>();
    if (!grainTypes.isNull() && index < (int)grainTypes.size()) {
      int i = 0;
      for (JsonVariant grainTypeCfg : grainTypes) {
        if (i == index) {
          float rate = grainTypeCfg["feed_ms_per_kg"] | 0.0f;
          if (rate > 0.0f) return rate;
          break;
        }
        i++;
      }
    }
  }

  if (index < DEFAULT_GRAIN_TYPE_COUNT) {
    return DEFAULT_GRAIN_TYPES[index].feed_ms_per_kg;
  }
  return FEED_MS_PER_KG_STANDARD_PELLETS;
}

int findGrainTypeIndex(JsonVariant cfg, const char* grainType) {
  if (grainType == nullptr || strlen(grainType) == 0) return 0;

  if (!cfg.isNull() && cfg.containsKey("grain_types")) {
    JsonArray grainTypes = cfg["grain_types"].as<JsonArray>();
    if (!grainTypes.isNull()) {
      int i = 0;
      for (JsonVariant grainTypeCfg : grainTypes) {
        const char* name = grainTypeCfg["grain_type"] | "";
        if (strcmp(name, grainType) == 0) return i;
        i++;
      }
    }
  }

  for (int i = 0; i < DEFAULT_GRAIN_TYPE_COUNT; i++) {
    if (strcmp(DEFAULT_GRAIN_TYPES[i].grain_type, grainType) == 0) return i;
  }

  return 0;
}

int getSelectedGrainTypeIndex(JsonVariant cfg) {
  int count = getGrainTypeCount(cfg);
  if (count <= 0) count = DEFAULT_GRAIN_TYPE_COUNT;

  if (!cfg.isNull() && cfg.containsKey("grain_type_index")) {
    int index = cfg["grain_type_index"] | 0;
    if (index < 0) index = 0;
    if (count > 0 && index >= count) index %= count;
    return index;
  }

  const char* grainType = cfg.isNull() ? DEFAULT_GRAIN_TYPE : (cfg["grain_type"] | DEFAULT_GRAIN_TYPE);
  return findGrainTypeIndex(cfg, grainType);
}

void saveGrainTypeSelection(JsonVariant cfg, int selectedIndex) {
  int count = getGrainTypeCount(cfg);
  if (count <= 0) count = DEFAULT_GRAIN_TYPE_COUNT;
  if (count <= 0) return;
  if (selectedIndex < 0) selectedIndex = 0;
  if (selectedIndex >= count) selectedIndex %= count;

  const char* selectedType = getGrainTypeNameByIndex(cfg, selectedIndex);
  float selectedRate = getGrainTypeMsPerKgByIndex(cfg, selectedIndex);

  String cfgStr = loadLocalConfig();
  DynamicJsonDocument d(8192);
  DeserializationError err = deserializeJson(d, cfgStr);
  if (err) return;

  JsonVariant root = d.as<JsonVariant>();
  root["grain_type_index"] = selectedIndex;
  root["grain_type"] = selectedType;
  root["feed_ms_per_kg"] = selectedRate;
  stampConfigOwnerFields(root);

  String out;
  serializeJson(d, out);
  saveLocalConfig(out);
}

String loadLocalConfig() {
  if (!prefs.begin(PREF_NAMESPACE, true)) {
    LOG_ERROR("Preferences begin(readonly) failed");
    return "{}";
  }
  String cfg;
  if (prefs.isKey(PREF_CONFIG_CHUNK_COUNT)) {
    uint16_t chunkCount = (uint16_t)prefs.getUInt(PREF_CONFIG_CHUNK_COUNT, 0U);
    if (chunkCount > 0) {
      cfg.reserve((size_t)chunkCount * LOCAL_CONFIG_CHUNK_SIZE);
      for (uint16_t index = 0; index < chunkCount; ++index) {
        String key = makeConfigChunkKey(index);
        cfg += prefs.getString(key.c_str(), "");
      }
    }
  }

  if (cfg.length() == 0) {
    cfg = prefs.getString(PREF_CONFIG_KEY, "{}");
  }
  prefs.end();
  LOG_DEBUG("Loaded local config bytes=%u", (unsigned int)cfg.length());
  return cfg;
}

void saveLocalConfig(const String& jsonCfg) {
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    LOG_ERROR("Preferences begin(write) failed");
    return;
  }
  writeLocalConfigChunked(jsonCfg);
  prefs.end();
  cachedCfgStr = jsonCfg;
  LOG_INFO("Saved local config bytes=%u chunks=%u", (unsigned int)jsonCfg.length(),
           (unsigned int)((jsonCfg.length() + LOCAL_CONFIG_CHUNK_SIZE - 1U) / LOCAL_CONFIG_CHUNK_SIZE));
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

  DynamicJsonDocument d(8192);
  if (loadConfigDoc(d)) {
    syncMaxSingleFeedKg(d.as<JsonVariant>());
  }
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

String loadEventOutbox() {
  if (!prefs.begin(PREF_NAMESPACE, true)) {
    LOG_ERROR("Preferences begin(read outbox) failed");
    return getEmptyOutboxJson();
  }
  String outbox = prefs.getString(PREF_EVENT_OUTBOX, getEmptyOutboxJson());
  prefs.end();
  return outbox;
}

void saveEventOutbox(const String& jsonOutbox) {
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    LOG_ERROR("Preferences begin(write outbox) failed");
    return;
  }
  prefs.putString(PREF_EVENT_OUTBOX, jsonOutbox);
  prefs.end();
  LOG_DEBUG("Saved event outbox bytes=%u", (unsigned int)jsonOutbox.length());
}

uint32_t readEventSequence() {
  if (!prefs.begin(PREF_NAMESPACE, true)) {
    LOG_ERROR("Preferences begin(read event seq) failed");
    return 1UL;
  }
  uint32_t seq = prefs.getULong(PREF_EVENT_SEQ, 1UL);
  prefs.end();
  return seq;
}

void writeEventSequence(uint32_t seq) {
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    LOG_ERROR("Preferences begin(write event seq) failed");
    return;
  }
  prefs.putULong(PREF_EVENT_SEQ, seq);
  prefs.end();
}

unsigned int readBufferedEventCount() {
  String raw = loadEventOutbox();
  DynamicJsonDocument d(8192);
  DeserializationError err = deserializeJson(d, raw);
  if (err) return 0U;
  JsonArray events = d["events"].as<JsonArray>();
  return events.isNull() ? 0U : (unsigned int)events.size();
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
    // Initialize max single feed to the capacity by default so UI isn't artificially capped.
    d["max_single_feed_kg"] = d["max_feeds_capacity_kg"] | DEFAULT_MAX_FEEDS_CAPACITY_KG;
    d["grain_type_index"] = 0;
    d["grain_type"] = DEFAULT_GRAIN_TYPE;
    d["feed_ms_per_kg"] = FEED_MS_PER_KG_STANDARD_PELLETS;
    d["feeder_low_threshold_pct"] = DEFAULT_FEEDER_LOW_THRESHOLD_PCT;
    d["feeder_high_threshold_pct"] = DEFAULT_FEEDER_HIGH_THRESHOLD_PCT;
    d["water_low_threshold_pct"] = DEFAULT_WATER_LOW_THRESHOLD_PCT;
    d["water_high_threshold_pct"] = DEFAULT_WATER_HIGH_THRESHOLD_PCT;
    d["feeder_tank_bottom_distance_cm"] = FEEDER_TANK_BOTTOM_DISTANCE_CM;
    d["feeder_tank_full_distance_cm"] = FEEDER_MAX_FEED_HEIGHT_CM;
    d["feeder_max_feed_height_cm"] = FEEDER_MAX_FEED_HEIGHT_CM;
    d["feeder_tank_depth_cm"] = FEEDER_TANK_BOTTOM_DISTANCE_CM;
    d["water_tank_depth_cm"] = WATER_TANK_DEPTH_CM;
    d["battery_sense_enabled"] = true;
    d["battery_adc_pin"] = BATTERY_ADC_PIN;
    d["battery_divider_top_ohms"] = BATTERY_DIVIDER_TOP_OHMS;
    d["battery_divider_bottom_ohms"] = BATTERY_DIVIDER_BOTTOM_OHMS;
    d["battery_adc_reference_v"] = BATTERY_ADC_REFERENCE_V;
    d["battery_adc_gain_correction"] = BATTERY_ADC_GAIN_CORRECTION;
    d["low_battery_shutdown_v"] = DEFAULT_LOW_BATTERY_SHUTDOWN_V;
    d["max_feeds_capacity_updated_at"] = nowIso;
    d["max_feeds_capacity_updated_by"] = "esp32";
    seedDefaultGrainTypes(d.as<JsonVariant>());
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

  if (!prefs.isKey(PREF_EVENT_OUTBOX)) {
    prefs.putString(PREF_EVENT_OUTBOX, getEmptyOutboxJson());
    LOG_INFO("Initialized event outbox default");
  }

  if (!prefs.isKey(PREF_EVENT_SEQ)) {
    prefs.putULong(PREF_EVENT_SEQ, 1UL);
    LOG_INFO("Initialized event sequence default");
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
      if (!d.containsKey("grain_type")) {
        d["grain_type"] = DEFAULT_GRAIN_TYPE;
        changed = true;
      }
      if (!d.containsKey("grain_types") || !d["grain_types"].is<JsonArray>()) {
        seedDefaultGrainTypes(d.as<JsonVariant>());
        changed = true;
      }
      int selectedGrainIndex = getSelectedGrainTypeIndex(d.as<JsonVariant>());
      const char* selectedGrainType = getGrainTypeNameByIndex(d.as<JsonVariant>(), selectedGrainIndex);
      float selectedGrainRate = getGrainTypeMsPerKgByIndex(d.as<JsonVariant>(), selectedGrainIndex);
      if (!d.containsKey("grain_type_index")) {
        d["grain_type_index"] = selectedGrainIndex;
        changed = true;
      }
      if (!d.containsKey("grain_type") || strcmp(d["grain_type"] | "", selectedGrainType) != 0) {
        d["grain_type"] = selectedGrainType;
        changed = true;
      }
      if (!d.containsKey("feed_ms_per_kg") || !nearlyEqualFloat(d["feed_ms_per_kg"] | 0.0f, selectedGrainRate)) {
        d["feed_ms_per_kg"] = selectedGrainRate;
        changed = true;
      }
      if (!d.containsKey("feeder_tank_bottom_distance_cm")) {
        d["feeder_tank_bottom_distance_cm"] = d["feeder_tank_depth_cm"] | FEEDER_TANK_BOTTOM_DISTANCE_CM;
        changed = true;
      }
      if (!d.containsKey("feeder_tank_full_distance_cm")) {
        d["feeder_tank_full_distance_cm"] = d["feeder_max_feed_height_cm"] | FEEDER_MAX_FEED_HEIGHT_CM;
        changed = true;
      }
      if (!d.containsKey("feeder_max_feed_height_cm")) {
        d["feeder_max_feed_height_cm"] = d["feeder_tank_full_distance_cm"] | FEEDER_MAX_FEED_HEIGHT_CM;
        changed = true;
      }
      if (!d.containsKey("battery_sense_enabled")) {
        d["battery_sense_enabled"] = true;
        changed = true;
      }
      if (!d.containsKey("battery_adc_pin")) {
        d["battery_adc_pin"] = BATTERY_ADC_PIN;
        changed = true;
      }
      if (!d.containsKey("battery_divider_top_ohms")) {
        d["battery_divider_top_ohms"] = BATTERY_DIVIDER_TOP_OHMS;
        changed = true;
      }
      if (!d.containsKey("battery_divider_bottom_ohms")) {
        d["battery_divider_bottom_ohms"] = BATTERY_DIVIDER_BOTTOM_OHMS;
        changed = true;
      }
      if (!d.containsKey("battery_adc_reference_v")) {
        d["battery_adc_reference_v"] = BATTERY_ADC_REFERENCE_V;
        changed = true;
      }
      if (!d.containsKey("battery_adc_gain_correction")) {
        d["battery_adc_gain_correction"] = BATTERY_ADC_GAIN_CORRECTION;
        changed = true;
      }
      if (!d.containsKey("low_battery_shutdown_v")) {
        d["low_battery_shutdown_v"] = DEFAULT_LOW_BATTERY_SHUTDOWN_V;
        changed = true;
      }

      // Ensure a reasonable max_single_feed_kg exists and does not exceed the capacity.
      if (!d.containsKey("max_single_feed_kg")) {
        d["max_single_feed_kg"] = d["max_feeds_capacity_kg"] | DEFAULT_MAX_FEEDS_CAPACITY_KG;
        changed = true;
      } else {
        float cap = d["max_feeds_capacity_kg"] | DEFAULT_MAX_FEEDS_CAPACITY_KG;
        float single = d["max_single_feed_kg"] | DEFAULT_MAX_SINGLE_FEED_KG;
        if (single > cap) {
          d["max_single_feed_kg"] = cap;
          changed = true;
        }
      }

      int selectedIndex = getSelectedGrainTypeIndex(d.as<JsonVariant>());
      const char* selectedType = getGrainTypeNameByIndex(d.as<JsonVariant>(), selectedIndex);
      float selectedRate = getGrainTypeMsPerKgByIndex(d.as<JsonVariant>(), selectedIndex);
      if ((d["grain_type_index"] | -1) != selectedIndex) {
        d["grain_type_index"] = selectedIndex;
        changed = true;
      }
      if (strcmp(d["grain_type"] | "", selectedType) != 0) {
        d["grain_type"] = selectedType;
        changed = true;
      }
      if (!nearlyEqualFloat(d["feed_ms_per_kg"] | 0.0f, selectedRate)) {
        d["feed_ms_per_kg"] = selectedRate;
        changed = true;
      }

      // One-time migration: older firmware wrote legacy divider defaults (100k/22k).
      // Replace only that exact pair so existing custom calibrations are preserved.
      float dividerTop = d["battery_divider_top_ohms"] | BATTERY_DIVIDER_TOP_OHMS;
      float dividerBottom = d["battery_divider_bottom_ohms"] | BATTERY_DIVIDER_BOTTOM_OHMS;
      if (nearlyEqualFloat(dividerTop, 100000.0f) && nearlyEqualFloat(dividerBottom, 22000.0f)) {
        d["battery_divider_top_ohms"] = BATTERY_DIVIDER_TOP_OHMS;
        d["battery_divider_bottom_ohms"] = BATTERY_DIVIDER_BOTTOM_OHMS;
        changed = true;
        LOG_INFO("Migrated legacy battery divider defaults to top=%.1f bottom=%.1f",
                 BATTERY_DIVIDER_TOP_OHMS,
                 BATTERY_DIVIDER_BOTTOM_OHMS);
      }

      if (changed) {
        stampConfigOwnerFields(d.as<JsonVariant>());
        String out;
        serializeJson(d, out);
        writeLocalConfigChunked(out);
        cachedCfgStr = out;
        LOG_INFO("Initialized daily total fields in local config");
      }
    }
  }

  DynamicJsonDocument syncDoc(8192);
  if (loadConfigDoc(syncDoc)) {
    syncMaxSingleFeedKg(syncDoc.as<JsonVariant>());
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
