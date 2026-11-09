// ============================================================================
//  Interfejs graficzny termostatu – 480x320, dotyk XPT2046.
//  Układ ekranów jest 1:1 z symulacją w docs/symulacja.html.
// ============================================================================
#include "ui.h"
#include "display.h"
#include "settings.h"
#include "thermostat.h"
#include "sensor.h"
#include "clock.h"
#include "net.h"
#include "params.h"
#include "fonts/FontS.h"
#include "fonts/FontM.h"
#include "fonts/FontB.h"
#include "fonts/FontXL.h"
#include "fonts/FontBig.h"

LGFX lcd;

// ------------------------------------------------------------- kolory ----
static constexpr uint32_t C_BG      = 0x0E1116;
static constexpr uint32_t C_PANEL   = 0x1A1F27;
static constexpr uint32_t C_PANEL2  = 0x262D38;
static constexpr uint32_t C_LINE    = 0x353E4C;
static constexpr uint32_t C_TEXT    = 0xE8ECF1;
static constexpr uint32_t C_MUTED   = 0x8A94A3;
static constexpr uint32_t C_HEAT    = 0xFF6B2C;
static constexpr uint32_t C_COMFORT = 0xF5A524;
static constexpr uint32_t C_ECO     = 0x4C9EEB;
static constexpr uint32_t C_ECO_DK  = 0x1D3550;
static constexpr uint32_t C_FROST   = 0x5ED6E6;
static constexpr uint32_t C_VAC     = 0x2EC4B6;
static constexpr uint32_t C_BOOST   = 0xFF4FA3;
static constexpr uint32_t C_OK      = 0x3DD68C;
static constexpr uint32_t C_ALARM   = 0xF2545B;

// -------------------------------------------------------------- czcionki --
enum FontId : uint8_t { F_S = 0, F_M, F_B, F_XL, F_BIG };
static const uint8_t ASC[] = {14, 18, 25, 38, 78};
static lgfx::PointerWrapper fontData[5];
static lgfx::VLWfont vfont[5];

static void fontsInit() {
  const uint8_t *src[5] = {FontS, FontM, FontB, FontXL, FontBig};
  const size_t len[5] = {sizeof(FontS), sizeof(FontM), sizeof(FontB), sizeof(FontXL), sizeof(FontBig)};
  for (int i = 0; i < 5; i++) {
    fontData[i].set(src[i], len[i]);
    vfont[i].loadFont(&fontData[i]);
  }
}

enum Align : uint8_t { AL_L = 0, AL_C, AL_R };

static int textW(const char *s, FontId f) {
  lcd.setFont(&vfont[f]);
  return lcd.textWidth(s);
}

// y = górna krawędź tekstu
static int text(int x, int y, const char *s, FontId f, uint32_t fg, uint32_t bg, Align a = AL_L) {
  lcd.setFont(&vfont[f]);
  lcd.setTextColor(fg, bg);
  lcd.setTextDatum(textdatum_t::top_left);
  int w = lcd.textWidth(s);
  int dx = a == AL_C ? x - w / 2 : a == AL_R ? x - w : x;
  lcd.drawString(s, dx, y);
  return w;
}
// cy = środek wysokości wielkich liter
static int textMid(int x, int cy, const char *s, FontId f, uint32_t fg, uint32_t bg, Align a = AL_L) {
  return text(x, cy - ASC[f] / 2, s, f, fg, bg, a);
}

// ------------------------------------------------------------- ikony -----
static void iconFlame(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillCircle(cx, cy + 4, 7, c);
  lcd.fillTriangle(cx - 7, cy + 3, cx + 7, cy + 3, cx + 1, cy - 11, c);
  lcd.fillCircle(cx, cy + 6, 3, bg);
}
static void iconSnow(int cx, int cy, uint32_t c) {
  for (int i = 0; i < 3; i++) {
    float a = i * PI / 3;
    float dx = cos(a) * 9, dy = sin(a) * 9;
    lcd.drawWideLine(cx - dx, cy - dy, cx + dx, cy + dy, 1.2f, c);
  }
}
static void iconWifi(int cx, int cy, int level, uint32_t on, uint32_t off) {
  lcd.fillCircle(cx, cy + 6, 2, level > 0 ? on : off);
  lcd.fillArc(cx, cy + 6, 7, 9, 225, 315, level > 0 ? on : off);
  lcd.fillArc(cx, cy + 6, 12, 14, 225, 315, level > 1 ? on : off);
}
static void iconLock(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillArc(cx, cy - 2, 4, 6, 180, 360, c);
  lcd.fillRect(cx - 6, cy - 2, 2, 2, c);
  lcd.fillRect(cx + 4, cy - 2, 2, 2, c);
  lcd.fillRoundRect(cx - 8, cy, 16, 11, 2, c);
  lcd.fillCircle(cx, cy + 5, 2, bg);
}
static void iconWarn(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillTriangle(cx, cy - 10, cx - 10, cy + 8, cx + 10, cy + 8, c);
  lcd.fillRect(cx - 1, cy - 4, 3, 7, bg);
  lcd.fillRect(cx - 1, cy + 4, 3, 2, bg);
}
static void iconCalendar(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillRoundRect(cx - 10, cy - 8, 20, 18, 3, c);
  lcd.fillRect(cx - 8, cy - 3, 16, 11, bg);
  lcd.fillRect(cx - 6, cy - 12, 3, 6, c);
  lcd.fillRect(cx + 3, cy - 12, 3, 6, c);
  lcd.fillRect(cx - 5, cy, 4, 3, c);
  lcd.fillRect(cx + 1, cy + 3, 4, 3, c);
}
static void iconChart(int cx, int cy, uint32_t c) {
  lcd.fillRect(cx - 10, cy + 2, 5, 8, c);
  lcd.fillRect(cx - 2, cy - 6, 5, 16, c);
  lcd.fillRect(cx + 6, cy - 2, 5, 12, c);
}
static void iconMenu(int cx, int cy, uint32_t c) {
  for (int i = -1; i <= 1; i++) lcd.fillRoundRect(cx - 10, cy + i * 7 - 1, 20, 3, 1, c);
}
static void iconMode(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillCircle(cx, cy, 10, c);
  lcd.fillCircle(cx, cy, 7, bg);
  lcd.fillArc(cx, cy, 0, 7, 270, 90, c);
}
static void iconMoon(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillCircle(cx, cy, 10, c);
  lcd.fillCircle(cx + 6, cy - 4, 9, bg);
}
static void iconThermo(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillRoundRect(cx - 4, cy - 12, 8, 18, 4, c);
  lcd.fillCircle(cx, cy + 7, 6, c);
  lcd.fillRect(cx - 1, cy - 9, 2, 12, bg);
}
static void iconCase(int cx, int cy, uint32_t c, uint32_t bg) {
  lcd.fillRoundRect(cx - 11, cy - 6, 22, 16, 3, c);
  lcd.drawRoundRect(cx - 5, cy - 11, 10, 7, 2, c);
  lcd.fillRect(cx - 11, cy, 22, 2, bg);
}
static void iconWindow(int cx, int cy, uint32_t c) {
  lcd.drawRect(cx - 9, cy - 9, 18, 18, c);
  lcd.drawRect(cx - 8, cy - 8, 16, 16, c);
  lcd.drawFastVLine(cx, cy - 8, 16, c);
  lcd.drawFastHLine(cx - 8, cy, 16, c);
}
static void iconPlus(int cx, int cy, uint32_t c) {
  lcd.fillRect(cx - 12, cy - 2, 24, 4, c);
  lcd.fillRect(cx - 2, cy - 12, 4, 24, c);
}
static void iconMinus(int cx, int cy, uint32_t c) { lcd.fillRect(cx - 12, cy - 2, 24, 4, c); }
static void iconChevron(int cx, int cy, char dir, uint32_t c) {
  // dir: 'l','r','u','d'
  int s = 6;
  if (dir == 'l') { lcd.drawWideLine(cx + s / 2, cy - s, cx - s / 2, cy, 1.5f, c); lcd.drawWideLine(cx - s / 2, cy, cx + s / 2, cy + s, 1.5f, c); }
  if (dir == 'r') { lcd.drawWideLine(cx - s / 2, cy - s, cx + s / 2, cy, 1.5f, c); lcd.drawWideLine(cx + s / 2, cy, cx - s / 2, cy + s, 1.5f, c); }
  if (dir == 'u') { lcd.drawWideLine(cx - s, cy + s / 2, cx, cy - s / 2, 1.5f, c); lcd.drawWideLine(cx, cy - s / 2, cx + s, cy + s / 2, 1.5f, c); }
  if (dir == 'd') { lcd.drawWideLine(cx - s, cy - s / 2, cx, cy + s / 2, 1.5f, c); lcd.drawWideLine(cx, cy + s / 2, cx + s, cy - s / 2, 1.5f, c); }
}
static void iconCheck(int cx, int cy, uint32_t c) {
  lcd.drawWideLine(cx - 6, cy, cx - 2, cy + 4, 1.5f, c);
  lcd.drawWideLine(cx - 2, cy + 4, cx + 6, cy - 5, 1.5f, c);
}

