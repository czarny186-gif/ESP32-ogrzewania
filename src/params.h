// Tabela parametrów menu "Ustawienia" – opis, zakres, odczyt/zapis.
#pragma once
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
