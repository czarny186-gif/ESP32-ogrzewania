#include "params.h"
#include "settings.h"
#include "thermostat.h"
#include "sensor.h"
#include "clock.h"
#include "net.h"
#include "config.h"
#include "display.h"

// --------------------------------------------------------- konstruktory ---
static Param NUM(const char *name, int mn, int mx, int st, uint8_t dec, const char *unit,
                 int (*g)(), void (*s)(int), const char *help, const char *zero = nullptr) {
  Param p{};
  p.name = name; p.type = P_NUM; p.min = mn; p.max = mx; p.step = st; p.dec = dec; p.unit = unit;
  p.get = g; p.set = s; p.help = help; p.zeroText = zero;
  return p;
}
static Param ENUM(const char *name, const char *const *opts, uint8_t n, int (*g)(), void (*s)(int),
                  const char *help) {
  Param p{};
  p.name = name; p.type = P_ENUM; p.min = 0; p.max = n - 1; p.step = 1; p.opts = opts; p.nopts = n;
  p.get = g; p.set = s; p.help = help;
  return p;
}
static Param BOOL(const char *name, int (*g)(), void (*s)(int), const char *help) {
  Param p{};
  p.name = name; p.type = P_BOOL; p.min = 0; p.max = 1; p.step = 1; p.get = g; p.set = s; p.help = help;
  return p;
}
static Param INFO(const char *name, void (*f)(char *, size_t)) {
  Param p{};
  p.name = name; p.type = P_INFO; p.info = f;
  return p;
}
static Param ACTION(const char *name, void (*a)(), bool confirm, const char *help) {
  Param p{};
  p.name = name; p.type = P_ACTION; p.action = a; p.confirm = confirm; p.help = help;
  return p;
}

#define GS(field) [] { return (int)cfg.field; }, [](int v) { cfg.field = v; }
#define COUNT(a) (uint8_t)(sizeof(a) / sizeof(a[0]))

static const char *const O_ALGO[] = {"Histereza", "TPI (PWM)"};
static const char *const O_LOGIC[] = {"Ujemna (LOW)", "Dodatnia (HIGH)"};
static const char *const O_VALVE[] = {"NC (bez napięcia zamkn.)", "NO (bez napięcia otw.)"};
static const char *const O_DAYS[] = {"Poniedziałek", "Wtorek", "Środa", "Czwartek", "Piątek", "Sobota", "Niedziela"};
static const char *const O_FAULT[] = {"Wyłącz grzanie", "Praca cykliczna"};

// ------------------------------------------------------- Temperatury -----
static const Param P_TEMP[] = {
  NUM("Komfort", 50, 300, 1, 1, "°C", GS(tComfort), "Temperatura w okresach komfortu programu tygodniowego."),
  NUM("Eko (obniżona)", 50, 300, 1, 1, "°C", GS(tEco), "Temperatura poza okresami komfortu oraz w trybie EKO."),
  NUM("Tryb ręczny", 50, 300, 1, 1, "°C", GS(tManual), "Stała temperatura w trybie RĘCZNYM."),
  NUM("Urlop", 50, 250, 1, 1, "°C", GS(tVacation), "Temperatura utrzymywana w czasie urlopu."),
  NUM("Ochrona przed mrozem", 30, 120, 1, 1, "°C", GS(tFrost),
      "Minimalna temperatura utrzymywana zawsze – także w trybie WYŁĄCZONY i przy otwartym oknie."),
  NUM("Boost", 150, 300, 1, 1, "°C", GS(tBoost), "Temperatura w czasie szybkiego dogrzewania (BOOST)."),
  NUM("Czas boost", 15, 240, 15, 0, "min", GS(boostMin), "Jak długo trwa BOOST po włączeniu."),
  NUM("Limit maksymalny", 200, 350, 5, 1, "°C", GS(tMax), "Żadna zadana nie przekroczy tej wartości."),
};

// ---------------------------------------------------------- Regulacja ----
static const Param P_REG[] = {
  ENUM("Algorytm", O_ALGO, 2, GS(algo),
       "Histereza: zał./wył. wokół zadanej. TPI: proporcjonalne wypełnienie cyklu – stabilniejsza temperatura."),
  NUM("Histereza", 1, 20, 1, 1, "°C", GS(hyst),
      "Grzanie włącza się przy T < zadana − H/2, wyłącza przy T > zadana + H/2."),
  NUM("Cykl TPI", 10, 30, 5, 0, "min", GS(tpiCycle), "Długość cyklu modulacji TPI."),
  NUM("Pasmo TPI", 5, 40, 1, 1, "°C", GS(tpiBand),
      "Uchyb, przy którym wypełnienie zmienia się od 0 do 100%. Mniejsze = szybsza reakcja."),
  NUM("Min. czas załączenia", 0, 15, 1, 0, "min", GS(minOn),
      "Chroni siłownik i stycznik. Głowica termoelektryczna otwiera się ok. 3 min."),
  NUM("Min. czas wyłączenia", 0, 15, 1, 0, "min", GS(minOff), "Minimalna przerwa między załączeniami."),
};