// --------------------------------------------------------------- przyciski
struct Rect { int16_t x, y, w, h; bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; } };

static void button(const Rect &r, const char *label, uint32_t bg, uint32_t fg, FontId f = F_M) {
  lcd.fillRoundRect(r.x, r.y, r.w, r.h, 10, bg);
  if (label) textMid(r.x + r.w / 2, r.y + r.h / 2, label, f, fg, bg, AL_C);
}

// ------------------------------------------------------------ stan UI -----
enum Screen : uint8_t {
  SCR_HOME = 0, SCR_MODE, SCR_VACATION, SCR_PROGRAM, SCR_PERIOD, SCR_COPY,
  SCR_CHART, SCR_MENU, SCR_LIST, SCR_EDIT, SCR_CONFIRM, SCR_AP
};

static Screen scr = SCR_HOME;
static bool needFull = true;
static uint32_t lastInputMs = 0;
static bool dimmed = false;
static bool locked = false;
static bool touchDown = false;
static uint32_t touchStartMs = 0, lastRepeatMs = 0;
static int16_t tx = 0, ty = 0;
static bool wakeTouch = false;      // dotyk wybudzający – ignoruj do puszczenia
static bool calibrateReq = false;

static char toastMsg[48];
static uint32_t toastUntil = 0;

// kontekst ekranów
static uint8_t progDay = 0;         // 0 = poniedziałek
static int8_t periodIdx = -1;       // -1 = nowy
static uint16_t perStart = 0, perEnd = 0;
static uint8_t vacDaysEdit = 7;
static int16_t vacTempEdit = 150;
static uint8_t catIdx = 0, listPage = 0;
static uint8_t paramIdx = 0;
static int editVal = 0;
static const Param *confirmParam = nullptr;
static Screen confirmReturn = SCR_LIST;

static const char *const DAY_SHORT[] = {"Pn", "Wt", "Śr", "Cz", "Pt", "So", "Nd"};
static const char *const DAY_LONG[] = {"Poniedziałek", "Wtorek", "Środa", "Czwartek", "Piątek", "Sobota", "Niedziela"};
static const char *const DAY_ABBR[] = {"pon", "wt", "śr", "czw", "pt", "sob", "nd"};
static const char *const MONTH_ABBR[] = {"sty", "lut", "mar", "kwi", "maj", "cze", "lip", "sie", "wrz", "paź", "lis", "gru"};

static void go(Screen s) { scr = s; needFull = true; }

void uiToast(const char *msg) {
  strlcpy(toastMsg, msg, sizeof(toastMsg));
  toastUntil = millis() + 2000;
  needFull = true;
}
void uiOpenApScreen() { netStartAP(); go(SCR_AP); }
void uiRequestCalibrate() { calibrateReq = true; }

static void fmtTemp(char *b, size_t n, int16_t t10, bool deg = true) {
  int a = abs(t10);
  snprintf(b, n, "%s%d,%d%s", t10 < 0 ? "-" : "", a / 10, a % 10, deg ? "°" : "");
}
static void fmtHM(char *b, size_t n, uint16_t minutes) { snprintf(b, n, "%02u:%02u", minutes / 60, minutes % 60); }

static uint32_t modeColor(uint8_t src) {
  switch (src) {
    case SRC_COMFORT: case SRC_OVERRIDE: case SRC_MANUAL: return C_COMFORT;
    case SRC_ECO: return C_ECO;
    case SRC_FROST: case SRC_WINDOW: return C_FROST;
    case SRC_VACATION: return C_VAC;
    case SRC_BOOST: return C_BOOST;
    case SRC_FAULT: return C_ALARM;
  }
  return C_TEXT;
}

static void header(const char *title, bool back = true) {
  lcd.fillRect(0, 0, SCREEN_W, 48, C_PANEL);
  if (back) iconChevron(26, 24, 'l', C_TEXT);
  text(back ? 56 : 16, 24 - ASC[F_B] / 2, title, F_B, C_TEXT, C_PANEL);
}
static const Rect R_BACK = {0, 0, 120, 48};

// ========================================================== EKRAN GŁÓWNY ===
static const Rect R_PLUS = {324, 56, 132, 56};
static const Rect R_MINUS = {324, 186, 132, 56};
static const Rect R_CHIP = {16, 174, 290, 38};
static const Rect R_NAV[4] = {{16, 262, 108, 50}, {132, 262, 108, 50}, {248, 262, 108, 50}, {364, 262, 108, 50}};

struct HomeCache {
  char time[8], date[24], temp[12], set[12], mode[64], chip[48];
  uint8_t icons;
  uint32_t setColor;
  bool canAdj;
} hc;

static void homeModeLine(char *name, size_t nn, char *rest, size_t nr) {
  struct tm t;
  bool tv = clockLocal(t);
  rest[0] = 0;
  if (th.boost && cfg.mode != MODE_OFF) {
    snprintf(name, nn, "BOOST");
    snprintf(rest, nr, " · jeszcze %lu min", (unsigned long)(thermoBoostLeftS() + 59) / 60);
    return;
  }
  switch (cfg.mode) {
    case MODE_MANUAL: snprintf(name, nn, "RĘCZNY"); snprintf(rest, nr, " · stała temperatura"); break;
    case MODE_ECO: snprintf(name, nn, "EKO"); snprintf(rest, nr, " · obniżona"); break;
    case MODE_OFF: {
      char f[12]; fmtTemp(f, sizeof(f), cfg.tFrost);
      snprintf(name, nn, "WYŁĄCZONY"); snprintf(rest, nr, " · ochrona %s", f); break;
    }
    case MODE_VACATION: {
      snprintf(name, nn, "URLOP");
      if (cfg.vacUntil) {
        time_t u = cfg.vacUntil; struct tm e; localtime_r(&u, &e);
        snprintf(rest, nr, " · do %d %s, %02d:%02d", e.tm_mday, MONTH_ABBR[e.tm_mon], e.tm_hour, e.tm_min);
      }
      break;
    }
    default: {
      snprintf(name, nn, "AUTO");
      if (!tv) { snprintf(rest, nr, " · ustaw zegar!"); break; }
      uint8_t off; uint16_t at;
      const char *what = th.override ? "zmiana chwilowa" : (th.source == SRC_COMFORT ? "komfort" : "eko");
      if (scheduleNextChange(clockDow(t), t.tm_hour * 60 + t.tm_min, off, at)) {
        char hm[8]; fmtHM(hm, sizeof(hm), at);
        snprintf(rest, nr, " · %s do %s%s", what, off == 0 ? "" : off == 1 ? "jutra " : "", hm);
      } else {
        snprintf(rest, nr, " · %s", what);
      }
    }
  }
}

