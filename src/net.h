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
