#pragma once
#include <Arduino.h>

struct SafetyStatus {
    bool  tripped = false;
    char  reason[80] = "";
};

class SafetyMonitor {
public:
    void reset();
    SafetyStatus check(float tempC, bool tcOk, const char* tcFault,
                       float outputPct, bool firing, uint32_t elapsedSec);

private:
    float    lastT_ = NAN;
    uint32_t stallStart_ = 0;
    float    stallT_ = NAN;
    bool     haveLast_ = false;
};
