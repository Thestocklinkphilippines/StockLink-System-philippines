#include "sf_sensors.h"

#include <WiFi.h>

#include "sf_config.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_network.h"
#include "sf_pins.h"
#include "sf_utils.h"

float getConfigOrDefault(JsonVariant cfg, const char* key, float fallback) {
  if (cfg.isNull() || !cfg.containsKey(key)) return fallback;
  return cfg[key].as<float>();
}

float measureDistanceCm(int trigPin, int echoPin) {
#if SF_SIMULATION_MODE
  (void)trigPin;
  (void)echoPin;
  return 15.0f;
#else
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, 30000UL);
  if (duration == 0) {
    LOG_WARN("Ultrasonic timeout trig=%d echo=%d", trigPin, echoPin);
    return -1.0f;
  }
  return (duration * 0.0343f) / 2.0f;
#endif
}

float distanceToLevelPct(float distanceCm, float tankDepthCm) {
  if (distanceCm < 0.0f) return 0.0f;
  float boundedDistance = clampf(distanceCm, 0.0f, tankDepthCm);
  float levelPct = ((tankDepthCm - boundedDistance) / tankDepthCm) * 100.0f;
  return clampf(levelPct, 0.0f, 100.0f);
}

float getFeederLevelPct(JsonVariant cfg) {
#if SF_SIMULATION_MODE
  float decayed = state.simFeederLevelPct - 0.02f;
  state.simFeederLevelPct = clampf(decayed, 5.0f, 100.0f);
  LOG_DEBUG("SIM feeder level=%.1f", state.simFeederLevelPct);
  return state.simFeederLevelPct;
#else
  float tankDepth = getConfigOrDefault(cfg, "feeder_tank_depth_cm", FEEDER_TANK_DEPTH_CM);
  float d = measureDistanceCm(PIN_FEEDER_TRIG, PIN_FEEDER_ECHO);
  if (d < MIN_VALID_DISTANCE_CM || d > MAX_VALID_DISTANCE_CM) {
    LOG_WARN("Feeder ultrasonic out-of-range %.2fcm", d);
    return 0.0f;
  }
  float pct = distanceToLevelPct(d, tankDepth);
  LOG_DEBUG("Feeder ultrasonic dist=%.2fcm depth=%.2fcm pct=%.1f", d, tankDepth, pct);
  return pct;
#endif
}

float getWaterLevelPct(JsonVariant cfg) {
#if SF_SIMULATION_MODE
  float drift = state.isRefilling ? 0.8f : -0.05f;
  state.simWaterLevelPct = clampf(state.simWaterLevelPct + drift, 0.0f, 100.0f);
  LOG_DEBUG("SIM water level=%.1f", state.simWaterLevelPct);
  return state.simWaterLevelPct;
#else
  float tankDepth = getConfigOrDefault(cfg, "water_tank_depth_cm", WATER_TANK_DEPTH_CM);
  float d = measureDistanceCm(PIN_WATER_TRIG, PIN_WATER_ECHO);
  if (d < MIN_VALID_DISTANCE_CM || d > MAX_VALID_DISTANCE_CM) {
    LOG_WARN("Water ultrasonic out-of-range %.2fcm", d);
    return 0.0f;
  }
  float pct = distanceToLevelPct(d, tankDepth);
  LOG_DEBUG("Water ultrasonic dist=%.2fcm depth=%.2fcm pct=%.1f", d, tankDepth, pct);
  return pct;
#endif
}

char decodeKeypadAnalog(int adc) {
  if (adc >= KEYPAD_ADC_NO_KEY_MIN) return '\0';
  if (adc < 120) return '1';
  if (adc < 280) return '2';
  if (adc < 430) return '3';
  if (adc < 590) return 'A';
  if (adc < 760) return '4';
  if (adc < 940) return '5';
  if (adc < 1120) return '6';
  if (adc < 1310) return 'B';
  if (adc < 1510) return '7';
  if (adc < 1730) return '8';
  if (adc < 1960) return '9';
  if (adc < 2200) return 'C';
  if (adc < 2480) return '*';
  if (adc < 2780) return '0';
  if (adc < 3100) return '#';
  if (adc < 3450) return 'D';
  return '\0';
}

void pollKeypad() {
#if !SF_ENABLE_KEYPAD_INPUT
  return;
#else
  static char lastKey = '\0';
  int adc = analogRead(PIN_KEYPAD_ADC);
  char key = decodeKeypadAnalog(adc);
  if (key != '\0' && key != lastKey) {
    LOG_INFO("Keypad key=%c adc=%d", key, adc);
    if (key == 'A' && SF_SEND_KEYPAD_LOGS) {
      StaticJsonDocument<64> p;
      p["source"] = "keypad";
      p["key"] = "A";
      sendLog("ui", p.as<JsonVariant>());
    }
  }
  lastKey = key;
#endif
}

bool readMainsPowerPresent() {
  int raw = digitalRead(PIN_MAINS_SENSE_ADC);
  bool present = (raw == HIGH);
  LOG_DEBUG("Mains digital=%d present=%d", raw, present ? 1 : 0);
  return present;
}

void handlePowerFailMonitoring() {
  bool nowPresent = readMainsPowerPresent();
  if (nowPresent != state.mainsPowerPresent) {
    state.mainsPowerPresent = nowPresent;
    if (!nowPresent) {
      LOG_WARN("Power outage detected (UPS active)");
      tone(PIN_BUZZER, 2500, 250);
      sendAlert("power_outage");
    } else {
      LOG_INFO("Mains power restored");
      StaticJsonDocument<128> p;
      p["event"] = "mains_restored";
      sendLog("power", p.as<JsonVariant>());
    }
  }
}

void reportSensorLevels(JsonVariant cfg) {
  if (!WiFi.isConnected()) {
    LOG_WARN("Skipping sensor report; WiFi disconnected");
    return;
  }

  float feederLevel = getFeederLevelPct(cfg);
  float waterLevel = getWaterLevelPct(cfg);
  String url = String(SERVER_BASE) + "/device/" + DEVICE_ID + "/sensor-state/";

  StaticJsonDocument<512> doc;
  doc["feeder_level_pct"] = feederLevel;
  doc["water_level_pct"] = waterLevel;
  doc["mains_power_present"] = state.mainsPowerPresent;
  doc["timestamp"] = getUtcIsoNow();
  doc["simulated"] = SF_SIMULATION_MODE ? true : false;

  String body;
  serializeJson(doc, body);
  String resp;
  bool ok = httpPostJson(url, body, resp);
  LOG_INFO("Sensor report ok=%d feeder=%.1f water=%.1f", ok ? 1 : 0, feederLevel, waterLevel);
  if (!ok) LOG_WARN("Sensor report response: %s", resp.c_str());
}
