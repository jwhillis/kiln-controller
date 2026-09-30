#pragma once
#include <Arduino.h>

struct TuneResult {
    bool  done = false;
    bool  running = false;
    float ku = 0, pu = 0;
    float znKp = 0, znKi = 0, znKd = 0;
    float tlKp = 0, tlKi = 0, tlKd = 0;
    uint32_t cycles = 0;
    float output = 0;
};

class AutoTune {
public:
    void start(float targetC, float relayAmp = 50);
    void stop();
    TuneResult update(float tempC, float dtSec);
    TuneResult result() const { return r_; }
    bool running() const { return r_.running; }

private:
    TuneResult r_;
    float target_ = 500;
    float amp_ = 50;      // ± around 50%
    bool  high_ = true;
    uint32_t crossings_ = 0;
    uint32_t lastCrossMs_ = 0;
    float peak_ = -1e9, trough_ = 1e9;
    float periodAcc_ = 0;
    float ampAcc_ = 0;
    uint32_t periods_ = 0;
};
