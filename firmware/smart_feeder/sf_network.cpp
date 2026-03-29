#include "sf_network.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include "sf_config.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_storage.h"
#include "sf_utils.h"

bool httpGetJson(const String& url, String& outBody) {
  HTTPClient http;
  http.begin(url);
  http.addHeader("Authorization", String("Token ") + AUTH_TOKEN);
  LOG_DEBUG("HTTP GET %s", url.c_str());
  int code = http.GET();
  outBody = http.getString();
  http.end();
  LOG_DEBUG("HTTP GET status=%d bytes=%u", code, (unsigned int)outBody.length());
  return code == HTTP_CODE_OK;
}

bool httpPostJson(const String& url, const String& payload, String& outBody) {
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Token ") + AUTH_TOKEN);
  LOG_DEBUG("HTTP POST %s payloadBytes=%u", url.c_str(), (unsigned int)payload.length());
  int code = http.POST(payload);
  outBody = http.getString();
  http.end();
  LOG_DEBUG("HTTP POST status=%d bytes=%u", code, (unsigned int)outBody.length());
  return code == HTTP_CODE_OK || code == HTTP_CODE_CREATED;
}

void sendLog(const char* type, JsonVariant payload) {
  if (!WiFi.isConnected()) {
    LOG_WARN("Skipping log upload; WiFi disconnected");
    return;
  }
  String url = String(SERVER_BASE) + "/device/" + DEVICE_ID + "/logs/";
  StaticJsonDocument<512> doc;
  doc["log_type"] = type;
  doc["payload"] = payload;
  String body;
  serializeJson(doc, body);
  String resp;
  bool ok = httpPostJson(url, body, resp);
  LOG_INFO("sendLog type=%s ok=%d", type, ok ? 1 : 0);
  if (!ok) LOG_WARN("sendLog response: %s", resp.c_str());
}

void sendAlert(const char* alertType) {
  if (!WiFi.isConnected()) {
    LOG_WARN("Skipping alert upload; WiFi disconnected");
    return;
  }
  String url = String(SERVER_BASE) + "/device/" + DEVICE_ID + "/alerts/";
  StaticJsonDocument<256> doc;
  doc["alert_type"] = alertType;
  String body;
  serializeJson(doc, body);
  String resp;
  bool ok = httpPostJson(url, body, resp);
  LOG_WARN("sendAlert type=%s ok=%d", alertType, ok ? 1 : 0);
  if (!ok) LOG_WARN("sendAlert response: %s", resp.c_str());
}

void syncWithServer() {
  String url = String(SERVER_BASE) + "/device/" + DEVICE_ID + "/config/";
  String body;
  if (!httpGetJson(url, body)) {
    LOG_WARN("Config GET failed");
    return;
  }

  StaticJsonDocument<3072> serverDoc;
  DeserializationError err = deserializeJson(serverDoc, body);
  if (err) {
    LOG_ERROR("Invalid config JSON from server");
    return;
  }

  const char* serverTs = serverDoc["last_updated"] | "";
  if (strlen(serverTs) == 0) {
    serverTs = serverDoc["config"]["last_updated"] | "";
  }

  String localStr = loadLocalConfig();
  StaticJsonDocument<3072> localDoc;
  deserializeJson(localDoc, localStr);
  const char* localTs = localDoc["last_updated"] | "";

  time_t sTs = strlen(serverTs) > 0 ? parseIsoUtc(serverTs) : 0;
  time_t lTs = strlen(localTs) > 0 ? parseIsoUtc(localTs) : 0;

  LOG_INFO("LWW compare server=%ld local=%ld", (long)sTs, (long)lTs);

  if (sTs > lTs) {
    serverDoc["config"]["last_updated"] = serverTs;
    serverDoc["config"]["updated_by"] = serverDoc["updated_by"] | "server";
    String out;
    serializeJson(serverDoc["config"], out);
    saveLocalConfig(out);
    LOG_INFO("Applied server config to local");
  } else if (lTs > sTs) {
    StaticJsonDocument<2048> postDoc;
    postDoc["config"] = localDoc.as<JsonVariant>();
    postDoc["last_updated"] = localDoc["last_updated"] | getUtcIsoNow();
    postDoc["updated_by"] = "esp32";
    String postBody;
    serializeJson(postDoc, postBody);
    String resp;
    bool ok = httpPostJson(url, postBody, resp);
    LOG_INFO("Pushed local config to server ok=%d", ok ? 1 : 0);
    if (!ok) {
      LOG_WARN("Push config response: %s", resp.c_str());
      StaticJsonDocument<4096> conflictDoc;
      DeserializationError cErr = deserializeJson(conflictDoc, resp);
      if (!cErr && conflictDoc.containsKey("server_config")) {
        JsonVariant serverCopy = conflictDoc["server_config"];
        const char* copyTs = serverCopy["last_updated"] | "";
        JsonVariant copyCfg = serverCopy["config"];
        if (!copyCfg.isNull()) {
          copyCfg["last_updated"] = copyTs;
          copyCfg["updated_by"] = serverCopy["updated_by"] | "server";
          String out;
          serializeJson(copyCfg, out);
          saveLocalConfig(out);
          LOG_WARN("Recovered from conflict by applying server_config to local cache");
        }
      }
    }
  } else {
    LOG_DEBUG("Config in sync; no update needed");
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  LOG_INFO("Connecting WiFi SSID=%s", WIFI_SSID);
  int cnt = 0;
  while (WiFi.status() != WL_CONNECTED && cnt < 40) {
    delay(500);
    Serial.print('.');
    cnt++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    LOG_INFO("WiFi connected IP=%s", WiFi.localIP().toString().c_str());
  } else {
    LOG_WARN("WiFi connect timeout; continuing offline");
  }
}
