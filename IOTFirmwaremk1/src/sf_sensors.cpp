#include "sf_sensors.h"

#include <WiFi.h>

#include "sf_adc.h"
#include "sf_config.h"
#include "sf_debug.h"
#include "keypad_calc.h"
#include "sf_globals.h"
#include "sf_network.h"
#include "sf_pins.h"
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
bool gKeypadCalibrationLoaded = false;
unsigned long gLastKeypadCalibrationLoadMs = 0;
bool gKeypadInputEnabled = SF_ENABLE_KEYPAD_INPUT != 0;

static KeypadAdcTuning buildDefaultKeypadTuning() {
  KeypadAdcTuning tuning;
  tuning.toleranceAdc = KEYPAD_ADC_TOLERANCE;
  tuning.releaseHysteresisAdc = KEYPAD_ADC_RELEASE_HYSTERESIS;
  tuning.idleBandAdc = KEYPAD_IDLE_BAND_ADC;
  tuning.windowEdgePadAdc = KEYPAD_WINDOW_EDGE_PAD_ADC;
  tuning.adcOffset = KEYPAD_ADC_OFFSET;
  tuning.samples = KEYPAD_RUNTIME_SAMPLES;
  return tuning;
}

KeypadAdcTuning gKeypadTuning = buildDefaultKeypadTuning();

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

