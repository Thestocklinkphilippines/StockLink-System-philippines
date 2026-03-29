#include "sf_utils.h"

#include <ctype.h>
#include <stdlib.h>

float clampf(float v, float minV, float maxV) {
  if (v < minV) return minV;
  if (v > maxV) return maxV;
  return v;
}

String getUtcIsoNow() {
  time_t now;
  time(&now);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

time_t parseIsoUtc(const char* iso) {
  auto parseTzOffsetSeconds = [](const char* tzPart) -> int {
    if (!tzPart || *tzPart == '\0') return 0;
    if (*tzPart == 'Z' || *tzPart == 'z') return 0;
    if (*tzPart != '+' && *tzPart != '-') return 0;

    int sign = (*tzPart == '-') ? -1 : 1;
    int hh = 0;
    int mm = 0;
    const char* p = tzPart + 1;

    if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) return 0;
    hh = (p[0] - '0') * 10 + (p[1] - '0');
    p += 2;

    if (*p == ':') {
      p++;
      if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) return sign * hh * 3600;
      mm = (p[0] - '0') * 10 + (p[1] - '0');
    } else if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
      mm = (p[0] - '0') * 10 + (p[1] - '0');
    }

    return sign * (hh * 3600 + mm * 60);
  };

  struct tm tm = {};
  if (!iso) return 0;

  char* end = strptime(iso, "%Y-%m-%dT%H:%M:%S", &tm);
  if (end == NULL) {
    end = strptime(iso, "%Y-%m-%d %H:%M:%S", &tm);
  }
  if (end == NULL) return 0;

  if (*end == '.') {
    end++;
    while (*end && isdigit((unsigned char)*end)) end++;
  }

  int tzOffsetSeconds = parseTzOffsetSeconds(end);

  char* tz = getenv("TZ");
  setenv("TZ", "UTC", 1);
  tzset();
  time_t t = mktime(&tm);
  if (tz) setenv("TZ", tz, 1); else unsetenv("TZ");
  tzset();
  if (t <= 0) return 0;

  return t - tzOffsetSeconds;
}
