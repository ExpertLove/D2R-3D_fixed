#pragma once
// Experimental, opt-in native adapter. No camera outputs, timer attacks or
// target-global writes. Original native actions are called only from their hooks.
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <D2RLPlugin/context.h>
#include "click_target_native.h"
#include "target_runtime.h"
#include "nearest_target.h"
#include "elite_target.h"

namespace click_aim {
using namespace click_target;
inline std::atomic<bool> ready{false}, enabled{false}, eliteReady{false};
inline std::atomic<unsigned> controls{0}; // active=1, Shift=2
inline std::atomic<std::uint64_t> epoch{1};
inline std::atomic<unsigned> selections{0}, aims{0}, visuals{0}, failures{0};
inline std::atomic<unsigned> nameVisuals{0}, glowVisuals{0}, foreignVisuals{0};
inline std::atomic<unsigned> searches{0},noTarget{0};
struct SelectionIntent { std::uint64_t serial=0,session=0; ULONGLONG at=0; int direction=1; bool elite=false,center=false; };
inline SRWLOCK intentLock=SRWLOCK_INIT;
inline std::array<SelectionIntent,64> intents{};
inline unsigned intentCount=0;
inline std::atomic<std::uint64_t> selectionSerial{0};
inline void ClearIntents() {
    AcquireSRWLockExclusive(&intentLock); intentCount=0; ReleaseSRWLockExclusive(&intentLock);
}
inline bool PopIntent(std::uint64_t boundary,SelectionIntent& out) {
    AcquireSRWLockExclusive(&intentLock);
    const bool found=intentCount && intents[0].serial<=boundary;
    if(found) { out=intents[0]; for(unsigned i=1;i<intentCount;++i) intents[i-1]=intents[i]; --intentCount; }
    ReleaseSRWLockExclusive(&intentLock); return found;
}
inline std::uintptr_t base=0;
inline bool(*controlGate)()=nullptr;
inline SRWLOCK lock=SRWLOCK_INIT;
inline State state;
inline Lifetime lifetime;
struct RingPoint { Identity identity; float x=0,z=0; ULONGLONG tick=0; bool valid=false; };
inline RingPoint ringPoint;
inline TargetCycle cycle, eliteCycle;
inline bool lastWasElite=false;
inline bool lastPickOk=true;
inline int cycleDirection=1; // world thread under lock
inline std::atomic<std::uint64_t> leftSerial{0}, leftSession{0};
inline std::atomic<ULONGLONG> leftAt{0};
inline std::atomic<bool> leftSelect{false};
inline std::uint64_t handledLeftSerial=0;
inline bool selectionLeftHeld=false, nativeLeftPair=false;
inline thread_local bool releaseScope=false;
using MouseFn=bool(__fastcall*)(std::uintptr_t);
inline MouseFn originalLeftDown=nullptr, originalUiLeftUp=nullptr;
// Input thread publishes a physical-down latch only. Never reads units.
inline void PhysicalLeftDown() {
    const auto c=controls.load();
    leftSession.store(epoch.load()); leftAt.store(GetTickCount64());
    leftSelect.store(ready.load() && enabled.load() && (c&3)==3);
    leftSerial.fetch_add(1);
}
inline DWORD ownerThread=0;
inline std::uintptr_t playerWitness=0;
inline SelectionReader selectionReader;
inline third_person::ClientMemoryReader memory;
inline thread_local bool manualScope=false;
inline thread_local bool predicateScope=false;
using PickUpdateFn=void(__fastcall*)(unsigned);
using RequestFn=int(__fastcall*)(std::uintptr_t,int,int,int,std::uint16_t);
using GetterFn=std::uintptr_t(__fastcall*)(unsigned);
using FreeFn=void(__fastcall*)(std::uintptr_t);
using PredicateFn=int(__fastcall*)(std::uintptr_t);
inline PickUpdateFn originalPickUpdate=nullptr;
inline RequestFn originalRequest=nullptr;
inline GetterFn originalGetter=nullptr;
inline FreeFn originalClientFree=nullptr, originalCommonFree=nullptr;
inline PredicateFn originalPredicate=nullptr;
struct GuardLock {
    GuardLock() { AcquireSRWLockExclusive(&lock); }
    ~GuardLock() { ReleaseSRWLockExclusive(&lock); }
};
inline void Invalidate() { epoch.fetch_add(1); ClearIntents(); }
inline void Publish(bool active,bool shift) {
    const unsigned next=(active?1u:0u)|(shift?2u:0u);
    const auto before=controls.exchange(next);
    if ((before&1) && !(next&1)) Invalidate();
}
inline const char* Toggle() {
    if (!ready.load()) return "clickaim unavailable: native guards/hooks failed";
    const bool next=!enabled.load(); enabled.store(next); Invalidate();
    return next ? "clickaim ON: default F/G cycle, X elites, V center, Shift+LMB selects; gear configures keys; manual attacks only" : "clickaim OFF: target hotkeys and mouse input native";
}
inline Context context() {
    const auto c=controls.load();
    return {epoch.load(),ready.load() && enabled.load() && (c&1)!=0 && (!controlGate || controlGate()),(c&2)!=0};
}
template<class T> inline bool get(std::uintptr_t at,T& out) { return memory(at,&out,sizeof out); }
inline std::uintptr_t resolve(unsigned id,unsigned type) {
    // Read-only bounded hash walk; never forward an old stored unit pointer.
    if (type>5 || id==0xffffffffu) return 0;
    std::uintptr_t unit=0;
    if (!get(base+0x2a23910+type*0x400+(id&127)*8,unit)) return 0;
    std::array<std::uintptr_t,512> seen{}; unsigned count=0;
    while (unit) {
        if (count==seen.size()) return 0;
        for(unsigned i=0;i<count;++i) if(seen[i]==unit) return 0;
        seen[count++]=unit;
        unsigned header[4]{};
        if (!memory(unit,header,sizeof header)) return 0;
        if (header[0]==type && header[2]==id) return unit;
        if (!get(unit+0x158,unit)) return 0;
    }
    return 0;
}
inline std::uintptr_t local(unsigned& slot) {
    unsigned id=0;
    if (!get(base+0x2a23704,slot) || slot>=8 || !get(base+0x2a238f0+slot*4,id)) return 0;
    return resolve(id,0);
}
inline bool hostile(std::uintptr_t unit) {
    unsigned header[4]{},flags=0;
    if (!memory(unit,header,sizeof header) || header[0]!=1 || header[3]==0 || header[3]==12 ||
        !get(unit+0x124,flags) || (flags&0x0e)!=0x0e ||
        third_person::ReadAlignment(memory,unit)!=third_person::Alignment::Evil) return false;
    // Exclude registered pets of ANY owner. Unknown/broken list fails closed.
    std::uintptr_t pet=0;
    if (!get(base+0x2a4dc10,pet)) return false;
    std::array<std::uintptr_t,512> seen{}; unsigned count=0;
    while(pet) {
        if(count==seen.size()) return false;
        for(unsigned i=0;i<count;++i) if(seen[i]==pet) return false;
        seen[count++]=pet;
        unsigned id=0;
        if(!get(pet+8,id) || id==header[2] || !get(pet+0x30,pet)) return false;
    }
    return true;
}
inline void clear() { state.clear(); lifetime.clear(); ringPoint={}; cycle.reset(); eliteCycle.reset(); lastWasElite=false; lastPickOk=true; }
inline bool CopyRingPoint(RingPoint& out) {
    GuardLock guard;
    const auto c=context(); const auto now=GetTickCount64();
    if(!c.active || !ringPoint.valid || ringPoint.identity.session!=c.session ||
       !(ringPoint.identity==state.target()) || now<ringPoint.tick || now-ringPoint.tick>150) return false;
    out=ringPoint; return true; // copies only; render thread never dereferences units
}
// Under lock; native pointer reads only on the thread owning world input.
inline bool sync(Context c) {
    if(ownerThread && ownerThread!=GetCurrentThreadId()) return false;
    state.update(c);
    unsigned slot=0; const auto player=local(slot);
    unsigned mode=0;
    if(!player || !get(player+0xc,mode) || mode==0 || mode==17 ||
        (playerWitness && playerWitness!=player)) { clear(); playerWitness=player; return false; }
    playerWitness=player;
    if(!c.active) { lifetime.clear(); return false; }
    return true;
}
inline std::uintptr_t fresh(Context c, bool manual) {
    if(!sync(c)) return 0;
    const auto key=state.target();
    if(!key.valid()) return 0;
    const auto unit=resolve(key.id,1);
    const auto identity=lifetime.resolve(c.session,key.id,unit);
    Candidate candidate{identity,unit && hostile(unit),true};
    state.validate(candidate);
    if(!state.target().valid()) { lifetime.clear(); ringPoint={}; return 0; }
    // Native request builder retains skill/range checks. No custom skill call.
    if(manual && !state.manualAim(candidate,true,true,true)) return 0;
    if(c.session!=epoch.load() || !context().active) { clear(); return 0; }
    std::uintptr_t path=0; std::uint32_t x=0,z=0;
    ringPoint={};
    if(get(unit+0x38,path) && path && get(path,x) && get(path+4,z))
        ringPoint={key,float(x)/32768.0f,float(z)/32768.0f,GetTickCount64(),true};
    return unit;
}
inline third_person::TargetProvider nearestProvider;
inline NearestResult FindNearest(std::uintptr_t player,std::uint64_t session) {
    unsigned id=0; third_person::TargetFrame frame;
    if(!get(player+8,id) || !nearestProvider.sample(memory,base,session,id,GetTickCount64(),frame)) return {};
    const auto target=state.target();
    return cycle.next(frame,80.0f,cycleDirection,target.valid()?target.id:UINT32_MAX);
}
using NearestFn=NearestResult(*)(std::uintptr_t,std::uint64_t);
inline NearestFn nearestFinder=&FindNearest;
inline NearestResult FindElite(std::uintptr_t player,std::uint64_t session) {
    if(!eliteReady.load()) return {};
    unsigned id=0; third_person::TargetFrame frame;
    if(!get(player+8,id) || !nearestProvider.sample(memory,base,session,id,GetTickCount64(),frame)) return {};
    unsigned count=0;
    for(unsigned i=0;i<frame.count;++i) {
        const auto unit=resolve(frame.units[i].key.id,1);
        if(unit && IsElite(ReadElite(memory,base,unit))) frame.units[count++]=frame.units[i];
    }
    frame.count=count;
    const auto target=state.target();
    if(!lastWasElite || !target.valid()) eliteCycle.reset();
    return eliteCycle.next(frame,80.0f,1,lastWasElite && target.valid()?target.id:UINT32_MAX);
}
inline NearestFn eliteFinder=&FindElite;
inline NearestFn centerFinder=nullptr;

// Called by LL keyboard thread. Publishes intent only: no unit reads/native calls.
inline bool QueueSelection(int direction=1,bool elite=false,bool center=false) {
    const auto c=context();
    if(!c.active || (elite && !eliteReady.load())) return false;
    AcquireSRWLockExclusive(&intentLock);
    const bool room=intentCount<intents.size();
    if(room) intents[intentCount++]={selectionSerial.fetch_add(1)+1,c.session,GetTickCount64(),direction<0?-1:1,elite,center};
    ReleaseSRWLockExclusive(&intentLock);
    return room;
}
inline void __fastcall PickUpdate(unsigned slot) {
    // Reuse the verified native local/UI update thread as a safe execution point.
    // Capture F intent before the pass. Hover result and cursor are NOT used.
    const auto boundary=selectionSerial.load();
    originalPickUpdate(slot);
    if(!ready.load()) return;
    GuardLock guard;
    auto c=context();
    if(ownerThread && ownerThread!=GetCurrentThreadId()) return;
    unsigned localSlot=0;
    if(!local(localSlot) || slot!=localSlot) return;
    if(!ownerThread && c.active) ownerThread=GetCurrentThreadId();
    if(ownerThread!=GetCurrentThreadId()) return;
    if(!sync(c)) { ClearIntents(); return; }
    if(state.target().valid()) fresh(c,false); // death/removal invalidation even without drawing
    SelectionIntent intent;
    while(PopIntent(boundary,intent)) {
    c=context();
    if(!c.active || c.session!=intent.session || GetTickCount64()-intent.at>150) continue;
    unsigned slotNow=0; const auto player=local(slotNow);
    ++searches;
    // No live target after a good pick (death/Escape/session) restarts at nearest;
    // after a failed pick keep advancing so one bad candidate cannot stall F.
    if(!state.target().valid() && lastPickOk) cycle.reset();
    cycleDirection=intent.direction;
    const auto finder=intent.center?centerFinder:intent.elite?eliteFinder:nearestFinder;
    const auto result=finder?finder(player,c.session):NearestResult{};
    // Explicit center/elite misses preserve current selection.
    if((intent.center || intent.elite) && (!result.complete || !result.found)) { ++noTarget; continue; }
    Pick pick{PickKind::NonMonster,{}};
    if(result.complete && result.found) {
        const auto unit=resolve(result.id,1); // provider generation is NOT an attack handle
        const bool eligible=unit && hostile(unit) && (!intent.elite || IsElite(ReadElite(memory,base,unit)));
        if((intent.elite || intent.center) && !eligible) { ++noTarget; continue; }
        pick={PickKind::Monster,{eligible?lifetime.bind(c.session,result.id,unit,true):Identity{},eligible,eligible}};
    }
    const auto current=context();
    if(!current.active || current.session!=c.session) { clear(); return; }
    if(state.choose(pick)) { lastWasElite=intent.elite; lastPickOk=true; selections.fetch_add(1); fresh(current,false); }
    else { lastPickOk=false; lifetime.clear(); ringPoint={}; ++noTarget; }
    }
}
// Native WORLD down runs only after native UI declined the click. Suppress
// before world button-state writes, not merely inside the late action builder.
inline bool __fastcall LeftDown(std::uintptr_t event) {
    {
        GuardLock guard;
        const auto serial=leftSerial.load();
        const bool newDown=serial!=handledLeftSerial;
        if(newDown) { selectionLeftHeld=false; nativeLeftPair=false; handledLeftSerial=serial; }
        if(selectionLeftHeld) return true;
        const auto c=context();
        const bool latched=newDown && leftSelect.load();
        if(latched) {
            // A cancelled/late Shift-click is consumed, never converted to attack.
            selectionLeftHeld=true;
            const auto now=GetTickCount64();
            if(!c.active || c.session!=leftSession.load() || now<leftAt.load() || now-leftAt.load()>250) return true;
            if(ownerThread && ownerThread!=GetCurrentThreadId()) return true;
            unsigned slot=0; if(!local(slot)) return true;
            if(!ownerThread) ownerThread=GetCurrentThreadId();
            if(!sync(c)) return true;
            originalPickUpdate(slot); // fresh native cursor picker, not pinned Getter
            const auto pick=selectionReader.sample(memory);
            if(!pick.complete) return true;
            if(!pick.available || pick.type!=1) {
                selectionLeftHeld=false; nativeLeftPair=true; // ground/item: preserve entire native pair
            } else if(!hostile(pick.address)) {
                if(pick.mode==0 || pick.mode==12) return true; // never attack a selection corpse
                const auto alignment=third_person::ReadAlignment(memory,pick.address);
                if(alignment==third_person::Alignment::Unknown || alignment==third_person::Alignment::Evil) return true;
                selectionLeftHeld=false; nativeLeftPair=true; // verified neutral/good NPC/pet: native pair
            } else {
                const auto current=context();
                if(!current.active || current.session!=c.session || serial!=leftSerial.load() || resolve(pick.id,1)!=pick.address) return true;
                const auto id=lifetime.bind(c.session,pick.id,pick.address,true);
                state.choose({PickKind::Monster,{id,true,true}});
                lastWasElite=false; lastPickOk=true; ++selections; fresh(current,false);
                return true;
            }
        }
    }
    return originalLeftDown(event);
}
inline bool __fastcall UiLeftUp(std::uintptr_t event) {
    bool owned=false;
    { GuardLock guard; owned=selectionLeftHeld; }
    const bool before=releaseScope; releaseScope=owned;
    const bool result=originalUiLeftUp(event); // preserve UI button bookkeeping
    releaseScope=before;
    { GuardLock guard; selectionLeftHeld=false; }
    return owned || result; // prevent world up (including after Shift/mode loss)
}
inline bool manualCaller(std::uintptr_t rva,int action) {
    return (action==0 && rva==0x840ae) || (action==1 && rva==0x8ba1f) ||
        (action==2 && rva==0x94296) || (action==3 && rva==0x83ad1) ||
        (action==4 && rva==0x8baac) || (action==5 && rva==0x83bf7);
}
inline int __fastcall Request(std::uintptr_t player,int action,int x,int y,std::uint16_t modifiers) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-base;
    const auto c=context();
    bool allow=false;
    {
        GuardLock guard;
        if(action>=0 && action<=2 && (selectionLeftHeld || releaseScope)) return 0;
        if(ownerThread==GetCurrentThreadId()) {
            unsigned slot=0;
            allow=c.active && !c.shift && !(action<=2 && nativeLeftPair) && manualCaller(caller,action) && player==local(slot);
            // Never redirect a keyboard/WASD repeat as a mouse attack.
            if(action==1 || action==4)
                allow=allow && (GetAsyncKeyState(action==1?VK_LBUTTON:VK_RBUTTON)&0x8000)!=0;
        }
    }
    const bool saved=manualScope;
    manualScope=allow;
    const int result=originalRequest(player,action,x,y,modifiers);
    manualScope=saved;
    return result;
}
inline std::uintptr_t GetterForCaller(unsigned slot,std::uintptr_t caller) {
    const bool action=manualScope && caller==0xfe4e8;
    const bool visual=caller==0x880a9f || caller==0x784f98 || predicateScope;
    if(ready.load() && (action || visual)) {
        GuardLock guard;
        unsigned localSlot=0;
        if(visual && ownerThread && ownerThread!=GetCurrentThreadId() && enabled.load() && state.target().valid())
            foreignVisuals.fetch_add(1);
        if(ownerThread==GetCurrentThreadId() && local(localSlot) && slot==localSlot) {
            if(const auto unit=fresh(context(),action)) {
                (action?aims:visuals).fetch_add(1);
                if(visual) (caller==0x880a9f?nameVisuals:glowVisuals).fetch_add(1);
                return unit;
            }
        }
    }
    return originalGetter(slot);
}
inline std::uintptr_t __fastcall Getter(unsigned slot) {
    return GetterForCaller(slot,reinterpret_cast<std::uintptr_t>(_ReturnAddress())-base);
}
inline int __fastcall Predicate(std::uintptr_t unit) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-base;
    const bool saved=predicateScope;
    predicateScope=caller==0x784fb0;
    const int result=originalPredicate(unit);
    predicateScope=saved;
    return result;
}
inline void destroying(std::uintptr_t unit) {
    GuardLock guard;
    // Both hooks run before original destruction; never dereference unit here.
    if(lifetime.destroying(unit)) {
        state.clear(); ringPoint={};
        if(ownerThread && ownerThread!=GetCurrentThreadId()) {
            enabled.store(false); failures.fetch_add(1); Invalidate();
        }
    }
    if(unit && unit==playerWitness) { clear(); playerWitness=0; Invalidate(); }
}
inline void __fastcall ClientFree(std::uintptr_t unit) { destroying(unit); originalClientFree(unit); }
inline void __fastcall CommonFree(std::uintptr_t unit) { destroying(unit); originalCommonFree(unit); }