static void homeChip(char *b, size_t n, uint8_t &kind) {
  // kind: 0 czuwanie, 1 grzanie, 2 okno, 3 awaria, 4 ochrona zaworu, 5 test
  if (sensor.fault) {
    kind = 3;
    if (cfg.faultMode == FAULT_DUTY) snprintf(b, n, "Awaria czujnika · praca %u%%", cfg.faultDuty);
    else snprintf(b, n, "Awaria czujnika · grzanie wył.");
  } else if (th.windowOpen) {
    kind = 2;
    snprintf(b, n, "Okno otwarte · %lu min", (unsigned long)((th.windowEndMs - millis()) / 60000 + 1));
  } else if (th.testOutput) { kind = 5; snprintf(b, n, "Test wyjścia"); }
  else if (th.antiBlock) { kind = 4; snprintf(b, n, "Ochrona zaworu"); }
  else if (th.heating) {
    kind = 1;
    if (cfg.algo == ALGO_TPI) snprintf(b, n, "Grzanie · %d%%", (int)lroundf(th.duty * 100));
    else snprintf(b, n, "Grzanie");
  } else { kind = 0; snprintf(b, n, "Czuwanie"); }
}

static void drawHome(bool full) {
  struct tm t;
  bool tv = clockLocal(t);
  char buf[64];

  if (full) {
    lcd.fillScreen(C_BG);
    memset(&hc, 0, sizeof(hc));
    hc.icons = 0xFF;
    text(24, 52, "W pomieszczeniu", F_S, C_MUTED, C_BG);
    lcd.fillRoundRect(316, 48, 148, 202, 14, C_PANEL);
    textMid(390, 128, "Zadana", F_S, C_MUTED, C_PANEL, AL_C);
    const char *labels[4] = {"Tryb", "Program", "Wykres", "Menu"};
    for (int i = 0; i < 4; i++) {
      const Rect &r = R_NAV[i];
      lcd.fillRoundRect(r.x, r.y, r.w, r.h, 10, C_PANEL);
      int cx = r.x + 26, cy = r.y + r.h / 2;
      if (i == 0) iconMode(cx, cy, C_TEXT, C_PANEL);
      if (i == 1) iconCalendar(cx, cy, C_TEXT, C_PANEL);
      if (i == 2) iconChart(cx, cy, C_TEXT);
      if (i == 3) iconMenu(cx, cy, C_TEXT);
      textMid(r.x + 44, cy, labels[i], F_M, C_TEXT, C_PANEL);
    }
  }

  // --- zegar
  if (tv) snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min); else strcpy(buf, "--:--");
  if (strcmp(buf, hc.time)) {
    strcpy(hc.time, buf);
    lcd.fillRect(16, 6, 70, 30, C_BG);
    text(16, 8, buf, F_B, tv ? C_TEXT : C_COMFORT, C_BG);
  }
  if (tv) snprintf(buf, sizeof(buf), "%s, %d %s", DAY_ABBR[clockDow(t)], t.tm_mday, MONTH_ABBR[t.tm_mon]);
  else strcpy(buf, "zegar nieustawiony");
  if (strcmp(buf, hc.date)) {
    strcpy(hc.date, buf);
    lcd.fillRect(90, 6, 200, 30, C_BG);
    text(92, 14, buf, F_S, tv ? C_MUTED : C_COMFORT, C_BG);
  }

  // --- ikony stanu (prawy górny róg)
  NetState ns = netState();
  int wl = ns == NET_CONNECTED ? (netRssi() > -70 ? 2 : 1) : 0;
  uint8_t icons = (uint8_t)(wl | (cfg.wifiEnable ? 4 : 0) | (locked ? 8 : 0) | (cfg.childLock ? 16 : 0) |
                            (sensor.fault ? 32 : 0) | (ns == NET_AP ? 64 : 0));
  if (icons != hc.icons) {
    hc.icons = icons;
    lcd.fillRect(330, 0, 150, 42, C_BG);
    int x = 452;
    if (cfg.wifiEnable) { iconWifi(x, 18, ns == NET_AP ? 2 : wl, ns == NET_AP ? C_VAC : C_TEXT, C_LINE); x -= 34; }
    if (cfg.childLock) { iconLock(x, 18, locked ? C_COMFORT : C_MUTED, C_BG); x -= 30; }
    if (sensor.fault) iconWarn(x, 20, C_ALARM, C_BG);
  }

  // --- temperatura
  if (sensor.fault || isnan(sensor.value)) strcpy(buf, "--,-");
  else fmtTemp(buf, sizeof(buf), (int16_t)lroundf(sensor.value * 10), false);
  if (strcmp(buf, hc.temp)) {
    strcpy(hc.temp, buf);
    lcd.fillRect(16, 72, 296, 96, C_BG);
    uint32_t col = sensor.fault ? C_ALARM : C_TEXT;
    int w = text(20, 78, buf, F_BIG, col, C_BG);
    text(20 + w + 6, 82, "°C", F_B, C_MUTED, C_BG);
  }

  // --- etykieta stanu
  uint8_t kind;
  homeChip(buf, sizeof(buf), kind);
  if (strcmp(buf, hc.chip)) {
    strcpy(hc.chip, buf);
    lcd.fillRect(R_CHIP.x, R_CHIP.y, R_CHIP.w, R_CHIP.h, C_BG);
    uint32_t bg = kind == 1 ? 0x3A2216 : kind == 3 ? 0x3A1A1D : kind == 2 ? 0x173740 : C_PANEL;
    uint32_t fg = kind == 1 ? C_HEAT : kind == 3 ? C_ALARM : kind == 2 ? C_FROST : kind == 0 ? C_MUTED : C_COMFORT;
    int w = textW(buf, F_M) + 52;
    if (w > R_CHIP.w) w = R_CHIP.w;
    lcd.fillRoundRect(R_CHIP.x + 4, R_CHIP.y, w, R_CHIP.h, 19, bg);
    int ix = R_CHIP.x + 24, iy = R_CHIP.y + R_CHIP.h / 2;
    if (kind == 1) iconFlame(ix, iy, C_HEAT, bg);
    else if (kind == 2) iconWindow(ix, iy, C_FROST);
    else if (kind == 3) iconWarn(ix, iy, C_ALARM, bg);
    else lcd.fillCircle(ix, iy, 5, fg);
    textMid(R_CHIP.x + 42, iy, buf, F_M, fg, bg);
  }

  // --- linia trybu
  char name[16], rest[48];
  homeModeLine(name, sizeof(name), rest, sizeof(rest));
  snprintf(buf, sizeof(buf), "%s%s", name, rest);
  if (strcmp(buf, hc.mode)) {
    strcpy(hc.mode, buf);
    lcd.fillRect(16, 218, 298, 32, C_BG);
    uint32_t mc = th.boost ? C_BOOST : cfg.mode == MODE_OFF ? C_FROST : cfg.mode == MODE_VACATION ? C_VAC :
                  cfg.mode == MODE_ECO ? C_ECO : C_COMFORT;
    int w = text(24, 226, name, F_M, mc, C_BG);
    text(24 + w, 226, rest, F_M, C_MUTED, C_BG);
  }

  // --- zadana i przyciski
  bool can = thermoCanAdjust();
  fmtTemp(buf, sizeof(buf), th.target);
  uint32_t sc = modeColor(th.source);
  if (strcmp(buf, hc.set) || sc != hc.setColor || can != hc.canAdj || full) {
    strcpy(hc.set, buf);
    hc.setColor = sc;
    hc.canAdj = can;
    lcd.fillRect(320, 140, 140, 44, C_PANEL);
    textMid(390, 162, buf, F_XL, sc, C_PANEL, AL_C);
    button(R_PLUS, nullptr, C_PANEL2, 0);
    button(R_MINUS, nullptr, C_PANEL2, 0);
    iconPlus(R_PLUS.x + R_PLUS.w / 2, R_PLUS.y + R_PLUS.h / 2, can ? C_TEXT : C_LINE);
    iconMinus(R_MINUS.x + R_MINUS.w / 2, R_MINUS.y + R_MINUS.h / 2, can ? C_TEXT : C_LINE);
  }
}

