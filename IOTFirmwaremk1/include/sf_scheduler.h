#ifndef SF_SCHEDULER_H
#define SF_SCHEDULER_H

#include <ArduinoJson.h>

void checkLowFeedPrediction(JsonArray schedules, JsonVariant cfg);
void checkSchedulesAndExecute(JsonArray schedules, JsonVariant cfg);

#endif
