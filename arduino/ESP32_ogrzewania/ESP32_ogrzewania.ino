// ============================================================================
//  ESP32_ogrzewania – termostat pokojowy z dotykowym menu graficznym
//
//  Płytka:      WEMOS LOLIN C3 Mini (ESP32-C3)
//  Wyświetlacz: 4" ST7796 480x320 SPI + dotyk rezystancyjny XPT2046
//  Czujnik:     DS18B20
//  Wyjście:     GPIO3, AKTYWNE STANEM NISKIM -> przekaźnik / stycznik głowicy
//
//  Arduino IDE:
//   1. Menedżer płytek: "esp32" (Espressif) w wersji 3.x
//   2. Menedżer bibliotek: LovyanGFX, OneWire, DallasTemperature
//   3. Narzędzia -> Płytka: "LOLIN C3 Mini"
//      Narzędzia -> Partition Scheme: "No OTA (2MB APP/2MB SPIFFS)"
//      Narzędzia -> USB CDC On Boot: "Enabled"
//   4. Plik czcionki.h musi leżeć w tym samym folderze co ten plik.
//
//  POŁĄCZENIA
//   ESP32-C3      ST7796 / XPT2046 / DS18B20 / przekaźnik
//   GPIO4   ->    SCK  + T_CLK
//   GPIO6   ->    SDI  + T_DIN  (MOSI)
//   GPIO5   <-    T_DO          (SDO wyświetlacza NIE podłączać)
//   GPIO10  ->    TFT CS
//   GPIO8   ->    TFT DC/RS
//   GPIO1   ->    TFT RESET
//   GPIO21  ->    TFT LED (podświetlenie PWM)
//   GPIO20  ->    T_CS          (T_IRQ niepodłączony)
//   GPIO0   <->   DS18B20 DQ    + 4,7 kΩ do 3V3
//   GPIO3   ->    IN przekaźnika (aktywny LOW) + 10 kΩ do 3V3
//   5V/GND  ->    VCC/GND wyświetlacza, JD-VCC przekaźnika
//
//  Plik wygenerowany z repozytorium przez tools/make_ino.py.
// ============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <sys/time.h>
#include "czcionki.h"


// ========================================================== config.h
// ============================================================================
//  ESP32-ogrzewanie – konfiguracja sprzętu
//  Płytka: WEMOS LOLIN C3 Mini (ESP32-C3FH4), wyświetlacz 4" ST7796 480x320
//  z dotykiem rezystancyjnym XPT2046, czujnik DS18B20, wyjście na stycznik /
//  przekaźnik głowicy termoelektrycznej sterowane stanem NISKIM.
// ============================================================================
#include <Arduino.h>

#define FW_VERSION "1.0.0"
#define DEVICE_NAME "Termostat"

// ---------------------------------------------------------------- PINY -----
// Wspólna magistrala SPI (FSPI / SPI2) dla wyświetlacza i panelu dotyku
#define PIN_SPI_SCK    4   // TFT SCK  + T_CLK
#define PIN_SPI_MOSI   6   // TFT SDI  + T_DIN
#define PIN_SPI_MISO   5   // T_DO (SDO wyświetlacza NIE podłączać)

#define PIN_TFT_CS    10   // TFT CS
#define PIN_TFT_DC     8   // TFT DC/RS   (pin "strapping" – sterowany dopiero po starcie, OK)
#define PIN_TFT_RST    1   // TFT RESET
#define PIN_TFT_BL    21   // TFT LED – podświetlenie PWM (w czasie bootu mignie, to normalne)
#define PIN_TOUCH_CS  20   // T_CS
                           // T_IRQ – niepodłączony (odpytywanie)

#define PIN_DS18B20    0   // DQ czujnika + rezystor 4,7 kΩ do 3V3
#define PIN_OUTPUT     3   // IN modułu przekaźnika / stycznika – AKTYWNY STAN NISKI
                           // + rezystor 10 kΩ do 3V3 (stan bezpieczny przy resecie)
#define PIN_RGB_LED    7   // wbudowana dioda WS2812 (LOLIN C3 Mini v2.1) – status
#define PIN_BOOT_BTN   9   // przycisk BOOT na płytce – wybudzenie ekranu / reset dotyku

// ------------------------------------------------------------ STAŁE --------
#define SCREEN_W 480
#define SCREEN_H 320

#define SENSOR_PERIOD_MS     2000UL  // co ile odczyt DS18B20
#define SENSOR_FAIL_COUNT    5       // ile błędnych odczytów = awaria
#define CONTROL_PERIOD_MS    1000UL  // pętla regulacji
#define HISTORY_PERIOD_S     300     // próbka wykresu co 5 min
#define HISTORY_LEN          288     // 24 h * 12
#define UI_IDLE_HOME_MS      60000UL // powrót do ekranu głównego po 60 s bezczynności
#define LONG_PRESS_UNLOCK_MS 3000UL  // odblokowanie blokady rodzicielskiej

#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.google.com"
#define TZ_POLAND   "CET-1CEST,M3.5.0,M10.5.0/3"  // czas polski z DST

// ========================================================== display.h
// Konfiguracja LovyanGFX: ST7796 (SPI) + XPT2046 na wspólnej magistrali.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796  _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
  lgfx::Touch_XPT2046 _touch;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host    = SPI2_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;   // przy zakłóceniach obniż do 27 MHz
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = PIN_SPI_SCK;
      cfg.pin_mosi    = PIN_SPI_MOSI;
      cfg.pin_miso    = PIN_SPI_MISO;
      cfg.pin_dc      = PIN_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs          = PIN_TFT_CS;
      cfg.pin_rst         = PIN_TFT_RST;
      cfg.pin_busy        = -1;
      cfg.panel_width     = 320;
      cfg.panel_height    = 480;
      cfg.offset_x        = 0;
      cfg.offset_y        = 0;
      cfg.offset_rotation = 0;
      cfg.readable        = false;  // SDO wyświetlacza nie jest podłączone
      cfg.invert          = false;  // jeśli kolory są w negatywie -> true
      cfg.rgb_order       = false;  // jeśli zamienione R i B -> true
      cfg.dlen_16bit      = false;
      cfg.bus_shared      = true;   // magistrala współdzielona z dotykiem
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl      = PIN_TFT_BL;
      cfg.invert      = false;
      cfg.freq        = 12000;
      cfg.pwm_channel = 1;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min           = 300;
      cfg.x_max           = 3900;
      cfg.y_min           = 200;
      cfg.y_max           = 3900;
      cfg.pin_int         = -1;
      cfg.bus_shared      = true;
      cfg.offset_rotation = 0;
      cfg.spi_host        = SPI2_HOST;
      cfg.freq            = 1000000;
      cfg.pin_sclk        = PIN_SPI_SCK;
      cfg.pin_mosi        = PIN_SPI_MOSI;
      cfg.pin_miso        = PIN_SPI_MISO;
      cfg.pin_cs          = PIN_TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

extern LGFX lcd;

// ========================================================== settings.h
// Ustawienia trwałe (NVS / Preferences). Temperatury w dziesiątych °C.
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

// ========================================================== sensor.h
// Odczyt DS18B20 bez blokowania pętli (konwersja asynchroniczna).
#include <Arduino.h>

struct SensorState {
  bool    present = false;   // znaleziono czujnik przy starcie
  bool    fault = true;      // brak poprawnego odczytu
  float   raw = NAN;         // ostatni surowy odczyt
  float   value = NAN;       // po korekcie i filtrze
  uint8_t rom[8] = {0};
  uint32_t lastOk = 0;
  uint16_t errors = 0;       // licznik błędów od startu
};

extern SensorState sensor;

void sensorBegin();
void sensorLoop();

// ========================================================== clock.h
// Czas lokalny (strefa PL z DST). Źródło: NTP przez Wi-Fi lub ręczne ustawienie.
#include <Arduino.h>
#include <time.h>

void clockBegin();
bool clockValid();                 // czy czas jest ustawiony
time_t clockNow();
bool clockLocal(struct tm &t);     // false, gdy czas nieustawiony
uint8_t clockDow(const struct tm &t); // 0 = poniedziałek
void clockSetLocal(int year, int mon, int day, int hour, int min);
void clockStartNtp();

// ========================================================== thermostat.h
// Logika regulatora: tryby, program, histereza / TPI, ochrony, statystyki.
#include <Arduino.h>

enum TargetSource : uint8_t {
  SRC_COMFORT = 0, SRC_ECO, SRC_MANUAL, SRC_FROST, SRC_VACATION,
  SRC_BOOST, SRC_OVERRIDE, SRC_WINDOW, SRC_FAULT
};

struct ThermoState {
  int16_t  target = 210;        // zadana efektywna x0.1 °C
  uint8_t  source = SRC_COMFORT;
  bool     demand = false;      // żądanie ciepła z regulatora
  bool     heating = false;     // zawór otwarty (logicznie)
  bool     outputLevel = true;  // stan pinu (HIGH = nieaktywny przy sterowaniu ujemnym)
  float    duty = 0;            // TPI / awaria: wypełnienie 0..1

  bool     boost = false;
  uint32_t boostEndMs = 0;

  bool     override = false;    // chwilowa zmiana zadanej w trybie AUTO
  int16_t  overrideTemp = 0;
  time_t   overrideUntil = 0;   // 0 = do zmiany trybu

  bool     windowOpen = false;
  uint32_t windowEndMs = 0;

  bool     antiBlock = false;
  uint32_t antiBlockEndMs = 0;
  bool     testOutput = false;
  uint32_t testEndMs = 0;

  // statystyki
  uint32_t heatSecToday = 0, heatSecYesterday = 0;
  uint16_t cyclesToday = 0;
  uint16_t weekMinutes[7] = {0};  // [0] = dziś, [1] = wczoraj...

  // historia 24 h (co 5 min)
  int16_t  histTemp[HISTORY_LEN];
  int16_t  histTarget[HISTORY_LEN];
  uint8_t  histHeat[HISTORY_LEN];  // % czasu grzania w próbce
  uint16_t histCount = 0, histHead = 0;
};

extern ThermoState th;

void thermoBegin();
void thermoLoop();                       // wołać często; działa co 1 s
void thermoSetMode(uint8_t mode);
void thermoAdjust(int16_t delta);        // przyciski +/- na ekranie głównym
int16_t thermoAdjustable();              // jaką wartość zmieniają +/- (x0.1)
bool thermoCanAdjust();
void thermoBoost(bool on);
void thermoStartVacation();
void thermoCancelWindow();
void thermoTestOutput(uint16_t seconds);
uint32_t thermoBoostLeftS();
const char *thermoSourceName(uint8_t src);

// ========================================================== net.h
// Wi-Fi: połączenie ze skonfigurowaną siecią (czas NTP + panel WWW),
// tryb punktu dostępowego do konfiguracji z telefonu.
#include <Arduino.h>

enum NetState : uint8_t { NET_OFF = 0, NET_CONNECTING, NET_CONNECTED, NET_AP };

void netBegin();
void netLoop();
void netStartAP();
void netStop();
void netReconnect();
NetState netState();
String netIP();
String netApName();
const char *netApPass();
int netRssi();

// ========================================================== params.h
// Tabela parametrów menu "Ustawienia" – opis, zakres, odczyt/zapis.
#include <Arduino.h>

enum PType : uint8_t { P_NUM, P_ENUM, P_BOOL, P_INFO, P_ACTION };

struct Param {
  const char *name;
  PType type;
  int16_t min, max, step;
  uint8_t dec;                 // miejsca po przecinku (wartość przechowywana x10^dec)
  const char *unit;
  const char *const *opts;     // P_ENUM
  uint8_t nopts;
  int (*get)();
  void (*set)(int);
  void (*info)(char *buf, size_t n);  // P_INFO
  void (*action)();                   // P_ACTION
  const char *help;
  const char *zeroText;        // tekst dla wartości 0 (np. "Nigdy")
  bool confirm;                // akcja wymaga potwierdzenia
};

struct Category {
  const char *name;
  const Param *params;
  uint8_t count;
};

extern const Category CATEGORIES[];
extern const uint8_t CATEGORY_COUNT;

void paramFormat(const Param &p, int v, char *buf, size_t n);

// usługi UI wołane przez akcje
void uiToast(const char *msg);
void uiOpenApScreen();
void uiRequestCalibrate();

// ========================================================== ui.h
// Interfejs graficzny 480x320 z obsługą dotyku.
#include <Arduino.h>

void uiBegin();
void uiLoop();
void uiCalibrateTouch();   // uruchamia kalibrację dotyku (zapis do NVS)
void uiWake();             // wybudzenie ekranu (np. przycisk BOOT)

// ========================================================== typy interfejsu
enum FontId : uint8_t { F_S = 0, F_M, F_B, F_XL, F_BIG };

enum Align : uint8_t { AL_L = 0, AL_C, AL_R };

struct Rect { int16_t x, y, w, h; bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; } };

enum Screen : uint8_t {
  SCR_HOME = 0, SCR_MODE, SCR_VACATION, SCR_PROGRAM, SCR_PERIOD, SCR_COPY,
  SCR_CHART, SCR_MENU, SCR_LIST, SCR_EDIT, SCR_CONFIRM, SCR_AP
};

struct HomeCache {
  char time[8], date[24], temp[12], set[12], mode[64], chip[48];
  uint8_t icons;
  uint32_t setColor;
  bool canAdj;
} hc;

// ========================================================== settings.cpp
#include <Preferences.h>

Settings cfg;
Stats stats;

static Preferences prefs;
static const uint32_t CFG_MAGIC = 0x0E5A2026;
static const uint16_t CFG_VERSION = 1;
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
}

