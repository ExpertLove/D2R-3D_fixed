#pragma once
// Pure camera-control math (no game memory). Input thread owns targets; the
// render-thread hooks only read the published current values.
#include <algorithm>
#include <cmath>

namespace camera_motion {
inline constexpr float MinDistance=3.5f, MaxDistance=1500.0f;
// Pitch is relative to the native ~30 degree isometric elevation. Below about
// -30 the eye drops under the pivot/terrain and sees through the ground.
inline constexpr float MinPitch=-25.0f, MaxPitch=60.0f;
inline constexpr float DefaultDistance=45.0f, DefaultPitch=22.0f;

struct Preset { const char* name; float distance, pitch; };
inline constexpr Preset Presets[]={
    {"CLASSIC",DefaultDistance,DefaultPitch},
    {"CLOSE",20.0f,8.0f},
    {"TACTICAL",85.0f,38.0f},
};
inline constexpr unsigned PresetCount=sizeof(Presets)/sizeof(Presets[0]);

inline float ClampDistance(float d) { return std::isfinite(d) ? std::clamp(d,MinDistance,MaxDistance) : DefaultDistance; }
inline float ClampPitch(float p) { return std::isfinite(p) ? std::clamp(p,MinPitch,MaxPitch) : DefaultPitch; }

// High-resolution wheels/touchpads send fractions of WHEEL_DELTA(120). Integer
// division used to drop them entirely while still swallowing the message.
inline float WheelNotches(int delta) { return float(delta)/120.0f; }
inline float Zoom(float distance,float notches) {
    if(!std::isfinite(notches)) return ClampDistance(distance);
    return ClampDistance(distance/std::pow(1.12f,notches));
}

// Frame-rate independent exponential approach; snaps when close enough.
inline float Approach(float current,float target,float dt,float tau,float epsilon=0.002f) {
    if(!std::isfinite(current)) return target;
    if(!(tau>0) || !(dt>0)) return current;
    const float next=current+(target-current)*(1.0f-std::exp(-dt/tau));
    return std::abs(target-next)<epsilon ? target : next;
}

// Over-the-shoulder lateral camera offset in view units. Positive moves the
// eye to the RIGHT (character appears left of center). Bounded so that very
// far tactical zoom never throws the character off screen.
inline float ShoulderOffset(unsigned mode,float distance) {
    if(mode==0 || !std::isfinite(distance)) return 0.0f;
    const float magnitude=std::clamp(distance*0.16f,0.0f,7.0f);
    return mode==1 ? magnitude : -magnitude;
}

inline unsigned NextPreset(unsigned current) { return (current+1)%PresetCount; }
} // namespace camera_motion
