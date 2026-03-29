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
  bool isRefilling;
  bool mainsPowerPresent;
  float simFeederLevelPct;
  float simWaterLevelPct;
};

#endif
