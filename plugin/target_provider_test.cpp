#include "target_runtime.h"
#include <map>
#include <cstdio>
#include <cstring>

struct Memory {
    std::map<uintptr_t,unsigned char> bytes;
    template<class T> void put(uintptr_t p,T value) {
        const auto* b=reinterpret_cast<const unsigned char*>(&value);
        for (size_t i=0;i<sizeof value;++i) bytes[p+i]=b[i];
    }
    bool operator()(uintptr_t p,void* out,size_t size) {
        auto* b=static_cast<unsigned char*>(out);
        for (size_t i=0;i<size;++i) {
            const auto it=bytes.find(p+i);
            if (it==bytes.end()) return false;
            b[i]=it->second;
        }
        return true;
    }
};
constexpr uintptr_t base=0x140000000, player=0x100000, path=0x110000,
                    room=0x120000, monster=0x130000, mpath=0x140000, pet=0x150000;
Memory fixture() {
    Memory m;
    m.put<uint32_t>(base+0x2a23704,0); m.put<uint32_t>(base+0x2a238f0,1);
    m.put<uintptr_t>(base+0x2a23910+8,player);
    m.put<uint32_t>(player,0); m.put<uint32_t>(player+8,1); m.put<uint32_t>(player+12,1);
    m.put<uintptr_t>(player+0x38,path); m.put<uintptr_t>(path+0x20,room);
    m.put<uint32_t>(path,100*32768); m.put<uint32_t>(path+4,100*32768);
    m.put<uintptr_t>(base+0x2a4dc10,0);
    m.put<uintptr_t>(room,0); m.put<uint32_t>(room+0x40,0);
    m.put<uintptr_t>(room+0xa8,monster);
    m.put<uint32_t>(monster,1); m.put<uint32_t>(monster+4,19);
    m.put<uint32_t>(monster+8,16); m.put<uint32_t>(monster+12,1);
    m.put<uint32_t>(monster+0x124,0x3000000f);
    m.put<uintptr_t>(monster+0x160,0); m.put<uintptr_t>(monster+0x38,mpath);
    m.put<uint32_t>(mpath,102*32768); m.put<uint32_t>(mpath+4,103*32768);
    return m;
}
int main() {
    using namespace third_person;
    int failures=0;
    const auto check=[&](bool value,const char* name) {
        if (!value) { fprintf(stderr,"FAIL: %s\n",name); ++failures; }
    };
    TargetProvider provider; TargetFrame frame;
    Memory m=fixture();
    const auto sample=[&](uint64_t session=1) { return provider.sample(m,base,session,1,100,frame); };
    check(sample() && frame.complete && frame.count==1,"complete bounded snapshot");
    const auto first=frame.units[0].key;
    check(frame.units[0].x==102 && frame.playerZ==100,"fixed coordinate hypothesis");
    check(frame.units[0].relation==Relation::Unknown,"enemy class and flags do not prove hostility");
    check(sample() && frame.units[0].key==first,"continuous observation keeps generation");
    m.put<uint32_t>(monster+12,12);
    check(sample() && frame.count==0 && frame.rejected==1,"corpse excluded despite attackable flag");
    m.put<uint32_t>(monster+12,0);
    check(sample() && frame.count==0,"dying excluded");
    m.put<uint32_t>(monster+12,1);
    check(sample() && !(frame.units[0].key==first),"disappearance invalidates observation generation");
    m.put<uint32_t>(monster+0x124,0x0b);
    check(sample() && frame.count==0,"town NPC target flags excluded");
    m.put<uint32_t>(monster+0x124,0x0f);
    m.put<uintptr_t>(base+0x2a4dc10,pet); m.put<uint32_t>(pet+8,16);
    m.put<uintptr_t>(pet+0x30,0);
    check(sample() && frame.pets==1 && frame.units[0].relation==Relation::OwnedPet,"all listed pets excluded, not just own wolf");
    m.put<uintptr_t>(pet+0x30,pet);
    check(!sample() && !frame.complete && !frame.count,"pet cycle fails closed, no partial snapshot");
    m=fixture(); m.put<uintptr_t>(monster+0x160,monster);
    check(!sample(),"room cycle fails closed");
    m=fixture(); m.put<uint32_t>(room+0x40,65);
    check(!sample(),"oversized neighbor array rejected");
    m=fixture(); m.put<uint32_t>(player+8,9); m.put<uintptr_t>(player+0x158,player);
    check(!sample(),"hash cycle fails closed");
    m=fixture();
    for (uint32_t i=0;i<129;++i) {
        const uintptr_t p=monster+i*0x200;
        m.put<uint32_t>(p,1); m.put<uint32_t>(p+4,19); m.put<uint32_t>(p+8,16+i);
        m.put<uint32_t>(p+12,1); m.put<uint32_t>(p+0x124,15);
        m.put<uintptr_t>(p+0x38,mpath);
        m.put<uintptr_t>(p+0x160,i<128 ? p+0x200 : 0);
    }
    check(!sample() && !frame.count,"candidate overflow discards whole frame");
    m=fixture(); check(sample(),"baseline for path replacement");
    const auto beforePath=frame.units[0].key;
    m.put<uintptr_t>(monster+0x38,mpath+0x100);
    m.put<uint32_t>(mpath+0x100,102*32768); m.put<uint32_t>(mpath+0x104,103*32768);
    check(sample() && !(frame.units[0].key==beforePath),"same ID with replaced path gets new observation generation");
    m=fixture(); m.bytes.erase(base+0x2a4dc10);
    check(!sample(),"unreadable pet list is not assumed empty");
    m=fixture(); m.put<uint32_t>(base+0x2a238f0,2);
    check(!sample(),"lifecycle player identity mismatch rejected");
    m=fixture(); m.put<uint32_t>(player+12,17);
    check(!sample(),"dead player fails closed");
    m=fixture(); check(sample(2) && frame.units[0].key.session==2,"session stamped on copied key");
    check(!sample(0),"no session no read");
    m=fixture(); check(sample(),"recover after invalid sample");
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    auto candidate=ProjectTarget(frame.units[0],frame,identity,identity,0);
    check(!candidate.hostile && !candidate.visible,"projection never invents hostility or LOS");
    TargetSelector selector;
    const auto result=selector.update(&candidate,1,1,100,true,true);
    check(!result.selected.valid() && !result.locked && !result.attackAllowed,"real provider unknown rejected on MMB");
    // End-to-end positive path: good player, unowned evil monster, clear terrain.
    m=fixture(); provider.reset(); selector.reset();
    constexpr uintptr_t pr=0x500000,ps=0x510000,pe=0x520000,mr=0x600000,cg=0x700000,cells=0x710000;
    m.put<uintptr_t>(player+0x88,pr); m.put<int32_t>(pr+0x1c,int32_t(0x80000000));
    m.put<uintptr_t>(pr+0x90,ps); m.put<uintptr_t>(pr+0x98,0);
    m.put<uint32_t>(ps+0x20,0x69); m.put<int32_t>(ps+0x1c,0);
    m.put<uintptr_t>(ps+0x30,pe); m.put<uint64_t>(ps+0x38,1);
    m.put<uint64_t>(pe,0xac00000000ULL); m.put<int32_t>(pe+8,2);
    m.put<uintptr_t>(monster+0x88,mr); m.put<int32_t>(mr+0x1c,int32_t(0x80000000));
    m.put<uintptr_t>(mr+0x90,0); m.put<uintptr_t>(mr+0x98,0);
    m.put<uintptr_t>(room+0x38,cg);
    m.put<int32_t>(cg,40); m.put<int32_t>(cg+4,40); m.put<int32_t>(cg+8,20); m.put<int32_t>(cg+12,20);
    m.put<uintptr_t>(cg+0x20,cells);
    for (int i=0;i<400;++i) m.put<uint16_t>(cells+2*i,0);
    check(sample() && frame.units[0].relation==Relation::Hostile && frame.units[0].sight==Sight::Clear,"live native alignment and grid reach provider");
    float view[16]={1,0,0,0,0,1,0,0,0,0,1,0,-102,-4,-113,1};
    float projection[16]={1,0,0,0,0,1,0,0,0,0,0,-1,0,0,1,0};
    candidate=ProjectTarget(frame.units[0],frame,view,projection,0);
    auto selected=selector.update(&candidate,1,1,101,true,true);
    check(selected.selected.id==16 && selected.locked,"actual provider projection acquires and locks");
    m.put<uint16_t>(cells+2*(11*20+10),0x800);
    check(sample() && frame.units[0].sight==Sight::Blocked,"door occludes provider candidate");
    candidate=ProjectTarget(frame.units[0],frame,view,projection,0);
    selected=selector.update(&candidate,1,1,102,false,true);
    check(selected.locked && !selected.attackAllowed,"brief terrain occlusion freezes tracking without attack permission");
    check(!selector.update(&candidate,1,1,402,false,true).locked,"terrain occlusion grace expires");
    m.put<uintptr_t>(mr+0x90,ps); // same alignment state represents conversion to Good
    check(sample() && frame.units[0].relation==Relation::Friendly,"converted monster becomes friendly without flag changes");
    m.put<uintptr_t>(mr+0x90,0); m.put<uint32_t>(monster+0x124,0x40f);
    check(sample() && frame.units[0].relation==Relation::Unknown,"ownership redirect flag rejected");
    fprintf(stdout,"target provider: %d failures\n",failures);
    return failures ? 1 : 0;
}
