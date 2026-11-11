# ESP32-ogrzewanie

Termostat pokojowy z dotykowym menu graficznym. Steruje głowicą termoelektryczną grzejnika przez przekaźnik lub stycznik, a wyjście jest aktywne stanem niskim.

| Element | Model |
|---|---|
| Mikrokontroler | WEMOS LOLIN C3 Mini (ESP32-C3) |
| Wyświetlacz | 4" ST7796, 480×320, SPI |
| Dotyk | rezystancyjny, XPT2046 (na tej samej magistrali SPI) |
| Czujnik | SHT31 – temperatura i wilgotność (I²C) |
| Wyjście | GPIO3, aktywne LOW → moduł przekaźnika lub stycznik |

## Połączenia

```
LOLIN C3 Mini            Moduł ST7796 + XPT2046
-------------            ----------------------
5V  ───────────────────  VCC
GND ───────────────────  GND
GPIO10 ────────────────  CS
GPIO1  ────────────────  RESET
GPIO8  ────────────────  DC/RS
GPIO6  ──────────┬─────  SDI (MOSI)
                 └─────  T_DIN
GPIO4  ──────────┬─────  SCK
                 └─────  T_CLK
GPIO21 ────────────────  LED (podświetlenie, PWM)
GPIO5  ────────────────  T_DO        (SDO/MISO wyświetlacza NIE podłączać)
GPIO20 ────────────────  T_CS
                         T_IRQ – wolny

SHT31 (moduł):  VIN → 3V3,  GND → GND,  SDA → GPIO0,  SCL → GPIO2
                ADDR → GND lub wolny (adres 0x44); ADDR → 3V3 = 0x45 (wykrywany automatycznie)
                Moduły mają własne rezystory podciągające 10 kΩ – nie trzeba dodawać.

Przekaźnik (moduł z transoptorem, aktywny LOW):
  VCC   → 3V3          (strona sterująca)
  IN    → GPIO3        + 10 kΩ GPIO3↔3V3 (stan bezpieczny przy starcie)
  GND   → GND
  JD-VCC→ 5V           (zdjęta zworka VCC–JD-VCC, cewka z 5 V)
  COM/NO → obwód głowicy 230 V lub 24 V (albo cewka stycznika)

Zasilanie: zasilacz 5 V min. 1 A na pin 5V płytki (lub USB-C).
```

Dlaczego te piny:
- GPIO2, 8 i 9 to piny startowe ESP32-C3. GPIO8 (DC) i GPIO9 (przycisk BOOT) są bezpieczne, bo przy starcie nic ich nie wymusza. GPIO2 (SCL czujnika) przy starcie musi mieć stan wysoki – zapewniają to rezystory podciągające I²C na module SHT31.
- GPIO18 i 19 to natywne USB, więc nie są używane.
- GPIO3 jest „czysty”: nie jest pinem startowym ani UART i nie zmienia stanu podczas resetu. Razem z rezystorem podciągającym 10 kΩ głowica nie załączy się przy włączaniu zasilania.
- GPIO7 to wbudowana dioda WS2812 (płytka v2.1). Świeci zielono przy czuwaniu, pomarańczowo przy grzaniu i miga na czerwono przy awarii czujnika.

> **Uwaga 230 V:** głowice 230 V i cewki styczników łącz przez przekaźnik lub stycznik z bezpiecznikiem. Cewkę stycznika zabezpiecz gasikiem RC. Prace przy instalacji sieciowej wykonuje osoba z uprawnieniami.

## Funkcje

**Tryby:** AUTO (program tygodniowy), RĘCZNY, EKO, WYŁĄCZONY (zostaje tylko ochrona przed mrozem), URLOP (liczba dni i temperatura, potem automatyczny powrót do AUTO) i BOOST (dogrzewanie na określony czas).

**Program tygodniowy:** do 4 okresów komfortu na dzień z krokiem 15 min. Program dnia można skopiować na dni robocze, weekend albo cały tydzień. Przyciski +/− w trybie AUTO dają zmianę chwilową, która trwa do najbliższej zmiany w programie.

**Regulacja:**
- histereza albo TPI (wypełnienie PWM w cyklu 10–30 min)
- minimalny czas załączenia i wyłączenia, który chroni głowicę i stycznik
- limit maksymalnej temperatury

**Ochrony:**
- ochrona przed mrozem
- wykrywanie otwartego okna (spadek o X °C w Y min wstrzymuje grzanie)
- awaria czujnika: grzanie wyłączone albo praca cykliczna z zadanym procentem
- ochrona zaworu przed zastaniem (cotygodniowe przewietrzenie)

