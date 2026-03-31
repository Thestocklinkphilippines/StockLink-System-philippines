#include "sf_serial.h"

#include <WiFi.h>
#include <stdlib.h>

#include "sf_config.h"
#include "sf_actuators.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_network.h"
#include "sf_storage.h"
#include "sf_utils.h"

// Keep large JSON buffers off the loopTask stack.
static StaticJsonDocument<4096> gSerialCfgDoc;

static bool tryParseFloat(const String& token, float& outValue) {
  if (token.length() == 0) return false;
  char buf[32];
  token.toCharArray(buf, sizeof(buf));
  char* endPtr = nullptr;
  float v = strtof(buf, &endPtr);
  if (endPtr == buf || *endPtr != '\0') return false;
  outValue = v;
  return true;
}

static bool tryParseInt(const String& token, int& outValue) {
  if (token.length() == 0) return false;
  char buf[16];
  token.toCharArray(buf, sizeof(buf));
  char* endPtr = nullptr;
  long v = strtol(buf, &endPtr, 10);
  if (endPtr == buf || *endPtr != '\0') return false;
  outValue = (int)v;
  return true;
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
    Serial.printf("sim_feeder_level_pct set to %.2f\n", state.simFeederLevelPct);
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
  Serial.println("Simulation values reset: feeder=85.00 water=90.00 mains=1");
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
  Serial.println("===============================================================");
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
  String cfg = loadLocalConfig();
  Serial.printf("config_bytes=%u\n", (unsigned int)cfg.length());
  Serial.println("----- END ESP32 PREFS SNAPSHOT -----");
}

void executeSerialCommand(const String& rawCmd) {
  String cmd = rawCmd;
  cmd.trim();
  cmd.toLowerCase();

  if (cmd.length() == 0) return;

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
