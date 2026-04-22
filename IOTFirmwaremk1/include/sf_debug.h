#ifndef SF_DEBUG_H
#define SF_DEBUG_H

#include "sf_config.h"

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

#endif
