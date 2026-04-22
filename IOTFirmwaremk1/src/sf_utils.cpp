#include "sf_utils.h"

#include <ctype.h>
#include <stdlib.h>

#include "sf_config.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_pins.h"
#include "sf_sensors.h"

float clampf(float v, float minV, float maxV) {
  if (v < minV) return minV;
  if (v > maxV) return maxV;
  return v;
}

String getUtcIsoNow() {
  time_t now;
  time(&now);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

time_t parseIsoUtc(const char* iso) {
  auto parseTzOffsetSeconds = [](const char* tzPart) -> int {
    if (!tzPart || *tzPart == '\0') return 0;
    if (*tzPart == 'Z' || *tzPart == 'z') return 0;
    if (*tzPart != '+' && *tzPart != '-') return 0;

    int sign = (*tzPart == '-') ? -1 : 1;
    int hh = 0;
    int mm = 0;
    const char* p = tzPart + 1;

    if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) return 0;
    hh = (p[0] - '0') * 10 + (p[1] - '0');
    p += 2;

    if (*p == ':') {
      p++;
      if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) return sign * hh * 3600;
      mm = (p[0] - '0') * 10 + (p[1] - '0');
    } else if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
      mm = (p[0] - '0') * 10 + (p[1] - '0');
    }

    return sign * (hh * 3600 + mm * 60);
  };

  struct tm tm = {};
  if (!iso) return 0;

  char* end = strptime(iso, "%Y-%m-%dT%H:%M:%S", &tm);
  if (end == NULL) {
    end = strptime(iso, "%Y-%m-%d %H:%M:%S", &tm);
  }
  if (end == NULL) return 0;

  if (*end == '.') {
    end++;
    while (*end && isdigit((unsigned char)*end)) end++;
  }

  int tzOffsetSeconds = parseTzOffsetSeconds(end);

  char* tz = getenv("TZ");
  setenv("TZ", "UTC", 1);
  tzset();
  time_t t = mktime(&tm);
  if (tz) setenv("TZ", tz, 1); else unsetenv("TZ");
  tzset();
  if (t <= 0) return 0;

  return t - tzOffsetSeconds;
}

void serviceLevelErrorBuzzer(JsonVariant cfg) {
  float feederLowThreshold = getConfigOrDefault(
      cfg,
      "alert_feeder_low_threshold_pct",
      getConfigOrDefault(cfg, "feeder_low_threshold_pct", DEFAULT_ALERT_FEEDER_LOW_THRESHOLD_PCT));
  float waterLowThreshold = getConfigOrDefault(
      cfg,
      "alert_water_low_threshold_pct",
      getConfigOrDefault(cfg, "water_low_threshold_pct", DEFAULT_ALERT_WATER_LOW_THRESHOLD_PCT));

  bool alarmActive = state.lastFeederLevelPct <= feederLowThreshold || state.lastWaterLevelPct <= waterLowThreshold;
  unsigned long nowMs = millis();

  if (alarmActive != state.buzzerAlarmActive) {
    state.buzzerAlarmActive = alarmActive;
    state.buzzerPhaseStartedMs = nowMs;
    state.buzzerAlarmCycleStartedMs = nowMs;
    state.buzzerPatternStep = 0;

    if (alarmActive) {
      LOG_WARN("Error tone enabled feeder=%.1f water=%.1f", state.lastFeederLevelPct, state.lastWaterLevelPct);
      state.buzzerResolvedToneActive = false;
      state.buzzerResolvedStep = 0;
      tone(PIN_BUZZER, BUZZER_ERROR_TONE_A_HZ);
      state.buzzerToneOn = true;
    } else {
      LOG_INFO("Error tone cleared feeder=%.1f water=%.1f", state.lastFeederLevelPct, state.lastWaterLevelPct);
      noTone(PIN_BUZZER);
      state.buzzerToneOn = false;
      state.buzzerResolvedToneActive = true;
      state.buzzerResolvedStep = 0;
      state.buzzerResolvedPhaseStartedMs = nowMs;
    }
    return;
  }

  if (alarmActive) {
    unsigned long cycleElapsedMs = nowMs - state.buzzerAlarmCycleStartedMs;
    if (cycleElapsedMs >= BUZZER_ERROR_ALERT_INTERVAL_MS) {
      state.buzzerAlarmCycleStartedMs = nowMs;
      state.buzzerPhaseStartedMs = nowMs;
      state.buzzerPatternStep = 0;
      cycleElapsedMs = 0;
    }

    if (cycleElapsedMs >= BUZZER_ERROR_ALERT_WINDOW_MS) {
      if (state.buzzerToneOn) {
        noTone(PIN_BUZZER);
        state.buzzerToneOn = false;
      }
      return;
    }

    unsigned long elapsedMs = nowMs - state.buzzerPhaseStartedMs;
    if (state.buzzerToneOn) {
      unsigned long targetMs = (state.buzzerPatternStep == 0) ? BUZZER_ERROR_TONE_A_MS : BUZZER_ERROR_TONE_E_MS;
      if (elapsedMs >= targetMs) {
        noTone(PIN_BUZZER);
        state.buzzerToneOn = false;
        state.buzzerPhaseStartedMs = nowMs;
        state.buzzerPatternStep = (state.buzzerPatternStep == 0) ? 1 : 0;
      }
    } else if (elapsedMs >= BUZZER_ERROR_TONE_GAP_MS) {
      unsigned int toneHz = (state.buzzerPatternStep == 0) ? BUZZER_ERROR_TONE_A_HZ : BUZZER_ERROR_TONE_E_HZ;
      tone(PIN_BUZZER, toneHz);
      state.buzzerToneOn = true;
      state.buzzerPhaseStartedMs = nowMs;
    }
    return;
  }

  if (state.buzzerToneOn) {
    noTone(PIN_BUZZER);
    state.buzzerToneOn = false;
  }

  if (!state.buzzerResolvedToneActive) {
    return;
  }

  unsigned long resolvedElapsedMs = nowMs - state.buzzerResolvedPhaseStartedMs;
  if (state.buzzerResolvedStep == 0) {
    tone(PIN_BUZZER, BUZZER_RESOLVED_TONE_C_HZ, BUZZER_RESOLVED_TONE_C_MS);
    state.buzzerResolvedStep = 1;
    state.buzzerResolvedPhaseStartedMs = nowMs;
    return;
  }

  if (state.buzzerResolvedStep == 1 && resolvedElapsedMs >= BUZZER_RESOLVED_TONE_GAP_MS) {
    tone(PIN_BUZZER, BUZZER_RESOLVED_TONE_E_HZ, BUZZER_RESOLVED_TONE_E_MS);
    state.buzzerResolvedStep = 2;
    state.buzzerResolvedPhaseStartedMs = nowMs;
    return;
  }

  if (state.buzzerResolvedStep == 2 && resolvedElapsedMs >= BUZZER_RESOLVED_TONE_E_MS) {
    noTone(PIN_BUZZER);
    state.buzzerResolvedToneActive = false;
    state.buzzerResolvedStep = 0;
    state.buzzerResolvedPhaseStartedMs = nowMs;
  }
}
