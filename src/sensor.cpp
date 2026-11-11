// SHT31 – własny, mały sterownik na Wire (bez dodatkowych bibliotek).
#include "sensor.h"
#include "config.h"
#include "settings.h"
#include <Wire.h>

SensorState sensor;

static uint32_t lastRequest = 0;
static bool measuring = false;
static uint8_t badInRow = 0;

// CRC-8 SHT3x: wielomian 0x31, start 0xFF
static uint8_t crc8(const uint8_t *d, int n) {
  uint8_t c = 0xFF;
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
  }
  return c;
}

static bool sendCmd(uint16_t cmd) {
  Wire.beginTransmission(sensor.addr);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)(cmd & 0xFF));
  return Wire.endTransmission() == 0;
}

static bool probe(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

static void findSensor() {
  sensor.addr = probe(0x44) ? 0x44 : probe(0x45) ? 0x45 : 0;
  sensor.present = sensor.addr != 0;
  if (sensor.present) {
    sendCmd(0x30A2);   // miękki reset
    delay(2);
    sendCmd(0x3066);   // grzałka wyłączona
  }
}

static void fail() {
  sensor.errors++;
  if (++badInRow >= SENSOR_FAIL_COUNT) {
    sensor.fault = true;
    sensor.present = false;            // spróbuj wykryć ponownie
  }
}

void sensorBegin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  Wire.setTimeOut(20);
  findSensor();
  lastRequest = millis() - SENSOR_PERIOD_MS;
}

void sensorLoop() {
  uint32_t now = millis();
  if (!measuring) {
    if (now - lastRequest < SENSOR_PERIOD_MS) return;
    lastRequest = now;
    if (!sensor.present) findSensor();   // ponowne wykrycie po podłączeniu
    if (!sensor.present) { fail(); return; }
    if (!sendCmd(0x2400)) { fail(); return; }   // pomiar, wysoka powtarzalność, bez rozciągania zegara
    measuring = true;
    return;
  }
  if (now - lastRequest < 20) return;   // pomiar trwa max 15 ms
  measuring = false;

  uint8_t d[6];
  if (Wire.requestFrom((int)sensor.addr, 6) != 6) { fail(); return; }
  for (int i = 0; i < 6; i++) d[i] = Wire.read();
  if (crc8(d, 2) != d[2] || crc8(d + 3, 2) != d[5]) { fail(); return; }

  float t = -45.0f + 175.0f * (float)((d[0] << 8) | d[1]) / 65535.0f;
  float rh = 100.0f * (float)((d[3] << 8) | d[4]) / 65535.0f;
  if (t < -40 || t > 100) { fail(); return; }

  badInRow = 0;
  sensor.raw = t;
  sensor.humRaw = rh;
  float tc = t + cfg.offset / 10.0f;
  float hc = constrain(rh + cfg.humOffset, 0.0f, 100.0f);
  if (sensor.fault || isnan(sensor.value)) {
    sensor.value = tc;
    sensor.hum = hc;
  } else {
    // filtr wykładniczy; filter 0 = brak, 5 = mocny
    float alpha = 1.0f / (1.0f + cfg.filter * 1.5f);
    sensor.value += alpha * (tc - sensor.value);
    sensor.hum += alpha * (hc - sensor.hum);
  }
  sensor.fault = false;
  sensor.lastOk = now;
}
