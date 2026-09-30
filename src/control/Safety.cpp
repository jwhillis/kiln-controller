#include "Safety.h"
#include "../config/Settings.h"
#include <math.h>

void SafetyMonitor::reset() {
    lastT_ = NAN;
    stallStart_ = 0;
    stallT_ = NAN;
    haveLast_ = false;
}

SafetyStatus SafetyMonitor::check(float tempC, bool tcOk, const char* tcFault,
                                  float outputPct, bool firing, uint32_t elapsedSec) {
    SafetyStatus s;
    const Settings& cfg = settings();

    if (!tcOk) {
        s.tripped = true;
        snprintf(s.reason, sizeof s.reason, "Thermocouple: %s",
                 tcFault && tcFault[0] ? tcFault : "no reading");
        return s;
    }
    if (!isfinite(tempC)) {
        s.tripped = true;
        strncpy(s.reason, "Thermocouple: stale / NaN", sizeof s.reason - 1);
        return s;
    }
    if (tempC > cfg.maxTempC) {
        s.tripped = true;
        snprintf(s.reason, sizeof s.reason, "Over-temp %.0f C (limit %.0f)", tempC, cfg.maxTempC);
        return s;
    }
    if (firing && elapsedSec > cfg.maxDurationMin * 60UL) {
        s.tripped = true;
        strncpy(s.reason, "Maximum firing duration exceeded", sizeof s.reason - 1);
        return s;
    }

    if (haveLast_ && firing) {
        float dT = tempC - lastT_;
        if (outputPct < 1.0f && dT > cfg.runawayRiseC) {
            s.tripped = true;
            snprintf(s.reason, sizeof s.reason, "Runaway: rising %.1f C with output off", dT);
            return s;
        }
        if (outputPct > 99.0f) {
            if (!stallStart_) { stallStart_ = millis(); stallT_ = tempC; }
            float risen = tempC - stallT_;
            if ((millis() - stallStart_) > (uint32_t)(cfg.stallMinutes * 60000.0f)
                && risen < cfg.stallRiseC) {
                s.tripped = true;
                snprintf(s.reason, sizeof s.reason,
                         "Stall: 100%% for %.0f min, rose only %.1f C", cfg.stallMinutes, risen);
                return s;
            }
        } else {
            stallStart_ = 0;
        }
    }
    lastT_ = tempC;
    haveLast_ = true;
    return s;
}
