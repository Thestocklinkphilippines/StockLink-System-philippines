#include "sf_network.h"

#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "sf_config.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_storage.h"
#include "sf_utils.h"

#undef LOG_DEBUG
#undef LOG_INFO
#undef LOG_WARN
#undef LOG_ERROR
#define LOG_DEBUG(fmt, ...) do { SFMultiConsole.networkPrintf("[DBG ] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_INFO(fmt, ...)  do { SFMultiConsole.networkPrintf("[INFO] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_WARN(fmt, ...)  do { SFMultiConsole.networkPrintf("[WARN] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_ERROR(fmt, ...) do { SFMultiConsole.networkPrintf("[ERR ] " fmt "\n", ##__VA_ARGS__); } while (0)

// Keep large JSON documents out of loopTask stack.
// Config payload can grow with schedules/runtime fields, so use extra headroom.
static StaticJsonDocument<8192> gServerDoc;
static StaticJsonDocument<8192> gLocalDoc;
static StaticJsonDocument<12288> gPostDoc;
static StaticJsonDocument<8192> gConflictDoc;
static bool gOtaStarted = false;
static String gOtaHostname;

static unsigned long gServerReachabilityRetryAfterMs = 0;
static const unsigned long kServerReachabilityTimeoutMs = 1500UL;
static const unsigned long kServerReachabilityCooldownMs = 30000UL;

static bool canProbeServerNow(unsigned long nowMs) {
  if (gServerReachabilityRetryAfterMs == 0) return true;
  return (long)(nowMs - gServerReachabilityRetryAfterMs) >= 0;
}

static void markServerUnreachable(unsigned long nowMs) {
  gServerReachabilityRetryAfterMs = nowMs + kServerReachabilityCooldownMs;
}

static const unsigned int kOutboxMaxEvents = 20U;

static bool isQueuedLogImportant(const char* type) {
  if (type == nullptr) return false;
  return strcmp(type, "feeding") == 0 || strcmp(type, "watering") == 0 || strcmp(type, "power") == 0 ||
         strcmp(type, "feed_now") == 0 || strcmp(type, "low_feed") == 0 || strcmp(type, "low_water") == 0 ||
         strcmp(type, "shutdown") == 0;
}

static bool isSystemConsoleCriticalType(const char* type) {
  if (type == nullptr) return false;
  return strcmp(type, "feeding") == 0 || strcmp(type, "watering") == 0 || strcmp(type, "power") == 0 ||
         strcmp(type, "feed_now") == 0 || strcmp(type, "low_feed") == 0 || strcmp(type, "low_water") == 0 ||
         strcmp(type, "shutdown") == 0;
}

static void emitSystemEventForLog(const char* type, JsonVariant payload) {
  if (!isSystemConsoleCriticalType(type)) return;

  if (strcmp(type, "power") == 0) {
    const char* event = payload["event"] | "update";
    LOG_SYSTEM_EVENT("power_%s", event);
    return;
  }

  if (strcmp(type, "feeding") == 0) {
    float amountKg = payload["amount_kg"] | -1.0f;
    float remainingKg = payload["remaining_kg"] | -1.0f;
    if (amountKg >= 0.0f && remainingKg >= 0.0f) {
      LOG_SYSTEM_EVENT("feeding amount=%.3fkg remaining=%.3fkg", amountKg, remainingKg);
    } else {
      LOG_SYSTEM_EVENT("feeding");
    }
    return;
  }

  if (strcmp(type, "watering") == 0) {
    const char* event = payload["event"] | "update";
    float waterLevelPct = payload["water_level_pct"] | -1.0f;
    if (waterLevelPct >= 0.0f) {
      LOG_SYSTEM_EVENT("watering_%s level=%.1f%%", event, waterLevelPct);
    } else {
      LOG_SYSTEM_EVENT("watering_%s", event);
    }
    return;
  }

  LOG_SYSTEM_EVENT("%s", type);
}

