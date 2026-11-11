// ============================================================================
//  ESP32-ogrzewanie – termostat pokojowy z ekranem dotykowym
//  ESP32-C3 (LOLIN C3 Mini) + ST7796 480x320 + XPT2046 + SHT31
//  Wyjście GPIO3 sterowane UJEMNIE -> przekaźnik/stycznik głowicy grzejnika.
// ============================================================================
#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "sensor.h"
#include "thermostat.h"
#include "clock.h"
#include "net.h"
#include "ui.h"
#include "display.h"

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
