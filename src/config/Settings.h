#pragma once
#include <Arduino.h>

enum TcChip : uint8_t { TC_MAX31856 = 0, TC_MAX31855 = 1 };

struct Settings {
    // Pins
    uint8_t pinSsr          = 21;
    uint8_t pinSafetyRelay  = 255;  // 255 = unused
    uint8_t pinTcCs         = 10;
    uint8_t pinSck          = 12;
    uint8_t pinMiso         = 13;
    uint8_t pinMosi         = 11;
    uint8_t pinLed          = 38;

    // Sensor
    uint8_t tcChip          = TC_MAX31856;
    uint8_t tcTypeK         = 3;    // Adafruit MAX31856_TCTYPE_K

    // PID / output
    float   kp              = 8.0f;
    float   ki              = 0.04f;
    float   kd              = 40.0f;
    float   kf              = 0.15f;    // feed-forward % per °C/h
    float   ssrWindowSec    = 4.0f;
    uint16_t controlMs      = 250;

    // Display
    bool    useFahrenheit   = true;

    // Safety
    float   maxTempC        = 1320.0f;
    float   stallMinutes    = 20.0f;
    float   stallRiseC      = 5.0f;
    float   runawayRiseC    = 8.0f;
    uint32_t maxDurationMin = 36 * 60;
    float   holdBandC       = 5.0f;
    float   lagLimitC       = 25.0f;
    float   resumeBandC     = 40.0f;
    float   logUntilC       = 80.0f;

    // Energy
    float   elementWatts    = 4500.0f;
    float   costPerKwh      = 0.14f;

    // Quartz / cristobalite defaults
    float   quartzCenterC   = 573.0f;
    float   quartzBandC     = 25.0f;
    float   quartzRateHeat  = 50.0f;
    float   quartzRateCool  = 50.0f;
    float   cristoCenterC   = 226.0f;
    float   cristoBandC     = 20.0f;
    float   cristoRateCool  = 40.0f;

    // Admin
    char    adminPass[32]   = "admin";

    // MQTT
    bool    mqttEnabled     = false;
    char    mqttHost[64]    = "192.168.1.10";
    uint16_t mqttPort       = 1883;
    char    mqttUser[32]    = "";
    char    mqttPass[32]    = "";
    char    mqttClient[32]  = "kiln";
    char    mqttBase[32]    = "kiln/";
    uint16_t mqttIntervalSec = 5;
    bool    mqttRemoteStart = false;
};

void     settingsLoad();
void     settingsSave();
void     settingsFactoryReset();
Settings& settings();
String   settingsToJson();
bool     settingsFromJson(const String& json, String& err);
