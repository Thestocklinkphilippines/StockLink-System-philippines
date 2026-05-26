#ifndef SF_DEBUG_H
#define SF_DEBUG_H

#include "sf_config.h"
#include "sf_wireless_console.h"
#include "sf_multi_console.h"

extern bool gSerialConsoleExclusive;

#if SF_VERBOSE_SERIAL
  #define LOG_INFO(fmt, ...)  do { if (!gSerialConsoleExclusive) Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__); } while (0)
  #define LOG_WARN(fmt, ...)  do { if (!gSerialConsoleExclusive) Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__); } while (0)
  #define LOG_ERROR(fmt, ...) do { if (!gSerialConsoleExclusive) Serial.printf("[ERR ] " fmt "\n", ##__VA_ARGS__); } while (0)
  #define LOG_DEBUG(fmt, ...) do { if (!gSerialConsoleExclusive) Serial.printf("[DBG ] " fmt "\n", ##__VA_ARGS__); } while (0)
#else
  #define LOG_INFO(fmt, ...)
  #define LOG_WARN(fmt, ...)
  #define LOG_ERROR(fmt, ...)
  #define LOG_DEBUG(fmt, ...)
#endif

// System alert routing to dedicated port (always shown, not muted by console exclusive mode)
#define LOG_SYSTEM_ALERT(fmt, ...) do { SFMultiConsole.systemPrintf("[ALERT] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_SYSTEM_EVENT(fmt, ...) do { SFMultiConsole.systemPrintf("[EVENT] " fmt "\n", ##__VA_ARGS__); } while (0)

// Keypad routing to dedicated keypad port
#define LOG_KEYPAD_INFO(fmt, ...) do { SFMultiConsole.keypadPrintf("[INFO] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_KEYPAD_WARN(fmt, ...) do { SFMultiConsole.keypadPrintf("[WARN] " fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_KEYPAD_ERROR(fmt, ...) do { SFMultiConsole.keypadPrintf("[ERR ] " fmt "\n", ##__VA_ARGS__); } while (0)

#endif
