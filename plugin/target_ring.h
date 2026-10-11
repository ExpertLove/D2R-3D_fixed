#pragma once
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "automap_math.h"

namespace target_ring {
using Point=third_person::automap::Point;
using Vertex=third_person::automap::Vertex;
constexpr unsigned Segments=48, SealQuads=Segments*3+12*2+8*2+24, MaxQuads=4096;
struct Camera { float view[16]{},projection[16]{}; float groundY=0; };
// Native untextured UI packet0, traced from657820 producer / f8b8d0 consumer.
struct SolidPacket {
    std::uint32_t kind=0;
    float rect[4]{};
    float uv[4]{0,0,1,1};
    float color[4]{1,0.16f,0.03f,1};
    std::int32_t clip[4]{};
    std::uint32_t pad44=0;
    std::uintptr_t texture=0;
    float opacity=1;
    std::uint32_t pad54=0;
    unsigned char flags[8]{};
};
static_assert(sizeof(SolidPacket)==0x60 && offsetof(SolidPacket,color)==0x24 &&
    offsetof(SolidPacket,texture)==0x48 && offsetof(SolidPacket,opacity)==0x50);
struct Quad { Point points[4]; SolidPacket packet; };
struct Batch { std::vector<Quad> quads=std::vector<Quad>(MaxQuads); unsigned count=0,width=0,height=0; };
inline bool Project(const Camera& camera,float x,float y,float z,unsigned width,unsigned height,Point& out) {
    const float world[4]={x,y,z,1}; float view[4]{},clip[4]{};
    for(unsigned j=0;j<4;++j) for(unsigned i=0;i<4;++i) view[j]+=world[i]*camera.view[i*4+j];
    for(unsigned j=0;j<4;++j) for(unsigned i=0;i<4;++i) clip[j]+=view[i]*camera.projection[i*4+j];
    if(!std::isfinite(clip[3]) || clip[3]<=0.02f) return false;
    out={(clip[0]/clip[3]+1)*float(width)*0.5f,(1-clip[1]/clip[3])*float(height)*0.5f};
    return std::isfinite(out.x) && std::isfinite(out.y) &&
        std::abs(out.x)<float(width)*4 && std::abs(out.y)<float(height)*4;
}
// Magic seal on the ground plane (screen-projected, NOT a depth-tested decal;
// groundY = camera look-at Y = player's feet, slopes may offset). Pure function of
// (position, camera, timeMs): same time -> same phase. Never an opaque fill.
// Layout: [0,48) glow band, [48,96) outer ring, [96,144) inner ring (each closed),
// then rotating rune arcs and radial ticks.
inline bool Build(const Camera& camera,float x,float z,unsigned width,unsigned height,Batch& out,std::uint32_t timeMs=0) {
    out.count=0; out.width=width; out.height=height;
    if(width<640 || height<360 || width>16384 || height>16384 ||
        !std::isfinite(x) || !std::isfinite(z) || !std::isfinite(camera.groundY)) return false;
    const float y=camera.groundY+0.05f;
    Point center;
    if(!Project(camera,x,y,z,width,height,center) ||
        center.x<0 || center.x>width || center.y<0 || center.y>height*0.86f) return false;
    constexpr float pi=3.14159265358979323846f,tau=2*pi;
    // Near-plane blow-up guard. Scaled with resolution: a fixed 350 px hid the
    // whole seal at 1440p/4K and at close zoom, where it is legitimately larger.
    const float maxSpan=std::max(350.0f,0.42f*float(height));
    const float t=float(timeMs%1000000u)*0.001f;
    const float pulse=0.5f+0.5f*std::sin(tau*t/1.8f);          // 0..1, 1.8 s period
    const float breathe=0.88f+0.12f*pulse;
    const float spin=tau*t/24.0f;                               // outer layer: 24 s / turn
    const float spinIn=-tau*t/16.0f;                            // inner layer: counter-rotates
    struct Rgb { float r,g,b; };
    const auto emit=[&](float r0,float r1,float a0,float a1,Rgb c,float alpha)->bool {
        Quad q{}; q.packet.opacity=std::min(1.0f,std::max(0.0f,alpha));
        q.packet.color[0]=c.r;q.packet.color[1]=c.g;q.packet.color[2]=c.b;
        const float radii[2]={r1,r0}; const float angles[2]={a0,a1}; // edge0 outer, edge1 inner
        for(unsigned end=0;end<2;++end) for(unsigned edge=0;edge<2;++edge)
            if(!Project(camera,x+std::cos(angles[end])*radii[edge],y,z+std::sin(angles[end])*radii[edge],
                width,height,q.points[end*2+edge])) return false;
        float left=q.points[0].x,right=left,top=q.points[0].y,bottom=top;
        for(auto p:q.points) {
            if(std::hypot(p.x-center.x,p.y-center.y)>maxSpan) return false;
            left=std::min(left,p.x);right=std::max(right,p.x);
            top=std::min(top,p.y);bottom=std::max(bottom,p.y);
        }
        if(bottom>height*0.88f) return false; // don't paint across the bottom HUD
        q.packet.rect[0]=left;q.packet.rect[1]=top;q.packet.rect[2]=right;q.packet.rect[3]=bottom;
        if(out.count>=MaxQuads) return false;
        out.quads[out.count++]=q;
        return true;
    };
    const Rgb glow{1.0f,0.30f,0.04f},ring{1.0f,0.16f,0.03f},rune{1.0f,0.62f,0.16f};
    const float step=tau/float(Segments);
    const auto closed=[&](float r0,float r1,Rgb c,float alpha)->bool {
        for(unsigned i=0;i<Segments;++i) if(!emit(r0,r1,step*float(i),step*float(i+1),c,alpha)) return false;
        return true;
    };
    constexpr float R=2.3f;
    bool ok=closed(R-0.30f,R+0.22f,glow,0.10f*breathe)         // soft glow halo
        && closed(R-0.05f,R+0.05f,ring,0.95f*breathe)          // outer ring
        && closed(1.55f-0.035f,1.55f+0.035f,ring,0.80f*breathe); // inner ring
    // 12 rune arcs (2 quads each) between the rings, slow clockwise rotation, pulsing.
    for(unsigned k=0;ok && k<12;++k) {
        const float a0=spin+tau*float(k)/12.0f,len=tau/12.0f*0.42f;
        const float alpha=(0.55f+0.40f*pulse)*(k%3==0?1.0f:0.8f);
        ok=emit(1.80f,2.00f,a0,a0+len*0.5f,rune,alpha) && emit(1.80f,2.00f,a0+len*0.5f,a0+len,rune,alpha);
    }
    // 8 inner arcs, counter-rotating, with a phase-offset pulse.
    for(unsigned k=0;ok && k<8;++k) {
        const float a0=spinIn+tau*float(k)/8.0f,len=tau/8.0f*0.30f;
        const float alpha=0.5f+0.4f*(1.0f-pulse);
        ok=emit(1.22f,1.38f,a0,a0+len*0.5f,rune,alpha) && emit(1.22f,1.38f,a0+len*0.5f,a0+len,rune,alpha);
    }
    // 24 radial tick strokes between inner ring and outer ring zone (short dashes).
    for(unsigned k=0;ok && k<24;++k) {
        const float a0=spin*0.5f+tau*float(k)/24.0f,w=0.018f;
        ok=emit(2.08f,2.20f,a0-w,a0+w,rune,0.75f*breathe);
    }
    if(!ok) { out.count=0; return false; }
    return out.count>0;
}
// Screen-space bar above an approximate head anchor. The 8-unit height is a
// presentation offset, NOT a native skeleton/monster-height field. No unit reads.
inline bool AppendHealth(const Camera& camera,float x,float z,float fraction,Batch& out) {
    if(!std::isfinite(fraction) || fraction<0 || fraction>1 || out.count>MaxQuads-3 ||
       out.width<640 || out.height<360 || out.width>16384 || out.height>16384) return false;
    Point head;
    if(!Project(camera,x,camera.groundY+8.0f,z,out.width,out.height,head)) return false;
    const float scale=std::clamp(float(out.height)/1080.0f,0.75f,2.0f);
    const float width=72*scale,height=7*scale,border=1.5f*scale;
    const float left=head.x-width/2,top=head.y-12*scale;
    if(left<4 || left+width>out.width-4 || top<out.height*0.10f || top+height>out.height*0.85f) return false;
    const auto rect=[&](float l,float t,float r,float b,float red,float green,float blue) {
        Quad q{};
        // Native f8ce10 order (right/top first); left-first is back-face culled.
        q.points[0]={r,t};q.points[1]={l,t};q.points[2]={r,b};q.points[3]={l,b};
        q.packet.rect[0]=l;q.packet.rect[1]=t;q.packet.rect[2]=r;q.packet.rect[3]=b;
        q.packet.color[0]=red;q.packet.color[1]=green;q.packet.color[2]=blue;
        q.packet.opacity=0.95f;
        out.quads[out.count++]=q;
    };
    rect(left-border,top-border,left+width+border,top+height+border,0.60f,0.43f,0.23f);
    rect(left,top,left+width,top+height,0.07f,0.025f,0.02f);
    if(fraction>0) rect(left,top,left+width*fraction,top+height,0.85f,0.055f,0.025f);
    return true;
}
// Thick screen-space segment with the verified native winding: corners are a
// rotated copy of right/top, left/top, right/bottom, left/bottom.
inline bool AppendBar(Batch& out,Point from,Point to,float thickness,float r,float g,float b,float alpha) {
    if(out.count>=MaxQuads || !(thickness>0)) return false;
    const float dx=to.x-from.x,dy=to.y-from.y,length=std::hypot(dx,dy);
    if(!std::isfinite(length) || length<1e-3f) return false;
    const float ax=dx/length,ay=dy/length,nx=-ay*thickness*0.5f,ny=ax*thickness*0.5f;
    Quad q{};
    q.points[0]={to.x-nx,to.y-ny};   // "right/top"
    q.points[1]={from.x-nx,from.y-ny};
    q.points[2]={to.x+nx,to.y+ny};
    q.points[3]={from.x+nx,from.y+ny};
    float left=q.points[0].x,right=left,top=q.points[0].y,bottom=top;
    for(auto p:q.points) {
        if(!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
        left=std::min(left,p.x);right=std::max(right,p.x);top=std::min(top,p.y);bottom=std::max(bottom,p.y);
    }
    q.packet.rect[0]=left;q.packet.rect[1]=top;q.packet.rect[2]=right;q.packet.rect[3]=bottom;
    q.packet.color[0]=r;q.packet.color[1]=g;q.packet.color[2]=b;
    q.packet.opacity=std::clamp(alpha,0.0f,1.0f);
    out.quads[out.count++]=q;
    return true;
}
// Edge indicator for a selected target whose seal cannot be on screen (off
// screen or behind the camera). Pure function; draws nothing when the target
// centre is visible. Arrow stays above the bottom HUD band.
inline bool AppendOffscreenArrow(const Camera& camera,float x,float z,Batch& out,std::uint32_t timeMs=0) {
    const unsigned width=out.width,height=out.height;
    if(width<640 || height<360 || width>16384 || height>16384 || out.count>MaxQuads-6 ||
       !std::isfinite(x) || !std::isfinite(z) || !std::isfinite(camera.groundY)) return false;
    const float world[4]={x,camera.groundY+2.0f,z,1}; float view[4]{},clip[4]{};
    for(unsigned j=0;j<4;++j) for(unsigned i=0;i<4;++i) view[j]+=world[i]*camera.view[i*4+j];
    for(unsigned j=0;j<4;++j) for(unsigned i=0;i<4;++i) clip[j]+=view[i]*camera.projection[i*4+j];
    for(float v:clip) if(!std::isfinite(v)) return false;
    const float w=float(width),h=float(height);
    if(clip[3]>0.02f) {
        const float sx=(clip[0]/clip[3]+1)*w*0.5f,sy=(1-clip[1]/clip[3])*h*0.5f;
        if(sx>=0 && sx<=w && sy>=0 && sy<=h*0.86f) return false; // seal territory
    }
    // Sign-preserving direction: behind the camera keep raw clip x/y (no flip).
    const float scale=std::max(std::abs(clip[3]),1e-4f);
    float dx=clip[0]/scale*w*0.5f,dy=-clip[1]/scale*h*0.5f;
    float length=std::hypot(dx,dy);
    if(!std::isfinite(length) || length<1e-4f) { dx=0;dy=1;length=1; } // straight behind: point down
    dx/=length;dy/=length;
    const float cx=w*0.5f,cy=h*0.5f;
    const float left=w*0.06f,right=w*0.94f,top=h*0.10f,bottom=h*0.80f;
    float t=1e9f;
    if(dx>1e-6f) t=std::min(t,(right-cx)/dx); else if(dx<-1e-6f) t=std::min(t,(left-cx)/dx);
    if(dy>1e-6f) t=std::min(t,(bottom-cy)/dy); else if(dy<-1e-6f) t=std::min(t,(top-cy)/dy);
    if(!std::isfinite(t) || t<=0 || t>1e8f) return false;
    const Point tip{cx+dx*t,cy+dy*t};
    const float size=0.032f*h,thick=std::max(2.0f,0.0075f*h);
    const float pulse=0.5f+0.5f*std::sin(6.2831853f*float(timeMs%1000000u)*0.001f/0.9f);
    // Chevron ">" pointing along (dx,dy): two arms swept back by +-40 degrees.
    const float c=std::cos(0.698f),s=std::sin(0.698f);
    const Point armA{-dx*c+dy*s,-dx*s-dy*c},armB{-dx*c-dy*s,dx*s-dy*c};
    const unsigned start=out.count;
    bool ok=true;
    for(int layer=0;ok && layer<2;++layer) {
        const float back=layer*size*0.7f, alpha=(layer?0.45f:0.95f)*(0.65f+0.35f*pulse);
        const Point apex{tip.x-dx*back,tip.y-dy*back};
        ok=AppendBar(out,{apex.x+armA.x*size,apex.y+armA.y*size},apex,thick,1.0f,0.22f,0.04f,alpha) &&
           AppendBar(out,{apex.x+armB.x*size,apex.y+armB.y*size},apex,thick,1.0f,0.22f,0.04f,alpha);
    }
    if(!ok) { out.count=start; return false; }
    return true;
}
inline bool Apply(const Quad& q,unsigned width,unsigned height,Vertex (&vertices)[4]) {
    if(width<640 || height<360 || width>16384 || height>16384) return false;
    for(auto p:q.points) if(!std::isfinite(p.x)||!std::isfinite(p.y)) return false;
    for(unsigned i=0;i<4;++i) {
        vertices[i].x=float(width)*0.5f-q.points[i].x;
        vertices[i].y=float(height)*0.5f+q.points[i].y;
        // Preserve native z/UV; this is the same synchronous copy/upload as map vertices.
    }
    return true;
}
} // namespace target_ring
