#include "thermostat.h"
#include "settings.h"
#include "sensor.h"
#include "clock.h"

ThermoState th;

static uint32_t lastTick = 0;
static uint32_t lastSwitchMs = 0;
static uint32_t lastOnMs = 0;
static bool everOn = false;
static uint32_t cycleStartMs = 0;
static uint32_t cycleOnMs = 0;
static int lastYday = -1;
static int lastAbYday = -1;
static uint16_t histSecs = 0, histHeatSecs = 0;
static uint16_t statsMinuteTicker = 0;

// próbki co minutę do wykrywania otwartego okna (max 15 min)
static float winBuf[16];
static uint8_t winCount = 0;
static uint8_t secToMinute = 0;

static void applyOutput(bool heat) {
  bool energize = cfg.valveNO ? !heat : heat;
  bool level = cfg.outActiveLow ? !energize : energize;
  th.outputLevel = level;
  digitalWrite(PIN_OUTPUT, level ? HIGH : LOW);
}

void thermoBegin() {
  // Stan bezpieczny ZANIM pin stanie się wyjściem (brak "kliknięcia" przy starcie)
  bool heat = false;
  bool energize = cfg.valveNO ? !heat : heat;
  bool level = cfg.outActiveLow ? !energize : energize;
  digitalWrite(PIN_OUTPUT, level ? HIGH : LOW);
  pinMode(PIN_OUTPUT, OUTPUT);
  applyOutput(false);
  lastSwitchMs = millis() - 3600000UL;   // pozwól od razu załączyć
  for (int i = 0; i < HISTORY_LEN; i++) { th.histTemp[i] = INT16_MIN; th.histTarget[i] = 0; th.histHeat[i] = 0; }
}

const char *thermoSourceName(uint8_t src) {
  switch (src) {
    case SRC_COMFORT:  return "Komfort";
    case SRC_ECO:      return "Eko";
    case SRC_MANUAL:   return "Ręczny";
    case SRC_FROST:    return "Ochrona";
    case SRC_VACATION: return "Urlop";
    case SRC_BOOST:    return "Boost";
    case SRC_OVERRIDE: return "Zmiana chwilowa";
    case SRC_WINDOW:   return "Okno otwarte";
    case SRC_FAULT:    return "Awaria";
  }
  return "";
}

// ------------------------------------------------------- zadana efektywna --
static void computeTarget() {
  struct tm t;
  bool tv = clockLocal(t);
  int16_t target;
  uint8_t src;

  // koniec urlopu -> powrót do AUTO
  if (cfg.mode == MODE_VACATION && cfg.vacUntil && tv && clockNow() >= (time_t)cfg.vacUntil) {
    cfg.mode = MODE_AUTO;
    cfg.vacUntil = 0;
    settingsSave();
  }
  // koniec zmiany chwilowej
  if (th.override && th.overrideUntil && tv && clockNow() >= th.overrideUntil) th.override = false;

  switch (cfg.mode) {
    case MODE_MANUAL:   target = cfg.tManual;   src = SRC_MANUAL;   break;
    case MODE_ECO:      target = cfg.tEco;      src = SRC_ECO;      break;
    case MODE_OFF:      target = cfg.tFrost;    src = SRC_FROST;    break;
    case MODE_VACATION: target = cfg.tVacation; src = SRC_VACATION; break;
    default: {
      bool comfort = tv ? scheduleIsComfort(clockDow(t), t.tm_hour * 60 + t.tm_min) : true;
      target = comfort ? cfg.tComfort : cfg.tEco;
      src = comfort ? SRC_COMFORT : SRC_ECO;
      if (th.override) { target = th.overrideTemp; src = SRC_OVERRIDE; }
    }
  }
  if (th.boost) {
    if ((int32_t)(millis() - th.boostEndMs) >= 0) th.boost = false;
    else if (cfg.mode != MODE_OFF) { target = cfg.tBoost; src = SRC_BOOST; }
  }
  if (th.windowOpen) {
    if ((int32_t)(millis() - th.windowEndMs) >= 0) th.windowOpen = false;
    else { target = cfg.tFrost; src = SRC_WINDOW; }
  }
  if (target > cfg.tMax) target = cfg.tMax;
  if (target < cfg.tFrost && cfg.mode != MODE_OFF) target = cfg.tFrost;
  th.target = target;
  th.source = src;
}

