#include "sf_scheduler.h"

#include <time.h>

#include "sf_actuators.h"
#include "sf_config.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_network.h"
#include "sf_sensors.h"
#include "sf_simulation.h"
#include "sf_storage.h"

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

  float feederThreshold = getConfigOrDefault(
      cfg,
      "alert_feeder_low_threshold_pct",
      getConfigOrDefault(cfg, "feeder_low_threshold_pct", DEFAULT_ALERT_FEEDER_LOW_THRESHOLD_PCT));
  float feederLevelPct = getFeederLevelPct(cfg);
  LOG_DEBUG("Low-feed prediction requiredNext=%.3fkg feederPct=%.1f alertThreshold=%.1f",
            required,
            feederLevelPct,
            feederThreshold);

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

void processFeedNowCommand(JsonVariant cfg) {
  static uint32_t sLastDuplicateLoggedId = 0;

  JsonVariant cmd = cfg["feed_now_command"];
  if (cmd.isNull()) return;

  long cmdIdRaw = cmd["command_id"] | -1;
  if (cmdIdRaw <= 0) {
    cmdIdRaw = cmd["id"] | -1;
  }
  if (cmdIdRaw <= 0) {
    LOG_WARN("feed_now_command ignored; invalid command id (expected command_id or id)");
    return;
  }

  uint32_t commandId = (uint32_t)cmdIdRaw;
  uint32_t lastAckId = readLastFeedNowCommandId();
  if (commandId <= lastAckId) {
    if (sLastDuplicateLoggedId != commandId) {
      LOG_DEBUG("feed_now_command duplicate id=%lu lastAck=%lu",
                (unsigned long)commandId,
                (unsigned long)lastAckId);
      sLastDuplicateLoggedId = commandId;
    }
    return;
  }
  sLastDuplicateLoggedId = 0;

  float amountKg = cmd["amount_kg"] | -1.0f;
  bool executeOk = false;
  const char* status = "failed";
  String reason = "";

  float maxCap = getConfigOrDefault(cfg, "max_feeds_capacity_kg", DEFAULT_MAX_FEEDS_CAPACITY_KG);
  if (maxCap <= 0.0f) maxCap = DEFAULT_MAX_FEEDS_CAPACITY_KG;
  float maxSingle = getConfigOrDefault(cfg, "max_single_feed_kg", DEFAULT_MAX_SINGLE_FEED_KG);
  if (maxSingle <= 0.0f) maxSingle = DEFAULT_MAX_SINGLE_FEED_KG;
  float safeMaxKg = (maxSingle < maxCap) ? maxSingle : maxCap;

  if (amountKg <= 0.0f) {
    reason = "invalid_amount";
  } else if (amountKg > safeMaxKg) {
    reason = "amount_exceeds_limit";
  } else if (SF_SIMULATE_FEED_MOTOR) {
    reason = "simulation_mode_enabled";
  } else if (!isFeedSufficient(amountKg, cfg)) {
    reason = "insufficient_feed";
  } else {
    dispenseFeed(amountKg, cfg);
    executeOk = true;
    status = "executed";
  }

  bool ackOk = sendFeedNowAck(commandId, status, executeOk ? nullptr : reason.c_str());
  if (!ackOk) {
    LOG_WARN("feed_now ack upload failed for id=%lu", (unsigned long)commandId);
  }

  if (!executeOk) {
    LOG_WARN("feed_now rejected id=%lu amount=%.3f reason=%s safeMax=%.3f maxCap=%.3f",
             (unsigned long)commandId,
             amountKg,
             reason.c_str(),
             safeMaxKg,
             maxCap);
  }

  // Persist dedup watermark even if ack upload fails to avoid duplicate motor runs.
  writeLastFeedNowCommandId(commandId);

  StaticJsonDocument<256> p;
  p["event"] = "feed_now";
  p["command_id"] = commandId;
  p["amount_kg"] = amountKg;
  p["status"] = status;
  if (!executeOk) p["reason"] = reason;
  sendLog("feeding", p.as<JsonVariant>());

  LOG_INFO("feed_now handled id=%lu amount=%.3f status=%s reason=%s",
           (unsigned long)commandId,
           amountKg,
           status,
           executeOk ? "-" : reason.c_str());
}
