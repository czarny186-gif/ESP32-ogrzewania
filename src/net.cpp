#include "net.h"
#include "config.h"
#include "settings.h"
#include "thermostat.h"
#include "sensor.h"
#include "clock.h"
#include <WiFi.h>
#include <WebServer.h>

static WebServer server(80);
static NetState state = NET_OFF;
static bool serverStarted = false;
static uint32_t connectStart = 0;
static uint32_t lastRetry = 0;
static const char *AP_PASS = "termostat";

static WifiNet scanRes[WIFI_SCAN_MAX];
static int scanCount = -2;
static bool scanning = false;
static bool connectFailed = false;
static volatile int lastReason = 0;
static bool eventsRegistered = false;

// Moc nadajnika. Wiele płytek ESP32-C3 Mini (m.in. LOLIN C3 Mini v1.0 i klony
// "Super Mini") ma źle dopasowaną antenę: przy domyślnych 19,5 dBm nadajnik
// się przesterowuje, sieci są widoczne, ale połączenie i hotspot nie działają.
static const wifi_power_t TX_POWER[] = {WIFI_POWER_8_5dBm, WIFI_POWER_11dBm, WIFI_POWER_13dBm,
                                        WIFI_POWER_15dBm, WIFI_POWER_17dBm, WIFI_POWER_19_5dBm};
static const char *const TX_NAME[] = {"8,5 dBm", "11 dBm", "13 dBm", "15 dBm", "17 dBm", "19,5 dBm"};

const char *netTxPowerName(uint8_t idx) { return TX_NAME[idx < 6 ? idx : 0]; }

void netApplyTxPower() {
  if (WiFi.getMode() == WIFI_OFF) return;
  bool ok = WiFi.setTxPower(TX_POWER[cfg.wifiTx < 6 ? cfg.wifiTx : 0]);
  Serial.printf("[WiFi] moc nadajnika %s %s\n", netTxPowerName(cfg.wifiTx), ok ? "OK" : "BŁĄD");
}

const char *netReasonText(int r) {
  switch (r) {
    case 0:   return "";
    case 2:   return "uwierzytelnienie wygasło";
    case 14: case 15: case 204: return "nieprawidłowe hasło";
    case 200: return "utracono sygnał sieci";
    case 201: return "nie znaleziono sieci";
    case 202: return "router odrzucił hasło";
    case 203: return "router odrzucił połączenie";
    case 205: return "błąd połączenia";
    case 210: case 211: return "niezgodne zabezpieczenia sieci";
    case 212: return "za słaby sygnał";
    default:  return "błąd połączenia";
  }
}
int netLastReason() { return lastReason; }

