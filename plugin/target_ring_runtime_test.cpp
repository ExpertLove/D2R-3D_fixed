#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "target_ring_runtime.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
namespace {
bool active=true,buildOK=true,wantCopy=true,expectMapClip=false;
bool nativeClip(std::uintptr_t,std::uintptr_t packet,std::uintptr_t,unsigned,unsigned) {
    target_ring::SolidPacket p{}; CHECK(camera_automap::Read(packet,p));
    if(expectMapClip) for(auto n:p.clip) CHECK(n==0);
    else CHECK(p.clip[0]==11 && p.clip[1]==22);
    CHECK(p.color[0]==0.4f);return true;
}
unsigned calls=0;
std::uintptr_t originalQueue=0,originalData=0;
std::size_t originalBytes=0;
std::uint64_t dims=std::uint64_t(720)<<32|1280;
std::uint64_t sizeFn(std::uintptr_t) { return dims; }
bool activeFn() { return active; }
bool buildFn(unsigned w,unsigned h,target_ring::Batch& out) {
    if(!buildOK)return false;
    target_ring::Camera cam{};cam.view[0]=0.05f;cam.view[9]=0.03f;cam.view[15]=1;
    for(int i=0;i<4;++i)cam.projection[i*5]=1;
    return target_ring::Build(cam,0,0,w,h,out) && target_ring::AppendHealth(cam,0,0,0.5f,out);
}
void native(std::uintptr_t,std::uintptr_t queue,std::uintptr_t) {
    ++calls;
    if(!wantCopy) { CHECK(queue==originalQueue); return; }
    CHECK(queue!=originalQueue);
    camera_automap::Queue q{};CHECK(camera_automap::Read(queue,q));
    CHECK(q.words==originalBytes/4+(target_ring::SealQuads+3)*sizeof(target_ring::SolidPacket)/4);
    CHECK(camera_automap::consumingBase==q.data && camera_automap::consumingWords==originalBytes/4);
    CHECK(!std::memcmp((void*)q.data,(void*)originalData,originalBytes));
    CHECK(camera_automap::consuming->find(0,third_person::automap::Fingerprint((unsigned char*)q.data)));
    third_person::automap::Vertex v[4]={{0,0,1,0,0},{1,0,1,1,0},{0,1,1,0,1},{1,1,1,1,1}};
    CHECK(!ring_ui::Vertices(q.data,1280,720,v)); // original HUD/map packet unchanged
    const auto first=q.data+originalBytes;
    CHECK(!ring_ui::Vertices(first+1,1280,720,v));
    CHECK(ring_ui::Vertices(first,1280,720,v)); CHECK(v[0].x!=0 && v[0].z==1 && v[3].u==1);
    CHECK(ring_ui::Vertices(first,1920,1080,v)); // mismatch must collapse, not draw a red bounding box
    for(int i=1;i<4;++i) CHECK(!std::memcmp(&v[0],&v[i],sizeof v[0]));
    CHECK(!ring_ui::Vertices(first+(target_ring::SealQuads+3)*sizeof(target_ring::SolidPacket),1280,720,v));
}
}
int main() {
    std::vector<unsigned char> scene(0x15ed8+64);
    const auto renderer=(std::uintptr_t)scene.data(); originalQueue=renderer+0x15ed8;
    target_ring::SolidPacket prefix{};prefix.color[0]=0.4f;
    originalData=(std::uintptr_t)&prefix;originalBytes=sizeof prefix;
    const camera_automap::Queue source{originalData,originalBytes/4};
    std::memcpy((void*)originalQueue,&source,sizeof source);
    const std::uintptr_t fakeTarget=renderer+0x100;
    std::memcpy((void*)(renderer+0x1c0),&fakeTarget,8);
    ring_ui::ready=true;ring_ui::activeMode=activeFn;ring_ui::build=buildFn;ring_ui::sizeGetter=sizeFn;
    third_person::automap::FramePackets frame;
    CHECK(frame.add(0,third_person::automap::Fingerprint((unsigned char*)&prefix),{}));
    camera_automap::consuming=&frame;camera_automap::consumingBase=originalData;
    camera_automap::consumingWords=source.words;camera_automap::originalRender=native;
    ring_ui::Render(renderer,originalQueue,0,camera_automap::NativeRender);
    CHECK(calls==1 && camera_automap::consumingBase==originalData && !ring_ui::scope.batch);
    CHECK(!std::memcmp((void*)originalQueue,&source,sizeof source));
    CHECK(prefix.color[0]==0.4f && prefix.opacity==1);
    wantCopy=false; active=false;
    ring_ui::Render(renderer,originalQueue,0,camera_automap::NativeRender);CHECK(calls==2);
    active=true;buildOK=false;
    ring_ui::Render(renderer,originalQueue,0,camera_automap::NativeRender);CHECK(calls==3);
    buildOK=true;dims=32;
    ring_ui::Render(renderer,originalQueue,0,camera_automap::NativeRender);CHECK(calls==4);
    dims=std::uint64_t(720)<<32|1280;
    ring_ui::Render(renderer+1,originalQueue,0,camera_automap::NativeRender);CHECK(calls==5); // not main UI queue
    ring_ui::ready=false;
    ring_ui::Render(renderer,originalQueue,0,camera_automap::NativeRender);CHECK(calls==6);
    // A matched map packet inherits pass clip; stale tags / native HUD retain theirs.
    prefix.clip[0]=11;prefix.clip[1]=22;prefix.clip[2]=33;prefix.clip[3]=44;
    frame={};frame.epoch=camera_automap::epoch.load();
    CHECK(frame.add(0,third_person::automap::Fingerprint((unsigned char*)&prefix),{}));
    camera_automap::consuming=&frame;camera_automap::consumingBase=originalData;
    camera_automap::consumingWords=originalBytes/4;camera_automap::originalClip=nativeClip;
    expectMapClip=true;CHECK(camera_automap::HookClip(0,originalData,0,1280,720));
    CHECK(prefix.clip[0]==11 && prefix.clip[3]==44); // original packet not modified
    ++frame.epoch;expectMapClip=false;CHECK(camera_automap::HookClip(0,originalData,0,1280,720));
    camera_automap::consuming=nullptr;CHECK(camera_automap::HookClip(0,originalData,0,1280,720));
    std::puts("private native ring queue/vertex scopes PASS; not a game/pixel test");
}
