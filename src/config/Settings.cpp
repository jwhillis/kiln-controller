#include "Settings.h"
#include <Preferences.h>
#include <ArduinoJson.h>

static Settings g;
static Preferences prefs;

Settings& settings() { return g; }

static void putStr(const char* k, const char* v) { prefs.putString(k, v); }

void settingsLoad() {
    prefs.begin("kiln", true);
    auto getU8  = [&](const char* k, uint8_t d) { return (uint8_t)prefs.getUChar(k, d); };
    auto getU16 = [&](const char* k, uint16_t d) { return (uint16_t)prefs.getUShort(k, d); };
    auto getU32 = [&](const char* k, uint32_t d) { return prefs.getUInt(k, d); };
    auto getF   = [&](const char* k, float d) { return prefs.getFloat(k, d); };
    auto getB   = [&](const char* k, bool d) { return prefs.getBool(k, d); };

    g.pinSsr         = getU8("pinSsr", g.pinSsr);
    g.pinSafetyRelay = getU8("pinSafe", g.pinSafetyRelay);
    g.pinTcCs        = getU8("pinCs", g.pinTcCs);
    g.pinSck         = getU8("pinSck", g.pinSck);
    g.pinMiso        = getU8("pinMiso", g.pinMiso);
    g.pinMosi        = getU8("pinMosi", g.pinMosi);
    g.pinLed         = getU8("pinLed", g.pinLed);
    g.tcChip         = getU8("tcChip", g.tcChip);
    g.kp             = getF("kp", g.kp);
    g.ki             = getF("ki", g.ki);
    g.kd             = getF("kd", g.kd);
    g.kf             = getF("kf", g.kf);
    g.ssrWindowSec   = getF("ssrWin", g.ssrWindowSec);
    g.controlMs      = getU16("ctlMs", g.controlMs);
    g.useFahrenheit  = getB("useF", g.useFahrenheit);
    g.maxTempC       = getF("maxT", g.maxTempC);
    g.stallMinutes   = getF("stallM", g.stallMinutes);
    g.stallRiseC     = getF("stallR", g.stallRiseC);
    g.runawayRiseC   = getF("runR", g.runawayRiseC);
    g.maxDurationMin = getU32("maxDur", g.maxDurationMin);
    g.holdBandC      = getF("holdB", g.holdBandC);
    g.lagLimitC      = getF("lag", g.lagLimitC);
    g.resumeBandC    = getF("resumeB", g.resumeBandC);
    g.logUntilC      = getF("logUntil", g.logUntilC);
    g.elementWatts   = getF("watts", g.elementWatts);
    g.costPerKwh     = getF("kwh$", g.costPerKwh);
    g.quartzCenterC  = getF("qC", g.quartzCenterC);
    g.quartzBandC    = getF("qB", g.quartzBandC);
    g.quartzRateHeat = getF("qRH", g.quartzRateHeat);
    g.quartzRateCool = getF("qRC", g.quartzRateCool);
    g.cristoCenterC  = getF("cC", g.cristoCenterC);
    g.cristoBandC    = getF("cB", g.cristoBandC);
    g.cristoRateCool = getF("cRC", g.cristoRateCool);
    g.mqttEnabled    = getB("mqEn", g.mqttEnabled);
    g.mqttPort       = getU16("mqPort", g.mqttPort);
    g.mqttIntervalSec = getU16("mqInt", g.mqttIntervalSec);
    g.mqttRemoteStart = getB("mqRem", g.mqttRemoteStart);

    String p = prefs.getString("admin", g.adminPass);
    p.toCharArray(g.adminPass, sizeof g.adminPass);
    prefs.getString("mqHost", g.mqttHost, sizeof g.mqttHost);
    prefs.getString("mqUser", g.mqttUser, sizeof g.mqttUser);
    prefs.getString("mqPass", g.mqttPass, sizeof g.mqttPass);
    prefs.getString("mqCli", g.mqttClient, sizeof g.mqttClient);
    prefs.getString("mqBase", g.mqttBase, sizeof g.mqttBase);
    prefs.end();
}

