#include "sf_serial.h"

#include <WiFi.h>
#include <stdlib.h>

#include "sf_config.h"
#include "sf_actuators.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_network.h"
#include "sf_pins.h"
#include "sf_sensors.h"
#include "sf_storage.h"
#include "sf_utils.h"

// Keep large JSON buffers off the loopTask stack.
static DynamicJsonDocument gSerialCfgDoc(8192);

namespace {

static const char kCalKeys[16] = {
    '1', '2', '3', 'A',
    '4', '5', '6', 'B',
    '7', '8', '9', 'C',
    '*', '0', '#', 'D'};

static const char* kCalFieldNames[16] = {
    "adc_1", "adc_2", "adc_3", "adc_a",
    "adc_4", "adc_5", "adc_6", "adc_b",
    "adc_7", "adc_8", "adc_9", "adc_c",
    "adc_star", "adc_0", "adc_hash", "adc_d"};

struct KeypadCalibrationSession {
  bool active;
  int step;
  int trend;
  bool idleIsLow;
  int adcByStep[16];
  int noKeyAdc;
};

KeypadCalibrationSession gKeypadCal = {false, 0, 0, true, {0}, 0};

bool tryParseFloat(const String& token, float& outValue) {
  if (token.length() == 0) return false;
  char buf[32];
  token.toCharArray(buf, sizeof(buf));
  char* endPtr = nullptr;
  float v = strtof(buf, &endPtr);
  if (endPtr == buf || *endPtr != '\0') return false;
  outValue = v;
  return true;
}

bool tryParseInt(const String& token, int& outValue) {
  if (token.length() == 0) return false;
  char buf[16];
  token.toCharArray(buf, sizeof(buf));
  char* endPtr = nullptr;
  long v = strtol(buf, &endPtr, 10);
  if (endPtr == buf || *endPtr != '\0') return false;
  outValue = (int)v;
  return true;
}

bool hasStrictlyMonotonicValues(const int* values, int count, int* outTrend = nullptr) {
  if (!values || count <= 1) return false;

  int trend = 0;
  for (int i = 0; i < count; i++) {
    if (values[i] <= 0 || values[i] >= 4095) return false;
    if (i == 0) continue;

    int delta = values[i] - values[i - 1];
    if (delta == 0) return false;
    int stepTrend = (delta > 0) ? 1 : -1;
    if (trend == 0) trend = stepTrend;
    if (stepTrend != trend) return false;
  }

  if (outTrend) *outTrend = trend;
  return true;
}

int sampleKeypadAdcAverage() {
  const int kSamples = 40;
  long total = 0;
  for (int i = 0; i < kSamples; i++) {
    total += analogRead(PIN_KEYPAD_ADC);
    delay(3);
  }
  return (int)(total / kSamples);
}

void printCalibrationPrompt() {
  if (!gKeypadCal.active) return;

  if (gKeypadCal.step < 16) {
    Serial.println();
    Serial.printf("[KEYPAD CAL] Step %d/16\n", gKeypadCal.step + 1);
    Serial.printf("[KEYPAD CAL] Press and hold key '%c', then type: sample\n", kCalKeys[gKeypadCal.step]);
    Serial.println("[KEYPAD CAL] Type 'cancel' anytime to abort.");
    return;
  }

  Serial.println();
  Serial.println("[KEYPAD CAL] Final step");
  Serial.println("[KEYPAD CAL] Release all keys, then type: sample");
}

void endCalibrationSession(bool success) {
  gKeypadCal.active = false;
  gSerialConsoleExclusive = false;
  if (!success) {
    Serial.println("[KEYPAD CAL] Calibration cancelled.");
  }
}

bool persistKeypadCalibration() {
  String cfg = loadLocalConfig();
  DynamicJsonDocument d(8192);
  d.clear();
  DeserializationError err = deserializeJson(d, cfg);
  if (err) {
    Serial.printf("[KEYPAD CAL] Failed to parse config for save: %s\n", err.c_str());
    return false;
  }

  JsonVariant kc = d.createNestedObject("keypad_calibration");
  kc["enabled"] = true;
  kc["no_key_adc"] = gKeypadCal.noKeyAdc;
  kc["idle_is_low"] = gKeypadCal.idleIsLow;
  for (int i = 0; i < 16; i++) {
    kc[kCalFieldNames[i]] = gKeypadCal.adcByStep[i];
  }
  kc["calibrated_at"] = getUtcIsoNow();

  d["last_updated"] = getUtcIsoNow();
  d["updated_by"] = "esp32";

  String out;
  serializeJson(d, out);
  saveLocalConfig(out);
  reloadKeypadCalibration();
  return true;
}

void processKeypadCalibrationLine(const String& rawLine) {
  String line = rawLine;
  line.trim();
  line.toLowerCase();

  if (line == "") return;

  if (line == "cancel") {
    endCalibrationSession(false);
    return;
  }

  if (line == "help") {
    Serial.println("[KEYPAD CAL] Commands: sample | cancel");
    printCalibrationPrompt();
    return;
  }

  if (line != "sample") {
    Serial.println("[KEYPAD CAL] Unknown command during calibration. Use: sample or cancel");
    printCalibrationPrompt();
    return;
  }

  int adc = sampleKeypadAdcAverage();

  if (gKeypadCal.step < 16) {
    if (gKeypadCal.step > 0) {
      int prev = gKeypadCal.adcByStep[gKeypadCal.step - 1];
      int delta = adc - prev;

      if (delta >= -15 && delta <= 15) {
        Serial.printf("[KEYPAD CAL] ADC=%d is too close to previous=%d. Retry step %d.\n",
                      adc,
                      prev,
                      gKeypadCal.step + 1);
        return;
      }

      int stepTrend = (delta > 0) ? 1 : -1;
      if (gKeypadCal.trend == 0) {
        gKeypadCal.trend = stepTrend;
      } else if (stepTrend != gKeypadCal.trend) {
        const char* expected = (gKeypadCal.trend > 0) ? "higher" : "lower";
        Serial.printf("[KEYPAD CAL] ADC=%d has wrong direction vs previous=%d (expected %s). Retry step %d.\n",
                      adc,
                      prev,
                      expected,
                      gKeypadCal.step + 1);
        return;
      }
    }

    gKeypadCal.adcByStep[gKeypadCal.step] = adc;
    Serial.printf("[KEYPAD CAL] Captured key '%c' -> ADC=%d\n", kCalKeys[gKeypadCal.step], adc);
    gKeypadCal.step++;
    printCalibrationPrompt();
    return;
  }

  if (adc > 80) {
    Serial.printf("[KEYPAD CAL] No-key ADC=%d is not near zero. Release all keys and retry.\n", adc);
    return;
  }

  gKeypadCal.noKeyAdc = adc;

  int finalTrend = 0;
  if (!hasStrictlyMonotonicValues(gKeypadCal.adcByStep, 16, &finalTrend)) {
    Serial.println("[KEYPAD CAL] Captured values are invalid (not strictly monotonic). Calibration aborted.");
    endCalibrationSession(false);
    return;
  }

  gKeypadCal.idleIsLow = (finalTrend < 0);

  if (!persistKeypadCalibration()) {
    Serial.println("[KEYPAD CAL] Failed to save calibration.");
    endCalibrationSession(false);
    return;
  }

  Serial.printf("[KEYPAD CAL] Saved. idle=%s no_key_adc=%d trend=%s\n",
                gKeypadCal.idleIsLow ? "low" : "high",
                gKeypadCal.noKeyAdc,
                finalTrend > 0 ? "ascending" : "descending");
  Serial.println("[KEYPAD CAL] Calibration complete. Returning to normal operation.");
  endCalibrationSession(true);
}

void startKeypadCalibrationSession() {
  gKeypadCal.active = true;
  gKeypadCal.step = 0;
  gKeypadCal.trend = 0;
  gKeypadCal.idleIsLow = true;
  gKeypadCal.noKeyAdc = 0;
  for (int i = 0; i < 16; i++) gKeypadCal.adcByStep[i] = 0;

  gSerialConsoleExclusive = true;
  Serial.println();
  Serial.println("[KEYPAD CAL] Serial output is now in exclusive calibration mode.");
  Serial.println("[KEYPAD CAL] Normal logs are temporarily muted.");
  Serial.println("[KEYPAD CAL] Commands during calibration: sample | cancel");
  printCalibrationPrompt();
}

}  // namespace

