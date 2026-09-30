#include "ProfileEngine.h"
#include "../config/Settings.h"
#include "../storage/Files.h"
#include <Preferences.h>
#include <math.h>

const std::vector<Segment>& ProfileEngine::segs() const {
    return fireDownPhase_ ? p_.fireDown : p_.segments;
}

bool ProfileEngine::inBand(const BandOverride& b, float t) const {
    if (!b.enabled || !isfinite(t)) return false;
    return fabsf(t - b.centerC) <= b.bandC;
}

float ProfileEngine::effectiveRate(float tempC, float segRate, bool heating) const {
    float r = segRate;
    if (r < 0) r = 0;
    if (inBand(p_.quartz, tempC)) {
        float cap = heating ? p_.quartz.rateHeatCph : p_.quartz.rateCoolCph;
        if (cap > 0 && cap < r) r = cap;
    }
    if (inBand(p_.cristobalite, tempC)) {
        float cap = heating ? p_.cristobalite.rateHeatCph : p_.cristobalite.rateCoolCph;
        if (cap > 0 && cap < r) r = cap;
    }
    return r;
}

void ProfileEngine::enterSegment(int i, float tempC) {
    (void)tempC;
    idx_ = i;
    holding_ = false;
    const auto& s = segs();
    if (i >= 0 && i < (int)s.size()) holdLeft_ = s[i].holdMin * 60.0f;
    else holdLeft_ = 0;
}

bool ProfileEngine::start(const Profile& p, float tempC, uint32_t delaySec) {
    if (!p.segments.size()) return false;
    p_ = p;
    running_ = true;
    paused_ = false;
    fireDownPhase_ = false;
    coolingOut_ = false;
    elapsedMs_ = 0;
    lastCk_ = 0;
    delayUntil_ = delaySec ? (millis() + delaySec * 1000UL) : 0;
    sp_ = tempC;
    enterSegment(0, tempC);
    saveCheckpoint();
    return true;
}

void ProfileEngine::stop() {
    running_ = false;
    paused_ = false;
    coolingOut_ = false;
    clearCheckpoint();
}

void ProfileEngine::pause() { if (running_) paused_ = true; }
void ProfileEngine::resume() { paused_ = false; }

void ProfileEngine::skip() {
    if (!running_ || paused_) return;
    const auto& s = segs();
    if (idx_ + 1 < (int)s.size()) {
        enterSegment(idx_ + 1, sp_);
    } else if (!fireDownPhase_ && p_.fireDownEnabled && p_.fireDown.size()) {
        fireDownPhase_ = true;
        enterSegment(0, sp_);
    } else {
        coolingOut_ = true;
    }
}

static uint32_t estimateRemain(const std::vector<Segment>& segs, int idx, float sp,
                               float holdLeft, bool holding, float tempC, float rateNow) {
    (void)tempC;
    uint32_t sec = 0;
    if (idx < 0 || idx >= (int)segs.size()) return 0;
    if (holding) sec += (uint32_t)fmaxf(holdLeft, 0);
    else {
        float dist = fabsf(segs[idx].targetC - sp);
        float r = rateNow > 1 ? rateNow : segs[idx].rateCph;
        if (r < 1) r = 1;
        sec += (uint32_t)(dist / r * 3600.0f);
        sec += (uint32_t)(segs[idx].holdMin * 60.0f);
    }
    for (int i = idx + 1; i < (int)segs.size(); i++) {
        float prev = segs[i - 1].targetC;
        float dist = fabsf(segs[i].targetC - prev);
        float r = segs[i].rateCph < 1 ? 1 : segs[i].rateCph;
        sec += (uint32_t)(dist / r * 3600.0f + segs[i].holdMin * 60.0f);
    }
    return sec;
}

