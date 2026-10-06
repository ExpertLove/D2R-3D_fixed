#include "nearest_target.h"
#include "target_ring.h"
#include "marker_frame.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
using namespace third_person;
int main() {
    TargetFrame f{}; f.session=1;f.complete=true;f.count=3;
    f.units[0]={{1,7,1},19,3,4,Relation::Hostile,Sight::Clear};
    f.units[1]={{1,9,1},19,2,0,Relation::Hostile,Sight::Clear};
    f.units[2]={{1,2,1},19,-2,0,Relation::Hostile,Sight::Clear};
    CHECK(click_target::Nearest(f).id==2); // tie: stable lower ID, not random list order
    f.units[2].relation=Relation::OwnedPet;CHECK(click_target::Nearest(f).id==9);
    f.units[1].sight=Sight::Blocked;CHECK(click_target::Nearest(f).id==7);
    f.units[0].relation=Relation::Friendly;CHECK(!click_target::Nearest(f).found);
    f.units[0].relation=Relation::Hostile;f.units[0].sight=Sight::Unknown;CHECK(!click_target::Nearest(f).found);
    f.units[0].sight=Sight::Clear; CHECK(click_target::Nearest(f,5).id==7); CHECK(!click_target::Nearest(f,4.99f).found);
    f.units[0].x=std::numeric_limits<float>::quiet_NaN(); CHECK(!click_target::Nearest(f).found);
    f.units[0].x=3;f.units[0].key.session=2;CHECK(!click_target::Nearest(f).found);
    f.units[0].key.session=1;f.units[0].key.id=UINT32_MAX;CHECK(!click_target::Nearest(f).found);
    CHECK(!click_target::Nearest(f,-1).complete);
    f.count=129;CHECK(!click_target::Nearest(f).complete);
    f.count=3;f.complete=false;CHECK(!click_target::Nearest(f).complete);

    // Cycle: nearest first, stable order, wrap, no A-B ping-pong, prune/append.
    {
        TargetFrame g{}; g.session=1;g.complete=true;g.count=3;
        g.units[0]={{1,10,1},19,5,0,Relation::Hostile,Sight::Clear};
        g.units[1]={{1,20,1},19,2,0,Relation::Hostile,Sight::Clear};
        g.units[2]={{1,30,1},19,9,0,Relation::Hostile,Sight::Clear};
        click_target::TargetCycle cy;
        CHECK(cy.next(g).id==20); CHECK(cy.next(g).id==10); CHECK(cy.next(g).id==30);
        CHECK(cy.next(g).id==20); // wrap to first
        g.units[1].x=100;g.units[0].x=1; // movement must not reorder cycle (and 20 is out of range)
        g.units[1].x=6;
        CHECK(cy.next(g).id==10); CHECK(cy.next(g).id==30); CHECK(cy.next(g).id==20);
        g.units[1].relation=Relation::Hostile; g.units[2].sight=Sight::Blocked; // 30 gone
        CHECK(cy.next(g).id==10); CHECK(cy.next(g).id==20); CHECK(cy.next(g).id==10);
        g.count=4; g.units[3]={{1,40,1},19,1,0,Relation::Hostile,Sight::Clear}; // newcomer appended at the end
        CHECK(cy.next(g).id==40); CHECK(cy.next(g).id==20); CHECK(cy.next(g).id==10);
        g.units[0].relation=Relation::Friendly; // current-next removal keeps successor
        CHECK(cy.next(g).id==40);
        g.session=2;for(unsigned i=0;i<4;++i)g.units[i].key.session=2; // session reset -> nearest
        CHECK(cy.next(g).id==40);
        g.complete=false;CHECK(!cy.next(g).complete);
        g.complete=true;g.count=0;CHECK(!cy.next(g).found);
        g.count=1;g.units[0]={{2,7,1},19,3,0,Relation::Hostile,Sight::Clear};
        CHECK(cy.next(g).id==7); CHECK(cy.next(g).id==7); // single enemy
        cy.reset(); CHECK(cy.size()==0);
        g.session=9;g.count=3;
        for(unsigned i=0;i<3;++i) g.units[i]={{9,10+i,1},19,float(i+1),0,Relation::Hostile,Sight::Clear};
        CHECK(cy.next(g,80,-1).id==10); // first G also nearest
        CHECK(cy.next(g,80,-1,10).id==12); // reverse wrap
        CHECK(cy.next(g,80,-1,12).id==11);
        CHECK(cy.next(g,80,1,11).id==12); // change direction immediately
        CHECK(cy.next(g,80,1,10).id==11); // mouse anchor at 10
        CHECK(cy.next(g,80,-1,10).id==12);
        std::swap(g.units[0],g.units[2]);
        CHECK(cy.next(g,80,1,11).id==12); // snapshot order irrelevant
    }

    // Camera cache must refresh on native callbacks without a control-gen change.
    {
        target_ring::FrameCache cache; target_ring::Camera live{},copy{};
        live.view[0]=live.view[5]=live.view[10]=live.view[15]=1;
        live.projection[0]=live.projection[5]=1;live.projection[11]=-1;
        CHECK(cache.publish(1,live,100,true));CHECK(cache.copy(1,110,copy));
        CHECK(!cache.copy(2,110,copy));CHECK(!cache.copy(1,99,copy));CHECK(!cache.copy(1,251,copy));
        for(unsigned t=120;t<600000;t+=16) {
            live.view[12]=float(t); live.projection[8]=float(t%100)*0.001f;
            CHECK(cache.publish(1,live,t,true));CHECK(cache.copy(1,t,copy));
            CHECK(copy.view[12]==live.view[12] && copy.projection[8]==live.projection[8]);
        }
        CHECK(!cache.publish(1,live,600000,false));CHECK(!cache.copy(1,600000,copy));
        CHECK(cache.publish(2,live,600001,true));cache.reset();CHECK(!cache.copy(2,600001,copy));
        live.projection[11]=0;live.projection[15]=1;CHECK(!cache.publish(2,live,600002,true));
    }
    target_ring::Camera cam{};cam.view[0]=0.05f;cam.view[9]=0.03f;cam.view[15]=1;
    for(int i=0;i<4;++i)cam.projection[i*5]=1;
    target_ring::Batch ring;
    CHECK(target_ring::Build(cam,0,0,1280,720,ring)); CHECK(ring.count==target_ring::SealQuads);
    for(unsigned i=0;i<ring.count;++i) {
        const auto& q=ring.quads[i];
        CHECK(q.packet.kind==0 && q.packet.texture==0 && q.packet.color[0]==1 && q.packet.color[2]<0.2f);
        CHECK(q.packet.color[1]<0.7f && q.packet.opacity>0 && q.packet.opacity<=1);
        CHECK(q.packet.rect[0]<=q.packet.rect[2] && q.packet.rect[1]<=q.packet.rect[3]);
        target_ring::Vertex vertices[4]={{0,0,1,0,0},{0,0,1,1,0},{0,0,1,0,1},{0,0,1,1,1}},before[4];
        std::memcpy(before,vertices,sizeof vertices);
        CHECK(target_ring::Apply(q,1280,720,vertices));
        for(unsigned j=0;j<4;++j) {
            CHECK(std::abs((640-vertices[j].x)-q.points[j].x)<0.001f);
            CHECK(std::abs((vertices[j].y-360)-q.points[j].y)<0.001f);
            CHECK(vertices[j].z==before[j].z && vertices[j].u==before[j].u && vertices[j].v==before[j].v);
        }
    }
    // Closed annulus: each segment's end matches the next start, all 3 bands.
    for(unsigned band=0;band<3;++band) for(unsigned i=0;i<48;++i) { // glow, outer, inner rings
        const auto& a=ring.quads[band*48+i];const auto& b=ring.quads[band*48+(i+1)%48];
        for(unsigned e=0;e<2;++e) CHECK(std::hypot(a.points[2+e].x-b.points[e].x,a.points[2+e].y-b.points[e].y)<0.001f);
    }
    // Animation: deterministic phase, actually moves, opacity bounded, no opaque fill.
    target_ring::Batch a,b,c;
    CHECK(target_ring::Build(cam,0,0,1280,720,a,5000)&&target_ring::Build(cam,0,0,1280,720,b,5000)&&target_ring::Build(cam,0,0,1280,720,c,7300));
    bool moved=false;
    for(unsigned i=0;i<a.count;++i) {
        CHECK(a.quads[i].points[0].x==b.quads[i].points[0].x && a.quads[i].packet.opacity==b.quads[i].packet.opacity);
        CHECK(a.quads[i].packet.opacity<=1.0f);
        if(i>=144 && a.quads[i].points[0].x!=c.quads[i].points[0].x) moved=true;
    }
    CHECK(moved);
    const auto sealCount=a.count;
    CHECK(target_ring::AppendHealth(cam,0,0,0.5f,a)); CHECK(a.count==sealCount+3);
    const auto& bg=a.quads[sealCount+1];const auto& fill=a.quads[sealCount+2];
    CHECK(std::abs((fill.points[1].x-fill.points[0].x)*2-(bg.points[1].x-bg.points[0].x))<0.001f);
    a.count=target_ring::MaxQuads-2;
    CHECK(!target_ring::AppendHealth(cam,0,0,0.5f,a)); // capacity guard (batch also holds settings UI)
    b.count=0; CHECK(target_ring::AppendHealth(cam,0,0,0,b) && b.count==2);
    CHECK(!target_ring::AppendHealth(cam,0,0,-1,b));
    CHECK(!target_ring::AppendHealth(cam,0,0,std::numeric_limits<float>::quiet_NaN(),b));
    for(unsigned i=0;i<48;++i) CHECK(a.quads[i].packet.opacity<=0.15f); // glow is faint
    CHECK(!target_ring::Build(cam,1000,0,1280,720,ring));CHECK(ring.count==0);
    CHECK(!target_ring::Build(cam,0,0,0,720,ring));
    cam.projection[15]=-1;CHECK(!target_ring::Build(cam,0,0,1280,720,ring)); // behind camera
    cam.projection[15]=1;cam.groundY=std::numeric_limits<float>::quiet_NaN();CHECK(!target_ring::Build(cam,0,0,1280,720,ring));
    std::puts("nearest hostile + red ring geometry PASS; not native pixel proof");
}
