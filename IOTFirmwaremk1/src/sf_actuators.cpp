#include "sf_actuators.h"

#include "sf_config.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_network.h"
#include "sf_pins.h"
#include "sf_sensors.h"
#include "sf_storage.h"
#include "sf_utils.h"

bool isFeedSufficient(float requiredKg, JsonVariant cfg) {
  float maxCap = getConfigOrDefault(cfg, "max_feeds_capacity_kg", DEFAULT_MAX_FEEDS_CAPACITY_KG);
  if (maxCap <= 0.0f) maxCap = DEFAULT_MAX_FEEDS_CAPACITY_KG;
  float rem = readRemainingKg();
  if (rem < 0.0f || rem > maxCap * 2.0f) {
    rem = (getFeederLevelPct(cfg) / 100.0f) * maxCap;
  }
  LOG_DEBUG("Feed sufficiency check required=%.3fkg remaining=%.3fkg", requiredKg, rem);
  return rem >= requiredKg;
}

void dispenseFeed(float amountKg, JsonVariant cfg) {
  LOG_INFO("Dispense request amount=%.3fkg", amountKg);
#if SF_SIMULATION_MODE
  LOG_INFO("SIM mode: virtual feed dispense executed");
#else
  digitalWrite(PIN_FEED_MOTOR, HIGH);
  unsigned long runMs = (unsigned long)(amountKg * 4000.0f);
  if (runMs < 300) runMs = 300;
  delay(runMs);
  digitalWrite(PIN_FEED_MOTOR, LOW);
#endif

  float rem = readRemainingKg();
  rem -= amountKg;
  if (rem < 0.0f) rem = 0.0f;
  writeRemainingKg(rem);

  StaticJsonDocument<256> p;
  p["amount_kg"] = amountKg;
  p["remaining_kg"] = rem;
  p["simulated"] = SF_SIMULATION_MODE ? true : false;
  p["feeder_level_pct"] = getFeederLevelPct(cfg);
  sendLog("feeding", p.as<JsonVariant>());
}

void attemptRefill(JsonVariant cfg) {
  LOG_INFO("Attempt refill invoked");
#if SF_SIMULATION_MODE
  state.simWaterLevelPct = clampf(state.simWaterLevelPct + 8.0f, 0.0f, 100.0f);
  LOG_INFO("SIM refill water pct now=%.1f", state.simWaterLevelPct);
#else
  digitalWrite(PIN_WATER_SOLENOID, HIGH);
  delay(4000);
  digitalWrite(PIN_WATER_SOLENOID, LOW);
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
