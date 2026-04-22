#include "sf_network.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include "sf_config.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_storage.h"
#include "sf_utils.h"

// Keep large JSON documents out of loopTask stack.
// Config payload can grow with schedules/runtime fields, so use extra headroom.
static StaticJsonDocument<8192> gServerDoc;
static StaticJsonDocument<8192> gLocalDoc;
static StaticJsonDocument<12288> gPostDoc;
static StaticJsonDocument<8192> gConflictDoc;

static bool applyServerConfigEnvelope(JsonVariant envelope, const char* fallbackUpdatedBy) {
  if (envelope.isNull()) return false;

  JsonVariant cfg = envelope["config"];
  if (cfg.isNull()) return false;

  const char* rootTs = envelope["last_updated"] | "";
  if (strlen(rootTs) > 0) {
    cfg["last_updated"] = rootTs;
  }
  cfg["updated_by"] = envelope["updated_by"] | fallbackUpdatedBy;

  String out;
  serializeJson(cfg, out);
  if (out.length() == 0) return false;

  saveLocalConfig(out);
  return true;
}

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

bool httpPostJson(const String& url, const String& payload, String& outBody, int* outStatusCode) {
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Token ") + AUTH_TOKEN);
  LOG_DEBUG("HTTP POST %s payloadBytes=%u", url.c_str(), (unsigned int)payload.length());
  int code = http.POST(payload);
  if (outStatusCode) {
    *outStatusCode = code;
  }
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
  doc["timestamp"] = getUtcIsoNow();  // Add current UTC timestamp
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

bool sendFeedNowAck(uint32_t commandId, const char* status, const char* reason) {
  if (!WiFi.isConnected()) {
    LOG_WARN("Skipping feed_now ack upload; WiFi disconnected");
    return false;
  }

  String url = String(SERVER_BASE) + "/device/" + DEVICE_ID + "/feed-now/" + String(commandId) + "/ack/";
  StaticJsonDocument<256> doc;
  doc["status"] = status;
  if (reason != nullptr && strlen(reason) > 0) {
    doc["reason"] = reason;
  }

  String body;
  serializeJson(doc, body);
  String resp;
  bool ok = httpPostJson(url, body, resp);
  LOG_INFO("feed_now ack id=%lu status=%s ok=%d", (unsigned long)commandId, status, ok ? 1 : 0);
  if (!ok) LOG_WARN("feed_now ack response: %s", resp.c_str());
  return ok;
}

void syncWithServer() {
  String url = String(SERVER_BASE) + "/device/" + DEVICE_ID + "/config/";
  String body;
  if (!httpGetJson(url, body)) {
    LOG_WARN("Config GET failed");
    return;
  }

  gServerDoc.clear();
  DeserializationError err = deserializeJson(gServerDoc, body);
  if (err) {
    LOG_ERROR("Invalid config JSON from server: %s (bytes=%u)", err.c_str(), (unsigned int)body.length());
    return;
  }

  const char* serverTs = gServerDoc["last_updated"] | "";
  if (strlen(serverTs) == 0) {
    serverTs = gServerDoc["config"]["last_updated"] | "";
  }

  String localStr = loadLocalConfig();
  gLocalDoc.clear();
  DeserializationError localErr = deserializeJson(gLocalDoc, localStr);
  if (localErr) {
    LOG_ERROR("Local config parse failed before sync POST: %s (bytes=%u)",
              localErr.c_str(),
              (unsigned int)localStr.length());
    return;
  }
  const char* localTs = gLocalDoc["last_updated"] | "";

  time_t sTs = strlen(serverTs) > 0 ? parseIsoUtc(serverTs) : 0;
  time_t lTs = strlen(localTs) > 0 ? parseIsoUtc(localTs) : 0;

  LOG_INFO("LWW compare server=%ld local=%ld", (long)sTs, (long)lTs);

  if (sTs > lTs) {
    if (applyServerConfigEnvelope(gServerDoc.as<JsonVariant>(), "server")) {
      LOG_INFO("Applied server config to local");
    } else {
      LOG_WARN("Server config envelope missing fields; skipped local apply");
    }
  } else if (lTs > sTs) {
    gPostDoc.clear();
    gPostDoc["config"] = gLocalDoc.as<JsonVariant>();
    gPostDoc["last_updated"] = gLocalDoc["last_updated"] | getUtcIsoNow();
    gPostDoc["updated_by"] = "esp32";
    if (gPostDoc.overflowed()) {
      LOG_ERROR("Config POST envelope overflowed; cfgBytes=%u", (unsigned int)localStr.length());
      return;
    }

    String postBody;
    size_t postBytes = serializeJson(gPostDoc, postBody);
    LOG_INFO("Config POST envelope bytes=%u localTs=%s", (unsigned int)postBytes, (const char*)(gPostDoc["last_updated"] | ""));
    if (postBytes == 0 || postBody.length() == 0) {
      LOG_ERROR("Config POST envelope serialization failed");
      return;
    }

    String resp;
    int postCode = 0;
    bool ok = httpPostJson(url, postBody, resp, &postCode);
    LOG_INFO("Pushed local config to server ok=%d status=%d", ok ? 1 : 0, postCode);
    if (ok) {
      gConflictDoc.clear();
      DeserializationError okErr = deserializeJson(gConflictDoc, resp);
      if (!okErr) {
        if (applyServerConfigEnvelope(gConflictDoc.as<JsonVariant>(), "server")) {
          LOG_INFO("Applied server canonical config from POST response");
        } else {
          LOG_DEBUG("POST success response has no config envelope; keeping local copy");
        }
      } else if (resp.length() > 0) {
        LOG_WARN("POST success response parse failed: %s", okErr.c_str());
      }
    }

    if (!ok) {
      LOG_WARN("Push config response: %s", resp.c_str());
      gConflictDoc.clear();
      DeserializationError cErr = deserializeJson(gConflictDoc, resp);
      if (postCode == HTTP_CODE_CONFLICT && !cErr && gConflictDoc.containsKey("server_config")) {
        JsonVariant serverCopy = gConflictDoc["server_config"];
        if (applyServerConfigEnvelope(serverCopy, "server")) {
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
