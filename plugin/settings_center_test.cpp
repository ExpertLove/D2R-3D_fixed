#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "settings_ui.h"
#include "center_target.h"
#include "target_key.h"
#include <cstdlib>
#include <limits>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
int main() {
    using namespace camera_settings;
    Config c;CHECK(Valid(c));CHECK(c.keys[Center]==0x2f);
    CHECK(!Bind(c,Center,c.keys[Next]));CHECK(!Bind(c,Center,0x11)); // duplicate / W
    CHECK(!Bind(c,Center,0x1c));CHECK(!Bind(c,Center,0xaf)); // enter / extended
    CHECK(Bind(c,Center,0x30));CHECK(Valid(c));
    c.radius=19;CHECK(!Valid(c));c={};c.sensitivity=0;CHECK(!Valid(c));
    click_target::SelectionKey physical;
    physical.initialize(false);auto e=physical.event(true,true);CHECK(e.consume && e.request);
    CHECK(!physical.event(true,true).request);CHECK(physical.event(false,false).consume);
    CHECK(!physical.event(false,true).request); // paired release even after modal switch
    // Geometric center, eligibility, bounded radius, tie breaking and unknown inputs.
    third_person::TargetFrame f{};f.complete=true;f.session=1;f.count=3;
    using third_person::Relation;using third_person::Sight;
    f.units[0]={{1,7,1},19,0,0,Relation::Hostile,Sight::Clear};
    f.units[1]={{1,2,1},19,0,0,Relation::Hostile,Sight::Clear};
    f.units[2]={{1,3,1},19,1,0,Relation::Hostile,Sight::Clear};
    target_ring::Camera cam{};cam.view[0]=0.05f;cam.view[9]=0.03f;cam.view[15]=1;
    for(unsigned i=0;i<4;++i)cam.projection[i*5]=1;
    CHECK(click_target::CenterTarget(f,cam,1280,720).id==2);
    f.units[1].relation=Relation::OwnedPet;CHECK(click_target::CenterTarget(f,cam,1280,720).id==7);
    f.units[0].sight=Sight::Blocked;CHECK(click_target::CenterTarget(f,cam,1280,720).id==3);
    f.units[2].x=3;CHECK(!click_target::CenterTarget(f,cam,1280,720).found);
    f.units[2].x=0;f.units[2].key.session=2;CHECK(!click_target::CenterTarget(f,cam,1280,720).found);
    f.units[2].key.session=1;cam.projection[15]=-1;CHECK(!click_target::CenterTarget(f,cam,1280,720).found);
    cam.projection[15]=1;f.units[2].x=std::numeric_limits<float>::quiet_NaN();CHECK(!click_target::CenterTarget(f,cam,1280,720).found);
    f.complete=false;CHECK(!click_target::CenterTarget(f,cam,1280,720).complete);
    f.complete=true;CHECK(!click_target::CenterTarget(f,cam,1280,720,0.3f).complete);
    CHECK(!click_target::CenterTarget(f,cam,0,0).complete);
    // Entire modal UI fits the bounded native batch, with room for the full seal.
    // Native f8ce10 emits screen-space right/top first (x reflected on upload).
    // Preview rect bounds alone cannot detect reversed/cullable winding.
    target_ring::Batch winding;winding.width=1280;winding.height=720;
    camera_settings::Rect(winding,100,200,30,40,1,1,1);
    const auto& q=winding.quads[0];
    CHECK(q.points[0].x==130 && q.points[0].y==200);
    CHECK(q.points[1].x==100 && q.points[1].y==200);
    CHECK(q.points[2].x==130 && q.points[2].y==240);
    CHECK(q.points[3].x==100 && q.points[3].y==240);
    target_ring::Vertex vertices[4]{};
    CHECK(target_ring::Apply(q,1280,720,vertices));
    const float cross=(vertices[1].x-vertices[0].x)*(vertices[2].y-vertices[0].y)-(vertices[1].y-vertices[0].y)*(vertices[2].x-vertices[0].x);
    CHECK(cross>0);
    Set(Config{});open=true;
    for(const auto size:{std::pair<unsigned,unsigned>{640,360},{1280,720},{1920,1080},{3440,1440}}) {
        target_ring::Batch b;b.width=size.first;b.height=size.second;b.count=target_ring::SealQuads;
        const unsigned start=b.count;
        Draw(b,true,true);CHECK(b.count<target_ring::MaxQuads-100);CHECK(b.count>start+1000);
        for(unsigned i=start;i<b.count;++i) for(auto p:b.quads[i].points)
            CHECK(std::isfinite(p.x) && std::isfinite(p.y) && p.x>=0 && p.y>=0 && p.x<=b.width && p.y<=b.height);
        if(b.width==1280) if(const char* path=std::getenv("D2CAM_UI_PREVIEW")) {
            std::vector<unsigned char> pixels(b.width*b.height*3,35);
            for(unsigned i=start;i<b.count;++i) {
                const auto& q=b.quads[i];
                for(int y=int(q.packet.rect[1]);y<int(q.packet.rect[3]);++y)
                    for(int x=int(q.packet.rect[0]);x<int(q.packet.rect[2]);++x)
                        for(unsigned channel=0;channel<3;++channel) {
                            auto& value=pixels[(y*b.width+x)*3+channel];
                            value=static_cast<unsigned char>(value*(1-q.packet.opacity)+q.packet.color[channel]*255*q.packet.opacity);
                        }
            }
            FILE* file=nullptr;CHECK(fopen_s(&file,path,"wb")==0 && file);
            std::fprintf(file,"P6\n%u %u\n255\n",b.width,b.height);CHECK(std::fwrite(pixels.data(),1,pixels.size(),file)==pixels.size());CHECK(std::fclose(file)==0);
        }
        Layout l(b.width,b.height);
        for(int i=0;i<ActionCount;++i) CHECK(l.hit(l.x+340*l.scale,l.y+(65+i*30)*l.scale)==i);
        CHECK(l.hit(l.x+500*l.scale,l.y+20*l.scale)==100);
        std::printf("UI %ux%u: %u quads\n",b.width,b.height,b.count-start);
    }
    Close();target_ring::Batch gear;gear.width=1280;gear.height=720;Draw(gear,false,false);CHECK(gear.count>100 && gear.count<=GearSize*GearSize && waiting==-1);
    CHECK(GearPixels[0]!=0); // original cropped red face, no enclosing procedural frame
    // Settings persistence uses only a disposable temp file, never game saves.
    wchar_t dir[MAX_PATH],path[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,dir));CHECK(GetTempFileNameW(dir,L"cam",0,path));
    filename=path;Config saved;CHECK(Bind(saved,Center,0x30));saved.sensitivity=145;saved.seal=false;Set(saved);CHECK(Save());
    CHECK(GetPrivateProfileIntW(L"3dcam",L"key3",0,path)==0x30);
    CHECK(GetPrivateProfileIntW(L"3dcam",L"sensitivity",0,path)==145);
    CHECK(GetPrivateProfileIntW(L"3dcam",L"seal",1,path)==0);
    CHECK(DeleteFileW(path));filename.clear();CHECK(!Save());
    std::puts("center selection / bindings / modal layout / atomic config save PASS; not gameplay proof");
}