static void touchHome(int x, int y, bool repeat) {
  if (R_PLUS.hit(x, y) || R_MINUS.hit(x, y)) {
    if (!thermoCanAdjust()) { if (!repeat) uiToast(cfg.mode == MODE_OFF ? "Tryb WYŁĄCZONY – zmień tryb" : "Awaria czujnika"); return; }
    thermoAdjust(R_PLUS.hit(x, y) ? 5 : -5);
    thermoLoop();
    drawHome(false);
    return;
  }
  if (repeat) return;
  if (R_CHIP.hit(x, y) && th.windowOpen) { thermoCancelWindow(); uiToast("Grzanie wznowione"); return; }
  if (R_NAV[0].hit(x, y)) go(SCR_MODE);
  if (R_NAV[1].hit(x, y)) {
    struct tm t;
    progDay = clockLocal(t) ? clockDow(t) : 0;
    go(SCR_PROGRAM);
  }
  if (R_NAV[2].hit(x, y)) go(SCR_CHART);
  if (R_NAV[3].hit(x, y)) go(SCR_MENU);
}

// ============================================================ TRYB PRACY ===
static Rect modeTile(int i) { return Rect{(int16_t)(16 + (i % 3) * 152), (int16_t)(60 + (i / 3) * 102), 144, 94}; }

static void drawMode() {
  lcd.fillScreen(C_BG);
  header("Tryb pracy");
  const char *names[6] = {"AUTO", "RĘCZNY", "EKO", "WYŁĄCZ", "URLOP", "BOOST"};
  char d[6][32];
  strcpy(d[0], "wg programu");
  fmtTemp(d[1], 32, cfg.tManual);
  { char tmp[12]; strcpy(tmp, d[1]); snprintf(d[1], 32, "stała %s", tmp); }
  { char tmp[12]; fmtTemp(tmp, 12, cfg.tEco); snprintf(d[2], 32, "obniżona %s", tmp); }
  { char tmp[12]; fmtTemp(tmp, 12, cfg.tFrost); snprintf(d[3], 32, "tylko ochrona %s", tmp); }
  snprintf(d[4], 32, "nieobecność");
  if (th.boost) snprintf(d[5], 32, "aktywny · %lu min", (unsigned long)(thermoBoostLeftS() + 59) / 60);
  else snprintf(d[5], 32, "%u min", cfg.boostMin);
  const uint32_t col[6] = {C_COMFORT, C_COMFORT, C_ECO, C_FROST, C_VAC, C_BOOST};
  for (int i = 0; i < 6; i++) {
    Rect r = modeTile(i);
    bool sel = (i < 5 && cfg.mode == i) || (i == 5 && th.boost);
    lcd.fillRoundRect(r.x, r.y, r.w, r.h, 12, C_PANEL);
    if (sel) {
      lcd.drawRoundRect(r.x, r.y, r.w, r.h, 12, col[i]);
      lcd.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 11, col[i]);
      lcd.drawRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 10, col[i]);
    }
    int ix = r.x + 26, iy = r.y + 30;
    switch (i) {
      case 0: iconCalendar(ix, iy, col[i], C_PANEL); break;
      case 1: iconThermo(ix, iy, col[i], C_PANEL); break;
      case 2: iconMoon(ix, iy, col[i], C_PANEL); break;
      case 3: iconSnow(ix, iy, col[i]); break;
      case 4: iconCase(ix, iy, col[i], C_PANEL); break;
      case 5: iconFlame(ix, iy, col[i], C_PANEL); break;
    }
    textMid(r.x + 48, iy, names[i], F_B, C_TEXT, C_PANEL);
    text(r.x + 14, r.y + 62, d[i], F_S, C_MUTED, C_PANEL);
    if (sel) iconCheck(r.x + r.w - 20, r.y + 20, col[i]);
  }
  text(240, 278, "Dotknij kafelka, aby zmienić tryb", F_S, C_MUTED, C_BG, AL_C);
}

static void touchMode(int x, int y) {
  if (R_BACK.hit(x, y)) { go(SCR_HOME); return; }
  for (int i = 0; i < 6; i++) {
    if (!modeTile(i).hit(x, y)) continue;
    if (i == 4) { vacDaysEdit = cfg.vacDays; vacTempEdit = cfg.tVacation; go(SCR_VACATION); return; }
    if (i == 5) {
      if (cfg.mode == MODE_OFF) { uiToast("Boost niedostępny w trybie WYŁ."); return; }
      thermoBoost(!th.boost);
      uiToast(th.boost ? "Boost włączony" : "Boost wyłączony");
      go(SCR_HOME);
      return;
    }
    thermoSetMode(i);
    go(SCR_HOME);
    return;
  }
}

// ================================================================= URLOP ===
static const Rect R_VD_M = {232, 66, 64, 56}, R_VD_P = {400, 66, 64, 56};
static const Rect R_VT_M = {232, 136, 64, 56}, R_VT_P = {400, 136, 64, 56};
static const Rect R_BTN_L = {16, 256, 218, 52}, R_BTN_R = {246, 256, 218, 52};

static void drawVacationValues() {
  char b[32];
  lcd.fillRect(298, 66, 100, 126, C_BG);
  snprintf(b, sizeof(b), "%u", vacDaysEdit);
  textMid(348, 94, b, F_XL, C_TEXT, C_BG, AL_C);
  fmtTemp(b, sizeof(b), vacTempEdit);
  textMid(348, 164, b, F_XL, C_VAC, C_BG, AL_C);
  lcd.fillRect(16, 204, 448, 40, C_BG);
  if (clockValid()) {
    time_t u = clockNow() + (time_t)vacDaysEdit * 86400;
    struct tm e; localtime_r(&u, &e);
    snprintf(b, sizeof(b), "Powrót do AUTO: %s %d %s, %02d:%02d", DAY_ABBR[clockDow(e)], e.tm_mday, MONTH_ABBR[e.tm_mon], e.tm_hour, e.tm_min);
    text(240, 214, b, F_S, C_MUTED, C_BG, AL_C);
  } else {
    text(240, 214, "Zegar nieustawiony – urlop do ręcznej zmiany trybu", F_S, C_COMFORT, C_BG, AL_C);
  }
}

static void drawVacation() {
  lcd.fillScreen(C_BG);
  header("Urlop");
  textMid(24, 94, "Liczba dni", F_M, C_TEXT, C_BG);
  textMid(24, 164, "Temperatura", F_M, C_TEXT, C_BG);
  for (const Rect *r : {&R_VD_M, &R_VT_M}) { button(*r, nullptr, C_PANEL2, 0); iconMinus(r->x + r->w / 2, r->y + r->h / 2, C_TEXT); }
  for (const Rect *r : {&R_VD_P, &R_VT_P}) { button(*r, nullptr, C_PANEL2, 0); iconPlus(r->x + r->w / 2, r->y + r->h / 2, C_TEXT); }
  button(R_BTN_L, "Anuluj", C_PANEL2, C_TEXT);
  button(R_BTN_R, "Rozpocznij", C_VAC, C_BG);
  drawVacationValues();
}

static void touchVacation(int x, int y, bool repeat) {
  bool ch = true;
  if (R_VD_M.hit(x, y)) vacDaysEdit = max(1, vacDaysEdit - 1);
  else if (R_VD_P.hit(x, y)) vacDaysEdit = min(60, vacDaysEdit + 1);
  else if (R_VT_M.hit(x, y)) vacTempEdit = max((int)cfg.tFrost, vacTempEdit - 5);
  else if (R_VT_P.hit(x, y)) vacTempEdit = min(250, vacTempEdit + 5);
  else ch = false;
  if (ch) { drawVacationValues(); return; }
  if (repeat) return;
  if (R_BACK.hit(x, y) || R_BTN_L.hit(x, y)) { go(SCR_MODE); return; }
  if (R_BTN_R.hit(x, y)) {
    cfg.vacDays = vacDaysEdit;
    cfg.tVacation = vacTempEdit;
    thermoStartVacation();
    uiToast("Tryb urlopowy włączony");
    go(SCR_HOME);
  }
}

