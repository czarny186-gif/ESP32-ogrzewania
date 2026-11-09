#include "sensor.h"
#include "config.h"
#include "settings.h"
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
