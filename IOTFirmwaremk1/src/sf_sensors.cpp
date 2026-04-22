#include "sf_sensors.h"

#include <WiFi.h>

#include "sf_config.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_network.h"
#include "sf_pins.h"
#include "sf_simulation.h"
#include "sf_storage.h"
#include "sf_utils.h"

namespace {
static const uint8_t kKeyEventQueueSize = 8;
char gKeyEventQueue[kKeyEventQueueSize] = {'\0'};
uint8_t gKeyEventHead = 0;
uint8_t gKeyEventTail = 0;
uint8_t gKeyEventCount = 0;

static const char kKeypadKeys[16] = {
    '1', '2', '3', 'A',
    '4', '5', '6', 'B',
    '7', '8', '9', 'C',
    '*', '0', '#', 'D'};

static const char* kKeypadFieldNames[16] = {
    "adc_1", "adc_2", "adc_3", "adc_a",
    "adc_4", "adc_5", "adc_6", "adc_b",
    "adc_7", "adc_8", "adc_9", "adc_c",
    "adc_star", "adc_0", "adc_hash", "adc_d"};

int gKeypadCenters[16] = {
    60, 200, 355, 510,
    675, 850, 1030, 1215,
    1410, 1620, 1845, 2080,
    2340, 2630, 2940, 3275};

int gKeypadNoKeyMin = KEYPAD_ADC_NO_KEY_MIN;
bool gKeypadIdleIsLow = false;
int gKeypadIdleThreshold = 0;
bool gKeypadCalibrationLoaded = false;
unsigned long gLastKeypadCalibrationLoadMs = 0;
bool gKeypadInputEnabled = SF_ENABLE_KEYPAD_INPUT != 0;

char gLastStableKey = '\0';
char gLastCandidateKey = '\0';
uint8_t gCandidateStablePolls = 0;
unsigned long gLastKeyEventMs = 0;
}

static void enqueueKeyEvent(char key) {
  if (key == '\0') return;

  if (gKeyEventCount >= kKeyEventQueueSize) {
    gKeyEventHead = (uint8_t)((gKeyEventHead + 1U) % kKeyEventQueueSize);
    gKeyEventCount--;
  }

  gKeyEventQueue[gKeyEventTail] = key;
  gKeyEventTail = (uint8_t)((gKeyEventTail + 1U) % kKeyEventQueueSize);
  gKeyEventCount++;
}

static int sampleKeypadAdcAverageRuntime() {
  long total = 0;
  const int samples = (KEYPAD_RUNTIME_SAMPLES <= 0) ? 1 : KEYPAD_RUNTIME_SAMPLES;
  for (int i = 0; i < samples; i++) {
    total += analogRead(PIN_KEYPAD_ADC);
    delayMicroseconds(500);
  }
  return (int)(total / samples);
}

static bool hasValidMonotonicCenters(const int* values, int count) {
  if (!values || count <= 0) return false;
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
  return true;
}

void reloadKeypadCalibration() {
  gKeypadCalibrationLoaded = true;
  gLastKeypadCalibrationLoadMs = millis();

  String cfg = loadLocalConfig();
  if (cfg.length() == 0) return;

  DynamicJsonDocument d(8192);
  DeserializationError err = deserializeJson(d, cfg);
  if (err) {
    LOG_WARN("Keypad calibration reload skipped; config parse error: %s", err.c_str());
    return;
  }

  JsonVariant root = d.as<JsonVariant>();
  bool hasTopLevelKeypad = root.containsKey("keypad_input_enabled") || root.containsKey("keypad_calibration");
  JsonVariant cfgRoot = root;
  if (!hasTopLevelKeypad) {
    JsonVariant nestedCfg = root["config"];
    if (!nestedCfg.isNull()) {
      cfgRoot = nestedCfg;
    }
  }
  if (cfgRoot.isNull()) cfgRoot = root;

  // Runtime enable flag is independent from calibration; load it whenever config is readable.
  gKeypadInputEnabled = cfgRoot["keypad_input_enabled"] | gKeypadInputEnabled;

  JsonVariant kc = cfgRoot["keypad_calibration"];
  if (kc.isNull()) {
    // Keep the last known-good calibration instead of falling back to defaults.
    return;
  }

  int loaded[16];
  for (int i = 0; i < 16; i++) {
    loaded[i] = kc[kKeypadFieldNames[i]] | gKeypadCenters[i];
  }

  int noKey = kc["no_key_adc"] | gKeypadNoKeyMin;
  bool idleLow = kc["idle_is_low"] | gKeypadIdleIsLow;
  if (!hasValidMonotonicCenters(loaded, 16)) {
    LOG_WARN("Keypad calibration reload skipped; stored centers are invalid");
    return;
  }

  if (noKey < 0) noKey = 0;
  if (noKey > 4095) noKey = 4095;

  for (int i = 0; i < 16; i++) gKeypadCenters[i] = loaded[i];
  gKeypadNoKeyMin = noKey;
  gKeypadIdleIsLow = idleLow;
  gKeypadIdleThreshold = noKey;
}