void settingsLoad() {
  prefs.begin("termostat", false);
  size_t n = prefs.getBytesLength("cfg");
  bool ok = false;
  if (n == sizeof(Settings)) {
    prefs.getBytes("cfg", &cfg, sizeof(Settings));
    ok = (cfg.magic == CFG_MAGIC && cfg.version == CFG_VERSION);
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

// ========================================================== sensor.cpp
#include <OneWire.h>
#include <DallasTemperature.h>

SensorState sensor;

static OneWire oneWire(PIN_DS18B20);
static DallasTemperature ds(&oneWire);
static uint32_t lastRequest = 0;
static bool converting = false;
static uint8_t badInRow = 0;

static void findSensor() {
  ds.begin();
  sensor.present = ds.getAddress(sensor.rom, 0);
  if (sensor.present) {
    ds.setResolution(sensor.rom, 12);
    ds.setWaitForConversion(false);
  }
}

void sensorBegin() {
  findSensor();
  lastRequest = millis() - SENSOR_PERIOD_MS;
}

void sensorLoop() {
  uint32_t now = millis();
  if (!converting) {
    if (now - lastRequest < SENSOR_PERIOD_MS) return;
    if (!sensor.present) findSensor();      // ponowne wykrycie po podłączeniu
    if (!sensor.present) {
      lastRequest = now;
      if (++badInRow >= SENSOR_FAIL_COUNT) sensor.fault = true;
      sensor.errors++;
      return;
    }
    ds.requestTemperaturesByAddress(sensor.rom);
    lastRequest = now;
    converting = true;
    return;
  }
  if (now - lastRequest < 800) return;      // 12 bit = 750 ms
  converting = false;

  float t = ds.getTempC(sensor.rom);
  // -127 = brak odpowiedzi, 85.0 = wartość po resecie (błąd zasilania)
  bool bad = (t == DEVICE_DISCONNECTED_C) || (t == 85.0f) || t < -40 || t > 100;
  if (bad) {
    sensor.errors++;
    if (++badInRow >= SENSOR_FAIL_COUNT) {
      sensor.fault = true;
      sensor.present = false;               // spróbuj wykryć ponownie
    }
    return;
  }
  badInRow = 0;
  sensor.raw = t;
  float corrected = t + cfg.offset / 10.0f;
  if (sensor.fault || isnan(sensor.value)) {
    sensor.value = corrected;
  } else {
    // filtr wykładniczy; filter 0 = brak, 5 = mocny
    float alpha = 1.0f / (1.0f + cfg.filter * 1.5f);
    sensor.value += alpha * (corrected - sensor.value);
  }
  sensor.fault = false;
  sensor.lastOk = now;
}

// ========================================================== clock.cpp
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

// ========================================================== thermostat.cpp
ThermoState th;

static uint32_t lastTick = 0;
static uint32_t lastSwitchMs = 0;
static uint32_t lastOnMs = 0;
static bool everOn = false;
static uint32_t cycleStartMs = 0;
static uint32_t cycleOnMs = 0;
static int lastYday = -1;
static int lastAbYday = -1;
static uint16_t histSecs = 0, histHeatSecs = 0;
static uint16_t statsMinuteTicker = 0;

// próbki co minutę do wykrywania otwartego okna (max 15 min)
static float winBuf[16];
static uint8_t winCount = 0;
static uint8_t secToMinute = 0;

static void applyOutput(bool heat) {
  bool energize = cfg.valveNO ? !heat : heat;
  bool level = cfg.outActiveLow ? !energize : energize;
  th.outputLevel = level;
  digitalWrite(PIN_OUTPUT, level ? HIGH : LOW);
}

void thermoBegin() {
  // Stan bezpieczny ZANIM pin stanie się wyjściem (brak "kliknięcia" przy starcie)
  bool heat = false;
  bool energize = cfg.valveNO ? !heat : heat;
  bool level = cfg.outActiveLow ? !energize : energize;
  digitalWrite(PIN_OUTPUT, level ? HIGH : LOW);
  pinMode(PIN_OUTPUT, OUTPUT);
  applyOutput(false);
  lastSwitchMs = millis() - 3600000UL;   // pozwól od razu załączyć
  for (int i = 0; i < HISTORY_LEN; i++) { th.histTemp[i] = INT16_MIN; th.histTarget[i] = 0; th.histHeat[i] = 0; }
}

const char *thermoSourceName(uint8_t src) {
  switch (src) {
    case SRC_COMFORT:  return "Komfort";
    case SRC_ECO:      return "Eko";
    case SRC_MANUAL:   return "Ręczny";
    case SRC_FROST:    return "Ochrona";
    case SRC_VACATION: return "Urlop";
    case SRC_BOOST:    return "Boost";
    case SRC_OVERRIDE: return "Zmiana chwilowa";
    case SRC_WINDOW:   return "Okno otwarte";
    case SRC_FAULT:    return "Awaria";
  }
  return "";
}

// ------------------------------------------------------- zadana efektywna --
static void computeTarget() {
  struct tm t;
  bool tv = clockLocal(t);
  int16_t target;
  uint8_t src;

  // koniec urlopu -> powrót do AUTO
  if (cfg.mode == MODE_VACATION && cfg.vacUntil && tv && clockNow() >= (time_t)cfg.vacUntil) {
    cfg.mode = MODE_AUTO;
    cfg.vacUntil = 0;
    settingsSave();
  }
  // koniec zmiany chwilowej
  if (th.override && th.overrideUntil && tv && clockNow() >= th.overrideUntil) th.override = false;

  switch (cfg.mode) {
    case MODE_MANUAL:   target = cfg.tManual;   src = SRC_MANUAL;   break;
    case MODE_ECO:      target = cfg.tEco;      src = SRC_ECO;      break;
    case MODE_OFF:      target = cfg.tFrost;    src = SRC_FROST;    break;
    case MODE_VACATION: target = cfg.tVacation; src = SRC_VACATION; break;
    default: {
      bool comfort = tv ? scheduleIsComfort(clockDow(t), t.tm_hour * 60 + t.tm_min) : true;
      target = comfort ? cfg.tComfort : cfg.tEco;
      src = comfort ? SRC_COMFORT : SRC_ECO;
      if (th.override) { target = th.overrideTemp; src = SRC_OVERRIDE; }
    }
  }
  if (th.boost) {
    if ((int32_t)(millis() - th.boostEndMs) >= 0) th.boost = false;
    else if (cfg.mode != MODE_OFF) { target = cfg.tBoost; src = SRC_BOOST; }
  }
  if (th.windowOpen) {
    if ((int32_t)(millis() - th.windowEndMs) >= 0) th.windowOpen = false;
    else { target = cfg.tFrost; src = SRC_WINDOW; }
  }
  if (target > cfg.tMax) target = cfg.tMax;
  if (target < cfg.tFrost && cfg.mode != MODE_OFF) target = cfg.tFrost;
  th.target = target;
  th.source = src;
}

// ------------------------------------------------------ wykrywanie okna ----
static void windowDetect() {
  if (!cfg.winEnable || sensor.fault) { winCount = 0; return; }
  if (++secToMinute < 60) return;
  secToMinute = 0;
  if (winCount < 16) winBuf[winCount++] = sensor.value;
  else { memmove(winBuf, winBuf + 1, sizeof(float) * 15); winBuf[15] = sensor.value; }
  uint8_t span = cfg.winTime;
  if (winCount <= span || th.windowOpen) return;
  float past = winBuf[winCount - 1 - span];
  float drop = past - sensor.value;
  if (drop * 10.0f >= cfg.winDrop) {
    th.windowOpen = true;
    th.windowEndMs = millis() + (uint32_t)cfg.winPause * 60000UL;
    winCount = 0;
  }
}

// ----------------------------------------------------------- regulacja -----
static bool regulate() {
  uint32_t now = millis();
  float T = sensor.value;
  float target = th.target / 10.0f;

  if (sensor.fault) {
    th.source = SRC_FAULT;
    if (cfg.faultMode == FAULT_OFF) { th.duty = 0; return false; }
    // praca cykliczna: 20-minutowy cykl z zadanym wypełnieniem
    const uint32_t cyc = 20UL * 60000UL;
    if (now - cycleStartMs >= cyc) cycleStartMs = now;
    th.duty = cfg.faultDuty / 100.0f;
    return (now - cycleStartMs) < (uint32_t)(th.duty * cyc);
  }

  bool on = th.demand;
  bool canSwitchOff = (now - lastSwitchMs) >= (uint32_t)cfg.minOn * 60000UL;
  bool canSwitchOn  = (now - lastSwitchMs) >= (uint32_t)cfg.minOff * 60000UL;

  if (cfg.algo == ALGO_HYST) {
    float h = cfg.hyst / 20.0f;  // połowa histerezy
    if (on && T >= target + h && canSwitchOff) on = false;
    else if (!on && T <= target - h && canSwitchOn) on = true;
    th.duty = on ? 1 : 0;
  } else {
    // TPI: wypełnienie proporcjonalne do uchybu, liczone na początku cyklu
    uint32_t cyc = (uint32_t)cfg.tpiCycle * 60000UL;
    if (now - cycleStartMs >= cyc || cycleStartMs == 0) {
      cycleStartMs = now;
      float band = cfg.tpiBand / 10.0f;
      float d = 0.5f + (target - T) / band;
      d = constrain(d, 0.0f, 1.0f);
      uint32_t onMs = (uint32_t)(d * cyc);
      if (onMs < (uint32_t)cfg.minOn * 60000UL) onMs = 0;
      if (cyc - onMs < (uint32_t)cfg.minOff * 60000UL && onMs > 0) onMs = cyc;
      cycleOnMs = onMs;
      th.duty = (float)onMs / cyc;
    }
    on = (now - cycleStartMs) < cycleOnMs;
    // przegrzanie powyżej pasma – natychmiastowe wyłączenie
    if (T > target + cfg.tpiBand / 20.0f) on = false;
  }
  return on;
}

// -------------------------------------------------- ochrona zaworu --------
static void antiBlockCheck(const struct tm &t, bool tv) {
  uint32_t now = millis();
  if (th.antiBlock && (int32_t)(now - th.antiBlockEndMs) >= 0) th.antiBlock = false;
  if (!cfg.antiBlock || !tv || th.antiBlock) return;
  if (clockDow(t) == cfg.abDay && t.tm_hour == cfg.abHour && t.tm_min == 0 && lastAbYday != t.tm_yday) {
    lastAbYday = t.tm_yday;
    bool idle24h = !everOn || (now - lastOnMs) > 24UL * 3600000UL;
    if (idle24h) {
      th.antiBlock = true;
      th.antiBlockEndMs = now + (uint32_t)cfg.abMinutes * 60000UL;
    }
  }
}

// ---------------------------------------------------------- statystyki ----
static void statsTick(const struct tm &t, bool tv) {
  if (tv) {
    if (lastYday >= 0 && t.tm_yday != lastYday) {
      th.heatSecYesterday = th.heatSecToday;
      th.heatSecToday = 0;
      th.cyclesToday = 0;
      memmove(th.weekMinutes + 1, th.weekMinutes, sizeof(uint16_t) * 6);
      th.weekMinutes[0] = 0;
    }
    lastYday = t.tm_yday;
  }
  if (th.heating) {
    th.heatSecToday++;
    stats.heatSecondsTotal++;
    histHeatSecs++;
  }
  th.weekMinutes[0] = th.heatSecToday / 60;

  if (++histSecs >= HISTORY_PERIOD_S) {
    th.histTemp[th.histHead] = sensor.fault ? INT16_MIN : (int16_t)lroundf(sensor.value * 10);
    th.histTarget[th.histHead] = th.target;
    th.histHeat[th.histHead] = (uint8_t)(histHeatSecs * 100UL / histSecs);
    th.histHead = (th.histHead + 1) % HISTORY_LEN;
    if (th.histCount < HISTORY_LEN) th.histCount++;
    histSecs = histHeatSecs = 0;
  }
  if (++statsMinuteTicker >= 3600) {   // co godzinę zapis statystyk
    statsMinuteTicker = 0;
    stats.uptimeTotal += 60;
    statsSave();
  }
}

void thermoLoop() {
  uint32_t now = millis();
  if (now - lastTick < CONTROL_PERIOD_MS) return;
  lastTick = now;

  struct tm t;
  bool tv = clockLocal(t);

  computeTarget();
  windowDetect();
  if (th.windowOpen && th.source != SRC_WINDOW) computeTarget();
  th.demand = regulate();
  antiBlockCheck(t, tv);

  if (th.testOutput && (int32_t)(now - th.testEndMs) >= 0) th.testOutput = false;
  bool heat = th.demand || th.antiBlock || th.testOutput;

  if (heat != th.heating) {
    th.heating = heat;
    lastSwitchMs = now;
    if (heat) {
      stats.relayCycles++;
      th.cyclesToday++;
      everOn = true;
    }
  }
  if (heat) lastOnMs = now;
  applyOutput(heat);
  statsTick(t, tv);
}

// ---------------------------------------------------- akcje użytkownika ---
void thermoSetMode(uint8_t mode) {
  if (mode >= MODE_COUNT) return;
  cfg.mode = mode;
  th.override = false;
  if (mode != MODE_VACATION) cfg.vacUntil = 0;
  settingsSave();
  lastTick = 0;
}

bool thermoCanAdjust() { return cfg.mode != MODE_OFF && !sensor.fault; }

int16_t thermoAdjustable() {
  if (th.boost) return cfg.tBoost;
  switch (cfg.mode) {
    case MODE_MANUAL:   return cfg.tManual;
    case MODE_ECO:      return cfg.tEco;
    case MODE_VACATION: return cfg.tVacation;
    case MODE_OFF:      return cfg.tFrost;
    default:            return th.override ? th.overrideTemp : th.target;
  }
}

void thermoAdjust(int16_t delta) {
  if (!thermoCanAdjust()) return;
  auto clampT = [](int16_t v) { return (int16_t)constrain(v, 50, (int)cfg.tMax); };
  if (th.boost) { cfg.tBoost = clampT(cfg.tBoost + delta); settingsSave(); }
  else switch (cfg.mode) {
    case MODE_MANUAL:   cfg.tManual = clampT(cfg.tManual + delta); settingsSave(); break;
    case MODE_ECO:      cfg.tEco = clampT(cfg.tEco + delta); settingsSave(); break;
    case MODE_VACATION: cfg.tVacation = clampT(cfg.tVacation + delta); settingsSave(); break;
    default: {
      // AUTO: zmiana chwilowa do najbliższej zmiany programu
      int16_t base = th.override ? th.overrideTemp : th.target;
      th.overrideTemp = clampT(base + delta);
      th.override = true;
      th.overrideUntil = 0;
      struct tm t;
      if (clockLocal(t)) {
        uint8_t off; uint16_t at;
        if (scheduleNextChange(clockDow(t), t.tm_hour * 60 + t.tm_min, off, at)) {
          struct tm e = t;
          e.tm_mday += off;
          e.tm_hour = at / 60; e.tm_min = at % 60; e.tm_sec = 0;
          e.tm_isdst = -1;
          th.overrideUntil = mktime(&e);
        }
      }
    }
  }
  lastTick = 0;  // natychmiastowe przeliczenie
}

void thermoBoost(bool on) {
  th.boost = on;
  if (on) th.boostEndMs = millis() + (uint32_t)cfg.boostMin * 60000UL;
  lastTick = 0;
}

uint32_t thermoBoostLeftS() {
  if (!th.boost) return 0;
  int32_t left = (int32_t)(th.boostEndMs - millis());
  return left > 0 ? left / 1000 : 0;
}

void thermoStartVacation() {
  cfg.mode = MODE_VACATION;
  cfg.vacUntil = clockValid() ? (uint32_t)(clockNow() + (time_t)cfg.vacDays * 86400) : 0;
  th.override = false;
  settingsSave();
  lastTick = 0;
}

void thermoCancelWindow() {
  th.windowOpen = false;
  winCount = 0;
  lastTick = 0;
}

void thermoTestOutput(uint16_t seconds) {
  th.testOutput = seconds > 0;
  th.testEndMs = millis() + seconds * 1000UL;
  lastTick = 0;
}

// ========================================================== net.cpp
#include <WiFi.h>
#include <WebServer.h>

static WebServer server(80);
static NetState state = NET_OFF;
static bool serverStarted = false;
static uint32_t connectStart = 0;
static uint32_t lastRetry = 0;
static const char *AP_PASS = "termostat";

static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="pl"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Termostat</title><style>
body{font-family:system-ui,sans-serif;background:#0e1116;color:#e8ecf1;margin:0;padding:16px;max-width:420px}
.card{background:#1a1f27;border-radius:12px;padding:16px;margin-bottom:12px}
.big{font-size:48px;font-weight:600}.mut{color:#8a94a3;font-size:14px}
button,input,select{font-size:16px;padding:10px;border-radius:8px;border:1px solid #323b48;background:#252c37;color:#e8ecf1}
button{min-width:56px;cursor:pointer}.row{display:flex;gap:8px;align-items:center;margin:8px 0}
input{width:100%;box-sizing:border-box}.heat{color:#ff6b2c}</style></head><body>
<div class="card"><div class="mut">Temperatura</div><div class="big" id="t">--</div>
<div id="st" class="mut"></div></div>
<div class="card"><div class="mut">Zadana</div><div class="row"><button onclick="adj(-5)">−</button>
<div class="big" id="sp" style="font-size:32px;flex:1;text-align:center">--</div><button onclick="adj(5)">+</button></div>
<div class="row"><select id="mode" onchange="setMode(this.value)"><option value="0">AUTO</option><option value="1">Ręczny</option>
<option value="2">Eko</option><option value="3">Wyłączony</option><option value="4">Urlop</option></select>
<button onclick="post('/api/boost','')">Boost</button></div></div>
<div class="card"><div class="mut">Sieć Wi-Fi</div><form method="post" action="/wifi">
<div class="row"><input name="ssid" placeholder="Nazwa sieci (SSID)"></div>
<div class="row"><input name="pass" type="password" placeholder="Hasło"></div>
<button type="submit">Zapisz i połącz</button></form></div>
<script>
function post(u,b){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}).then(load)}
function adj(d){post('/api/adjust','d='+d)}function setMode(m){post('/api/mode','m='+m)}
function load(){fetch('/api/status').then(r=>r.json()).then(j=>{
t.textContent=j.fault?'BŁĄD':j.temp.toFixed(1)+' °C';sp.textContent=(j.target/10).toFixed(1)+' °C';
st.innerHTML=(j.heating?'<span class=heat>GRZANIE</span>':'czuwanie')+' · '+j.source;mode.value=j.mode})}
load();setInterval(load,5000);</script></body></html>)HTML";

static void sendStatus() {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"temp\":%.2f,\"fault\":%s,\"target\":%d,\"mode\":%u,\"heating\":%s,\"source\":\"%s\",\"boost\":%lu}",
           isnan(sensor.value) ? 0.0f : sensor.value, sensor.fault ? "true" : "false", th.target, cfg.mode,
           th.heating ? "true" : "false", thermoSourceName(th.source), (unsigned long)thermoBoostLeftS());
  server.send(200, "application/json", buf);
}

static void startServer() {
  if (serverStarted) return;
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", PAGE); });
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/adjust", HTTP_POST, [] { thermoAdjust(server.arg("d").toInt()); sendStatus(); });
  server.on("/api/mode", HTTP_POST, [] {
    int m = server.arg("m").toInt();
    if (m == MODE_VACATION) thermoStartVacation(); else thermoSetMode(m);
    sendStatus();
  });
  server.on("/api/boost", HTTP_POST, [] { thermoBoost(!th.boost); sendStatus(); });
  server.on("/wifi", HTTP_POST, [] {
    strlcpy(cfg.ssid, server.arg("ssid").c_str(), sizeof(cfg.ssid));
    strlcpy(cfg.pass, server.arg("pass").c_str(), sizeof(cfg.pass));
    cfg.wifiEnable = 1;
    settingsSaveNow();
    server.send(200, "text/html; charset=utf-8",
                "<meta charset=utf-8><body style='font-family:sans-serif'>Zapisano. Termostat łączy się z siecią…</body>");
    delay(500);
    netReconnect();
  });
  server.begin();
  serverStarted = true;
}

String netApName() {
  uint64_t mac = ESP.getEfuseMac();
  char b[24];
  snprintf(b, sizeof(b), "Termostat-%04X", (uint16_t)(mac >> 32));
  return String(b);
}
const char *netApPass() { return AP_PASS; }

void netReconnect() {
  if (!cfg.wifiEnable || cfg.ssid[0] == 0) { netStop(); return; }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("termostat");
  WiFi.begin(cfg.ssid, cfg.pass);
  state = NET_CONNECTING;
  connectStart = millis();
}

void netBegin() {
  if (cfg.ntp) clockStartNtp();
  netReconnect();
}

void netStartAP() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(netApName().c_str(), AP_PASS);
  state = NET_AP;
  startServer();
}

void netStop() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  state = NET_OFF;
}

void netLoop() {
  if (serverStarted) server.handleClient();
  switch (state) {
    case NET_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        state = NET_CONNECTED;
        if (cfg.ntp) clockStartNtp();
        startServer();
      } else if (millis() - connectStart > 20000) {
        WiFi.disconnect();
        state = NET_OFF;
        lastRetry = millis();
      }
      break;
    case NET_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) { state = NET_OFF; lastRetry = millis(); }
      break;
    case NET_OFF:
      // ponowna próba co 5 min
      if (cfg.wifiEnable && cfg.ssid[0] && millis() - lastRetry > 300000UL) netReconnect();
      break;
    default: break;
  }
}

