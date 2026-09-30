#include "HttpServer.h"
#include "WifiManager.h"
#include "Ota.h"
#include "../Version.h"
#include "../config/Settings.h"
#include "../state/SharedState.h"
#include "../control/ControlTask.h"
#include "../control/Profile.h"
#include "../storage/Files.h"
#include "../storage/FiringLog.h"
#include "../hw/StatusLed.h"
#include <LittleFS.h>
#include <WiFi.h>

extern const char INDEX_HTML[] asm("_binary_web_index_html_start");

static String authToken;
static String uploadPath;
static bool   uploadOk;

static bool authed() {
    if (!settings().adminPass[0]) return true;
    if (!authToken.length()) return false;
    if (server.hasHeader("X-Auth") && server.header("X-Auth") == authToken) return true;
    if (server.hasArg("token") && server.arg("token") == authToken) return true;
    if (server.hasHeader("Cookie")) {
        String c = server.header("Cookie");
        if (c.indexOf("kiln_auth=" + authToken) >= 0) return true;
    }
    return false;
}

static bool requireAuth() {
    if (authed()) return true;
    server.send(401, "text/plain", "auth required");
    return false;
}

static void handleStatus() {
    Snapshot s = stateRead();
    char buf[1400];
    snprintf(buf, sizeof buf,
        "{\"temp\":%.2f,\"setpoint\":%.2f,\"output\":%.1f,\"windowDuty\":%.1f,"
        "\"p\":%.2f,\"i\":%.2f,\"d\":%.2f,\"ff\":%.2f,"
        "\"rate1\":%.1f,\"rate5\":%.1f,\"rateTarget\":%.1f,"
        "\"cj\":%.2f,\"tcOk\":%s,\"tcFault\":\"%s\","
        "\"state\":%u,\"seg\":%d,\"segCount\":%d,\"segType\":\"%s\","
        "\"segRemain\":%lu,\"remain\":%lu,\"elapsed\":%lu,\"eta\":%lu,"
        "\"peak\":%.2f,\"peakHold\":%lu,\"holdBack\":%s,"
        "\"kwh\":%.4f,\"cost\":%.3f,\"firing\":%s,\"paused\":%s,\"faulted\":%s,"
        "\"fault\":\"%s\",\"profile\":\"%s\",\"ap\":%s,\"rssi\":%d,\"mqtt\":%s,"
        "\"uptime\":%lu,\"heap\":%lu,\"useF\":%s,\"inQuartz\":%s,\"inCristo\":%s,"
        "\"event\":\"%s\",\"tuneCycle\":%lu,\"tuneKu\":%.3f,\"tunePu\":%.2f}",
        s.tempC, s.setpointC, s.outputPct, s.windowDuty,
        s.pTerm, s.iTerm, s.dTerm, s.ffTerm,
        s.rateActual1, s.rateActual5, s.rateTarget,
        s.cjTempC, s.tcOk ? "true" : "false", jsonEscape(s.tcFault).c_str(),
        (unsigned)s.state, s.segmentIndex, s.segmentCount, s.segmentType,
        (unsigned long)s.segRemainSec, (unsigned long)s.totalRemainSec,
        (unsigned long)s.elapsedSec, (unsigned long)s.etaUnix,
        s.peakC, (unsigned long)s.peakHoldSec, s.holdBack ? "true" : "false",
        s.kwh, s.cost,
        s.firing ? "true" : "false", s.paused ? "true" : "false", s.faulted ? "true" : "false",
        jsonEscape(s.faultMsg).c_str(), jsonEscape(s.profileName).c_str(),
        s.apMode ? "true" : "false", s.rssi, s.mqttOk ? "true" : "false",
        (unsigned long)s.uptimeSec, (unsigned long)s.heap,
        s.useF ? "true" : "false",
        s.inQuartz ? "true" : "false", s.inCristo ? "true" : "false",
        jsonEscape(s.lastEvent).c_str(),
        (unsigned long)s.tuneCycle, s.tuneKu, s.tunePu);
    sendJson(buf);
}

