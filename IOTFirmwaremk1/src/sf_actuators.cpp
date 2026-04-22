#include "sf_actuators.h"

#include "sf_config.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_network.h"
#include "sf_pins.h"
#include "sf_sensors.h"
#include "sf_simulation.h"
#include "sf_storage.h"
#include "sf_utils.h"

static void delayWithKeypadPolling(unsigned long totalMs) {
  unsigned long startMs = millis();
  while ((millis() - startMs) < totalMs) {
    if (isKeypadInputEnabled()) {
      pollKeypad();
    }

    unsigned long elapsed = millis() - startMs;
    unsigned long remaining = (elapsed < totalMs) ? (totalMs - elapsed) : 0UL;
    unsigned long slice = remaining > 20UL ? 20UL : remaining;
    if (slice == 0UL) break;
    delay(slice);
  }
}

static void playFeedingEventTone() {
  // Keep this cue short so it does not materially delay dispense.
  tone(PIN_BUZZER, BUZZER_FEED_TONE_C_HZ, BUZZER_FEED_TONE_C_MS);
  delayWithKeypadPolling(BUZZER_FEED_TONE_C_MS + BUZZER_FEED_TONE_GAP_MS);
  tone(PIN_BUZZER, BUZZER_FEED_TONE_E_HZ, BUZZER_FEED_TONE_E_MS);
  delayWithKeypadPolling(BUZZER_FEED_TONE_E_MS);
  noTone(PIN_BUZZER);
}

void setFeedMotorEnabled(bool enabled) {
  int activeState = FEED_MOTOR_ACTIVE_HIGH ? HIGH : LOW;
  int idleState = FEED_MOTOR_ACTIVE_HIGH ? LOW : HIGH;
  digitalWrite(PIN_FEED_MOTOR, enabled ? activeState : idleState);
}

void setWaterSolenoidEnabled(bool enabled) {
  int activeState = WATER_SOLENOID_ACTIVE_HIGH ? HIGH : LOW;
  int idleState = WATER_SOLENOID_ACTIVE_HIGH ? LOW : HIGH;
  digitalWrite(PIN_WATER_SOLENOID, enabled ? activeState : idleState);
}

static float resolveGrainMsPerKg(JsonVariant cfg) {
  const char* grain = DEFAULT_GRAIN_TYPE;
  if (!cfg.isNull() && cfg.containsKey("grain_type")) {
    grain = cfg["grain_type"] | DEFAULT_GRAIN_TYPE;
  }

  if (strcmp(grain, "pellet_large") == 0) return FEED_MS_PER_KG_PELLET_LARGE;
  if (strcmp(grain, "crumble") == 0) return FEED_MS_PER_KG_CRUMBLE;
  if (strcmp(grain, "mash") == 0) return FEED_MS_PER_KG_MASH;
  return FEED_MS_PER_KG_PELLET_SMALL;
}

unsigned long computeFeedMotorRunMs(float amountKg, JsonVariant cfg) {
  float safeAmountKg = amountKg;
  if (safeAmountKg < 0.0f) safeAmountKg = 0.0f;

  // Primary calibration equation for feeder runtime:
  // run_ms = startup_ms + amount_kg * grain_ms_per_kg
  float msPerKg = resolveGrainMsPerKg(cfg);
  float runMs = FEED_MOTOR_STARTUP_MS + (safeAmountKg * msPerKg);
  if (runMs < FEED_MOTOR_STARTUP_MS) runMs = FEED_MOTOR_STARTUP_MS;
  return (unsigned long)runMs;
}

