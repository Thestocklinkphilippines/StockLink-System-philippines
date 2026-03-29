#ifndef SF_DEBUG_H
#define SF_DEBUG_H

#include "sf_config.h"

#if SF_VERBOSE_SERIAL
  #define LOG_INFO(fmt, ...)  Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
  #define LOG_WARN(fmt, ...)  Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
  #define LOG_ERROR(fmt, ...) Serial.printf("[ERR ] " fmt "\n", ##__VA_ARGS__)
  #define LOG_DEBUG(fmt, ...) Serial.printf("[DBG ] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG_INFO(fmt, ...)
  #define LOG_WARN(fmt, ...)
  #define LOG_ERROR(fmt, ...)
  #define LOG_DEBUG(fmt, ...)
#endif

#endif
