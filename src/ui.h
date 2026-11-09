// Interfejs graficzny 480x320 z obsługą dotyku.
#pragma once
#include <Arduino.h>

void uiBegin();
void uiLoop();
void uiCalibrateTouch();   // uruchamia kalibrację dotyku (zapis do NVS)
void uiWake();             // wybudzenie ekranu (np. przycisk BOOT)
