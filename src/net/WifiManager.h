#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

#define AP_SSID    "Kiln-Setup"
#define MDNS_NAME  "kiln"
#define DNS_PORT   53

extern DNSServer   dnsServer;
extern WebServer   server;
extern bool        apMode;
extern bool        lfsOk;
extern String      savedSsid, savedPass, scanJson;

void wifiSetup();
void wifiLoop();
void wifiRegisterBaseRoutes();
bool wifiIsAp();
void wifiScan();
void startAP();
void startSTA();
bool connectWiFi();
void staRetryLoop();
bool bootHeldForReset();
String jsonEscape(const String& s);
void sendJson(const String& body);