EngineTick ProfileEngine::tick(float tempC, float dtSec) {
    EngineTick e;
    e.segmentCount = (int)p_.segments.size() + (p_.fireDownEnabled ? (int)p_.fireDown.size() : 0);
    if (!running_) return e;

    e.active = true;
    e.inQuartz = inBand(p_.quartz, tempC);
    e.inCristo = inBand(p_.cristobalite, tempC);

    if (delayUntil_ && (int32_t)(millis() - delayUntil_) < 0) {
        e.state = ST_DELAYED;
        e.setpointC = tempC;
        e.elapsedSec = 0;
        strncpy(e.segmentType, "delay", sizeof e.segmentType - 1);
        return e;
    }
    delayUntil_ = 0;

    if (paused_) {
        e.state = ST_PAUSE;
        e.setpointC = sp_;
        e.elapsedSec = elapsedMs_ / 1000;
        strncpy(e.segmentType, "pause", sizeof e.segmentType - 1);
        return e;
    }

    elapsedMs_ += (uint32_t)(dtSec * 1000.0f);
    e.elapsedSec = elapsedMs_ / 1000;

    if (coolingOut_) {
        e.state = ST_COOL;
        e.coolingOut = true;
        e.setpointC = tempC;
        strncpy(e.segmentType, "cool", sizeof e.segmentType - 1);
        if (isfinite(tempC) && tempC <= settings().logUntilC) {
            running_ = false;
            e.finished = true;
            e.active = false;
            e.state = ST_COMPLETE;
            strncpy(e.segmentType, "done", sizeof e.segmentType - 1);
            clearCheckpoint();
        }
        return e;
    }

    const auto& s = segs();
    if (idx_ < 0 || idx_ >= (int)s.size()) {
        if (!fireDownPhase_ && p_.fireDownEnabled && p_.fireDown.size()) {
            fireDownPhase_ = true;
            enterSegment(0, tempC);
        } else {
            coolingOut_ = true;
        }
        e.setpointC = sp_;
        return e;
    }

    const Segment& seg = s[idx_];
    bool heating = seg.targetC >= sp_ - 0.05f;
    float rate = effectiveRate(tempC, seg.rateCph, heating);
    e.rateTarget = heating ? rate : -rate;
    e.segmentIndex = fireDownPhase_ ? (int)p_.segments.size() + idx_ : idx_;
    e.inQuartz = inBand(p_.quartz, tempC);
    e.inCristo = inBand(p_.cristobalite, tempC);

    const float band = settings().holdBandC;
    const float lag = settings().lagLimitC;

    if (!holding_) {
        float dist = seg.targetC - sp_;
        bool reached = fabsf(dist) <= 0.15f;
        if (!reached && rate > 0) {
            float step = rate / 3600.0f * dtSec;
            if (heating) {
                if (isfinite(tempC) && tempC < sp_ - lag) {
                    e.holdBack = true;
                } else {
                    sp_ += step;
                    if (sp_ > seg.targetC) sp_ = seg.targetC;
                }
            } else {
                if (isfinite(tempC) && tempC > sp_ + lag) {
                    e.holdBack = true;  // kiln cooling slower than ramp
                } else {
                    sp_ -= step;
                    if (sp_ < seg.targetC) sp_ = seg.targetC;
                }
            }
            reached = fabsf(seg.targetC - sp_) <= 0.15f;
        }
        if (reached) {
            if (isfinite(tempC) && fabsf(tempC - seg.targetC) <= band) {
                holding_ = true;
                holdLeft_ = seg.holdMin * 60.0f;
            } else {
                e.holdBack = true;  // guaranteed soak
            }
        }
        strncpy(e.segmentType, fireDownPhase_ ? "firedown" : (heating ? "ramp" : "cool"),
                sizeof e.segmentType - 1);
        e.state = fireDownPhase_ ? ST_FIRE_DOWN : ST_RAMP;
        if (e.inQuartz) strncpy(e.segmentType, "quartz", sizeof e.segmentType - 1);
        else if (e.inCristo) strncpy(e.segmentType, "cristo", sizeof e.segmentType - 1);
    }

    if (holding_) {
        e.state = fireDownPhase_ ? ST_FIRE_DOWN : ST_HOLD;
        strncpy(e.segmentType, fireDownPhase_ ? "fd-hold" : "hold", sizeof e.segmentType - 1);
        if (isfinite(tempC) && fabsf(tempC - seg.targetC) > band) {
            e.holdBack = true;
        } else {
            holdLeft_ -= dtSec;
        }
        e.segRemainSec = (uint32_t)fmaxf(holdLeft_, 0);
        if (holdLeft_ <= 0) {
            if (idx_ + 1 < (int)s.size()) {
                enterSegment(idx_ + 1, tempC);
            } else if (!fireDownPhase_ && p_.fireDownEnabled && p_.fireDown.size()) {
                fireDownPhase_ = true;
                enterSegment(0, tempC);
            } else {
                coolingOut_ = true;
            }
        }
    } else {
        float dist = fabsf(seg.targetC - sp_);
        float r = rate > 1 ? rate : 1;
        e.segRemainSec = (uint32_t)(dist / r * 3600.0f + seg.holdMin * 60.0f);
    }

    e.setpointC = sp_;
    uint32_t rest = estimateRemain(s, idx_, sp_, holdLeft_, holding_, tempC, rate);
    if (!fireDownPhase_ && p_.fireDownEnabled)
        rest += estimateRemain(p_.fireDown, 0, p_.segments.back().targetC, 0, false, tempC, 0);
    e.totalRemainSec = rest;

    if (millis() - lastCk_ > 60000) {
        saveCheckpoint();
        lastCk_ = millis();
    }
    return e;
}

