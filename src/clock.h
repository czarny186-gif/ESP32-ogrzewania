// Czas lokalny (strefa PL z DST). Źródło: NTP przez Wi-Fi lub ręczne ustawienie.
#pragma once
#include <Arduino.h>
#include <time.h>

void clockBegin();
bool clockValid();                 // czy czas jest ustawiony
time_t clockNow();
bool clockLocal(struct tm &t);     // false, gdy czas nieustawiony
uint8_t clockDow(const struct tm &t); // 0 = poniedziałek
void clockSetLocal(int year, int mon, int day, int hour, int min);
void clockStartNtp();
