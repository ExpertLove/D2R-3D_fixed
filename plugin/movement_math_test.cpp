#include "movement_math.h"
#include <cstdio>
#include <limits>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-5f; };
    float inv[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    third_person::GroundDirection d;
    check(third_person::CameraRelativeDirection(inv, 0, 1, d) && near(d.x,0) && near(d.z,-1), "forward");
    check(third_person::CameraRelativeDirection(inv, 1, 0, d) && near(d.x,1) && near(d.z,0), "right");
    check(third_person::CameraRelativeDirection(inv, 0, -1, d) && near(d.z,1), "back");
    check(third_person::CameraRelativeDirection(inv, -1, 0, d) && near(d.x,-1), "left");
    check(third_person::CameraRelativeDirection(inv, 1, 1, d) && near(std::hypot(d.x,d.z),1), "diagonal normalized");
    inv[8] = -1; inv[10] = 0;
    check(third_person::CameraRelativeDirection(inv, 0, 1, d) && near(d.x,1) && near(d.z,0), "rotated forward");
    check(third_person::CameraRelativeDirection(inv, 1, 0, d) && near(d.x,0) && near(d.z,1), "rotated right");
    inv[8] = 0; inv[9] = .8f; inv[10] = .6f;
    check(third_person::CameraRelativeDirection(inv, 0, 1, d) && near(d.z,-1), "pitch ignored");
    check(third_person::CameraRelativeDirection(inv, 0, 0, d) && near(d.x,0) && near(d.z,0), "opposing keys cancel");
    check(third_person::CameraRelativeDirection(inv, .5f, 0, d) && near(d.x,.5f), "analog magnitude preserved");
    inv[10] = 0;
    check(!third_person::CameraRelativeDirection(inv, 0, 1, d) && near(d.x,0) && near(d.z,0), "vertical camera rejects movement");
    inv[8] = std::numeric_limits<float>::quiet_NaN();
    check(!third_person::CameraRelativeDirection(inv, 0, 1, d), "invalid camera rejected");
    check(!third_person::CameraRelativeDirection(nullptr, 0, 1, d), "missing camera rejected");
    check(!third_person::CameraRelativeDirection(inv, std::numeric_limits<float>::infinity(), 1, d), "invalid input rejected");
    float base[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float camera[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    check(third_person::NativeKeyMask(base,camera,0,1) == 1, "native W");
    check(third_person::NativeKeyMask(base,camera,1,0) == 8, "native D");
    check(third_person::NativeKeyMask(base,camera,-1,0) == 2, "native A");
    check(third_person::NativeKeyMask(base,camera,0,-1) == 4, "native S");
    check(third_person::NativeKeyMask(base,camera,0,0) == 0, "native stop");
    check(third_person::NativeKeyMask(base,camera,1,1) == 9, "native diagonal");
    constexpr unsigned octants[] = {1,9,8,12,4,6,2,3};
    for (int i = 0; i < 8; ++i) {
        const float angle = i * 3.14159265358979323846f / 4;
        camera[8] = -std::sin(angle); camera[10] = std::cos(angle);
        check(third_person::NativeKeyMask(base,camera,0,1) == octants[i], "all yaw octants");
        check(third_person::NativeKeyMask(camera,camera,0,1) == 1, "matching base orientation");
    }
    camera[8] = camera[10] = 0;
    check(third_person::NativeKeyMask(base,camera,0,1) == 0, "vertical native stop");
    return failures ? 1 : 0;
}
