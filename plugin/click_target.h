#pragma once
#include <cstdint>

// Policy only: no camera, game memory, input injection or attack calls.
// All calls must be serialized on the native input/game thread. The adapter
// must resolve a fresh native pick, not an input-thread cached hover snapshot.
namespace click_target {
struct Identity {
    std::uint64_t session = 0;
    std::uint64_t lifetime = 0; // Verified spawn token; observation count is NOT sufficient.
    std::uint32_t id = 0;
    bool valid() const { return session != 0 && lifetime != 0; }
    bool operator==(const Identity& b) const {
        return session == b.session && lifetime == b.lifetime && id == b.id;
    }
};
struct Candidate {
    Identity identity;
    bool liveHostileMonster = false;
    bool targetable = false;
};
struct Context {
    std::uint64_t session = 0;
    bool active = false; // F10 + focus + live player + no blocking UI.
    bool shift = false;
};
enum class PickKind { Unavailable, NonMonster, Monster };
struct Pick {
    PickKind kind = PickKind::Unavailable;
    Candidate candidate;
};
enum class ClickResult { PassNative, Consume };

class State {
public:
    void update(Context next) {
        if (!next.active || next.session == 0 || next.session != context_.session)
            clear();
        context_ = next;
        // Keep button ownership across focus/session changes so a consumed
        // down never leaks its up (nor is a native down deprived of its up).
    }
    void clear() { target_ = {}; }
    Identity target() const { return target_; }
    bool leftHeld() const { return leftHeld_; }
    bool selectionHeld() const { return leftHeld_ && consumed_; }

    // Explicit F selection; independent of cursor/Shift and mouse ownership.
    bool choose(const Pick& pick) {
        if(!context_.active || !context_.session) return false;
        clear();
        if(pick.kind==PickKind::Monster && eligible(pick.candidate))
            target_=pick.candidate.identity;
        return target_.valid();
    }

    // Legacy .12 policy, retained for regression tests; NOT used by .13 runtime.
    ClickResult leftDown(const Pick& pick) {
        if (leftHeld_) return consumed_ ? ClickResult::Consume : ClickResult::PassNative;
        leftHeld_ = true;
        consumed_ = false;
        if (!context_.active || !context_.shift || context_.session == 0)
            return ClickResult::PassNative;
        // Even an ineligible monster must not receive an accidental selection
        // attack. Non-monster UI/items retain native behavior. An unavailable
        // picker is fail-closed while Shift is held in this feature's mode.
        if (pick.kind == PickKind::NonMonster) return ClickResult::PassNative;
        consumed_ = true;
        clear();
        if (pick.kind == PickKind::Monster && eligible(pick.candidate))
            target_ = pick.candidate.identity;
        return ClickResult::Consume;
    }
    ClickResult leftUp() {
        const bool consume = leftHeld_ && consumed_;
        leftHeld_ = consumed_ = false;
        return consume ? ClickResult::Consume : ClickResult::PassNative;
    }
    // Invoke on validated lifecycle observations; missing/dead/recycled units
    // clear selection. Never reacquire automatically when a unit reappears.
    void validate(const Candidate& current) {
        if (!target_.valid()) return;
        if (!eligible(current) || !(current.identity == target_)) clear();
    }
    // Only consulted inside a verified, explicit native manual-action path.
    // Returning true is permission to substitute the freshly resolved unit,
    // NOT an instruction to dispatch an attack or retain its pointer.
    bool manualAim(const Candidate& current, bool explicitAction,
                   bool skillSupportsUnit, bool nativeCanAim) {
        validate(current);
        return explicitAction && skillSupportsUnit && nativeCanAim &&
            context_.active && !context_.shift && !(leftHeld_ && consumed_) && target_.valid();
    }
private:
    bool eligible(const Candidate& c) const {
        return context_.active && c.identity.valid() &&
            c.identity.session == context_.session && c.liveHostileMonster && c.targetable;
    }
    Context context_;
    Identity target_;
    bool leftHeld_ = false;
    bool consumed_ = false;
};
} // namespace click_target