NetState netState() { return state; }
String netIP() {
  if (state == NET_AP) return WiFi.softAPIP().toString();
  if (state == NET_CONNECTED) return WiFi.localIP().toString();
  return "-";
}
int netRssi() { return state == NET_CONNECTED ? WiFi.RSSI() : 0; }

// ========================================================== params.cpp
// --------------------------------------------------------- konstruktory ---
static Param NUM(const char *name, int mn, int mx, int st, uint8_t dec, const char *unit,
                 int (*g)(), void (*s)(int), const char *help, const char *zero = nullptr) {
  Param p{};
  p.name = name; p.type = P_NUM; p.min = mn; p.max = mx; p.step = st; p.dec = dec; p.unit = unit;
  p.get = g; p.set = s; p.help = help; p.zeroText = zero;
  return p;
}
static Param ENUM(const char *name, const char *const *opts, uint8_t n, int (*g)(), void (*s)(int),
                  const char *help) {
  Param p{};
  p.name = name; p.type = P_ENUM; p.min = 0; p.max = n - 1; p.step = 1; p.opts = opts; p.nopts = n;
  p.get = g; p.set = s; p.help = help;
  return p;
}
static Param BOOL(const char *name, int (*g)(), void (*s)(int), const char *help) {
  Param p{};
  p.name = name; p.type = P_BOOL; p.min = 0; p.max = 1; p.step = 1; p.get = g; p.set = s; p.help = help;
  return p;
}
static Param INFO(const char *name, void (*f)(char *, size_t)) {
  Param p{};
  p.name = name; p.type = P_INFO; p.info = f;
  return p;
}
static Param ACTION(const char *name, void (*a)(), bool confirm, const char *help) {
  Param p{};
  p.name = name; p.type = P_ACTION; p.action = a; p.confirm = confirm; p.help = help;
  return p;
}

#define GS(field) [] { return (int)cfg.field; }, [](int v) { cfg.field = v; }
#define COUNT(a) (uint8_t)(sizeof(a) / sizeof(a[0]))

static const char *const O_ALGO[] = {"Histereza", "TPI (PWM)"};
static const char *const O_LOGIC[] = {"Ujemna (LOW)", "Dodatnia (HIGH)"};
static const char *const O_VALVE[] = {"NC (bez napięcia zamkn.)", "NO (bez napięcia otw.)"};
static const char *const O_DAYS[] = {"Poniedziałek", "Wtorek", "Środa", "Czwartek", "Piątek", "Sobota", "Niedziela"};
static const char *const O_FAULT[] = {"Wyłącz grzanie", "Praca cykliczna"};

// ------------------------------------------------------- Temperatury -----
static const Param P_TEMP[] = {
  NUM("Komfort", 50, 300, 1, 1, "°C", GS(tComfort), "Temperatura w okresach komfortu programu tygodniowego."),
  NUM("Eko (obniżona)", 50, 300, 1, 1, "°C", GS(tEco), "Temperatura poza okresami komfortu oraz w trybie EKO."),
  NUM("Tryb ręczny", 50, 300, 1, 1, "°C", GS(tManual), "Stała temperatura w trybie RĘCZNYM."),
  NUM("Urlop", 50, 250, 1, 1, "°C", GS(tVacation), "Temperatura utrzymywana w czasie urlopu."),
  NUM("Ochrona przed mrozem", 30, 120, 1, 1, "°C", GS(tFrost),
      "Minimalna temperatura utrzymywana zawsze – także w trybie WYŁĄCZONY i przy otwartym oknie."),
  NUM("Boost", 150, 300, 1, 1, "°C", GS(tBoost), "Temperatura w czasie szybkiego dogrzewania (BOOST)."),
  NUM("Czas boost", 15, 240, 15, 0, "min", GS(boostMin), "Jak długo trwa BOOST po włączeniu."),
  NUM("Limit maksymalny", 200, 350, 5, 1, "°C", GS(tMax), "Żadna zadana nie przekroczy tej wartości."),
};

// ---------------------------------------------------------- Regulacja ----
static const Param P_REG[] = {
  ENUM("Algorytm", O_ALGO, 2, GS(algo),
       "Histereza: zał./wył. wokół zadanej. TPI: proporcjonalne wypełnienie cyklu – stabilniejsza temperatura."),
  NUM("Histereza", 1, 20, 1, 1, "°C", GS(hyst),
      "Grzanie włącza się przy T < zadana − H/2, wyłącza przy T > zadana + H/2."),
  NUM("Cykl TPI", 10, 30, 5, 0, "min", GS(tpiCycle), "Długość cyklu modulacji TPI."),
  NUM("Pasmo TPI", 5, 40, 1, 1, "°C", GS(tpiBand),
      "Uchyb, przy którym wypełnienie zmienia się od 0 do 100%. Mniejsze = szybsza reakcja."),
  NUM("Min. czas załączenia", 0, 15, 1, 0, "min", GS(minOn),
      "Chroni siłownik i stycznik. Głowica termoelektryczna otwiera się ok. 3 min."),
  NUM("Min. czas wyłączenia", 0, 15, 1, 0, "min", GS(minOff), "Minimalna przerwa między załączeniami."),
};

// -------------------------------------------------------- Wyjście / zawór -
static const Param P_OUT[] = {
  ENUM("Logika wyjścia", O_LOGIC, 2, [] { return cfg.outActiveLow ? 0 : 1; },
       [](int v) { cfg.outActiveLow = v == 0; },
       "Ujemna: stan NISKI na GPIO3 załącza przekaźnik/stycznik (moduły aktywne stanem niskim)."),
  ENUM("Typ siłownika", O_VALVE, 2, GS(valveNO),
       "NC: zasilenie głowicy otwiera zawór (najczęstsze). NO: zasilenie zamyka zawór."),
  BOOL("Ochrona zaworu", GS(antiBlock),
       "Raz w tygodniu otwiera zawór, jeśli nie grzał przez 24 h – zapobiega zastaniu poza sezonem."),
  ENUM("Dzień ochrony", O_DAYS, 7, GS(abDay), "Dzień tygodnia przewietrzenia zaworu."),
  NUM("Godzina ochrony", 0, 23, 1, 0, ":00", GS(abHour), "Godzina przewietrzenia zaworu."),
  NUM("Czas ochrony", 1, 15, 1, 0, "min", GS(abMinutes), "Jak długo zawór pozostaje otwarty."),
};

// ------------------------------------------------------------- Czujnik ---
static void infoRaw(char *b, size_t n) {
  if (isnan(sensor.raw)) snprintf(b, n, "—");
  else snprintf(b, n, "%.2f °C", sensor.raw);
}
static void infoRom(char *b, size_t n) {
  if (!sensor.present) { snprintf(b, n, "nie wykryto"); return; }
  snprintf(b, n, "%02X%02X%02X%02X%02X%02X", sensor.rom[1], sensor.rom[2], sensor.rom[3], sensor.rom[4],
           sensor.rom[5], sensor.rom[6]);
}
static const Param P_SENS[] = {
  NUM("Korekta odczytu", -50, 50, 1, 1, "°C", GS(offset),
      "Dodawana do odczytu. Porównaj z termometrem wzorcowym na wysokości 1,5 m."),
  NUM("Wygładzanie", 0, 5, 1, 0, "", GS(filter), "Filtr odczytu: 0 = brak, 5 = mocne (wolniejsza reakcja).", "Brak"),
  ENUM("Przy awarii czujnika", O_FAULT, 2, GS(faultMode),
       "Praca cykliczna grzeje przez zadany % czasu – chroni przed wychłodzeniem przy uszkodzonym czujniku."),
  NUM("Wypełnienie awaryjne", 10, 50, 5, 0, "%", GS(faultDuty), "Udział czasu grzania w cyklu 20 min przy awarii."),
  INFO("Odczyt surowy", infoRaw),
  INFO("Adres czujnika", infoRom),
};

// -------------------------------------------------------- Okno otwarte ---
static const Param P_WIN[] = {
  BOOL("Wykrywanie okna", GS(winEnable),
       "Szybki spadek temperatury wstrzymuje grzanie (utrzymywana jest tylko ochrona przed mrozem)."),
  NUM("Spadek temperatury", 5, 30, 1, 1, "°C", GS(winDrop), "O ile musi spaść temperatura..."),
  NUM("W czasie", 2, 15, 1, 0, "min", GS(winTime), "...w ciągu ilu minut."),
  NUM("Przerwa w grzaniu", 5, 60, 5, 0, "min", GS(winPause), "Czas wstrzymania grzania po wykryciu."),
};

