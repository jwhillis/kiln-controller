#include "SsrOutput.h"
#include "../config/Settings.h"

void SsrOutput::begin() {
    const Settings& s = settings();
    pinMode(s.pinSsr, OUTPUT);
    digitalWrite(s.pinSsr, LOW);
    if (s.pinSafetyRelay != 255) {
        pinMode(s.pinSafetyRelay, OUTPUT);
        digitalWrite(s.pinSafetyRelay, HIGH);  // energized = safe path closed
    }
    winStart_ = millis();
    lastMs_ = winStart_;
}

void SsrOutput::setDuty(float pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    duty_ = pct;
    forcedOff_ = false;
}

void SsrOutput::forceOff() {
    duty_ = 0;
    forcedOff_ = true;
    on_ = false;
    const Settings& s = settings();
    digitalWrite(s.pinSsr, LOW);
    if (s.pinSafetyRelay != 255) digitalWrite(s.pinSafetyRelay, LOW);
}

void SsrOutput::service() {
    const Settings& s = settings();
    uint32_t now = millis();
    uint32_t win = (uint32_t)(s.ssrWindowSec * 1000.0f);
    if (win < 500) win = 500;

    if (forcedOff_) {
        digitalWrite(s.pinSsr, LOW);
        on_ = false;
        lastMs_ = now;
        return;
    }

    uint32_t elapsed = now - winStart_;
    if (elapsed >= win) {
        lastWindowDuty_ = (float)onMsAcc_ * 100.0f / (float)win;
        winStart_ = now;
        onMsAcc_ = 0;
        elapsed = 0;
    }

    uint32_t onNeed = (uint32_t)(duty_ / 100.0f * (float)win);
    // Minimum on/off of 100 ms avoids chattering a mechanical relay if one is used.
    if (onNeed > 0 && onNeed < 100) onNeed = 100;
    if (onNeed < win && win - onNeed < 100) onNeed = win;

    bool want = elapsed < onNeed;
    if (want != on_) {
        on_ = want;
        digitalWrite(s.pinSsr, on_ ? HIGH : LOW);
    }
    if (on_) onMsAcc_ += now - lastMs_;
    lastMs_ = now;
}
