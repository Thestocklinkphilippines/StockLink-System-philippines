#ifndef SF_NETWORK_H
#define SF_NETWORK_H

#include <Arduino.h>
#include <ArduinoJson.h>

auto httpGetJson(const String& url, String& outBody) -> bool;
auto httpPostJson(const String& url, const String& payload, String& outBody, int* outStatusCode = nullptr) -> bool;
void sendLog(const char* type, JsonVariant payload);
void sendAlert(const char* alertType);
auto sendFeedNowAck(uint32_t commandId, const char* status, const char* reason) -> bool;
bool queueBufferedRequest(const String& endpoint, const String& body, const char* kind, bool critical);
void syncWithServer();
void connectWiFi();
void setupOTA();
void serviceOTA();
void serviceBufferedOutbox();

#endif
