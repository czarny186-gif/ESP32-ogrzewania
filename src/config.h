// ============================================================================
//  ESP32-ogrzewanie – konfiguracja sprzętu
//  Płytka: WEMOS LOLIN C3 Mini (ESP32-C3FH4), wyświetlacz 4" ST7796 480x320
//  z dotykiem rezystancyjnym XPT2046, czujnik DS18B20, wyjście na stycznik /
//  przekaźnik głowicy termoelektrycznej sterowane stanem NISKIM.
// ============================================================================
#pragma once
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
