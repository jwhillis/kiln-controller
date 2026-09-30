#include "WifiManager.h"
#include "HttpServer.h"
#include "../hw/StatusLed.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_arduino_version.h>

#define STA_RETRY_MS        60000
#define STA_RETRY_WINDOW_MS 15000
#define BOOT_PIN            0
#define BOOT_HOLD_MS        3000

DNSServer   dnsServer;
WebServer   server(80);
bool        apMode = false;
bool        lfsOk  = false;
String      savedSsid, savedPass;
String      scanJson = "[]";

String jsonEscape(const String& s) {
    String o;
    o.reserve(s.length() + 4);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o;
}

void sendJson(const String& body) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", body);
}

void wifiScan() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    int n = WiFi.scanNetworks();
    Serial.printf("WiFi scan: %d networks\n", n);
    String out = "[";
    int added = 0;
    for (int i = 0; i < n && added < 20; i++) {
        String ssid = WiFi.SSID(i);
        if (!ssid.length()) continue;
        bool dup = false;
        for (int j = 0; j < i; j++) if (WiFi.SSID(j) == ssid) { dup = true; break; }
        if (dup) continue;
        if (added++) out += ',';
        out += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
               ",\"open\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
    }
    out += "]";
    scanJson = out;
    WiFi.scanDelete();
}

static const char CAPTIVE_HTML[] PROGMEM = R"html(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Kiln WiFi Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#1a1a2e;color:#eee;
     display:flex;align-items:center;justify-content:center;min-height:100vh;padding:16px}
.card{background:#16213e;border-radius:12px;padding:32px;width:100%;max-width:400px;
      box-shadow:0 8px 32px rgba(0,0,0,.4)}
