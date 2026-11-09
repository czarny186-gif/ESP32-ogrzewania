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
void netScanBegin();
int netScanStatus();
String netScanName(int i);
int netScanSignal(int i);
bool netScanSecure(int i);
void netTryCredentials(const String &ssid, const String &password);
bool netCredentialsPending();
bool netCredentialsFailed();
NetState netState();
String netIP();
String netApName();
const char *netApPass();
int netRssi();
