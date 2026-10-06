#pragma once
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cmath>

namespace third_person {
enum class Alignment : int { Unknown=-1, Evil=0, Neutral=1, Good=2 };

// Current loader's 16-byte stat entries, NOT the stock executable's 8-byte entries.
// Caller must validate the loader detour/layout before using this reader.
template<class Reader> Alignment ReadAlignment(Reader& read, uintptr_t unit) {
    const auto get=[&](uintptr_t p,auto& v) { return p>=0x10000 && read(p,&v,sizeof v); };
    uintptr_t root=0;
    int32_t flags=0;
    if (!get(unit+0x88,root) || !root || !get(root+0x1c,flags) || flags>=0) return Alignment::Unknown;
    uintptr_t state=0;
    // The native state lookup searches active, then inactive lists. Missing data
    // is unknown, unlike native's permissive zero fallback on invalid pointers.
    for (uintptr_t offset : {uintptr_t(0x90),uintptr_t(0x98)}) {
        if (!get(root+offset,state)) return Alignment::Unknown;
        std::array<uintptr_t,128> seen{};
        size_t count=0;
        while (state) {
            if (count==seen.size()) return Alignment::Unknown;
            for (size_t i=0;i<count;++i) if (seen[i]==state) return Alignment::Unknown;
            seen[count++]=state;
            uint32_t stateId=0;
            if (!get(state+0x20,stateId)) return Alignment::Unknown;
            if (stateId==0x69) break;
            if (!get(state+0x68,state)) return Alignment::Unknown;
        }
        if (state) break;
    }
    if (!state) return Alignment::Evil; // native default, only after complete valid lists
    if (!get(state+0x1c,flags)) return Alignment::Unknown;
    const uintptr_t offset=flags<0 ? 0xa8 : 0x30;
    uintptr_t entries=0;
    uint64_t count=0;
    if (!get(state+offset,entries) || !get(state+offset+8,count) || count>512 ||
        (count && !entries)) return Alignment::Unknown;
    // Scan the complete bounded array; reject malformed/unsorted/duplicate keys.
    uint64_t previous=0;
    int32_t alignment=0;
    for (uint64_t i=0;i<count;++i) {
        uint64_t key=0; int32_t value=0;
        if (!get(entries+i*16,key) || !get(entries+i*16+8,value) || (i && key<=previous))
            return Alignment::Unknown;
        previous=key;
        if (key==0xac00000000ULL) alignment=value;
    }
    // For alignment the loader's min clamp cannot alter 0..2 (table minimum=0).
    if (alignment<0 || alignment>2) return Alignment::Unknown;
    return static_cast<Alignment>(alignment);
}

struct CollisionGrid {
    int32_t x=0,z=0,width=0,height=0;
    uintptr_t cells=0; // confined to this sample's UI callback; never published
};
template<class Reader> bool ReadCollisionGrid(Reader& read,uintptr_t room,CollisionGrid& out) {
    uintptr_t grid=0;
    if (!read(room+0x38,&grid,sizeof grid) || grid<0x10000 ||
        !read(grid,&out.x,4) || !read(grid+4,&out.z,4) ||
        !read(grid+8,&out.width,4) || !read(grid+12,&out.height,4) ||
        !read(grid+0x20,&out.cells,sizeof out.cells)) return false;
    return out.x>=0 && out.z>=0 && out.x<=65535 && out.z<=65535 &&
        out.width>0 && out.width<=512 && out.height>0 && out.height<=512 && out.cells>=0x10000;
}

enum class Sight { Unknown, Blocked, Clear };
// Conservative supercover of a ground-plane segment through native collision
// cells. This is terrain LOS, NOT renderer occlusion or permission to attack.
// Missing rooms/read failures fail closed. Ignore unit footprints; block static
// walls, visual/missile barriers, invalid tiles, objects and doors (0x0c27).
template<class CellReader> Sight TraceGroundSegment(double startX,double startZ,double stopX,double stopZ,CellReader cell) {
    for (double v : {startX,startZ,stopX,stopZ})
        if (!std::isfinite(v) || std::abs(v)>1000000) return Sight::Unknown;
    int x=int(std::floor(startX)),z=int(std::floor(startZ));
    const int endX=int(std::floor(stopX)),endZ=int(std::floor(stopZ));
    if (std::abs(endX-x)>128 || std::abs(endZ-z)>128) return Sight::Unknown;
    const double dx=stopX-startX,dz=stopZ-startZ;
    const int sx=dx>0?1:-1,sz=dz>0?1:-1;
    const double infinity=1e100;
    const double deltaX=dx!=0 ? 1/std::abs(dx) : infinity;
    const double deltaZ=dz!=0 ? 1/std::abs(dz) : infinity;
    double nextX=dx>0 ? (x+1-startX)*deltaX : dx<0 ? (startX-x)*deltaX : infinity;
    double nextZ=dz>0 ? (z+1-startZ)*deltaZ : dz<0 ? (startZ-z)*deltaZ : infinity;
    const auto visit=[&](int cx,int cz) {
        uint16_t flags=0;
        if (!cell(cx,cz,flags)) return Sight::Unknown;
        return (flags&0x0c27) ? Sight::Blocked : Sight::Clear;
    };
    auto result=visit(x,z);
    if (result!=Sight::Clear) return result;
    unsigned steps=0;
    while (x!=endX || z!=endZ) {
        if (++steps>512) return Sight::Unknown;
        if (std::abs(nextX-nextZ)<1e-12) {
            // At a corner test BOTH adjacent cells; no diagonal wall leaks.
            result=visit(x+sx,z); if (result!=Sight::Clear) return result;
            result=visit(x,z+sz); if (result!=Sight::Clear) return result;
            x+=sx; z+=sz; nextX+=deltaX; nextZ+=deltaZ;
        } else if (nextX<nextZ) { x+=sx; nextX+=deltaX; }
        else { z+=sz; nextZ+=deltaZ; }
        result=visit(x,z); if (result!=Sight::Clear) return result;
    }
    return Sight::Clear;
}

template<class CellReader> Sight TraceGroundCells(int x,int z,int endX,int endZ,CellReader cell) {
    return TraceGroundSegment(x+0.5,z+0.5,endX+0.5,endZ+0.5,cell);
}

template<class Reader> Sight GroundSight(Reader& read,const CollisionGrid* grids,size_t count,
                                         float x,float z,float endX,float endZ) {
    if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(endX) || !std::isfinite(endZ) ||
        x<0 || z<0 || endX<0 || endZ<0 || x>131071 || z>131071 || endX>131071 || endZ>131071)
        return Sight::Unknown;
    const auto cell=[&](int cx,int cz,uint16_t& flags) {
        bool found=false; flags=0;
        for (size_t i=0;i<count;++i) {
            const auto& g=grids[i];
            const int64_t gx=int64_t(cx)-g.x,gz=int64_t(cz)-g.z;
            if (gx<0 || gz<0 || gx>=g.width || gz>=g.height) continue;
            uint16_t bits=0;
            if (!read(g.cells+2*(gz*g.width+gx),&bits,sizeof bits)) return false;
            flags|=bits; found=true;
        }
        return found;
    };
    return TraceGroundSegment(double(x)/2,double(z)/2,double(endX)/2,double(endZ)/2,cell);
}
} // namespace third_person
