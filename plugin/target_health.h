#pragma once
#include <cstdint>
#include <algorithm>

namespace click_target {
struct Health { float fraction=0; bool valid=false; };
// Read-only client total-stat array, using the loader's guarded 16-byte layout.
// Stats 6/7 are HP/maxHP, both fixed8 so their ratio needs no unit conversion.
// No synthesized 100% bar if this client does not expose both stats.
template<class Reader> Health ReadHealth(Reader& read,std::uintptr_t unit) {
    const auto get=[&](std::uintptr_t p,auto& v) { return p>=0x10000 && read(p,&v,sizeof v); };
    std::uintptr_t root=0,entries=0; std::int32_t flags=0;
    std::uint64_t count=0;
    if(!get(unit+0x88,root) || !root || !get(root+0x1c,flags) || flags>=0 ||
       !get(root+0xa8,entries) || !get(root+0xb0,count) || !entries || !count || count>512) return {};
    bool haveHp=false,haveMax=false; std::int32_t hp=0,max=0;
    std::uint64_t previous=0;
    for(std::uint64_t i=0;i<count;++i) {
        std::uint64_t key=0; std::int32_t value=0;
        if(!get(entries+i*16,key) || !get(entries+i*16+8,value) || (i && key<=previous)) return {};
        previous=key;
        if(key==(6ULL<<32)) { hp=value; haveHp=true; }
        if(key==(7ULL<<32)) { max=value; haveMax=true; }
    }
    std::uintptr_t rootEnd=0,entriesEnd=0; std::uint64_t countEnd=0;
    if(!get(unit+0x88,rootEnd) || rootEnd!=root || !get(root+0xa8,entriesEnd) || entriesEnd!=entries ||
       !get(root+0xb0,countEnd) || countEnd!=count || !haveHp || !haveMax || hp<0 || max<=0) return {};
    return {std::min(1.0f,float(double(hp)/double(max))),true};
}
} // namespace click_target