h1{font-size:1.4rem;margin-bottom:24px;text-align:center;color:#4fc3f7}
label{display:block;font-size:.85rem;margin-bottom:6px;color:#90caf9}
input,select{width:100%;padding:10px 14px;border-radius:8px;border:1px solid #0f3460;
      background:#0f3460;color:#fff;font-size:1rem;margin-bottom:18px}
button{width:100%;padding:12px;background:#0288d1;border:none;border-radius:8px;
       color:#fff;font-size:1rem;cursor:pointer;font-weight:600}
button:hover{background:#0277bd}
.hint{margin-top:16px;font-size:.85rem;text-align:center;color:#607d8b;line-height:1.5}
.back{display:block;text-align:center;margin-bottom:18px;color:#546e7a;
      text-decoration:none;font-size:.85rem}
.back:hover{color:#90caf9}
.show{display:flex;align-items:center;gap:8px;font-size:.8rem;color:#607d8b;
      margin:-10px 0 18px}
.show input{width:auto;margin:0}
</style></head><body>
<div class="card">
  <a class="back" href="/">&#8592; Use kiln without WiFi</a>
  <h1>&#9832; Kiln WiFi Setup</h1>
  <form method="POST" action="/wifi-save">
    <label>Network (SSID)</label>
    <select id="nets" onchange="if(this.value)ssid.value=this.value">
      <option value="">Scanning&hellip;</option></select>
    <input type="text" id="ssid" name="ssid" placeholder="or type a network name" required>
    <label>Password</label>
    <input type="password" id="pass" name="pass" placeholder="WiFi password">
    <label class="show"><input type="checkbox"
      onchange="pass.type=this.checked?'text':'password'"> Show password</label>
    <button type="submit">Connect &amp; Save</button>
  </form>
  <p class="hint">Device will restart and connect.<br>Then visit <strong>http://kiln.local</strong></p>
</div>
<script>
fetch('/api/scan').then(r=>r.json()).then(l=>{
  const s=document.getElementById('nets');
  s.innerHTML='<option value="">'+(l.length?'Choose a network…':'No networks found')+'</option>'+
    l.map(n=>{const o=document.createElement('option');o.value=n.ssid;
      o.textContent=n.ssid+'  ('+n.rssi+' dBm'+(n.open?', open':'')+')';return o.outerHTML;}).join('');
}).catch(()=>{});
</script>
</body></html>
)html";

static const char SAVED_HTML[] PROGMEM = R"html(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Saved</title>
<style>
body{font-family:-apple-system,sans-serif;background:#1a1a2e;color:#eee;
     display:flex;align-items:center;justify-content:center;min-height:100vh;text-align:center;padding:16px}
h1{color:#4fc3f7;margin-bottom:12px}p{color:#90caf9;line-height:1.6}strong{color:#fff}
</style></head><body>
<div>
  <h1>&#9832; Credentials Saved</h1>
  <p>Device is restarting&hellip;<br>
  Connect to your WiFi and visit<br><strong>http://kiln.local</strong></p>
  <p style="color:#607d8b;font-size:.85rem;margin-top:14px">If it can't connect, the
  <strong>Kiln-Setup</strong> hotspot comes back so you can try again.</p>
</div>
</body></html>
)html";

void wifiRegisterBaseRoutes() {
    server.on("/wifi-setup", HTTP_GET, []() {
        server.send_P(200, "text/html", CAPTIVE_HTML);
    });
    server.on("/wifi-save", HTTP_POST, []() {
        String ssid = server.arg("ssid");
        String pass = server.arg("pass");
        ssid.trim();
        if (ssid.length()) {
            Preferences prefs;
            prefs.begin("wifi", false);
            prefs.putString("ssid", ssid);
            prefs.putString("pass", pass);
            prefs.end();
            server.send_P(200, "text/html", SAVED_HTML);
            delay(1500);
            ESP.restart();
        } else {
            server.sendHeader("Location", "/wifi-setup");
            server.send(302);
        }
    });
    server.on("/wifi-reset", HTTP_GET, []() {
        Preferences prefs;
        prefs.begin("wifi", false);
        prefs.clear();
        prefs.end();
        server.send(200, "text/plain", "WiFi credentials cleared. Restarting...");
        delay(500);
        ESP.restart();
    });
    server.on("/api/scan", HTTP_GET, []() { sendJson(scanJson); });
}

void startAP() {
    apMode = true;
    wifiScan();
    WiFi.mode(WIFI_AP_STA);

    IPAddress apIP(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    WiFi.softAPConfig(apIP, apIP, subnet, IPAddress((uint32_t)0), apIP);
#else
    WiFi.softAPConfig(apIP, apIP, subnet);
#endif
    WiFi.softAP(AP_SSID);
    delay(300);
    Serial.printf("AP started — SSID: \"%s\"  IP: %s\n", AP_SSID, apIP.toString().c_str());

    dnsServer.start(DNS_PORT, "*", apIP);
    httpRegisterRoutes();

    auto redir = []() {
        server.sendHeader("Location", "http://192.168.4.1/wifi-setup", true);
        server.send(302, "text/plain", "");
    };
    server.on("/generate_204",        redir);
    server.on("/gen_204",             redir);
    server.on("/hotspot-detect.html", redir);
    server.on("/library/test/success.html", redir);
    server.on("/ncsi.txt",            redir);
    server.on("/connecttest.txt",     redir);
    server.on("/redirect",            redir);
    server.onNotFound(redir);
    server.begin();
}

bool connectWiFi() {
    Preferences prefs;
    prefs.begin("wifi", true);
    savedSsid = prefs.getString("ssid", "");
    savedPass = prefs.getString("pass", "");
    prefs.end();

    if (!savedSsid.length()) {
        Serial.println("No saved WiFi credentials — captive portal mode");
        return false;
    }

    Serial.printf("Saved SSID found: \"%s\" — connecting", savedSsid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setHostname(MDNS_NAME);
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());

    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
        delay(250);
        if ((millis() - t) % 1000 < 250) Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Connected!  IP: %s  RSSI: %d dBm\n",
            WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    }
    Serial.printf("Connection failed (status %d) — falling back to captive portal\n",
        WiFi.status());
    return false;
}

void startSTA() {
    httpRegisterRoutes();
    server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
    server.begin();

    if (MDNS.begin(MDNS_NAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("mDNS  : http://%s.local\n", MDNS_NAME);
    }
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.printf("Direct: http://%s\n", WiFi.localIP().toString().c_str());
}

void staRetryLoop() {
    static bool     trying   = false;
    static uint32_t tryStart = 0;
    static uint32_t nextTry  = STA_RETRY_MS;
    if (!apMode || !savedSsid.length()) return;

    if (!trying) {
        if (millis() >= nextTry && WiFi.softAPgetStationNum() == 0) {
            Serial.printf("[AP] retrying saved network \"%s\"\n", savedSsid.c_str());
            WiFi.begin(savedSsid.c_str(), savedPass.c_str());
            trying = true;
            tryStart = millis();
        }
    } else if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[AP] saved network is back — restarting into WiFi mode");
        delay(200);
        ESP.restart();
    } else if (millis() - tryStart > STA_RETRY_WINDOW_MS || WiFi.softAPgetStationNum() > 0) {
        WiFi.disconnect();
        trying  = false;
        nextTry = millis() + STA_RETRY_MS;
    }
}

bool bootHeldForReset() {
    pinMode(BOOT_PIN, INPUT_PULLUP);
    if (digitalRead(BOOT_PIN) != LOW) return false;
    Serial.println("BOOT held — keep holding 3 s to clear WiFi credentials");
    uint32_t t = millis();
    while (digitalRead(BOOT_PIN) == LOW) {
        if (millis() - t >= BOOT_HOLD_MS) return true;
        delay(10);
    }
    return false;
}

void wifiSetup() {
    if (connectWiFi()) startSTA();
    else               startAP();
}

void wifiLoop() {
    if (apMode) dnsServer.processNextRequest();
    server.handleClient();
    staRetryLoop();
}

bool wifiIsAp() { return apMode; }
