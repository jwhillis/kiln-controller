#pragma once
#include <Arduino.h>

void ledBegin();
void ledSetPin(uint8_t pin);
uint8_t ledPin();
void ledTest(uint32_t ms = 3000);
void ledLoop(bool apMode, bool sensorOk, bool faulted, bool firing);
