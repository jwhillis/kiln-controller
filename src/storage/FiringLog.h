#pragma once
#include <Arduino.h>

struct HistPoint {
    uint32_t tSec;
    float tempC;
    float spC;
    float outPct;
};

void logBegin();
void logStart(const String& profileName);
void logPoint(float tempC, float spC, float outPct, const char* state, int seg);
void logFinish();
String logCurrentName();
String logsListJson();

// In-RAM ring for the live chart
void histPush(uint32_t tSec, float tempC, float spC, float outPct);
String histToJson();
void histClear();