void ProfileEngine::saveCheckpoint() {
    Preferences pr;
    pr.begin("run", false);
    pr.putString("name", p_.name);
    pr.putInt("idx", idx_);
    pr.putBool("fd", fireDownPhase_);
    pr.putBool("cool", coolingOut_);
    pr.putFloat("sp", sp_);
    pr.putUInt("el", elapsedMs_);
    pr.putBool("hold", holding_);
    pr.putFloat("hl", holdLeft_);
    pr.putBool("on", running_ && !coolingOut_);
    pr.end();
}

void ProfileEngine::clearCheckpoint() {
    Preferences pr;
    pr.begin("run", false);
    pr.clear();
    pr.end();
}

void ProfileEngine::checkpoint() { saveCheckpoint(); }

bool ProfileEngine::resumeIfPossible(float tempC) {
    Preferences pr;
    pr.begin("run", true);
    bool on = pr.getBool("on", false);
    String name = pr.getString("name", "");
    int idx = pr.getInt("idx", 0);
    bool fd = pr.getBool("fd", false);
    bool cool = pr.getBool("cool", false);
    float sp = pr.getFloat("sp", NAN);
    uint32_t el = pr.getUInt("el", 0);
    bool hold = pr.getBool("hold", false);
    float hl = pr.getFloat("hl", 0);
    pr.end();
    if (!on || !name.length() || !isfinite(sp)) return false;
    if (!isfinite(tempC) || fabsf(tempC - sp) > settings().resumeBandC) {
        Serial.printf("Resume aborted: T=%.1f SP=%.1f\n", tempC, sp);
        clearCheckpoint();
        return false;
    }
    Profile p;
    String err;
    if (!filesLoadProfile(name, p, err)) {
        Serial.printf("Resume: load '%s' failed: %s\n", name.c_str(), err.c_str());
        clearCheckpoint();
        return false;
    }
    p_ = p;
    running_ = true;
    paused_ = false;
    fireDownPhase_ = fd;
    coolingOut_ = cool;
    idx_ = idx;
    sp_ = sp;
    elapsedMs_ = el;
    holding_ = hold;
    holdLeft_ = hl;
    delayUntil_ = 0;
    Serial.printf("Resumed '%s' seg %d SP=%.1f\n", name.c_str(), idx, sp);
    return true;
}
