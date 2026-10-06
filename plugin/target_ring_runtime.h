#pragma once
#include <vector>
#include <memory>
#include "automap_runtime.h"
#include "target_ring.h"

namespace ring_ui {
using BuildFn=bool(*)(unsigned,unsigned,target_ring::Batch&);
using ActiveFn=bool(*)();
using SizeFn=std::uint64_t(*)(std::uintptr_t);
inline BuildFn build=nullptr;
inline ActiveFn activeMode=nullptr;
inline SizeFn sizeGetter=nullptr;
inline std::atomic<bool> ready{false};
inline std::atomic<unsigned> frames{0},uploads{0},skipped{0};
struct Scope { std::uintptr_t first=0; const target_ring::Batch* batch=nullptr; };
inline thread_local Scope scope;
inline bool Size(std::uintptr_t target,std::uint64_t& out) noexcept {
    if(!target || !sizeGetter) return false;
    __try { out=sizeGetter(target); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool Vertices(std::uintptr_t packet,unsigned width,unsigned height,third_person::automap::Vertex (&v)[4]) {
    if(!scope.batch || packet<scope.first) return false;
    const auto offset=packet-scope.first;
    if(offset%sizeof(target_ring::SolidPacket) || offset/sizeof(target_ring::SolidPacket)>=scope.batch->count) return false;
    const auto& batch=*scope.batch;
    if(width==batch.width && height==batch.height && target_ring::Apply(batch.quads[offset/sizeof(target_ring::SolidPacket)],width,height,v)) {
        ++uploads; return true;
    }
    // Own packet must never fall back to a filled bounding rectangle on mismatch.
    for(unsigned i=1;i<4;++i) v[i]=v[0];
    ++skipped; return true;
}
inline void Render(std::uintptr_t renderer,std::uintptr_t queue,std::uintptr_t commands,camera_automap::RenderFn original) {
    if(!ready.load() || scope.batch || !build || !activeMode || !activeMode() ||
        queue!=renderer+0x15ed8) { original(renderer,queue,commands); return; }
    camera_automap::Queue source{};
    std::uintptr_t target=0; std::uint64_t dimensions=0;
    if(!camera_automap::Read(queue,source) || source.words>1048576 || (source.words && !source.data) ||
       !camera_automap::Read(renderer+0x1c0,target) || !Size(target,dimensions)) {
        ++skipped; original(renderer,queue,commands); return;
    }
    std::unique_ptr<target_ring::Batch> owned;
    bool built=false;
    try {
        owned=std::make_unique<target_ring::Batch>();
        built=build(unsigned(dimensions),unsigned(dimensions>>32),*owned);
    } catch(...) { ++skipped; original(renderer,queue,commands); return; }
    auto& batch=*owned;
    if(!built || !batch.count || batch.count>target_ring::MaxQuads) {
        ++skipped; original(renderer,queue,commands); return;
    }
    const auto oldBytes=std::size_t(source.words)*4;
    const auto totalBytes=oldBytes+std::size_t(batch.count)*sizeof(target_ring::SolidPacket);
    std::vector<std::uint32_t> storage; // no nontrivial DLL TLS destructor on renderer thread
    try { storage.resize(totalBytes/4); }
    catch(...) { ++skipped; original(renderer,queue,commands); return; }
    auto* bytes=reinterpret_cast<unsigned char*>(storage.data());
    for(std::size_t offset=0;offset<oldBytes;) {
        const auto n=std::min(std::size_t(262144),oldBytes-offset);
        if(!camera_automap::Read(source.data+offset,bytes+offset,n)) {
            ++skipped; original(renderer,queue,commands); return;
        }
        offset+=n;
    }
    for(unsigned i=0;i<batch.count;++i)
        std::memcpy(bytes+oldBytes+i*sizeof(target_ring::SolidPacket),&batch.quads[i].packet,sizeof(target_ring::SolidPacket));
    // Preserve original queue & packets byte-for-byte. The map adapter remaps
    // only consumingBase to this private copy; tag indices/fingerprints unchanged.
    struct LocalQueue { std::uintptr_t data; std::uint64_t words,capacity; };
    LocalQueue copy{reinterpret_cast<std::uintptr_t>(bytes),totalBytes/4,totalBytes/4};
    const auto previous=scope;
    scope={copy.data+oldBytes,&batch};
    original(renderer,reinterpret_cast<std::uintptr_t>(&copy),commands);
    scope=previous;
    ++frames;
}
inline bool Install(const D2RL::PluginContext* ctx,std::uintptr_t base,ActiveFn active,BuildFn builder) {
    const auto match=[&](std::uintptr_t rva,std::initializer_list<unsigned char> expected) {
        unsigned char bytes[32]{};
        return expected.size()<=sizeof bytes && camera_automap::Read(base+rva,bytes,expected.size()) &&
            !std::memcmp(bytes,expected.begin(),expected.size());
    };
    if(!camera_automap::ready.load() ||
       !match(0xe1967d,{0x48,0x8d,0x93,0x28,0x80,0x03,0x00}) ||
       !match(0xe19687,{0x48,0x8d,0x8b,0x50,0x21,0x02,0x00}) ||
       !match(0xe1968e,{0xe8,0x0d,0x2a,0x17,0x00}) ||
       !match(0xedcaf0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10}) ||
       !match(0xedcb68,{0xf3,0x0f,0x2c,0xc0,0x8b,0xcb}) ||
       !match(0xf8c3eb,{0x48,0x8b,0x46,0x08}) ||
       !match(0xf8c4f9,{0xe8,0xd2,0xf3,0xff,0xff,0xbf,0x60,0x00,0x00,0x00}) ||
       !match(0xf8b8d0,{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x41,0x57}) ||
       !match(0xf8b8fe,{0x4c,0x39,0x7b,0x48,0x74,0x14}) ||
       !match(0xf8ba1f,{0x0f,0x10,0x43,0x24}) ||
       !match(0xf8ba31,{0x8b,0x43,0x50}) ||
       !match(0x657910,{0x89,0x4c,0x24,0x60}) ||
       !match(0x657998,{0x0f,0x11,0x45,0x84}) ||
       !match(0x6579bd,{0xf3,0x44,0x0f,0x11,0x45,0xb0})) {
        ctx->LogWarn("target visuals: UI layout rejected; targeting stays unavailable"); return false;
    }
    activeMode=active; build=builder; sizeGetter=reinterpret_cast<SizeFn>(base+0xedcaf0);
    camera_automap::extraRender=&Render;
    camera_automap::extraVertices=&Vertices;
    ready.store(true);
    ctx->LogInfo("target visuals: animated seal ready; custom overhead HP disabled; native name/HP panel retained");
    return true;
}
inline void Flush(const D2RL::PluginContext* ctx) {
    static ULONGLONG last=0; const auto now=GetTickCount64(); if(!ctx || now-last<5000) return; last=now;
    const auto f=frames.exchange(0),u=uploads.exchange(0),s=skipped.exchange(0);
    if(f || u || s) { char message[160]; std::snprintf(message,sizeof message,"target ring: frames=%u vertex-quads=%u skipped=%u (not pixel proof)",f,u,s); ctx->LogInfo(message); }
}
} // namespace ring_ui