bool isKeypadInputEnabled() {
  if (!gKeypadCalibrationLoaded || (millis() - gLastKeypadCalibrationLoadMs) > 5000UL) {
    reloadKeypadCalibration();
  }
  return gKeypadInputEnabled;
}

void setKeypadInputEnabled(bool enabled) {
  gKeypadInputEnabled = enabled;
}

float getConfigOrDefault(JsonVariant cfg, const char* key, float fallback) {
  if (cfg.isNull() || !cfg.containsKey(key)) return fallback;
  return cfg[key].as<float>();
}

float measureDistanceCm(int trigPin, int echoPin) {
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
}

float distanceToLevelPct(float distanceCm, float tankDepthCm) {
  if (distanceCm < 0.0f) return 0.0f;
  float boundedDistance = clampf(distanceCm, 0.0f, tankDepthCm);
  float levelPct = ((tankDepthCm - boundedDistance) / tankDepthCm) * 100.0f;
  return clampf(levelPct, 0.0f, 100.0f);
}

float getFeederLevelPct(JsonVariant cfg) {
#if SF_SIMULATE_FEEDER_LEVEL_SENSOR
  float maxCap = getConfigOrDefault(cfg, "max_feeds_capacity_kg", DEFAULT_MAX_FEEDS_CAPACITY_KG);
  if (maxCap <= 0.0f) maxCap = DEFAULT_MAX_FEEDS_CAPACITY_KG;

  float remKg = readRemainingKg();
  if (remKg < 0.0f) remKg = 0.0f;
  remKg = clampf(remKg, 0.0f, maxCap);

  state.simFeederLevelPct = (maxCap > 0.0f) ? clampf((remKg / maxCap) * 100.0f, 0.0f, 100.0f) : 0.0f;
  state.lastFeederLevelPct = state.simFeederLevelPct;
  LOG_DEBUG("SIM feeder level=%.1f remaining=%.3fkg max=%.3fkg", state.simFeederLevelPct, remKg, maxCap);
  return state.simFeederLevelPct;
#else
  float tankDepth = getConfigOrDefault(cfg, "feeder_tank_depth_cm", FEEDER_TANK_DEPTH_CM);
  float d = measureDistanceCm(PIN_FEEDER_TRIG, PIN_FEEDER_ECHO);
  if (d < MIN_VALID_DISTANCE_CM || d > MAX_VALID_DISTANCE_CM) {
    LOG_WARN("Feeder ultrasonic out-of-range %.2fcm", d);
    state.lastFeederLevelPct = 0.0f;
    return 0.0f;
  }
  float pct = distanceToLevelPct(d, tankDepth);
  state.lastFeederLevelPct = pct;
  LOG_DEBUG("Feeder ultrasonic dist=%.2fcm depth=%.2fcm pct=%.1f", d, tankDepth, pct);
  return pct;
#endif
}

float getWaterLevelPct(JsonVariant cfg) {
#if SF_SIMULATE_WATER_LEVEL_SENSOR
  float drift = state.isRefilling ? 0.8f : -0.05f;
  state.simWaterLevelPct = clampf(state.simWaterLevelPct + drift, 0.0f, 100.0f);
  state.lastWaterLevelPct = state.simWaterLevelPct;
  LOG_DEBUG("SIM water level=%.1f", state.simWaterLevelPct);
  return state.simWaterLevelPct;
#else
  float tankDepth = getConfigOrDefault(cfg, "water_tank_depth_cm", WATER_TANK_DEPTH_CM);
  float d = measureDistanceCm(PIN_WATER_TRIG, PIN_WATER_ECHO);
  if (d < MIN_VALID_DISTANCE_CM || d > MAX_VALID_DISTANCE_CM) {
    LOG_WARN("Water ultrasonic out-of-range %.2fcm", d);
    state.lastWaterLevelPct = 0.0f;
    return 0.0f;
  }
  float pct = distanceToLevelPct(d, tankDepth);
  state.lastWaterLevelPct = pct;
  LOG_DEBUG("Water ultrasonic dist=%.2fcm depth=%.2fcm pct=%.1f", d, tankDepth, pct);
  return pct;
#endif
}

char decodeKeypadAnalog(int adc) {
  if (!gKeypadCalibrationLoaded) reloadKeypadCalibration();

  if (gKeypadIdleIsLow) {
    if (adc <= gKeypadIdleThreshold + KEYPAD_IDLE_BAND_ADC) return '\0';
  } else {
    if (adc >= gKeypadIdleThreshold - KEYPAD_IDLE_BAND_ADC) return '\0';
  }

  int nearestIdx = -1;
  int nearestDist = 4096;
  for (int i = 0; i < 16; i++) {
    int d = abs(adc - gKeypadCenters[i]);
    if (d < nearestDist) {
      nearestDist = d;
      nearestIdx = i;
    }
  }

  if (nearestIdx < 0) return '\0';

  int tolerance = KEYPAD_MATCH_TOLERANCE_ADC;
  if (gLastStableKey != '\0' && kKeypadKeys[nearestIdx] == gLastStableKey) {
    tolerance += KEYPAD_SAME_KEY_HYSTERESIS_ADC;
  }
  if (nearestDist > tolerance) return '\0';

  return kKeypadKeys[nearestIdx];
}