static void registerEvents() {
  if (eventsRegistered) return;
  eventsRegistered = true;
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    lastReason = info.wifi_sta_disconnected.reason;
    Serial.printf("[WiFi] rozłączono, kod %d (%s)\n", lastReason, netReasonText(lastReason));
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
    Serial.printf("[WiFi] połączono, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
}

static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="pl"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Termostat</title><style>
body{font-family:system-ui,sans-serif;background:#0e1116;color:#e8ecf1;margin:0;padding:16px;max-width:420px}
.card{background:#1a1f27;border-radius:12px;padding:16px;margin-bottom:12px}
.big{font-size:48px;font-weight:600}.mut{color:#8a94a3;font-size:14px}
button,input,select{font-size:16px;padding:10px;border-radius:8px;border:1px solid #323b48;background:#252c37;color:#e8ecf1}
button{min-width:56px;cursor:pointer}.row{display:flex;gap:8px;align-items:center;margin:8px 0}
input{width:100%;box-sizing:border-box}.heat{color:#ff6b2c}</style></head><body>
<div class="card"><div class="mut">Temperatura</div><div class="big" id="t">--</div><div class="mut" id="hu"></div>
<div id="st" class="mut"></div></div>
<div class="card"><div class="mut">Zadana</div><div class="row"><button onclick="adj(-5)">−</button>
<div class="big" id="sp" style="font-size:32px;flex:1;text-align:center">--</div><button onclick="adj(5)">+</button></div>
<div class="row"><select id="mode" onchange="setMode(this.value)"><option value="0">AUTO</option><option value="1">Ręczny</option>
<option value="2">Eko</option><option value="3">Wyłączony</option><option value="4">Urlop</option></select>
<button onclick="post('/api/boost','')">Boost</button></div></div>
<div class="card"><div class="mut">Sieć Wi-Fi</div><form method="post" action="/wifi">
<div class="row"><input name="ssid" placeholder="Nazwa sieci (SSID)"></div>
<div class="row"><input name="pass" type="password" placeholder="Hasło"></div>
<button type="submit">Zapisz i połącz</button></form></div>
<script>
function post(u,b){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}).then(load)}
function adj(d){post('/api/adjust','d='+d)}function setMode(m){post('/api/mode','m='+m)}
function load(){fetch('/api/status').then(r=>r.json()).then(j=>{
t.textContent=j.fault?'BŁĄD':j.temp.toFixed(1)+' °C';hu.textContent=j.fault?'':'wilgotność '+Math.round(j.hum)+' %';sp.textContent=(j.target/10).toFixed(1)+' °C';
st.innerHTML=(j.heating?'<span class=heat>GRZANIE</span>':'czuwanie')+' · '+j.source;mode.value=j.mode})}
load();setInterval(load,5000);</script></body></html>)HTML";

static void sendStatus() {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"temp\":%.2f,\"hum\":%.1f,\"fault\":%s,\"target\":%d,\"mode\":%u,\"heating\":%s,\"source\":\"%s\",\"boost\":%lu}",
           isnan(sensor.value) ? 0.0f : sensor.value, isnan(sensor.hum) ? 0.0f : sensor.hum, sensor.fault ? "true" : "false", th.target, cfg.mode,
           th.heating ? "true" : "false", thermoSourceName(th.source), (unsigned long)thermoBoostLeftS());
  server.send(200, "application/json", buf);
}

static void startServer() {
  if (serverStarted) return;
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", PAGE); });
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/adjust", HTTP_POST, [] { thermoAdjust(server.arg("d").toInt()); sendStatus(); });
  server.on("/api/mode", HTTP_POST, [] {
    int m = server.arg("m").toInt();
    if (m == MODE_VACATION) thermoStartVacation(); else thermoSetMode(m);
    sendStatus();
  });
  server.on("/api/boost", HTTP_POST, [] { thermoBoost(!th.boost); sendStatus(); });
  server.on("/wifi", HTTP_POST, [] {
    strlcpy(cfg.ssid, server.arg("ssid").c_str(), sizeof(cfg.ssid));
    strlcpy(cfg.pass, server.arg("pass").c_str(), sizeof(cfg.pass));
    cfg.wifiEnable = 1;
    settingsSaveNow();
    server.send(200, "text/html; charset=utf-8",
                "<meta charset=utf-8><body style='font-family:sans-serif'>Zapisano. Termostat łączy się z siecią…</body>");
    delay(500);
    netReconnect();
  });
  server.begin();
  serverStarted = true;
}

String netApName() {
  uint64_t mac = ESP.getEfuseMac();
  char b[24];
  snprintf(b, sizeof(b), "Termostat-%04X", (uint16_t)(mac >> 32));
  return String(b);
}
const char *netApPass() { return AP_PASS; }

void netReconnect() {
  if (!cfg.wifiEnable || cfg.ssid[0] == 0) { netStop(); return; }
  registerEvents();
  WiFi.persistent(false);              // dane sieci trzymamy sami w NVS
  if (WiFi.getMode() != WIFI_STA) {
    WiFi.softAPdisconnect(true);
    WiFi.setHostname("termostat");
    WiFi.mode(WIFI_STA);
  }
  WiFi.disconnect(false, false);
  delay(50);
  netApplyTxPower();                   // przed i po begin – część wersji SDK resetuje moc
  WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);  // także starsze routery WPA/TKIP
  lastReason = 0;
  WiFi.begin(cfg.ssid, cfg.pass);
  netApplyTxPower();
  Serial.printf("[WiFi] łączenie z \"%s\" (hasło: %u znaków)\n", cfg.ssid, (unsigned)strlen(cfg.pass));
  state = NET_CONNECTING;
  connectStart = millis();
}

void netBegin() {
  if (cfg.ntp) clockStartNtp();
  netReconnect();
}

void netStartAP() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  netApplyTxPower();
  bool ok = WiFi.softAP(netApName().c_str(), AP_PASS, 6);   // stały kanał 6
  netApplyTxPower();
  Serial.printf("[WiFi] punkt dostępowy %s: %s, IP %s\n", netApName().c_str(), ok ? "OK" : "BŁĄD",
                WiFi.softAPIP().toString().c_str());
  state = NET_AP;
  startServer();
}