// ======================================================= PROGRAM TYGODNIA ===
static const Rect R_COPY = {372, 6, 96, 36};
static Rect dayTab(int i) { return Rect{(int16_t)(16 + i * 64), 56, 58, 38}; }
static Rect periodRow(int i) { return Rect{16, (int16_t)(158 + i * 38), 448, 34}; }

static void drawTimeline(int x, int y, int w, int h, const DaySchedule &d, int hilite, uint16_t hs, uint16_t he) {
  lcd.fillRoundRect(x, y, w, h, 6, C_ECO_DK);
  for (int i = 0; i < d.count; i++) {
    int x0 = x + (long)d.start[i] * w / 1440, x1 = x + (long)d.end[i] * w / 1440;
    lcd.fillRect(x0, y, max(2, x1 - x0), h, i == hilite ? C_LINE : C_COMFORT);
  }
  if (hilite >= 0 || hilite == -2) {
    int x0 = x + (long)hs * w / 1440, x1 = x + (long)he * w / 1440;
    lcd.fillRect(x0, y, max(2, x1 - x0), h, C_HEAT);
  }
  for (int hh = 3; hh < 24; hh += 3) lcd.drawFastVLine(x + hh * w / 24, y + h - 5, 5, C_BG);
}

static void drawProgram() {
  lcd.fillScreen(C_BG);
  header("Program tygodniowy");
  button(R_COPY, "Kopiuj", C_PANEL2, C_TEXT, F_S);
  struct tm t;
  bool tv = clockLocal(t);
  uint8_t today = tv ? clockDow(t) : 255;
  for (int i = 0; i < 7; i++) {
    Rect r = dayTab(i);
    bool sel = i == progDay;
    uint32_t bg = sel ? C_COMFORT : C_PANEL;
    lcd.fillRoundRect(r.x, r.y, r.w, r.h, 8, bg);
    textMid(r.x + r.w / 2, r.y + r.h / 2, DAY_SHORT[i], F_M, sel ? C_BG : C_TEXT, bg, AL_C);
    if (i == today) lcd.fillRect(r.x + 20, r.y + r.h - 5, 18, 3, sel ? C_BG : C_COMFORT);
  }
  const DaySchedule &d = cfg.week[progDay];
  drawTimeline(16, 102, 448, 26, d, -1, 0, 0);
  if (tv && today == progDay) {
    int nx = 16 + (t.tm_hour * 60 + t.tm_min) * 448 / 1440;
    lcd.fillRect(nx - 1, 98, 3, 34, C_TEXT);
  }
  const char *lbl[] = {"0", "6", "12", "18", "24"};
  for (int i = 0; i < 5; i++) text(16 + i * 112, 134, lbl[i], F_S, C_MUTED, C_BG, i == 0 ? AL_L : i == 4 ? AL_R : AL_C);

  for (int i = 0; i < MAX_PERIODS; i++) {
    Rect r = periodRow(i);
    if (i < d.count) {
      char a[8], b[8], s[24];
      fmtHM(a, sizeof(a), d.start[i]);
      fmtHM(b, sizeof(b), d.end[i]);
      snprintf(s, sizeof(s), "%s – %s", a, b);
      lcd.fillRoundRect(r.x, r.y, r.w, r.h, 8, C_PANEL);
      lcd.fillRoundRect(r.x + 10, r.y + 9, 16, 16, 4, C_COMFORT);
      textMid(r.x + 40, r.y + r.h / 2, s, F_M, C_TEXT, C_PANEL);
      textMid(r.x + r.w - 34, r.y + r.h / 2, "Edytuj", F_S, C_MUTED, C_PANEL, AL_R);
      iconChevron(r.x + r.w - 18, r.y + r.h / 2, 'r', C_MUTED);
    } else if (i == d.count) {
      lcd.drawRoundRect(r.x, r.y, r.w, r.h, 8, C_LINE);
      iconPlus(r.x + 22, r.y + r.h / 2, C_MUTED);
      textMid(r.x + 44, r.y + r.h / 2, d.count == 0 ? "Dodaj okres komfortu (teraz cały dzień Eko)" : "Dodaj okres komfortu",
              F_M, C_MUTED, C_BG);
    }
  }
}

static void touchProgram(int x, int y) {
  if (R_BACK.hit(x, y)) { go(SCR_HOME); return; }
  if (R_COPY.hit(x, y)) { go(SCR_COPY); return; }
  for (int i = 0; i < 7; i++) if (dayTab(i).hit(x, y)) { progDay = i; go(SCR_PROGRAM); return; }
  const DaySchedule &d = cfg.week[progDay];
  for (int i = 0; i < MAX_PERIODS; i++) {
    if (!periodRow(i).hit(x, y)) continue;
    if (i < d.count) { periodIdx = i; perStart = d.start[i]; perEnd = d.end[i]; go(SCR_PERIOD); }
    else if (i == d.count) {
      periodIdx = -1;
      perStart = d.count ? min(1440 - 60, d.end[d.count - 1] + 60) : 17 * 60;
      perEnd = min(1440, perStart + 120);
      go(SCR_PERIOD);
    }
    return;
  }
}

// ------------------------------------------------------- edycja okresu ----
static const Rect R_PS_M = {232, 62, 64, 56}, R_PS_P = {400, 62, 64, 56};
static const Rect R_PE_M = {232, 128, 64, 56}, R_PE_P = {400, 128, 64, 56};
static const Rect R_P_DEL = {16, 256, 140, 52}, R_P_CAN = {170, 256, 140, 52}, R_P_OK = {324, 256, 140, 52};

static void drawPeriodValues() {
  char b[8];
  lcd.fillRect(298, 62, 100, 122, C_BG);
  fmtHM(b, sizeof(b), perStart);
  textMid(348, 90, b, F_XL, C_TEXT, C_BG, AL_C);
  fmtHM(b, sizeof(b), perEnd);
  textMid(348, 156, b, F_XL, C_TEXT, C_BG, AL_C);
  drawTimeline(16, 204, 448, 22, cfg.week[progDay], periodIdx >= 0 ? periodIdx : -2, perStart, perEnd);
}

static void drawPeriod() {
  lcd.fillScreen(C_BG);
  char t[40];
  if (periodIdx >= 0) snprintf(t, sizeof(t), "%s · okres %d", DAY_LONG[progDay], periodIdx + 1);
  else snprintf(t, sizeof(t), "%s · nowy okres", DAY_LONG[progDay]);
  header(t);
  textMid(24, 90, "Początek", F_M, C_TEXT, C_BG);
  textMid(24, 156, "Koniec", F_M, C_TEXT, C_BG);
  for (const Rect *r : {&R_PS_M, &R_PE_M}) { button(*r, nullptr, C_PANEL2, 0); iconMinus(r->x + r->w / 2, r->y + r->h / 2, C_TEXT); }
  for (const Rect *r : {&R_PS_P, &R_PE_P}) { button(*r, nullptr, C_PANEL2, 0); iconPlus(r->x + r->w / 2, r->y + r->h / 2, C_TEXT); }
  text(16, 232, "Krok 15 min · przytrzymaj, aby przewijać", F_S, C_MUTED, C_BG);
  if (periodIdx >= 0) button(R_P_DEL, "Usuń", 0x3A1A1D, C_ALARM);
  button(R_P_CAN, "Anuluj", C_PANEL2, C_TEXT);
  button(R_P_OK, "Zapisz", C_COMFORT, C_BG);
  drawPeriodValues();
}

