// Wi-Fi: połączenie ze skonfigurowaną siecią (czas NTP + panel WWW),
// tryb punktu dostępowego do konfiguracji z telefonu.
#pragma once
#include <Arduino.h>

enum NetState : uint8_t { NET_OFF = 0, NET_CONNECTING, NET_CONNECTED, NET_AP };

void netBegin();
void netLoop();
void netStartAP();
void netStop();
void netReconnect();
NetState netState();
String netIP();
String netApName();
const char *netApPass();
int netRssi();

// --- wyszukiwanie sieci i łączenie z poziomu ekranu
struct WifiNet {
  char   ssid[33];
  int8_t rssi;
  bool   secure;
};
#define WIFI_SCAN_MAX 20
void netScanStart();                 // skanowanie w tle (nie blokuje)
int netScanState();                  // -2 brak wyników, -1 trwa, >= 0 liczba sieci
const WifiNet *netScanResults();     // posortowane od najsilniejszej
void netConnectTo(const char *ssid, const char *pass);  // zapis + łączenie
bool netLastFailed();                // ostatnia próba połączenia nieudana
int netLastReason();                 // kod przyczyny rozłączenia (ESP-IDF), 0 = brak
const char *netReasonText(int reason);
void netApplyTxPower();              // po zmianie ustawienia mocy
const char *netTxPowerName(uint8_t idx);
