#pragma once
#include <Arduino.h>

struct SensorReading {
    float tempC   = NAN;
    float cjTempC = NAN;
    bool  ok      = false;
    char  fault[40] = "";
};

class TempSensor {
public:
    bool begin();
    SensorReading read();
    bool ok() const { return last_.ok; }

private:
    SensorReading last_;
    float ema_ = NAN;
    float medBuf_[5] = {};
    uint8_t medN_ = 0;
    uint8_t medI_ = 0;
    bool begun56_ = false;
    bool begun55_ = false;

    float medianPush(float v);
};
