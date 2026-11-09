#include "clock.h"
#include "config.h"
#include <sys/time.h>

void clockBegin() {
  setenv("TZ", TZ_POLAND, 1);
  tzset();
}

void clockStartNtp() {
  configTzTime(TZ_POLAND, NTP_SERVER1, NTP_SERVER2);
}

time_t clockNow() { return time(nullptr); }

bool clockValid() { return clockNow() > 1700000000; }  // po 2023 r.

bool clockLocal(struct tm &t) {
  time_t n = clockNow();
  localtime_r(&n, &t);
  return clockValid();
}

uint8_t clockDow(const struct tm &t) { return (t.tm_wday + 6) % 7; }

void clockSetLocal(int year, int mon, int day, int hour, int min) {
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = mon - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_sec = 0;
  t.tm_isdst = -1;
  time_t e = mktime(&t);
  struct timeval tv = {e, 0};
  settimeofday(&tv, nullptr);
}
