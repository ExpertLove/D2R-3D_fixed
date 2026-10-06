#pragma once
// Build-specific native automap adapter. No world/unit writes, no attack calls.
// Queue sidecar tags keep map transforms separate from HUD and deferred rendering.
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <D2RLPlugin/context.h>
#include "automap_packets.h"

namespace camera_automap {
using namespace third_person::automap;
using ActiveFn=bool(*)();
using HeadingFn=bool(*)(float&,float&);
inline ActiveFn activeMode=nullptr;
inline HeadingFn heading=nullptr;
inline uintptr_t base=0;
inline std::atomic<bool> ready{false},enabled{true};
inline std::atomic<uint64_t> epoch{1},serial{0};
inline std::atomic<uint64_t> produced{0},consumed{0},transformed{0},rejected{0},clipAdjusted{0},mismatches{0};
inline SRWLOCK bankLock=SRWLOCK_INIT;
inline FramePackets banks[4];
inline thread_local FramePackets* producing=nullptr;
inline thread_local const FramePackets* consuming=nullptr;
inline thread_local uintptr_t consumingBase=0;
inline thread_local uint64_t consumingWords=0;
struct Layer { bool active=false; Affine transform; };
inline thread_local Layer layer;
struct VertexScope { bool active=false; Affine transform; uint32_t width=0,height=0; };
inline thread_local VertexScope vertices;
struct MapContext {
    int32_t bounds[4],origin[2],clip[4],anchor[2];
    float scaleX,scaleY,spriteScale;
};
static_assert(sizeof(MapContext)==0x3c && offsetof(MapContext,scaleX)==0x30);
struct IntPoint { int32_t x,y; };
struct Queue { uintptr_t data; uint64_t words; };
inline bool Read(uintptr_t p,void* out,size_t n) noexcept {
    if (p<0x10000 || n>262144 || p>UINTPTR_MAX-n) return false;
    __try { memcpy(out,reinterpret_cast<const void*>(p),n); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool Read(uintptr_t p,T& value) { return Read(p,&value,sizeof value); }
inline bool Active() { return ready.load() && enabled.load() && activeMode && activeMode(); }
inline void Invalidate() { epoch.fetch_add(1); }
inline Point Unpack(uint64_t p) { return {float(int32_t(p)),float(int32_t(p>>32))}; }
inline bool PlayerProjection(uint64_t& projected) {
    uint32_t slot=0,id=0;
    uintptr_t unit=0,path=0;
    if (!Read(base+0x2a23704,slot)||slot>=8||!Read(base+0x2a238f0+4*slot,id)||id==UINT32_MAX||
        !Read(base+0x2a23910+(id&127)*8,unit)) return false;
    for (unsigned n=0;unit && n<512;++n) {
        uint32_t type=0,uid=0;
        if (!Read(unit,type)||!Read(unit+8,uid)) return false;
        if (type==0 && uid==id) {
            return Read(unit+0x38,path)&&path && Read(path+8,projected);
        }
        if (!Read(unit+0x158,unit)) return false;
    }
    return false;
}
using MapFn=void(*)(uintptr_t,uintptr_t,uintptr_t);
using ContextFn=uintptr_t(*)(uintptr_t,uintptr_t,uint64_t,float);
using TileFn=void(*)(int,uintptr_t,uintptr_t,uintptr_t);
using IconFn=void(*)(uint64_t,int,float);
using TextFn=void(*)(uintptr_t,uint64_t,float,int);
using AppendFn=uintptr_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t);
using RenderFn=void(*)(uintptr_t,uintptr_t,uintptr_t);
using QuadFn=void(*)(uintptr_t,uintptr_t,uintptr_t,uint32_t,uint32_t);
using ClipFn=bool(*)(uintptr_t,uintptr_t,uintptr_t,uint32_t,uint32_t);
using UploadFn=uintptr_t(*)(uintptr_t,uintptr_t,const void*,uint32_t,uint32_t,uint32_t);
inline MapFn originalMap=nullptr;
inline ContextFn originalContext=nullptr;
inline TileFn originalTile=nullptr;
inline IconFn originalIcon=nullptr;
inline TextFn originalText=nullptr;
inline AppendFn originalAppend=nullptr;
inline RenderFn originalRender=nullptr;
inline QuadFn originalQuad=nullptr;
inline ClipFn originalClip=nullptr;
inline UploadFn originalUpload=nullptr;
// Optional append-only UI decoration. Existing map tags continue to address the
// unchanged prefix of a private queue copy; no native queue/world writes.
using ExtraRenderFn=void(*)(uintptr_t,uintptr_t,uintptr_t,RenderFn);
using ExtraVerticesFn=bool(*)(uintptr_t,uint32_t,uint32_t,Vertex (&)[4]);
inline ExtraRenderFn extraRender=nullptr;
inline ExtraVerticesFn extraVertices=nullptr;
struct QuadScope { uintptr_t packet=0; uint32_t width=0,height=0; };
inline thread_local QuadScope quadScope;
inline void NativeRender(uintptr_t renderer,uintptr_t queue,uintptr_t commands) {
    const auto savedBase=consumingBase;
    Queue q{};
    if(consuming && Read(queue,q)) consumingBase=q.data; // same prefix indices in private copy
    originalRender(renderer,queue,commands);
    consumingBase=savedBase;
}
inline void DispatchRender(uintptr_t renderer,uintptr_t queue,uintptr_t commands) {
    if(extraRender) extraRender(renderer,queue,commands,&NativeRender);
    else NativeRender(renderer,queue,commands);
}

inline bool ProjectNative(uintptr_t context,uint64_t coords,IntPoint& result) noexcept {
    using Fn=uintptr_t(*)(uintptr_t,IntPoint*,uint64_t);
    __try { reinterpret_cast<Fn>(base+0xd4910)(context,&result,coords); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool SetContextBounds(uintptr_t p,const MapContext& value) noexcept {
    // Only the transient automap draw context created by originalContext.
    // Origin/scale remain unchanged; only producer culling rectangles widen.
    __try {
        memcpy(reinterpret_cast<void*>(p),value.bounds,sizeof value.bounds);
        memcpy(reinterpret_cast<void*>(p+0x18),value.clip,sizeof value.clip);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool IsHdRenderer() noexcept {
    // This is the same no-argument getter used by every native sprite submit.
    // A null override pointer is normal; the getter then uses its stock fallback.
    __try { return reinterpret_cast<bool(*)()>(base+0x846210)(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline void __cdecl HookMap(uintptr_t rect,uintptr_t anchor,uintptr_t options) {
    const bool usable=Active() && IsHdRenderer();
    if (!usable || producing) {
        // A nested/disabled pass must not inherit another map's scope.
        auto* previous=producing; const auto previousLayer=layer;
        producing=nullptr; layer.active=false;
        originalMap(rect,anchor,options);
        producing=previous; layer=previousLayer;
        return;
    }
    FramePackets frame;
    frame.valid=false; // context hook must establish the pivot and heading first
    frame.tick=GetTickCount64(); frame.epoch=epoch.load(); frame.sequence=++serial;
    producing=&frame;
    originalMap(rect,anchor,options);
    producing=nullptr;
    if (!frame.valid || !frame.queue || !frame.count || frame.epoch!=epoch.load()) { ++rejected; return; }
    AcquireSRWLockExclusive(&bankLock);
    size_t slot=0;
    for (size_t i=0;i<4;++i) {
        if (banks[i].queue==frame.queue) { slot=i; break; }
        if (banks[i].tick<banks[slot].tick) slot=i;
    }
    banks[slot]=frame;
    ReleaseSRWLockExclusive(&bankLock);
    ++produced;
}
inline uintptr_t HookContext(uintptr_t out,uintptr_t rect,uint64_t anchor,float scale) {
    const auto result=originalContext(out,rect,anchor,scale);
    if (!producing || uintptr_t(_ReturnAddress())!=base+0xd26a7) return result;
    MapContext context{};
    uint64_t player=0; IntPoint pivot{}; float fx=0,fz=0;
    if (result!=out || !Read(out,context) || !heading || !heading(fx,fz) ||
        !PlayerProjection(player) || !ProjectNative(out,player,pivot)) return result;
    const Rect viewport{float(context.clip[0]),float(context.clip[1]),float(context.clip[2]),float(context.clip[3])};
    // Full Tab overlay only. Tiny native corner maps retain their stock behavior.
    if (viewport.w<640 || viewport.h<360 || viewport.w>16384 || viewport.h>16384) return result;
    const Point center{viewport.x+viewport.w*0.5f,viewport.y+viewport.h*0.5f};
    Affine map;
    if (!HeadingUp(fx,fz,context.scaleX,context.scaleY,{float(pivot.x),float(pivot.y)},center,map)) return result;
    Rect inverse;
    if (!InverseBounds(map,viewport,inverse)) return result;
    // More original tiles may rotate into the viewport. Native GPU scissor is
    // still the ORIGINAL viewport (draw-pass argument), not this expanded box.
    const float left=std::floor(inverse.x)-32,top=std::floor(inverse.y)-32;
    const float right=std::ceil(inverse.x+inverse.w)+32,bottom=std::ceil(inverse.y+inverse.h)+32;
    if (std::abs(left)>100000 || std::abs(top)>100000 || right-left>32768 || bottom-top>32768) return result;
    const double bx=std::floor(context.origin[0]+(left-context.anchor[0])/context.scaleX)-2;
    const double by=std::floor(context.origin[1]+(top-context.anchor[1])/context.scaleY)-2;
    const double bw=std::ceil((right-left)/context.scaleX)+4;
    const double bh=std::ceil((bottom-top)/context.scaleY)+4;
    if (!std::isfinite(bx)||!std::isfinite(by)||std::abs(bx)>1000000||std::abs(by)>1000000||bw>100000||bh>100000) return result;
    context.bounds[0]=int32_t(bx); context.bounds[1]=int32_t(by);
    context.bounds[2]=int32_t(bw); context.bounds[3]=int32_t(bh);
    context.clip[0]=int32_t(left); context.clip[1]=int32_t(top);
    context.clip[2]=int32_t(right-left); context.clip[3]=int32_t(bottom-top);
    if (!SetContextBounds(out,context)) return result;
    producing->map=map; producing->valid=true;
    return result;
}
inline void HookTile(int kind,uintptr_t cells,uintptr_t context,uintptr_t options) {
    const auto previous=layer;
    if (producing && producing->valid) layer={true,producing->map};
    originalTile(kind,cells,context,options);
    layer=previous;
}
inline void HookIcon(uint64_t point,int kind,float scale) {
    const auto previous=layer;
    if (producing && producing->valid) layer={true,UprightAt(producing->map,Unpack(point))};
    originalIcon(point,kind,scale);
    layer=previous;
}
inline void HookText(uintptr_t text,uint64_t point,float scale,int style) {
    const auto previous=layer;
    if (producing && producing->valid) layer={true,UprightAt(producing->map,Unpack(point))};
    originalText(text,point,scale,style);
    layer=previous;
}
inline uintptr_t HookAppend(uintptr_t queue,uintptr_t at,uintptr_t first,uintptr_t last) {
    // Bind/invalidate even a skipped map pass (its scissor command arrives first),
    // so a failed heading/player read cannot reuse a prior frame's sidecar.
    if (producing && !producing->queue) {
        Queue q{};
        if (Read(queue,q) && q.words<=1048576 && (!q.words || q.data) && at==q.data+q.words*4) {
            producing->queue=queue;
            AcquireSRWLockExclusive(&bankLock);
            for (auto& bank:banks) if (bank.queue==queue) { bank.valid=false; bank.count=0; }
            ReleaseSRWLockExclusive(&bankLock);
        }
    }
    if (producing && producing->valid && layer.active) {
        auto& frame=*producing;
        Queue q{};
        if (last<first || last-first>262144 || !Read(queue,q) || q.words>1048576 ||
            (q.words && !q.data) || at!=q.data+q.words*4 || (frame.queue && frame.queue!=queue)) frame.reject();
        else {
            for (size_t offset=0;frame.valid && offset<last-first;) {
                uint32_t kind=0;
                if (!Read(first+offset,kind)) { frame.reject(); break; }
                const size_t size=PacketSize(kind);
                if (!size || size>last-first-offset) { frame.reject(); break; }
                if (IsQuad(kind)) {
                    unsigned char bytes[FingerprintBytes];
                    if (!Read(first+offset,bytes,sizeof bytes)) { frame.reject(); break; }
                    frame.add(uint32_t(q.words+offset/4),Fingerprint(bytes),layer.transform);
                }
                offset+=size;
            }
        }
    }
    return originalAppend(queue,at,first,last); // exact original payload and result
}
inline void HookRender(uintptr_t renderer,uintptr_t queue,uintptr_t commandContext) {
    if (!Active() || consuming) {
        const auto* previous=consuming; const auto previousVertices=vertices;
        consuming=nullptr; vertices.active=false;
        DispatchRender(renderer,queue,commandContext);
        consuming=previous; vertices=previousVertices;
        return;
    }
    FramePackets snapshot;
    snapshot.valid=false;
    AcquireSRWLockShared(&bankLock);
    for (const auto& frame:banks) if (frame.queue==queue && frame.valid) { snapshot=frame; break; }
    ReleaseSRWLockShared(&bankLock);
    const auto now=GetTickCount64(); Queue q{};
    if (!snapshot.valid || snapshot.epoch!=epoch.load() || now<snapshot.tick || now-snapshot.tick>250 ||
        !Read(queue,q) || !q.data || q.words>1048576 || !snapshot.count || snapshot.tags[snapshot.count-1].index>=q.words) {
        DispatchRender(renderer,queue,commandContext); return;
    }
    // All-or-nothing: a partially matching sidecar used to rotate some map
    // pieces while leaving others at native positions after queue reuse.
    if(!snapshot.matches([](uintptr_t p,void* dst,size_t n){return Read(p,dst,n);},q.data,q.words)) {
        ++mismatches; DispatchRender(renderer,queue,commandContext); return;
    }
    consuming=&snapshot; consumingBase=q.data; consumingWords=q.words;
    DispatchRender(renderer,queue,commandContext);
    consuming=nullptr; consumingBase=0; consumingWords=0;
    AcquireSRWLockExclusive(&bankLock);
    for (auto& frame:banks) if (frame.queue==queue && frame.sequence==snapshot.sequence) { frame.valid=false; frame.count=0; }
    ReleaseSRWLockExclusive(&bankLock);
    ++consumed;
}
inline const Affine* TaggedTransform(uintptr_t packet) {
    if (consuming && consuming->epoch==epoch.load() && packet>=consumingBase &&
        (packet-consumingBase)%4==0 && (packet-consumingBase)/4<consumingWords) {
        const auto index=uint32_t((packet-consumingBase)/4);
        // Only read tagged packets; no game/HUD geometry is classified by texture.
        size_t lo=0,hi=consuming->count;
        while (lo<hi) { size_t mid=lo+(hi-lo)/2; if (consuming->tags[mid].index<index) lo=mid+1; else hi=mid; }
        if (lo<consuming->count && consuming->tags[lo].index==index && consumingWords-index>=FingerprintBytes/4) {
            unsigned char bytes[FingerprintBytes];
            if (Read(packet,bytes,sizeof bytes))
                return consuming->find(index,Fingerprint(bytes));
        }
    }
    return nullptr;
}
inline bool HookClip(uintptr_t renderer,uintptr_t packet,uintptr_t commands,uint32_t width,uint32_t height) {
    if(TaggedTransform(packet)) {
        uint32_t kind=0;
        if(Read(packet,kind)) {
            const auto size=PacketSize(kind);
            alignas(16) unsigned char copy[0x80]{};
            if(size && size<=sizeof copy && Read(packet,copy,size) && UsePassClip(copy,size)) {
                ++clipAdjusted;
                // Run original to maintain native GPU-scissor state, not return true blindly.
                return originalClip(renderer,reinterpret_cast<uintptr_t>(copy),commands,width,height);
            }
        }
    }
    return originalClip(renderer,packet,commands,width,height);
}
inline void HookQuad(uintptr_t renderer,uintptr_t packet,uintptr_t commands,uint32_t width,uint32_t height) {
    const auto previous=vertices;
    const auto savedQuad=quadScope;
    quadScope={packet,width,height};
    vertices.active=false;
    if(const auto* transform=TaggedTransform(packet)) vertices={true,*transform,width,height};
    originalQuad(renderer,packet,commands,width,height);
    vertices=previous; quadScope=savedQuad;
}
inline uintptr_t HookUpload(uintptr_t allocator,uintptr_t out,const void* data,uint32_t size,uint32_t alignment,uint32_t flags) {
    if (extraVertices && size==80 && uintptr_t(_ReturnAddress())==base+0xf8cf4d) {
        alignas(16) Vertex copy[4];
        if(Read(uintptr_t(data),copy,sizeof copy) && extraVertices(quadScope.packet,quadScope.width,quadScope.height,copy))
            return originalUpload(allocator,out,copy,size,alignment,flags);
    }
    if (vertices.active && size==80 && uintptr_t(_ReturnAddress())==base+0xf8cf4d) {
        alignas(16) Vertex copy[4];
        if (Read(uintptr_t(data),copy,sizeof copy) && TransformQuad(copy,vertices.transform,vertices.width,vertices.height)) {
            ++transformed;
            return originalUpload(allocator,out,copy,size,alignment,flags);
        }
    }
    return originalUpload(allocator,out,data,size,alignment,flags);
}
inline void Flush(const D2RL::PluginContext* ctx) {
    if (!ctx || !ready.load()) return;
    static ULONGLONG last=0;
    const auto now=GetTickCount64();
    if (now-last<5000) return;
    last=now;
    const auto p=produced.exchange(0),c=consumed.exchange(0),v=transformed.exchange(0),r=rejected.exchange(0);
    if (!p && !c && !v && !r) return;
    const auto clips=clipAdjusted.exchange(0),bad=mismatches.exchange(0);
    char message[224];
    snprintf(message,sizeof message,"automap follow: frames=%llu consumed=%llu quads=%llu skipped=%llu clips=%llu mismatches=%llu (visual test pending)",p,c,v,r,clips,bad);
    ctx->LogInfo(message);
}
inline const char* Toggle() {
    if (!ready.load()) return "mapfollow unavailable: native map hook/layout check failed";
    enabled.store(!enabled.load()); Invalidate();
    return enabled.load()?"mapfollow ON (in F10; native Tab map)":"mapfollow OFF (stock map)";
}
inline bool Install(const D2RL::PluginContext* ctx,uintptr_t image,ActiveFn active,HeadingFn getHeading) {
    base=image; activeMode=active; heading=getHeading;
    const auto match=[&](uintptr_t rva,std::initializer_list<unsigned char> expected) {
        unsigned char bytes[32]{};
        return expected.size()<=sizeof bytes && Read(base+rva,bytes,expected.size()) && !memcmp(bytes,expected.begin(),expected.size());
    };
    // Guard coordinate chain, packet layout and upload coordinate convention.
    if (!match(0x846210,{0x48,0x8b,0x05,0x59,0x9f,0xbf,0x02}) ||
        !match(0x84621c,{0x0f,0xb6,0x80,0xb9,0,0,0}) ||
        !match(0x846224,{0xe9,0xe7,0x9f,0xe0,0xff}) ||
        !match(0x8b2d0,{0x8b,0x05,0x2e,0x84,0x99,0x02,0xc3}) ||
        !match(0x9a4f4,{0x48,0x8d,0x0d,0x15,0x94,0x98,0x02}) ||
        !match(0x341c10,{0x8b,0x41,0x08,0xc3}) || !match(0x341c20,{0x8b,0x41,0x0c,0xc3}) ||
        !match(0xd4910,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20}) ||
        !match(0xd26a2,{0xe8,0x99,0xf2,0xff,0xff}) ||
        !match(0xf8ce70,{0xf3,0x0f,0x5c,0x5a,0x0c,0xf3,0x0f,0x5c,0x4a,0x04}) ||
        !match(0xf8ce7d,{0xf3,0x0f,0x58,0x62,0x08,0xf3,0x0f,0x58,0x52,0x10}) ||
        !match(0x1cba900,{0,0,0,0x3f}) ||
        !match(0xf8cf48,{0xe8,0x13,0x09,0x0f,0}) ||
        !match(0x107d88d,{0x48,0x8b,0x4e,0x08,0x44,0x8b,0xc3,0x48,0x8b,0xd7})) {
        ctx->LogError("automap follow: layout rejected; map unchanged"); return false;
    }
    const uint8_t mapSig[]={0x40,0x55,0x56,0x57,0x41,0x56,0x48,0x8d,0xac,0x24,0x28,0xff,0xff,0xff};
    const uint8_t contextSig[]={0x4c,0x89,0x44,0x24,0x18,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57};
    const uint8_t tileSig[]={0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
    const uint8_t iconSig[]={0x83,0xfa,0xff,0x0f,0x84,0x4b,0x01,0,0,0x55,0x48,0x8d,0x6c,0x24,0xf0};
    const uint8_t textSig[]={0x4c,0x8b,0xdc,0x55,0x53,0x56,0x57,0x41,0x54,0x49,0x8d,0x6b,0xa1};
    const uint8_t appendSig[]={0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x18,0x49,0x89,0x6b,0x20,0x56,0x57,0x41,0x56};
    const uint8_t renderSig[]={0x4c,0x8b,0xdc,0x55,0x53,0x56,0x57,0x49,0x8d,0xab,0xc8,0xfc,0xff,0xff};
    const uint8_t clipSig[]={0x48,0x83,0xec,0x48,0x48,0x8b,0x05,0x0d,0xe6,0xa3,0x01,0x48,0x33,0xc4};
    const uint8_t quadSig[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x7c,0x24,0x20,0x55,0x48,0x8d,0x6c,0x24,0xb1};
    const uint8_t uploadSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30};
    bool ok=true;
    ok=ctx->InstallInlineHook(0xd2600,mapSig,sizeof mapSig,&HookMap,&originalMap)&&ok;
    ok=ctx->InstallInlineHook(0xd1940,contextSig,sizeof contextSig,&HookContext,&originalContext)&&ok;
    ok=ctx->InstallInlineHook(0xd6f10,tileSig,sizeof tileSig,&HookTile,&originalTile)&&ok;
    ok=ctx->InstallInlineHook(0xd6db0,iconSig,sizeof iconSig,&HookIcon,&originalIcon)&&ok;
    ok=ctx->InstallInlineHook(0xd6b20,textSig,sizeof textSig,&HookText,&originalText)&&ok;
    ok=ctx->InstallInlineHook(0x64c440,appendSig,sizeof appendSig,&HookAppend,&originalAppend)&&ok;
    ok=ctx->InstallInlineHook(0xf8c0a0,renderSig,sizeof renderSig,&HookRender,&originalRender)&&ok;
    ok=ctx->InstallInlineHook(0xf8ccb0,clipSig,sizeof clipSig,&HookClip,&originalClip)&&ok;
    ok=ctx->InstallInlineHook(0xf8ce10,quadSig,sizeof quadSig,&HookQuad,&originalQuad)&&ok;
    ok=ctx->InstallInlineHook(0x107d860,uploadSig,sizeof uploadSig,&HookUpload,&originalUpload)&&ok;
    ready.store(ok);
    ctx->LogInfo(ok?"automap follow: native Tab-map transform + scoped viewport clipping installed; only in F10; mapfollow toggles; gameplay unverified":
        "automap follow: hook rejected; all installed map hooks stay passthrough");
    return ok;
}
} // namespace camera_automap
