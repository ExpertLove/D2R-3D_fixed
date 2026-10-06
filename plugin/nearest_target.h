#pragma once
#include "target_provider.h"
#include <cmath>
#include <limits>
#include <vector>
#include <algorithm>

namespace click_target {
// Deterministic nearest eligible client monster. Never returns provider's
// observation generation as an attack handle: caller must bind native lifetime.
struct NearestResult { bool complete=false, found=false; unsigned id=0; float distance=0; };
inline NearestResult Nearest(const third_person::TargetFrame& frame,float range=80.0f) {
    NearestResult out;
    if(!frame.complete || !frame.session || frame.count>frame.units.size() ||
       !std::isfinite(frame.playerX) || !std::isfinite(frame.playerZ) ||
       !std::isfinite(range) || range<=0 || range>256) return out;
    double best=double(range)*range;
    out.complete=true;
    for(unsigned i=0;i<frame.count;++i) {
        const auto& unit=frame.units[i];
        if(unit.key.session!=frame.session || unit.key.id==UINT32_MAX || unit.relation!=third_person::Relation::Hostile ||
           unit.sight!=third_person::Sight::Clear || !std::isfinite(unit.x) || !std::isfinite(unit.z)) continue;
        const double dx=double(unit.x)-frame.playerX,dz=double(unit.z)-frame.playerZ;
        const double distance=dx*dx+dz*dz;
        if(distance>best || (out.found && distance==best && unit.key.id>=out.id)) continue;
        best=distance; out.found=true; out.id=unit.key.id; out.distance=float(std::sqrt(best));
    }
    return out;
}

// Stable F cycle. The order is formed once by distance (nearest first) and then
// only pruned/appended: moving enemies never reshuffle it, so no A-B ping-pong.
// Stores IDs only; the runtime revalidates identity/lifetime before binding.
class TargetCycle {
public:
    void reset() { order_.clear(); next_=0; session_=0; }
    size_t size() const { return order_.size(); }
    NearestResult next(const third_person::TargetFrame& frame,float range=80.0f,int direction=1,unsigned anchor=UINT32_MAX) {
        NearestResult out;
        if(!frame.complete || !frame.session || frame.count>frame.units.size() ||
           !std::isfinite(frame.playerX) || !std::isfinite(frame.playerZ) ||
           !std::isfinite(range) || range<=0 || range>256) return out;
        if(session_!=frame.session) { order_.clear(); next_=0; session_=frame.session; }
        out.complete=true;
        struct Item { unsigned id; double d2; };
        std::vector<Item> eligible;
        eligible.reserve(frame.count);
        for(unsigned i=0;i<frame.count;++i) {
            const auto& u=frame.units[i];
            if(u.key.session!=frame.session || u.key.id==UINT32_MAX || u.relation!=third_person::Relation::Hostile ||
               u.sight!=third_person::Sight::Clear || !std::isfinite(u.x) || !std::isfinite(u.z)) continue;
            const double dx=double(u.x)-frame.playerX,dz=double(u.z)-frame.playerZ;
            bool dup=false;
            for(const auto& e:eligible) if(e.id==u.key.id) { dup=true; break; }
            if(!dup) eligible.push_back({u.key.id,dx*dx+dz*dz});
        }
        const auto find=[&](unsigned id)->const Item* {
            for(const auto& e:eligible) if(e.id==id) return &e;
            return nullptr;
        };
        // Prune: gone/dead/hostile-lost/blocked, or clearly out of range (10% hysteresis).
        const double keep=double(range)*range*1.21;
        for(size_t i=0;i<order_.size();) {
            const auto* e=find(order_[i]);
            if(e && e->d2<=keep) { ++i; continue; }
            order_.erase(order_.begin()+i);
            if(i<next_) --next_;
        }
        // Append newcomers in range, nearest first; existing relative order untouched.
        std::vector<Item> fresh;
        const double limit=double(range)*range;
        for(const auto& e:eligible) {
            if(e.d2>limit || std::find(order_.begin(),order_.end(),e.id)!=order_.end()) continue;
            fresh.push_back(e);
        }
        std::sort(fresh.begin(),fresh.end(),[](const Item& a,const Item& b){ return a.d2<b.d2 || (a.d2==b.d2 && a.id<b.id); });
        for(const auto& e:fresh) order_.push_back(e.id);
        if(order_.empty()) { next_=0; return out; }
        // An explicit live target (including a mouse pick) is the cursor in both
        // directions. No target: first press of either key selects nearest.
        const auto at=std::find(order_.begin(),order_.end(),anchor);
        if(at!=order_.end()) {
            const auto index=size_t(at-order_.begin());
            next_=(index+order_.size()+(direction<0?-1:1))%order_.size();
        }
        if(next_>=order_.size()) next_=0;
        const unsigned id=order_[next_++];
        out.found=true; out.id=id; out.distance=float(std::sqrt(find(id)->d2));
        return out;
    }
private:
    std::vector<unsigned> order_;
    size_t next_=0;
    std::uint64_t session_=0;
};
} // namespace click_target