static float getConfiguredMaxCapacityKg() {
  String cfg = loadLocalConfig();
  if (cfg.length() == 0) return DEFAULT_MAX_FEEDS_CAPACITY_KG;

  static DynamicJsonDocument d(8192);
  d.clear();
  DeserializationError err = deserializeJson(d, cfg);
  if (err) {
    LOG_WARN("serial max capacity parse failed; using default %.3f", DEFAULT_MAX_FEEDS_CAPACITY_KG);
    return DEFAULT_MAX_FEEDS_CAPACITY_KG;
  }

  float maxCap = d["max_feeds_capacity_kg"] | 0.0f;
  if (maxCap <= 0.0f) {
    maxCap = d["config"]["max_feeds_capacity_kg"] | DEFAULT_MAX_FEEDS_CAPACITY_KG;
  }
  if (maxCap <= 0.0f) maxCap = DEFAULT_MAX_FEEDS_CAPACITY_KG;
  return maxCap;
}

static void setSimValueCommand(const String& args) {
#if SF_SIMULATION_MODE
  int sp = args.indexOf(' ');
  if (sp < 0) {
    Serial.println("Usage: sim_set feeder|water|mains <value>");
    return;
  }

  String field = args.substring(0, sp);
  String valueToken = args.substring(sp + 1);
  field.trim();
  valueToken.trim();

  if (field == "feeder") {
    float v = 0.0f;
    if (!tryParseFloat(valueToken, v)) {
      Serial.println("Invalid feeder value. Example: sim_set feeder 72.5");
      return;
    }
    state.simFeederLevelPct = clampf(v, 0.0f, 100.0f);
    state.lastFeederLevelPct = state.simFeederLevelPct;

    float maxCap = getConfiguredMaxCapacityKg();
    float remKg = (state.simFeederLevelPct / 100.0f) * maxCap;
    writeRemainingKg(remKg);

    Serial.printf("sim_feeder_level_pct set to %.2f (remaining_kg=%.3f max=%.3f)\n",
                  state.simFeederLevelPct,
                  remKg,
                  maxCap);
    return;
  }

  if (field == "water") {
    float v = 0.0f;
    if (!tryParseFloat(valueToken, v)) {
      Serial.println("Invalid water value. Example: sim_set water 35");
      return;
    }
    state.simWaterLevelPct = clampf(v, 0.0f, 100.0f);
    Serial.printf("sim_water_level_pct set to %.2f\n", state.simWaterLevelPct);
    return;
  }

  if (field == "mains") {
    int v = 0;
    if (!tryParseInt(valueToken, v) || (v != 0 && v != 1)) {
      Serial.println("Invalid mains value. Use 0 or 1. Example: sim_set mains 0");
      return;
    }
    state.mainsPowerPresent = (v == 1);
    Serial.printf("mains_power_present set to %d\n", state.mainsPowerPresent ? 1 : 0);
    return;
  }

  Serial.println("Unknown sim_set field. Use feeder|water|mains");
#else
  (void)args;
  Serial.println("Simulation mode is disabled; sim_set unavailable.");
#endif
}

