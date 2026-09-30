#include "FiringLog.h"
#include <LittleFS.h>
#include <time.h>

static String current_;
static File logFile;
static uint32_t lastWrite_ = 0;

static const int HIST_MAX = 2400;
static HistPoint hist_[HIST_MAX];
static int histN_ = 0, histHead_ = 0;

void histClear() { histN_ = 0; histHead_ = 0; }

void histPush(uint32_t tSec, float tempC, float spC, float outPct) {
    hist_[histHead_] = {tSec, tempC, spC, outPct};
    histHead_ = (histHead_ + 1) % HIST_MAX;
    if (histN_ < HIST_MAX) histN_++;
}

String histToJson() {
    String o = "[";
    int start = (histHead_ - histN_ + HIST_MAX) % HIST_MAX;
    for (int i = 0; i < histN_; i++) {
        const HistPoint& p = hist_[(start + i) % HIST_MAX];
        if (i) o += ',';
        o += '[';
        o += String(p.tSec);
        o += ',';
        o += String(p.tempC, 2);
        o += ',';
        o += isfinite(p.spC) ? String(p.spC, 2) : String("null");
        o += ',';
        o += String(p.outPct, 1);
        o += ']';
    }
    o += ']';
    return o;
}

void logBegin() {}

static String stampName(const String& profile) {
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[80];
    if (now > 1600000000) {
        snprintf(buf, sizeof buf, "/logs/%04d-%02d-%02d_%02d%02d_",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    } else {
        snprintf(buf, sizeof buf, "/logs/boot%lu_", (unsigned long)(millis() / 1000));
    }
    String p = profile;
    p.replace(' ', '_');
    return String(buf) + p + ".csv";
}

void logStart(const String& profileName) {
    if (logFile) logFile.close();
    current_ = stampName(profileName);
    logFile = LittleFS.open(current_, "w");
    if (logFile) {
        logFile.println("t_s,temp_c,setpoint_c,output_pct,state,segment");
        logFile.flush();
    }
    lastWrite_ = 0;
    histClear();
}

void logPoint(float tempC, float spC, float outPct, const char* state, int seg) {
    uint32_t sec = millis() / 1000;
    histPush(sec, tempC, spC, outPct);
    if (!logFile) return;
    if (sec - lastWrite_ < 10) return;
    lastWrite_ = sec;
    logFile.printf("%lu,%.2f,%.2f,%.1f,%s,%d\n",
                (unsigned long)sec, tempC, isfinite(spC) ? spC : tempC, outPct,
                state ? state : "", seg);
    logFile.flush();
}

void logFinish() {
    if (logFile) { logFile.close(); }
}

String logCurrentName() { return current_; }

String logsListJson() {
    String out = "[";
    File root = LittleFS.open("/logs");
    if (!root) { out += "]"; return out; }
    int n = 0;
    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            if (n++) out += ',';
            String nm = f.name();
            int slash = nm.lastIndexOf('/');
            if (slash >= 0) nm = nm.substring(slash + 1);
            out += "{\"name\":\"";
            out += nm;
            out += "\",\"size\":";
            out += String((unsigned)f.size());
            out += '}';
        }
        f = root.openNextFile();
    }
    out += ']';
    return out;
}
