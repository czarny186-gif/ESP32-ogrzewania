// Logika regulatora: tryby, program, histereza / TPI, ochrony, statystyki.
#pragma once
#include <Arduino.h>
#include "config.h"

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
