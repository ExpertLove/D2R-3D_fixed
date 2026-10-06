#pragma once
#include <windows.h>
#include <cstring>
#include "target_provider.h"

namespace third_person {
struct ClientMemoryReader {
    bool operator()(uintptr_t address, void* out, size_t size) const noexcept {
        if (address<0x10000 || size>65536 || address>UINTPTR_MAX-size) return false;
        __try { memcpy(out,reinterpret_cast<const void*>(address),size); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
};
// Validate the active loader's alignment adapter, following its ASLR addresses.
// Do not parse stock 8-byte stats if this 16-byte layout cannot be established.
inline bool LoaderAlignmentLayoutSupported(uintptr_t base) {
    ClientMemoryReader read;
    const auto match=[&](uintptr_t p,std::initializer_list<unsigned char> expected) {
        unsigned char bytes[32]{};
        return expected.size()<=sizeof bytes && read(p,bytes,expected.size()) &&
            !memcmp(bytes,expected.begin(),expected.size());
    };
    const uintptr_t entry=base+0x2f4190;
    int32_t displacement=0; uintptr_t wrapper=0;
    if (!match(entry,{0xff,0x25}) || !read(entry+2,&displacement,4) ||
        !read(entry+6+displacement,&wrapper,8) ||
        !match(wrapper,{0x48,0x83,0xec,0x28,0xe8}) ||
        !match(wrapper+9,{0x89,0xc0,0x48,0x83,0xc4,0x28,0xc3}) ||
        !read(wrapper+5,&displacement,4)) return false;
    const uintptr_t impl=wrapper+9+displacement;
    return match(impl+0x10,{0x48,0x8b,0x91,0x88,0,0,0}) &&
        match(impl+0x26,{0x83,0x7a,0x1c,0,0x78,0x0e}) &&
        match(impl+0x3d,{0xba,0x69,0,0,0}) &&
        match(impl+0x74,{0xb8,0xa8,0,0,0,0x41,0xb9,0x30,0,0,0}) &&
        match(impl+0x83,{0x49,0xb8,0,0,0,0,0xac,0,0,0}) &&
        match(impl+0xbf,{0x49,0xc1,0xe7,0x04}) &&
        match(impl+0xf8,{0x41,0x8b,0x41,0x08});
}
inline bool TargetLayoutSupported(uintptr_t base) {
    struct Signature { uintptr_t rva; unsigned size; unsigned char bytes[8]; };
    const Signature signatures[]={
        {0x8b2d0,7,{0x8b,0x05,0x2e,0x84,0x99,0x02,0xc3}},
        {0x2efd90,8,{0x48,0x8b,0x81,0xa8,0,0,0,0xc3}},
        {0x341c30,5,{0x48,0x8b,0x41,0x20,0xc3}},
        {0x34b4ce,7,{0x48,0x8b,0x81,0x60,0x01,0,0}},
        {0x9a4f4,7,{0x48,0x8d,0x0d,0x15,0x94,0x98,0x02}},
        {0x341c10,4,{0x8b,0x41,0x08,0xc3}},
        {0x341c20,4,{0x8b,0x41,0x0c,0xc3}},
        {0x13b270,7,{0x48,0x8b,0x05,0x99,0x29,0x91,0x02}},
        {0x34b89e,7,{0x48,0x8b,0x81,0x88,0,0,0}},
        {0x2f59bb,7,{0x48,0x8b,0x87,0x90,0,0,0}},
        {0x2f59cc,4,{0x48,0x8b,0x40,0x68}},
        {0x2f59d5,7,{0x48,0x8b,0x87,0x98,0,0,0}},
        {0x2efb30,5,{0x48,0x8b,0x41,0x38,0xc3}},
        {0x363153,4,{0x48,0x8b,0x57,0x20}},
        {0x36315c,7,{0x2b,0x77,0x04,0x0f,0xaf,0x77,0x08}},
        {0x36316e,5,{0x44,0x0f,0xb7,0x34,0x4a}},
        {0x365d6f,6,{0x8b,0x46,0x04,0x03,0x46,0x0c}},
    };
    ClientMemoryReader read;
    for (const auto& sig:signatures) {
        unsigned char bytes[8]{};
        if (!read(base+sig.rva,bytes,sig.size) || memcmp(bytes,sig.bytes,sig.size)) return false;
    }
    return LoaderAlignmentLayoutSupported(base);
}

// Projection uses the observed fixed16.16 -> render X/Z hypothesis. Height and
// native LOS remain unverified: this is diagnostics, not permission to attack.
inline TargetCandidate ProjectTarget(const ObservedTarget& unit, const TargetFrame& frame,
                                     const float* view, const float* projection, float groundY) {
    const float world[4]={unit.x,groundY+4.0f,unit.z,1};
    float camera[4]{},clip[4]{};
    for (int j=0;j<4;++j) for (int i=0;i<4;++i) camera[j]+=world[i]*view[i*4+j];
    for (int j=0;j<4;++j) for (int i=0;i<4;++i) clip[j]+=camera[i]*projection[i*4+j];
    TargetCandidate candidate{};
    candidate.key=unit.key;
    candidate.distance=std::hypot(unit.x-frame.playerX,unit.z-frame.playerZ);
    candidate.alive=candidate.targetable=true; // provider's basic prefilter only
    candidate.hostile=unit.relation==Relation::Hostile;
    candidate.inFront=std::isfinite(clip[3]) && clip[3]>0.0001f;
    if (candidate.inFront) {
        candidate.screenX=clip[0]/clip[3]; candidate.screenY=clip[1]/clip[3];
    }
    candidate.visible=unit.sight==Sight::Clear; // terrain LOS, not raster visibility
    return candidate;
}
} // namespace third_person