static bool loadOutboxDoc(DynamicJsonDocument& outboxDoc) {
  String raw = loadEventOutbox();
  outboxDoc.clear();
  DeserializationError err = deserializeJson(outboxDoc, raw);
  if (err) {
    outboxDoc.clear();
    outboxDoc["next_seq"] = readEventSequence();
    outboxDoc.createNestedArray("events");
    return false;
  }

  if (!outboxDoc.containsKey("next_seq")) {
    outboxDoc["next_seq"] = readEventSequence();
  }
  if (!outboxDoc.containsKey("events") || !outboxDoc["events"].is<JsonArray>()) {
    outboxDoc.createNestedArray("events");
  }
  return true;
}

static void updateOutboxState(DynamicJsonDocument& outboxDoc) {
  JsonArray events = outboxDoc["events"].as<JsonArray>();
  state.bufferedEventCount = events.isNull() ? 0U : (unsigned int)events.size();
}

static void saveOutboxDoc(DynamicJsonDocument& outboxDoc) {
  String out;
  serializeJson(outboxDoc, out);
  saveEventOutbox(out);
  uint32_t nextSeq = outboxDoc["next_seq"] | readEventSequence();
  writeEventSequence(nextSeq);
  updateOutboxState(outboxDoc);
}

bool queueBufferedRequest(const String& endpoint, const String& body, const char* kind, bool critical) {
  if (endpoint.length() == 0 || body.length() == 0) return false;

  DynamicJsonDocument outboxDoc(8192);
  loadOutboxDoc(outboxDoc);
  JsonArray events = outboxDoc["events"].as<JsonArray>();

  if (strcmp(kind, "sensor_state") == 0) {
    for (JsonVariant ev : events) {
      if (strcmp((const char*)(ev["kind"] | ""), "sensor_state") == 0) {
        ev["endpoint"] = endpoint;
        ev["body"] = body;
        ev["ts"] = getUtcIsoNow();
        saveOutboxDoc(outboxDoc);
        LOG_WARN("Buffered sensor state replaced existing queued sample");
        return true;
      }
    }
  }

  if (events.size() >= kOutboxMaxEvents) {
    int dropIndex = -1;
    for (int i = 0; i < (int)events.size(); i++) {
      const char* evKind = events[i]["kind"] | "";
      bool evCritical = strcmp(evKind, "sensor_state") != 0 && strcmp(evKind, "heartbeat") != 0;
      if (!evCritical) {
        dropIndex = i;
        break;
      }
    }
    if (dropIndex >= 0) {
      events.remove(dropIndex);
    } else if (!critical) {
      LOG_WARN("Outbox full; dropping non-critical %s event", kind);
      return false;
    } else {
      events.remove(0);
      LOG_WARN("Outbox full; dropped oldest event to preserve critical %s event", kind);
    }
  }

  JsonObject ev = events.createNestedObject();
  uint32_t seq = outboxDoc["next_seq"] | readEventSequence();
  if (seq == 0UL) seq = 1UL;
  ev["seq"] = seq;
  ev["event_id"] = String(DEVICE_ID) + "-" + String(seq);
  ev["kind"] = kind;
  ev["endpoint"] = endpoint;
  ev["body"] = body;
  ev["ts"] = getUtcIsoNow();
  ev["critical"] = critical;
  outboxDoc["next_seq"] = seq + 1UL;

  saveOutboxDoc(outboxDoc);
  LOG_INFO("Buffered outbound %s event seq=%lu queued=%u", kind, (unsigned long)seq, state.bufferedEventCount);
  return true;
}

static bool sendQueuedEvent(JsonVariantConst ev) {
  const char* endpoint = ev["endpoint"] | "";
  const char* body = ev["body"] | "";
  if (strlen(endpoint) == 0 || strlen(body) == 0) return false;

  String url = String(SERVER_BASE) + endpoint;
  String resp;
  int status = 0;
  bool ok = httpPostJson(url, String(body), resp, &status);
  if (!ok) {
    LOG_WARN("Queued event send failed kind=%s status=%d", (const char*)(ev["kind"] | ""), status);
  }
  return ok;
}