static void touchPeriod(int x, int y, bool repeat) {
  bool ch = true;
  if (R_PS_M.hit(x, y)) { if (perStart >= 15) perStart -= 15; }
  else if (R_PS_P.hit(x, y)) { if (perStart <= 1440 - 30) { perStart += 15; if (perEnd < perStart + 15) perEnd = perStart + 15; } }
  else if (R_PE_M.hit(x, y)) { if (perEnd >= perStart + 30) perEnd -= 15; }
  else if (R_PE_P.hit(x, y)) { if (perEnd <= 1440 - 15) perEnd += 15; }
  else ch = false;
  if (ch) { drawPeriodValues(); return; }
  if (repeat) return;
  DaySchedule &d = cfg.week[progDay];
  if (R_BACK.hit(x, y) || R_P_CAN.hit(x, y)) { go(SCR_PROGRAM); return; }
  if (R_P_DEL.hit(x, y) && periodIdx >= 0) {
    d.end[periodIdx] = d.start[periodIdx];   // zostanie usunięty przez scheduleSort
    scheduleSort(d);
    settingsSave();
    go(SCR_PROGRAM);
    return;
  }
  if (R_P_OK.hit(x, y)) {
    if (periodIdx < 0) {
      if (d.count >= MAX_PERIODS) { go(SCR_PROGRAM); return; }
      periodIdx = d.count++;
    }
    d.start[periodIdx] = perStart;
    d.end[periodIdx] = perEnd;
    scheduleSort(d);
    settingsSave();
    uiToast("Zapisano");
    go(SCR_PROGRAM);
  }
}

// ---------------------------------------------------------- kopiowanie ----
static const Rect R_CP[3] = {{16, 104, 448, 52}, {16, 166, 448, 52}, {16, 228, 448, 52}};

static void drawCopy() {
  lcd.fillScreen(C_BG);
  char t[40];
  snprintf(t, sizeof(t), "Kopiuj: %s", DAY_LONG[progDay]);
  header(t);
  text(16, 66, "Skopiuj program tego dnia do:", F_M, C_MUTED, C_BG);
  const char *l[3] = {"Dni robocze (Pn–Pt)", "Weekend (So–Nd)", "Wszystkie dni"};
  for (int i = 0; i < 3; i++) button(R_CP[i], l[i], C_PANEL2, C_TEXT);
}

static void touchCopy(int x, int y) {
  if (R_BACK.hit(x, y)) { go(SCR_PROGRAM); return; }
  for (int i = 0; i < 3; i++) {
    if (!R_CP[i].hit(x, y)) continue;
    int from = i == 1 ? 5 : 0, to = i == 0 ? 4 : 6;
    for (int dd = from; dd <= to; dd++) if (dd != progDay) cfg.week[dd] = cfg.week[progDay];
    settingsSave();
    uiToast("Skopiowano");
    go(SCR_PROGRAM);
    return;
  }
}

// ================================================================ WYKRES ===
static void fmtDur(char *b, size_t n, uint32_t s) {
  uint32_t h = s / 3600, m = (s / 60) % 60;
  if (h) snprintf(b, n, "%lu h %lu m", (unsigned long)h, (unsigned long)m);
  else snprintf(b, n, "%lu min", (unsigned long)m);
}

static void drawChart() {
  lcd.fillScreen(C_BG);
  header("Historia 24 h");
  // legenda
  lcd.fillRect(300, 22, 16, 3, C_TEXT);
  text(320, 16, "temp.", F_S, C_MUTED, C_PANEL);
  lcd.fillRect(372, 22, 16, 3, C_COMFORT);
  text(392, 16, "zadana", F_S, C_MUTED, C_PANEL);

  const int X0 = 56, X1 = 464, Y0 = 60, Y1 = 214;
  int n = th.histCount;
  if (n < 2) {
    text(260, 130, "Brak danych – próbka co 5 minut", F_M, C_MUTED, C_BG, AL_C);
  } else {
    int16_t mn = INT16_MAX, mx = INT16_MIN;
    for (int i = 0; i < n; i++) {
      int idx = (th.histHead - n + i + HISTORY_LEN) % HISTORY_LEN;
      int16_t v = th.histTemp[idx], s = th.histTarget[idx];
      if (v != INT16_MIN) { mn = min(mn, v); mx = max(mx, v); }
      mn = min(mn, s); mx = max(mx, s);
    }
    int lo = (mn / 10) - 1, hi = (mx + 9) / 10 + 1;
    if (hi - lo < 4) hi = lo + 4;
    int stepG = (hi - lo) > 8 ? 2 : 1;
    for (int g = lo; g <= hi; g += stepG) {
      int y = Y1 - (long)(g - lo) * (Y1 - Y0) / (hi - lo);
      lcd.drawFastHLine(X0, y, X1 - X0, C_PANEL2);
      char b[8]; snprintf(b, sizeof(b), "%d°", g);
      textMid(X0 - 8, y, b, F_S, C_MUTED, C_BG, AL_R);
    }
    auto px = [&](int i) { return X0 + (long)(HISTORY_LEN - n + i) * (X1 - X0) / (HISTORY_LEN - 1); };
    auto py = [&](int v10) { return Y1 - (long)(v10 - lo * 10) * (Y1 - Y0) / ((hi - lo) * 10); };
    int prevX = -1, prevY = 0, prevS = 0;
    for (int i = 0; i < n; i++) {
      int idx = (th.histHead - n + i + HISTORY_LEN) % HISTORY_LEN;
      int x = px(i);
      int h = th.histHeat[idx];
      if (h > 0) lcd.fillRect(x - 1, 222, 3, 10, h >= 50 ? C_HEAT : 0x7A3A1E);
      int sy = py(th.histTarget[idx]);
      if (i > 0) { lcd.drawFastHLine(px(i - 1), prevS, x - px(i - 1), C_COMFORT); lcd.drawLine(x, prevS, x, sy, C_COMFORT); }
      prevS = sy;
      int16_t v = th.histTemp[idx];
      if (v == INT16_MIN) { prevX = -1; continue; }
      int y = py(v);
      if (prevX >= 0) lcd.drawWideLine(prevX, prevY, x, y, 1.0f, C_TEXT);
      prevX = x; prevY = y;
    }
  }
  const char *xl[] = {"−24 h", "−18 h", "−12 h", "−6 h", "teraz"};
  for (int i = 0; i < 5; i++) text(X0 + i * (X1 - X0) / 4, 236, xl[i], F_S, C_MUTED, C_BG, i == 0 ? AL_L : i == 4 ? AL_R : AL_C);

  char v[4][20];
  fmtDur(v[0], 20, th.heatSecToday);
  fmtDur(v[1], 20, th.heatSecYesterday);
  uint32_t wk = 0; for (int i = 0; i < 7; i++) wk += th.weekMinutes[i];
  fmtDur(v[2], 20, wk * 60);
  snprintf(v[3], 20, "%u", th.cyclesToday);
  const char *lab[4] = {"Grzanie dziś", "Wczoraj", "7 dni", "Załączeń dziś"};
  for (int i = 0; i < 4; i++) {
    Rect r = R_NAV[i];
    lcd.fillRoundRect(r.x, r.y, r.w, r.h, 10, C_PANEL);
    text(r.x + 10, r.y + 6, lab[i], F_S, C_MUTED, C_PANEL);
    text(r.x + 10, r.y + 25, v[i], F_M, C_TEXT, C_PANEL);
  }
}

// ================================================================== MENU ===
static Rect catTile(int i) { return Rect{(int16_t)(16 + (i % 2) * 228), (int16_t)(58 + (i / 2) * 50), 220, 44}; }

static void drawMenu() {
  lcd.fillScreen(C_BG);
  header("Ustawienia");
  for (int i = 0; i < CATEGORY_COUNT; i++) {
    Rect r = catTile(i);
    lcd.fillRoundRect(r.x, r.y, r.w, r.h, 10, C_PANEL);
    textMid(r.x + 16, r.y + r.h / 2, CATEGORIES[i].name, F_M, C_TEXT, C_PANEL);
    iconChevron(r.x + r.w - 20, r.y + r.h / 2, 'r', C_MUTED);
  }
}

static void touchMenu(int x, int y) {
  if (R_BACK.hit(x, y)) { go(SCR_HOME); return; }
  for (int i = 0; i < CATEGORY_COUNT; i++)
    if (catTile(i).hit(x, y)) { catIdx = i; listPage = 0; go(SCR_LIST); return; }
}

