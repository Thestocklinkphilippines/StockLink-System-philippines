#include <Arduino.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

#include "sf_actuators.h"
#include "sf_config.h"
#include "sf_control_panel.h"
#include "sf_debug.h"
#include "sf_globals.h"
#include "sf_network.h"
#include "sf_pins.h"
#include "sf_scheduler.h"
#include "sf_sensors.h"
#include "sf_serial.h"
#include "sf_simulation.h"
#include "sf_storage.h"
#include "sf_utils.h"

static LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLS, LCD_ROWS);

void setupPins() {
  pinMode(PIN_FEEDER_TRIG, OUTPUT);
  pinMode(PIN_FEEDER_ECHO, INPUT);
  pinMode(PIN_WATER_TRIG, OUTPUT);
  pinMode(PIN_WATER_ECHO, INPUT);

  pinMode(PIN_FEED_MOTOR, OUTPUT);
  pinMode(PIN_WATER_SOLENOID, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_MAINS_SENSE_ADC, INPUT);

  setWaterSolenoidEnabled(false);
  digitalWrite(PIN_BUZZER, LOW);
  noTone(PIN_BUZZER);
  setFeedMotorEnabled(false);
  LOG_INFO("Feed motor polarity activeHigh=%d", FEED_MOTOR_ACTIVE_HIGH ? 1 : 0);
  LOG_INFO("Water solenoid polarity activeHigh=%d", WATER_SOLENOID_ACTIVE_HIGH ? 1 : 0);

  analogReadResolution(12);
  LOG_INFO("Pins initialized");
}

void setupTime() {
  configTime(DEVICE_TZ_OFFSET_SECONDS, 0, "pool.ntp.org", "time.nist.gov");
  LOG_INFO("Waiting for NTP sync...");
  time_t now = time(nullptr);
  int wait = 0;
  while (now < 100000 && wait < 15) {
    delay(1000);
    now = time(nullptr);
    wait++;
    LOG_DEBUG("NTP wait %d current=%ld", wait, (long)now);
  }
  LOG_INFO("Time ready epoch=%ld", (long)now);
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  LOG_INFO("Smart feeder booting");
  LOG_INFO("Build mode simulation=%d verbose=%d", SF_SIMULATION_MODE ? 1 : 0, SF_VERBOSE_SERIAL ? 1 : 0);
  LOG_INFO("Sim toggles feederSensor=%d waterSensor=%d feedMotor=%d refill=%d mains=%d",
           SF_SIMULATE_FEEDER_LEVEL_SENSOR,
           SF_SIMULATE_WATER_LEVEL_SENSOR,
           SF_SIMULATE_FEED_MOTOR,
           SF_SIMULATE_WATER_REFILL,
           SF_SIMULATE_MAINS_INPUT);

  setupPins();
  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
  LOG_INFO("I2C initialized SDA=%d SCL=%d", PIN_LCD_SDA, PIN_LCD_SCL);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Feeder MK1");
  lcd.setCursor(0, 1);
  lcd.print("LCD test: ONLINE");
  lcd.setCursor(0, 2);
  lcd.print("SDA=21 SCL=22");
  lcd.setCursor(0, 3);
  lcd.print("Addr 0x27 20x4");
  LOG_INFO("LCD initialized addr=0x%02X cols=%d rows=%d", LCD_I2C_ADDRESS, LCD_COLS, LCD_ROWS);
  initControlPanel(&lcd);

  connectWiFi();
  setupTime();
  ensureLocalDefaults();
  reloadKeypadCalibration();

  state.lastSyncMs = millis();
  state.lastScheduleCheckMs = millis();
  state.lastWaterCheckMs = millis();
  state.lastSensorReportMs = millis();
  state.lastWiFiReconnectAttemptMs = millis();
  state.lastMainsCheckMs = millis();
  state.lastKeypadPollMs = millis();
  state.lastConfigRefreshMs = 0;
  state.lastHeartbeatLogMs = millis();
  cachedCfgStr = loadLocalConfig();
  state.mainsPowerPresent = SF_ENABLE_MAINS_MONITOR ? readMainsPowerPresent() : true;

  LOG_INFO("Setup complete mainsPresent=%d", state.mainsPowerPresent ? 1 : 0);
  printSerialHelp();
}

