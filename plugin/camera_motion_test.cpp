#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "camera_motion.h"
#include "hud_toast.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)

namespace {
float Cross(const target_ring::Quad& q,unsigned w,unsigned h) {
    target_ring::Vertex v[4]{};
    CHECK(target_ring::Apply(q,w,h,v));
    return (v[1].x-v[0].x)*(v[2].y-v[0].y)-(v[1].y-v[0].y)*(v[2].x-v[0].x);
}
target_ring::Camera FlatCamera(float sx,float sz) {
    target_ring::Camera cam{};cam.view[0]=sx;cam.view[9]=sz;cam.view[15]=1;
    for(int i=0;i<4;++i) cam.projection[i*5]=1;
    return cam;
}
std::wstring TempIni(const char* body) {
    wchar_t dir[MAX_PATH],path[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH,dir));CHECK(GetTempFileNameW(dir,L"cam",0,path));
    FILE* f=nullptr;CHECK(_wfopen_s(&f,path,L"wb")==0 && f);
    std::fputs(body,f);std::fclose(f);
    return path;
}
} // namespace

int main() {
    using namespace camera_motion;
    // Wheel: fractional deltas zoom proportionally; 120 = one classic notch.
    CHECK(WheelNotches(120)==1.0f && WheelNotches(-240)==-2.0f && WheelNotches(30)==0.25f);
    CHECK(Zoom(45,1)<45 && Zoom(45,-1)>45);
    CHECK(Zoom(45,0.25f)<45 && Zoom(45,0.25f)>Zoom(45,1)); // old integer division ignored this
    CHECK(std::abs(Zoom(Zoom(45,0.5f),0.5f)-Zoom(45,1))<1e-3f); // two half notches == one notch
    CHECK(Zoom(3.6f,50)==MinDistance && Zoom(1400,-50)==MaxDistance);
    CHECK(Zoom(45,std::numeric_limits<float>::quiet_NaN())==45);
    CHECK(ClampDistance(std::numeric_limits<float>::infinity())==DefaultDistance);
    // Pitch: the old -85 floor put the eye under the terrain.
    CHECK(ClampPitch(-85)==MinPitch && ClampPitch(80)==MaxPitch && ClampPitch(22)==22);
    CHECK(ClampPitch(std::numeric_limits<float>::quiet_NaN())==DefaultPitch);
    CHECK(MinPitch>-30.0f); // keep above native ~30 degree elevation horizon
    // Smoothing: converges, never overshoots, frame-rate independent.
    float v=0; for(int i=0;i<200;++i) { v=Approach(v,10,0.01f,0.07f); CHECK(v<=10); }
    CHECK(v==10);
    const float one=Approach(0,10,0.02f,0.07f),two=Approach(Approach(0,10,0.01f,0.07f),10,0.01f,0.07f);
    CHECK(std::abs(one-two)<1e-4f);
    CHECK(Approach(5,10,0,0.07f)==5 && Approach(std::numeric_limits<float>::quiet_NaN(),3,0.01f,0.07f)==3);
    // Shoulder: sign convention and bounded magnitude.
    CHECK(ShoulderOffset(0,45)==0 && ShoulderOffset(1,45)>0 && ShoulderOffset(2,45)<0);
    CHECK(ShoulderOffset(1,45)==-ShoulderOffset(2,45) && ShoulderOffset(1,1500)==7.0f);
    CHECK(ShoulderOffset(1,std::numeric_limits<float>::quiet_NaN())==0);
    // Presets cycle and stay inside limits; CLASSIC equals the shipped default.
    CHECK(PresetCount==3 && NextPreset(PresetCount-1)==0 && NextPreset(0)==1);
    CHECK(Presets[0].distance==DefaultDistance && Presets[0].pitch==DefaultPitch);
    for(const auto& p:Presets) CHECK(ClampDistance(p.distance)==p.distance && ClampPitch(p.pitch)==p.pitch);

    // Toasts: uppercase/truncated copy, fade envelope, bounded native quads.
    hud_toast::Show("view: close",1000);
    target_ring::Batch toast;toast.width=1920;toast.height=1080;
    CHECK(hud_toast::Draw(toast,1100));
    CHECK(std::strcmp(hud_toast::text,"VIEW: CLOSE")==0);
    CHECK(toast.count>20 && toast.count<600);
    for(unsigned i=0;i<toast.count;++i) {
        CHECK(Cross(toast.quads[i],1920,1080)>0);
        for(auto p:toast.quads[i].points) CHECK(p.x>=0 && p.y>=0 && p.x<=1920 && p.y<=1080*0.3f);
    }
    CHECK(hud_toast::Alpha(0)==1 && hud_toast::Alpha(hud_toast::DurationMs)==0);
    const float fading=hud_toast::Alpha(hud_toast::DurationMs-100);
    CHECK(fading>0 && fading<1);
    toast.count=0;CHECK(!hud_toast::Draw(toast,1000+hud_toast::DurationMs+1) && toast.count==0);
    hud_toast::Show("0123456789012345678901234567890123456789EXTRA",5);
    CHECK(std::strlen(hud_toast::text)==hud_toast::MaxText);
    target_ring::Batch tiny;tiny.width=640;tiny.height=360;CHECK(hud_toast::Draw(tiny,10));
    for(unsigned i=0;i<tiny.count;++i) for(auto p:tiny.quads[i].points) CHECK(p.x>=0 && p.x<=640);
    hud_toast::Clear();tiny.count=0;CHECK(!hud_toast::Draw(tiny,20));

    // Seal at 4K / close zoom: legitimately larger than the old fixed 350 px cap.
    target_ring::Batch seal;
    CHECK(target_ring::Build(FlatCamera(0.08f,0.05f),0,0,3840,2160,seal));
    CHECK(seal.count==target_ring::SealQuads);
    float span=0;
    for(unsigned i=0;i<seal.count;++i) for(auto p:seal.quads[i].points) span=std::max(span,std::hypot(p.x-1920,p.y-1080));
    CHECK(span>350 && span<0.42f*2160);
    CHECK(!target_ring::Build(FlatCamera(0.5f,0.3f),0,0,1280,720,seal)); // runaway near-plane size still rejected

    // Off-screen arrow: none while the seal centre is visible.
    target_ring::Batch arrow;arrow.width=1280;arrow.height=720;
    auto cam=FlatCamera(0.05f,0.03f);
    CHECK(!target_ring::AppendOffscreenArrow(cam,0,0,arrow) && arrow.count==0);
    // Far to the right in front: chevron hugs the right margin, correct winding.
    CHECK(target_ring::AppendOffscreenArrow(cam,1000,0,arrow,500) && arrow.count==4);
    for(unsigned i=0;i<arrow.count;++i) {
        CHECK(Cross(arrow.quads[i],1280,720)>0);
        for(auto p:arrow.quads[i].points) CHECK(p.x>1280*0.80f && p.x<=1280*0.95f && p.y>720*0.40f && p.y<720*0.60f);
        CHECK(arrow.quads[i].packet.opacity>0 && arrow.quads[i].packet.opacity<=1);
    }
    // Behind the camera, slightly left: arrow on the left edge, not mirrored right.
    cam.projection[15]=-1;arrow.count=0;
    CHECK(target_ring::AppendOffscreenArrow(cam,-100,0,arrow));
    for(unsigned i=0;i<arrow.count;++i) for(auto p:arrow.quads[i].points) CHECK(p.x<1280*0.2f);
    // Straight behind: points down, but stays above the bottom HUD band.
    arrow.count=0;CHECK(target_ring::AppendOffscreenArrow(cam,0,0,arrow));
    for(unsigned i=0;i<arrow.count;++i) for(auto p:arrow.quads[i].points) CHECK(p.y<=720*0.81f && p.y>720*0.6f);
    arrow.count=target_ring::MaxQuads-2;CHECK(!target_ring::AppendOffscreenArrow(cam,0,0,arrow)); // capacity
    arrow.count=0;CHECK(!target_ring::AppendOffscreenArrow(cam,std::numeric_limits<float>::quiet_NaN(),0,arrow));
    // Legacy health bar (unused) now uses the native, uncullable winding too.
    target_ring::Batch health;health.width=1280;health.height=720;
    CHECK(target_ring::AppendHealth(FlatCamera(0.05f,0.03f),0,0,0.5f,health));
    for(unsigned i=0;i<health.count;++i) CHECK(Cross(health.quads[i],1280,720)>0);

    // Settings: .23 INI migrates without losing anything; new keys get defaults.
    using namespace camera_settings;
    auto path=TempIni("[3dcam]\r\nversion=1\r\nsensitivity=90\r\nradius=6\r\nseal=1\r\ntargeting=1\r\nmapFollow=1\r\n"
        "key0=33\r\nkey1=34\r\nkey2=45\r\nkey3=47\r\nkey4=68\r\nkey5=67\r\nkey6=88\r\n");
    LoadFile(path);auto c=Copy();
    CHECK(status.load()==0 && c.sensitivity==90 && c.radius==6 && c.targeting && c.seal && c.mapFollow);
    CHECK(c.keys[Next]==0x21 && c.keys[Camera]==0x58 && c.keys[View]==0x57 && c.keys[Settings]==Unbound);
    CHECK(c.fov==60 && c.shoulder==0 && !c.invertY && zoomTenths.load()==450 && pitchTenths.load()==220);
    // Round trip of every new field, including a NEGATIVE pitch.
    c.fov=75;c.shoulder=2;c.invertY=true;CHECK(Bind(c,Settings,0x42));Set(c);
    zoomTenths.store(183);pitchTenths.store(-120);CHECK(Save());
    zoomTenths.store(0);pitchTenths.store(0);Set(Config{});
    LoadFile(path);c=Copy();
    CHECK(status.load()==0 && c.fov==75 && c.shoulder==2 && c.invertY && c.keys[Settings]==0x42);
    CHECK(zoomTenths.load()==183 && pitchTenths.load()==-120);
    CHECK(DeleteFileW(path.c_str()));
    // One bad field no longer wipes every setting; a conflicting new default is left unbound.
    path=TempIni("[3dcam]\r\nversion=1\r\nsensitivity=9999\r\nradius=6\r\nfov=200\r\nkey0=87\r\nkey3=47\r\nkey4=17\r\n");
    LoadFile(path);c=Copy();
    CHECK(status.load()==1 && c.sensitivity==100 && c.radius==6 && c.fov==60);
    CHECK(c.keys[Next]==0x57 && c.keys[Center]==0x2f && c.keys[Look]==0x44); // W(0x11) reserved -> default F10
    CHECK(c.keys[View]==Unbound && Valid(c));
    CHECK(DeleteFileW(path.c_str()));
    // Vertically merged glyph runs paint exactly the font pixels, each once.
    {
        const char* sample="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-%: ";
        const unsigned n=unsigned(std::strlen(sample)),W=6*n,H=7;
        target_ring::Batch t;t.width=1280;t.height=720;
        Text(t,0,0,1,sample);
        std::vector<int> painted(W*H,0);
        for(unsigned i=0;i<t.count;++i) {
            const auto& r=t.quads[i].packet.rect;
            for(int y=int(r[1]);y<int(r[3]);++y) for(int x=int(r[0]);x<int(r[2]);++x) ++painted[y*W+x];
        }
        unsigned rowRuns=0;
        for(unsigned ch=0;ch<n;++ch) {
            const auto* rows=Glyph(sample[ch]);
            for(unsigned y=0;y<7;++y) for(unsigned x=0;x<6;++x) {
                const bool on=x<5 && (rows[y]&(16>>x));
                CHECK(painted[y*W+ch*6+x]==(on?1:0));
                if(on && (x==0 || !(rows[y]&(16>>(x-1))))) ++rowRuns;
            }
        }
        CHECK(t.count<rowRuns*3/4);
        std::printf("font quads: %u merged vs %u row runs\n",t.count,rowRuns);
    }
    // Real module path: INI sits next to the DLL (a broken separator once cleared it).
    Load(nullptr);
    CHECK(filename.size()>20 && filename.substr(filename.size()-19)==L"\\3dcam-settings.ini");
    filename.clear();
    // Unbound keys never match input and do not conflict with each other.
    Config u;CHECK(Unbind(u,Next) && Unbind(u,Previous) && Valid(u));
    CHECK(ActionForKey(u,Unbound)==-1 && ActionForKey(u,0x2d)==Elite && KeyName(Unbound)=="NONE");
    CHECK(!Bind(u,ActionCount,0x30) && !Unbind(u,ActionCount));
    // Optional geometry dump for a CPU preview (NOT game pixels).
    if(const char* dump=std::getenv("D2CAM_HUD_DUMP")) {
        target_ring::Batch scene;
        auto sceneCam=FlatCamera(0.035f,0.045f);
        CHECK(target_ring::Build(sceneCam,-6,-4,1920,1080,scene,4200));
        sceneCam.projection[15]=-1;
        CHECK(target_ring::AppendOffscreenArrow(sceneCam,-60,-10,scene,4200));
        hud_toast::Show("VIEW: CLOSE",1);CHECK(hud_toast::Draw(scene,400));
        FILE* f=nullptr;CHECK(fopen_s(&f,dump,"wb")==0 && f);
        for(unsigned i=0;i<scene.count;++i) {
            const auto& q=scene.quads[i];
            for(unsigned k:{0u,1u,3u,2u}) std::fprintf(f,"%.2f %.2f ",q.points[k].x,q.points[k].y);
            std::fprintf(f,"%.3f %.3f %.3f %.3f\n",q.packet.color[0],q.packet.color[1],q.packet.color[2],q.packet.opacity);
        }
        std::fclose(f);
    }
    std::puts("camera motion / presets / shoulder / toasts / edge arrow / 4K seal / INI migration PASS; not gameplay proof");
}