// ---------------------------------------------------------- lista param. --
static const int ROWS = 5;
static const Rect R_UP = {16, 274, 120, 40}, R_DN = {344, 274, 120, 40};
static Rect listRow(int i) { return Rect{16, (int16_t)(54 + i * 44), 448, 40}; }

static void drawList() {
  lcd.fillScreen(C_BG);
  const Category &c = CATEGORIES[catIdx];
  header(c.name);
  int pages = (c.count + ROWS - 1) / ROWS;
  if (listPage >= pages) listPage = 0;
  for (int i = 0; i < ROWS; i++) {
    int pi = listPage * ROWS + i;
    if (pi >= c.count) break;
    const Param &p = c.params[pi];
    Rect r = listRow(i);
    lcd.fillRoundRect(r.x, r.y, r.w, r.h, 8, C_PANEL);
    textMid(r.x + 14, r.y + r.h / 2, p.name, F_M, C_TEXT, C_PANEL);
    char v[48];
    int val = (p.type == P_INFO || p.type == P_ACTION) ? 0 : p.get();
    paramFormat(p, val, v, sizeof(v));
    bool editable = p.type != P_INFO;
    int rx = r.x + r.w - (editable ? 34 : 14);
    uint32_t vc = p.type == P_INFO ? C_MUTED : (p.type == P_BOOL ? (val ? C_OK : C_MUTED) : C_COMFORT);
    if (p.type == P_ACTION) vc = C_MUTED;
    textMid(rx, r.y + r.h / 2, v, F_M, vc, C_PANEL, AL_R);
    if (editable) iconChevron(r.x + r.w - 18, r.y + r.h / 2, 'r', C_MUTED);
  }
  if (pages > 1) {
    button(R_UP, nullptr, C_PANEL2, 0); iconChevron(R_UP.x + R_UP.w / 2, R_UP.y + R_UP.h / 2, 'u', listPage > 0 ? C_TEXT : C_LINE);
    button(R_DN, nullptr, C_PANEL2, 0); iconChevron(R_DN.x + R_DN.w / 2, R_DN.y + R_DN.h / 2, 'd', listPage < pages - 1 ? C_TEXT : C_LINE);
    char b[16]; snprintf(b, sizeof(b), "%d / %d", listPage + 1, pages);
    textMid(240, 294, b, F_M, C_MUTED, C_BG, AL_C);
  }
}

static void startConfirm(const Param *p) { confirmParam = p; confirmReturn = scr; go(SCR_CONFIRM); }

static void touchList(int x, int y) {
  if (R_BACK.hit(x, y)) { go(SCR_MENU); return; }
  const Category &c = CATEGORIES[catIdx];
  int pages = (c.count + ROWS - 1) / ROWS;
  if (pages > 1 && R_UP.hit(x, y)) { if (listPage > 0) { listPage--; go(SCR_LIST); } return; }
  if (pages > 1 && R_DN.hit(x, y)) { if (listPage < pages - 1) { listPage++; go(SCR_LIST); } return; }
  for (int i = 0; i < ROWS; i++) {
    int pi = listPage * ROWS + i;
    if (pi >= c.count || !listRow(i).hit(x, y)) continue;
    const Param &p = c.params[pi];
    paramIdx = pi;
    switch (p.type) {
      case P_BOOL: p.set(!p.get()); settingsSave(); go(SCR_LIST); break;
      case P_NUM: case P_ENUM: editVal = p.get(); go(SCR_EDIT); break;
      case P_ACTION: if (p.confirm) startConfirm(&p); else { p.action(); if (scr == SCR_LIST) go(SCR_LIST); } break;
      default: break;
    }
    return;
  }
}

// ---------------------------------------------------------- edycja param. -
static const Rect R_E_M = {16, 70, 112, 84}, R_E_P = {352, 70, 112, 84};

static void wrapText(int x, int y, int w, const char *s, FontId f, uint32_t c, int lineH, int maxLines) {
  char line[96]; int ll = 0; const char *p = s; int lines = 0;
  line[0] = 0;
  while (*p && lines < maxLines) {
    const char *sp = strchr(p, ' ');
    int wl = sp ? (int)(sp - p) : (int)strlen(p);
    char trial[96];
    snprintf(trial, sizeof(trial), "%s%s%.*s", line, ll ? " " : "", wl, p);
    if (textW(trial, f) > w && ll) {
      text(x, y + lines * lineH, line, f, c, C_BG);
      lines++;
      snprintf(line, sizeof(line), "%.*s", wl, p);
    } else {
      strlcpy(line, trial, sizeof(line));
    }
    ll = strlen(line);
    p += wl;
    while (*p == ' ') p++;
  }
  if (ll && lines < maxLines) text(x, y + lines * lineH, line, f, c, C_BG);
}

static void drawEditValue() {
  const Param &p = CATEGORIES[catIdx].params[paramIdx];
  char v[48];
  paramFormat(p, editVal, v, sizeof(v));
  lcd.fillRect(130, 70, 220, 84, C_BG);
  FontId f = textW(v, F_XL) > 216 ? F_B : F_XL;
  textMid(240, 112, v, f, C_COMFORT, C_BG, AL_C);
  iconMinus(R_E_M.x + R_E_M.w / 2, R_E_M.y + R_E_M.h / 2, editVal > p.min ? C_TEXT : C_LINE);
  iconPlus(R_E_P.x + R_E_P.w / 2, R_E_P.y + R_E_P.h / 2, editVal < p.max ? C_TEXT : C_LINE);
}

static void drawEdit() {
  const Param &p = CATEGORIES[catIdx].params[paramIdx];
  lcd.fillScreen(C_BG);
  header(p.name);
  button(R_E_M, nullptr, C_PANEL2, 0);
  button(R_E_P, nullptr, C_PANEL2, 0);
  char a[24], b[24], r[64];
  if (p.type == P_ENUM) snprintf(r, sizeof(r), "Opcje: %d", p.nopts);
  else { paramFormat(p, p.min, a, sizeof(a)); paramFormat(p, p.max, b, sizeof(b)); snprintf(r, sizeof(r), "Zakres: %s … %s", a, b); }
  text(240, 166, r, F_S, C_MUTED, C_BG, AL_C);
  if (p.help) wrapText(24, 192, 432, p.help, F_S, C_TEXT, 20, 3);
  button(R_BTN_L, "Anuluj", C_PANEL2, C_TEXT);
  button(R_BTN_R, "Zapisz", C_COMFORT, C_BG);
  drawEditValue();
}

static void touchEdit(int x, int y, bool repeat, uint32_t heldMs) {
  const Param &p = CATEGORIES[catIdx].params[paramIdx];
  int step = p.step * (heldMs > 2500 && p.type == P_NUM && (p.max - p.min) / p.step > 50 ? 5 : 1);
  if (R_E_M.hit(x, y)) {
    if (p.type == P_ENUM) editVal = editVal > p.min ? editVal - 1 : (repeat ? editVal : p.max);
    else editVal = max((int)p.min, editVal - step);
    drawEditValue(); return;
  }
  if (R_E_P.hit(x, y)) {
    if (p.type == P_ENUM) editVal = editVal < p.max ? editVal + 1 : (repeat ? editVal : p.min);
    else editVal = min((int)p.max, editVal + step);
    drawEditValue(); return;
  }
  if (repeat) return;
  if (R_BACK.hit(x, y) || R_BTN_L.hit(x, y)) { go(SCR_LIST); return; }
  if (R_BTN_R.hit(x, y)) {
    p.set(editVal);
    settingsSave();
    lcd.setBrightness(cfg.brightness * 255 / 100);
    uiToast("Zapisano");
    go(SCR_LIST);
  }
}

// ---------------------------------------------------------- potwierdzenie -
static void drawConfirm() {
  lcd.fillScreen(C_BG);
  header("Potwierdź", false);
  if (confirmParam) {
    textMid(240, 110, confirmParam->name, F_B, C_TEXT, C_BG, AL_C);
    if (confirmParam->help && confirmParam->help[0]) wrapText(40, 150, 400, confirmParam->help, F_S, C_MUTED, 20, 3);
  }
  button(R_BTN_L, "Anuluj", C_PANEL2, C_TEXT);
  button(R_BTN_R, "Wykonaj", C_ALARM, C_BG);
}