static void resetSimDefaultsCommand() {
#if SF_SIMULATION_MODE
  state.simFeederLevelPct = 85.0f;
  state.simWaterLevelPct = 90.0f;
  state.mainsPowerPresent = true;
  state.lastFeederLevelPct = state.simFeederLevelPct;
  state.lastWaterLevelPct = state.simWaterLevelPct;

  float maxCap = getConfiguredMaxCapacityKg();
  writeRemainingKg((state.simFeederLevelPct / 100.0f) * maxCap);

  Serial.printf("Simulation values reset: feeder=85.00 water=90.00 mains=1 remaining_kg=%.3f\n",
                readRemainingKg());
#else
  Serial.println("Simulation mode is disabled; sim_defaults unavailable.");
#endif
}

static bool loadCfgDoc() {
  String cfg = loadLocalConfig();
  if (cfg.length() == 0) {
    Serial.println("Config is empty.");
    return false;
  }
  gSerialCfgDoc.clear();
  DeserializationError err = deserializeJson(gSerialCfgDoc, cfg);
  if (err) {
    Serial.printf("Config parse error: %s\n", err.c_str());
    return false;
  }
  return true;
}

static void listSchedulesCommand() {
  if (!loadCfgDoc()) return;

  JsonArray schedules = gSerialCfgDoc["schedules"].as<JsonArray>();
  if (schedules.isNull() || schedules.size() == 0) {
    Serial.println("No schedules in local config.");
    return;
  }

  Serial.println("Schedules (local config):");
  int idx = 0;
  for (JsonVariant s : schedules) {
    idx++;
    int id = s["id"] | -1;
    const char* name = s["schedule_name"] | "(unnamed)";
    const char* timeStr = s["time"] | "--:--";
    bool enabled = s["enabled"] | false;
    float amount = s["feeding_amount_kg"] | 0.0f;
    Serial.printf("  [%d] id=%d enabled=%d time=%s amount=%.3f name=%s\n",
                  idx, id, enabled ? 1 : 0, timeStr, amount, name);
  }
}

