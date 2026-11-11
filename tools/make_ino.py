#!/usr/bin/env python3
"""
Skleja kod z src/ w jeden szkic Arduino IDE:
  arduino/ESP32_ogrzewania/ESP32_ogrzewania.ino   – cały program
  arduino/ESP32_ogrzewania/czcionki.h             – czcionki z polskimi znakami

Użycie:  python3 tools/make_ino.py
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
NAME = "ESP32_ogrzewania"
OUT = os.path.join(ROOT, "arduino", NAME)

HEADERS = ["config.h", "display.h", "settings.h", "sensor.h", "clock.h",
           "thermostat.h", "net.h", "params.h", "ui.h"]
SOURCES = ["settings.cpp", "sensor.cpp", "clock.cpp", "thermostat.cpp",
           "net.cpp", "params.cpp", "ui.cpp", "main.cpp"]
FONTS = ["FontS", "FontM", "FontB", "FontXL", "FontBig"]

LOCAL_INC = re.compile(r'^\s*#include\s+"[^"]+"\s*$')


def read(name):
    with open(os.path.join(SRC, name), encoding="utf-8") as f:
        return f.read()


def strip(text):
    out = []
    for line in text.splitlines():
        if LOCAL_INC.match(line) or line.strip() == "#pragma once":
            continue
        out.append(line)
    return "\n".join(out).strip("\n")


# Typy z ui.cpp muszą stać przed pierwszą funkcją – Arduino IDE wstawia tam
# automatyczne prototypy funkcji, które z nich korzystają.
UI_TYPES = [
    r"enum FontId : uint8_t \{[^}]*\};",
    r"enum Align : uint8_t \{[^}]*\};",
    r"struct Rect \{[^\n]*\};",
    r"enum Screen : uint8_t \{.*?\};",
    r"struct HomeCache \{.*?\} hc;",
]


def main():
    os.makedirs(OUT, exist_ok=True)

    # ---- czcionki
    fonts = ["// Czcionki Barlow Semi Condensed (SIL Open Font License) z polskimi znakami.",
             "// Wygenerowane przez tools/make_fonts.py – nie edytować ręcznie.",
             "#pragma once", "#include <stdint.h>", ""]
    for fn in FONTS:
        fonts.append(strip(read(os.path.join("fonts", fn + ".h"))))
        fonts.append("")
    with open(os.path.join(OUT, "czcionki.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(fonts))

    # ---- program
    ui = read("ui.cpp")
    moved = []
    for pat in UI_TYPES:
        m = re.search(pat, ui, re.S)
        if not m:
            raise SystemExit("Nie znaleziono w ui.cpp: " + pat)
        moved.append(m.group(0))
        ui = ui[:m.start()] + ui[m.end():]

    parts = [f"""// ============================================================================
//  {NAME} – termostat pokojowy z dotykowym menu graficznym
//
//  Płytka:      WEMOS LOLIN C3 Mini (ESP32-C3)
//  Wyświetlacz: 4" ST7796 480x320 SPI + dotyk rezystancyjny XPT2046
//  Czujnik:     SHT31 (temperatura i wilgotność, I2C)
//  Wyjście:     GPIO3, AKTYWNE STANEM NISKIM -> przekaźnik / stycznik głowicy
//
//  Arduino IDE:
//   1. Menedżer płytek: "esp32" (Espressif) w wersji 3.x
//   2. Menedżer bibliotek: LovyanGFX (czujnik SHT31 nie wymaga biblioteki)
//   3. Narzędzia -> Płytka: "LOLIN C3 Mini"
//      Narzędzia -> Partition Scheme: "No OTA (2MB APP/2MB SPIFFS)"
//      Narzędzia -> USB CDC On Boot: "Enabled"
//   4. Plik czcionki.h musi leżeć w tym samym folderze co ten plik.
//
//  POŁĄCZENIA
//   ESP32-C3      ST7796 / XPT2046 / SHT31 / przekaźnik
//   GPIO4   ->    SCK  + T_CLK
//   GPIO6   ->    SDI  + T_DIN  (MOSI)
//   GPIO5   <-    T_DO          (SDO wyświetlacza NIE podłączać)
//   GPIO10  ->    TFT CS
//   GPIO8   ->    TFT DC/RS
//   GPIO1   ->    TFT RESET
//   GPIO21  ->    TFT LED (podświetlenie PWM)
//   GPIO20  ->    T_CS          (T_IRQ niepodłączony)
//   GPIO0   <->   SHT31 SDA     (moduł SHT31: VIN->3V3, GND->GND)
//   GPIO2   ->    SHT31 SCL     (ADDR do GND lub wolny = adres 0x44)
//   GPIO3   ->    IN przekaźnika (aktywny LOW) + 10 kΩ do 3V3
//   5V/GND  ->    VCC/GND wyświetlacza, JD-VCC przekaźnika
//
//  Plik wygenerowany z repozytorium przez tools/make_ino.py.
// ============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <sys/time.h>
#include "czcionki.h"
"""]
    for h in HEADERS:
        parts.append(f"// ========================================================== {h}\n" + strip(read(h)))
    parts.append("// ========================================================== typy interfejsu\n" + "\n\n".join(moved))
    for s in SOURCES:
        body = strip(ui if s == "ui.cpp" else read(s))
        parts.append(f"// ========================================================== {s}\n" + body)

    with open(os.path.join(OUT, NAME + ".ino"), "w", encoding="utf-8") as f:
        f.write("\n\n".join(parts) + "\n")
    print("Zapisano", OUT)


if __name__ == "__main__":
    main()