void settingsSave() {
    prefs.begin("kiln", false);
    prefs.putUChar("pinSsr", g.pinSsr);
    prefs.putUChar("pinSafe", g.pinSafetyRelay);
    prefs.putUChar("pinCs", g.pinTcCs);
    prefs.putUChar("pinSck", g.pinSck);
    prefs.putUChar("pinMiso", g.pinMiso);
    prefs.putUChar("pinMosi", g.pinMosi);
    prefs.putUChar("pinLed", g.pinLed);
    prefs.putUChar("tcChip", g.tcChip);
    prefs.putFloat("kp", g.kp);
    prefs.putFloat("ki", g.ki);
    prefs.putFloat("kd", g.kd);
    prefs.putFloat("kf", g.kf);
    prefs.putFloat("ssrWin", g.ssrWindowSec);
    prefs.putUShort("ctlMs", g.controlMs);
    prefs.putBool("useF", g.useFahrenheit);
    prefs.putFloat("maxT", g.maxTempC);
    prefs.putFloat("stallM", g.stallMinutes);
    prefs.putFloat("stallR", g.stallRiseC);
    prefs.putFloat("runR", g.runawayRiseC);
    prefs.putUInt("maxDur", g.maxDurationMin);
    prefs.putFloat("holdB", g.holdBandC);
    prefs.putFloat("lag", g.lagLimitC);
    prefs.putFloat("resumeB", g.resumeBandC);
    prefs.putFloat("logUntil", g.logUntilC);
    prefs.putFloat("watts", g.elementWatts);
    prefs.putFloat("kwh$", g.costPerKwh);
    prefs.putFloat("qC", g.quartzCenterC);
    prefs.putFloat("qB", g.quartzBandC);
    prefs.putFloat("qRH", g.quartzRateHeat);
    prefs.putFloat("qRC", g.quartzRateCool);
    prefs.putFloat("cC", g.cristoCenterC);
    prefs.putFloat("cB", g.cristoBandC);
    prefs.putFloat("cRC", g.cristoRateCool);
    prefs.putBool("mqEn", g.mqttEnabled);
    prefs.putUShort("mqPort", g.mqttPort);
    prefs.putUShort("mqInt", g.mqttIntervalSec);
    prefs.putBool("mqRem", g.mqttRemoteStart);
    putStr("admin", g.adminPass);
    putStr("mqHost", g.mqttHost);
    putStr("mqUser", g.mqttUser);
    putStr("mqPass", g.mqttPass);
    putStr("mqCli", g.mqttClient);
    putStr("mqBase", g.mqttBase);
    prefs.end();
}

void settingsFactoryReset() {
    prefs.begin("kiln", false);
    prefs.clear();
    prefs.end();
    g = Settings();
    settingsSave();
}

String settingsToJson() {
    JsonDocument doc;
    doc["pinSsr"] = g.pinSsr;
    doc["pinSafetyRelay"] = g.pinSafetyRelay;
    doc["pinTcCs"] = g.pinTcCs;
    doc["pinSck"] = g.pinSck;
    doc["pinMiso"] = g.pinMiso;
    doc["pinMosi"] = g.pinMosi;
    doc["pinLed"] = g.pinLed;
    doc["tcChip"] = g.tcChip;
    doc["kp"] = g.kp;
    doc["ki"] = g.ki;
    doc["kd"] = g.kd;
    doc["kf"] = g.kf;
    doc["ssrWindowSec"] = g.ssrWindowSec;
    doc["controlMs"] = g.controlMs;
    doc["useFahrenheit"] = g.useFahrenheit;
    doc["maxTempC"] = g.maxTempC;
    doc["stallMinutes"] = g.stallMinutes;
    doc["stallRiseC"] = g.stallRiseC;
    doc["runawayRiseC"] = g.runawayRiseC;
    doc["maxDurationMin"] = g.maxDurationMin;
    doc["holdBandC"] = g.holdBandC;
    doc["lagLimitC"] = g.lagLimitC;
    doc["resumeBandC"] = g.resumeBandC;
    doc["logUntilC"] = g.logUntilC;
    doc["elementWatts"] = g.elementWatts;
    doc["costPerKwh"] = g.costPerKwh;
    doc["quartzCenterC"] = g.quartzCenterC;
    doc["quartzBandC"] = g.quartzBandC;
    doc["quartzRateHeat"] = g.quartzRateHeat;
    doc["quartzRateCool"] = g.quartzRateCool;
    doc["cristoCenterC"] = g.cristoCenterC;
    doc["cristoBandC"] = g.cristoBandC;
    doc["cristoRateCool"] = g.cristoRateCool;
    doc["adminPass"] = g.adminPass;
    doc["mqttEnabled"] = g.mqttEnabled;
    doc["mqttHost"] = g.mqttHost;
    doc["mqttPort"] = g.mqttPort;
    doc["mqttUser"] = g.mqttUser;
    doc["mqttPass"] = g.mqttPass;
    doc["mqttClient"] = g.mqttClient;
    doc["mqttBase"] = g.mqttBase;
    doc["mqttIntervalSec"] = g.mqttIntervalSec;
    doc["mqttRemoteStart"] = g.mqttRemoteStart;
    String out;
    serializeJson(doc, out);
    return out;
}

