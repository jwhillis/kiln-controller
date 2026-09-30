#include "Files.h"
#include "../Version.h"
#include "../config/Settings.h"
#include <LittleFS.h>

static bool ok_ = false;

bool filesOk() { return ok_; }

bool filesEnsureDirs() {
    if (!ok_) return false;
    LittleFS.mkdir("/profiles");
    LittleFS.mkdir("/logs");
    LittleFS.mkdir("/docs");
    return true;
}

bool filesBegin() {
    ok_ = LittleFS.begin(true, "/littlefs", 10, LFS_LABEL);
    Serial.printf("LittleFS: %s\n", ok_ ? "mounted" : "FAILED");
    if (ok_) {
        filesEnsureDirs();
        filesSeedExamples();
    }
    return ok_;
}

static String pathFor(const String& name) {
    return "/profiles/" + sanitizeProfileName(name) + ".json";
}

bool filesLoadProfile(const String& name, Profile& p, String& err) {
    if (!ok_) { err = "LittleFS not mounted"; return false; }
    String path = pathFor(name);
    File f = LittleFS.open(path, "r");
    if (!f) { err = "not found"; return false; }
    String json = f.readString();
    f.close();
    return p.fromJson(json, err);
}

bool filesSaveProfile(const Profile& p, String& err) {
    if (!ok_) { err = "LittleFS not mounted"; return false; }
    File f = LittleFS.open(pathFor(p.name), "w");
    if (!f) { err = "write failed"; return false; }
    f.print(p.toJson());
    f.close();
    return true;
}

bool filesDeleteProfile(const String& name) {
    if (!ok_) return false;
    return LittleFS.remove(pathFor(name));
}

String filesListProfilesJson() {
    return filesListJson("/profiles");
}

String filesListJson(const char* dir) {
    String out = "[";
    if (!ok_) { out += "]"; return out; }
    File root = LittleFS.open(dir);
    if (!root || !root.isDirectory()) { out += "]"; return out; }
    int n = 0;
    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            if (n++) out += ',';
            out += "{\"name\":\"";
            String nm = f.name();
            int slash = nm.lastIndexOf('/');
            if (slash >= 0) nm = nm.substring(slash + 1);
            for (size_t i = 0; i < nm.length(); i++) {
                char c = nm[i];
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            out += "\",\"size\":";
            out += String((unsigned)f.size());
            out += '}';
        }
        f = root.openNextFile();
    }
    out += ']';
    return out;
}

bool filesDelete(const String& path) {
    if (!ok_) return false;
    if (!path.startsWith("/logs/") && !path.startsWith("/docs/") && !path.startsWith("/profiles/"))
        return false;
    if (path.indexOf("..") >= 0) return false;
    return LittleFS.remove(path);
}

static const char* EXAMPLE_CONE6 = R"json({
  "name": "Cone 6 Glaze",
  "segments": [
    {"rate": 80, "target": 100, "hold": 0},
    {"rate": 150, "target": 600, "hold": 0},
    {"rate": 60, "target": 1100, "hold": 0},
    {"rate": 80, "target": 1222, "hold": 10}
  ],
  "quartz": {"enabled": true, "center": 573, "band": 25, "rateHeat": 50, "rateCool": 50},
  "cristobalite": {"enabled": false, "center": 226, "band": 20, "rateHeat": 40, "rateCool": 40},
  "fireDown": {"enabled": true, "segments": [
    {"rate": 80, "target": 800, "hold": 0},
    {"rate": 50, "target": 226, "hold": 0}
  ]}
})json";

static const char* EXAMPLE_GLASS = R"json({
  "name": "Glass Anneal",
  "segments": [
    {"rate": 200, "target": 816, "hold": 15}
  ],
  "quartz": {"enabled": false, "center": 573, "band": 25, "rateHeat": 50, "rateCool": 50},
  "cristobalite": {"enabled": false, "center": 226, "band": 20, "rateHeat": 40, "rateCool": 40},
  "fireDown": {"enabled": true, "segments": [
    {"rate": 80, "target": 516, "hold": 60},
    {"rate": 30, "target": 371, "hold": 0}
  ]}
})json";

static const char* EXAMPLE_BISQUE = R"json({
  "name": "Slow Bisque",
  "segments": [
    {"rate": 60, "target": 100, "hold": 60},
    {"rate": 80, "target": 300, "hold": 0},
    {"rate": 100, "target": 700, "hold": 0},
    {"rate": 80, "target": 950, "hold": 10}
  ],
  "quartz": {"enabled": true, "center": 573, "band": 25, "rateHeat": 40, "rateCool": 40},
  "cristobalite": {"enabled": true, "center": 226, "band": 20, "rateHeat": 40, "rateCool": 40},
  "fireDown": {"enabled": false, "segments": []}
})json";

void filesSeedExamples() {
    if (!ok_) return;
    if (LittleFS.exists("/profiles/Cone 6 Glaze.json")) return;
    File f;
    f = LittleFS.open("/profiles/Cone 6 Glaze.json", "w"); if (f) { f.print(EXAMPLE_CONE6); f.close(); }
    f = LittleFS.open("/profiles/Glass Anneal.json", "w"); if (f) { f.print(EXAMPLE_GLASS); f.close(); }
    f = LittleFS.open("/profiles/Slow Bisque.json", "w"); if (f) { f.print(EXAMPLE_BISQUE); f.close(); }
    Serial.println("Seeded example profiles");
}
