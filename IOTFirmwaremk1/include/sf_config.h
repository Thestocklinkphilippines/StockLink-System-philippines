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
#define SF_ENABLE_MAINS_MONITOR 0

// =========================
// Network and API settings
// =========================
static const char* WIFI_SSID = "MERCUSYS_USER_noNet";
static const char* WIFI_PASS = "ProMercuUser!69";
static const char* SERVER_BASE = "http://192.168.0.100:8000/api";
static const char* DEVICE_ID = "001";
static const char* AUTH_TOKEN = "devtoken";

// =========================
// Preferences storage keys
// =========================
static const char* PREF_NAMESPACE = "feeder";
static const char* PREF_CONFIG_KEY = "cfg";
static const char* PREF_REMAINING_KG = "remaining";

// =========================
// Timing
// =========================
static const unsigned long SYNC_INTERVAL_MS = 10000UL;
static const unsigned long SCHEDULE_CHECK_INTERVAL_MS = 60000UL;
static const unsigned long WATER_CHECK_INTERVAL_MS = 5UL * 60UL * 1000UL;
static const unsigned long SENSOR_REPORT_INTERVAL_MS = 60000UL;
static const unsigned long WIFI_RECONNECT_INTERVAL_MS = 15000UL;
static const unsigned long MAINS_CHECK_INTERVAL_MS = 1000UL;
static const unsigned long KEYPAD_POLL_INTERVAL_MS = 250UL;
static const unsigned long LOCAL_CONFIG_REFRESH_INTERVAL_MS = 2000UL;
static const unsigned long HEARTBEAT_LOG_INTERVAL_MS = 60000UL;
static const unsigned long MAIN_LOOP_DELAY_MS = 100UL;

// =========================
// Hardware defaults
// =========================
static const float DEFAULT_MAX_FEEDS_CAPACITY_KG = 1.0f;
static const float DEFAULT_FEEDER_LOW_THRESHOLD_PCT = 20.0f;
static const float DEFAULT_FEEDER_HIGH_THRESHOLD_PCT = 80.0f;
static const float DEFAULT_WATER_LOW_THRESHOLD_PCT = 20.0f;
static const float DEFAULT_WATER_HIGH_THRESHOLD_PCT = 80.0f;

// Ultrasonic geometry defaults (distance from sensor to tank bottom)
static const float FEEDER_TANK_DEPTH_CM = 30.0f;
static const float WATER_TANK_DEPTH_CM = 30.0f;

// Sensor sanity
static const float MIN_VALID_DISTANCE_CM = 2.0f;
static const float MAX_VALID_DISTANCE_CM = 400.0f;

// Analog keypad tuning
static const int KEYPAD_ADC_NO_KEY_MIN = 3900;

#endif