// -------------------------------------------------------- Wyjście / zawór -
static const Param P_OUT[] = {
  ENUM("Logika wyjścia", O_LOGIC, 2, [] { return cfg.outActiveLow ? 0 : 1; },
       [](int v) { cfg.outActiveLow = v == 0; },
       "Ujemna: stan NISKI na GPIO3 załącza przekaźnik/stycznik (moduły aktywne stanem niskim)."),
  ENUM("Typ siłownika", O_VALVE, 2, GS(valveNO),
       "NC: zasilenie głowicy otwiera zawór (najczęstsze). NO: zasilenie zamyka zawór."),
  BOOL("Ochrona zaworu", GS(antiBlock),
       "Raz w tygodniu otwiera zawór, jeśli nie grzał przez 24 h – zapobiega zastaniu poza sezonem."),
  ENUM("Dzień ochrony", O_DAYS, 7, GS(abDay), "Dzień tygodnia przewietrzenia zaworu."),
  NUM("Godzina ochrony", 0, 23, 1, 0, ":00", GS(abHour), "Godzina przewietrzenia zaworu."),
  NUM("Czas ochrony", 1, 15, 1, 0, "min", GS(abMinutes), "Jak długo zawór pozostaje otwarty."),
};

// ------------------------------------------------------------- Czujnik ---
static void infoRaw(char *b, size_t n) {
  if (isnan(sensor.raw) || sensor.fault) snprintf(b, n, "—");
  else snprintf(b, n, "%.2f °C · %.1f %%", sensor.raw, sensor.humRaw);
}
static void infoRom(char *b, size_t n) {
  if (!sensor.present) snprintf(b, n, "nie wykryto");
  else snprintf(b, n, "SHT31 · 0x%02X", sensor.addr);
}
static const Param P_SENS[] = {
  NUM("Korekta temperatury", -50, 50, 1, 1, "°C", GS(offset),
      "Dodawana do odczytu. Porównaj z termometrem wzorcowym na wysokości 1,5 m."),
  NUM("Korekta wilgotności", -10, 10, 1, 0, "%", GS(humOffset),
      "Dodawana do odczytu wilgotności. Porównaj z higrometrem wzorcowym."),
  NUM("Wygładzanie", 0, 5, 1, 0, "", GS(filter), "Filtr odczytu: 0 = brak, 5 = mocne (wolniejsza reakcja).", "Brak"),
  ENUM("Przy awarii czujnika", O_FAULT, 2, GS(faultMode),
       "Praca cykliczna grzeje przez zadany % czasu – chroni przed wychłodzeniem przy uszkodzonym czujniku."),
  NUM("Wypełnienie awaryjne", 10, 50, 5, 0, "%", GS(faultDuty), "Udział czasu grzania w cyklu 20 min przy awarii."),
  INFO("Odczyt surowy", infoRaw),
  INFO("Czujnik", infoRom),
};

// -------------------------------------------------------- Okno otwarte ---
static const Param P_WIN[] = {
  BOOL("Wykrywanie okna", GS(winEnable),
       "Szybki spadek temperatury wstrzymuje grzanie (utrzymywana jest tylko ochrona przed mrozem)."),
  NUM("Spadek temperatury", 5, 30, 1, 1, "°C", GS(winDrop), "O ile musi spaść temperatura..."),
  NUM("W czasie", 2, 15, 1, 0, "min", GS(winTime), "...w ciągu ilu minut."),
  NUM("Przerwa w grzaniu", 5, 60, 5, 0, "min", GS(winPause), "Czas wstrzymania grzania po wykryciu."),
};

// ---------------------------------------------------------------- Ekran --
static const char *const O_THEME[] = {"Ciemny", "Jasny", "Automatyczny"};
static const Param P_DISP[] = {
  ENUM("Motyw", O_THEME, 3, GS(theme),
       "Kolory menu. Automatyczny: jasny w dzień, ciemny wieczorem i w nocy (godziny poniżej)."),
  NUM("Jasny od", 0, 23, 1, 0, ":00", GS(themeDayFrom), "Motyw automatyczny: od tej godziny menu jest jasne."),
  NUM("Ciemny od", 0, 23, 1, 0, ":00", GS(themeNightFrom), "Motyw automatyczny: od tej godziny menu jest ciemne."),
  NUM("Jasność", 10, 100, 5, 0, "%", GS(brightness), "Jasność podświetlenia podczas obsługi."),
  NUM("Jasność wygaszona", 0, 50, 5, 0, "%", GS(dimBrightness), "Jasność po bezczynności. 0 = ekran wygaszony."),
  NUM("Wygaszanie po", 0, 600, 10, 0, "s", [] { return (int)cfg.dimAfter; }, [](int v) { cfg.dimAfter = v; },
      "Czas bezczynności do przyciemnienia ekranu.", "Nigdy"),
  BOOL("Blokada rodzicielska", GS(childLock),
       "Po wygaszeniu ekran blokuje się. Odblokowanie: przytrzymaj palec 3 s."),
  ACTION("Kalibracja dotyku", [] { uiRequestCalibrate(); }, false, "Dotknij kolejno strzałek w rogach ekranu."),
};

