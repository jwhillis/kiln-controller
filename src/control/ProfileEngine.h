#pragma once
#include "Profile.h"
#include "../state/SharedState.h"

struct EngineTick {
    float     setpointC = NAN;
    float     rateTarget = 0;
    KilnState state = ST_IDLE;
    int       segmentIndex = -1;
    int       segmentCount = 0;
    char      segmentType[16] = "idle";
    uint32_t  segRemainSec = 0;
    uint32_t  totalRemainSec = 0;
    uint32_t  elapsedSec = 0;
    bool      holdBack = false;
    bool      inQuartz = false;
    bool      inCristo = false;
    bool      active = false;
    bool      finished = false;
    bool      coolingOut = false;
};

class ProfileEngine {
public:
    bool start(const Profile& p, float tempC, uint32_t delaySec = 0);
    bool resumeIfPossible(float tempC);
    void stop();
    void pause();
    void resume();
    void skip();
    EngineTick tick(float tempC, float dtSec);
    const Profile& profile() const { return p_; }
    bool active() const { return running_; }
    bool paused() const { return paused_; }
    void checkpoint();

private:
    Profile p_;
    bool running_ = false;
    bool paused_ = false;
    bool fireDownPhase_ = false;
    bool coolingOut_ = false;
    int  idx_ = 0;
    float sp_ = NAN;
    float holdLeft_ = 0;
    bool  holding_ = false;
    uint32_t elapsedMs_ = 0;
    uint32_t delayUntil_ = 0;
    uint32_t lastCk_ = 0;

    const std::vector<Segment>& segs() const;
    float effectiveRate(float tempC, float segRate, bool heating) const;
    bool  inBand(const BandOverride& b, float t) const;
    void  enterSegment(int i, float tempC);
    void  saveCheckpoint();
    void  clearCheckpoint();
};
