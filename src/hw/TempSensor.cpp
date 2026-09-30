#include "TempSensor.h"
#include "../config/Settings.h"
#include <SPI.h>
#include <Adafruit_MAX31856.h>
#include <Adafruit_MAX31855.h>

static Adafruit_MAX31856* tc56 = nullptr;
static Adafruit_MAX31855* tc55 = nullptr;

bool TempSensor::begin() {
    const Settings& s = settings();
    SPI.end();
    SPI.begin(s.pinSck, s.pinMiso, s.pinMosi, s.pinTcCs);

    if (s.tcChip == TC_MAX31856) {
        if (!tc56) tc56 = new Adafruit_MAX31856(s.pinTcCs, &SPI);
        begun56_ = tc56->begin();
        if (begun56_) {
            tc56->setThermocoupleType(MAX31856_TCTYPE_K);
            tc56->setConversionMode(MAX31856_CONTINUOUS);
            Serial.println("MAX31856 ready");
        } else {
            Serial.println("MAX31856 begin failed");
        }
        return begun56_;
    }

    if (!tc55) tc55 = new Adafruit_MAX31855(s.pinTcCs, &SPI);
    begun55_ = tc55->begin();
    Serial.printf("MAX31855 %s\n", begun55_ ? "ready" : "begin failed");
    return begun55_;
}

float TempSensor::medianPush(float v) {
    medBuf_[medI_] = v;
    medI_ = (medI_ + 1) % 5;
    if (medN_ < 5) medN_++;
    float t[5];
    for (uint8_t i = 0; i < medN_; i++) t[i] = medBuf_[i];
    for (uint8_t i = 1; i < medN_; i++) {
        float key = t[i];
        int j = i - 1;
        while (j >= 0 && t[j] > key) { t[j + 1] = t[j]; j--; }
        t[j + 1] = key;
    }
    return t[medN_ / 2];
}

SensorReading TempSensor::read() {
    SensorReading r;
    const Settings& s = settings();

    if (s.tcChip == TC_MAX31856) {
        if (!tc56 || !begun56_) {
            strncpy(r.fault, "MAX31856 not ready", sizeof r.fault - 1);
            last_ = r;
            return r;
        }
        uint8_t fault = tc56->readFault();
        r.cjTempC = tc56->readCJTemperature();
        float t = tc56->readThermocoupleTemperature();
        if (fault) {
            if (fault & MAX31856_FAULT_OPEN)      strncpy(r.fault, "open circuit", sizeof r.fault - 1);
            else if (fault & MAX31856_FAULT_OVUV) strncpy(r.fault, "over/under voltage", sizeof r.fault - 1);
            else if (fault & MAX31856_FAULT_TCLOW) strncpy(r.fault, "TC low", sizeof r.fault - 1);
            else if (fault & MAX31856_FAULT_TCHIGH) strncpy(r.fault, "TC high", sizeof r.fault - 1);
            else snprintf(r.fault, sizeof r.fault, "fault 0x%02X", fault);
            last_ = r;
            return r;
        }
        if (!isfinite(t) || t < -50 || t > 1400) {
            strncpy(r.fault, "implausible reading", sizeof r.fault - 1);
            last_ = r;
            return r;
        }
        float med = medianPush(t);
        if (!isfinite(ema_)) ema_ = med;
        ema_ = 0.25f * med + 0.75f * ema_;
        r.tempC = ema_;
        r.ok = true;
        last_ = r;
        return r;
    }

    if (!tc55 || !begun55_) {
        strncpy(r.fault, "MAX31855 not ready", sizeof r.fault - 1);
        last_ = r;
        return r;
    }
    uint8_t e = tc55->readError();
    r.cjTempC = tc55->readInternal();
    float t = tc55->readCelsius();
    if (e) {
        if (e & MAX31855_FAULT_OPEN)      strncpy(r.fault, "open circuit", sizeof r.fault - 1);
        else if (e & MAX31855_FAULT_SHORT_GND) strncpy(r.fault, "short to GND", sizeof r.fault - 1);
        else if (e & MAX31855_FAULT_SHORT_VCC) strncpy(r.fault, "short to VCC", sizeof r.fault - 1);
        else snprintf(r.fault, sizeof r.fault, "fault 0x%02X", e);
        last_ = r;
        return r;
    }
    if (!isfinite(t) || t < -50 || t > 1400) {
        strncpy(r.fault, "implausible reading", sizeof r.fault - 1);
        last_ = r;
        return r;
    }
    float med = medianPush(t);
    if (!isfinite(ema_)) ema_ = med;
    ema_ = 0.25f * med + 0.75f * ema_;
    r.tempC = ema_;
    r.ok = true;
    last_ = r;
    return r;
}