// ------------------------------------------------------ wykrywanie okna ----
static void windowDetect() {
  if (!cfg.winEnable || sensor.fault) { winCount = 0; return; }
  if (++secToMinute < 60) return;
  secToMinute = 0;
  if (winCount < 16) winBuf[winCount++] = sensor.value;
  else { memmove(winBuf, winBuf + 1, sizeof(float) * 15); winBuf[15] = sensor.value; }
  uint8_t span = cfg.winTime;
  if (winCount <= span || th.windowOpen) return;
  float past = winBuf[winCount - 1 - span];
  float drop = past - sensor.value;
  if (drop * 10.0f >= cfg.winDrop) {
    th.windowOpen = true;
    th.windowEndMs = millis() + (uint32_t)cfg.winPause * 60000UL;
    winCount = 0;
  }
}

// ----------------------------------------------------------- regulacja -----
static bool regulate() {
  uint32_t now = millis();
  float T = sensor.value;
  float target = th.target / 10.0f;

  if (sensor.fault) {
    th.source = SRC_FAULT;
    if (cfg.faultMode == FAULT_OFF) { th.duty = 0; return false; }
    // praca cykliczna: 20-minutowy cykl z zadanym wypełnieniem
    const uint32_t cyc = 20UL * 60000UL;
    if (now - cycleStartMs >= cyc) cycleStartMs = now;
    th.duty = cfg.faultDuty / 100.0f;
    return (now - cycleStartMs) < (uint32_t)(th.duty * cyc);
  }

  bool on = th.demand;
  bool canSwitchOff = (now - lastSwitchMs) >= (uint32_t)cfg.minOn * 60000UL;
  bool canSwitchOn  = (now - lastSwitchMs) >= (uint32_t)cfg.minOff * 60000UL;

  if (cfg.algo == ALGO_HYST) {
    float h = cfg.hyst / 20.0f;  // połowa histerezy
    if (on && T >= target + h && canSwitchOff) on = false;
    else if (!on && T <= target - h && canSwitchOn) on = true;
    th.duty = on ? 1 : 0;
  } else {
    // TPI: wypełnienie proporcjonalne do uchybu, liczone na początku cyklu
    uint32_t cyc = (uint32_t)cfg.tpiCycle * 60000UL;
    if (now - cycleStartMs >= cyc || cycleStartMs == 0) {
      cycleStartMs = now;
      float band = cfg.tpiBand / 10.0f;
      float d = 0.5f + (target - T) / band;
      d = constrain(d, 0.0f, 1.0f);
      uint32_t onMs = (uint32_t)(d * cyc);
      if (onMs < (uint32_t)cfg.minOn * 60000UL) onMs = 0;
      if (cyc - onMs < (uint32_t)cfg.minOff * 60000UL && onMs > 0) onMs = cyc;
      cycleOnMs = onMs;
      th.duty = (float)onMs / cyc;
    }
    on = (now - cycleStartMs) < cycleOnMs;
    // przegrzanie powyżej pasma – natychmiastowe wyłączenie
    if (T > target + cfg.tpiBand / 20.0f) on = false;
  }
  return on;
}

// -------------------------------------------------- ochrona zaworu --------
static void antiBlockCheck(const struct tm &t, bool tv) {
  uint32_t now = millis();
  if (th.antiBlock && (int32_t)(now - th.antiBlockEndMs) >= 0) th.antiBlock = false;
  if (!cfg.antiBlock || !tv || th.antiBlock) return;
  if (clockDow(t) == cfg.abDay && t.tm_hour == cfg.abHour && t.tm_min == 0 && lastAbYday != t.tm_yday) {
    lastAbYday = t.tm_yday;
    bool idle24h = !everOn || (now - lastOnMs) > 24UL * 3600000UL;
    if (idle24h) {
      th.antiBlock = true;
      th.antiBlockEndMs = now + (uint32_t)cfg.abMinutes * 60000UL;
    }
  }
}

// ---------------------------------------------------------- statystyki ----
static void statsTick(const struct tm &t, bool tv) {
  if (tv) {
    if (lastYday >= 0 && t.tm_yday != lastYday) {
      th.heatSecYesterday = th.heatSecToday;
      th.heatSecToday = 0;
      th.cyclesToday = 0;
      memmove(th.weekMinutes + 1, th.weekMinutes, sizeof(uint16_t) * 6);
      th.weekMinutes[0] = 0;
    }
    lastYday = t.tm_yday;
  }
  if (th.heating) {
    th.heatSecToday++;
    stats.heatSecondsTotal++;
    histHeatSecs++;
  }
  th.weekMinutes[0] = th.heatSecToday / 60;

  if (++histSecs >= HISTORY_PERIOD_S) {
    th.histTemp[th.histHead] = sensor.fault ? INT16_MIN : (int16_t)lroundf(sensor.value * 10);
    th.histTarget[th.histHead] = th.target;
    th.histHeat[th.histHead] = (uint8_t)(histHeatSecs * 100UL / histSecs);
    th.histHead = (th.histHead + 1) % HISTORY_LEN;
    if (th.histCount < HISTORY_LEN) th.histCount++;
    histSecs = histHeatSecs = 0;
  }
  if (++statsMinuteTicker >= 3600) {   // co godzinę zapis statystyk
    statsMinuteTicker = 0;
    stats.uptimeTotal += 60;
    statsSave();
  }
}