// Adapter used by the extracted keypad calculator: returns a single fresh ADC reading.
static int keypadAdcReader() {
  return readAdcFast(PIN_KEYPAD_ADC);
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
    LOG_KEYPAD_WARN("Keypad calibration reload skipped; config parse error: %s", err.c_str());
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
  bool keypadEnabled = cfgRoot["keypad_input_enabled"] | gKeypadInputEnabled;
  if (cfgRoot.containsKey("keypad_calibration")) {
    JsonVariant kcEnable = cfgRoot["keypad_calibration"];
    keypadEnabled = kcEnable["enabled"] | keypadEnabled;
  }
  gKeypadInputEnabled = keypadEnabled;

  LOG_KEYPAD_INFO("Keypad config reload enabled=%d has_cal=%d", (int)gKeypadInputEnabled, (int)cfgRoot.containsKey("keypad_calibration"));

  JsonVariant kc = cfgRoot["keypad_calibration"];
  if (kc.isNull()) {
    // Keep the last known-good calibration instead of falling back to defaults.
    resetKeypadCalcState();
    return;
  }

  int loaded[16];
  for (int i = 0; i < 16; i++) {
    loaded[i] = kc[kKeypadFieldNames[i]] | gKeypadCenters[i];
  }

  int noKey = kc["no_key_adc"] | gKeypadNoKeyMin;
  bool idleLow = kc["idle_is_low"] | gKeypadIdleIsLow;
  if (noKey <= 16) {
    idleLow = true;
  }
  if (!hasValidMonotonicCenters(loaded, 16)) {
    LOG_KEYPAD_WARN("Keypad calibration reload skipped; stored centers are invalid");
    return;
  }

  if (noKey < 0) noKey = 0;
  if (noKey > 4095) noKey = 4095;

  for (int i = 0; i < 16; i++) gKeypadCenters[i] = loaded[i];
  gKeypadNoKeyMin = noKey;
  gKeypadIdleIsLow = idleLow;
  resetKeypadCalcState();
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

static float distanceToLevelPctInRange(float distanceCm, float bottomDistanceCm, float fullDistanceCm) {
  if (distanceCm < 0.0f) return 0.0f;
  if (bottomDistanceCm <= 0.0f) return 0.0f;

  float boundedBottom = bottomDistanceCm;
  float boundedFull = fullDistanceCm;
  if (boundedFull < 0.0f) boundedFull = 0.0f;
  if (boundedFull >= boundedBottom) {
    return distanceToLevelPct(distanceCm, boundedBottom);
  }

  float boundedDistance = clampf(distanceCm, boundedFull, boundedBottom);
  float usableSpan = boundedBottom - boundedFull;
  if (usableSpan <= 0.0f) return distanceToLevelPct(distanceCm, boundedBottom);

  float levelPct = ((boundedBottom - boundedDistance) / usableSpan) * 100.0f;
  return clampf(levelPct, 0.0f, 100.0f);
}

float getFeederLevelPct(JsonVariant cfg) {
  float bottomDistance = getConfigOrDefault(
      cfg,
      "feeder_tank_bottom_distance_cm",
      getConfigOrDefault(cfg, "feeder_tank_depth_cm", FEEDER_TANK_DEPTH_CM));
  float fullDistance = getConfigOrDefault(
      cfg,
      "feeder_tank_full_distance_cm",
      getConfigOrDefault(cfg, "feeder_max_feed_height_cm", FEEDER_MAX_FEED_HEIGHT_CM));
  float d = measureDistanceCm(PIN_FEEDER_TRIG, PIN_FEEDER_ECHO);
  SFMultiConsole.systemPrintf("[FEEDER RAW] distance_cm=%.2f bottom_cm=%.2f full_cm=%.2f\n",
                              d,
                              bottomDistance,
                              fullDistance);
  if (d < MIN_VALID_DISTANCE_CM || d > MAX_VALID_DISTANCE_CM) {
    LOG_WARN("Feeder ultrasonic out-of-range %.2fcm", d);
    state.lastFeederLevelPct = 0.0f;
    return 0.0f;
  }
  float pct = distanceToLevelPctInRange(d, bottomDistance, fullDistance);
  state.lastFeederLevelPct = pct;
  LOG_DEBUG("Feeder ultrasonic dist=%.2fcm bottom=%.2fcm full=%.2fcm pct=%.1f",
            d,
            bottomDistance,
            fullDistance,
            pct);
  return pct;
}

float getWaterLevelPct(JsonVariant cfg) {
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
}

float getBatteryVoltageV(JsonVariant cfg) {
  bool enabled = cfg.isNull() ? true : (cfg["battery_sense_enabled"] | true);
  if (!enabled) return -1.0f;

  int adcPin = cfg.isNull() ? BATTERY_ADC_PIN : (cfg["battery_adc_pin"] | BATTERY_ADC_PIN);
  float dividerTop = cfg.isNull() ? BATTERY_DIVIDER_TOP_OHMS : (cfg["battery_divider_top_ohms"] | BATTERY_DIVIDER_TOP_OHMS);
  float dividerBottom = cfg.isNull() ? BATTERY_DIVIDER_BOTTOM_OHMS : (cfg["battery_divider_bottom_ohms"] | BATTERY_DIVIDER_BOTTOM_OHMS);
  float adcRefV = cfg.isNull() ? BATTERY_ADC_REFERENCE_V : (cfg["battery_adc_reference_v"] | BATTERY_ADC_REFERENCE_V);
  float gainCorrection = cfg.isNull() ? BATTERY_ADC_GAIN_CORRECTION : (cfg["battery_adc_gain_correction"] | BATTERY_ADC_GAIN_CORRECTION);

  if (dividerTop <= 0.0f || dividerBottom <= 0.0f || adcRefV <= 0.0f || gainCorrection <= 0.0f) {
    LOG_WARN("Battery sense config invalid top=%.1f bottom=%.1f ref=%.2f gain=%.3f",
             dividerTop,
             dividerBottom,
             adcRefV,
             gainCorrection);
    return -1.0f;
  }

  int raw = readAdcReliable(adcPin, ADC_PRIORITY_NORMAL);
  if (raw < 0) {
    LOG_WARN("Battery ADC read failed pin=%d", adcPin);
    return -1.0f;
  }

  float adcVoltage = ((float)raw / 4095.0f) * adcRefV;
  float batteryVoltage = adcVoltage * ((dividerTop + dividerBottom) / dividerBottom) * gainCorrection;
  LOG_DEBUG("Battery sense pin=%d raw=%d adcV=%.3f battV=%.3f gain=%.3f",
            adcPin,
            raw,
            adcVoltage,
            batteryVoltage,
            gainCorrection);
  return batteryVoltage;
}

char decodeKeypadAnalog(int adc) {
  if (!gKeypadCalibrationLoaded) reloadKeypadCalibration();

  int adjustedAdc = adc + gKeypadTuning.adcOffset;
  if (adjustedAdc < 0) adjustedAdc = 0;
  if (adjustedAdc > 4095) adjustedAdc = 4095;

  if (gKeypadIdleIsLow) {
    if (adjustedAdc <= gKeypadNoKeyMin + gKeypadTuning.idleBandAdc) return '\0';
  } else {
    if (adjustedAdc >= gKeypadNoKeyMin - gKeypadTuning.idleBandAdc) return '\0';
  }

  int bestIndex = -1;
  int bestDelta = 4096;
  for (int i = 0; i < 16; i++) {
    int delta = abs(adjustedAdc - gKeypadCenters[i]);
    if (delta < bestDelta) {
      bestDelta = delta;
      bestIndex = i;
    }
  }

  if (bestIndex < 0) return '\0';
  return (bestDelta <= gKeypadTuning.toleranceAdc) ? kKeypadKeys[bestIndex] : '\0';
}

void pollKeypad() {
  if (!gKeypadInputEnabled) return;

  if ((millis() - gLastKeypadCalibrationLoadMs) > 5000UL) {
    reloadKeypadCalibration();
  }

  // Use extracted calculator which may perform its own sampling/averaging.
  int rawAdc = 0;
  int adjustedAdc = 0;
  int appliedOffset = 0;
  char candidate = calculateKeypadKey(keypadAdcReader,
                                      gKeypadCenters,
                                      gKeypadNoKeyMin,
                                      gKeypadIdleIsLow,
                                      gKeypadTuning,
                                      &rawAdc,
                                      &adjustedAdc,
                                      &appliedOffset);

  char disp = (candidate == '\0') ? '-' : candidate;
  LOG_KEYPAD_INFO("poll raw=%d adjusted=%d offset=%d enabled=%d idle_is_low=%d no_key_min=%d cand=%c",
                  rawAdc,
                  adjustedAdc,
                  appliedOffset,
                  (int)gKeypadInputEnabled,
                  (int)gKeypadIdleIsLow,
                  gKeypadNoKeyMin,
                  disp);

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

  LOG_KEYPAD_INFO("Keypad key=%c raw=%d adjusted=%d offset=%d samples=%d",
                  candidate,
                  rawAdc,
                  adjustedAdc,
                  appliedOffset,
                  gKeypadTuning.samples);
  Serial.printf("[INFO] Keypad key=%c raw=%d adjusted=%d offset=%d samples=%d\n",
                candidate,
                rawAdc,
                adjustedAdc,
                appliedOffset,
                gKeypadTuning.samples);
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
  int raw = digitalRead(PIN_MAINS_SENSE_ADC);
  bool mainsLoss = MAINS_LOSS_SIGNAL_ACTIVE_HIGH ? (raw == HIGH) : (raw == LOW);
  bool present = !mainsLoss;
  LOG_DEBUG("Mains digital=%d loss=%d present=%d", raw, mainsLoss ? 1 : 0, present ? 1 : 0);
  return present;
}

static bool loadPersistedMainsPowerPresent(bool& present) {
  if (!prefs.begin(PREF_NAMESPACE, true)) {
    LOG_ERROR("Preferences begin(read mains state) failed");
    return false;
  }

  bool hasKey = prefs.isKey(PREF_MAINS_POWER_PRESENT);
  present = prefs.getBool(PREF_MAINS_POWER_PRESENT, false);
  prefs.end();
  return hasKey;
}

static void savePersistedMainsPowerPresent(bool present) {
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    LOG_ERROR("Preferences begin(write mains state) failed");
    return;
  }

  prefs.putBool(PREF_MAINS_POWER_PRESENT, present);
  prefs.end();
}

