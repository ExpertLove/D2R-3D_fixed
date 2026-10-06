#pragma once
namespace third_person {
enum class CaptureAction { Free, Recenter, Track };
// Mode is owned by F10/lifecycle. Shift only suspends it, never re-enables it.
class CursorHold {
    bool tracking_=false;
public:
    CaptureAction update(bool modeActive,bool shiftHeld) {
        if (!modeActive || shiftHeld) { reset(); return CaptureAction::Free; }
        if (!tracking_) { tracking_=true; return CaptureAction::Recenter; }
        return CaptureAction::Track;
    }
    void reset() { tracking_=false; }
};
// Own only complete physical Shift pairs started in our mode. A modifier held
// before F10 must retain its native key-up. Both Shift keys work independently.
class ShiftOwnership {
    unsigned held_=0,owned_=0;
public:
    void initialize(unsigned held) { held_=held&3; owned_=0; }
    bool held() const { return held_!=0; }
    bool event(unsigned bit,bool down,bool capture) {
        if (down) {
            const bool wasHeld=(held_&bit)!=0;
            held_|=bit;
            if (!wasHeld && capture) owned_|=bit;
            return (owned_&bit)!=0;
        }
        const bool swallow=(owned_&bit)!=0;
        held_&=~bit; owned_&=~bit;
        return swallow;
    }
};
} // namespace third_person