// ---------------------------------------------------------- Data i czas --
static struct tm editTime() {
  struct tm t;
  if (!clockLocal(t)) { t = {}; t.tm_year = 126; t.tm_mon = 0; t.tm_mday = 1; t.tm_hour = 12; }
  return t;
}
static void setPart(int part, int v) {
  struct tm t = editTime();
  int y = t.tm_year + 1900, mo = t.tm_mon + 1, d = t.tm_mday, h = t.tm_hour, mi = t.tm_min;
  switch (part) { case 0: h = v; break; case 1: mi = v; break; case 2: d = v; break; case 3: mo = v; break; case 4: y = v; break; }
  clockSetLocal(y, mo, d, h, mi);
}
static const Param P_CLOCK[] = {
  BOOL("Synchronizacja NTP", GS(ntp), "Automatyczny czas z internetu (wymaga Wi-Fi). Strefa: Polska, z czasem letnim."),
  NUM("Godzina", 0, 23, 1, 0, "", [] { return editTime().tm_hour; }, [](int v) { setPart(0, v); }, "Ustaw godzinę."),
  NUM("Minuta", 0, 59, 1, 0, "", [] { return editTime().tm_min; }, [](int v) { setPart(1, v); }, "Ustaw minutę."),
  NUM("Dzień", 1, 31, 1, 0, "", [] { return editTime().tm_mday; }, [](int v) { setPart(2, v); }, "Dzień miesiąca."),
  NUM("Miesiąc", 1, 12, 1, 0, "", [] { return editTime().tm_mon + 1; }, [](int v) { setPart(3, v); }, "Miesiąc."),
  NUM("Rok", 2024, 2099, 1, 0, "", [] { return editTime().tm_year + 1900; }, [](int v) { setPart(4, v); }, "Rok."),
};

// --------------------------------------------------------------- Wi-Fi ---
static void infoNet(char *b, size_t n) {
  switch (netState()) {
    case NET_CONNECTED:  snprintf(b, n, "Połączono (%d dBm)", netRssi()); break;
    case NET_CONNECTING: snprintf(b, n, "Łączenie…"); break;
    case NET_AP:         snprintf(b, n, "Punkt dostępowy"); break;
    default:             snprintf(b, n, cfg.wifiEnable ? "Brak połączenia" : "Wyłączone");
  }
}
static const char *const O_TXPOW[] = {"8,5 dBm (zalecane)", "11 dBm", "13 dBm", "15 dBm", "17 dBm", "19,5 dBm"};
static const Param P_WIFI[] = {
  BOOL("Wi-Fi", [] { return (int)cfg.wifiEnable; }, [](int v) { cfg.wifiEnable = v; netReconnect(); },
       "Wi-Fi daje czas z NTP i panel WWW pod adresem IP termostatu."),
  ACTION("Wyszukaj sieci", [] { uiOpenWifiScan(); }, false, "Lista sieci w zasięgu – wybierz sieć i wpisz hasło na ekranie."),
  INFO("Stan", infoNet),
  INFO("Sieć", [](char *b, size_t n) { snprintf(b, n, "%s", cfg.ssid[0] ? cfg.ssid : "—"); }),
  INFO("Adres IP", [](char *b, size_t n) { snprintf(b, n, "%s", netIP().c_str()); }),
  ENUM("Moc nadajnika", O_TXPOW, 6, [] { return (int)cfg.wifiTx; }, [](int v) { cfg.wifiTx = v; netApplyTxPower(); },
       "Płytki ESP32-C3 Mini łączą się pewnie tylko przy obniżonej mocy. Zalecane 8,5 dBm; zwiększ, jeśli router jest daleko."),
  ACTION("Konfiguruj przez telefon", [] { uiOpenApScreen(); }, false, "Uruchamia punkt dostępowy z formularzem."),
  ACTION("Zapomnij sieć", [] { cfg.ssid[0] = 0; cfg.pass[0] = 0; settingsSaveNow(); netStop(); uiToast("Usunięto dane sieci"); },
         true, "Usuwa zapisaną nazwę i hasło sieci."),
};

