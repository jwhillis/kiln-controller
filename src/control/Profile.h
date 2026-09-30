#pragma once
#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>

struct Segment {
    float rateCph = 100;
    float targetC = 100;
    float holdMin = 0;
};

struct BandOverride {
    bool  enabled = false;
    float centerC = 573;
    float bandC   = 25;
    float rateHeatCph = 50;
    float rateCoolCph = 50;
};

struct Profile {
    String name;
    std::vector<Segment> segments;
    BandOverride quartz;
    BandOverride cristobalite;
    bool fireDownEnabled = false;
    std::vector<Segment> fireDown;

    String toJson() const;
    bool   fromJson(const String& json, String& err);
    bool   fromDoc(JsonVariantConst doc, String& err);
};

String sanitizeProfileName(const String& name);
