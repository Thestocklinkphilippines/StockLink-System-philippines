#ifndef SF_STATE_H
#define SF_STATE_H

struct RuntimeState {
  unsigned long lastSyncMs;
  unsigned long lastScheduleCheckMs;
  unsigned long lastWaterCheckMs;
  unsigned long lastSensorReportMs;
  unsigned long lastWiFiReconnectAttemptMs;
  unsigned long lastMainsCheckMs;
  unsigned long lastKeypadPollMs;
  unsigned long lastConfigRefreshMs;
  unsigned long lastHeartbeatLogMs;
  bool isRefilling;
  bool mainsPowerPresent;
  float simFeederLevelPct;
  float simWaterLevelPct;
  float lastFeederLevelPct;
  float lastWaterLevelPct;
  bool buzzerAlarmActive;
  bool buzzerToneOn;
  unsigned char buzzerPatternStep;
  unsigned long buzzerPhaseStartedMs;
  unsigned long buzzerAlarmCycleStartedMs;
  bool buzzerResolvedToneActive;
  unsigned char buzzerResolvedStep;
  unsigned long buzzerResolvedPhaseStartedMs;
  bool pendingPowerOutageAlert;
  bool pendingPowerRestoredAlert;
};

#endif