// ---------------------------------------------------------- Informacje ---
static void fmtDuration(char *b, size_t n, uint32_t s) {
  uint32_t d = s / 86400, h = (s / 3600) % 24, m = (s / 60) % 60;
  if (d) snprintf(b, n, "%lu d %lu h %lu min", (unsigned long)d, (unsigned long)h, (unsigned long)m);
  else snprintf(b, n, "%lu h %lu min", (unsigned long)h, (unsigned long)m);
}
static const Param P_INFO_[] = {
  INFO("Wersja", [](char *b, size_t n) { snprintf(b, n, "%s", FW_VERSION); }),
  INFO("Czas pracy", [](char *b, size_t n) { fmtDuration(b, n, millis() / 1000); }),
  INFO("Grzanie łącznie", [](char *b, size_t n) { snprintf(b, n, "%lu h", (unsigned long)(stats.heatSecondsTotal / 3600)); }),
  INFO("Załączeń wyjścia", [](char *b, size_t n) { snprintf(b, n, "%lu", (unsigned long)stats.relayCycles); }),
  INFO("Stan GPIO3", [](char *b, size_t n) {
    snprintf(b, n, "%s (%s)", th.outputLevel ? "HIGH" : "LOW", th.heating ? "grzeje" : "wył.");
  }),
  INFO("Błędy czujnika", [](char *b, size_t n) { snprintf(b, n, "%u", sensor.errors); }),
  INFO("Wolna pamięć", [](char *b, size_t n) { snprintf(b, n, "%lu kB", (unsigned long)(ESP.getFreeHeap() / 1024)); }),
};

// --------------------------------------------------------------- Serwis --
static const Param P_SERV[] = {
  ACTION("Test wyjścia (30 s)", [] { thermoTestOutput(30); uiToast("Wyjście załączone na 30 s"); }, false,
         "Załącza stycznik/głowicę na 30 s."),
  ACTION("Kalibracja dotyku", [] { uiRequestCalibrate(); }, false, ""),
  ACTION("Zeruj statystyki", [] {
    statsReset();
    th.heatSecToday = th.heatSecYesterday = 0; th.cyclesToday = 0;
    memset(th.weekMinutes, 0, sizeof(th.weekMinutes));
    uiToast("Statystyki wyzerowane");
  }, true, "Zeruje liczniki czasu grzania i załączeń."),
  ACTION("Ustawienia fabryczne", [] { settingsFactoryReset(); uiToast("Przywrócono ustawienia"); }, true,
         "Przywraca wszystkie ustawienia i program tygodniowy (kalibracja dotyku zostaje)."),
  ACTION("Uruchom ponownie", [] { settingsSaveNow(); statsSave(); delay(200); ESP.restart(); }, true, ""),
};

const Category CATEGORIES[] = {
  {"Temperatury", P_TEMP, COUNT(P_TEMP)},
  {"Regulacja", P_REG, COUNT(P_REG)},
  {"Wyjście i zawór", P_OUT, COUNT(P_OUT)},
  {"Czujnik", P_SENS, COUNT(P_SENS)},
  {"Okno otwarte", P_WIN, COUNT(P_WIN)},
  {"Ekran", P_DISP, COUNT(P_DISP)},
  {"Data i czas", P_CLOCK, COUNT(P_CLOCK)},
  {"Wi-Fi", P_WIFI, COUNT(P_WIFI)},
  {"Informacje", P_INFO_, COUNT(P_INFO_)},
  {"Serwis", P_SERV, COUNT(P_SERV)},
};
const uint8_t CATEGORY_COUNT = COUNT(CATEGORIES);

void paramFormat(const Param &p, int v, char *buf, size_t n) {
  switch (p.type) {
    case P_BOOL: snprintf(buf, n, "%s", v ? "Wł." : "Wył."); return;
    case P_ENUM: snprintf(buf, n, "%s", (v >= 0 && v < p.nopts) ? p.opts[v] : "?"); return;
    case P_INFO: if (p.info) p.info(buf, n); else buf[0] = 0; return;
    case P_ACTION: buf[0] = 0; return;
    default: break;
  }
  if (v == 0 && p.zeroText) { snprintf(buf, n, "%s", p.zeroText); return; }
  char num[16];
  if (p.dec == 1) {
    int a = abs(v);
    snprintf(num, sizeof(num), "%s%d,%d", v < 0 ? "−" : (p.min < 0 && v > 0 ? "+" : ""), a / 10, a % 10);
  } else {
    snprintf(num, sizeof(num), "%d", v);
  }
  if (p.unit && p.unit[0]) {
    if (p.unit[0] == ':') snprintf(buf, n, "%s%s", num, p.unit);
    else snprintf(buf, n, "%s %s", num, p.unit);
  } else {
    snprintf(buf, n, "%s", num);
  }
}
