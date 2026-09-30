#include "SharedState.h"

static SemaphoreHandle_t mtx;
static Snapshot snap;
static QueueHandle_t q;

void stateInit() {
    mtx = xSemaphoreCreateMutex();
    q = xQueueCreate(8, sizeof(Command));
}

void statePublish(const Snapshot& s) {
    if (xSemaphoreTake(mtx, pdMS_TO_TICKS(20)) != pdTRUE) return;
    snap = s;
    xSemaphoreGive(mtx);
}

Snapshot stateRead() {
    Snapshot s;
    if (xSemaphoreTake(mtx, pdMS_TO_TICKS(50)) == pdTRUE) {
        s = snap;
        xSemaphoreGive(mtx);
    }
    return s;
}

void stateSetNet(bool ap, int rssi, bool mqtt) {
    if (xSemaphoreTake(mtx, pdMS_TO_TICKS(20)) != pdTRUE) return;
    snap.apMode = ap;
    snap.rssi = rssi;
    snap.mqttOk = mqtt;
    snap.uptimeSec = millis() / 1000;
    snap.heap = ESP.getFreeHeap();
    xSemaphoreGive(mtx);
}

void stateSetEvent(const char* ev) {
    if (!ev) return;
    if (xSemaphoreTake(mtx, pdMS_TO_TICKS(20)) != pdTRUE) return;
    strncpy(snap.lastEvent, ev, sizeof snap.lastEvent - 1);
    snap.lastEvent[sizeof snap.lastEvent - 1] = 0;
    xSemaphoreGive(mtx);
}

bool cmdPush(const Command& c) {
    return xQueueSend(q, &c, 0) == pdTRUE;
}

bool cmdPop(Command& c) {
    return xQueueReceive(q, &c, 0) == pdTRUE;
}

void cmdPushStart(const char* profile, uint32_t delaySec) {
    Command c;
    c.type = delaySec ? CMD_DELAY_START : CMD_START;
    c.delaySec = delaySec;
    if (profile) {
        strncpy(c.profile, profile, sizeof c.profile - 1);
        c.profile[sizeof c.profile - 1] = 0;
    }
    cmdPush(c);
}

void cmdPushSimple(CmdType t) {
    Command c;
    c.type = t;
    cmdPush(c);
}
