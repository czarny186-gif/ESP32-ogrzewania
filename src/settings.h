// Ustawienia trwałe (NVS / Preferences). Temperatury w dziesiątych °C.
#pragma once
#include <Arduino.h>

enum Mode : uint8_t { MODE_AUTO = 0, MODE_MANUAL, MODE_ECO, MODE_OFF, MODE_VACATION, MODE_COUNT };
enum Algo : uint8_t { ALGO_HYST = 0, ALGO_TPI };
enum FaultMode : uint8_t { FAULT_OFF = 0, FAULT_DUTY };

#define MAX_PERIODS 4

struct DaySchedule {
  uint8_t  count;                 // liczba okresów komfortu (0..4)
  uint16_t start[MAX_PERIODS];    // minuty od północy
  uint16_t end[MAX_PERIODS];
};

struct Settings {
  uint32_t magic;
  uint16_t version;

  // --- tryb i temperatury
  uint8_t mode;
  int16_t tComfort, tEco, tManual, tVacation, tFrost, tBoost, tMax;

  // --- regulacja
  uint8_t algo;          // ALGO_HYST / ALGO_TPI
  uint8_t hyst;          // x0.1 °C (1..20)
  uint8_t tpiCycle;      // min (10..30)
  uint8_t tpiBand;       // x0.1 °C (5..40) – pasmo proporcjonalności
  uint8_t minOn;         // min
  uint8_t minOff;        // min

  // --- wyjście / zawór
  uint8_t outActiveLow;  // 1 = sterowanie ujemne (domyślnie)
  uint8_t valveNO;       // 0 = siłownik NC (bez napięcia zamknięty), 1 = NO
  uint8_t antiBlock;     // ochrona zaworu przed zastaniem
  uint8_t abDay;         // 0 = poniedziałek ... 6 = niedziela
  uint8_t abHour;
  uint8_t abMinutes;     // czas przewietrzenia zaworu

  // --- czujnik
  int8_t  offset;        // x0.1 °C (-50..50)
  uint8_t filter;        // 0 = brak .. 5 = mocne wygładzanie
  uint8_t faultMode;     // co robić przy awarii czujnika
  uint8_t faultDuty;     // % czasu grzania w awarii (10..50)

  // --- okno otwarte
  uint8_t winEnable;
  uint8_t winDrop;       // x0.1 °C spadku
  uint8_t winTime;       // w ciągu ilu minut
  uint8_t winPause;      // przerwa w grzaniu (min)

  // --- boost / urlop
  uint8_t  boostMin;     // min (15..240)
  uint8_t  vacDays;      // dni (1..60)
  uint32_t vacUntil;     // epoch, 0 = nieaktywny

  // --- ekran
  uint8_t brightness;    // %
  uint8_t dimBrightness; // %
  uint16_t dimAfter;     // s (0 = nigdy)
  uint8_t childLock;

  // --- zegar / sieć
  uint8_t ntp;
  uint8_t wifiEnable;
  char    ssid[33];
  char    pass[65];

  // --- program tygodniowy
  DaySchedule week[7];   // 0 = poniedziałek

  // --- kalibracja dotyku
  uint8_t  touchValid;
  uint16_t touchCal[8];
};

extern Settings cfg;

void settingsDefaults(Settings &s);
void settingsLoad();
void settingsSave();          // zapis odroczony (po 3 s od ostatniej zmiany)
void settingsSaveNow();
void settingsLoop();
void settingsFactoryReset();

// Statystyki trwałe (zapisywane co godzinę)
struct Stats {
  uint32_t magic;
  uint32_t relayCycles;      // liczba załączeń
  uint32_t heatSecondsTotal; // łączny czas grzania
  uint32_t uptimeTotal;      // łączny czas pracy [min]
};
extern Stats stats;
void statsSave();
void statsReset();

// Program tygodniowy – pomocnicze
void scheduleSort(DaySchedule &d);
bool scheduleIsComfort(uint8_t dow, uint16_t minute);
// Najbliższa zmiana komfort/eko w ciągu 7 dni. Zwraca false, gdy program nie
// ma żadnych zmian (np. pusty). dayOffset: 0 = dziś, 1 = jutro...
bool scheduleNextChange(uint8_t dow, uint16_t minute, uint8_t &dayOffset, uint16_t &atMinute);