static bool flushBufferedOutboxOnce() {
  if (!WiFi.isConnected()) return false;

  DynamicJsonDocument outboxDoc(8192);
  loadOutboxDoc(outboxDoc);
  JsonArray events = outboxDoc["events"].as<JsonArray>();
  if (events.isNull() || events.size() == 0) {
    state.bufferedEventCount = 0;
    return false;
  }

  if (!sendQueuedEvent(events[0].as<JsonVariantConst>())) {
    return false;
  }

  events.remove(0);
  saveOutboxDoc(outboxDoc);
  LOG_INFO("Flushed queued event; remaining=%u", state.bufferedEventCount);
  return true;
}
static void preserveBatteryConfigFields(JsonVariant envelope, JsonVariant localCfg) {
  if (envelope.isNull() || localCfg.isNull()) return;

  JsonVariant cfg = envelope["config"];
  if (cfg.isNull()) return;

  const char* batteryKeys[] = {
      "battery_sense_enabled",
      "battery_adc_pin",
      "battery_divider_top_ohms",
      "battery_divider_bottom_ohms",
      "battery_adc_reference_v",
      "battery_adc_gain_correction",
      "low_battery_shutdown_v",
  };
  for (const char* key : batteryKeys) {
    if (!cfg.containsKey(key) && localCfg.containsKey(key)) {
      cfg[key] = localCfg[key];
    }
  }
}

static void preserveKeypadConfigFields(JsonVariant envelope, JsonVariant localCfg) {
  if (envelope.isNull() || localCfg.isNull()) return;

  JsonVariant cfg = envelope["config"];
  if (cfg.isNull()) return;

  if (!cfg.containsKey("keypad_input_enabled") && localCfg.containsKey("keypad_input_enabled")) {
    cfg["keypad_input_enabled"] = localCfg["keypad_input_enabled"];
  }

  JsonVariant localKeypad = localCfg["keypad_calibration"];
  if (!cfg.containsKey("keypad_calibration") && !localKeypad.isNull()) {
    cfg["keypad_calibration"] = localKeypad;
  }
}

static void preserveDerivedFeedLimitFields(JsonVariant envelope, JsonVariant localCfg) {
  if (envelope.isNull() || localCfg.isNull()) return;

  JsonVariant cfg = envelope["config"];
  if (cfg.isNull()) return;

  if (!cfg.containsKey("max_single_feed_kg") && localCfg.containsKey("max_single_feed_kg")) {
    cfg["max_single_feed_kg"] = localCfg["max_single_feed_kg"];
  }
}

static void preserveServerOwnedGrainFields(JsonVariant targetCfg, JsonVariant serverEnvelope) {
  if (targetCfg.isNull() || serverEnvelope.isNull()) return;

  JsonVariant serverCfg = serverEnvelope["config"];
  if (serverCfg.isNull()) return;

  if (serverCfg.containsKey("grain_types")) {
    targetCfg["grain_types"] = serverCfg["grain_types"];
  }
}

static void preserveLocalGrainSelectionFields(JsonVariant envelope, JsonVariant localCfg) {
  if (envelope.isNull() || localCfg.isNull()) return;

  JsonVariant cfg = envelope["config"];
  if (cfg.isNull()) return;

  if (!cfg.containsKey("grain_type_index") && !cfg.containsKey("grain_type") && localCfg.containsKey("grain_type_index")) {
    cfg["grain_type_index"] = localCfg["grain_type_index"];
  }
  if (!cfg.containsKey("grain_type") && !cfg.containsKey("grain_type_index") && localCfg.containsKey("grain_type")) {
    cfg["grain_type"] = localCfg["grain_type"];
  }
  if (!cfg.containsKey("feed_ms_per_kg") && !cfg.containsKey("grain_type_index") && localCfg.containsKey("feed_ms_per_kg")) {
    cfg["feed_ms_per_kg"] = localCfg["feed_ms_per_kg"];
  }
  if (!cfg.containsKey("grain_types") && localCfg.containsKey("grain_types")) {
    cfg["grain_types"] = localCfg["grain_types"];
  }
}

static void normalizeGrainSelectionFields(JsonVariant cfg) {
  if (cfg.isNull()) return;

  int selectedIndex = -1;
  if (cfg.containsKey("grain_type_index")) {
    selectedIndex = getSelectedGrainTypeIndex(cfg);
  } else if (cfg.containsKey("grain_type")) {
    selectedIndex = findGrainTypeIndex(cfg, cfg["grain_type"] | DEFAULT_GRAIN_TYPE);
  }

  if (selectedIndex < 0) selectedIndex = 0;
  cfg["grain_type_index"] = selectedIndex;
  cfg["grain_type"] = getGrainTypeNameByIndex(cfg, selectedIndex);
  cfg["feed_ms_per_kg"] = getGrainTypeMsPerKgByIndex(cfg, selectedIndex);
}

