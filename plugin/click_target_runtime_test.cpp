#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "click_target_runtime.h"
#include "target_key.h"
#include "target_health.h" // legacy reader regression only; not used by runtime
#include <cstdlib>
using namespace click_aim;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
namespace {
int requests=0,passes=0,frees=0,insidePass=0;
std::uintptr_t enemy=0;
bool found=true,complete=true;
int samples=0,downs=0,ups=0,lastDirection=0;
NearestResult nearest(std::uintptr_t,std::uint64_t) { ++samples; lastDirection=cycleDirection; return {complete,found,7,2}; }
bool __fastcall mouseDown(std::uintptr_t) { ++downs; return false; }
bool __fastcall mouseUp(std::uintptr_t) { ++ups; if(releaseScope) CHECK(Request(0,2,0,0,0)==0); return false; }
int __fastcall nativeRequest(std::uintptr_t,int,int,int,std::uint16_t) { ++requests; return 42; }
std::uintptr_t __fastcall nativeGetter(unsigned) { return 123; }
void __fastcall picker(unsigned) {
    ++passes;
    const auto action=insidePass; insidePass=0;
    if(action==1) Publish(true,false);
    if(action==2) CHECK(QueueSelection());
    if(action==3) Publish(false,false);
}
void __fastcall freeUnit(std::uintptr_t) { CHECK(!state.target().valid()); ++frees; }
template<class T> void write(std::uintptr_t at,T value) { std::memcpy((void*)at,&value,sizeof value); }
void signatures() {
    std::memcpy((void*)(base+0x8b2d0),"\x8b\x05\x2e\x84\x99\x02\xc3",7);
    std::memcpy((void*)(base+0x9a5d0),"\x4c\x63\xca\x48\x8d\x05\x36\x93\x98\x02",10);
    std::memcpy((void*)(base+0xf1935),"\x48\x8d\x2d\xf4\xfc\x94\x02",7);
    std::memcpy((void*)(base+0xf193f),"\x80\x7c\xc5\x00\x00",5);
    std::memcpy((void*)(base+0xf1996),"\x8b\x7c\xc5\x04",4);
    std::memcpy((void*)(base+0xf19c3),"\x8b\x4c\xc5\x08",4);
    std::memcpy((void*)(base+0xf1c9f),"\x48\x8d\x0d\x8b\xf9\x94\x02",7);
}
void choose() { Publish(true,false); CHECK(QueueSelection()); PickUpdate(0); CHECK(state.target().valid()); }
void keyTests() {
    click_target::SelectionKey key;
    auto d=key.event(true,true); CHECK(d.consume && d.request);
    d=key.event(true,true); CHECK(d.consume && !d.request); // repeat: no reselection
    d=key.event(true,false); CHECK(d.consume && !d.request); // mode/focus/Shift lost
    d=key.event(false,false); CHECK(d.consume && !d.request);
    CHECK(!key.event(false,false).consume);
    d=key.event(true,false); CHECK(!d.consume && !d.request); // normal F
    d=key.event(true,true); CHECK(!d.consume && !d.request); // Shift pressed after native F
    CHECK(!key.event(false,true).consume);
    key.initialize(true); CHECK(!key.event(true,true).consume); CHECK(!key.event(false,true).consume);
}
}
int main() {
    keyTests();
    base=(std::uintptr_t)VirtualAlloc(nullptr,0x2b00000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(base); signatures(); CHECK(selectionReader.initialize(memory,base));
    const auto player=base+0x2000; enemy=base+0x3000; const auto root=base+0x4000;
    write<unsigned>(base+0x2a238f0,1); write<std::uintptr_t>(base+0x2a23910+8,player);
    write<unsigned>(player+8,1); write<unsigned>(player+12,1);
    write<unsigned>(enemy,1); write<unsigned>(enemy+4,19); write<unsigned>(enemy+8,7);
    write<unsigned>(enemy+12,1); write<unsigned>(enemy+0x124,0x0f);
    write<std::uintptr_t>(enemy+0x88,root); write<int>(root+0x1c,-1);
    write<std::uintptr_t>(base+0x2a23910+0x400+7*8,enemy);
    write<unsigned>(base+0x2a41630,1); write<unsigned>(base+0x2a41634,1); write<unsigned>(base+0x2a41638,7);
    write<std::uintptr_t>(enemy+0x38,base+0x5000);
    write<unsigned>(base+0x5000,6*32768); write<unsigned>(base+0x5004,4*32768);
    nearestFinder=nearest; eliteFinder=nearest; centerFinder=nearest;
    originalPickUpdate=picker; originalRequest=nativeRequest; originalGetter=nativeGetter;
    originalLeftDown=mouseDown; originalUiLeftUp=mouseUp;
    originalClientFree=freeUnit; originalCommonFree=freeUnit;
    ready=true; enabled=true;
    Publish(false,false); CHECK(!QueueSelection()); PickUpdate(0); CHECK(!state.target().valid());
    Publish(true,true); PickUpdate(0); CHECK(!state.target().valid()); // Shift alone never selects
    Publish(true,false); // F now works without Shift or cursor hover
    CHECK(QueueSelection()); CHECK(requests==0 && !state.target().valid()); // LL only queues intent
    PickUpdate(1); CHECK(intentCount!=0); // unrelated local slot cannot consume
    PickUpdate(0); CHECK(requests==0 && state.target().valid() && !intentCount);
    RingPoint point; CHECK(CopyRingPoint(point) && point.x==6 && point.z==4);
    const auto first=state.target(); const auto sampled=samples;
    PickUpdate(0); CHECK(state.target()==first && samples==sampled); // no automatic search/reacquisition
    Publish(true,false); PickUpdate(0); CHECK(state.target()==first); // Shift release retains target
    CHECK(!state.leftHeld()); // No mouse selection/pair ownership
    manualScope=true; CHECK(GetterForCaller(0,0xfe4e8)==enemy);
    CHECK(GetterForCaller(0,0xff075)==123); CHECK(GetterForCaller(1,0xfe4e8)==123);
    manualScope=false; CHECK(GetterForCaller(0,0xfe4e8)==123);
    CHECK(GetterForCaller(0,0x880a9f)==enemy); CHECK(GetterForCaller(0,0x784f98)==enemy);
    CHECK(manualCaller(0x840ae,0) && manualCaller(0x83bf7,5)); CHECK(!manualCaller(0x840ae,1));
    Publish(true,true);
    for(int action=0;action<6;++action) CHECK(Request(player,action,0,0,0)==42);
    CHECK(requests==6 && state.target()==first); // mouse actions NEVER select/suppress, including Shift
    ClientFree(enemy); CHECK(frees==1 && !state.target().valid());
    PickUpdate(0); CHECK(!state.target().valid()); // destruction can't silently rebind recycled ID
    choose(); CHECK(!(state.target()==first));
    write<unsigned>(enemy+12,12); PickUpdate(0); CHECK(!state.target().valid());
    CHECK(QueueSelection()); PickUpdate(0); CHECK(!state.target().valid()); // corpse rejected
    write<unsigned>(enemy+12,1);
    CHECK(QueueSelection()); Publish(true,false); PickUpdate(0); CHECK(state.target().valid()); clear();
    Publish(true,true); CHECK(QueueSelection()); insidePass=1; PickUpdate(0); CHECK(state.target().valid()); clear(); // Shift changes do not cancel F
    Publish(true,true); CHECK(QueueSelection()); insidePass=3; PickUpdate(0); CHECK(!state.target().valid());
    Publish(true,true); CHECK(QueueSelection()); intents[0].at=GetTickCount64()-1000; PickUpdate(0); CHECK(!state.target().valid());
    CHECK(QueueSelection()); Invalidate(); PickUpdate(0); CHECK(!state.target().valid());
    insidePass=2; PickUpdate(0); CHECK(!state.target().valid() && intentCount==1); // arrives during pick -> next pass
    PickUpdate(0); CHECK(state.target().valid()); clear();
    CHECK(QueueSelection()); const auto sampledBefore=samples;
    insidePass=2; PickUpdate(0); CHECK(state.target().valid() && intentCount==1 && samples==sampledBefore+1);
    PickUpdate(0); CHECK(state.target().valid() && !intentCount && samples==sampledBefore+2);
    // X rejects unsupported layout without stealing state; no matches keep identity.
    const auto beforeElite=state.target();
    CHECK(!QueueSelection(1,true));
    eliteReady=true; found=false;
    CHECK(QueueSelection(1,true));PickUpdate(0);CHECK(state.target()==beforeElite);
    found=true;complete=false;
    CHECK(QueueSelection(1,true));PickUpdate(0);CHECK(state.target()==beforeElite);complete=true;
    const auto mondata=base+0x7000,table=base+0x8000,rows=base+0x9000;
    write<std::uintptr_t>(enemy+0x10,mondata);
    write<std::uintptr_t>(base+0x2a9a580,table);
    write<std::uintptr_t>(table+0xf58,rows);write<std::uint64_t>(table+0xf60,100);
    CHECK(ReadElite(memory,base,enemy)==EliteKind::Normal);
    write<std::uint16_t>(mondata+0x1a,0x10);CHECK(ReadElite(memory,base,enemy)==EliteKind::Normal); // minion alone excluded
    CHECK(QueueSelection(1,true));PickUpdate(0);CHECK(state.target()==beforeElite); // fresh filter rejects ordinary result
    write<std::uint16_t>(mondata+0x1a,4);CHECK(ReadElite(memory,base,enemy)==EliteKind::Champion);
    CHECK(QueueSelection(1,true));PickUpdate(0);CHECK(state.target().valid() && lastWasElite && !(state.target()==beforeElite));
    write<std::uint16_t>(mondata+0x1a,8);CHECK(ReadElite(memory,base,enemy)==EliteKind::Unique);
    write<std::uint16_t>(mondata+0x1a,2);CHECK(ReadElite(memory,base,enemy)==EliteKind::SuperUnique);
    write<std::uint16_t>(mondata+0x1a,0);
    write<unsigned>(rows+19*0x1fc+0x3c,0x40);CHECK(ReadElite(memory,base,enemy)==EliteKind::Boss);
    write<unsigned>(rows+19*0x1fc+0x3c,0x10);CHECK(ReadElite(memory,base,enemy)==EliteKind::Normal); // setboss != boss
    write<unsigned char>(enemy+0x1bd,4);CHECK(ReadElite(memory,base,enemy)==EliteKind::Unknown);
    write<unsigned char>(enemy+0x1bd,0);write<std::uint64_t>(table+0xf60,19);CHECK(ReadElite(memory,base,enemy)==EliteKind::Unknown);
    write<std::uint64_t>(table+0xf60,100);
    write<unsigned>(enemy+12,12);CHECK(ReadElite(memory,base,enemy)==EliteKind::Unknown);write<unsigned>(enemy+12,1);
    const auto rapid=samples;
    CHECK(QueueSelection(1));CHECK(QueueSelection(-1));CHECK(QueueSelection(-1));
    PickUpdate(0);CHECK(samples==rapid+3 && lastDirection==-1 && !lastWasElite); // no coalescing physical presses
    write<unsigned char>(base+0x2a41630,0); CHECK(QueueSelection()); PickUpdate(0); CHECK(state.target().valid()); // hover irrelevant
    const auto beforeCenter=state.target(); const auto requestsBeforeCenter=requests;
    found=false; CHECK(QueueSelection(1,false,true));PickUpdate(0);CHECK(state.target()==beforeCenter);
    found=true;complete=false;CHECK(QueueSelection(1,false,true));PickUpdate(0);CHECK(state.target()==beforeCenter);
    complete=true;CHECK(QueueSelection(1,false,true));PickUpdate(0);CHECK(state.target().valid() && requests==requestsBeforeCenter);
    found=false; CHECK(QueueSelection()); PickUpdate(0); CHECK(!state.target().valid() && !CopyRingPoint(point));
    found=true; choose(); complete=false; CHECK(QueueSelection()); PickUpdate(0); CHECK(!state.target().valid()); complete=true;
    write<unsigned char>(base+0x2a41630,1); choose();
    enabled=false; Invalidate(); CHECK(!QueueSelection()); PickUpdate(0); CHECK(!state.target().valid());
    enabled=true; choose();
    // Client HP array: exact fixed8 ratio, missing/invalid data hides rather than inventing HP.
    const auto stats=base+0x6000;
    write<std::uintptr_t>(root+0xa8,stats);write<std::uint64_t>(root+0xb0,2);
    write<std::uint64_t>(stats,6ULL<<32);write<int>(stats+8,50*256);
    write<std::uint64_t>(stats+16,7ULL<<32);write<int>(stats+24,100*256);
    CHECK(ReadHealth(memory,enemy).valid && ReadHealth(memory,enemy).fraction==0.5f);
    fresh(context(),false);CHECK(CopyRingPoint(point)); // seal snapshot, no custom HP
    write<int>(stats+8,0);CHECK(ReadHealth(memory,enemy).valid && ReadHealth(memory,enemy).fraction==0);
    write<int>(stats+24,0);CHECK(!ReadHealth(memory,enemy).valid);
    write<int>(stats+24,100*256);write<int>(stats+8,-1);CHECK(!ReadHealth(memory,enemy).valid);
    write<int>(stats+8,50*256);write<std::uint64_t>(stats+16,6ULL<<32);CHECK(!ReadHealth(memory,enemy).valid);
    write<std::uint64_t>(stats+16,7ULL<<32);write<std::uint64_t>(root+0xb0,513);CHECK(!ReadHealth(memory,enemy).valid);
    write<std::uint64_t>(root+0xb0,2);
    // Shift-click owns down/hold/up without a native world attack, even after Shift release.
    Publish(true,true); PhysicalLeftDown(); const auto beforeClick=state.target();
    CHECK(LeftDown(0) && downs==0 && selectionLeftHeld && !(state.target()==beforeClick));
    CHECK(LeftDown(0) && downs==0); // duplicate world down
    Publish(true,false);
    for(int action=0;action<3;++action) CHECK(Request(player,action,0,0,0)==0);
    CHECK(UiLeftUp(0) && ups==1 && !selectionLeftHeld && requests==6);
    const auto clicked=state.target(); CHECK(clicked.valid());
    // Nonselection ground/UI path must not clear the pinned target.
    Publish(true,true);write<unsigned char>(base+0x2a41630,0);PhysicalLeftDown();
    CHECK(!LeftDown(0) && downs==1 && state.target()==clicked);CHECK(!UiLeftUp(0));
    // Dead monster pick is consumed; no native corpse attack.
    write<unsigned char>(base+0x2a41630,1);write<unsigned>(enemy+12,12);PhysicalLeftDown();
    CHECK(LeftDown(0) && downs==1);CHECK(UiLeftUp(0));write<unsigned>(enemy+12,1);
    // Shift released between OS down and world dispatch keeps the selection-only latch.
    Publish(true,true);PhysicalLeftDown();Publish(true,false);CHECK(LeftDown(0));CHECK(UiLeftUp(0));
    // Mode loss before queued dispatch consumes rather than converting into attack.
    Publish(true,true);PhysicalLeftDown();Publish(false,false);CHECK(LeftDown(0));CHECK(UiLeftUp(0));
    // Missing native release: a new physical down repairs ownership.
    Publish(true,true);PhysicalLeftDown();CHECK(LeftDown(0));
    Publish(true,false);PhysicalLeftDown();CHECK(!LeftDown(0) && downs==2);CHECK(!UiLeftUp(0));
    choose();
    const auto thread=CreateThread(nullptr,0,[](void*) -> DWORD { CommonFree(enemy); return 0; },nullptr,0,nullptr);
    CHECK(thread); WaitForSingleObject(thread,INFINITE); CloseHandle(thread);
    CHECK(!enabled.load() && !state.target().valid() && failures.load()==1);
    CHECK(requests==6); // all key selection paths caused ZERO native actions
    ready=false; VirtualFree((void*)base,0,MEM_RELEASE);
    std::puts("F/G FIFO, Shift-click pair and HP runtime mocks PASS; not native gameplay proof");
}
