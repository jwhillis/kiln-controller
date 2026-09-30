#include "ControlTask.h"
#include "Pid.h"
#include "AutoTune.h"
#include "ProfileEngine.h"
#include "Safety.h"
#include "../hw/TempSensor.h"
#include "../hw/SsrOutput.h"
#include "../config/Settings.h"
#include "../state/SharedState.h"
#include "../storage/Files.h"
#include "../storage/FiringLog.h"
#include <esp_task_wdt.h>

static TempSensor sensor;
static SsrOutput  ssr;
static Pid        pid;
static AutoTune   tuner;
static ProfileEngine engine;
static SafetyMonitor safety;
static TaskHandle_t task_ = nullptr;
static volatile bool busy_ = false;

static const int RATE_N = 320;
static float    rateT_[RATE_N];
static uint32_t rateMs_[RATE_N];
static int      rateI_ = 0, rateN_ = 0;

static void ratePush(float t) {
    uint32_t now = millis();
    rateT_[rateI_] = t;
    rateMs_[rateI_] = now;
    rateI_ = (rateI_ + 1) % RATE_N;
    if (rateN_ < RATE_N) rateN_++;
}

static float rateOver(uint32_t windowMs) {
    if (rateN_ < 4) return 0;
    uint32_t now = millis();
    int last = (rateI_ - 1 + RATE_N) % RATE_N;
    float t1 = rateT_[last];
    uint32_t m1 = rateMs_[last];
    float t0 = t1;
    uint32_t m0 = m1;
    for (int i = 0; i < rateN_; i++) {
        int idx = (rateI_ - 1 - i + RATE_N * 4) % RATE_N;
        if (now - rateMs_[idx] >= windowMs) { t0 = rateT_[idx]; m0 = rateMs_[idx]; break; }
        t0 = rateT_[idx];
        m0 = rateMs_[idx];
    }
    float dtH = (m1 - m0) / 3600000.0f;
    if (dtH < 1.0f / 3600.0f) return 0;
    return (t1 - t0) / dtH;
}

bool controlBusy() { return busy_; }

void controlApplyPidGains() {
    const Settings& s = settings();
    pid.setGains(s.kp, s.ki, s.kd, s.kf);
}

void controlAcceptTune() {
    TuneResult r = tuner.result();
    if (r.ku <= 0) return;
    Settings& s = settings();
    s.kp = r.tlKp;
    s.ki = r.tlKi;
    s.kd = r.tlKd;
    settingsSave();
    controlApplyPidGains();
}

static bool loadAndStart(const char* name, float tempC, uint32_t delaySec) {
    Profile p;
    String err;
    if (!filesLoadProfile(name, p, err)) {
        Serial.printf("Start failed: %s\n", err.c_str());
        stateSetEvent("Could not load profile");
        return false;
    }
    if (!engine.start(p, tempC, delaySec)) return false;
    pid.reset(0);
    safety.reset();
    logStart(p.name);
    histClear();
    stateSetEvent("Firing started");
    return true;
}

static void faultTrip(const char* reason, Snapshot& snap) {
    ssr.forceOff();
    engine.stop();
    logFinish();
    snap.faulted = true;
    snap.firing = false;
    snap.state = ST_FAULT;
    strncpy(snap.faultMsg, reason, sizeof snap.faultMsg - 1);
    snap.outputPct = 0;
    stateSetEvent(reason);
    Serial.printf("FAULT: %s\n", reason);
}

static float tuneTargetC_ = 500;

