#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "Version.h"
#include "config/Settings.h"
#include "state/SharedState.h"
#include "storage/Files.h"
#include "storage/FiringLog.h"
#include "hw/StatusLed.h"
#include "control/ControlTask.h"
#include "net/WifiManager.h"
#include "net/Mqtt.h"

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n\n=== Kiln Controller v%s ===\n", FW_VERSION);

    ledBegin();
    if (bootHeldForReset()) {
        Preferences p;
        p.begin("wifi", false);
        p.clear();
        p.end();
        Serial.println("WiFi credentials cleared.");
    }

    settingsLoad();
    stateInit();
    lfsOk = filesBegin();
    logBegin();
    mqttBegin();

    controlStart();
    wifiSetup();
}

void loop() {
    wifiLoop();
    mqttLoop();
    Snapshot s = stateRead();
    stateSetNet(apMode, apMode ? 0 : WiFi.RSSI(), mqttConnected());
    ledLoop(apMode, s.tcOk, s.faulted, s.firing);
    delay(1);
}