inline bool Install(const D2RL::PluginContext* ctx,std::uintptr_t image,bool(*gate)()) {
    base=image; controlGate=gate;
    if(!selectionReader.initialize(memory,base) || !third_person::TargetLayoutSupported(base)) {
        ctx->LogWarn("clickaim: layout not supported; feature unavailable"); return false;
    }
    eliteReady.store(EliteLayoutSupported(memory,base));
    ctx->LogInfo(eliteReady.load()?"elite X: guarded champion/unique/superunique + MonStats boss filter ready":"elite X: layout rejected; X stays native, F/G unchanged");
    // Validate scoped callsites as well as entries: a getter hook must never
    // accidentally redirect a different native consumer after a game update.
    const std::uintptr_t requestReturns[]={0x840ae,0x8ba1f,0x94296,0x83ad1,0x8baac,0x83bf7};
    const std::uintptr_t getterReturns[]={0xfe4e8,0x880a9f,0x784f98,0x9b7e9};
    const auto callMatches=[&](std::uintptr_t ret,std::uintptr_t target) {
        unsigned char bytes[5]{}; std::int32_t displacement=0;
        if(!memory(base+ret-5,bytes,5) || bytes[0]!=0xe8) return false;
        std::memcpy(&displacement,bytes+1,4);
        return base+ret+displacement==base+target;
    };
    for(auto ret:requestReturns) if(!callMatches(ret,0xfe3b0)) return false;
    for(auto ret:getterReturns) if(!callMatches(ret,0xf1900)) return false;
    if(!callMatches(0x784fb0,0x9b7d0)) return false;
    unsigned char picker[8]{};
    if(!memory(base+0xf1cc0,picker,8) || std::memcmp(picker,"\x48\x8b\xc4\x89\x48\x08\x55\x53",8)) return false;
    struct Hook { std::uintptr_t rva; const char* bytes; unsigned size; void* fn; void** original; };
    const Hook hooks[]={
        {0x83fc0,"\x40\x57\x48\x83\xec\x30\x48\x8b\xf9",9,(void*)&LeftDown,(void**)&originalLeftDown},
        {0x83f870,"\x48\x89\x74\x24\x20\x55\x48\x8d\xac\x24\xe0\xfd\xff\xff",14,(void*)&UiLeftUp,(void**)&originalUiLeftUp},
        {0xf1cc0,"\x48\x8b\xc4\x89\x48\x08\x55\x53",8,(void*)&PickUpdate,(void**)&originalPickUpdate},
        {0xfe3b0,"\x48\x89\x5c\x24\x10\x55\x56\x57",8,(void*)&Request,(void**)&originalRequest},
        {0xf1900,"\x40\x53\x55\x56\x48\x83\xec\x30",8,(void*)&Getter,(void**)&originalGetter},
        {0x9b7d0,"\x48\x89\x74\x24\x10\x57\x48\x83\xec\x20",10,(void*)&Predicate,(void**)&originalPredicate},
        {0x9f0c0,"\x48\x89\x5c\x24\x10\x57\x48\x83\xec\x20",10,(void*)&ClientFree,(void**)&originalClientFree},
        {0x349250,"\x48\x85\xc9\x0f\x84\xba\x00\x00\x00",9,(void*)&CommonFree,(void**)&originalCommonFree},
    };
    // Validate ALL guards before installing any. Partial installation remains
    // pass-through and feature cannot be enabled; SDK owns hook teardown.
    for(const auto& h:hooks) {
        unsigned char bytes[16]{};
        if(!memory(base+h.rva,bytes,h.size) || std::memcmp(bytes,h.bytes,h.size)) {
            ctx->LogWarn("clickaim: hook guard failed; native input unchanged"); return false;
        }
    }
    bool ok=true;
    for(const auto& h:hooks)
        if(!ctx->InstallInlineHook(h.rva,reinterpret_cast<const std::uint8_t*>(h.bytes),h.size,h.fn,h.original)) ok=false;
    ready.store(ok);
    ctx->LogInfo(ok?"clickaim: 8 guarded hooks ready; F/G cycle, Shift+LMB selects, native name/HP panel; OFF by default, command clickaim":"clickaim: incomplete hooks; feature OFF");
    return ok;
}
inline void Flush(const D2RL::PluginContext* ctx) {
    static ULONGLONG last=0;
    const auto now=GetTickCount64(); if(now-last<5000) return; last=now;
    const unsigned s=selections.exchange(0),a=aims.exchange(0),v=visuals.exchange(0),f=failures.exchange(0);
    const unsigned n=nameVisuals.exchange(0),g=glowVisuals.exchange(0),other=foreignVisuals.exchange(0);
    const unsigned search=searches.exchange(0),miss=noTarget.exchange(0);
    if(s || a || v || f || other || search) { char line[256]; std::snprintf(line,sizeof line,"clickaim: searches=%u no-target=%u selected=%u manual-aim=%u native-visual=%u names=%u glow=%u other-thread=%u thread-fault=%u",search,miss,s,a,v,n,g,other,f); ctx->LogInfo(line); }
}
} // namespace click_aim