static void touchConfirm(int x, int y) {
  if (R_BTN_L.hit(x, y)) { go(confirmReturn); return; }
  if (R_BTN_R.hit(x, y) && confirmParam) {
    go(confirmReturn);
    confirmParam->action();
  }
}

// ---------------------------------------------------------- Wi-Fi AP ------
static void drawAp() {
  lcd.fillScreen(C_BG);
  header("Konfiguracja Wi-Fi", false);
  text(24, 64, "1. Połącz telefon z siecią:", F_M, C_MUTED, C_BG);
  text(48, 90, netApName().c_str(), F_B, C_VAC, C_BG);
  char b[48]; snprintf(b, sizeof(b), "hasło: %s", netApPass());
  text(48, 122, b, F_M, C_TEXT, C_BG);
  text(24, 156, "2. Otwórz w przeglądarce:", F_M, C_MUTED, C_BG);
  snprintf(b, sizeof(b), "http://%s", netIP().c_str());
  text(48, 182, b, F_B, C_VAC, C_BG);
  text(24, 218, "3. Wpisz nazwę i hasło sieci domowej.", F_M, C_MUTED, C_BG);
  button(R_BTN_R, "Zakończ", C_PANEL2, C_TEXT);
}

static void touchAp(int x, int y) {
  if (R_BTN_R.hit(x, y)) { netReconnect(); go(SCR_LIST); }
}

// ================================================================ toast ====
static void drawToast() {
  int w = textW(toastMsg, F_M) + 40;
  int x = (SCREEN_W - w) / 2;
  lcd.fillRoundRect(x, 212, w, 40, 20, C_TEXT);
  textMid(SCREEN_W / 2, 232, toastMsg, F_M, C_BG, C_TEXT, AL_C);
}

// ============================================================= dispatch ===
static void drawScreen() {
  switch (scr) {
    case SCR_HOME: drawHome(true); break;
    case SCR_MODE: drawMode(); break;
    case SCR_VACATION: drawVacation(); break;
    case SCR_PROGRAM: drawProgram(); break;
    case SCR_PERIOD: drawPeriod(); break;
    case SCR_COPY: drawCopy(); break;
    case SCR_CHART: drawChart(); break;
    case SCR_MENU: drawMenu(); break;
    case SCR_LIST: drawList(); break;
    case SCR_EDIT: drawEdit(); break;
    case SCR_CONFIRM: drawConfirm(); break;
    case SCR_AP: drawAp(); break;
  }
}

static void handleTouch(int x, int y, bool repeat, uint32_t heldMs) {
  switch (scr) {
    case SCR_HOME: touchHome(x, y, repeat); break;
    case SCR_MODE: if (!repeat) touchMode(x, y); break;
    case SCR_VACATION: touchVacation(x, y, repeat); break;
    case SCR_PROGRAM: if (!repeat) touchProgram(x, y); break;
    case SCR_PERIOD: touchPeriod(x, y, repeat); break;
    case SCR_COPY: if (!repeat) touchCopy(x, y); break;
    case SCR_CHART: if (!repeat && R_BACK.hit(x, y)) go(SCR_HOME); break;
    case SCR_MENU: if (!repeat) touchMenu(x, y); break;
    case SCR_LIST: if (!repeat) touchList(x, y); break;
    case SCR_EDIT: touchEdit(x, y, repeat, heldMs); break;
    case SCR_CONFIRM: if (!repeat) touchConfirm(x, y); break;
    case SCR_AP: if (!repeat) touchAp(x, y); break;
  }
}

static bool isStepper(int x, int y) {
  switch (scr) {
    case SCR_HOME: return R_PLUS.hit(x, y) || R_MINUS.hit(x, y);
    case SCR_VACATION: return R_VD_M.hit(x, y) || R_VD_P.hit(x, y) || R_VT_M.hit(x, y) || R_VT_P.hit(x, y);
    case SCR_PERIOD: return R_PS_M.hit(x, y) || R_PS_P.hit(x, y) || R_PE_M.hit(x, y) || R_PE_P.hit(x, y);
    case SCR_EDIT: return R_E_M.hit(x, y) || R_E_P.hit(x, y);
    default: return false;
  }
}

void uiWake() {
  lastInputMs = millis();
  if (dimmed) { dimmed = false; lcd.setBrightness(cfg.brightness * 255 / 100); }
}

void uiCalibrateTouch() {
  lcd.fillScreen(C_BG);
  text(240, 130, "Kalibracja dotyku", F_B, C_TEXT, C_BG, AL_C);
  text(240, 170, "Dotykaj kolejno wskazanych rogów", F_M, C_MUTED, C_BG, AL_C);
  delay(1200);
  lcd.calibrateTouch(cfg.touchCal, C_TEXT, C_BG, 24);
  cfg.touchValid = 1;
  settingsSaveNow();
  lcd.setTouchCalibrate(cfg.touchCal);
  touchDown = true;   // zignoruj ostatni dotyk kalibracji
  wakeTouch = true;
  uiToast("Kalibracja zapisana");
}

void uiBegin() {
  lcd.init();
  lcd.setRotation(1);   // poziomo 480x320; jeśli obraz odwrócony: 3
  lcd.setBrightness(cfg.brightness * 255 / 100);
  lcd.fillScreen(C_BG);
  fontsInit();
  if (cfg.touchValid) lcd.setTouchCalibrate(cfg.touchCal);
  else uiCalibrateTouch();
  lastInputMs = millis();
  needFull = true;
}

void uiLoop() {
  uint32_t now = millis();

  if (calibrateReq) { calibrateReq = false; uiCalibrateTouch(); go(SCR_HOME); }

  // ---- dotyk
  int32_t x, y;
  bool pressed = lcd.getTouch(&x, &y) > 0;
  if (pressed) {
    if (!touchDown) {
      touchDown = true;
      touchStartMs = lastRepeatMs = now;
      tx = x; ty = y;
      lastInputMs = now;
      if (dimmed) { uiWake(); wakeTouch = true; if (scr != SCR_HOME) go(SCR_HOME); }
      else if (locked) { uiToast("Zablokowane – przytrzymaj 3 s"); }
      else if (!wakeTouch) handleTouch(x, y, false, 0);
    } else if (!wakeTouch) {
      lastInputMs = now;
      uint32_t held = now - touchStartMs;
      if (locked) {
        if (held > LONG_PRESS_UNLOCK_MS) { locked = false; wakeTouch = true; uiToast("Odblokowano"); }
      } else if (isStepper(tx, ty) && held > 500 && now - lastRepeatMs > 110) {
        lastRepeatMs = now;
        handleTouch(tx, ty, true, held);
      }
    }
  } else if (touchDown) {
    touchDown = false;
    wakeTouch = false;
  }

  // ---- wygaszanie / blokada / powrót do ekranu głównego
  uint32_t idle = now - lastInputMs;
  if (!dimmed && cfg.dimAfter && idle > (uint32_t)cfg.dimAfter * 1000UL) {
    dimmed = true;
    lcd.setBrightness(cfg.dimBrightness * 255 / 100);
    if (cfg.childLock) locked = true;
    if (scr != SCR_HOME && scr != SCR_AP) go(SCR_HOME);
  }
  if (idle > UI_IDLE_HOME_MS && scr != SCR_HOME && scr != SCR_AP) go(SCR_HOME);
  if (!cfg.childLock) locked = false;

  // ---- rysowanie
  if (toastUntil && (int32_t)(now - toastUntil) >= 0) { toastUntil = 0; needFull = true; }
  if (needFull) {
    needFull = false;
    drawScreen();
    if (toastUntil) drawToast();
  } else if (scr == SCR_HOME && !toastUntil) {
    static uint32_t lastHome = 0;
    if (now - lastHome > 500) { lastHome = now; drawHome(false); }
  }
}