static void runScheduleNowCommand(const String& args) {
  if (!loadCfgDoc()) return;

  JsonArray schedules = gSerialCfgDoc["schedules"].as<JsonArray>();
  if (schedules.isNull() || schedules.size() == 0) {
    Serial.println("No schedules to run.");
    return;
  }

  String selector = args;
  selector.trim();

  JsonVariant selected;
  if (selector.length() == 0) {
    for (JsonVariant s : schedules) {
      if (s["enabled"] | false) {
        selected = s;
        break;
      }
    }
  } else {
    int wanted = 0;
    if (!tryParseInt(selector, wanted) || wanted <= 0) {
      Serial.println("Usage: sched_run [id]. Example: sched_run 12");
      return;
    }
    for (JsonVariant s : schedules) {
      if ((s["id"] | -1) == wanted) {
        selected = s;
        break;
      }
    }
  }

  if (selected.isNull()) {
    if (selector.length() == 0) {
      Serial.println("No enabled schedule found.");
    } else {
      Serial.printf("Schedule id=%s not found in local config.\n", selector.c_str());
    }
    return;
  }

  bool enabled = selected["enabled"] | false;
  float amt = selected["feeding_amount_kg"] | 0.0f;
  int id = selected["id"] | -1;
  const char* name = selected["schedule_name"] | "(unnamed)";
  const char* timeStr = selected["time"] | "--:--";

  if (!enabled) {
    Serial.printf("Schedule id=%d is disabled; not running.\n", id);
    return;
  }
  if (amt <= 0.0f) {
    Serial.printf("Schedule id=%d has invalid amount %.3f; not running.\n", id, amt);
    return;
  }

  Serial.printf("Manual run schedule id=%d name=%s time=%s amount=%.3f (time ignored)\n", id, name, timeStr, amt);
  JsonVariant cfg = gSerialCfgDoc.as<JsonVariant>();
  if (isFeedSufficient(amt, cfg)) {
    dispenseFeed(amt, cfg);
    Serial.println("Manual schedule run complete.");
  } else {
    Serial.println("Manual schedule run blocked: insufficient feed.");
    sendAlert("low_feed");
  }
}

void printSerialHelp() {
  Serial.println("================ SMART FEEDER SERIAL COMMANDS ================");
  Serial.println("help         -> show this command list");
  Serial.println("dump_cfg     -> print full local JSON config from Preferences");
  Serial.println("dump_state   -> print runtime state snapshot");
  Serial.println("dump_prefs   -> print key preferences values");
  Serial.println("sched_list   -> list schedules from local config");
  Serial.println("sched_run    -> run schedule now (ignore time): sched_run [id]");
  Serial.println("sim_set      -> set sim value: sim_set feeder|water|mains <value>");
  Serial.println("sim_defaults -> reset sim values to defaults");
  Serial.println("keypad_input -> toggle keypad polling: keypad_input [toggle|on|off|status]");
  Serial.println("keypad_cal   -> interactive keypad ADC calibration wizard");
  Serial.println("===============================================================");
}

static void keypadInputCommand(const String& args) {
  String action = args;
  action.trim();
  action.toLowerCase();

  bool current = isKeypadInputEnabled();
  bool next = current;

  if (action.length() == 0 || action == "toggle") {
    next = !current;
  } else if (action == "on" || action == "1" || action == "true") {
    next = true;
  } else if (action == "off" || action == "0" || action == "false") {
    next = false;
  } else if (action == "status") {
    Serial.printf("keypad_input_enabled=%d\n", current ? 1 : 0);
    return;
  } else {
    Serial.println("Usage: keypad_input [toggle|on|off|status]");
    return;
  }

  String cfg = loadLocalConfig();
  DynamicJsonDocument d(8192);
  d.clear();
  DeserializationError err = deserializeJson(d, cfg);
  if (err) {
    Serial.printf("Failed to parse config for keypad_input save: %s\n", err.c_str());
    return;
  }

  d["keypad_input_enabled"] = next;
  d["last_updated"] = getUtcIsoNow();
  d["updated_by"] = "esp32";

  String out;
  serializeJson(d, out);
  saveLocalConfig(out);
  setKeypadInputEnabled(next);
  Serial.printf("keypad_input_enabled=%d\n", next ? 1 : 0);
}

