#ifndef SF_SENSORS_H
#define SF_SENSORS_H

#include <Arduino.h>
#include <ArduinoJson.h>

auto getConfigOrDefault(JsonVariant cfg, const char* key, float fallback) -> float;
auto measureDistanceCm(int trigPin, int echoPin) -> float;
auto distanceToLevelPct(float distanceCm, float tankDepthCm) -> float;
auto getFeederLevelPct(JsonVariant cfg) -> float;
auto getWaterLevelPct(JsonVariant cfg) -> float;
auto decodeKeypadAnalog(int adc) -> char;
bool isKeypadInputEnabled();
void setKeypadInputEnabled(bool enabled);
void reloadKeypadCalibration();
void pollKeypad();
auto consumeKeypadKeyEvent() -> char;
auto readMainsPowerPresent() -> bool;
void handlePowerFailMonitoring();
void reportSensorLevels(JsonVariant cfg);

#endif