static bool applyServerConfigEnvelope(JsonVariant envelope, const char* fallbackUpdatedBy) {
  if (envelope.isNull()) return false;

  JsonVariant cfg = envelope["config"];
  if (cfg.isNull()) return false;

  String localStr = loadLocalConfig();
  DynamicJsonDocument localDoc(8192);
  DeserializationError localErr = deserializeJson(localDoc, localStr);
  if (!localErr) {
    preserveBatteryConfigFields(envelope, localDoc.as<JsonVariant>());
    preserveKeypadConfigFields(envelope, localDoc.as<JsonVariant>());
    preserveDerivedFeedLimitFields(envelope, localDoc.as<JsonVariant>());
    preserveLocalGrainSelectionFields(envelope, localDoc.as<JsonVariant>());
  }

  normalizeGrainSelectionFields(cfg);


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
  // Quick pre-check to avoid long blocking when server unreachable
  if (!WiFi.isConnected()) return false;
  unsigned long nowMs = millis();
  if (!canProbeServerNow(nowMs)) return false;

  // If server host isn't reachable within the timeout, skip HTTPClient call to keep loop responsive.
  auto isServerReachable = [](unsigned long timeoutMs) -> bool {
    String base = String(SERVER_BASE);
    int start = 0;
    int defaultPort = 80;
    if (base.startsWith("http://")) start = 7;
    else if (base.startsWith("https://")) { start = 8; defaultPort = 443; }
    int slash = base.indexOf('/', start);
    String hostPort = (slash == -1) ? base.substring(start) : base.substring(start, slash);
    int colon = hostPort.indexOf(':');
    String host;
    int port = defaultPort;
    if (colon != -1) {
      host = hostPort.substring(0, colon);
      port = hostPort.substring(colon + 1).toInt();
    } else {
      host = hostPort;
    }
    if (host.length() == 0) return false;
    WiFiClient client;
    bool ok = client.connect(host.c_str(), (uint16_t)port, timeoutMs);
    if (ok) client.stop();
    return ok;
  };
  if (!isServerReachable(kServerReachabilityTimeoutMs)) {
    LOG_WARN("Server unreachable; HTTP GET skipped");
    markServerUnreachable(nowMs);
    return false;
  }
  gServerReachabilityRetryAfterMs = 0;
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
  // Quick pre-check to avoid long blocking when server unreachable
  if (!WiFi.isConnected()) return false;
  unsigned long nowMs = millis();
  if (!canProbeServerNow(nowMs)) return false;

  auto isServerReachable = [](unsigned long timeoutMs) -> bool {
    String base = String(SERVER_BASE);
    int start = 0;
    int defaultPort = 80;
    if (base.startsWith("http://")) start = 7;
    else if (base.startsWith("https://")) { start = 8; defaultPort = 443; }
    int slash = base.indexOf('/', start);
    String hostPort = (slash == -1) ? base.substring(start) : base.substring(start, slash);
    int colon = hostPort.indexOf(':');
    String host;
    int port = defaultPort;
    if (colon != -1) {
      host = hostPort.substring(0, colon);
      port = hostPort.substring(colon + 1).toInt();
    } else {
      host = hostPort;
    }
    if (host.length() == 0) return false;
    WiFiClient client;
    bool ok = client.connect(host.c_str(), (uint16_t)port, timeoutMs);
    if (ok) client.stop();
    return ok;
  };
  if (!isServerReachable(kServerReachabilityTimeoutMs)) {
    LOG_WARN("Server unreachable; HTTP POST skipped");
    markServerUnreachable(nowMs);
    return false;
  }
  gServerReachabilityRetryAfterMs = 0;
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
  String url = String(SERVER_BASE) + "/device/" + DEVICE_ID + "/logs/";
  StaticJsonDocument<512> doc;
  doc["log_type"] = type;
  doc["payload"] = payload;
  doc["timestamp"] = getUtcIsoNow();  // Add current UTC timestamp
  emitSystemEventForLog(type, payload);
  String body;
  serializeJson(doc, body);
  if (type != nullptr && (strcmp(type, "feeding") == 0 || strcmp(type, "feed_now") == 0)) {
    LOG_DEBUG("sendLog payload type=%s body=%s", type, body.c_str());
  }
  String resp;
  bool ok = httpPostJson(url, body, resp);
  LOG_INFO("sendLog type=%s ok=%d", type, ok ? 1 : 0);
  if (!ok) {
    if (!WiFi.isConnected()) {
      LOG_WARN("Skipping log upload; WiFi disconnected");
    } else {
      LOG_WARN("sendLog response: %s", resp.c_str());
    }
    if (strcmp(type, "heartbeat") != 0) {
      queueBufferedRequest(String("/device/") + DEVICE_ID + "/logs/", body, type, isQueuedLogImportant(type));
    }
  }
}

