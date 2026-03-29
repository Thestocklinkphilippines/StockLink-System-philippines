#ifndef SF_UTILS_H
#define SF_UTILS_H

#include <Arduino.h>
#include <time.h>

auto clampf(float v, float minV, float maxV) -> float;
auto getUtcIsoNow() -> String;
auto parseIsoUtc(const char* iso) -> time_t;

#endif
