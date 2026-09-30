#pragma once
#include <Arduino.h>

enum KilnState : uint8_t {
    ST_IDLE = 0,
    ST_DELAYED,
    ST_RAMP,
    ST_HOLD,
    ST_FIRE_DOWN,
    ST_COOL,
    ST_COMPLETE,
    ST_FAULT,
    ST_PAUSE,
    ST_AUTOTUNE
};

struct Snapshot {
    float    tempC        = NAN;
    float    setpointC    = NAN;
    float    outputPct    = 0;
    float    pTerm        = 0;
    float    iTerm        = 0;
    float    dTerm        = 0;
    float    ffTerm       = 0;
    float    rateActual1  = 0;
    float    rateActual5  = 0;
    float    rateTarget   = 0;
    float    cjTempC      = NAN;
    bool     tcOk         = false;
    char     tcFault[40]  = "";
    KilnState state       = ST_IDLE;
    int      segmentIndex = -1;
    int      segmentCount = 0;
    char     segmentType[16] = "idle";
    uint32_t segRemainSec = 0;
    uint32_t totalRemainSec = 0;
    uint32_t elapsedSec   = 0;
    uint32_t etaUnix      = 0;
    float    peakC        = NAN;
    uint32_t peakHoldSec  = 0;
    bool     holdBack     = false;
    float    kwh          = 0;
    float    cost         = 0;
    bool     firing       = false;
    bool     paused       = false;
    bool     faulted      = false;
    char     faultMsg[80] = "";
    char     profileName[48] = "";
    bool     apMode       = false;
    int      rssi         = 0;
    bool     mqttOk       = false;
    uint32_t uptimeSec    = 0;
    uint32_t heap         = 0;
    bool     useF         = true;
    bool     inQuartz     = false;
    bool     inCristo     = false;
    float    windowDuty   = 0;
    uint32_t delayUntil   = 0;
    char     lastEvent[96] = "";
    uint32_t tuneCycle    = 0;
    float    tuneKu       = 0;
    float    tunePu       = 0;
};

enum CmdType : uint8_t {
    CMD_NONE = 0,
    CMD_START,
    CMD_STOP,
    CMD_PAUSE,
    CMD_RESUME,
    CMD_SKIP,
    CMD_DELAY_START,
    CMD_TUNE_START,
    CMD_TUNE_STOP,
    CMD_TUNE_ACCEPT,
    CMD_TUNE_REJECT,
    CMD_MANUAL
};

struct Command {
    CmdType type = CMD_NONE;
    char    profile[48] = "";
    uint32_t delaySec = 0;
    float   manualPct = 0;
    float   tuneTargetC = 500;
};

void     stateInit();
void     statePublish(const Snapshot& s);
Snapshot stateRead();
void     stateSetNet(bool ap, int rssi, bool mqtt);
void     stateSetEvent(const char* ev);

bool     cmdPush(const Command& c);
bool     cmdPop(Command& c);
void     cmdPushStart(const char* profile, uint32_t delaySec = 0);
void     cmdPushSimple(CmdType t);
