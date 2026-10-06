#pragma once
#include "nearest_target.h"
#include "target_ring.h"
namespace click_target {
// Explicit key selection only. Body anchor is approximate (ground +4 render units),
// not a native skeleton joint. Radius is a fraction of viewport height.
inline NearestResult CenterTarget(const third_person::TargetFrame& frame,
    const target_ring::Camera& camera,unsigned width,unsigned height,float radius=0.09f) {
    NearestResult out;
    if(!frame.complete || !frame.session || frame.count>frame.units.size() || width<640 || height<360 ||
       width>16384 || height>16384 || !std::isfinite(radius) || radius<0.03f || radius>0.18f ||
       !std::isfinite(frame.playerX) || !std::isfinite(frame.playerZ)) return out;
    out.complete=true;
    double best=double(height*radius)*double(height*radius),bestWorld=80.0*80.0;
    for(unsigned i=0;i<frame.count;++i) {
        const auto& u=frame.units[i];
        if(u.key.session!=frame.session || u.key.id==UINT32_MAX || u.relation!=third_person::Relation::Hostile ||
           u.sight!=third_person::Sight::Clear || !std::isfinite(u.x) || !std::isfinite(u.z)) continue;
        const double dx=double(u.x)-frame.playerX,dz=double(u.z)-frame.playerZ,world=dx*dx+dz*dz;
        if(world>80.0*80.0) continue;
        target_ring::Point p;
        if(!target_ring::Project(camera,u.x,camera.groundY+4.0f,u.z,width,height,p) ||
           p.x<0 || p.x>width || p.y<0 || p.y>height) continue;
        const double sx=p.x-width*0.5,sy=p.y-height*0.5,d=sx*sx+sy*sy;
        if(d>best || (out.found && d==best && (world>bestWorld || (world==bestWorld && u.key.id>=out.id)))) continue;
        best=d; bestWorld=world; out.found=true; out.id=u.key.id; out.distance=float(std::sqrt(world));
    }
    return out;
}
}