**Wyjście:** logika ujemna lub dodatnia, siłownik NC lub NO, test wyjścia.

**Czujnik SHT31:** temperatura i wilgotność (wilgotność widoczna na ekranie głównym i w panelu WWW), korekta temperatury i wilgotności, filtr wygładzający, kontrola CRC odczytu, automatyczne wykrycie adresu 0x44/0x45 i ponowne wykrywanie po podłączeniu. Czujnik montuj na przewodzie z dala od płytki i wyświetlacza, które się nagrzewają.

**Ekran:**
- motyw ciemny, jasny albo automatyczny (jasny w dzień, ciemny wieczorem, godziny do ustawienia)
- jasność i automatyczne przyciemnianie; dowolny dotyk budzi wygaszony ekran
- blokada rodzicielska (odblokowanie przez przytrzymanie palca 3 s)
- kalibracja dotyku zapisywana w pamięci

**Historia:** wykres 24 h (temperatura, zadana, grzanie), czas grzania dziś, wczoraj i przez 7 dni, liczba załączeń.

**Wi-Fi:**
- wyszukiwanie sieci z poziomu wyświetlacza (Ustawienia → Wi-Fi → Wyszukaj sieci), z siłą sygnału i oznaczeniem sieci zabezpieczonych
- wpisywanie hasła klawiaturą ekranową (litery, wielkie litery, cyfry i wszystkie znaki ASCII); przycisk z okiem pokazuje lub ukrywa hasło, a przy ukrytym haśle ostatni wpisany znak jest widoczny przez sekundę
- ręczne wpisanie nazwy sieci ukrytej („Inna sieć”)
- ekran łączenia z wynikiem: połączono z adresem IP albo błąd z możliwością poprawienia hasła
- czas z NTP (strefa PL z DST), panel WWW pod adresem IP termostatu
- awaryjnie konfiguracja z telefonu (punkt dostępowy `Termostat-XXXX`, hasło `termostat`, adres 192.168.4.1)

Wszystkie ustawienia są zapisywane w pamięci NVS i zostają po zaniku zasilania.

## Kompilacja

**PlatformIO:** otwórz folder i uruchom `pio run -t upload`.

**Arduino IDE (najprościej):** otwórz `arduino/ESP32_ogrzewania/ESP32_ogrzewania.ino`. Cały program jest w tym jednym pliku, a obok leży `czcionki.h` z czcionkami.
1. W Menedżerze płytek zainstaluj „esp32” (Espressif) w wersji 3.x.
2. W Menedżerze bibliotek zainstaluj LovyanGFX (czujnik SHT31 obsługuje kod programu, bez dodatkowej biblioteki).
3. Ustaw: Płytka „LOLIN C3 Mini”, Partition Scheme „No OTA (2MB APP/2MB SPIFFS)”, USB CDC On Boot „Enabled”.
4. Kliknij Wgraj.

Ten szkic jest generowany z katalogu `src/` poleceniem `python3 tools/make_ino.py`.

Przy pierwszym uruchomieniu pojawi się kalibracja dotyku. Ponowną kalibrację uruchomisz z menu Ekran albo przytrzymując przycisk BOOT podczas startu.

Jeśli obraz jest odwrócony, zmień `lcd.setRotation(1)` na `3` w `src/ui.cpp`. Jeśli kolory wyglądają jak negatyw, ustaw `cfg.invert = true` w `src/display.h`.

## Symulacja

`docs/symulacja.html` to symulacja całego menu w przeglądarce, z układem ekranów 1:1 z urządzeniem. Zawiera model pokoju, grzejnika i głowicy, więc regulacja, wykres, wykrywanie okna i awaria czujnika działają jak w urządzeniu.

## Struktura

```
src/main.cpp        pętla główna
src/config.h        piny i stałe
src/display.h       konfiguracja LovyanGFX (ST7796 + XPT2046)
src/settings.*      ustawienia w NVS, program tygodniowy
src/sensor.*        SHT31 (I2C) bez blokowania, z kontrolą CRC
src/thermostat.*    regulator, tryby, ochrony, statystyki
src/params.*        tabela parametrów menu Ustawienia
src/ui.*            ekrany i obsługa dotyku
src/net.*           Wi-Fi, NTP, panel WWW
src/fonts/          czcionki z polskimi znakami (Barlow, licencja OFL)
tools/make_fonts.py generator czcionek
```
