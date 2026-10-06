#include "target_world.h"
#include "target_tracking.h"
#include <map>
#include <cstdio>
#include <limits>

struct Memory {
    std::map<uintptr_t,unsigned char> bytes;
    template<class T> void put(uintptr_t p,T value) {
        const auto* b=reinterpret_cast<unsigned char*>(&value);
        for (size_t i=0;i<sizeof value;++i) bytes[p+i]=b[i];
    }
    bool operator()(uintptr_t p,void* out,size_t size) {
        auto* b=static_cast<unsigned char*>(out);
        for (size_t i=0;i<size;++i) {
            auto it=bytes.find(p+i); if (it==bytes.end()) return false;
            b[i]=it->second;
        }
        return true;
    }
};
int main() {
    using namespace third_person;
    int failures=0;
    const auto check=[&](bool ok,const char* label) {
        if (!ok) { fprintf(stderr,"FAIL: %s\n",label); ++failures; }
    };
    Memory m;
    constexpr uintptr_t unit=0x10000,root=0x20000,state=0x30000,entries=0x40000;
    check(ReadAlignment(m,unit)==Alignment::Unknown,"missing root is not evil");
    m.put<uintptr_t>(unit+0x88,root); m.put<int32_t>(root+0x1c,int32_t(0x80000000));
    m.put<uintptr_t>(root+0x90,0); m.put<uintptr_t>(root+0x98,0);
    check(ReadAlignment(m,unit)==Alignment::Evil,"valid root without alignment state uses native default");
    m.put<uintptr_t>(root+0x90,state); m.put<uint32_t>(state+0x20,0x69);
    m.put<int32_t>(state+0x1c,0); m.put<uintptr_t>(state+0x30,entries); m.put<uint64_t>(state+0x38,1);
    m.put<uint64_t>(entries,0xac00000000ULL); m.put<int32_t>(entries+8,2);
    check(ReadAlignment(m,unit)==Alignment::Good,"loader 16-byte alignment key");
    m.put<int32_t>(entries+8,1);
    check(ReadAlignment(m,unit)==Alignment::Neutral,"neutral not hostile");
    m.put<int32_t>(entries+8,3);
    check(ReadAlignment(m,unit)==Alignment::Unknown,"invalid alignment not hostile");
    m.put<int32_t>(entries+8,0);
    check(ReadAlignment(m,unit)==Alignment::Evil,"explicit evil alignment");
    m.put<uint64_t>(state+0x38,513);
    check(ReadAlignment(m,unit)==Alignment::Unknown,"stat array bound");
    m.put<uint64_t>(state+0x38,2); m.put<uint64_t>(entries+16,0xac00000000ULL); m.put<int32_t>(entries+24,0);
    check(ReadAlignment(m,unit)==Alignment::Unknown,"duplicate stat keys rejected");
    m.put<uint64_t>(state+0x38,1);
    m.put<int32_t>(state+0x1c,int32_t(0x80000000)); m.put<uintptr_t>(state+0xa8,entries); m.put<uint64_t>(state+0xb0,1);
    m.put<int32_t>(entries+8,2);
    check(ReadAlignment(m,unit)==Alignment::Good,"extended state uses total stats");
    m.put<uintptr_t>(root+0x90,0); m.put<uintptr_t>(root+0x98,state);
    check(ReadAlignment(m,unit)==Alignment::Good,"second native state list searched");
    m.put<uint32_t>(state+0x20,0x68); m.put<uintptr_t>(state+0x68,state);
    check(ReadAlignment(m,unit)==Alignment::Unknown,"state cycle rejects");

    int wallX=-1,wallZ=-1,missingX=-1,calls=0;
    uint16_t bits=1;
    const auto cell=[&](int x,int z,uint16_t& out) {
        ++calls; if (x==missingX) return false;
        out=(x==wallX && z==wallZ)?bits:0; return true;
    };
    check(TraceGroundCells(0,0,4,0,cell)==Sight::Clear,"horizontal open ray");
    wallX=2; wallZ=0;
    check(TraceGroundCells(0,0,4,0,cell)==Sight::Blocked,"wall blocks ray");
    bits=0x800;
    check(TraceGroundCells(4,0,0,0,cell)==Sight::Blocked,"door blocks reverse ray");
    bits=0x100;
    check(TraceGroundCells(0,0,4,0,cell)==Sight::Clear,"monster footprint does not hide itself");
    bits=1; wallX=1; wallZ=0;
    check(TraceGroundCells(0,0,2,2,cell)==Sight::Blocked,"supercover corner cannot leak through walls");
    wallX=-1; missingX=2;
    check(TraceGroundCells(0,0,4,0,cell)==Sight::Unknown,"unloaded cell unknown not visible");
    calls=0;
    check(TraceGroundCells(0,0,129,0,cell)==Sight::Unknown && calls==0,"ray budget before reads");
    missingX=-1;
    check(TraceGroundCells(0,0,0,0,cell)==Sight::Clear,"zero length ray");

    constexpr uintptr_t room=0x50000,grid=0x60000,cells=0x70000;
    m.put<uintptr_t>(room+0x38,grid);
    m.put<int32_t>(grid,10); m.put<int32_t>(grid+4,20); m.put<int32_t>(grid+8,4); m.put<int32_t>(grid+12,3);
    m.put<uintptr_t>(grid+0x20,cells);
    for (int i=0;i<12;++i) m.put<uint16_t>(cells+i*2,0);
    CollisionGrid g;
    check(ReadCollisionGrid(m,room,g),"traced grid layout");
    check(GroundSight(m,&g,1,20,40,26,44)==Sight::Clear,"render coordinates divide by two for grid");
    m.put<uint16_t>(cells+2*(1*4+2),4);
    check(GroundSight(m,&g,1,20,42,26,42)==Sight::Blocked,"row-major word indexing");
    check(GroundSight(m,&g,1,20,42,28,42)!=Sight::Clear,"outside known grid fails closed");
    m.put<int32_t>(grid+8,513);
    check(!ReadCollisionGrid(m,room,g),"grid bound");
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    float target[3]={0,0,-10};
    auto step=CameraLockStep(identity,identity,target,0.01f);
    check(step.valid && step.yaw==0 && step.pitch==0,"center has no steering");
    target[0]=10; target[1]=10;
    step=CameraLockStep(identity,identity,target,0.01f);
    check(step.valid && step.yaw>0 && step.yaw<=0.901f && step.pitch<0 && step.pitch>=-0.601f,"orbit sign and speed limits");
    target[2]=10;
    step=CameraLockStep(identity,identity,target,0.01f);
    check(step.valid && step.pitch==0,"behind target no pitch flip");
    check(!CameraLockStep(identity,identity,target,1).valid,"long frame cannot snap camera");
    const float pivot[3]={0,0,-20};
    target[0]=0; target[1]=5; target[2]=-10;
    step=CameraLockStep(identity,identity,target,0.01f,pivot);
    check(step.valid && std::abs(step.yaw)>0 && step.pitch==0,"enemy between eye and player turns orbit, not pitch");
    wallX=0; wallZ=1;
    check(TraceGroundSegment(0.9,0.1,1.1,2.9,cell)==Sight::Blocked,"fractional endpoints cannot use wrong cell-center ray");
    target[0]=std::numeric_limits<float>::quiet_NaN();
    check(!CameraLockStep(identity,identity,target,0.01f).valid,"NaN rejected");
    fprintf(stdout,"target world/tracking: %d failures\n",failures);
    return failures?1:0;
}