void netStop() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  state = NET_OFF;
}

// ------------------------------------------------------ skanowanie sieci --
void netScanStart() {
  if (scanning) return;
  if (state == NET_CONNECTING) {        // skan przerywa łączenie – zakończ je czysto
    WiFi.disconnect();
    state = NET_OFF;
    lastRetry = millis();
  }
  wifi_mode_t m = WiFi.getMode();
  if (m == WIFI_OFF) WiFi.mode(WIFI_STA);
  else if (m == WIFI_AP) WiFi.mode(WIFI_AP_STA);
  netApplyTxPower();
  WiFi.scanDelete();
  if (WiFi.scanNetworks(true, false) == WIFI_SCAN_FAILED) {
    scanCount = 0;
    return;
  }
  scanning = true;
  scanCount = -1;
}

static void scanPoll() {
  if (!scanning) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  scanning = false;
  int c = 0;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;      // sieci ukryte – przez "Inna sieć"
    int8_t rssi = WiFi.RSSI(i);
    int j;
    for (j = 0; j < c; j++) if (strcmp(scanRes[j].ssid, s.c_str()) == 0) break;
    if (j < c) {                        // ta sama sieć z kilku punktów – zostaw najsilniejszy
      if (rssi > scanRes[j].rssi) scanRes[j].rssi = rssi;
      continue;
    }
    if (c >= WIFI_SCAN_MAX) continue;
    strlcpy(scanRes[c].ssid, s.c_str(), sizeof(scanRes[c].ssid));
    scanRes[c].rssi = rssi;
    scanRes[c].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    c++;
  }
  for (int i = 1; i < c; i++)
    for (int j = i; j > 0 && scanRes[j].rssi > scanRes[j - 1].rssi; j--) std::swap(scanRes[j], scanRes[j - 1]);
  scanCount = c;
  WiFi.scanDelete();
  if (state == NET_OFF && !cfg.wifiEnable) WiFi.mode(WIFI_OFF);
}

int netScanState() { return scanning ? -1 : scanCount; }
const WifiNet *netScanResults() { return scanRes; }
bool netLastFailed() { return connectFailed; }

void netConnectTo(const char *ssid, const char *pass) {
  strlcpy(cfg.ssid, ssid, sizeof(cfg.ssid));
  strlcpy(cfg.pass, pass, sizeof(cfg.pass));
  cfg.wifiEnable = 1;
  settingsSaveNow();
  connectFailed = false;
  netReconnect();
}

void netLoop() {
  if (serverStarted) server.handleClient();
  scanPoll();
  switch (state) {
    case NET_CONNECTING: {
      wl_status_t st = WiFi.status();
      uint32_t el = millis() - connectStart;
      if (st == WL_CONNECTED) {
        state = NET_CONNECTED;
        connectFailed = false;
        if (cfg.ntp) clockStartNtp();
        startServer();
      } else if (el > 25000 || ((st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) && el > 12000)) {
        Serial.printf("[WiFi] nie udało się połączyć (status %d, kod %d)\n", (int)st, lastReason);
        WiFi.disconnect();
        state = NET_OFF;
        connectFailed = true;
        lastRetry = millis();
      }
      break;
    }
    case NET_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) { state = NET_OFF; lastRetry = millis(); }
      break;
    case NET_OFF:
      // ponowna próba co 5 min
      if (cfg.wifiEnable && cfg.ssid[0] && millis() - lastRetry > 300000UL) netReconnect();
      break;
    default: break;
  }
}

NetState netState() { return state; }
String netIP() {
  if (state == NET_AP) return WiFi.softAPIP().toString();
  if (state == NET_CONNECTED) return WiFi.localIP().toString();
  return "-";
}
int netRssi() { return state == NET_CONNECTED ? WiFi.RSSI() : 0; }
