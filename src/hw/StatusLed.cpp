#include "StatusLed.h"
#include <Preferences.h>

static uint8_t  pin_ = 38;
static uint32_t last_ = 0xFFFFFFFF;
static uint32_t testUntil_ = 0;

void ledBegin() {
    Preferences p;
    p.begin("dev", true);
    pin_ = p.getUChar("ledPin", 38);
    p.end();
}

uint8_t ledPin() { return pin_; }

static void setRgb(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t c = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    if (c == last_) return;
    last_ = c;
    neopixelWrite(pin_, r, g, b);
}

void ledSetPin(uint8_t pin) {
    if (pin == pin_) return;
    neopixelWrite(pin_, 0, 0, 0);
    pin_ = pin;
    last_ = 0xFFFFFFFF;
    Preferences p;
    p.begin("dev", false);
    p.putUChar("ledPin", pin);
    p.end();
}

void ledTest(uint32_t ms) { testUntil_ = millis() + ms; }

void ledLoop(bool apMode, bool sensorOk, bool faulted, bool firing) {
    bool blinkOn = (millis() / 500) % 2;
    if (millis() < testUntil_) {
        setRgb(blinkOn ? 255 : 0, blinkOn ? 255 : 0, blinkOn ? 255 : 0);
        return;
    }
    if (faulted || (!sensorOk && millis() > 3000)) {
        setRgb(blinkOn ? 40 : 0, 0, 0);
        return;
    }
    if (apMode) {
        setRgb(0, 0, blinkOn ? 40 : 0);
        return;
    }
    if (firing) {
        setRgb(blinkOn ? 40 : 8, blinkOn ? 18 : 8, 0);  // amber pulse
        return;
    }
    setRgb(0, 30, 0);
}
