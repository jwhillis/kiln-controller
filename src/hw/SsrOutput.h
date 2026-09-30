#pragma once
#include <Arduino.h>

class SsrOutput {
public:
    void begin();
    void setDuty(float pct);     // 0–100
    void forceOff();
    void service();              // call every loop / control tick
    float duty() const { return duty_; }
    float windowDuty() const { return lastWindowDuty_; }
    bool  on() const { return on_; }

private:
    float    duty_ = 0;
    bool     on_ = false;
    bool     forcedOff_ = false;
    uint32_t winStart_ = 0;
    uint32_t onMsAcc_ = 0;
    uint32_t lastMs_ = 0;
    float    lastWindowDuty_ = 0;
};
