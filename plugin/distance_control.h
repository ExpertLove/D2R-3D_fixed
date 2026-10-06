#pragma once
#include <atomic>
#include <cstdint>
namespace distance_control {
inline constexpr char Provider[]="d2r-3d-renderdistance",ServiceName[]="distance-control";
inline constexpr std::uint32_t Version=1;
enum Flags : std::uint32_t { Present=1,Supported=2,Requested=4,Enabled=8,Pending=16,SaveFailed=32,Fault=64 };
// Loader service lease pins the provider until its last consumer call finishes.
// API calls publish intent/read atomics only; native changes run on client DRLG updates.
struct Api {
    std::uint32_t size,version;
    std::uint32_t (__cdecl* state)() noexcept;
    std::uint32_t (__cdecl* setEnabled)(std::uint32_t) noexcept;
};
class State {
    std::atomic<std::uint64_t> wanted_{0},processed_{0};
    std::atomic<bool> supported_{false},enabled_{false},fault_{false},saveFailed_{false};
public:
    void support(bool value) { supported_=value; }
    bool request(bool value) {
        if(!supported_ || fault_) return false;
        auto before=wanted_.load();
        while(!wanted_.compare_exchange_weak(before,((before&~std::uint64_t(1))+2)|unsigned(value))) {}
        return true;
    }
    std::uint64_t ticket() const { return wanted_.load(); }
    bool pending(std::uint64_t t) const { return t!=processed_.load() && supported_ && !fault_; }
    static bool desired(std::uint64_t t) { return (t&1)!=0; }
    void complete(std::uint64_t t,bool saveOk) {
        enabled_=desired(t);saveFailed_=!saveOk;processed_=t;
    }
    void fault() { fault_=true;enabled_=false; }
    std::uint32_t flags() const {
        const auto wanted=wanted_.load();
        return Present | (supported_ && !fault_?Supported:0) | (desired(wanted)?Requested:0) |
            (enabled_?Enabled:0) | (pending(wanted)?Pending:0) | (saveFailed_?SaveFailed:0) | (fault_?Fault:0);
    }
};
inline const char* Label(std::uint32_t flags) {
    if(!(flags&Present)) return "";
    if(!(flags&Supported)) return "DISTANCE: N/A";
    if(flags&SaveFailed) return "DISTANCE: UNSAVED";
    if(flags&Pending) return (flags&Requested)?"DISTANCE: WAIT ON":"DISTANCE: WAIT OFF";
    return (flags&Enabled)?"DISTANCE: ON":"DISTANCE: OFF";
}
}
