#pragma once
#include "target_ring.h"
#include <cstring>

namespace target_ring {
// Externally synchronized. Published from native camera callbacks, never rebuilt
// from independently sampled controls. Reader cannot retain an old camera forever.
struct FrameCache {
    Camera camera{};
    std::uintptr_t source=0;
    std::uint64_t tick=0;
    bool valid=false;
    void reset() { valid=false; source=0; tick=0; }
    bool publish(std::uintptr_t master,const Camera& value,std::uint64_t now,bool settled) {
        reset();
        if(!master || !settled || !std::isfinite(value.groundY)) return false;
        for(float v:value.view) if(!std::isfinite(v)) return false;
        for(float v:value.projection) if(!std::isfinite(v)) return false;
        // Our world camera is reverse-Z perspective, never a UI/ortho camera.
        if(std::abs(value.projection[11]+1)>0.001f || std::abs(value.projection[15])>0.001f ||
           value.projection[0]<=0 || value.projection[5]<=0) return false;
        camera=value;source=master;tick=now;valid=true;return true;
    }
    bool copy(std::uintptr_t master,std::uint64_t now,Camera& out) const {
        if(!valid || source!=master || now<tick || now-tick>150) return false;
        out=camera; return true;
    }
};
} // namespace target_ring