void loop() {
  unsigned long nowMs = millis();

  // Safety guard: keep motor output in OFF state unless a dispense routine is actively running.
  setFeedMotorEnabled(false);

  handleSerialCommands();

  if (gSerialConsoleExclusive) {
    delay(20);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (nowMs - state.lastWiFiReconnectAttemptMs >= WIFI_RECONNECT_INTERVAL_MS) {
      state.lastWiFiReconnectAttemptMs = nowMs;
      LOG_WARN("WiFi disconnected; retrying connection");
      connectWiFi();
    }
  }

  if (SF_ENABLE_MAINS_MONITOR && nowMs - state.lastMainsCheckMs >= MAINS_CHECK_INTERVAL_MS) {
    state.lastMainsCheckMs = nowMs;
    handlePowerFailMonitoring();
  }

  if (isKeypadInputEnabled() && nowMs - state.lastKeypadPollMs >= KEYPAD_POLL_INTERVAL_MS) {
    state.lastKeypadPollMs = nowMs;
    pollKeypad();
  }

  if (nowMs - state.lastSyncMs >= SYNC_INTERVAL_MS) {
    state.lastSyncMs = nowMs;
    if (WiFi.status() == WL_CONNECTED) {
      syncWithServer();
    }
  }

  if (nowMs - state.lastConfigRefreshMs >= LOCAL_CONFIG_REFRESH_INTERVAL_MS || cachedCfgStr.length() == 0) {
    state.lastConfigRefreshMs = nowMs;
    cachedCfgStr = loadLocalConfig();
  }

  bool doScheduleCheck = (nowMs - state.lastScheduleCheckMs >= SCHEDULE_CHECK_INTERVAL_MS);
  if (doScheduleCheck) {
    // Ensure device-owned daily total fields stay current by date rollover.
    ensureDailyFeedTotalForToday();
  }

  String cfgStr = cachedCfgStr;
  static DynamicJsonDocument cfgDoc(8192);
  cfgDoc.clear();
  DeserializationError cfgErr = deserializeJson(cfgDoc, cfgStr);
  if (cfgErr) {
    LOG_ERROR("Config JSON parse failed: %s (bytes=%u); using empty defaults",
              cfgErr.c_str(),
              (unsigned int)cfgStr.length());
    cfgDoc.clear();
  }
  JsonVariant cfg = cfgDoc.as<JsonVariant>();
  JsonArray schedules = cfg["schedules"].as<JsonArray>();

  updateControlPanel(cfg, schedules);

  processFeedNowCommand(cfg);

  if (doScheduleCheck) {
    state.lastScheduleCheckMs = nowMs;
    checkLowFeedPrediction(schedules, cfg);
    checkSchedulesAndExecute(schedules, cfg);
  }

  if (nowMs - state.lastSensorReportMs >= SENSOR_REPORT_INTERVAL_MS) {
    state.lastSensorReportMs = nowMs;
    reportSensorLevels(cfg);
  }

  if (SF_SEND_HEARTBEAT_LOGS && nowMs - state.lastHeartbeatLogMs >= HEARTBEAT_LOG_INTERVAL_MS) {
    state.lastHeartbeatLogMs = nowMs;
    StaticJsonDocument<192> hb;
    hb["event"] = "alive";
    hb["uptime_ms"] = nowMs;
    hb["simulated"] = SF_SIMULATION_MODE ? true : false;
    hb["sim_feeder_sensor"] = SF_SIMULATE_FEEDER_LEVEL_SENSOR ? true : false;
    hb["sim_water_sensor"] = SF_SIMULATE_WATER_LEVEL_SENSOR ? true : false;
    hb["sim_feed_motor"] = SF_SIMULATE_FEED_MOTOR ? true : false;
    hb["wifi_connected"] = WiFi.status() == WL_CONNECTED;
    sendLog("heartbeat", hb.as<JsonVariant>());
  }

  if (nowMs - state.lastWaterCheckMs >= WATER_CHECK_INTERVAL_MS) {
    state.lastWaterCheckMs = nowMs;
    float waterLow = getConfigOrDefault(cfg, "water_low_threshold_pct", DEFAULT_WATER_LOW_THRESHOLD_PCT);
    float waterHigh = getConfigOrDefault(cfg, "water_high_threshold_pct", DEFAULT_WATER_HIGH_THRESHOLD_PCT);
    float waterPct = getWaterLevelPct(cfg);

    LOG_INFO("Water hysteresis check pct=%.1f low=%.1f high=%.1f isRefilling=%d",
             waterPct, waterLow, waterHigh, state.isRefilling ? 1 : 0);

    if (!state.isRefilling && waterPct <= waterLow) {
      state.isRefilling = true;
      LOG_WARN("Water low threshold crossed; start refill");
      attemptRefill(cfg);
    } else if (state.isRefilling && waterPct >= waterHigh) {
      state.isRefilling = false;
      LOG_INFO("Water high threshold reached; stop refill state");
      StaticJsonDocument<128> p;
      p["event"] = "refill_complete";
      p["water_level_pct"] = waterPct;
      sendLog("watering", p.as<JsonVariant>());
    }
  }

  serviceLevelErrorBuzzer(cfg);

  delay(MAIN_LOOP_DELAY_MS);
}