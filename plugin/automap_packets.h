#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "automap_math.h"

namespace third_person::automap {
inline constexpr size_t MaxPackets=4096, FingerprintBytes=0x54;
// Verified f8c0a0 switch strides. 1/2=scissor, 7/8=debug scopes, not matrices!
inline size_t PacketSize(uint32_t kind) {
    constexpr size_t sizes[]={0x60,0x14,4,0x58,0x80,0x68,0x80,0x44,4,0x58};
    return kind<10?sizes[kind]:0;
}
inline bool IsQuad(uint32_t kind) { return kind==0||kind==3||kind==4||kind==5||kind==6||kind==9; }
inline uint64_t Fingerprint(const unsigned char* bytes) {
    uint64_t hash=14695981039346656037ULL;
    for (size_t i=0;i<FingerprintBytes;++i) { hash^=bytes[i]; hash*=1099511628211ULL; }
    return hash;
}
// Native f8ccb0 treats these as a screen-space scissor. A rotated map tile
// cannot keep its old unrotated per-packet scissor. Zero delegates to the native
// map-pass viewport scissor (renderer+188), not an unbounded screen draw.
inline bool UsePassClip(unsigned char* packet,size_t size) {
    if(size<0x44) return false;
    uint32_t kind=0; std::memcpy(&kind,packet,4);
    if(!IsQuad(kind) || PacketSize(kind)!=size) return false;
    std::memset(packet+0x34,0,16); return true;
}
struct PacketTag { uint32_t index=0; uint64_t fingerprint=0; Affine transform; };
struct FramePackets {
    uintptr_t queue=0;
    uint64_t tick=0,epoch=0,sequence=0;
    size_t count=0;
    bool valid=true;
    Affine map;
    std::array<PacketTag,MaxPackets> tags;
    void reject() { valid=false; count=0; }
    bool add(uint32_t index,uint64_t fingerprint,const Affine& transform) {
        if (!valid) return false;
        if (count==MaxPackets || (count && index<=tags[count-1].index) || !transform.finite()) {
            reject(); return false;
        }
        tags[count++]={index,fingerprint,transform}; return true;
    }
    template<class Reader> bool matches(Reader read,uintptr_t data,uint64_t words) const {
        if(!valid || !data || !count || count>MaxPackets || words>1048576) return false;
        for(size_t i=0;i<count;++i) {
            const auto index=tags[i].index;
            unsigned char bytes[FingerprintBytes];
            if(index>words || words-index<FingerprintBytes/4 ||
               !read(data+uintptr_t(index)*4,bytes,sizeof bytes) || Fingerprint(bytes)!=tags[i].fingerprint) return false;
        }
        return true;
    }
    const Affine* find(uint32_t index,uint64_t fingerprint) const {
        if (!valid) return nullptr;
        size_t low=0,high=count;
        while (low<high) {
            const size_t mid=low+(high-low)/2;
            if (tags[mid].index<index) low=mid+1; else high=mid;
        }
        return low<count && tags[low].index==index && tags[low].fingerprint==fingerprint ? &tags[low].transform : nullptr;
    }
};
} // namespace third_person::automap
