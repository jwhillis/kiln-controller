#include "AutoTune.h"

void AutoTune::start(float targetC, float relayAmp) {
    r_ = TuneResult();
    r_.running = true;
    target_ = targetC;
    amp_ = relayAmp;
    if (amp_ < 10) amp_ = 10;
    if (amp_ > 50) amp_ = 50;
    high_ = true;
    crossings_ = 0;
    lastCrossMs_ = 0;
    peak_ = -1e9;
    trough_ = 1e9;
    periodAcc_ = 0;
    ampAcc_ = 0;
    periods_ = 0;
}

void AutoTune::stop() {
    r_.running = false;
    r_.output = 0;
}

TuneResult AutoTune::update(float tempC, float dtSec) {
    (void)dtSec;
    if (!r_.running) return r_;

    if (tempC > peak_) peak_ = tempC;
    if (tempC < trough_) trough_ = tempC;

    bool wantHigh = tempC < target_;
    if (wantHigh != high_) {
        uint32_t now = millis();
        if (lastCrossMs_ && crossings_ > 0) {
            float pu = (now - lastCrossMs_) / 1000.0f;
            float a = (peak_ - trough_) / 2.0f;
            if (pu > 5 && a > 0.5f) {
                periodAcc_ += pu;
                ampAcc_ += a;
                periods_++;
            }
        }
        lastCrossMs_ = now;
        crossings_++;
        r_.cycles = crossings_ / 2;
        peak_ = tempC;
        trough_ = tempC;
        high_ = wantHigh;

        // Need several half-cycles after the first to settle.
        if (periods_ >= 3) {
            float pu = (periodAcc_ / periods_) * 2.0f; // lastCross is half-period
            // crossings toggle each edge; period between same-direction edges is 2 half-periods.
            // We accumulated half-periods; full period ≈ 2 * mean(half).
            pu = periodAcc_ / periods_ * 2.0f;
            float A = ampAcc_ / periods_;
            float d = amp_;
            float ku = (4.0f * d) / (3.14159265f * A);
            r_.ku = ku;
            r_.pu = pu;
            r_.znKp = 0.6f * ku;
            r_.znKi = 1.2f * ku / pu;
            r_.znKd = 0.075f * ku * pu;
            r_.tlKp = ku / 3.2f;
            float ti = 2.2f * pu;
            float td = pu / 6.3f;
            r_.tlKi = r_.tlKp / ti;
            r_.tlKd = r_.tlKp * td;
            r_.done = true;
            r_.running = false;
            r_.output = 0;
            return r_;
        }
    }

    r_.output = high_ ? (50.0f + amp_) : (50.0f - amp_);
    if (r_.output < 0) r_.output = 0;
    if (r_.output > 100) r_.output = 100;
    return r_;
}
