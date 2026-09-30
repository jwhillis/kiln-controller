#include "Mqtt.h"
#include "../config/Settings.h"
#include "../state/SharedState.h"
#include <WiFi.h>
#include <PubSubClient.h>

static WiFiClient net;
static PubSubClient mqtt(net);
static uint32_t lastPub_ = 0;
static String lastAlarm_;

static String topic(const char* suffix) {
    String b = settings().mqttBase;
    if (!b.endsWith("/")) b += '/';
    return b + suffix;
}

static void onMsg(char* t, byte* payload, unsigned int len) {
    String body;
    body.reserve(len);
    for (unsigned i = 0; i < len; i++) body += (char)payload[i];
    String tp = t;
    const Settings& s = settings();
    String base = s.mqttBase;
    if (!base.endsWith("/")) base += '/';

    if (tp == base + "cmd/stop") { cmdPushSimple(CMD_STOP); return; }
    if (tp == base + "cmd/pause") { cmdPushSimple(CMD_PAUSE); return; }
    if (tp == base + "cmd/start") {
        if (!s.mqttRemoteStart) {
            Serial.println("MQTT start ignored (remote start disabled)");
            return;
        }
        String name = body;
        name.trim();
        if (name.startsWith("{")) {
            int i = name.indexOf("profile");
            int q = name.indexOf('"', name.indexOf(':', i));
            int q2 = name.indexOf('"', q + 1);
            if (q >= 0 && q2 > q) name = name.substring(q + 1, q2);
        }
        cmdPushStart(name.c_str());
    }
}

static void ensureConnected() {
    const Settings& s = settings();
    if (!s.mqttEnabled || WiFi.status() != WL_CONNECTED) return;
    if (mqtt.connected()) return;
    mqtt.setServer(s.mqttHost, s.mqttPort);
    mqtt.setCallback(onMsg);
    String avail = topic("availability");
    bool ok;
    if (s.mqttUser[0])
        ok = mqtt.connect(s.mqttClient, s.mqttUser, s.mqttPass, avail.c_str(), 0, true, "offline");
    else
        ok = mqtt.connect(s.mqttClient, avail.c_str(), 0, true, "offline");
    if (ok) {
        mqtt.publish(avail.c_str(), "online", true);
        mqtt.subscribe((topic("cmd/start")).c_str());
        mqtt.subscribe((topic("cmd/stop")).c_str());
        mqtt.subscribe((topic("cmd/pause")).c_str());
        Serial.println("MQTT connected");
    }
}

void mqttBegin() {
    mqtt.setBufferSize(1024);
}

bool mqttConnected() { return mqtt.connected(); }

void mqttPublishAlarm(const char* msg) {
    if (!mqtt.connected() || !msg) return;
    mqtt.publish(topic("alarm").c_str(), msg, true);
}

void mqttLoop() {
    const Settings& s = settings();
    if (!s.mqttEnabled) return;
    ensureConnected();
    if (!mqtt.connected()) return;
    mqtt.loop();

    uint32_t iv = s.mqttIntervalSec * 1000UL;
    if (iv < 1000) iv = 1000;
    if (millis() - lastPub_ < iv) return;
    lastPub_ = millis();

    Snapshot sn = stateRead();
    char buf[512];
    snprintf(buf, sizeof buf,
        "{\"temp\":%.2f,\"setpoint\":%.2f,\"output\":%.1f,\"state\":%u,"
        "\"segment\":%d,\"eta\":%lu,\"kwh\":%.3f,\"faulted\":%s}",
        sn.tempC, sn.setpointC, sn.outputPct, (unsigned)sn.state,
        sn.segmentIndex, (unsigned long)sn.etaUnix, sn.kwh,
        sn.faulted ? "true" : "false");
    mqtt.publish(topic("status").c_str(), buf, true);

    if (sn.faulted && sn.faultMsg[0] && lastAlarm_ != sn.faultMsg) {
        lastAlarm_ = sn.faultMsg;
        mqttPublishAlarm(sn.faultMsg);
    }
}
