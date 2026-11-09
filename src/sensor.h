// Odczyt DS18B20 bez blokowania pętli (konwersja asynchroniczna).
#pragma once
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
