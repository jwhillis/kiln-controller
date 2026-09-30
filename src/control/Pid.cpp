#include "Pid.h"
#include <math.h>

void Pid::setGains(float kp, float ki, float kd, float kf) {
    kp_ = kp; ki_ = ki; kd_ = kd; kf_ = kf;
}

void Pid::reset(float output) {
    i_ = output;
    lastOut_ = output;
    haveLast_ = false;
}

void Pid::setManual(float output) {
    if (output < 0) output = 0;
    if (output > 100) output = 100;
    i_ = output;
    lastOut_ = output;
}

PidTerms Pid::compute(float setpoint, float input, float rampCph, float dtSec) {
    PidTerms t;
    if (dtSec <= 0 || !isfinite(input) || !isfinite(setpoint)) {
        t.output = lastOut_;
        return t;
    }
    float err = setpoint - input;
    t.p = kp_ * err;

    float candidateI = i_ + ki_ * err * dtSec;
    // Conditional integration: don't wind up further when saturated
    // unless the error would drive the output back toward the linear region.
    float trial = t.p + candidateI;
    bool satHi = lastOut_ >= 99.5f && trial > 100.0f;
    bool satLo = lastOut_ <= 0.5f && trial < 0.0f;
    if (!satHi && !satLo) i_ = candidateI;
    t.i = i_;

    t.d = 0;
    if (haveLast_ && kd_ != 0) {
        t.d = -kd_ * (input - lastInput_) / dtSec;
    }
    lastInput_ = input;
    haveLast_ = true;

    t.ff = kf_ * rampCph;
    float out = t.p + t.i + t.d + t.ff;
    if (out < 0) out = 0;
    if (out > 100) out = 100;
    t.output = lastOut_ = out;
    return t;
}