void sendAlert(const char* alertType) {
  String url = String(SERVER_BASE) + "/device/" + DEVICE_ID + "/alerts/";
  StaticJsonDocument<256> doc;
  doc["alert_type"] = alertType;
  LOG_SYSTEM_ALERT("%s", alertType);
  String body;
  serializeJson(doc, body);
  String resp;
  bool ok = httpPostJson(url, body, resp);
  LOG_WARN("sendAlert type=%s ok=%d", alertType, ok ? 1 : 0);
  if (!ok) {
    if (!WiFi.isConnected()) {
      LOG_WARN("Skipping alert upload; WiFi disconnected");
    } else {
      LOG_WARN("sendAlert response: %s", resp.c_str());
    }
    queueBufferedRequest(String("/device/") + DEVICE_ID + "/alerts/", body, "alert", true);
  }
}

bool sendFeedNowAck(uint32_t commandId, const char* status, const char* reason) {
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
  if (!ok) {
    if (!WiFi.isConnected()) {
      LOG_WARN("Skipping feed_now ack upload; WiFi disconnected");
    } else {
      LOG_WARN("feed_now ack response: %s", resp.c_str());
    }
    queueBufferedRequest(String("/device/") + DEVICE_ID + "/feed-now/" + String(commandId) + "/ack/", body, "feed_now_ack", true);
  }
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
    LOG_ERROR("Local config parse failed before sync compare: %s (bytes=%u)",
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
    preserveServerOwnedGrainFields(gPostDoc["config"].as<JsonVariant>(), gServerDoc.as<JsonVariant>());
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
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  if (gOtaHostname.length() == 0) {
    gOtaHostname = String("smart-feeder-") + DEVICE_ID;
  }
  WiFi.setHostname(gOtaHostname.c_str());
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  LOG_INFO("Connecting WiFi SSID=%s", WIFI_SSID);
  if (WiFi.status() == WL_CONNECTED) {
    LOG_INFO("WiFi connected IP=%s", WiFi.localIP().toString().c_str());
  } else {
    LOG_WARN("WiFi connect initiated; continuing without blocking");
  }
}

void serviceBufferedOutbox() {
  if (!WiFi.isConnected()) return;
  for (int i = 0; i < 2; i++) {
    if (!flushBufferedOutboxOnce()) break;
  }
}

void setupOTA() {
  if (gOtaStarted) return;
  if (WiFi.status() != WL_CONNECTED) {
    LOG_WARN("OTA setup skipped; WiFi not connected yet");
    return;
  }

  if (gOtaHostname.length() == 0) {
    gOtaHostname = String("smart-feeder-") + DEVICE_ID;
  }

  ArduinoOTA.setHostname(gOtaHostname.c_str());
  ArduinoOTA.onStart([]() {
    LOG_INFO("OTA start");
  });
  ArduinoOTA.onEnd([]() {
    LOG_INFO("OTA end");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (total > 0U) {
      unsigned int pct = (progress * 100U) / total;
      LOG_INFO("OTA progress %u%%", pct);
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    LOG_ERROR("OTA error=%u", (unsigned int)error);
  });

  ArduinoOTA.begin();
  gOtaStarted = true;
  MDNS.begin(gOtaHostname.c_str());
  LOG_INFO("OTA ready hostname=%s ip=%s", gOtaHostname.c_str(), WiFi.localIP().toString().c_str());
}

void serviceOTA() {
  if (!gOtaStarted) {
    if (WiFi.status() == WL_CONNECTED) {
      setupOTA();
    } else {
      return;
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }
}
