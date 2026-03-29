#ifndef SF_STORAGE_H
#define SF_STORAGE_H

#include <Arduino.h>

auto loadLocalConfig() -> String;
void saveLocalConfig(const String& jsonCfg);
auto readRemainingKg() -> float;
void writeRemainingKg(float v);
void ensureLocalDefaults();

#endif