// ---------------------------------------------------------------- Ekran --
static const Param P_DISP[] = {
  NUM("Jasność", 10, 100, 5, 0, "%", GS(brightness), "Jasność podświetlenia podczas obsługi."),
  NUM("Jasność wygaszona", 0, 50, 5, 0, "%", GS(dimBrightness), "Jasność po bezczynności. 0 = ekran wygaszony."),
  NUM("Wygaszanie po", 0, 600, 10, 0, "s", [] { return (int)cfg.dimAfter; }, [](int v) { cfg.dimAfter = v; },
      "Czas bezczynności do przyciemnienia ekranu.", "Nigdy"),
  BOOL("Blokada rodzicielska", GS(childLock),
       "Po wygaszeniu ekran blokuje się. Odblokowanie: przytrzymaj palec 3 s."),
  ACTION("Kalibracja dotyku", [] { uiRequestCalibrate(); }, false, "Dotknij kolejno strzałek w rogach ekranu."),
};

// ---------------------------------------------------------- Data i czas --
static struct tm editTime() {
  struct tm t;
  if (!clockLocal(t)) { t = {}; t.tm_year = 126; t.tm_mon = 0; t.tm_mday = 1; t.tm_hour = 12; }
  return t;
}
static void setPart(int part, int v) {
  struct tm t = editTime();
  int y = t.tm_year + 1900, mo = t.tm_mon + 1, d = t.tm_mday, h = t.tm_hour, mi = t.tm_min;
  switch (part) { case 0: h = v; break; case 1: mi = v; break; case 2: d = v; break; case 3: mo = v; break; case 4: y = v; break; }
  clockSetLocal(y, mo, d, h, mi);
}
static const Param P_CLOCK[] = {
  BOOL("Synchronizacja NTP", GS(ntp), "Automatyczny czas z internetu (wymaga Wi-Fi). Strefa: Polska, z czasem letnim."),
  NUM("Godzina", 0, 23, 1, 0, "", [] { return editTime().tm_hour; }, [](int v) { setPart(0, v); }, "Ustaw godzinę."),
  NUM("Minuta", 0, 59, 1, 0, "", [] { return editTime().tm_min; }, [](int v) { setPart(1, v); }, "Ustaw minutę."),
  NUM("Dzień", 1, 31, 1, 0, "", [] { return editTime().tm_mday; }, [](int v) { setPart(2, v); }, "Dzień miesiąca."),
  NUM("Miesiąc", 1, 12, 1, 0, "", [] { return editTime().tm_mon + 1; }, [](int v) { setPart(3, v); }, "Miesiąc."),
  NUM("Rok", 2024, 2099, 1, 0, "", [] { return editTime().tm_year + 1900; }, [](int v) { setPart(4, v); }, "Rok."),
};

// --------------------------------------------------------------- Wi-Fi ---
static void infoNet(char *b, size_t n) {
  switch (netState()) {
    case NET_CONNECTED:  snprintf(b, n, "Połączono (%d dBm)", netRssi()); break;
    case NET_CONNECTING: snprintf(b, n, "Łączenie…"); break;
    case NET_AP:         snprintf(b, n, "Punkt dostępowy"); break;
    default:             snprintf(b, n, cfg.wifiEnable ? "Brak połączenia" : "Wyłączone");
  }
}
static const Param P_WIFI[] = {
  BOOL("Wi-Fi", [] { return (int)cfg.wifiEnable; }, [](int v) { cfg.wifiEnable = v; netReconnect(); },
       "Wi-Fi daje czas z NTP i panel WWW pod adresem IP termostatu."),
  INFO("Stan", infoNet),
  INFO("Sieć", [](char *b, size_t n) { snprintf(b, n, "%s", cfg.ssid[0] ? cfg.ssid : "—"); }),
  INFO("Adres IP", [](char *b, size_t n) { snprintf(b, n, "%s", netIP().c_str()); }),
  ACTION("Konfiguruj przez telefon", [] { uiOpenApScreen(); }, false, "Uruchamia punkt dostępowy z formularzem."),
  ACTION("Zapomnij sieć", [] { cfg.ssid[0] = 0; cfg.pass[0] = 0; settingsSaveNow(); netStop(); uiToast("Usunięto dane sieci"); },
         true, "Usuwa zapisaną nazwę i hasło sieci."),
};

// ---------------------------------------------------------- Informacje ---
static void fmtDuration(char *b, size_t n, uint32_t s) {
  uint32_t d = s / 86400, h = (s / 3600) % 24, m = (s / 60) % 60;
  if (d) snprintf(b, n, "%lu d %lu h %lu min", (unsigned long)d, (unsigned long)h, (unsigned long)m);
  else snprintf(b, n, "%lu h %lu min", (unsigned long)h, (unsigned long)m);
}
static const Param P_INFO_[] = {
  INFO("Wersja", [](char *b, size_t n) { snprintf(b, n, "%s", FW_VERSION); }),
  INFO("Czas pracy", [](char *b, size_t n) { fmtDuration(b, n, millis() / 1000); }),
  INFO("Grzanie łącznie", [](char *b, size_t n) { snprintf(b, n, "%lu h", (unsigned long)(stats.heatSecondsTotal / 3600)); }),
  INFO("Załączeń wyjścia", [](char *b, size_t n) { snprintf(b, n, "%lu", (unsigned long)stats.relayCycles); }),
  INFO("Stan GPIO3", [](char *b, size_t n) {
    snprintf(b, n, "%s (%s)", th.outputLevel ? "HIGH" : "LOW", th.heating ? "grzeje" : "wył.");
  }),
  INFO("Błędy czujnika", [](char *b, size_t n) { snprintf(b, n, "%u", sensor.errors); }),
  INFO("Wolna pamięć", [](char *b, size_t n) { snprintf(b, n, "%lu kB", (unsigned long)(ESP.getFreeHeap() / 1024)); }),
};

// --------------------------------------------------------------- Serwis --
static const Param P_SERV[] = {
  ACTION("Test wyjścia (30 s)", [] { thermoTestOutput(30); uiToast("Wyjście załączone na 30 s"); }, false,
         "Załącza stycznik/głowicę na 30 s."),
  ACTION("Kalibracja dotyku", [] { uiRequestCalibrate(); }, false, ""),
  ACTION("Zeruj statystyki", [] {
    statsReset();
    th.heatSecToday = th.heatSecYesterday = 0; th.cyclesToday = 0;
    memset(th.weekMinutes, 0, sizeof(th.weekMinutes));
    uiToast("Statystyki wyzerowane");
  }, true, "Zeruje liczniki czasu grzania i załączeń."),
  ACTION("Ustawienia fabryczne", [] { settingsFactoryReset(); uiToast("Przywrócono ustawienia"); }, true,
         "Przywraca wszystkie ustawienia i program tygodniowy (kalibracja dotyku zostaje)."),
  ACTION("Uruchom ponownie", [] { settingsSaveNow(); statsSave(); delay(200); ESP.restart(); }, true, ""),
};

const Category CATEGORIES[] = {
  {"Temperatury", P_TEMP, COUNT(P_TEMP)},
  {"Regulacja", P_REG, COUNT(P_REG)},
  {"Wyjście i zawór", P_OUT, COUNT(P_OUT)},
  {"Czujnik", P_SENS, COUNT(P_SENS)},
  {"Okno otwarte", P_WIN, COUNT(P_WIN)},
  {"Ekran", P_DISP, COUNT(P_DISP)},
  {"Data i czas", P_CLOCK, COUNT(P_CLOCK)},
  {"Wi-Fi", P_WIFI, COUNT(P_WIFI)},
  {"Informacje", P_INFO_, COUNT(P_INFO_)},
  {"Serwis", P_SERV, COUNT(P_SERV)},
};
const uint8_t CATEGORY_COUNT = COUNT(CATEGORIES);

void paramFormat(const Param &p, int v, char *buf, size_t n) {
  switch (p.type) {
    case P_BOOL: snprintf(buf, n, "%s", v ? "Wł." : "Wył."); return;
    case P_ENUM: snprintf(buf, n, "%s", (v >= 0 && v < p.nopts) ? p.opts[v] : "?"); return;
    case P_INFO: if (p.info) p.info(buf, n); else buf[0] = 0; return;
    case P_ACTION: buf[0] = 0; return;
    default: break;
  }
  if (v == 0 && p.zeroText) { snprintf(buf, n, "%s", p.zeroText); return; }
  char num[16];
  if (p.dec == 1) {
    int a = abs(v);
    snprintf(num, sizeof(num), "%s%d,%d", v < 0 ? "−" : (p.min < 0 && v > 0 ? "+" : ""), a / 10, a % 10);
  } else {
    snprintf(num, sizeof(num), "%d", v);
  }
  if (p.unit && p.unit[0]) {
    if (p.unit[0] == ':') snprintf(buf, n, "%s%s", num, p.unit);
    else snprintf(buf, n, "%s %s", num, p.unit);
  } else {
    snprintf(buf, n, "%s", num);
  }
}

// ========================================================== ui.cpp
// ============================================================================
//  Interfejs graficzny termostatu – 480x320, dotyk XPT2046.
//  Układ ekranów jest 1:1 z symulacją w docs/symulacja.html.
// ============================================================================

LGFX lcd;

// ------------------------------------------------------------- kolory ----
static constexpr uint32_t C_BG      = 0x0E1116;
static constexpr uint32_t C_PANEL   = 0x1A1F27;
static constexpr uint32_t C_PANEL2  = 0x262D38;
static constexpr uint32_t C_LINE    = 0x353E4C;
static constexpr uint32_t C_TEXT    = 0xE8ECF1;
static constexpr uint32_t C_MUTED   = 0x8A94A3;
static constexpr uint32_t C_HEAT    = 0xFF6B2C;
static constexpr uint32_t C_COMFORT = 0xF5A524;
static constexpr uint32_t C_ECO     = 0x4C9EEB;
static constexpr uint32_t C_ECO_DK  = 0x1D3550;
static constexpr uint32_t C_FROST   = 0x5ED6E6;
static constexpr uint32_t C_VAC     = 0x2EC4B6;
static constexpr uint32_t C_BOOST   = 0xFF4FA3;
static constexpr uint32_t C_OK      = 0x3DD68C;
static constexpr uint32_t C_ALARM   = 0xF2545B;

// -------------------------------------------------------------- czcionki --

static const uint8_t ASC[] = {14, 18, 25, 38, 78};
static lgfx::PointerWrapper fontData[5];
static lgfx::VLWfont vfont[5];

static void fontsInit() {
  const uint8_t *src[5] = {FontS, FontM, FontB, FontXL, FontBig};
  const size_t len[5] = {sizeof(FontS), sizeof(FontM), sizeof(FontB), sizeof(FontXL), sizeof(FontBig)};
  for (int i = 0; i < 5; i++) {
    fontData[i].set(src[i], len[i]);
    vfont[i].loadFont(&fontData[i]);
  }
}



static int textW(const char *s, FontId f) {
  lcd.setFont(&vfont[f]);
  return lcd.textWidth(s);
}

// y = górna krawędź tekstu
static int text(int x, int y, const char *s, FontId f, uint32_t fg, uint32_t bg, Align a = AL_L) {
  lcd.setFont(&vfont[f]);
  lcd.setTextColor(fg, bg);
  lcd.setTextDatum(textdatum_t::top_left);
  int w = lcd.textWidth(s);
  int dx = a == AL_C ? x - w / 2 : a == AL_R ? x - w : x;
  lcd.drawString(s, dx, y);
  return w;
}
// cy = środek wysokości wielkich liter
static int textMid(int x, int cy, const char *s, FontId f, uint32_t fg, uint32_t bg, Align a = AL_L) {
  return text(x, cy - ASC[f] / 2, s, f, fg, bg, a);
}