bool isFeedSufficient(float requiredKg, JsonVariant cfg) {
  float maxCap = getConfigOrDefault(cfg, "max_feeds_capacity_kg", DEFAULT_MAX_FEEDS_CAPACITY_KG);
  if (maxCap <= 0.0f) maxCap = DEFAULT_MAX_FEEDS_CAPACITY_KG;

#if SF_SIMULATE_LEVEL_SENSORS
  // In simulation, feeder percentage is the source of truth for amount remaining.
  float rem = (clampf(state.simFeederLevelPct, 0.0f, 100.0f) / 100.0f) * maxCap;
  LOG_DEBUG("Feed sufficiency check required=%.3fkg remaining=%.3fkg (sim)", requiredKg, rem);
  return rem >= requiredKg;
#else
  float rem = readRemainingKg();
  if (rem < 0.0f || rem > maxCap * 2.0f) {
    rem = (getFeederLevelPct(cfg) / 100.0f) * maxCap;
  }
  LOG_DEBUG("Feed sufficiency check required=%.3fkg remaining=%.3fkg", requiredKg, rem);
  return rem >= requiredKg;
#endif
}

void dispenseFeed(float amountKg, JsonVariant cfg) {
  LOG_INFO("Dispense request amount=%.3fkg", amountKg);
  playFeedingEventTone();

  float maxCap = getConfigOrDefault(cfg, "max_feeds_capacity_kg", DEFAULT_MAX_FEEDS_CAPACITY_KG);
  if (maxCap <= 0.0f) maxCap = DEFAULT_MAX_FEEDS_CAPACITY_KG;

  float rem = 0.0f;
  float feederPct = 0.0f;
#if SF_SIMULATE_FEED_MOTOR
  LOG_INFO("SIM mode: virtual feed dispense executed");

  float simRem = readRemainingKg();
  if (simRem < 0.0f || simRem > maxCap * 2.0f) {
    simRem = (clampf(state.simFeederLevelPct, 0.0f, 100.0f) / 100.0f) * maxCap;
  }
  simRem -= amountKg;
  if (simRem < 0.0f) simRem = 0.0f;

  rem = simRem;
  feederPct = (maxCap > 0.0f) ? clampf((rem / maxCap) * 100.0f, 0.0f, 100.0f) : 0.0f;
  state.simFeederLevelPct = feederPct;
#else
  unsigned long runMs = computeFeedMotorRunMs(amountKg, cfg);
  LOG_INFO("Feed motor runMs=%lu amount=%.3fkg grain=%s",
           runMs,
           amountKg,
           (cfg.isNull() ? DEFAULT_GRAIN_TYPE : (cfg["grain_type"] | DEFAULT_GRAIN_TYPE)));
  setFeedMotorEnabled(true);
  delayWithKeypadPolling(runMs);
  setFeedMotorEnabled(false);

  rem = readRemainingKg();
  rem -= amountKg;
  if (rem < 0.0f) rem = 0.0f;
  feederPct = getFeederLevelPct(cfg);
#endif

  writeRemainingKg(rem);
  addToDailyFeedTotalKg(amountKg);

  StaticJsonDocument<256> p;
  p["amount_kg"] = amountKg;
  p["remaining_kg"] = rem;
  p["simulated"] = SF_SIMULATE_FEED_MOTOR ? true : false;
  p["feeder_level_pct"] = feederPct;
  sendLog("feeding", p.as<JsonVariant>());
}

void attemptRefill(JsonVariant cfg) {
  LOG_INFO("Attempt refill invoked");
#if SF_SIMULATE_WATER_REFILL
  state.simWaterLevelPct = clampf(state.simWaterLevelPct + 8.0f, 0.0f, 100.0f);
  LOG_INFO("SIM refill water pct now=%.1f", state.simWaterLevelPct);
#else
  setWaterSolenoidEnabled(true);
  delayWithKeypadPolling(4000);
  setWaterSolenoidEnabled(false);
#endif

  float waterPct = getWaterLevelPct(cfg);
  if (waterPct <= 5.0f) {
    LOG_WARN("Refill attempt did not recover water level");
    sendAlert("low_water");
  } else {
    StaticJsonDocument<128> p;
    p["event"] = "refill";
    p["water_level_pct"] = waterPct;
    sendLog("watering", p.as<JsonVariant>());
  }
}
