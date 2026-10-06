#include "automap_math.h"
#include "automap_packets.h"
#include <cstdio>
#include <limits>
#include <cstring>
int main() {
    using namespace third_person::automap;
    int failures=0;
    const auto check=[&](bool ok,const char* label) { if (!ok) { fprintf(stderr,"FAIL: %s\n",label); ++failures; } };
    const auto near=[](float a,float b) { return std::abs(a-b)<0.002f; };
    const Point player{913,507},center{960,540};
    for (int i=0;i<32;++i) {
        const float angle=float(i)*3.14159265359f/16;
        const float fx=std::sin(angle),fz=-std::cos(angle);
        for (float scale : {0.5f,1.0f,2.5f}) {
            Affine map;
            check(HeadingUp(fx,fz,scale,scale,player,center,map),"finite native-isometric heading");
            const auto p=map.apply(player);
            check(near(p.x,center.x)&&near(p.y,center.y),"player anchored at viewport center");
            const Point forward{player.x+0.8f*scale*(fx-fz)*100,player.y+0.4f*scale*(fx+fz)*100};
            const auto f=map.apply(forward);
            check(near(f.x,center.x)&&f.y<center.y,"camera forward is always map up");
            check(near(map.a*map.d-map.b*map.c,1),"no mirrored map or scale/pitch distortion");
            const auto label=UprightAt(map,{player.x+30,player.y-50});
            const auto a=label.apply({100,100}),b=label.apply({130,100});
            check(near(b.x-a.x,30)&&near(b.y-a.y,0),"text/glyphs stay upright as a group");
            Rect cover;
            check(InverseBounds(map,{0,0,1920,1080},cover),"inverse culling bounds finite");
            for (Point corner : {Point{0,0},Point{1920,0},Point{0,1080},Point{1920,1080}}) {
                // Invert rotation directly; each screen corner must be in producer bounds.
                const float x=corner.x-map.tx,y=corner.y-map.ty;
                const Point original{map.d*x-map.b*y,-map.c*x+map.a*y};
                check(original.x>=cover.x-0.01f&&original.x<=cover.x+cover.w+0.01f&&
                      original.y>=cover.y-0.01f&&original.y<=cover.y+cover.h+0.01f,"rotated viewport has no culling corner holes");
            }
        }
    }
    Affine map;
    check(HeadingUp(0.6f,-0.8f,2,0.75f,player,center,map),"nonuniform native tileset scale");
    const auto unequal=map.apply({player.x+0.8f*2*1.4f*100,player.y+0.4f*0.75f*(-0.2f)*100});
    check(near(unequal.x,center.x)&&unequal.y<center.y,"anisotropic native map still aligns heading");
    check(!HeadingUp(0,0,1,1,player,center,map),"degenerate heading fails closed");
    check(!HeadingUp(1,0,0,1,player,center,map),"invalid scale fails closed");
    const float nan=std::numeric_limits<float>::quiet_NaN();
    check(!HeadingUp(nan,0,1,1,player,center,map),"NaN heading rejected");
    check(!HeadingUp(1,0,1,1,{nan,0},center,map),"NaN pivot rejected");
    Vertex quad[4]={{860,640,1,0,0},{840,640,1,1,0},{860,670,1,0,1},{840,670,1,1,1}};
    Vertex original[4]; memcpy(original,quad,sizeof quad);
    check(TransformQuad(quad,{},1920,1080)&&!memcmp(quad,original,sizeof quad),"identity upload exactly preserves vertices");
    const Affine turn{0,-1,1,0,300,0};
    check(TransformQuad(quad,turn,1920,1080),"rotate full quad, not only sprite center");
    for (int i=0;i<4;++i) {
        const Point p{960-original[i].x,original[i].y-540};
        const auto want=turn.apply(p);
        check(near(960-quad[i].x,want.x)&&near(quad[i].y-540,want.y),"native vertex coordinate convention");
        check(quad[i].z==original[i].z&&quad[i].u==original[i].u&&quad[i].v==original[i].v,"UV and depth unchanged");
    }
    Vertex bad[4]; memcpy(bad,original,sizeof bad); bad[3].x=nan;
    Vertex before[4]; memcpy(before,bad,sizeof bad);
    check(!TransformQuad(bad,turn,1920,1080)&&!memcmp(bad,before,sizeof bad),"quad transform atomic on invalid input");
    check(!TransformQuad(quad,turn,0,1080),"invalid viewport rejected");
    FramePackets frame;
    unsigned char bytes[FingerprintBytes]{};
    const auto fingerprint=Fingerprint(bytes);
    check(frame.add(12,fingerprint,turn),"record exact command index");
    check(frame.find(12,fingerprint)!=nullptr,"matching packet only");
    check(!frame.find(13,fingerprint),"neighboring HUD packet untouched");
    bytes[40]=1;
    check(!frame.find(12,Fingerprint(bytes)),"recycled queue offset with different payload rejected");
    check(!frame.add(12,fingerprint,turn)&&!frame.valid&&!frame.count,"non-monotonic tags reject whole map frame");
    frame.valid=true;
    for (uint32_t i=0;i<MaxPackets;++i) check(frame.add(i,fingerprint,turn),"bounded capacity accepts valid tags");
    check(!frame.add(uint32_t(MaxPackets),fingerprint,turn)&&!frame.count,"overflow is passthrough not partial map");
    check(PacketSize(0)==96&&PacketSize(3)==88&&PacketSize(4)==128&&PacketSize(5)==104&&PacketSize(6)==128,"native quad packet strides");
    check(!PacketSize(10)&&!IsQuad(7)&&!IsQuad(8)&&PacketSize(7)==68,"debug commands are not transforms or quads");
    // Mixed old/new sidecars must not produce a partly rotated map.
    FramePackets batch;
    unsigned char packets[192]{};
    check(batch.add(0,Fingerprint(packets),turn)&&batch.add(24,Fingerprint(packets+96),turn),"two map packets");
    const auto read=[](uintptr_t p,void* out,size_t n){memcpy(out,(void*)p,n);return true;};
    check(batch.matches(read,(uintptr_t)packets,48),"whole batch verified");
    packets[96+40]=1;check(!batch.matches(read,(uintptr_t)packets,48),"one changed packet rejects entire batch");
    packets[96+40]=0;check(!batch.matches(read,(uintptr_t)packets,44),"truncated batch rejected before reads");
    // Remove only per-packet clipping, never rect, UV, style or textures.
    for(uint32_t kind : {0u,3u,4u,5u,6u,9u}) {
        unsigned char packet[128],saved[128];memset(packet,0x5a,sizeof packet);memcpy(packet,&kind,4);
        memcpy(saved,packet,sizeof packet);check(UsePassClip(packet,PacketSize(kind)),"known map quad clip");
        check(!memcmp(packet,saved,0x34)&&!memcmp(packet+0x44,saved+0x44,sizeof packet-0x44),"all non-clip bytes preserved");
        for(unsigned j=0x34;j<0x44;++j)check(packet[j]==0,"inherit pass viewport");
    }
    fprintf(stdout,"automap math/packet isolation: %d failures\n",failures);
    return failures?1:0;
}