// ------------------------------------------------------------- ikony -----
static void iconFlame(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillCircle(cx, cy + 4, 7, c);
  lcd.fillTriangle(cx - 7, cy + 3, cx + 7, cy + 3, cx + 1, cy - 11, c);
  lcd.fillCircle(cx, cy + 6, 3, bg);
}
static void iconSnow(int cx, int cy, uint32_t c) {
  for (int i = 0; i < 3; i++) {
    float a = i * PI / 3;
    float dx = cos(a) * 9, dy = sin(a) * 9;
    lcd.drawWideLine(cx - dx, cy - dy, cx + dx, cy + dy, 1.2f, c);
  }
}
static void iconWifi(int cx, int cy, int level, uint32_t on, uint32_t off) {
  lcd.fillCircle(cx, cy + 6, 2, level > 0 ? on : off);
  lcd.fillArc(cx, cy + 6, 7, 9, 225, 315, level > 0 ? on : off);
  lcd.fillArc(cx, cy + 6, 12, 14, 225, 315, level > 1 ? on : off);
}
static void iconLock(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillArc(cx, cy - 2, 4, 6, 180, 360, c);
  lcd.fillRect(cx - 6, cy - 2, 2, 2, c);
  lcd.fillRect(cx + 4, cy - 2, 2, 2, c);
  lcd.fillRoundRect(cx - 8, cy, 16, 11, 2, c);
  lcd.fillCircle(cx, cy + 5, 2, bg);
}
static void iconWarn(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillTriangle(cx, cy - 10, cx - 10, cy + 8, cx + 10, cy + 8, c);
  lcd.fillRect(cx - 1, cy - 4, 3, 7, bg);
  lcd.fillRect(cx - 1, cy + 4, 3, 2, bg);
}
static void iconCalendar(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillRoundRect(cx - 10, cy - 8, 20, 18, 3, c);
  lcd.fillRect(cx - 8, cy - 3, 16, 11, bg);
  lcd.fillRect(cx - 6, cy - 12, 3, 6, c);
  lcd.fillRect(cx + 3, cy - 12, 3, 6, c);
  lcd.fillRect(cx - 5, cy, 4, 3, c);
  lcd.fillRect(cx + 1, cy + 3, 4, 3, c);
}
static void iconChart(int cx, int cy, uint32_t c) {
  lcd.fillRect(cx - 10, cy + 2, 5, 8, c);
  lcd.fillRect(cx - 2, cy - 6, 5, 16, c);
  lcd.fillRect(cx + 6, cy - 2, 5, 12, c);
}
static void iconMenu(int cx, int cy, uint32_t c) {
  for (int i = -1; i <= 1; i++) lcd.fillRoundRect(cx - 10, cy + i * 7 - 1, 20, 3, 1, c);
}
static void iconMode(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillCircle(cx, cy, 10, c);
  lcd.fillCircle(cx, cy, 7, bg);
  lcd.fillArc(cx, cy, 0, 7, 270, 90, c);
}
static void iconMoon(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillCircle(cx, cy, 10, c);
  lcd.fillCircle(cx + 6, cy - 4, 9, bg);
}
static void iconThermo(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillRoundRect(cx - 4, cy - 12, 8, 18, 4, c);
  lcd.fillCircle(cx, cy + 7, 6, c);
  lcd.fillRect(cx - 1, cy - 9, 2, 12, bg);
}
static void iconCase(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillRoundRect(cx - 11, cy - 6, 22, 16, 3, c);
  lcd.drawRoundRect(cx - 5, cy - 11, 10, 7, 2, c);
  lcd.fillRect(cx - 11, cy, 22, 2, bg);
}
static void iconWindow(int cx, int cy, uint32_t c) {
  lcd.drawRect(cx - 9, cy - 9, 18, 18, c);
  lcd.drawRect(cx - 8, cy - 8, 16, 16, c);
  lcd.drawFastVLine(cx, cy - 8, 16, c);
  lcd.drawFastHLine(cx - 8, cy, 16, c);
}
static void iconPlus(int cx, int cy, uint32_t c) {
  lcd.fillRect(cx - 12, cy - 2, 24, 4, c);
  lcd.fillRect(cx - 2, cy - 12, 4, 24, c);
}
static void iconMinus(int cx, int cy, uint32_t c) { lcd.fillRect(cx - 12, cy - 2, 24, 4, c); }
static void iconChevron(int cx, int cy, char dir, uint32_t c) {
  // dir: 'l','r','u','d'
  int s = 6;
  if (dir == 'l') { lcd.drawWideLine(cx + s / 2, cy - s, cx - s / 2, cy, 1.5f, c); lcd.drawWideLine(cx - s / 2, cy, cx + s / 2, cy + s, 1.5f, c); }
  if (dir == 'r') { lcd.drawWideLine(cx - s / 2, cy - s, cx + s / 2, cy, 1.5f, c); lcd.drawWideLine(cx + s / 2, cy, cx - s / 2, cy + s, 1.5f, c); }
  if (dir == 'u') { lcd.drawWideLine(cx - s, cy + s / 2, cx, cy - s / 2, 1.5f, c); lcd.drawWideLine(cx, cy - s / 2, cx + s, cy + s / 2, 1.5f, c); }
  if (dir == 'd') { lcd.drawWideLine(cx - s, cy - s / 2, cx, cy + s / 2, 1.5f, c); lcd.drawWideLine(cx, cy + s / 2, cx + s, cy - s / 2, 1.5f, c); }
}
static void iconCheck(int cx, int cy, uint32_t c) {
  lcd.drawWideLine(cx - 6, cy, cx - 2, cy + 4, 1.5f, c);
  lcd.drawWideLine(cx - 2, cy + 4, cx + 6, cy - 5, 1.5f, c);
}

// --------------------------------------------------------------- przyciski


static void button(const Rect &r, const char *label, uint32_t bg, uint32_t fg, FontId f = F_M) {
  lcd.fillRoundRect(r.x, r.y, r.w, r.h, 10, bg);
  if (label) textMid(r.x + r.w / 2, r.y + r.h / 2, label, f, fg, bg, AL_C);
}

// ------------------------------------------------------------ stan UI -----


static Screen scr = SCR_HOME;
static bool needFull = true;
static uint32_t lastInputMs = 0;
static bool dimmed = false;
static bool locked = false;
static bool touchDown = false;
static uint32_t touchStartMs = 0, lastRepeatMs = 0;
static int16_t tx = 0, ty = 0;
static bool wakeTouch = false;      // dotyk wybudzający – ignoruj do puszczenia
static bool calibrateReq = false;

static char toastMsg[48];
static uint32_t toastUntil = 0;

// kontekst ekranów
static uint8_t progDay = 0;         // 0 = poniedziałek
static int8_t periodIdx = -1;       // -1 = nowy
static uint16_t perStart = 0, perEnd = 0;
static uint8_t vacDaysEdit = 7;
static int16_t vacTempEdit = 150;
static uint8_t catIdx = 0, listPage = 0;
static uint8_t paramIdx = 0;
static int editVal = 0;
static const Param *confirmParam = nullptr;
static Screen confirmReturn = SCR_LIST;

static const char *const DAY_SHORT[] = {"Pn", "Wt", "Śr", "Cz", "Pt", "So", "Nd"};
static const char *const DAY_LONG[] = {"Poniedziałek", "Wtorek", "Środa", "Czwartek", "Piątek", "Sobota", "Niedziela"};
static const char *const DAY_ABBR[] = {"pon", "wt", "śr", "czw", "pt", "sob", "nd"};
static const char *const MONTH_ABBR[] = {"sty", "lut", "mar", "kwi", "maj", "cze", "lip", "sie", "wrz", "paź", "lis", "gru"};

static void go(Screen s) { scr = s; needFull = true; }

void uiToast(const char *msg) {
  strlcpy(toastMsg, msg, sizeof(toastMsg));
  toastUntil = millis() + 2000;
  needFull = true;
}
void uiOpenApScreen() { netStartAP(); go(SCR_AP); }
void uiRequestCalibrate() { calibrateReq = true; }

static void fmtTemp(char *b, size_t n, int16_t t10, bool deg = true) {
  int a = abs(t10);
  snprintf(b, n, "%s%d,%d%s", t10 < 0 ? "-" : "", a / 10, a % 10, deg ? "°" : "");
}
static void fmtHM(char *b, size_t n, uint16_t minutes) { snprintf(b, n, "%02u:%02u", minutes / 60, minutes % 60); }

static uint32_t modeColor(uint8_t src) {
  switch (src) {
    case SRC_COMFORT: case SRC_OVERRIDE: case SRC_MANUAL: return C_COMFORT;
    case SRC_ECO: return C_ECO;
    case SRC_FROST: case SRC_WINDOW: return C_FROST;
    case SRC_VACATION: return C_VAC;
    case SRC_BOOST: return C_BOOST;
    case SRC_FAULT: return C_ALARM;
  }
  return C_TEXT;
}

static void header(const char *title, bool back = true) {
  lcd.fillRect(0, 0, SCREEN_W, 48, C_PANEL);
  if (back) iconChevron(26, 24, 'l', C_TEXT);
  text(back ? 56 : 16, 24 - ASC[F_B] / 2, title, F_B, C_TEXT, C_PANEL);
}
static const Rect R_BACK = {0, 0, 120, 48};

// ========================================================== EKRAN GŁÓWNY ===
static const Rect R_PLUS = {324, 56, 132, 56};
static const Rect R_MINUS = {324, 186, 132, 56};
static const Rect R_CHIP = {16, 174, 290, 38};
static const Rect R_NAV[4] = {{16, 262, 108, 50}, {132, 262, 108, 50}, {248, 262, 108, 50}, {364, 262, 108, 50}};



static void homeModeLine(char *name, size_t nn, char *rest, size_t nr) {
  struct tm t;
  bool tv = clockLocal(t);
  rest[0] = 0;
  if (th.boost && cfg.mode != MODE_OFF) {
    snprintf(name, nn, "BOOST");
    snprintf(rest, nr, " · jeszcze %lu min", (unsigned long)(thermoBoostLeftS() + 59) / 60);
    return;
  }
  switch (cfg.mode) {
    case MODE_MANUAL: snprintf(name, nn, "RĘCZNY"); snprintf(rest, nr, " · stała temperatura"); break;
    case MODE_ECO: snprintf(name, nn, "EKO"); snprintf(rest, nr, " · obniżona"); break;
    case MODE_OFF: {
      char f[12]; fmtTemp(f, sizeof(f), cfg.tFrost);
      snprintf(name, nn, "WYŁĄCZONY"); snprintf(rest, nr, " · ochrona %s", f); break;
    }
    case MODE_VACATION: {
      snprintf(name, nn, "URLOP");
      if (cfg.vacUntil) {
        time_t u = cfg.vacUntil; struct tm e; localtime_r(&u, &e);
        snprintf(rest, nr, " · do %d %s, %02d:%02d", e.tm_mday, MONTH_ABBR[e.tm_mon], e.tm_hour, e.tm_min);
      }
      break;
    }
    default: {
      snprintf(name, nn, "AUTO");
      if (!tv) { snprintf(rest, nr, " · ustaw zegar!"); break; }
      uint8_t off; uint16_t at;
      const char *what = th.override ? "zmiana chwilowa" : (th.source == SRC_COMFORT ? "komfort" : "eko");
      if (scheduleNextChange(clockDow(t), t.tm_hour * 60 + t.tm_min, off, at)) {
        char hm[8]; fmtHM(hm, sizeof(hm), at);
        snprintf(rest, nr, " · %s do %s%s", what, off == 0 ? "" : off == 1 ? "jutra " : "", hm);
      } else {
        snprintf(rest, nr, " · %s", what);
      }
    }
  }
}

static void homeChip(char *b, size_t n, uint8_t &kind) {
  // kind: 0 czuwanie, 1 grzanie, 2 okno, 3 awaria, 4 ochrona zaworu, 5 test
  if (sensor.fault) {
    kind = 3;
    if (cfg.faultMode == FAULT_DUTY) snprintf(b, n, "Awaria czujnika · praca %u%%", cfg.faultDuty);
    else snprintf(b, n, "Awaria czujnika · grzanie wył.");
  } else if (th.windowOpen) {
    kind = 2;
    snprintf(b, n, "Okno otwarte · %lu min", (unsigned long)((th.windowEndMs - millis()) / 60000 + 1));
  } else if (th.testOutput) { kind = 5; snprintf(b, n, "Test wyjścia"); }
  else if (th.antiBlock) { kind = 4; snprintf(b, n, "Ochrona zaworu"); }
  else if (th.heating) {
    kind = 1;
    if (cfg.algo == ALGO_TPI) snprintf(b, n, "Grzanie · %d%%", (int)lroundf(th.duty * 100));
    else snprintf(b, n, "Grzanie");
  } else { kind = 0; snprintf(b, n, "Czuwanie"); }
}

static void drawHome(bool full) {
  struct tm t;
  bool tv = clockLocal(t);
  char buf[64];

  if (full) {
    lcd.fillScreen(C_BG);
    memset(&hc, 0, sizeof(hc));
    hc.icons = 0xFF;
    text(24, 52, "W pomieszczeniu", F_S, C_MUTED, C_BG);
    lcd.fillRoundRect(316, 48, 148, 202, 14, C_PANEL);
    textMid(390, 128, "Zadana", F_S, C_MUTED, C_PANEL, AL_C);
    const char *labels[4] = {"Tryb", "Program", "Wykres", "Menu"};
    for (int i = 0; i < 4; i++) {
      const Rect &r = R_NAV[i];
      lcd.fillRoundRect(r.x, r.y, r.w, r.h, 10, C_PANEL);
      int cx = r.x + 26, cy = r.y + r.h / 2;
      if (i == 0) iconMode(cx, cy, C_TEXT, C_PANEL);
      if (i == 1) iconCalendar(cx, cy, C_TEXT, C_PANEL);
      if (i == 2) iconChart(cx, cy, C_TEXT);
      if (i == 3) iconMenu(cx, cy, C_TEXT);
      textMid(r.x + 44, cy, labels[i], F_M, C_TEXT, C_PANEL);
    }
  }

  // --- zegar
  if (tv) snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min); else strcpy(buf, "--:--");
  if (strcmp(buf, hc.time)) {
    strcpy(hc.time, buf);
    lcd.fillRect(16, 6, 70, 30, C_BG);
    text(16, 8, buf, F_B, tv ? C_TEXT : C_COMFORT, C_BG);
  }
  if (tv) snprintf(buf, sizeof(buf), "%s, %d %s", DAY_ABBR[clockDow(t)], t.tm_mday, MONTH_ABBR[t.tm_mon]);
  else strcpy(buf, "zegar nieustawiony");
  if (strcmp(buf, hc.date)) {
    strcpy(hc.date, buf);
    lcd.fillRect(90, 6, 200, 30, C_BG);
    text(92, 14, buf, F_S, tv ? C_MUTED : C_COMFORT, C_BG);
  }

  // --- ikony stanu (prawy górny róg)
  NetState ns = netState();
  int wl = ns == NET_CONNECTED ? (netRssi() > -70 ? 2 : 1) : 0;
  uint8_t icons = (uint8_t)(wl | (cfg.wifiEnable ? 4 : 0) | (locked ? 8 : 0) | (cfg.childLock ? 16 : 0) |
                            (sensor.fault ? 32 : 0) | (ns == NET_AP ? 64 : 0));
  if (icons != hc.icons) {
    hc.icons = icons;
    lcd.fillRect(330, 0, 150, 42, C_BG);
    int x = 452;
    if (cfg.wifiEnable) { iconWifi(x, 18, ns == NET_AP ? 2 : wl, ns == NET_AP ? C_VAC : C_TEXT, C_LINE); x -= 34; }
    if (cfg.childLock) { iconLock(x, 18, locked ? C_COMFORT : C_MUTED, C_BG); x -= 30; }
    if (sensor.fault) iconWarn(x, 20, C_ALARM, C_BG);
  }

  // --- temperatura
  if (sensor.fault || isnan(sensor.value)) strcpy(buf, "--,-");
  else fmtTemp(buf, sizeof(buf), (int16_t)lroundf(sensor.value * 10), false);
  if (strcmp(buf, hc.temp)) {
    strcpy(hc.temp, buf);
    lcd.fillRect(16, 72, 296, 96, C_BG);
    uint32_t col = sensor.fault ? C_ALARM : C_TEXT;
    int w = text(20, 78, buf, F_BIG, col, C_BG);
    text(20 + w + 6, 82, "°C", F_B, C_MUTED, C_BG);
  }

  // --- etykieta stanu
  uint8_t kind;
  homeChip(buf, sizeof(buf), kind);
  if (strcmp(buf, hc.chip)) {
    strcpy(hc.chip, buf);
    lcd.fillRect(R_CHIP.x, R_CHIP.y, R_CHIP.w, R_CHIP.h, C_BG);
    uint32_t bg = kind == 1 ? 0x3A2216 : kind == 3 ? 0x3A1A1D : kind == 2 ? 0x173740 : C_PANEL;
    uint32_t fg = kind == 1 ? C_HEAT : kind == 3 ? C_ALARM : kind == 2 ? C_FROST : kind == 0 ? C_MUTED : C_COMFORT;
    int w = textW(buf, F_M) + 52;
    if (w > R_CHIP.w) w = R_CHIP.w;
    lcd.fillRoundRect(R_CHIP.x + 4, R_CHIP.y, w, R_CHIP.h, 19, bg);
    int ix = R_CHIP.x + 24, iy = R_CHIP.y + R_CHIP.h / 2;
    if (kind == 1) iconFlame(ix, iy, C_HEAT, bg);
    else if (kind == 2) iconWindow(ix, iy, C_FROST);
    else if (kind == 3) iconWarn(ix, iy, C_ALARM, bg);
    else lcd.fillCircle(ix, iy, 5, fg);
    textMid(R_CHIP.x + 42, iy, buf, F_M, fg, bg);
  }

  // --- linia trybu
  char name[16], rest[48];
  homeModeLine(name, sizeof(name), rest, sizeof(rest));
  snprintf(buf, sizeof(buf), "%s%s", name, rest);
  if (strcmp(buf, hc.mode)) {
    strcpy(hc.mode, buf);
    lcd.fillRect(16, 218, 298, 32, C_BG);
    uint32_t mc = th.boost ? C_BOOST : cfg.mode == MODE_OFF ? C_FROST : cfg.mode == MODE_VACATION ? C_VAC :
                  cfg.mode == MODE_ECO ? C_ECO : C_COMFORT;
    int w = text(24, 226, name, F_M, mc, C_BG);
    text(24 + w, 226, rest, F_M, C_MUTED, C_BG);
  }

  // --- zadana i przyciski
  bool can = thermoCanAdjust();
  fmtTemp(buf, sizeof(buf), th.target);
  uint32_t sc = modeColor(th.source);
  if (strcmp(buf, hc.set) || sc != hc.setColor || can != hc.canAdj || full) {
    strcpy(hc.set, buf);
    hc.setColor = sc;
    hc.canAdj = can;
    lcd.fillRect(320, 140, 140, 44, C_PANEL);
    textMid(390, 162, buf, F_XL, sc, C_PANEL, AL_C);
    button(R_PLUS, nullptr, C_PANEL2, 0);
    button(R_MINUS, nullptr, C_PANEL2, 0);
    iconPlus(R_PLUS.x + R_PLUS.w / 2, R_PLUS.y + R_PLUS.h / 2, can ? C_TEXT : C_LINE);
    iconMinus(R_MINUS.x + R_MINUS.w / 2, R_MINUS.y + R_MINUS.h / 2, can ? C_TEXT : C_LINE);
  }
}