static void copyStr(char* dst, size_t n, const char* src) {
    if (!src) return;
    strncpy(dst, src, n - 1);
    dst[n - 1] = 0;
}

bool settingsFromJson(const String& json, String& err) {
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, json);
    if (e) { err = e.c_str(); return false; }

    auto f = [&](const char* k, float& v) { if (!doc[k].isNull()) v = doc[k].as<float>(); };
    auto u8 = [&](const char* k, uint8_t& v) { if (!doc[k].isNull()) v = (uint8_t)doc[k].as<int>(); };
    auto u16 = [&](const char* k, uint16_t& v) { if (!doc[k].isNull()) v = (uint16_t)doc[k].as<int>(); };
    auto u32 = [&](const char* k, uint32_t& v) { if (!doc[k].isNull()) v = (uint32_t)doc[k].as<uint32_t>(); };
    auto b = [&](const char* k, bool& v) { if (!doc[k].isNull()) v = doc[k].as<bool>(); };

    u8("pinSsr", g.pinSsr);
    u8("pinSafetyRelay", g.pinSafetyRelay);
    u8("pinTcCs", g.pinTcCs);
    u8("pinSck", g.pinSck);
    u8("pinMiso", g.pinMiso);
    u8("pinMosi", g.pinMosi);
    u8("pinLed", g.pinLed);
    u8("tcChip", g.tcChip);
    f("kp", g.kp); f("ki", g.ki); f("kd", g.kd); f("kf", g.kf);
    f("ssrWindowSec", g.ssrWindowSec);
    u16("controlMs", g.controlMs);
    b("useFahrenheit", g.useFahrenheit);
    f("maxTempC", g.maxTempC);
    f("stallMinutes", g.stallMinutes);
    f("stallRiseC", g.stallRiseC);
    f("runawayRiseC", g.runawayRiseC);
    u32("maxDurationMin", g.maxDurationMin);
    f("holdBandC", g.holdBandC);
    f("lagLimitC", g.lagLimitC);
    f("resumeBandC", g.resumeBandC);
    f("logUntilC", g.logUntilC);
    f("elementWatts", g.elementWatts);
    f("costPerKwh", g.costPerKwh);
    f("quartzCenterC", g.quartzCenterC);
    f("quartzBandC", g.quartzBandC);
    f("quartzRateHeat", g.quartzRateHeat);
    f("quartzRateCool", g.quartzRateCool);
    f("cristoCenterC", g.cristoCenterC);
    f("cristoBandC", g.cristoBandC);
    f("cristoRateCool", g.cristoRateCool);
    b("mqttEnabled", g.mqttEnabled);
    u16("mqttPort", g.mqttPort);
    u16("mqttIntervalSec", g.mqttIntervalSec);
    b("mqttRemoteStart", g.mqttRemoteStart);
    if (doc["adminPass"].is<const char*>() && strlen(doc["adminPass"]) > 0)
        copyStr(g.adminPass, sizeof g.adminPass, doc["adminPass"]);
    if (doc["mqttHost"].is<const char*>()) copyStr(g.mqttHost, sizeof g.mqttHost, doc["mqttHost"]);
    if (doc["mqttUser"].is<const char*>()) copyStr(g.mqttUser, sizeof g.mqttUser, doc["mqttUser"]);
    if (doc["mqttPass"].is<const char*>()) copyStr(g.mqttPass, sizeof g.mqttPass, doc["mqttPass"]);
    if (doc["mqttClient"].is<const char*>()) copyStr(g.mqttClient, sizeof g.mqttClient, doc["mqttClient"]);
    if (doc["mqttBase"].is<const char*>()) copyStr(g.mqttBase, sizeof g.mqttBase, doc["mqttBase"]);

    if (g.ssrWindowSec < 1.0f) g.ssrWindowSec = 1.0f;
    if (g.ssrWindowSec > 30.0f) g.ssrWindowSec = 30.0f;
    if (g.controlMs < 100) g.controlMs = 100;
    settingsSave();
    return true;
}
