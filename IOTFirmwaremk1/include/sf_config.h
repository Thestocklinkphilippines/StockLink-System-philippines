#ifndef SF_CONFIG_H
#define SF_CONFIG_H

// =========================
// Build and runtime toggles
// =========================
#define SF_SIMULATION_MODE 1
#define SF_VERBOSE_SERIAL 1
#define SF_SEND_KEYPAD_LOGS 0
#define SF_SEND_HEARTBEAT_LOGS 1
#define SF_ENABLE_KEYPAD_INPUT 0
#define SF_ENABLE_MAINS_MONITOR 1

// =========================
// Network and API settings
// =========================
static const char* WIFI_SSID = "Joecille_CES_2.4G";
static const char* WIFI_PASS = "Icwifi1235xyz";
static const char* SERVER_BASE = "http://192.168.254.146:8000/api";
static const char* DEVICE_ID = "001";
static const char* AUTH_TOKEN = "devtoken";

// Local timezone offset from UTC in seconds (e.g. UTC+8 = 28800).
static const long DEVICE_TZ_OFFSET_SECONDS = 8L * 3600L;

// =========================
// Preferences storage keys
// =========================
static const char* PREF_NAMESPACE = "feeder";
static const char* PREF_CONFIG_KEY = "cfg";
static const char* PREF_REMAINING_KG = "remaining";
static const char* PREF_LAST_FEED_CMD_ID = "feed_cmd_id";

// =========================
// Timing
// =========================
static const unsigned long SYNC_INTERVAL_MS = 5000UL;
static const unsigned long SCHEDULE_CHECK_INTERVAL_MS = 60000UL;
static const unsigned long WATER_CHECK_INTERVAL_MS = 5UL * 60UL * 1000UL;
static const unsigned long SENSOR_REPORT_INTERVAL_MS = 5000UL;
static const unsigned long WIFI_RECONNECT_INTERVAL_MS = 15000UL;
static const unsigned long MAINS_CHECK_INTERVAL_MS = 1000UL;
static const unsigned long KEYPAD_POLL_INTERVAL_MS = 40UL;
static const unsigned long LOCAL_CONFIG_REFRESH_INTERVAL_MS = 2000UL;
static const unsigned long HEARTBEAT_LOG_INTERVAL_MS = 5000UL;
static const unsigned long MAIN_LOOP_DELAY_MS = 20UL;

// =========================
// Hardware defaults
// =========================
static const float DEFAULT_MAX_FEEDS_CAPACITY_KG = 1.0f;
static const float DEFAULT_FEEDER_LOW_THRESHOLD_PCT = 20.0f;
static const float DEFAULT_FEEDER_HIGH_THRESHOLD_PCT = 80.0f;
static const float DEFAULT_WATER_LOW_THRESHOLD_PCT = 20.0f;
static const float DEFAULT_WATER_HIGH_THRESHOLD_PCT = 80.0f;
static const float DEFAULT_ALERT_FEEDER_LOW_THRESHOLD_PCT = 20.0f;
static const float DEFAULT_ALERT_FEEDER_HIGH_THRESHOLD_PCT = 80.0f;
static const float DEFAULT_ALERT_WATER_LOW_THRESHOLD_PCT = 20.0f;
static const float DEFAULT_ALERT_WATER_HIGH_THRESHOLD_PCT = 80.0f;
static const float DEFAULT_MAX_SINGLE_FEED_KG = 1.00f;

// Feeder motor behavior
// Set false for active-low relay modules; true for active-high drivers.
static const bool FEED_MOTOR_ACTIVE_HIGH = true;
static const bool WATER_SOLENOID_ACTIVE_HIGH = true;
// AC sense behavior: HIGH means mains loss, LOW means mains present.
static const bool MAINS_LOSS_SIGNAL_ACTIVE_HIGH = true;
static const char* DEFAULT_GRAIN_TYPE = "pellet_small";
static const unsigned long FEED_MOTOR_STARTUP_MS = 200UL;

// Arbitrary starter calibrations; replace after real measurement.
static const float FEED_MS_PER_KG_PELLET_SMALL = 4200.0f;
static const float FEED_MS_PER_KG_PELLET_LARGE = 5200.0f;
static const float FEED_MS_PER_KG_CRUMBLE = 6100.0f;
static const float FEED_MS_PER_KG_MASH = 7000.0f;

// Ultrasonic geometry defaults (distance from sensor to tank bottom)
static const float FEEDER_TANK_DEPTH_CM = 30.0f;
static const float WATER_TANK_DEPTH_CM = 30.0f;

// Sensor sanity
static const float MIN_VALID_DISTANCE_CM = 2.0f;
static const float MAX_VALID_DISTANCE_CM = 400.0f;

// Buzzer error tone
static const unsigned int BUZZER_ERROR_TONE_A_HZ = 440U;
static const unsigned int BUZZER_ERROR_TONE_E_HZ = 659U;
static const unsigned long BUZZER_ERROR_TONE_A_MS = 150UL;
static const unsigned long BUZZER_ERROR_TONE_E_MS = 150UL;
static const unsigned long BUZZER_ERROR_TONE_GAP_MS = 180UL;
static const unsigned long BUZZER_ERROR_ALERT_WINDOW_MS = 10UL * 1000UL;
static const unsigned long BUZZER_ERROR_ALERT_INTERVAL_MS = 5UL * 60UL * 1000UL;

// Buzzer resolved tone
static const unsigned int BUZZER_RESOLVED_TONE_C_HZ = 523U;
static const unsigned int BUZZER_RESOLVED_TONE_E_HZ = 659U;
static const unsigned long BUZZER_RESOLVED_TONE_C_MS = 120UL;
static const unsigned long BUZZER_RESOLVED_TONE_E_MS = 150UL;
static const unsigned long BUZZER_RESOLVED_TONE_GAP_MS = 150UL;

// Buzzer feeding cue tone
static const unsigned int BUZZER_FEED_TONE_C_HZ = 523U;
static const unsigned int BUZZER_FEED_TONE_E_HZ = 659U;
static const unsigned long BUZZER_FEED_TONE_C_MS = 70UL;
static const unsigned long BUZZER_FEED_TONE_E_MS = 90UL;
static const unsigned long BUZZER_FEED_TONE_GAP_MS = 35UL;

// Analog keypad tuning
static const int KEYPAD_ADC_NO_KEY_MIN = 3900;
static const int KEYPAD_IDLE_BAND_ADC = 30;
static const int KEYPAD_MATCH_TOLERANCE_ADC = 110;
static const int KEYPAD_SAME_KEY_HYSTERESIS_ADC = 60;
static const int KEYPAD_RUNTIME_SAMPLES = 5;
static const unsigned long KEYPAD_EVENT_DEBOUNCE_MS = 60UL;
static const uint8_t KEYPAD_STABLE_POLLS_REQUIRED = 2U;

// LCD defaults
static const uint8_t LCD_I2C_ADDRESS = 0x27;
static const uint8_t LCD_COLS = 20;
static const uint8_t LCD_ROWS = 4;

#endif
