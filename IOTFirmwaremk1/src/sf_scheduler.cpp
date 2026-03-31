#include "sf_scheduler.h"

#include <time.h>

#include "sf_actuators.h"
#include "sf_config.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_network.h"
#include "sf_sensors.h"

void checkLowFeedPrediction(JsonArray schedules, JsonVariant cfg) {
  float required = 0.0f;
  int count = 0;
  for (JsonVariant s : schedules) {
    if (!s["enabled"]) continue;
    if (!s.containsKey("feeding_amount_kg")) continue;
    required += s["feeding_amount_kg"].as<float>();
    count++;
    if (count >= 2) break;
  }

  float feederThreshold = getConfigOrDefault(cfg, "feeder_low_threshold_pct", DEFAULT_FEEDER_LOW_THRESHOLD_PCT);
  float feederLevelPct = getFeederLevelPct(cfg);
  LOG_DEBUG("Low-feed prediction requiredNext=%.3fkg feederPct=%.1f threshold=%.1f", required, feederLevelPct, feederThreshold);

  if (!isFeedSufficient(required, cfg) || feederLevelPct <= feederThreshold) {
    LOG_WARN("Low feed predicted");
    sendAlert("low_feed");
  }
}

void checkSchedulesAndExecute(JsonArray schedules, JsonVariant cfg) {
  (void)cfg;
  time_t now;
  time(&now);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char nowStr[6];
  strftime(nowStr, sizeof(nowStr), "%H:%M", &timeinfo);
  String slot = String(nowStr);

  if (slot == lastScheduleSlot) {
    LOG_DEBUG("Schedule slot already processed: %s", slot.c_str());
    return;
  }

  LOG_INFO("Checking schedules for slot=%s", slot.c_str());
  for (JsonVariant s : schedules) {
    if (!s["enabled"]) continue;
    const char* t = s["time"] | "";
    if (strcmp(t, nowStr) == 0) {
      float amt = s["feeding_amount_kg"].as<float>();
      LOG_INFO("Schedule match time=%s amount=%.3fkg", t, amt);
      if (isFeedSufficient(amt, cfg)) {
        dispenseFeed(amt, cfg);
      } else {
        LOG_WARN("Insufficient feed for scheduled dispense");
        sendAlert("low_feed");
      }
    }
  }
  lastScheduleSlot = slot;
}
