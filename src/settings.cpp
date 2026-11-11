#include "settings.h"
#include <Preferences.h>
#include <stddef.h>

Settings cfg;
Stats stats;

static Preferences prefs;
static const uint32_t CFG_MAGIC = 0x0E5A2026;
static const uint16_t CFG_VERSION = 3;
static uint32_t saveRequestedAt = 0;

void settingsDefaults(Settings &s) {
  memset(&s, 0, sizeof(s));
  s.magic = CFG_MAGIC;
  s.version = CFG_VERSION;

  s.mode = MODE_AUTO;
  s.tComfort = 215;  s.tEco = 180;  s.tManual = 210;
  s.tVacation = 150; s.tFrost = 70; s.tBoost = 240; s.tMax = 280;

  s.algo = ALGO_HYST;
  s.hyst = 4;        // 0,4 °C
  s.tpiCycle = 15;
  s.tpiBand = 15;
  s.minOn = 3;       // siłownik termoelektryczny otwiera się ok. 3 min
  s.minOff = 3;

  s.outActiveLow = 1;
  s.valveNO = 0;
  s.antiBlock = 1;
  s.abDay = 0;       // poniedziałek
  s.abHour = 10;
  s.abMinutes = 5;

  s.offset = 0;
  s.filter = 2;
  s.faultMode = FAULT_DUTY;
  s.faultDuty = 20;

  s.winEnable = 1;
  s.winDrop = 10;    // 1,0 °C
  s.winTime = 5;
  s.winPause = 20;

  s.boostMin = 60;
  s.vacDays = 7;
  s.vacUntil = 0;

  s.brightness = 80;
  s.dimBrightness = 10;
  s.dimAfter = 30;
  s.childLock = 0;

  s.ntp = 1;
  s.wifiEnable = 1;

  // Program domyślny: dni robocze 6:00–8:00 i 15:00–22:00, weekend 7:30–23:00
  for (int d = 0; d < 7; d++) {
    DaySchedule &ds = s.week[d];
    if (d < 5) {
      ds.count = 2;
      ds.start[0] = 6 * 60;       ds.end[0] = 8 * 60;
      ds.start[1] = 15 * 60;      ds.end[1] = 22 * 60;
    } else {
      ds.count = 1;
      ds.start[0] = 7 * 60 + 30;  ds.end[0] = 23 * 60;
    }
  }
  s.touchValid = 0;

  s.theme = THEME_DARK;
  s.themeDayFrom = 7;
  s.themeNightFrom = 21;
  s.wifiTx = 0;
  s.humOffset = 0;      // 8,5 dBm – płytki ESP32-C3 Mini mają wadliwy tor antenowy przy pełnej mocy
}

void settingsLoad() {
  prefs.begin("termostat", false);
  size_t n = prefs.getBytesLength("cfg");
  bool ok = false;
  // Akceptuj także krótszy zapis ze starszej wersji: brakujące (nowe) pola
  // dostają wartości domyślne, a kalibracja dotyku i ustawienia zostają.
  if (n >= offsetof(Settings, theme) && n <= sizeof(Settings)) {
    Settings def, tmp;
    settingsDefaults(def);
    tmp = def;
    prefs.getBytes("cfg", &tmp, n);
    if (tmp.magic == CFG_MAGIC && tmp.version >= 1 && tmp.version <= CFG_VERSION) {
      bool migrate = tmp.version < CFG_VERSION || n != sizeof(Settings);
      if (tmp.version == 1) {
        // 1.0 nie miała motywu (rozpoznanie po długości zapisu), 1.0 i 1.1 – mocy nadajnika
        if (n < offsetof(Settings, themeNightFrom) + 1) {
          tmp.theme = def.theme;
          tmp.themeDayFrom = def.themeDayFrom;
          tmp.themeNightFrom = def.themeNightFrom;
        }
        tmp.wifiTx = def.wifiTx;
      }
      if (tmp.version <= 2) tmp.humOffset = def.humOffset;   // 1.0–1.2 bez wilgotności
      tmp.version = CFG_VERSION;
      if (tmp.wifiTx > 5) tmp.wifiTx = def.wifiTx;
      if (tmp.humOffset < -10 || tmp.humOffset > 10) tmp.humOffset = 0;
      cfg = tmp;
      ok = true;
      if (migrate) settingsSaveNow();
    }
  }
  if (!ok) {
    settingsDefaults(cfg);
    settingsSaveNow();
  }
  if (prefs.getBytesLength("stats") == sizeof(Stats)) {
    prefs.getBytes("stats", &stats, sizeof(Stats));
  }
  if (stats.magic != CFG_MAGIC) {
    memset(&stats, 0, sizeof(stats));
    stats.magic = CFG_MAGIC;
  }
}

void settingsSave() { saveRequestedAt = millis() | 1; }

void settingsSaveNow() {
  prefs.putBytes("cfg", &cfg, sizeof(Settings));
  saveRequestedAt = 0;
}

void settingsLoop() {
  if (saveRequestedAt && millis() - saveRequestedAt > 3000) settingsSaveNow();
}

void settingsFactoryReset() {
  uint16_t cal[8];
  uint8_t calValid = cfg.touchValid;
  memcpy(cal, cfg.touchCal, sizeof(cal));
  settingsDefaults(cfg);
  cfg.touchValid = calValid;          // kalibracja dotyku zostaje
  memcpy(cfg.touchCal, cal, sizeof(cal));
  settingsSaveNow();
}

void statsSave() { prefs.putBytes("stats", &stats, sizeof(Stats)); }

void statsReset() {
  memset(&stats, 0, sizeof(stats));
  stats.magic = CFG_MAGIC;
  statsSave();
}

// ------------------------------------------------------------- program -----
void scheduleSort(DaySchedule &d) {
  // usuń nieprawidłowe, posortuj po początku
  uint8_t n = 0;
  for (uint8_t i = 0; i < d.count && i < MAX_PERIODS; i++) {
    if (d.end[i] > d.start[i]) {
      d.start[n] = d.start[i];
      d.end[n] = d.end[i];
      n++;
    }
  }
  d.count = n;
  for (uint8_t i = 1; i < n; i++)
    for (uint8_t j = i; j > 0 && d.start[j] < d.start[j - 1]; j--) {
      std::swap(d.start[j], d.start[j - 1]);
      std::swap(d.end[j], d.end[j - 1]);
    }
}

bool scheduleIsComfort(uint8_t dow, uint16_t minute) {
  const DaySchedule &d = cfg.week[dow % 7];
  for (uint8_t i = 0; i < d.count; i++)
    if (minute >= d.start[i] && minute < d.end[i]) return true;
  return false;
}

bool scheduleNextChange(uint8_t dow, uint16_t minute, uint8_t &dayOffset, uint16_t &atMinute) {
  bool now = scheduleIsComfort(dow, minute);
  // skanowanie co 15 min przez 7 dni (rozdzielczość programu = 15 min)
  uint16_t m = (minute / 15 + 1) * 15;
  for (uint8_t off = 0; off < 8; off++) {
    for (; m < 1440; m += 15) {
      if (scheduleIsComfort((dow + off) % 7, m) != now) {
        dayOffset = off;
        atMinute = m;
        return true;
      }
    }
    m = 0;
  }
  return false;
}
