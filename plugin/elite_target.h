#pragma once
#include <cstdint>
#include <cstring>

namespace click_target {
// Verified on live 3.2.92777: native flag accessor38e870 + champion/unique
// wrappers1a5c10/1a5cd0, monstats getter976e0 + boss schema39e0d3/39e247.
// No native calls or writes. Layout failure disables X only.
template<class Reader> bool EliteLayoutSupported(Reader& read,std::uintptr_t base) {
    struct Guard { std::uintptr_t rva; const char* bytes; unsigned size; };
    const Guard guards[]={
        {0x38e8b0,"\x83\xf8\x01\x75\x1c\x48\x8b\x43\x10\x48\x85\xc0\x74\x13\x66\x85\x78\x1a",18},
        {0x1a5c28,"\x8d\x50\x03\x48\x8b\xcb\xe8\x3d\x8c\x1e\x00",11},
        {0x1a5c68,"\x8d\x50\x01\x48\x8b\xcb\xe8\xfd\x8b\x1e\x00",11},
        {0x1a5ce8,"\x8d\x50\x07\x48\x8b\xcb\xe8\x7d\x8b\x1e\x00",11},
        {0x34a103,"\x0f\xb6\x81\xbd\x01\x00\x00",7},
        {0x300ac0,"\x48\x8d\x0d\xb9\x9a\x79\x02\x48\x03\xc0\x48\x8b\x04\xc1",14},
        {0x97722,"\x48\x3b\x9f\x60\x0f\x00\x00\x73\x4c\x48\x81\xc7\x58\x0f\x00\x00",16},
        {0x9775b,"\x48\x69\x44\x24\x50\xfc\x01\x00\x00\x48\x03\x07",12},
        {0x39e0d3,"\x48\x8d\x05\x7a\x83\x96\x01\x48\x89\x85\x60\x04\x00\x00",14},
        {0x39e247,"\xc7\x85\x6c\x04\x00\x00\x06\x00\x00\x00\x48\xc7\x85\x70\x04\x00\x00\x3c\x00\x00\x00",21},
        {0x1d06454,"boss",5},
    };
    for(const auto& g:guards) {
        unsigned char actual[32]{};
        if(!read(base+g.rva,actual,g.size) || std::memcmp(actual,g.bytes,g.size)) return false;
    }
    return true;
}
enum class EliteKind { Unknown, Normal, Champion, Unique, SuperUnique, Boss };
inline bool IsElite(EliteKind k) { return k!=EliteKind::Unknown && k!=EliteKind::Normal; }
template<class Reader> EliteKind ReadElite(Reader& read,std::uintptr_t base,std::uintptr_t unit) {
    const auto get=[&](std::uintptr_t at,auto& v) { return at>=0x10000 && read(at,&v,sizeof v); };
    unsigned header[4]{},end[4]{}; std::uintptr_t data=0,dataEnd=0;
    std::uint16_t flags=0,flagsEnd=0;
    if(!unit || !read(unit,header,sizeof header) || header[0]!=1 || header[3]==0 || header[3]==12 ||
       !get(unit+0x10,data) || !data || !get(data+0x1a,flags)) return EliteKind::Unknown;
    EliteKind out=EliteKind::Normal;
    if(flags&2) out=EliteKind::SuperUnique;
    else if(flags&4) out=EliteKind::Champion;
    else if(flags&8) out=EliteKind::Unique;
    else {
        // Do NOT assume mondata[0] is MonStats in this loader. Use the native
        // expansion-indexed table, bounded class lookup, without calling asserts.
        unsigned char expansion=0;
        std::uintptr_t root=0,rows=0; std::uint64_t count=0; std::uint32_t monFlags=0;
        if(!get(unit+0x1bd,expansion) || expansion>=4 ||
           !get(base+0x2a9a580+16*expansion,root) || !root ||
           !get(root+0xf58,rows) || !rows || !get(root+0xf60,count) || count>65536 || header[1]>=count ||
           !get(rows+std::uintptr_t(header[1])*0x1fc+0x3c,monFlags)) return EliteKind::Unknown;
        if(monFlags&0x40) out=EliteKind::Boss;
    }
    if(!read(unit,end,sizeof end) || std::memcmp(header,end,sizeof end) ||
       !get(unit+0x10,dataEnd) || dataEnd!=data || !get(data+0x1a,flagsEnd) || flagsEnd!=flags) return EliteKind::Unknown;
    return out; // caller still checks hostile/pets/range/LOS + fresh lifetime
}
} // namespace click_target
