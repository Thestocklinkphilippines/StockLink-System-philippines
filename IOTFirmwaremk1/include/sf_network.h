#ifndef SF_NETWORK_H
#define SF_NETWORK_H

#include <Arduino.h>
#include <ArduinoJson.h>

auto httpGetJson(const String& url, String& outBody) -> bool;
auto httpPostJson(const String& url, const String& payload, String& outBody) -> bool;
void sendLog(const char* type, JsonVariant payload);
void sendAlert(const char* alertType);
void syncWithServer();
void connectWiFi();

#endif
