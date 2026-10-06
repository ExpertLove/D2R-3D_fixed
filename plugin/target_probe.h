#pragma once

// Passive observation of a traced native predicate. Never replaces its answer,
// initiates attacks, or retains native pointers. Build-specific and signature guarded.
#include <windows.h>
#include <D2RLPlugin/context.h>
#include <cstdint>
#include <cstdio>

namespace target_probe {
using Predicate = int (*)(uintptr_t source, uintptr_t target, int option);
inline Predicate original = nullptr;
struct Record {
    uint32_t sourceId, targetId, targetClass, mode, flags;
    int option, result;
};
inline SRWLOCK lock = SRWLOCK_INIT;
inline Record pending[32]{}, seen[64]{};
inline unsigned pendingCount = 0, seenCount = 0;

// Native pointers only read during the original call; SEH guards a failed read.
inline bool Snapshot(uintptr_t source, uintptr_t target, int option, int result, Record* r) noexcept {
    __try {
        if (!source || !target || *(const uint32_t*)source != 0 || *(const uint32_t*)target != 1) return false;
        *r = {*(const uint32_t*)(source+8), *(const uint32_t*)(target+8),
              *(const uint32_t*)(target+4), *(const uint32_t*)(target+12),
              *(const uint32_t*)(target+0x124), option, result};
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool Equal(const Record& a, const Record& b) {
    return a.sourceId==b.sourceId && a.targetId==b.targetId && a.targetClass==b.targetClass &&
           a.mode==b.mode && a.flags==b.flags && a.option==b.option && a.result==b.result;
}
inline int Observe(uintptr_t source, uintptr_t target, int option) {
    const int result = original(source,target,option);
    Record record{};
    if (!Snapshot(source,target,option,result,&record) || !TryAcquireSRWLockExclusive(&lock)) return result;
    bool duplicate = false;
    for (unsigned i=0;i<seenCount;++i) if (Equal(seen[i],record)) { duplicate=true; break; }
    if (!duplicate && seenCount<64 && pendingCount<32) {
        seen[seenCount++]=record;
        pending[pendingCount++]=record;
    }
    ReleaseSRWLockExclusive(&lock);
    return result;
}
inline bool Install(const D2RL::PluginContext* ctx) {
    // RVA 0x971e0: source in RCX, target in RDX, option in R8D; observed callers use -1.
    const uint8_t expected[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
    const bool ok=ctx->InstallInlineHook(0x971e0,expected,sizeof expected,&Observe,&original);
    ctx->LogInfo(ok ? "target probe: passive native relation observer installed; answers unchanged, no auto targeting" :
                     "target probe: signature/hook rejected; probe disabled");
    return ok;
}
inline void Flush(const D2RL::PluginContext* ctx) {
    if (!ctx) return;
    Record batch[32]; unsigned count=0;
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    count=pendingCount;
    for (unsigned i=0;i<count;++i) batch[i]=pending[i];
    pendingCount=0;
    ReleaseSRWLockExclusive(&lock);
    for (unsigned i=0;i<count;++i) {
        const auto& r=batch[i]; char message[224];
        snprintf(message,sizeof message,
                 "target probe: source=%u target=%u class=%u mode=%u flags=0x%08X option=%d nativeResult=%d (diagnostic only)",
                 r.sourceId,r.targetId,r.targetClass,r.mode,r.flags,r.option,r.result);
        ctx->LogInfo(message);
    }
}
} // namespace target_probe
