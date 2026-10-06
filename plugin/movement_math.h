#pragma once

#include <cmath>

namespace third_person {

struct GroundDirection { float x = 0, z = 0; };

// Render-world direction only; NOT native D2R path/grid coordinates.
// invView is row-major; a conventional camera looks down local -Z.
// Input axes: right is D-A, forward is W-S. Ignore camera pitch and roll.
inline bool CameraRelativeDirection(const float* invView, float right, float forward,
                                    GroundDirection& output) {
    output = {};
    if (!invView || !std::isfinite(right) || !std::isfinite(forward)) return false;
    const float inputLength = std::hypot(right, forward);
    if (!std::isfinite(inputLength)) return false;
    if (inputLength < 1e-6f) return true;
    float fx = -invView[8], fz = -invView[10];
    const float length = std::hypot(fx, fz);
    if (!std::isfinite(length) || length < 1e-6f) return false;
    fx /= length; fz /= length;
    // Derive horizontal right from horizontal forward, not camera roll.
    const float rx = -fz, rz = fx;
    const float scale = inputLength > 1.0f ? 1.0f / inputLength : 1.0f;
    output.x = (rx * right + fx * forward) * scale;
    output.z = (rz * right + fz * forward) * scale;
    return true;
}

// Bits are W=1, A=2, S=4, D=8. Native W is assumed to follow the
// original camera's projected forward; verify this assumption in-game.
inline unsigned NativeKeyMask(const float* baseInv, const float* cameraInv,
                              float right, float forward) {
    GroundDirection wanted, base;
    if (!CameraRelativeDirection(cameraInv, right, forward, wanted) ||
        !CameraRelativeDirection(baseInv, 0, 1, base) ||
        std::hypot(wanted.x, wanted.z) < 1e-6f) return 0;
    const float localForward = wanted.x * base.x + wanted.z * base.z;
    const float localRight = wanted.x * -base.z + wanted.z * base.x;
    const int octant = (int(std::lround(std::atan2(localRight, localForward) *
                                      4.0f / 3.14159265358979323846f)) + 8) % 8;
    constexpr unsigned masks[] = {1, 9, 8, 12, 4, 6, 2, 3};
    return masks[octant];
}

} // namespace third_person
