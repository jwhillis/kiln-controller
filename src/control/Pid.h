#pragma once

struct PidTerms {
    float output = 0;
    float p = 0, i = 0, d = 0, ff = 0;
};

class Pid {
public:
    void setGains(float kp, float ki, float kd, float kf);
    void reset(float output = 0);
    void setManual(float output);
    PidTerms compute(float setpoint, float input, float rampCph, float dtSec);

private:
    float kp_ = 0, ki_ = 0, kd_ = 0, kf_ = 0;
    float i_ = 0;
    float lastInput_ = 0;
    bool  haveLast_ = false;
    float lastOut_ = 0;
};
