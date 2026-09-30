#include "Ota.h"
#include "WifiManager.h"
#include "../control/ControlTask.h"
#include <Update.h>

static bool started_ = false;

void otaRegister(WebServer&) {
    server.on("/api/ota", HTTP_POST,
        []() {
            if (controlBusy()) {
                // already rejected in upload handler; still answer
            }
            if (!started_ || Update.hasError()) {
                server.send(500, "text/plain",
                    started_ ? Update.errorString() : "No firmware received");
            } else {
                server.send(200, "text/plain", "Update complete. Restarting...");
                delay(300);
                ESP.restart();
            }
        },
        []() {
            HTTPUpload& up = server.upload();
            if (up.status == UPLOAD_FILE_START) {
                if (controlBusy()) {
                    started_ = false;
                    Update.abort();
                    return;
                }
                started_ = true;
                Serial.printf("OTA start: %s\n", up.filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
                    Serial.println(Update.errorString());
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (!started_) return;
                if (Update.write(up.buf, up.currentSize) != up.currentSize)
                    Serial.println(Update.errorString());
                delay(5);
            } else if (up.status == UPLOAD_FILE_END) {
                if (started_ && Update.end(true))
                    Serial.printf("OTA done: %u bytes\n", up.totalSize);
                else
                    Serial.println(Update.errorString());
            } else if (up.status == UPLOAD_FILE_ABORTED) {
                Update.abort();
                started_ = false;
            }
        }
    );
}

bool otaFiringBlocked() { return controlBusy(); }