void reconcilePowerAlertStateOnBoot() {
  bool currentPresent = readMainsPowerPresent();
  bool persistedPresent = currentPresent;
  bool hadPersistedState = loadPersistedMainsPowerPresent(persistedPresent);

  state.mainsPowerPresent = currentPresent;
  state.pendingPowerOutageAlert = false;
  state.pendingPowerRestoredAlert = false;

  if (!hadPersistedState) {
    savePersistedMainsPowerPresent(currentPresent);
    LOG_INFO("Boot power state initialized present=%d", currentPresent ? 1 : 0);
    return;
  }

  if (persistedPresent == currentPresent) {
    LOG_INFO("Boot power state unchanged present=%d", currentPresent ? 1 : 0);
    return;
  }

  savePersistedMainsPowerPresent(currentPresent);

  if (currentPresent) {
    LOG_INFO("Boot detected mains restored after outage");
    sendAlert("power_restored");
    StaticJsonDocument<128> p;
    p["event"] = "mains_restored";
    sendLog("power", p.as<JsonVariant>());
  } else {
    LOG_WARN("Boot detected mains outage while device restarted on battery");
    sendAlert("power_outage");
  }
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
    savePersistedMainsPowerPresent(nowPresent);
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
  float batteryVoltage = getBatteryVoltageV(cfg);

  String url = String(SERVER_BASE) + "/device/" + DEVICE_ID + "/sensor-state/";

  StaticJsonDocument<512> doc;
  doc["feeder_level_pct"] = feederLevel;
  doc["water_level_pct"] = waterLevel;
  if (batteryVoltage >= 0.0f) {
    doc["battery_voltage_v"] = batteryVoltage;
  }
  doc["mains_power_present"] = state.mainsPowerPresent;
  doc["timestamp"] = getUtcIsoNow();

  String body;
  serializeJson(doc, body);
  String resp;
  bool ok = httpPostJson(url, body, resp);
  LOG_INFO("Sensor report ok=%d feeder=%.1f water=%.1f batt=%.2f", ok ? 1 : 0, feederLevel, waterLevel, batteryVoltage);
  if (!ok) {
    if (!WiFi.isConnected()) {
      LOG_WARN("Skipping sensor upload; WiFi disconnected");
    } else {
      LOG_WARN("Sensor report response: %s", resp.c_str());
    }
    queueBufferedRequest(url.substring(String(SERVER_BASE).length()), body, "sensor_state", false);
  }
}
