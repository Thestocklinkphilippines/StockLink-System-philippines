#ifndef SF_CONFIG_H
#define SF_CONFIG_H

// =========================
// Build and runtime toggles
// =========================
#define SF_VERBOSE_SERIAL 1
#define SF_SEND_KEYPAD_LOGS 0
#define SF_SEND_HEARTBEAT_LOGS 1
#define SF_ENABLE_KEYPAD_INPUT 1
#define SF_ENABLE_MAINS_MONITOR 1

// =========================
// Network and API settings
// =========================
static const char* WIFI_SSID = "StockLink_Test_Router1";
static const char* WIFI_PASS = "chickenstock12";
static const char* SERVER_BASE = "http://192.168.0.100:8000/api";
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
static const char* PREF_EVENT_OUTBOX = "event_outbox";
static const char* PREF_EVENT_SEQ = "event_seq";
static const char* PREF_MAINS_POWER_PRESENT = "mains_present";

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
static const float DEFAULT_MAX_FEEDS_CAPACITY_KG = 5.0f;
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
static const char* DEFAULT_GRAIN_TYPE = "mash";
struct GrainTypeDefault {
	const char* grain_type;
	float feed_ms_per_kg;
};
static const GrainTypeDefault DEFAULT_GRAIN_TYPES[] = {
		{"mash", 137820.8f},
		{"crumbles", 7000.0f},
		{"mini_pellets", 4200.0f},
		{"standard_pellets", 5200.0f},
		{"large_pellets", 6100.0f},
};
static const int DEFAULT_GRAIN_TYPE_COUNT = (int)(sizeof(DEFAULT_GRAIN_TYPES) / sizeof(DEFAULT_GRAIN_TYPES[0]));
static const unsigned long FEED_MOTOR_STARTUP_MS = 200UL;

// Grain calibrations exposed to the server/UI.
static const float FEED_MS_PER_KG_MASH = DEFAULT_GRAIN_TYPES[0].feed_ms_per_kg;
static const float FEED_MS_PER_KG_CRUMBLES = DEFAULT_GRAIN_TYPES[1].feed_ms_per_kg;
static const float FEED_MS_PER_KG_MINI_PELLETS = DEFAULT_GRAIN_TYPES[2].feed_ms_per_kg;
static const float FEED_MS_PER_KG_STANDARD_PELLETS = DEFAULT_GRAIN_TYPES[3].feed_ms_per_kg;
static const float FEED_MS_PER_KG_LARGE_PELLETS = DEFAULT_GRAIN_TYPES[4].feed_ms_per_kg;

// Ultrasonic geometry defaults.
static const float FEEDER_TANK_BOTTOM_DISTANCE_CM = 35.0f;
static const float FEEDER_MAX_FEED_HEIGHT_CM = 25.0f;
static const float FEEDER_TANK_DEPTH_CM = FEEDER_TANK_BOTTOM_DISTANCE_CM;
static const float WATER_TANK_DEPTH_CM = 20.0f;

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
// Runtime ADC correction (polled values are known to read slightly high).
static const int KEYPAD_ADC_OFFSET = -50;
// Symmetric +/- tolerance used when acquiring a new key from ADC centers.
static const int KEYPAD_ADC_TOLERANCE = 110;
// Additional hold margin before a pressed key is released.
static const int KEYPAD_ADC_RELEASE_HYSTERESIS = 60;
// Shrinks each key's acceptance window away from nearest neighbors.
static const int KEYPAD_WINDOW_EDGE_PAD_ADC = 3;
static const int KEYPAD_RUNTIME_SAMPLES = 20;
static const unsigned long KEYPAD_EVENT_DEBOUNCE_MS = 100UL;
static const uint8_t KEYPAD_STABLE_POLLS_REQUIRED = 2U;

// Battery voltage sensing defaults
static const int BATTERY_ADC_PIN = 33;
static const float BATTERY_DIVIDER_TOP_OHMS = 46000.0f;
static const float BATTERY_DIVIDER_BOTTOM_OHMS = 8210.0f;
static const float BATTERY_ADC_REFERENCE_V = 3.295f;
static const float BATTERY_ADC_GAIN_CORRECTION = 1.106f;

// Low battery shutdown threshold (change to adjust shutdown voltage)
static const float DEFAULT_LOW_BATTERY_SHUTDOWN_V = 12.1f;

// LCD defaults
static const uint8_t LCD_I2C_ADDRESS = 0x27;
static const uint8_t LCD_COLS = 20;
static const uint8_t LCD_ROWS = 4;

#endif