static void touchHome(int x, int y, bool repeat) {
  if (R_PLUS.hit(x, y) || R_MINUS.hit(x, y)) {
    if (!thermoCanAdjust()) { if (!repeat) uiToast(cfg.mode == MODE_OFF ? "Tryb WYŁĄCZONY – zmień tryb" : "Awaria czujnika"); return; }
    thermoAdjust(R_PLUS.hit(x, y) ? 5 : -5);
    thermoLoop();
    drawHome(false);
    return;
  }
  if (repeat) return;
  if (R_CHIP.hit(x, y) && th.windowOpen) { thermoCancelWindow(); uiToast("Grzanie wznowione"); return; }
  if (R_NAV[0].hit(x, y)) go(SCR_MODE);
  if (R_NAV[1].hit(x, y)) {
    struct tm t;
    progDay = clockLocal(t) ? clockDow(t) : 0;
    go(SCR_PROGRAM);
  }
  if (R_NAV[2].hit(x, y)) go(SCR_CHART);
  if (R_NAV[3].hit(x, y)) go(SCR_MENU);
}

// ============================================================ TRYB PRACY ===
static Rect modeTile(int i) { return Rect{(int16_t)(16 + (i % 3) * 152), (int16_t)(60 + (i / 3) * 102), 144, 94}; }

static void drawMode() {
  lcd.fillScreen(C_BG);
  header("Tryb pracy");
  const char *names[6] = {"AUTO", "RĘCZNY", "EKO", "WYŁĄCZ", "URLOP", "BOOST"};
  char d[6][32];
  strcpy(d[0], "wg programu");
  fmtTemp(d[1], 32, cfg.tManual);
  { char tmp[12]; strcpy(tmp, d[1]); snprintf(d[1], 32, "stała %s", tmp); }
  { char tmp[12]; fmtTemp(tmp, 12, cfg.tEco); snprintf(d[2], 32, "obniżona %s", tmp); }
  { char tmp[12]; fmtTemp(tmp, 12, cfg.tFrost); snprintf(d[3], 32, "tylko ochrona %s", tmp); }
  snprintf(d[4], 32, "nieobecność");
  if (th.boost) snprintf(d[5], 32, "aktywny · %lu min", (unsigned long)(thermoBoostLeftS() + 59) / 60);
  else snprintf(d[5], 32, "%u min", cfg.boostMin);
  const uint32_t col[6] = {C_COMFORT, C_COMFORT, C_ECO, C_FROST, C_VAC, C_BOOST};
  for (int i = 0; i < 6; i++) {
    Rect r = modeTile(i);
    bool sel = (i < 5 && cfg.mode == i) || (i == 5 && th.boost);
    lcd.fillRoundRect(r.x, r.y, r.w, r.h, 12, C_PANEL);
    if (sel) {
      lcd.drawRoundRect(r.x, r.y, r.w, r.h, 12, col[i]);
      lcd.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 11, col[i]);
      lcd.drawRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 10, col[i]);
    }
    int ix = r.x + 26, iy = r.y + 30;
    switch (i) {
      case 0: iconCalendar(ix, iy, col[i], C_PANEL); break;
      case 1: iconThermo(ix, iy, col[i], C_PANEL); break;
      case 2: iconMoon(ix, iy, col[i], C_PANEL); break;
      case 3: iconSnow(ix, iy, col[i]); break;
      case 4: iconCase(ix, iy, col[i], C_PANEL); break;
      case 5: iconFlame(ix, iy, col[i], C_PANEL); break;
    }
    textMid(r.x + 48, iy, names[i], F_B, C_TEXT, C_PANEL);
    text(r.x + 14, r.y + 62, d[i], F_S, C_MUTED, C_PANEL);
    if (sel) iconCheck(r.x + r.w - 20, r.y + 20, col[i]);
  }
  text(240, 278, "Dotknij kafelka, aby zmienić tryb", F_S, C_MUTED, C_BG, AL_C);
}

static void touchMode(int x, int y) {
  if (R_BACK.hit(x, y)) { go(SCR_HOME); return; }
  for (int i = 0; i < 6; i++) {
    if (!modeTile(i).hit(x, y)) continue;
    if (i == 4) { vacDaysEdit = cfg.vacDays; vacTempEdit = cfg.tVacation; go(SCR_VACATION); return; }
    if (i == 5) {
      if (cfg.mode == MODE_OFF) { uiToast("Boost niedostępny w trybie WYŁ."); return; }
      thermoBoost(!th.boost);
      uiToast(th.boost ? "Boost włączony" : "Boost wyłączony");
      go(SCR_HOME);
      return;
    }
    thermoSetMode(i);
    go(SCR_HOME);
    return;
  }
}

// ================================================================= URLOP ===
static const Rect R_VD_M = {232, 66, 64, 56}, R_VD_P = {400, 66, 64, 56};
static const Rect R_VT_M = {232, 136, 64, 56}, R_VT_P = {400, 136, 64, 56};
static const Rect R_BTN_L = {16, 256, 218, 52}, R_BTN_R = {246, 256, 218, 52};

static void drawVacationValues() {
  char b[32];
  lcd.fillRect(298, 66, 100, 126, C_BG);
  snprintf(b, sizeof(b), "%u", vacDaysEdit);
  textMid(348, 94, b, F_XL, C_TEXT, C_BG, AL_C);
  fmtTemp(b, sizeof(b), vacTempEdit);
  textMid(348, 164, b, F_XL, C_VAC, C_BG, AL_C);
  lcd.fillRect(16, 204, 448, 40, C_BG);
  if (clockValid()) {
    time_t u = clockNow() + (time_t)vacDaysEdit * 86400;
    struct tm e; localtime_r(&u, &e);
    snprintf(b, sizeof(b), "Powrót do AUTO: %s %d %s, %02d:%02d", DAY_ABBR[clockDow(e)], e.tm_mday, MONTH_ABBR[e.tm_mon], e.tm_hour, e.tm_min);
    text(240, 214, b, F_S, C_MUTED, C_BG, AL_C);
  } else {
    text(240, 214, "Zegar nieustawiony – urlop do ręcznej zmiany trybu", F_S, C_COMFORT, C_BG, AL_C);
  }
}

static void drawVacation() {
  lcd.fillScreen(C_BG);
  header("Urlop");
  textMid(24, 94, "Liczba dni", F_M, C_TEXT, C_BG);
  textMid(24, 164, "Temperatura", F_M, C_TEXT, C_BG);
  for (const Rect *r : {&R_VD_M, &R_VT_M}) { button(*r, nullptr, C_PANEL2, 0); iconMinus(r->x + r->w / 2, r->y + r->h / 2, C_TEXT); }
  for (const Rect *r : {&R_VD_P, &R_VT_P}) { button(*r, nullptr, C_PANEL2, 0); iconPlus(r->x + r->w / 2, r->y + r->h / 2, C_TEXT); }
  button(R_BTN_L, "Anuluj", C_PANEL2, C_TEXT);
  button(R_BTN_R, "Rozpocznij", C_VAC, C_BG);
  drawVacationValues();
}

static void touchVacation(int x, int y, bool repeat) {
  bool ch = true;
  if (R_VD_M.hit(x, y)) vacDaysEdit = max(1, vacDaysEdit - 1);
  else if (R_VD_P.hit(x, y)) vacDaysEdit = min(60, vacDaysEdit + 1);
  else if (R_VT_M.hit(x, y)) vacTempEdit = max((int)cfg.tFrost, vacTempEdit - 5);
  else if (R_VT_P.hit(x, y)) vacTempEdit = min(250, vacTempEdit + 5);
  else ch = false;
  if (ch) { drawVacationValues(); return; }
  if (repeat) return;
  if (R_BACK.hit(x, y) || R_BTN_L.hit(x, y)) { go(SCR_MODE); return; }
  if (R_BTN_R.hit(x, y)) {
    cfg.vacDays = vacDaysEdit;
    cfg.tVacation = vacTempEdit;
    thermoStartVacation();
    uiToast("Tryb urlopowy włączony");
    go(SCR_HOME);
  }
}

// ======================================================= PROGRAM TYGODNIA ===
static const Rect R_COPY = {372, 6, 96, 36};
static Rect dayTab(int i) { return Rect{(int16_t)(16 + i * 64), 56, 58, 38}; }
static Rect periodRow(int i) { return Rect{16, (int16_t)(158 + i * 38), 448, 34}; }

static void drawTimeline(int x, int y, int w, int h, const DaySchedule &d, int hilite, uint16_t hs, uint16_t he) {
  lcd.fillRoundRect(x, y, w, h, 6, C_ECO_DK);
  for (int i = 0; i < d.count; i++) {
    int x0 = x + (long)d.start[i] * w / 1440, x1 = x + (long)d.end[i] * w / 1440;
    lcd.fillRect(x0, y, max(2, x1 - x0), h, i == hilite ? C_LINE : C_COMFORT);
  }
  if (hilite >= 0 || hilite == -2) {
    int x0 = x + (long)hs * w / 1440, x1 = x + (long)he * w / 1440;
    lcd.fillRect(x0, y, max(2, x1 - x0), h, C_HEAT);
  }
  for (int hh = 3; hh < 24; hh += 3) lcd.drawFastVLine(x + hh * w / 24, y + h - 5, 5, C_BG);
}

static void drawProgram() {
  lcd.fillScreen(C_BG);
  header("Program tygodniowy");
  button(R_COPY, "Kopiuj", C_PANEL2, C_TEXT, F_S);
  struct tm t;
  bool tv = clockLocal(t);
  uint8_t today = tv ? clockDow(t) : 255;
  for (int i = 0; i < 7; i++) {
    Rect r = dayTab(i);
    bool sel = i == progDay;
    uint32_t bg = sel ? C_COMFORT : C_PANEL;
    lcd.fillRoundRect(r.x, r.y, r.w, r.h, 8, bg);
    textMid(r.x + r.w / 2, r.y + r.h / 2, DAY_SHORT[i], F_M, sel ? C_BG : C_TEXT, bg, AL_C);
    if (i == today) lcd.fillRect(r.x + 20, r.y + r.h - 5, 18, 3, sel ? C_BG : C_COMFORT);
  }
  const DaySchedule &d = cfg.week[progDay];
  drawTimeline(16, 102, 448, 26, d, -1, 0, 0);
  if (tv && today == progDay) {
    int nx = 16 + (t.tm_hour * 60 + t.tm_min) * 448 / 1440;
    lcd.fillRect(nx - 1, 98, 3, 34, C_TEXT);
  }
  const char *lbl[] = {"0", "6", "12", "18", "24"};
  for (int i = 0; i < 5; i++) text(16 + i * 112, 134, lbl[i], F_S, C_MUTED, C_BG, i == 0 ? AL_L : i == 4 ? AL_R : AL_C);

  for (int i = 0; i < MAX_PERIODS; i++) {
    Rect r = periodRow(i);
    if (i < d.count) {
      char a[8], b[8], s[24];
      fmtHM(a, sizeof(a), d.start[i]);
      fmtHM(b, sizeof(b), d.end[i]);
      snprintf(s, sizeof(s), "%s – %s", a, b);
      lcd.fillRoundRect(r.x, r.y, r.w, r.h, 8, C_PANEL);
      lcd.fillRoundRect(r.x + 10, r.y + 9, 16, 16, 4, C_COMFORT);
      textMid(r.x + 40, r.y + r.h / 2, s, F_M, C_TEXT, C_PANEL);
      textMid(r.x + r.w - 34, r.y + r.h / 2, "Edytuj", F_S, C_MUTED, C_PANEL, AL_R);
      iconChevron(r.x + r.w - 18, r.y + r.h / 2, 'r', C_MUTED);
    } else if (i == d.count) {
      lcd.drawRoundRect(r.x, r.y, r.w, r.h, 8, C_LINE);
      iconPlus(r.x + 22, r.y + r.h / 2, C_MUTED);
      textMid(r.x + 44, r.y + r.h / 2, d.count == 0 ? "Dodaj okres komfortu (teraz cały dzień Eko)" : "Dodaj okres komfortu",
              F_M, C_MUTED, C_BG);
    }
  }
}