void pollKeypad() {
  if (!gKeypadInputEnabled) return;

  if ((millis() - gLastKeypadCalibrationLoadMs) > 5000UL) {
    reloadKeypadCalibration();
  }

  int adc = sampleKeypadAdcAverageRuntime();
  char candidate = decodeKeypadAnalog(adc);

  if (candidate == gLastCandidateKey) {
    if (gCandidateStablePolls < 250U) gCandidateStablePolls++;
  } else {
    gLastCandidateKey = candidate;
    gCandidateStablePolls = 1U;
  }

  if (gCandidateStablePolls < KEYPAD_STABLE_POLLS_REQUIRED) return;
  if (candidate == gLastStableKey) return;

  gLastStableKey = candidate;
  if (candidate == '\0') return;

  unsigned long nowMs = millis();
  if ((nowMs - gLastKeyEventMs) < KEYPAD_EVENT_DEBOUNCE_MS) return;
  gLastKeyEventMs = nowMs;

  LOG_INFO("Keypad key=%c adc=%d", candidate, adc);
  enqueueKeyEvent(candidate);
  if (candidate == 'A' && SF_SEND_KEYPAD_LOGS) {
    StaticJsonDocument<64> p;
    p["source"] = "keypad";
    p["key"] = "A";
    sendLog("ui", p.as<JsonVariant>());
  }
}

char consumeKeypadKeyEvent() {
  if (gKeyEventCount == 0) return '\0';
  char out = gKeyEventQueue[gKeyEventHead];
  gKeyEventHead = (uint8_t)((gKeyEventHead + 1U) % kKeyEventQueueSize);
  gKeyEventCount--;
  return out;
}

bool readMainsPowerPresent() {
#if SF_SIMULATE_MAINS_INPUT
  return state.mainsPowerPresent;
#else
  int raw = digitalRead(PIN_MAINS_SENSE_ADC);
  bool mainsLoss = MAINS_LOSS_SIGNAL_ACTIVE_HIGH ? (raw == HIGH) : (raw == LOW);
  bool present = !mainsLoss;
  LOG_DEBUG("Mains digital=%d loss=%d present=%d", raw, mainsLoss ? 1 : 0, present ? 1 : 0);
  return present;
#endif
}

static void trySendPendingPowerAlerts() {
  if (!WiFi.isConnected()) return;

  if (state.pendingPowerOutageAlert) {
    sendAlert("power_outage");
    state.pendingPowerOutageAlert = false;
  }

  if (state.pendingPowerRestoredAlert) {
    sendAlert("power_restored");
    state.pendingPowerRestoredAlert = false;
  }
}

void handlePowerFailMonitoring() {
  bool nowPresent = readMainsPowerPresent();
  trySendPendingPowerAlerts();

  if (nowPresent != state.mainsPowerPresent) {
    state.mainsPowerPresent = nowPresent;
    if (!nowPresent) {
      LOG_WARN("Power outage detected (UPS active)");
      tone(PIN_BUZZER, 2500, 250);
      state.pendingPowerOutageAlert = true;
      state.pendingPowerRestoredAlert = false;
      trySendPendingPowerAlerts();
    } else {
      LOG_INFO("Mains power restored");
      state.pendingPowerOutageAlert = false;
      state.pendingPowerRestoredAlert = true;
      trySendPendingPowerAlerts();
      StaticJsonDocument<128> p;
      p["event"] = "mains_restored";
      sendLog("power", p.as<JsonVariant>());
    }
  }
}

void reportSensorLevels(JsonVariant cfg) {
  float feederLevel = getFeederLevelPct(cfg);
  float waterLevel = getWaterLevelPct(cfg);

  if (!WiFi.isConnected()) {
    LOG_WARN("Skipping sensor upload; WiFi disconnected");
    return;
  }

  String url = String(SERVER_BASE) + "/device/" + DEVICE_ID + "/sensor-state/";

  StaticJsonDocument<512> doc;
  doc["feeder_level_pct"] = feederLevel;
  doc["water_level_pct"] = waterLevel;
  doc["mains_power_present"] = state.mainsPowerPresent;
  doc["timestamp"] = getUtcIsoNow();
  doc["simulated"] = (SF_SIMULATE_FEEDER_LEVEL_SENSOR || SF_SIMULATE_WATER_LEVEL_SENSOR) ? true : false;
  doc["simulated_feeder"] = SF_SIMULATE_FEEDER_LEVEL_SENSOR ? true : false;
  doc["simulated_water"] = SF_SIMULATE_WATER_LEVEL_SENSOR ? true : false;

  String body;
  serializeJson(doc, body);
  String resp;
  bool ok = httpPostJson(url, body, resp);
  LOG_INFO("Sensor report ok=%d feeder=%.1f water=%.1f", ok ? 1 : 0, feederLevel, waterLevel);
  if (!ok) LOG_WARN("Sensor report response: %s", resp.c_str());
}
