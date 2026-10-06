#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace third_person {

// Integration must provide validated snapshots, never raw game pointers.
// Spawn generation is required: native unit IDs can be recycled within a session.
struct TargetKey {
    uint64_t session = 0;
    uint32_t id = 0;
    uint32_t generation = 0;
    bool valid() const { return session != 0 && generation != 0; }
    bool operator==(const TargetKey& other) const {
        return session == other.session && id == other.id && generation == other.generation;
    }
};

struct TargetCandidate {
    TargetKey key;
    // Normalized screen coordinates: center=(0,0), edges=(-1,+1).
    float screenX = 0, screenY = 0;
    float distance = 0; // adapter chooses consistent world-distance units
    bool alive = false, hostile = false, targetable = false;
    bool inFront = false, visible = false;
};

struct TargetPolicy {
    float acquireRadius = 0.35f;
    float acquireDistance = 40.0f;
    float retainDistance = 50.0f;
    float switchMargin = 0.04f;
    uint64_t occlusionGraceMs = 300;
};

struct TargetResult {
    TargetKey selected;
    bool locked = false;
    bool attackAllowed = false;
};

class TargetSelector {
public:
    void reset() { soft_ = {}; locked_ = {}; lastVisibleMs_ = 0; }

    TargetResult update(const TargetCandidate* candidates, size_t count, uint64_t session,
                        uint64_t nowMs, bool toggleLock, bool active,
                        const TargetPolicy& policy = {}) {
        if (!active || session == 0 || (!candidates && count)) { reset(); return {}; }
        if (session != session_ || nowMs < lastUpdateMs_) reset();
        session_ = session; lastUpdateMs_ = nowMs;
        const auto usable = [&](const TargetCandidate& c, float range) {
            return c.key.valid() && c.key.session == session && c.alive && c.hostile &&
                c.targetable && std::isfinite(c.distance) && c.distance >= 0 && c.distance <= range;
        };
        const auto score = [&](const TargetCandidate& c) {
            if (!usable(c, policy.acquireDistance) || !c.inFront || !c.visible ||
                !std::isfinite(c.screenX) || !std::isfinite(c.screenY))
                return std::numeric_limits<float>::infinity();
            const float radius = std::hypot(c.screenX, c.screenY);
            return radius <= policy.acquireRadius ? radius : std::numeric_limits<float>::infinity();
        };
        const auto find = [&](TargetKey key) -> const TargetCandidate* {
            if (!key.valid()) return nullptr;
            for (size_t i = 0; i < count; ++i) if (candidates[i].key == key) return &candidates[i];
            return nullptr;
        };
        const bool wasLocked = locked_.valid();
        if (wasLocked && toggleLock) locked_ = {};
        if (locked_.valid()) {
            const auto* c = find(locked_);
            if (!c || !usable(*c, policy.retainDistance)) locked_ = {};
            else {
                if (c->visible) lastVisibleMs_ = nowMs;
                if (nowMs - lastVisibleMs_ > policy.occlusionGraceMs) locked_ = {};
                else return {locked_, true, c->visible && c->inFront};
            }
        }
        const TargetCandidate* best = nullptr;
        float bestScore = std::numeric_limits<float>::infinity();
        for (size_t i = 0; i < count; ++i) {
            const float s = score(candidates[i]);
            if (s < bestScore || (best && s == bestScore && candidates[i].key.id < best->key.id)) {
                best = &candidates[i]; bestScore = s;
            }
        }
        // Hysteresis: small crosshair movement must not flicker between two enemies.
        if (const auto* previous = find(soft_)) {
            const float s = score(*previous);
            if (std::isfinite(s) && s <= bestScore + policy.switchMargin) best = previous;
        }
        soft_ = best ? best->key : TargetKey{};
        if (toggleLock && !wasLocked && best) {
            locked_ = best->key; lastVisibleMs_ = nowMs;
            return {locked_, true, true};
        }
        return {soft_, false, best != nullptr};
    }

private:
    TargetKey soft_, locked_;
    uint64_t session_ = 0, lastVisibleMs_ = 0, lastUpdateMs_ = 0;
};

} // namespace third_person
