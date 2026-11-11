// Odczyt SHT31 (I2C) bez blokowania pętli: pomiar jednorazowy, odczyt po 20 ms.
#pragma once
#include <Arduino.h>

struct SensorState {
  bool    present = false;   // czujnik odpowiada na I2C
  bool    fault = true;      // brak poprawnego odczytu
  float   raw = NAN;         // ostatnia surowa temperatura [°C]
  float   value = NAN;       // temperatura po korekcie i filtrze
  float   humRaw = NAN;      // ostatnia surowa wilgotność [%RH]
  float   hum = NAN;         // wilgotność po korekcie i filtrze
  uint8_t addr = 0;          // 0x44 (ADDR do GND) lub 0x45 (ADDR do 3V3)
  uint32_t lastOk = 0;
  uint16_t errors = 0;       // licznik błędów od startu
};

extern SensorState sensor;

void sensorBegin();
void sensorLoop();