static void handleInfo() {
    Snapshot s = stateRead();
    char buf[520];
    snprintf(buf, sizeof buf,
        "{\"fw\":\"%s\",\"built\":\"%s %s\",\"ap\":%s,\"ssid\":\"%s\",\"ip\":\"%s\","
        "\"rssi\":%d,\"uptime\":%lu,\"heap\":%u,\"lfs_total\":%u,\"lfs_used\":%u,"
        "\"tc_ok\":%s,\"mqtt\":%s}",
        FW_VERSION, __DATE__, __TIME__,
        apMode ? "true" : "false",
        jsonEscape(apMode ? String(AP_SSID) : WiFi.SSID()).c_str(),
        (apMode ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str(),
        apMode ? 0 : WiFi.RSSI(),
        (unsigned long)(millis() / 1000),
        (unsigned)ESP.getFreeHeap(),
        filesOk() ? (unsigned)(LittleFS.totalBytes() / 1024) : 0u,
        filesOk() ? (unsigned)(LittleFS.usedBytes() / 1024) : 0u,
        s.tcOk ? "true" : "false",
        s.mqttOk ? "true" : "false");
    sendJson(buf);
}

static const char* STORE_KEYS[] = { "ui" };
static bool storeKeyOk(const String& k) {
    for (auto s : STORE_KEYS) if (k == s) return true;
    return false;
}

void httpRegisterRoutes() {
    static const char* hdrs[] = { "Cookie", "X-Auth" };
    server.collectHeaders(hdrs, 2);

    wifiRegisterBaseRoutes();
    otaRegister(server);

    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", INDEX_HTML);
    });
    server.on("/admin", HTTP_GET, []() {
        server.sendHeader("Location", "/#admin", true);
        server.send(302);
    });

    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/history", HTTP_GET, []() { sendJson(histToJson()); });
    server.on("/api/info", HTTP_GET, handleInfo);

    server.on("/api/login", HTTP_POST, []() {
        String pass = server.arg("password");
        if (pass != settings().adminPass) {
            server.send(403, "text/plain", "bad password");
            return;
        }
        authToken = String((uint32_t)esp_random(), HEX) + String((uint32_t)millis(), HEX);
        server.sendHeader("Set-Cookie", "kiln_auth=" + authToken + "; Path=/; SameSite=Lax");
        sendJson("{\"ok\":true,\"token\":\"" + authToken + "\"}");
    });
    server.on("/api/auth", HTTP_GET, []() {
        sendJson(String("{\"ok\":") + (authed() ? "true" : "false") + "}");
    });

    server.on("/api/fire/start", HTTP_POST, []() {
        if (controlBusy()) { server.send(409, "text/plain", "already running"); return; }
        String name = server.arg("profile");
        uint32_t delay = server.arg("delay").toInt();
        if (!name.length()) { server.send(400, "text/plain", "profile required"); return; }
        cmdPushStart(name.c_str(), delay);
        server.send(200, "text/plain", "OK");
    });
    server.on("/api/fire/stop", HTTP_POST, []() { cmdPushSimple(CMD_STOP); server.send(200, "text/plain", "OK"); });
    server.on("/api/fire/pause", HTTP_POST, []() { cmdPushSimple(CMD_PAUSE); server.send(200, "text/plain", "OK"); });
    server.on("/api/fire/resume", HTTP_POST, []() { cmdPushSimple(CMD_RESUME); server.send(200, "text/plain", "OK"); });
    server.on("/api/fire/skip", HTTP_POST, []() { cmdPushSimple(CMD_SKIP); server.send(200, "text/plain", "OK"); });

    server.on("/api/autotune/start", HTTP_POST, []() {
        if (!requireAuth()) return;
        if (controlBusy()) { server.send(409, "text/plain", "busy"); return; }
        Command c;
        c.type = CMD_TUNE_START;
        c.tuneTargetC = server.arg("target").length() ? server.arg("target").toFloat() : 500.0f;
        cmdPush(c);
        server.send(200, "text/plain", "OK");
    });
    server.on("/api/autotune/stop", HTTP_POST, []() {
        if (!requireAuth()) return;
        cmdPushSimple(CMD_TUNE_STOP);
        server.send(200, "text/plain", "OK");
    });
    server.on("/api/autotune/accept", HTTP_POST, []() {
        if (!requireAuth()) return;
        cmdPushSimple(CMD_TUNE_ACCEPT);
        server.send(200, "text/plain", "OK");
    });
    server.on("/api/autotune/reject", HTTP_POST, []() {
        if (!requireAuth()) return;
        cmdPushSimple(CMD_TUNE_REJECT);
        server.send(200, "text/plain", "OK");
    });

    server.on("/api/profiles", HTTP_GET, []() { sendJson(filesListProfilesJson()); });
    server.on("/api/profiles/get", HTTP_GET, []() {
        Profile p;
        String err;
        if (!filesLoadProfile(server.arg("name"), p, err)) {
            server.send(404, "text/plain", err);
            return;
        }
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "application/json", p.toJson());
    });
    server.on("/api/profiles", HTTP_POST, []() {
        String body = server.arg("plain");
        if (!body.length()) body = server.arg("data");
        Profile p;
        String err;
        if (!p.fromJson(body, err)) { server.send(400, "text/plain", err); return; }
        if (!filesSaveProfile(p, err)) { server.send(500, "text/plain", err); return; }
        server.send(200, "text/plain", "OK");
    });
    server.on("/api/profiles/delete", HTTP_POST, []() {
        if (!filesDeleteProfile(server.arg("name"))) {
            server.send(404, "text/plain", "not found");
            return;
        }
        server.send(200, "text/plain", "OK");
    });

    server.on("/api/settings", HTTP_GET, []() {
        if (!requireAuth()) return;
        sendJson(settingsToJson());
    });
    server.on("/api/settings", HTTP_POST, []() {
        if (!requireAuth()) return;
        String body = server.arg("plain");
        if (!body.length()) body = server.arg("data");
        String err;
        if (!settingsFromJson(body, err)) { server.send(400, "text/plain", err); return; }
        controlApplyPidGains();
        server.send(200, "text/plain", "OK");
    });

    server.on("/api/files", HTTP_GET, []() {
        if (!requireAuth()) return;
        String dir = server.arg("dir");
        if (dir != "/docs" && dir != "/logs" && dir != "/profiles") dir = "/docs";
        sendJson(filesListJson(dir.c_str()));
    });
    server.on("/api/files/delete", HTTP_POST, []() {
        if (!requireAuth()) return;
        if (!filesDelete(server.arg("path"))) { server.send(400, "text/plain", "cannot delete"); return; }
        server.send(200, "text/plain", "OK");
    });
    server.on("/api/files/download", HTTP_GET, []() {
        String path = server.arg("path");
        if (!path.startsWith("/docs/") && !path.startsWith("/logs/") && !path.startsWith("/profiles/")) {
            server.send(403, "text/plain", "bad path");
            return;
        }
        if (!LittleFS.exists(path)) { server.send(404, "text/plain", "not found"); return; }
        File f = LittleFS.open(path, "r");
        String ct = "application/octet-stream";
        if (path.endsWith(".pdf")) ct = "application/pdf";
        else if (path.endsWith(".json")) ct = "application/json";
        else if (path.endsWith(".csv")) ct = "text/csv";
        else if (path.endsWith(".txt")) ct = "text/plain";
        server.streamFile(f, ct);
        f.close();
    });
    server.on("/api/files/upload", HTTP_POST,
        []() {
            if (!requireAuth()) return;
            server.send(uploadOk ? 200 : 500, "text/plain", uploadOk ? "OK" : "upload failed");
        },
        []() {
            HTTPUpload& up = server.upload();
            if (up.status == UPLOAD_FILE_START) {
                if (!authed() || controlBusy()) { uploadOk = false; return; }
                String name = up.filename;
                int slash = name.lastIndexOf('/');
                if (slash >= 0) name = name.substring(slash + 1);
                uploadPath = "/docs/" + name;
                File f = LittleFS.open(uploadPath, "w");
                uploadOk = (bool)f;
                if (f) f.close();
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (!uploadOk) return;
                File f = LittleFS.open(uploadPath, "a");
                if (!f || f.write(up.buf, up.currentSize) != up.currentSize) uploadOk = false;
                if (f) f.close();
            }
        }
    );

    server.on("/api/logs", HTTP_GET, []() { sendJson(logsListJson()); });

    server.on("/api/store", HTTP_GET, []() {
        String key = server.arg("key");
        if (!storeKeyOk(key)) { server.send(400, "text/plain", "bad key"); return; }
        String path = "/" + key + ".json";
        if (!filesOk() || !LittleFS.exists(path)) { sendJson("{}"); return; }
        File f = LittleFS.open(path, "r");
        server.sendHeader("Cache-Control", "no-store");
        server.streamFile(f, "application/json");
        f.close();
    });
    server.on("/api/store", HTTP_POST, []() {
        String key = server.arg("key");
        String data = server.arg("data");
        if (!storeKeyOk(key)) { server.send(400, "text/plain", "bad key"); return; }
        File f = LittleFS.open("/" + key + ".json", "w");
        if (!f) { server.send(500, "text/plain", "write failed"); return; }
        f.print(data);
        f.close();
        server.send(200, "text/plain", "OK");
    });

    server.on("/api/led", HTTP_GET, []() {
        sendJson("{\"pin\":" + String(ledPin()) + "}");
    });
    server.on("/api/led", HTTP_POST, []() {
        if (!requireAuth()) return;
        int pin = server.arg("pin").toInt();
        if (pin < 1 || pin > 48 || pin == 19 || pin == 20 || !GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
            server.send(400, "text/plain", "invalid pin");
            return;
        }
        ledSetPin((uint8_t)pin);
        ledTest(3000);
        server.send(200, "text/plain", "LED on GPIO " + String(pin));
    });

    server.on("/api/reboot", HTTP_POST, []() {
        if (!requireAuth()) return;
        server.send(200, "text/plain", "rebooting");
        delay(300);
        ESP.restart();
    });
    server.on("/api/factory", HTTP_POST, []() {
        if (!requireAuth()) return;
        settingsFactoryReset();
        server.send(200, "text/plain", "reset");
        delay(300);
        ESP.restart();
    });
}

void httpBegin() {}
