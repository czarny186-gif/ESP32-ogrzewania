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

static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="pl"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Termostat</title><style>
body{font-family:system-ui,sans-serif;background:#0e1116;color:#e8ecf1;margin:0;padding:16px;max-width:420px}
.card{background:#1a1f27;border-radius:12px;padding:16px;margin-bottom:12px}
.big{font-size:48px;font-weight:600}.mut{color:#8a94a3;font-size:14px}
button,input,select{font-size:16px;padding:10px;border-radius:8px;border:1px solid #323b48;background:#252c37;color:#e8ecf1}
button{min-width:56px;cursor:pointer}.row{display:flex;gap:8px;align-items:center;margin:8px 0}
input{width:100%;box-sizing:border-box}.heat{color:#ff6b2c}</style></head><body>
<div class="card"><div class="mut">Temperatura</div><div class="big" id="t">--</div>
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
t.textContent=j.fault?'BŁĄD':j.temp.toFixed(1)+' °C';sp.textContent=(j.target/10).toFixed(1)+' °C';
st.innerHTML=(j.heating?'<span class=heat>GRZANIE</span>':'czuwanie')+' · '+j.source;mode.value=j.mode})}
load();setInterval(load,5000);</script></body></html>)HTML";

static void sendStatus() {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"temp\":%.2f,\"fault\":%s,\"target\":%d,\"mode\":%u,\"heating\":%s,\"source\":\"%s\",\"boost\":%lu}",
           isnan(sensor.value) ? 0.0f : sensor.value, sensor.fault ? "true" : "false", th.target, cfg.mode,
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
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("termostat");
  WiFi.begin(cfg.ssid, cfg.pass);
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
  WiFi.softAP(netApName().c_str(), AP_PASS);
  state = NET_AP;
  startServer();
}

void netStop() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  state = NET_OFF;
}

void netLoop() {
  if (serverStarted) server.handleClient();
  switch (state) {
    case NET_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        state = NET_CONNECTED;
        if (cfg.ntp) clockStartNtp();
        startServer();
      } else if (millis() - connectStart > 20000) {
        WiFi.disconnect();
        state = NET_OFF;
        lastRetry = millis();
      }
      break;
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