static void touchProgram(int x, int y) {
  if (R_BACK.hit(x, y)) { go(SCR_HOME); return; }
  if (R_COPY.hit(x, y)) { go(SCR_COPY); return; }
  for (int i = 0; i < 7; i++) if (dayTab(i).hit(x, y)) { progDay = i; go(SCR_PROGRAM); return; }
  const DaySchedule &d = cfg.week[progDay];
  for (int i = 0; i < MAX_PERIODS; i++) {
    if (!periodRow(i).hit(x, y)) continue;
    if (i < d.count) { periodIdx = i; perStart = d.start[i]; perEnd = d.end[i]; go(SCR_PERIOD); }
    else if (i == d.count) {
      periodIdx = -1;
      perStart = d.count ? min(1440 - 60, d.end[d.count - 1] + 60) : 17 * 60;
      perEnd = min(1440, perStart + 120);
      go(SCR_PERIOD);
    }
    return;
  }
}

// ------------------------------------------------------- edycja okresu ----
static const Rect R_PS_M = {232, 62, 64, 56}, R_PS_P = {400, 62, 64, 56};
static const Rect R_PE_M = {232, 128, 64, 56}, R_PE_P = {400, 128, 64, 56};
static const Rect R_P_DEL = {16, 256, 140, 52}, R_P_CAN = {170, 256, 140, 52}, R_P_OK = {324, 256, 140, 52};

static void drawPeriodValues() {
  char b[8];
  lcd.fillRect(298, 62, 100, 122, C_BG);
  fmtHM(b, sizeof(b), perStart);
  textMid(348, 90, b, F_XL, C_TEXT, C_BG, AL_C);
  fmtHM(b, sizeof(b), perEnd);
  textMid(348, 156, b, F_XL, C_TEXT, C_BG, AL_C);
  drawTimeline(16, 204, 448, 22, cfg.week[progDay], periodIdx >= 0 ? periodIdx : -2, perStart, perEnd);
}

static void drawPeriod() {
  lcd.fillScreen(C_BG);
  char t[40];
  if (periodIdx >= 0) snprintf(t, sizeof(t), "%s · okres %d", DAY_LONG[progDay], periodIdx + 1);
  else snprintf(t, sizeof(t), "%s · nowy okres", DAY_LONG[progDay]);
  header(t);
  textMid(24, 90, "Początek", F_M, C_TEXT, C_BG);
  textMid(24, 156, "Koniec", F_M, C_TEXT, C_BG);
  for (const Rect *r : {&R_PS_M, &R_PE_M}) { button(*r, nullptr, C_PANEL2, 0); iconMinus(r->x + r->w / 2, r->y + r->h / 2, C_TEXT); }
  for (const Rect *r : {&R_PS_P, &R_PE_P}) { button(*r, nullptr, C_PANEL2, 0); iconPlus(r->x + r->w / 2, r->y + r->h / 2, C_TEXT); }
  text(16, 232, "Krok 15 min · przytrzymaj, aby przewijać", F_S, C_MUTED, C_BG);
  if (periodIdx >= 0) button(R_P_DEL, "Usuń", 0x3A1A1D, C_ALARM);
  button(R_P_CAN, "Anuluj", C_PANEL2, C_TEXT);
  button(R_P_OK, "Zapisz", C_COMFORT, C_BG);
  drawPeriodValues();
}

static void touchPeriod(int x, int y, bool repeat) {
  bool ch = true;
  if (R_PS_M.hit(x, y)) { if (perStart >= 15) perStart -= 15; }
  else if (R_PS_P.hit(x, y)) { if (perStart <= 1440 - 30) { perStart += 15; if (perEnd < perStart + 15) perEnd = perStart + 15; } }
  else if (R_PE_M.hit(x, y)) { if (perEnd >= perStart + 30) perEnd -= 15; }
  else if (R_PE_P.hit(x, y)) { if (perEnd <= 1440 - 15) perEnd += 15; }
  else ch = false;
  if (ch) { drawPeriodValues(); return; }
  if (repeat) return;
  DaySchedule &d = cfg.week[progDay];
  if (R_BACK.hit(x, y) || R_P_CAN.hit(x, y)) { go(SCR_PROGRAM); return; }
  if (R_P_DEL.hit(x, y) && periodIdx >= 0) {
    d.end[periodIdx] = d.start[periodIdx];   // zostanie usunięty przez scheduleSort
    scheduleSort(d);
    settingsSave();
    go(SCR_PROGRAM);
    return;
  }
  if (R_P_OK.hit(x, y)) {
    if (periodIdx < 0) {
      if (d.count >= MAX_PERIODS) { go(SCR_PROGRAM); return; }
      periodIdx = d.count++;
    }
    d.start[periodIdx] = perStart;
    d.end[periodIdx] = perEnd;
    scheduleSort(d);
    settingsSave();
    uiToast("Zapisano");
    go(SCR_PROGRAM);
  }
}

// ---------------------------------------------------------- kopiowanie ----
static const Rect R_CP[3] = {{16, 104, 448, 52}, {16, 166, 448, 52}, {16, 228, 448, 52}};

static void drawCopy() {
  lcd.fillScreen(C_BG);
  char t[40];
  snprintf(t, sizeof(t), "Kopiuj: %s", DAY_LONG[progDay]);
  header(t);
  text(16, 66, "Skopiuj program tego dnia do:", F_M, C_MUTED, C_BG);
  const char *l[3] = {"Dni robocze (Pn–Pt)", "Weekend (So–Nd)", "Wszystkie dni"};
  for (int i = 0; i < 3; i++) button(R_CP[i], l[i], C_PANEL2, C_TEXT);
}

static void touchCopy(int x, int y) {
  if (R_BACK.hit(x, y)) { go(SCR_PROGRAM); return; }
  for (int i = 0; i < 3; i++) {
    if (!R_CP[i].hit(x, y)) continue;
    int from = i == 1 ? 5 : 0, to = i == 0 ? 4 : 6;
    for (int dd = from; dd <= to; dd++) if (dd != progDay) cfg.week[dd] = cfg.week[progDay];
    settingsSave();
    uiToast("Skopiowano");
    go(SCR_PROGRAM);
    return;
  }
}

// ================================================================ WYKRES ===
static void fmtDur(char *b, size_t n, uint32_t s) {
  uint32_t h = s / 3600, m = (s / 60) % 60;
  if (h) snprintf(b, n, "%lu h %lu m", (unsigned long)h, (unsigned long)m);
  else snprintf(b, n, "%lu min", (unsigned long)m);
}

static void drawChart() {
  lcd.fillScreen(C_BG);
  header("Historia 24 h");
  // legenda
  lcd.fillRect(300, 22, 16, 3, C_TEXT);
  text(320, 16, "temp.", F_S, C_MUTED, C_PANEL);
  lcd.fillRect(372, 22, 16, 3, C_COMFORT);
  text(392, 16, "zadana", F_S, C_MUTED, C_PANEL);

  const int X0 = 56, X1 = 464, Y0 = 60, Y1 = 214;
  int n = th.histCount;
  if (n < 2) {
    text(260, 130, "Brak danych – próbka co 5 minut", F_M, C_MUTED, C_BG, AL_C);
  } else {
    int16_t mn = INT16_MAX, mx = INT16_MIN;
    for (int i = 0; i < n; i++) {
      int idx = (th.histHead - n + i + HISTORY_LEN) % HISTORY_LEN;
      int16_t v = th.histTemp[idx], s = th.histTarget[idx];
      if (v != INT16_MIN) { mn = min(mn, v); mx = max(mx, v); }
      mn = min(mn, s); mx = max(mx, s);
    }
    int lo = (mn / 10) - 1, hi = (mx + 9) / 10 + 1;
    if (hi - lo < 4) hi = lo + 4;
    int stepG = (hi - lo) > 8 ? 2 : 1;
    for (int g = lo; g <= hi; g += stepG) {
      int y = Y1 - (long)(g - lo) * (Y1 - Y0) / (hi - lo);
      lcd.drawFastHLine(X0, y, X1 - X0, C_PANEL2);
      char b[8]; snprintf(b, sizeof(b), "%d°", g);
      textMid(X0 - 8, y, b, F_S, C_MUTED, C_BG, AL_R);
    }
    auto px = [&](int i) { return X0 + (long)(HISTORY_LEN - n + i) * (X1 - X0) / (HISTORY_LEN - 1); };
    auto py = [&](int v10) { return Y1 - (long)(v10 - lo * 10) * (Y1 - Y0) / ((hi - lo) * 10); };
    int prevX = -1, prevY = 0, prevS = 0;
    for (int i = 0; i < n; i++) {
      int idx = (th.histHead - n + i + HISTORY_LEN) % HISTORY_LEN;
      int x = px(i);
      int h = th.histHeat[idx];
      if (h > 0) lcd.fillRect(x - 1, 222, 3, 10, h >= 50 ? C_HEAT : 0x7A3A1E);
      int sy = py(th.histTarget[idx]);
      if (i > 0) { lcd.drawFastHLine(px(i - 1), prevS, x - px(i - 1), C_COMFORT); lcd.drawLine(x, prevS, x, sy, C_COMFORT); }
      prevS = sy;
      int16_t v = th.histTemp[idx];
      if (v == INT16_MIN) { prevX = -1; continue; }
      int y = py(v);
      if (prevX >= 0) lcd.drawWideLine(prevX, prevY, x, y, 1.0f, C_TEXT);
      prevX = x; prevY = y;
    }
  }
  const char *xl[] = {"−24 h", "−18 h", "−12 h", "−6 h", "teraz"};
  for (int i = 0; i < 5; i++) text(X0 + i * (X1 - X0) / 4, 236, xl[i], F_S, C_MUTED, C_BG, i == 0 ? AL_L : i == 4 ? AL_R : AL_C);

  char v[4][20];
  fmtDur(v[0], 20, th.heatSecToday);
  fmtDur(v[1], 20, th.heatSecYesterday);
  uint32_t wk = 0; for (int i = 0; i < 7; i++) wk += th.weekMinutes[i];
  fmtDur(v[2], 20, wk * 60);
  snprintf(v[3], 20, "%u", th.cyclesToday);
  const char *lab[4] = {"Grzanie dziś", "Wczoraj", "7 dni", "Załączeń dziś"};
  for (int i = 0; i < 4; i++) {
    Rect r = R_NAV[i];
    lcd.fillRoundRect(r.x, r.y, r.w, r.h, 10, C_PANEL);
    text(r.x + 10, r.y + 6, lab[i], F_S, C_MUTED, C_PANEL);
    text(r.x + 10, r.y + 25, v[i], F_M, C_TEXT, C_PANEL);
  }
}

// ================================================================== MENU ===
static Rect catTile(int i) { return Rect{(int16_t)(16 + (i % 2) * 228), (int16_t)(58 + (i / 2) * 50), 220, 44}; }

static void drawMenu() {
  lcd.fillScreen(C_BG);
  header("Ustawienia");
  for (int i = 0; i < CATEGORY_COUNT; i++) {
    Rect r = catTile(i);
    lcd.fillRoundRect(r.x, r.y, r.w, r.h, 10, C_PANEL);
    textMid(r.x + 16, r.y + r.h / 2, CATEGORIES[i].name, F_M, C_TEXT, C_PANEL);
    iconChevron(r.x + r.w - 20, r.y + r.h / 2, 'r', C_MUTED);
  }
}

static void touchMenu(int x, int y) {
  if (R_BACK.hit(x, y)) { go(SCR_HOME); return; }
  for (int i = 0; i < CATEGORY_COUNT; i++)
    if (catTile(i).hit(x, y)) { catIdx = i; listPage = 0; go(SCR_LIST); return; }
}

// ---------------------------------------------------------- lista param. --
static const int ROWS = 5;
static const Rect R_UP = {16, 274, 120, 40}, R_DN = {344, 274, 120, 40};
static Rect listRow(int i) { return Rect{16, (int16_t)(54 + i * 44), 448, 40}; }

static void drawList() {
  lcd.fillScreen(C_BG);
  const Category &c = CATEGORIES[catIdx];
  header(c.name);
  int pages = (c.count + ROWS - 1) / ROWS;
  if (listPage >= pages) listPage = 0;
  for (int i = 0; i < ROWS; i++) {
    int pi = listPage * ROWS + i;
    if (pi >= c.count) break;
    const Param &p = c.params[pi];
    Rect r = listRow(i);
    lcd.fillRoundRect(r.x, r.y, r.w, r.h, 8, C_PANEL);
    textMid(r.x + 14, r.y + r.h / 2, p.name, F_M, C_TEXT, C_PANEL);
    char v[48];
    int val = (p.type == P_INFO || p.type == P_ACTION) ? 0 : p.get();
    paramFormat(p, val, v, sizeof(v));
    bool editable = p.type != P_INFO;
    int rx = r.x + r.w - (editable ? 34 : 14);
    uint32_t vc = p.type == P_INFO ? C_MUTED : (p.type == P_BOOL ? (val ? C_OK : C_MUTED) : C_COMFORT);
    if (p.type == P_ACTION) vc = C_MUTED;
    textMid(rx, r.y + r.h / 2, v, F_M, vc, C_PANEL, AL_R);
    if (editable) iconChevron(r.x + r.w - 18, r.y + r.h / 2, 'r', C_MUTED);
  }
  if (pages > 1) {
    button(R_UP, nullptr, C_PANEL2, 0); iconChevron(R_UP.x + R_UP.w / 2, R_UP.y + R_UP.h / 2, 'u', listPage > 0 ? C_TEXT : C_LINE);
    button(R_DN, nullptr, C_PANEL2, 0); iconChevron(R_DN.x + R_DN.w / 2, R_DN.y + R_DN.h / 2, 'd', listPage < pages - 1 ? C_TEXT : C_LINE);
    char b[16]; snprintf(b, sizeof(b), "%d / %d", listPage + 1, pages);
    textMid(240, 294, b, F_M, C_MUTED, C_BG, AL_C);
  }
}

static void startConfirm(const Param *p) { confirmParam = p; confirmReturn = scr; go(SCR_CONFIRM); }

static void touchList(int x, int y) {
  if (R_BACK.hit(x, y)) { go(SCR_MENU); return; }
  const Category &c = CATEGORIES[catIdx];
  int pages = (c.count + ROWS - 1) / ROWS;
  if (pages > 1 && R_UP.hit(x, y)) { if (listPage > 0) { listPage--; go(SCR_LIST); } return; }
  if (pages > 1 && R_DN.hit(x, y)) { if (listPage < pages - 1) { listPage++; go(SCR_LIST); } return; }
  for (int i = 0; i < ROWS; i++) {
    int pi = listPage * ROWS + i;
    if (pi >= c.count || !listRow(i).hit(x, y)) continue;
    const Param &p = c.params[pi];
    paramIdx = pi;
    switch (p.type) {
      case P_BOOL: p.set(!p.get()); settingsSave(); go(SCR_LIST); break;
      case P_NUM: case P_ENUM: editVal = p.get(); go(SCR_EDIT); break;
      case P_ACTION: if (p.confirm) startConfirm(&p); else { p.action(); if (scr == SCR_LIST) go(SCR_LIST); } break;
      default: break;
    }
    return;
  }
}

