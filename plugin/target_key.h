#pragma once
namespace click_target {
struct KeyDecision { bool consume=false; bool request=false; };
// Input-thread only. Eligibility is sampled on first physical down. Never
// steal native repeats/up from a key held before Shift/F10/feature activation.
class SelectionKey {
public:
    void initialize(bool held) { held_=held; owned_=false; }
    KeyDecision event(bool down,bool eligible) {
        if(down) {
            if(held_) return {owned_,false};
            held_=true; owned_=eligible;
            return {owned_,owned_};
        }
        const bool consume=held_ && owned_;
        held_=owned_=false;
        return {consume,false};
    }
private:
    bool held_=false,owned_=false;
};
} // namespace click_target
