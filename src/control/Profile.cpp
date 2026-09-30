#include "Profile.h"
#include "../config/Settings.h"

String sanitizeProfileName(const String& name) {
    String o;
    for (size_t i = 0; i < name.length() && o.length() < 40; i++) {
        char c = name[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ') o += c;
    }
    o.trim();
    if (!o.length()) o = "untitled";
    return o;
}

static void writeBand(JsonObject o, const BandOverride& b) {
    o["enabled"] = b.enabled;
    o["center"] = b.centerC;
    o["band"] = b.bandC;
    o["rateHeat"] = b.rateHeatCph;
    o["rateCool"] = b.rateCoolCph;
}

static BandOverride readBand(JsonVariantConst v, const BandOverride& def) {
    BandOverride b = def;
    if (v.isNull()) return b;
    if (v["enabled"].is<bool>()) b.enabled = v["enabled"];
    if (!v["center"].isNull()) b.centerC = v["center"].as<float>();
    if (!v["band"].isNull()) b.bandC = v["band"].as<float>();
    if (!v["rateHeat"].isNull()) b.rateHeatCph = v["rateHeat"].as<float>();
    if (!v["rateCool"].isNull()) b.rateCoolCph = v["rateCool"].as<float>();
    return b;
}

static void writeSegs(JsonArray arr, const std::vector<Segment>& segs) {
    for (const auto& s : segs) {
        JsonObject o = arr.add<JsonObject>();
        o["rate"] = s.rateCph;
        o["target"] = s.targetC;
        o["hold"] = s.holdMin;
    }
}

static bool readSegs(JsonVariantConst arr, std::vector<Segment>& out, String& err) {
    out.clear();
    if (!arr.is<JsonArrayConst>()) { err = "segments must be an array"; return false; }
    for (JsonVariantConst v : arr.as<JsonArrayConst>()) {
        Segment s;
        s.rateCph = v["rate"] | 100.0f;
        s.targetC = v["target"] | 0.0f;
        s.holdMin = v["hold"] | 0.0f;
        if (s.rateCph < 0) s.rateCph = 0;
        out.push_back(s);
    }
    return true;
}

String Profile::toJson() const {
    JsonDocument doc;
    doc["name"] = name;
    writeSegs(doc["segments"].to<JsonArray>(), segments);
    writeBand(doc["quartz"].to<JsonObject>(), quartz);
    writeBand(doc["cristobalite"].to<JsonObject>(), cristobalite);
    JsonObject fd = doc["fireDown"].to<JsonObject>();
    fd["enabled"] = fireDownEnabled;
    writeSegs(fd["segments"].to<JsonArray>(), fireDown);
    String out;
    serializeJsonPretty(doc, out);
    return out;
}

bool Profile::fromDoc(JsonVariantConst doc, String& err) {
    const Settings& d = settings();
    name = sanitizeProfileName(doc["name"] | "untitled");
    if (!readSegs(doc["segments"], segments, err)) return false;
    if (!segments.size()) { err = "profile needs at least one segment"; return false; }

    BandOverride qdef;
    qdef.enabled = true;
    qdef.centerC = d.quartzCenterC;
    qdef.bandC = d.quartzBandC;
    qdef.rateHeatCph = d.quartzRateHeat;
    qdef.rateCoolCph = d.quartzRateCool;
    quartz = readBand(doc["quartz"], qdef);

    BandOverride cdef;
    cdef.enabled = false;
    cdef.centerC = d.cristoCenterC;
    cdef.bandC = d.cristoBandC;
    cdef.rateHeatCph = d.cristoRateCool;
    cdef.rateCoolCph = d.cristoRateCool;
    cristobalite = readBand(doc["cristobalite"], cdef);

    fireDownEnabled = false;
    fireDown.clear();
    if (!doc["fireDown"].isNull()) {
        auto fd = doc["fireDown"];
        if (fd["enabled"].is<bool>()) fireDownEnabled = fd["enabled"];
        if (!fd["segments"].isNull()) {
            if (!readSegs(fd["segments"], fireDown, err)) return false;
        }
    }
    return true;
}

bool Profile::fromJson(const String& json, String& err) {
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, json);
    if (e) { err = e.c_str(); return false; }
    return fromDoc(doc.as<JsonVariantConst>(), err);
}
