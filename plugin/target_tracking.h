#pragma once
#include <algorithm>
#include <cmath>

namespace third_person {
struct LockStep { float yaw=0,pitch=0; bool valid=false; };
// Row-vector matrices, reverse-Z camera looking down -Z. Outputs degrees for
// the existing orbit controls; no camera layout or movement mapping changes.
inline LockStep CameraLockStep(const float* view,const float* inverse,
                               const float* target,float dt,const float* pivot=nullptr) {
    if (!std::isfinite(dt) || dt<=0 || dt>0.1f) return {};
    for (int i=0;i<16;++i) if (!std::isfinite(view[i]) || !std::isfinite(inverse[i])) return {};
    for (int i=0;i<3;++i)
        if (!std::isfinite(target[i]) || (pivot && !std::isfinite(pivot[i]))) return {};
    // Orbit around the player, not the eye: an enemy between player and camera
    // must turn the orbit instead of trapping the pitch controller backwards.
    const float dx=target[0]-(pivot?pivot[0]:inverse[12]);
    const float dz=target[2]-(pivot?pivot[2]:inverse[14]);
    const float rightLength=std::hypot(inverse[0],inverse[2]);
    const float forwardLength=std::hypot(inverse[8],inverse[10]);
    if (rightLength<0.01f || forwardLength<0.01f || std::hypot(dx,dz)<0.1f) return {};
    const float right=(dx*inverse[0]+dz*inverse[2])/rightLength;
    const float forward=-(dx*inverse[8]+dz*inverse[10])/forwardLength;
    float cameraY=view[13],cameraZ=view[14];
    for (int i=0;i<3;++i) { cameraY+=target[i]*view[i*4+1]; cameraZ+=target[i]*view[i*4+2]; }
    constexpr float degrees=57.295779513f;
    const float alpha=1-std::exp(-dt/0.16f);
    const float yawError=std::atan2(right,forward)*degrees;
    // Do not chase vertical coordinates through the back of the camera.
    const float pitchError=cameraZ<-0.1f && forward>0.1f ? -std::atan2(cameraY,-cameraZ)*degrees : 0;
    return {std::clamp(yawError*alpha,-90*dt,90*dt),
            std::clamp(pitchError*alpha,-60*dt,60*dt),true};
}
} // namespace third_person