void thermoLoop() {
  uint32_t now = millis();
  if (now - lastTick < CONTROL_PERIOD_MS) return;
  lastTick = now;

  struct tm t;
  bool tv = clockLocal(t);

  computeTarget();
  windowDetect();
  if (th.windowOpen && th.source != SRC_WINDOW) computeTarget();
  th.demand = regulate();
  antiBlockCheck(t, tv);

  if (th.testOutput && (int32_t)(now - th.testEndMs) >= 0) th.testOutput = false;
  bool heat = th.demand || th.antiBlock || th.testOutput;

  if (heat != th.heating) {
    th.heating = heat;
    lastSwitchMs = now;
    if (heat) {
      stats.relayCycles++;
      th.cyclesToday++;
      everOn = true;
    }
  }
  if (heat) lastOnMs = now;
  applyOutput(heat);
  statsTick(t, tv);
}

// ---------------------------------------------------- akcje użytkownika ---
void thermoSetMode(uint8_t mode) {
  if (mode >= MODE_COUNT) return;
  cfg.mode = mode;
  th.override = false;
  if (mode != MODE_VACATION) cfg.vacUntil = 0;
  settingsSave();
  lastTick = 0;
}

bool thermoCanAdjust() { return cfg.mode != MODE_OFF && !sensor.fault; }

int16_t thermoAdjustable() {
  if (th.boost) return cfg.tBoost;
  switch (cfg.mode) {
    case MODE_MANUAL:   return cfg.tManual;
    case MODE_ECO:      return cfg.tEco;
    case MODE_VACATION: return cfg.tVacation;
    case MODE_OFF:      return cfg.tFrost;
    default:            return th.override ? th.overrideTemp : th.target;
  }
}

void thermoAdjust(int16_t delta) {
  if (!thermoCanAdjust()) return;
  auto clampT = [](int16_t v) { return (int16_t)constrain(v, 50, (int)cfg.tMax); };
  if (th.boost) { cfg.tBoost = clampT(cfg.tBoost + delta); settingsSave(); }
  else switch (cfg.mode) {
    case MODE_MANUAL:   cfg.tManual = clampT(cfg.tManual + delta); settingsSave(); break;
    case MODE_ECO:      cfg.tEco = clampT(cfg.tEco + delta); settingsSave(); break;
    case MODE_VACATION: cfg.tVacation = clampT(cfg.tVacation + delta); settingsSave(); break;
    default: {
      // AUTO: zmiana chwilowa do najbliższej zmiany programu
      int16_t base = th.override ? th.overrideTemp : th.target;
      th.overrideTemp = clampT(base + delta);
      th.override = true;
      th.overrideUntil = 0;
      struct tm t;
      if (clockLocal(t)) {
        uint8_t off; uint16_t at;
        if (scheduleNextChange(clockDow(t), t.tm_hour * 60 + t.tm_min, off, at)) {
          struct tm e = t;
          e.tm_mday += off;
          e.tm_hour = at / 60; e.tm_min = at % 60; e.tm_sec = 0;
          e.tm_isdst = -1;
          th.overrideUntil = mktime(&e);
        }
      }
    }
  }
  lastTick = 0;  // natychmiastowe przeliczenie
}

void thermoBoost(bool on) {
  th.boost = on;
  if (on) th.boostEndMs = millis() + (uint32_t)cfg.boostMin * 60000UL;
  lastTick = 0;
}

uint32_t thermoBoostLeftS() {
  if (!th.boost) return 0;
  int32_t left = (int32_t)(th.boostEndMs - millis());
  return left > 0 ? left / 1000 : 0;
}

void thermoStartVacation() {
  cfg.mode = MODE_VACATION;
  cfg.vacUntil = clockValid() ? (uint32_t)(clockNow() + (time_t)cfg.vacDays * 86400) : 0;
  th.override = false;
  settingsSave();
  lastTick = 0;
}

void thermoCancelWindow() {
  th.windowOpen = false;
  winCount = 0;
  lastTick = 0;
}

void thermoTestOutput(uint16_t seconds) {
  th.testOutput = seconds > 0;
  th.testEndMs = millis() + seconds * 1000UL;
  lastTick = 0;
}