// ---------------------------------------------------------- edycja param. -
static const Rect R_E_M = {16, 70, 112, 84}, R_E_P = {352, 70, 112, 84};

static void wrapText(int x, int y, int w, const char *s, FontId f, uint32_t c, int lineH, int maxLines) {
  char line[96]; int ll = 0; const char *p = s; int lines = 0;
  line[0] = 0;
  while (*p && lines < maxLines) {
    const char *sp = strchr(p, ' ');
    int wl = sp ? (int)(sp - p) : (int)strlen(p);
    char trial[96];
    snprintf(trial, sizeof(trial), "%s%s%.*s", line, ll ? " " : "", wl, p);
    if (textW(trial, f) > w && ll) {
      text(x, y + lines * lineH, line, f, c, C_BG);
      lines++;
      snprintf(line, sizeof(line), "%.*s", wl, p);
    } else {
      strlcpy(line, trial, sizeof(line));
    }
    ll = strlen(line);
    p += wl;
    while (*p == ' ') p++;
  }
  if (ll && lines < maxLines) text(x, y + lines * lineH, line, f, c, C_BG);
}

static void drawEditValue() {
  const Param &p = CATEGORIES[catIdx].params[paramIdx];
  char v[48];
  paramFormat(p, editVal, v, sizeof(v));
  lcd.fillRect(130, 70, 220, 84, C_BG);
  FontId f = textW(v, F_XL) > 216 ? F_B : F_XL;
  textMid(240, 112, v, f, C_COMFORT, C_BG, AL_C);
  iconMinus(R_E_M.x + R_E_M.w / 2, R_E_M.y + R_E_M.h / 2, editVal > p.min ? C_TEXT : C_LINE);
  iconPlus(R_E_P.x + R_E_P.w / 2, R_E_P.y + R_E_P.h / 2, editVal < p.max ? C_TEXT : C_LINE);
}

static void drawEdit() {
  const Param &p = CATEGORIES[catIdx].params[paramIdx];
  lcd.fillScreen(C_BG);
  header(p.name);
  button(R_E_M, nullptr, C_PANEL2, 0);
  button(R_E_P, nullptr, C_PANEL2, 0);
  char a[24], b[24], r[64];
  if (p.type == P_ENUM) snprintf(r, sizeof(r), "Opcje: %d", p.nopts);
  else { paramFormat(p, p.min, a, sizeof(a)); paramFormat(p, p.max, b, sizeof(b)); snprintf(r, sizeof(r), "Zakres: %s … %s", a, b); }
  text(240, 166, r, F_S, C_MUTED, C_BG, AL_C);
  if (p.help) wrapText(24, 192, 432, p.help, F_S, C_TEXT, 20, 3);
  button(R_BTN_L, "Anuluj", C_PANEL2, C_TEXT);
  button(R_BTN_R, "Zapisz", C_COMFORT, C_BG);
  drawEditValue();
}

static void touchEdit(int x, int y, bool repeat, uint32_t heldMs) {
  const Param &p = CATEGORIES[catIdx].params[paramIdx];
  int step = p.step * (heldMs > 2500 && p.type == P_NUM && (p.max - p.min) / p.step > 50 ? 5 : 1);
  if (R_E_M.hit(x, y)) {
    if (p.type == P_ENUM) editVal = editVal > p.min ? editVal - 1 : (repeat ? editVal : p.max);
    else editVal = max((int)p.min, editVal - step);
    drawEditValue(); return;
  }
  if (R_E_P.hit(x, y)) {
    if (p.type == P_ENUM) editVal = editVal < p.max ? editVal + 1 : (repeat ? editVal : p.min);
    else editVal = min((int)p.max, editVal + step);
    drawEditValue(); return;
  }
  if (repeat) return;
  if (R_BACK.hit(x, y) || R_BTN_L.hit(x, y)) { go(SCR_LIST); return; }
  if (R_BTN_R.hit(x, y)) {
    p.set(editVal);
    settingsSave();
    lcd.setBrightness(cfg.brightness * 255 / 100);
    uiToast("Zapisano");
    go(SCR_LIST);
  }
}

// ---------------------------------------------------------- potwierdzenie -
static void drawConfirm() {
  lcd.fillScreen(C_BG);
  header("Potwierdź", false);
  if (confirmParam) {
    textMid(240, 110, confirmParam->name, F_B, C_TEXT, C_BG, AL_C);
    if (confirmParam->help && confirmParam->help[0]) wrapText(40, 150, 400, confirmParam->help, F_S, C_MUTED, 20, 3);
  }
  button(R_BTN_L, "Anuluj", C_PANEL2, C_TEXT);
  button(R_BTN_R, "Wykonaj", C_ALARM, C_BG);
}

static void touchConfirm(int x, int y) {
  if (R_BTN_L.hit(x, y)) { go(confirmReturn); return; }
  if (R_BTN_R.hit(x, y) && confirmParam) {
    go(confirmReturn);
    confirmParam->action();
  }
}

// ---------------------------------------------------------- Wi-Fi AP ------
static void drawAp() {
  lcd.fillScreen(C_BG);
  header("Konfiguracja Wi-Fi", false);
  text(24, 64, "1. Połącz telefon z siecią:", F_M, C_MUTED, C_BG);
  text(48, 90, netApName().c_str(), F_B, C_VAC, C_BG);
  char b[48]; snprintf(b, sizeof(b), "hasło: %s", netApPass());
  text(48, 122, b, F_M, C_TEXT, C_BG);
  text(24, 156, "2. Otwórz w przeglądarce:", F_M, C_MUTED, C_BG);
  snprintf(b, sizeof(b), "http://%s", netIP().c_str());
  text(48, 182, b, F_B, C_VAC, C_BG);
  text(24, 218, "3. Wpisz nazwę i hasło sieci domowej.", F_M, C_MUTED, C_BG);
  button(R_BTN_R, "Zakończ", C_PANEL2, C_TEXT);
}

static void touchAp(int x, int y) {
  if (R_BTN_R.hit(x, y)) { netReconnect(); go(SCR_LIST); }
}

// ================================================================ toast ====
static void drawToast() {
  int w = textW(toastMsg, F_M) + 40;
  int x = (SCREEN_W - w) / 2;
  lcd.fillRoundRect(x, 212, w, 40, 20, C_TEXT);
  textMid(SCREEN_W / 2, 232, toastMsg, F_M, C_BG, C_TEXT, AL_C);
}

// ============================================================= dispatch ===
static void drawScreen() {
  switch (scr) {
    case SCR_HOME: drawHome(true); break;
    case SCR_MODE: drawMode(); break;
    case SCR_VACATION: drawVacation(); break;
    case SCR_PROGRAM: drawProgram(); break;
    case SCR_PERIOD: drawPeriod(); break;
    case SCR_COPY: drawCopy(); break;
    case SCR_CHART: drawChart(); break;
    case SCR_MENU: drawMenu(); break;
    case SCR_LIST: drawList(); break;
    case SCR_EDIT: drawEdit(); break;
    case SCR_CONFIRM: drawConfirm(); break;
    case SCR_AP: drawAp(); break;
  }
}

static void handleTouch(int x, int y, bool repeat, uint32_t heldMs) {
  switch (scr) {
    case SCR_HOME: touchHome(x, y, repeat); break;
    case SCR_MODE: if (!repeat) touchMode(x, y); break;
    case SCR_VACATION: touchVacation(x, y, repeat); break;
    case SCR_PROGRAM: if (!repeat) touchProgram(x, y); break;
    case SCR_PERIOD: touchPeriod(x, y, repeat); break;
    case SCR_COPY: if (!repeat) touchCopy(x, y); break;
    case SCR_CHART: if (!repeat && R_BACK.hit(x, y)) go(SCR_HOME); break;
    case SCR_MENU: if (!repeat) touchMenu(x, y); break;
    case SCR_LIST: if (!repeat) touchList(x, y); break;
    case SCR_EDIT: touchEdit(x, y, repeat, heldMs); break;
    case SCR_CONFIRM: if (!repeat) touchConfirm(x, y); break;
    case SCR_AP: if (!repeat) touchAp(x, y); break;
  }
}

static bool isStepper(int x, int y) {
  switch (scr) {
    case SCR_HOME: return R_PLUS.hit(x, y) || R_MINUS.hit(x, y);
    case SCR_VACATION: return R_VD_M.hit(x, y) || R_VD_P.hit(x, y) || R_VT_M.hit(x, y) || R_VT_P.hit(x, y);
    case SCR_PERIOD: return R_PS_M.hit(x, y) || R_PS_P.hit(x, y) || R_PE_M.hit(x, y) || R_PE_P.hit(x, y);
    case SCR_EDIT: return R_E_M.hit(x, y) || R_E_P.hit(x, y);
    default: return false;
  }
}

void uiWake() {
  lastInputMs = millis();
  if (dimmed) { dimmed = false; lcd.setBrightness(cfg.brightness * 255 / 100); }
}

void uiCalibrateTouch() {
  lcd.fillScreen(C_BG);
  text(240, 130, "Kalibracja dotyku", F_B, C_TEXT, C_BG, AL_C);
  text(240, 170, "Dotykaj kolejno wskazanych rogów", F_M, C_MUTED, C_BG, AL_C);
  delay(1200);
  lcd.calibrateTouch(cfg.touchCal, C_TEXT, C_BG, 24);
  cfg.touchValid = 1;
  settingsSaveNow();
  lcd.setTouchCalibrate(cfg.touchCal);
  touchDown = true;   // zignoruj ostatni dotyk kalibracji
  wakeTouch = true;
  uiToast("Kalibracja zapisana");
}

void uiBegin() {
  lcd.init();
  lcd.setRotation(1);   // poziomo 480x320; jeśli obraz odwrócony: 3
  lcd.setBrightness(cfg.brightness * 255 / 100);
  lcd.fillScreen(C_BG);
  fontsInit();
  if (cfg.touchValid) lcd.setTouchCalibrate(cfg.touchCal);
  else uiCalibrateTouch();
  lastInputMs = millis();
  needFull = true;
}

void uiLoop() {
  uint32_t now = millis();

  if (calibrateReq) { calibrateReq = false; uiCalibrateTouch(); go(SCR_HOME); }

  // ---- dotyk
  int32_t x, y;
  bool pressed = lcd.getTouch(&x, &y) > 0;
  if (pressed) {
    if (!touchDown) {
      touchDown = true;
      touchStartMs = lastRepeatMs = now;
      tx = x; ty = y;
      lastInputMs = now;
      if (dimmed) { uiWake(); wakeTouch = true; if (scr != SCR_HOME) go(SCR_HOME); }
      else if (locked) { uiToast("Zablokowane – przytrzymaj 3 s"); }
      else if (!wakeTouch) handleTouch(x, y, false, 0);
    } else if (!wakeTouch) {
      lastInputMs = now;
      uint32_t held = now - touchStartMs;
      if (locked) {
        if (held > LONG_PRESS_UNLOCK_MS) { locked = false; wakeTouch = true; uiToast("Odblokowano"); }
      } else if (isStepper(tx, ty) && held > 500 && now - lastRepeatMs > 110) {
        lastRepeatMs = now;
        handleTouch(tx, ty, true, held);
      }
    }
  } else if (touchDown) {
    touchDown = false;
    wakeTouch = false;
  }

  // ---- wygaszanie / blokada / powrót do ekranu głównego
  uint32_t idle = now - lastInputMs;
  if (!dimmed && cfg.dimAfter && idle > (uint32_t)cfg.dimAfter * 1000UL) {
    dimmed = true;
    lcd.setBrightness(cfg.dimBrightness * 255 / 100);
    if (cfg.childLock) locked = true;
    if (scr != SCR_HOME && scr != SCR_AP) go(SCR_HOME);
  }
  if (idle > UI_IDLE_HOME_MS && scr != SCR_HOME && scr != SCR_AP) go(SCR_HOME);
  if (!cfg.childLock) locked = false;

  // ---- rysowanie
  if (toastUntil && (int32_t)(now - toastUntil) >= 0) { toastUntil = 0; needFull = true; }
  if (needFull) {
    needFull = false;
    drawScreen();
    if (toastUntil) drawToast();
  } else if (scr == SCR_HOME && !toastUntil) {
    static uint32_t lastHome = 0;
    if (now - lastHome > 500) { lastHome = now; drawHome(false); }
  }
}

// ========================================================== main.cpp
// ============================================================================
//  ESP32-ogrzewanie – termostat pokojowy z ekranem dotykowym
//  ESP32-C3 (LOLIN C3 Mini) + ST7796 480x320 + XPT2046 + DS18B20
//  Wyjście GPIO3 sterowane UJEMNIE -> przekaźnik/stycznik głowicy grzejnika.
// ============================================================================
#include <Arduino.h>

static void statusLed() {
#ifdef RGB_BUILTIN
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  static bool blink = false;
  blink = !blink;
  // słabe kolory, żeby nie świecić w nocy: pomarańcz = grzeje, zielony = czuwa, czerwony = awaria
  if (sensor.fault) rgbLedWrite(PIN_RGB_LED, blink ? 20 : 0, 0, 0);
  else if (th.heating) rgbLedWrite(PIN_RGB_LED, 12, 4, 0);
  else rgbLedWrite(PIN_RGB_LED, 0, 3, 0);
#endif
}

void setup() {
  // Najpierw wyjście w stan bezpieczny (nieaktywne = HIGH przy sterowaniu ujemnym)
  pinMode(PIN_OUTPUT, INPUT_PULLUP);
  Serial.begin(115200);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  settingsLoad();
  thermoBegin();
  clockBegin();
  sensorBegin();
  uiBegin();

  // przytrzymany BOOT przy starcie = ponowna kalibracja dotyku
  if (digitalRead(PIN_BOOT_BTN) == LOW) uiCalibrateTouch();

  netBegin();
  Serial.printf("ESP32-ogrzewanie %s gotowy\n", FW_VERSION);
}

void loop() {
  sensorLoop();
  thermoLoop();
  uiLoop();
  netLoop();
  settingsLoop();
  statusLed();
  if (digitalRead(PIN_BOOT_BTN) == LOW) uiWake();
  delay(5);
}
