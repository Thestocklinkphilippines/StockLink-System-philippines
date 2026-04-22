#ifndef SF_STORAGE_H
#define SF_STORAGE_H

#include <Arduino.h>

auto loadLocalConfig() -> String;
void saveLocalConfig(const String& jsonCfg);
auto readRemainingKg() -> float;
void writeRemainingKg(float v);
auto readLastFeedNowCommandId() -> uint32_t;
void writeLastFeedNowCommandId(uint32_t id);
void ensureDailyFeedTotalForToday();
void addToDailyFeedTotalKg(float dispensedKg);
void ensureLocalDefaults();

#endif
