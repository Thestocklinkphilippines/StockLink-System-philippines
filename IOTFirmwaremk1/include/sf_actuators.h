#ifndef SF_ACTUATORS_H
#define SF_ACTUATORS_H

#include <ArduinoJson.h>

auto isFeedSufficient(float requiredKg, JsonVariant cfg) -> bool;
void dispenseFeed(float amountKg, JsonVariant cfg);
void attemptRefill(JsonVariant cfg);

#endif