static void controlLoop(void*) {
    esp_task_wdt_add(NULL);
    sensor.begin();
    ssr.begin();
    controlApplyPidGains();
    safety.reset();

    // Give the TC a moment, then try power-loss resume.
    vTaskDelay(pdMS_TO_TICKS(1500));
    SensorReading first = sensor.read();
    if (first.ok) engine.resumeIfPossible(first.tempC);

    Snapshot snap;
    float peakC = NAN;
    uint32_t peakHold = 0;
    float kwh = 0;
    uint32_t lastMs = millis();
    TuneResult lastTune;

    for (;;) {
        uint32_t now = millis();
        float dt = (now - lastMs) / 1000.0f;
        if (dt <= 0) dt = settings().controlMs / 1000.0f;
        lastMs = now;

        SensorReading rd = sensor.read();
        Command cmd;
        while (cmdPop(cmd)) {
            switch (cmd.type) {
                case CMD_START:
                case CMD_DELAY_START:
                    if (rd.ok) loadAndStart(cmd.profile, rd.tempC, cmd.delaySec);
                    break;
                case CMD_STOP:
                    engine.stop();
                    tuner.stop();
                    ssr.setDuty(0);
                    pid.reset(0);
                    logFinish();
                    stateSetEvent("Stopped");
                    break;
                case CMD_PAUSE:  engine.pause(); stateSetEvent("Paused"); break;
                case CMD_RESUME: engine.resume(); stateSetEvent("Resumed"); break;
                case CMD_SKIP:   engine.skip(); stateSetEvent("Skipped segment"); break;
                case CMD_TUNE_START:
                    if (rd.ok) {
                        engine.stop();
                        tuneTargetC_ = cmd.tuneTargetC > 0 ? cmd.tuneTargetC : 500.0f;
                        tuner.start(tuneTargetC_);
                        pid.reset(0);
                        stateSetEvent("Autotune started");
                    }
                    break;
                case CMD_TUNE_STOP:
                    tuner.stop();
                    ssr.setDuty(0);
                    stateSetEvent("Autotune stopped");
                    break;
                case CMD_TUNE_ACCEPT:
                    controlAcceptTune();
                    tuner.stop();
                    stateSetEvent("Autotune gains saved (Tyreus-Luyben)");
                    break;
                case CMD_TUNE_REJECT:
                    tuner.stop();
                    stateSetEvent("Autotune discarded");
                    break;
                case CMD_MANUAL:
                    if (!engine.active() && !tuner.running()) {
                        pid.setManual(cmd.manualPct);
                        ssr.setDuty(cmd.manualPct);
                    }
                    break;
                default: break;
            }
        }

        EngineTick et;
        float out = 0;
        PidTerms terms;
        TuneResult tr = tuner.result();

        if (tuner.running()) {
            tr = tuner.update(rd.ok ? rd.tempC : NAN, dt);
            lastTune = tr;
            out = rd.ok ? tr.output : 0;
            snap.state = ST_AUTOTUNE;
            strncpy(snap.segmentType, "autotune", sizeof snap.segmentType - 1);
        } else if (engine.active()) {
            et = engine.tick(rd.ok ? rd.tempC : NAN, dt);
            if (et.state == ST_DELAYED || et.state == ST_PAUSE || et.state == ST_COOL || et.finished) {
                out = 0;
                pid.reset(0);
            } else if (rd.ok) {
                terms = pid.compute(et.setpointC, rd.tempC, et.rateTarget, dt);
                out = terms.output;
            }
            if (et.finished) {
                logFinish();
                stateSetEvent("Firing complete");
            }
        } else {
            out = 0;
        }

        SafetyStatus saf = safety.check(rd.ok ? rd.tempC : NAN, rd.ok, rd.fault,
                                        out, engine.active() && !et.finished, et.elapsedSec);
        if (saf.tripped) {
            faultTrip(saf.reason, snap);
            out = 0;
        } else if (!tuner.running()) {
            ssr.setDuty(out);
        } else {
            ssr.setDuty(out);
        }
        ssr.service();

        if (rd.ok) ratePush(rd.tempC);
        if (rd.ok && (!isfinite(peakC) || rd.tempC > peakC)) { peakC = rd.tempC; peakHold = 0; }
        else if (rd.ok && isfinite(peakC) && fabsf(rd.tempC - peakC) < 2.0f) peakHold += (uint32_t)(dt * 1000);

        kwh += (ssr.duty() / 100.0f) * (settings().elementWatts / 1000.0f) * (dt / 3600.0f);

        busy_ = engine.active() || tuner.running();

        snap.tempC = rd.tempC;
        snap.setpointC = NAN;
        if (tuner.running()) snap.setpointC = tuneTargetC_;
        else if (engine.active()) snap.setpointC = et.setpointC;
        snap.outputPct = ssr.duty();
        snap.windowDuty = ssr.windowDuty();
        snap.pTerm = terms.p;
        snap.iTerm = terms.i;
        snap.dTerm = terms.d;
        snap.ffTerm = terms.ff;
        snap.rateActual1 = rateOver(60000);
        snap.rateActual5 = rateOver(300000);
        snap.rateTarget = et.rateTarget;
        snap.cjTempC = rd.cjTempC;
        snap.tcOk = rd.ok;
        strncpy(snap.tcFault, rd.fault, sizeof snap.tcFault - 1);
        if (!tuner.running()) snap.state = engine.active() ? et.state : ST_IDLE;
        snap.segmentIndex = et.segmentIndex;
        snap.segmentCount = et.segmentCount;
        if (!tuner.running())
            strncpy(snap.segmentType, et.segmentType, sizeof snap.segmentType - 1);
        snap.segRemainSec = et.segRemainSec;
        snap.totalRemainSec = et.totalRemainSec;
        snap.elapsedSec = et.elapsedSec;
        time_t nowUnix = time(nullptr);
        snap.etaUnix = (nowUnix > 1600000000 && et.totalRemainSec)
                       ? (uint32_t)nowUnix + et.totalRemainSec : 0;
        snap.peakC = peakC;
        snap.peakHoldSec = peakHold / 1000;
        snap.holdBack = et.holdBack;
        snap.kwh = kwh;
        snap.cost = kwh * settings().costPerKwh;
        snap.firing = engine.active();
        snap.paused = engine.paused();
        if (!saf.tripped) snap.faulted = false;
        strncpy(snap.profileName, engine.profile().name.c_str(), sizeof snap.profileName - 1);
        snap.useF = settings().useFahrenheit;
        snap.inQuartz = et.inQuartz;
        snap.inCristo = et.inCristo;
        snap.delayUntil = 0;
        snap.tuneCycle = lastTune.cycles;
        snap.tuneKu = lastTune.ku;
        snap.tunePu = lastTune.pu;
        if (lastTune.done && !lastTune.running) {
            // leave ku/pu visible
        }

        if (engine.active() || tuner.running())
            logPoint(rd.ok ? rd.tempC : NAN, snap.setpointC, snap.outputPct,
                     snap.segmentType, snap.segmentIndex);

        statePublish(snap);
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(settings().controlMs));
    }
}

void controlStart() {
    xTaskCreatePinnedToCore(controlLoop, "kiln", 8192, nullptr, 4, &task_, 1);
}