void dumpLocalConfigToSerial() {
  String cfg = loadLocalConfig();
  Serial.println("----- BEGIN ESP32 LOCAL CONFIG JSON -----");
  Serial.println(cfg);
  Serial.println("----- END ESP32 LOCAL CONFIG JSON -----");
}

void dumpStateToSerial() {
  Serial.println("----- BEGIN ESP32 RUNTIME STATE -----");
  Serial.printf("wifi_connected=%d\n", WiFi.status() == WL_CONNECTED ? 1 : 0);
  Serial.printf("wifi_ip=%s\n", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "0.0.0.0");
  Serial.printf("mains_power_present=%d\n", state.mainsPowerPresent ? 1 : 0);
  Serial.printf("is_refilling=%d\n", state.isRefilling ? 1 : 0);
  Serial.printf("sim_feeder_level_pct=%.2f\n", state.simFeederLevelPct);
  Serial.printf("sim_water_level_pct=%.2f\n", state.simWaterLevelPct);
  Serial.printf("remaining_kg=%.4f\n", readRemainingKg());
  Serial.printf("uptime_ms=%lu\n", millis());
  Serial.println("----- END ESP32 RUNTIME STATE -----");
}

void dumpPrefsToSerial() {
  Serial.println("----- BEGIN ESP32 PREFS SNAPSHOT -----");
  Serial.printf("namespace=%s\n", PREF_NAMESPACE);
  Serial.printf("config_key=%s\n", PREF_CONFIG_KEY);
  Serial.printf("remaining_key=%s\n", PREF_REMAINING_KG);
  Serial.printf("remaining_kg=%.4f\n", readRemainingKg());
  Serial.printf("last_feed_now_command_id=%lu\n", (unsigned long)readLastFeedNowCommandId());
  String cfg = loadLocalConfig();
  Serial.printf("config_bytes=%u\n", (unsigned int)cfg.length());
  Serial.println("----- END ESP32 PREFS SNAPSHOT -----");
}

void executeSerialCommand(const String& rawCmd) {
  String cmd = rawCmd;
  cmd.trim();
  cmd.toLowerCase();

  if (cmd.length() == 0) return;

  if (gKeypadCal.active) {
    processKeypadCalibrationLine(cmd);
    return;
  }

  LOG_INFO("Serial command received: %s", cmd.c_str());

  if (cmd == "help") {
    printSerialHelp();
    return;
  }

  if (cmd == "dump_cfg") {
    dumpLocalConfigToSerial();
    return;
  }

  if (cmd == "dump_state") {
    dumpStateToSerial();
    return;
  }

  if (cmd == "dump_prefs") {
    dumpPrefsToSerial();
    return;
  }

  if (cmd == "sched_list") {
    listSchedulesCommand();
    return;
  }

  if (cmd == "sched_run") {
    runScheduleNowCommand("");
    return;
  }

  if (cmd.startsWith("sched_run ")) {
    runScheduleNowCommand(cmd.substring(10));
    return;
  }

  if (cmd.startsWith("sim_set ")) {
    setSimValueCommand(cmd.substring(8));
    return;
  }

  if (cmd == "sim_defaults") {
    resetSimDefaultsCommand();
    return;
  }

  if (cmd == "keypad_input" || cmd.startsWith("keypad_input ")) {
    keypadInputCommand(cmd.substring(String("keypad_input").length()));
    return;
  }

  if (cmd == "keypad_cal") {
    startKeypadCalibrationSession();
    return;
  }

  Serial.printf("Unknown command: %s\n", cmd.c_str());
  printSerialHelp();
}

void handleSerialCommands() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      executeSerialCommand(serialCmdBuffer);
      serialCmdBuffer = "";
      continue;
    }
    serialCmdBuffer += c;
    if (serialCmdBuffer.length() > 120) {
      serialCmdBuffer = "";
      Serial.println("Command too long; buffer cleared.");
    }
  }
}
