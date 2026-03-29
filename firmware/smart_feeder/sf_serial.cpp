#include "sf_serial.h"

#include <WiFi.h>
#include <stdlib.h>

#include "sf_config.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_storage.h"
#include "sf_utils.h"

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

void printSerialHelp() {
  Serial.println("================ SMART FEEDER SERIAL COMMANDS ================");
  Serial.println("help         -> show this command list");
  Serial.println("dump_cfg     -> print full local JSON config from Preferences");
  Serial.println("dump_state   -> print runtime state snapshot");
  Serial.println("dump_prefs   -> print key preferences values");
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
