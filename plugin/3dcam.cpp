// Console command:  3dcam
// Hotkey:           F12
// While on: wheel = zoom, MMB = orbit. F10 = experimental continuous mouse look.
// In F10: hold Shift for a free cursor; release to resume mouse look without a jump.
// Camera tracking removed. Opt-in clickaim: F selects nearest hostile, manual mouse attacks aim.
// Red projected ground ring uses native UI packets; slope height remains approximate.
// Escape/Alt/UI shortcuts release mouse look and injected movement keys.
// Experimental 8-way camera-relative WASD requires native W/A/S/D movement bindings.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <D2RLPlugin/context.h>
#include <D2RLPlugin/lifecycle.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/overlay.h>
#include "movement_math.h"
#include "cursor_hold.h"
#include "automap_runtime.h"
#include "click_target_runtime.h"
#include "target_key.h"
#include "target_ring_runtime.h"
#include "marker_frame.h"
#include "center_target.h"
#include "settings_ui.h"

#pragma intrinsic(_ReturnAddress)

using namespace D2RL;

namespace {

// Defaults
constexpr float kFov = 60.0f;            // perspective vertical FOV (deg)
constexpr float kDist = 45.0f;           // default camera-to-pivot distance (world units); the wheel changes it
constexpr float kPitch = 22.0f;          // default pitch (deg)
constexpr float kHeight = 6.0f;          // pivot lift above the ground look-at point (world up)
constexpr float kShadowSlope = 0.5f;     // min downward slope of shadow-fit rays
constexpr bool  kWheelSwallow = true;    // don't pass the wheel to the game while on

const PluginContext* g_ctx = nullptr;
uintptr_t g_base = 0;
HMODULE g_self = nullptr;

constexpr uint64_t RVA_CAMCOPY = 0x7BBDA0, RVA_COPY_CALL = 0x7BDA2E;   // camera operator=
constexpr uint64_t RVA_GETPROJ = 0xED67A0, RVA_VIEWREBUILD = 0xED70A0, RVA_RAYBUILD = 0xED62B0,
                   RVA_SHADOWFIT = 0xF4E4F0, RVA_SHADOWFIT_END = 0xF4FC7C;
const uint8_t kSigCamCopy[16]     = {0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0x1D,0xF5,0x20,0x02,0x48,0x33,0xC4,0x48,0x89};
const uint8_t kSigCopyCall[5]     = {0xE8,0x6D,0xE3,0xFF,0xFF};
const uint8_t kSigGetProj[16]     = {0x48,0x8B,0xC4,0x53,0x48,0x81,0xEC,0x80,0x00,0x00,0x00,0x80,0xB9,0x70,0x01,0x00};
const uint8_t kSigViewRebuild[16] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x81,0xEC,0xB0,0x00};
const uint8_t kSigRayBuild[16]    = {0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x55,0x57,0x41,0x56,0x48};

constexpr size_t C_VIEW = 0x10, C_INVVIEW = 0x50, C_PROJ = 0x90, C_LOOKAT = 0x11C, C_LOOKAT_OK = 0x128,
                 C_EXTW = 0x148, C_EXTH = 0x14C, C_VPW = 0x150, C_VPH = 0x154, C_NEAR = 0x158,
                 C_TYPE = 0x168, C_PDIRTY = 0x170, C_VDIRTY = 0x171;

template <class T> inline T& F(uintptr_t a, size_t off) { return *(T*)(a + off); }

void Say(const char* msg) {
    if (!g_ctx) return;
    g_ctx->WriteConsoleMessage(msg, ConsoleMessageKind::Output);
    g_ctx->LogInfo(msg);
}

// Matrix math helpers (row-major)
void Mul4(const float* A, const float* B, float* C) {
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c)
        C[r*4+c] = A[r*4]*B[c] + A[r*4+1]*B[4+c] + A[r*4+2]*B[8+c] + A[r*4+3]*B[12+c];
}
void Vec4Mat(const float* v, const float* M, float* o) {
    for (int j = 0; j < 4; ++j) o[j] = v[0]*M[j] + v[1]*M[4+j] + v[2]*M[8+j] + v[3]*M[12+j];
}
bool Invert4(const float* m, float* out) {
    double a[4][8];
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { a[i][j] = m[i*4+j]; a[i][j+4] = i == j; }
    for (int col = 0; col < 4; ++col) {
        int piv = col;
        for (int r = col + 1; r < 4; ++r) if (fabs(a[r][col]) > fabs(a[piv][col])) piv = r;
        if (fabs(a[piv][col]) < 1e-12) return false;
        for (int j = 0; j < 8; ++j) std::swap(a[col][j], a[piv][j]);
        const double d = a[col][col];
        for (int j = 0; j < 8; ++j) a[col][j] /= d;
        for (int r = 0; r < 4; ++r) if (r != col) { const double f = a[r][col]; for (int j = 0; j < 8; ++j) a[r][j] -= f * a[col][j]; }
    }
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) out[i*4+j] = (float)a[i][j+4];
    return true;
}
void AxisRot4(float* M, float ux, float uy, float uz, float t) {   // rotation about unit axis u
    const float c = cosf(t), s = sinf(t), k = 1.0f - c;
    const float R[16] = {c + ux*ux*k, uy*ux*k + uz*s, uz*ux*k - uy*s, 0,
                         ux*uy*k - uz*s, c + uy*uy*k, uz*uy*k + ux*s, 0,
                         ux*uz*k + uy*s, uy*uz*k - ux*s, c + uz*uz*k, 0,
                         0, 0, 0, 1};
    memcpy(M, R, sizeof R);
}

// State
std::atomic<bool> g_enabled{false};
std::atomic<float> g_dist{kDist}, g_pitch{kPitch}, g_yaw{0.0f};
std::atomic<uint32_t> g_gen{1};
std::atomic<uintptr_t> g_master{0};

struct Cam { uintptr_t ptr; float proj0[16]; float extW, extH, nearZ; int type; bool haveBase, written; uint32_t lastGen; float lookSign; };
SRWLOCK g_camLock = SRWLOCK_INIT;
Cam g_cams[8];
int g_camCount = 0;
// Guarded by g_camLock; read by input thread, written only by world-camera rebuild.
float g_movementBase[16]{}, g_movementCamera[16]{};
ULONGLONG g_movementTick = 0;
// Copies only for marker projection; renderer never dereferences a camera/unit.
target_ring::FrameCache g_ringFrame;

Cam* FindCam(uintptr_t c) { for (int i = 0; i < g_camCount; ++i) if (g_cams[i].ptr == c) return &g_cams[i]; return nullptr; }

// Camera-to-pivot distance.
float Radius(const Cam&) { return g_dist.load(); }

// Infinite reverse-Z perspective, same form as the game's own perspective builder
// (proj[10] = 0, proj[11] = -1, proj[14] = near), keeping the ortho view's screen offset.
void BuildProj(const Cam& c, float* M) {
    const float aspect = c.extH > 0 && c.extW / c.extH > 0.1f && c.extW / c.extH < 10.0f ? c.extW / c.extH : 16.0f / 9.0f;
    const float f = 1.0f / tanf(kFov * 3.14159265f / 360.0f);
    const float radius = Radius(c);
    const float nearZ = c.nearZ > 0.0f && c.nearZ < radius * 0.5f ? c.nearZ : radius * 0.05f;
    memset(M, 0, 64);
    M[0] = f / aspect; M[5] = f;
    M[8] = -c.proj0[12]; M[9] = -c.proj0[13];
    M[11] = -1.0f; M[14] = nearZ;
}

// Hooks
using GetProjFn = uintptr_t (*)(uintptr_t);
using ViewRebuildFn = void (*)(uintptr_t);
using RayBuildFn = void (*)(uintptr_t, float*, float*, float*);
GetProjFn OrigGetProj; ViewRebuildFn OrigViewRebuild; RayBuildFn OrigRayBuild;

using CamCopyFn = uintptr_t (*)(uintptr_t dst, uintptr_t src);
CamCopyFn OrigCamCopy;
std::atomic<bool> g_masterLogged{false};

// Called under g_camLock on a native camera callback. Read actual paired
// matrices, not BuildProj(current input) mixed with an older folded view.
void CaptureMarkerCamera(uintptr_t cam) {
    if(cam!=g_master.load()) return;
    const auto* c=FindCam(cam);
    const bool settled=g_enabled.load() && c && c->written && c->lookSign<0 &&
        F<uint8_t>(cam,C_LOOKAT_OK) && !F<uint8_t>(cam,C_PDIRTY) && !F<uint8_t>(cam,C_VDIRTY);
    if(!settled) { g_ringFrame.reset(); return; }
    target_ring::Camera actual;
    memcpy(actual.view,(void*)(cam+C_VIEW),sizeof actual.view);
    memcpy(actual.projection,(void*)(cam+C_PROJ),sizeof actual.projection);
    actual.groundY=F<float>(cam,C_LOOKAT+4);
    g_ringFrame.publish(cam,actual,GetTickCount64(),true);
}
uintptr_t HookCamCopy(uintptr_t dst, uintptr_t src) {
    const uintptr_t r = OrigCamCopy(dst, src);
    if ((uintptr_t)_ReturnAddress() == g_base + RVA_COPY_CALL + 5) {
        AcquireSRWLockExclusive(&g_camLock);
        if(g_master.exchange(src)!=src) g_ringFrame.reset();
        if(src) CaptureMarkerCamera(src); // native world-frame heartbeat, even when no rebuild occurred
        ReleaseSRWLockExclusive(&g_camLock);
    }
    return r;
}

uintptr_t HookGetProj(uintptr_t cam) {
    if (!cam) return OrigGetProj(cam);
    const bool wasDirty = F<uint8_t>(cam, C_PDIRTY) != 0;
    const uintptr_t ret = OrigGetProj(cam);
    AcquireSRWLockExclusive(&g_camLock);
    Cam* c = FindCam(cam);
    if (!c && g_camCount < 8) { c = &g_cams[g_camCount++]; memset(c, 0, sizeof *c); c->ptr = cam; c->lookSign = -1.0f; }
    if (c) {
        if (wasDirty || !c->haveBase) {
            memcpy(c->proj0, (void*)(cam + C_PROJ), 64); c->haveBase = true;
            c->type = F<int32_t>(cam, C_TYPE); c->extW = F<float>(cam, C_EXTW); c->extH = F<float>(cam, C_EXTH); c->nearZ = F<float>(cam, C_NEAR);
        }
        const uint32_t gen = g_gen.load();
        if (c->lastGen != gen) { F<uint8_t>(cam, C_VDIRTY) = 1; c->lastGen = gen; }
        if (g_enabled.load() && c->ptr == g_master.load()) {
            float M[16]; BuildProj(*c, M);
            memcpy((void*)(cam + C_PROJ), M, 64);
            c->written = true;
        } else if (c->written) {
            F<uint8_t>(cam, C_PDIRTY) = 1; c->written = false;
        }
    }
    CaptureMarkerCamera(cam);
    ReleaseSRWLockExclusive(&g_camLock);
    return ret;
}

void HookViewRebuild(uintptr_t cam) {
    OrigViewRebuild(cam);
    if (!cam || !g_enabled.load()) return;
    AcquireSRWLockExclusive(&g_camLock);
    Cam* c = FindCam(cam);
    if (c && c->haveBase && c->ptr == g_master.load()) {
        float view[16]; memcpy(view, (void*)(cam + C_VIEW), 64);
        float P[4] = {0, 0, -Radius(*c), 1};
        if (F<uint8_t>(cam, C_LOOKAT_OK)) {   // pivot = look-at point lifted to body height (world y-up)
            const float L[4] = {F<float>(cam, C_LOOKAT), F<float>(cam, C_LOOKAT + 4) + kHeight, F<float>(cam, C_LOOKAT + 8), 1};
            Vec4Mat(L, view, P);
        }
        const float sgn = P[2] <= 0.0f ? -1.0f : 1.0f;
        const float d2r = 3.14159265f / 180.0f;
        float R[16], Ry[16], RR[16];
        AxisRot4(R, 1, 0, 0, g_pitch.load() * d2r);
        const float un = sqrtf(view[4]*view[4] + view[5]*view[5] + view[6]*view[6]);   // world up in view space
        if (g_yaw.load() != 0.0f && un > 1e-6f) { AxisRot4(Ry, view[4]/un, view[5]/un, view[6]/un, g_yaw.load() * d2r); Mul4(Ry, R, RR); memcpy(R, RR, 64); }
        float A[16]; memcpy(A, R, 64);
        A[12] = -(P[0]*R[0] + P[1]*R[4] + P[2]*R[8]);
        A[13] = -(P[0]*R[1] + P[1]*R[5] + P[2]*R[9]);
        A[14] = sgn * Radius(*c) - (P[0]*R[2] + P[1]*R[6] + P[2]*R[10]);
        float folded[16], inv[16];
        Mul4(view, A, folded);
        memcpy((void*)(cam + C_VIEW), folded, 64);
        if (Invert4(folded, inv)) {
            memcpy((void*)(cam + C_INVVIEW), inv, 64);
            float baseInv[16];
            if (sgn < 0 && Invert4(view, baseInv)) {
                memcpy(g_movementBase, baseInv, sizeof baseInv);
                memcpy(g_movementCamera, inv, sizeof inv);
                g_movementTick = GetTickCount64();
            } else g_movementTick = 0;
        }
        c->lookSign = sgn;
        CaptureMarkerCamera(cam);
    }
    ReleaseSRWLockExclusive(&g_camLock);
}

// Mouse ray through the camera's live (our) matrices: origin at the eye, unit direction.
bool UnprojectRay(uintptr_t cam, const float* mouse, float lookSign, float* origin, float* dir) {
    float V[16], P[16], invV[16], invP[16];
    memcpy(V, (void*)(cam + C_VIEW), 64); memcpy(P, (void*)(cam + C_PROJ), 64);
    const float W = F<float>(cam, C_VPW), H = F<float>(cam, C_VPH);
    if (!(W > 0 && H > 0) || !Invert4(V, invV) || !Invert4(P, invP)) return false;
    const float nx = 2.0f * mouse[0] / W - 1.0f, ny = 1.0f - 2.0f * mouse[1] / H;
    float a[4], b[4];
    const float ca[4] = {nx, ny, 1.0f, 1}, cb[4] = {nx, ny, 0.25f, 1};   // two depths on the ray
    Vec4Mat(ca, invP, a); Vec4Mat(cb, invP, b);
    if (fabsf(a[3]) < 1e-12f || fabsf(b[3]) < 1e-12f) return false;
    float d[4] = {b[0]/b[3] - a[0]/a[3], b[1]/b[3] - a[1]/a[3], b[2]/b[3] - a[2]/a[3], 0};
    if (d[2] * lookSign < 0) { d[0] = -d[0]; d[1] = -d[1]; d[2] = -d[2]; }
    const float eye[4] = {0, 0, 0, 1};
    float ow[4], dw[4]; Vec4Mat(eye, invV, ow); Vec4Mat(d, invV, dw);
    const float n = sqrtf(dw[0]*dw[0] + dw[1]*dw[1] + dw[2]*dw[2]);
    if (!(n > 1e-9f) || fabsf(ow[3]) < 1e-12f) return false;
    for (int i = 0; i < 3; ++i) { origin[i] = ow[i] / ow[3]; dir[i] = dw[i] / n; }
    return true;
}

void HookRayBuild(uintptr_t cam, float* mouse, float* origin, float* dir) {
    const uintptr_t ra = (uintptr_t)_ReturnAddress();
    OrigRayBuild(cam, mouse, origin, dir);
    if (!cam || !mouse || !origin || !dir || !g_enabled.load()) return;
    bool world = false; float lookSign = -1.0f;
    AcquireSRWLockShared(&g_camLock);
    if (const Cam* c = FindCam(cam)) { world = c->ptr == g_master.load(); lookSign = c->lookSign; }
    ReleaseSRWLockShared(&g_camLock);
    float o[3], d[3];
    if (world && UnprojectRay(cam, mouse, lookSign, o, d)) { memcpy(origin, o, 12); memcpy(dir, d, 12); }
    // The directional shadow fits its box by intersecting screen-corner rays with the ground;
    // at shallow pitch those rays run nearly flat and the box explodes. Clamp their slope.
    if (ra >= g_base + RVA_SHADOWFIT && ra < g_base + RVA_SHADOWFIT_END && dir[1] > -kShadowSlope) {
        float hx = dir[0], hz = dir[2], hl = sqrtf(hx*hx + hz*hz);
        if (hl < 1e-6f) { hx = 0; hz = 1; hl = 1; }
        const float k = sqrtf(1.0f - kShadowSlope * kShadowSlope);
        dir[0] = hx / hl * k; dir[1] = -kShadowSlope; dir[2] = hz / hl * k;
    }
}

// Mouse
HHOOK g_mouseHook = nullptr;
HHOOK g_keyboardHook = nullptr;
unsigned g_physicalKeys = 0, g_consumedKeys = 0, g_sentKeys = 0;
bool g_movementArmed = false;
bool g_relativeMovement = true;
constexpr ULONG_PTR kMovementTag = 0x4432523344574153ULL;
constexpr WORD kMovementKeys[] = {'W', 'A', 'S', 'D'};
DWORD g_mouseThreadId = 0;
HANDLE g_mouseThread = nullptr;
std::atomic<bool> g_quit{false}, g_inGame{false}, g_freeLook{false};
bool g_lifecycleOk = false;
bool g_mmbDown = false;
POINT g_mmbLast{};
third_person::ShiftOwnership g_shift; // input thread only
click_target::SelectionKey g_actionKeys[256]; // ownership by physical key, stable across rebinding
bool g_uiKeys[256]{},g_keyHeld[256]{},g_uiMouse[5]{};
bool g_settingsDirty=false;
const char* Toggle();
uint64_t g_shiftEpoch = 0;
constexpr int kReleaseKeys[] = {VK_ESCAPE, VK_MENU, VK_RETURN, VK_OEM_3, 'I', 'C', 'Q', 'T', 'P', 'H'};

bool GameFocused() {
    DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

bool MapFollowActive() {
    return g_enabled.load() && g_freeLook.load() && g_inGame.load();
}
bool MapFollowHeading(float& x,float& z) {
    third_person::GroundDirection direction;
    AcquireSRWLockShared(&g_camLock);
    const bool valid=g_movementTick && third_person::CameraRelativeDirection(g_movementCamera,0,1,direction);
    ReleaseSRWLockShared(&g_camLock);
    x=direction.x; z=direction.z;
    return valid;
}

const OverlayService* g_overlay = nullptr;
Overlay::CallbackHandle g_overlayHandle = Overlay::InvalidCallbackHandle;

void __cdecl DrawAimReticle(const PluginContext* ctx, const Overlay::Frame* frame, void*) noexcept {
    static bool callbackLogged = false, successLogged = false, errorLogged = false;
    static unsigned previousState = ~0u, stateLogs = 0;
    if (!callbackLogged) {
        ctx->LogInfo("reticle diagnostic: overlay callback entered");
        callbackLogged = true;
    }
    const bool validFrame = frame && frame->structSize >= Overlay::FrameRequiredSize;
    const bool camera = g_enabled.load(), playing = g_inGame.load(), focused = GameFocused();
    const bool look = g_freeLook.load();
    const unsigned state = unsigned(validFrame) | (unsigned(camera)<<1) |
        (unsigned(playing)<<2) | (unsigned(focused)<<3) | (unsigned(look)<<4);
    if (state != previousState && stateLogs < 24) {
        char message[192];
        snprintf(message, sizeof message, "reticle diagnostic: frame=%d camera=%d playing=%d focused=%d look=%d size=%.0fx%.0f",
                 validFrame, camera, playing, focused, look,
                 validFrame ? frame->screenWidth : 0.0f, validFrame ? frame->screenHeight : 0.0f);
        ctx->LogInfo(message);
        previousState = state;
        ++stateLogs;
    }
    if (!g_overlay || !validFrame || !camera || !playing || !focused) return;
    if (!(frame->screenWidth > 0 && frame->screenHeight > 0)) return;
    const float x = frame->screenWidth * 0.5f, y = frame->screenHeight * 0.5f;
    // Diagnostic reticle: green means mouse look, gold means free cursor; NOT target status.
    const Overlay::Point starts[] = {{x-22,y}, {x+6,y}, {x,y-22}, {x,y+6}};
    const Overlay::Point ends[] = {{x-6,y}, {x+22,y}, {x,y-6}, {x,y+22}};
    const Overlay::Color foreground = look ? Overlay::Color{0.2f,1.0f,0.2f,1.0f} : Overlay::Color{1.0f,0.8f,0.1f,1.0f};
    for (int pass = 0; pass < 2; ++pass) for (int i = 0; i < 4; ++i) {
        Overlay::LineRequest line{};
        line.structSize = Overlay::LineRequestSize;
        line.canvas = frame->canvas;
        line.start = starts[i]; line.end = ends[i];
        line.color = pass == 0 ? Overlay::Color{0,0,0,1.0f} : foreground;
        line.thickness = pass == 0 ? 7.0f : 3.0f;
        const auto result = g_overlay->drawLine(ctx, &line);
        if (result != Overlay::Result::Success && !errorLogged) {
            char message[96];
            snprintf(message, sizeof message, "reticle diagnostic: drawLine failed, result=%u", unsigned(result));
            ctx->LogError(message);
            errorLogged = true;
        } else if (result == Overlay::Result::Success && !successLogged) {
            ctx->LogInfo("reticle diagnostic: drawLine accepted (pixel visibility not verified)");
            successLogged = true;
        }
    }
}

bool RegisterReticle(const PluginContext* ctx) {
    const OverlayService* service = nullptr;
    if (ctx->QueryService(&service) != ServiceQueryResult::Success ||
        !HasOverlayServiceField(service, OverlayServiceRequiredSize)) return false;
    g_overlay = service;
    const Overlay::CallbackRegistration registration{
        Overlay::CallbackRegistrationSize, 0, Overlay::Phase::AfterGameUi, 0, &DrawAimReticle, nullptr};
    if (service->registerFrameCallback(ctx, &registration, &g_overlayHandle) != Overlay::Result::Success) {
        g_overlay = nullptr;
        return false;
    }
    return true;
}

// These helpers and the LL keyboard callback run on the same input thread.
bool SetMovementKeys(unsigned desired) {
    // Release old directions before pressing new ones; track only successful sends.
    for (int phase = 0; phase < 2; ++phase) for (unsigned i = 0; i < 4; ++i) {
        const unsigned bit = 1u << i;
        const bool down = (desired & bit) != 0;
        if (down != (phase == 1) || down == ((g_sentKeys & bit) != 0)) continue;
        INPUT event{};
        event.type = INPUT_KEYBOARD;
        event.ki.wVk = kMovementKeys[i];
        event.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        event.ki.dwExtraInfo = kMovementTag;
        if (SendInput(1, &event, sizeof event) != 1) return false;
        if (down) g_sentKeys |= bit; else g_sentKeys &= ~bit;
    }
    return true;
}

bool MovementRequested() {
    return !camera_settings::open.load() && g_keyboardHook && g_relativeMovement && g_freeLook.load() && g_enabled.load() && g_inGame.load() && GameFocused();
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM message, LPARAM data) {
    if (code != HC_ACTION) return CallNextHookEx(g_keyboardHook, code, message, data);
    const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
    if (key->flags & LLKHF_INJECTED) return CallNextHookEx(g_keyboardHook, code, message, data);
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!down && !up) return CallNextHookEx(g_keyboardHook, code, message, data);
    const unsigned physical=(key->scanCode&127u)|((key->flags&LLKHF_EXTENDED)?128u:0u);
    const bool first=down && !g_keyHeld[physical];
    g_keyHeld[physical]=down;
    // Preserve key-up ownership after closing the panel or changing a binding.
    if(g_uiKeys[physical]) {
        if(up) {
            g_uiKeys[physical]=false;g_actionKeys[physical].event(false,false);
            for(unsigned i=0;i<4;++i) if(key->vkCode==kMovementKeys[i]) { g_physicalKeys&=~(1u<<i);g_consumedKeys&=~(1u<<i); }
        }
        return 1;
    }
    if(down && (key->vkCode==VK_MENU || key->vkCode==VK_LMENU || key->vkCode==VK_RMENU || key->vkCode==VK_LWIN || key->vkCode==VK_RWIN)) {
        camera_settings::Close();g_freeLook.store(false);++g_shiftEpoch;
    }
    if(camera_settings::open.load() && GameFocused() && key->vkCode!=VK_MENU && key->vkCode!=VK_LMENU && key->vkCode!=VK_RMENU) {
        // Modifiers still run through ShiftOwnership, preventing stuck held Shift.
        if(key->vkCode!=VK_SHIFT && key->vkCode!=VK_LSHIFT && key->vkCode!=VK_RSHIFT) {
            if(down) {
                if(first) {
                    if(key->vkCode==VK_ESCAPE) {
                        if(camera_settings::waiting.load()>=0) camera_settings::waiting.store(-1);
                        else { camera_settings::Close(); ++g_shiftEpoch; }
                    } else if(camera_settings::waiting.load()>=0) {
                        auto c=camera_settings::Copy();
                        const bool modifiers=g_keyHeld[0x1d] || g_keyHeld[0x9d] || g_keyHeld[0x38] || g_keyHeld[0xb8] || g_shift.held();
                        if(!modifiers && camera_settings::Bind(c,unsigned(camera_settings::waiting.load()),physical)) {
                            camera_settings::Set(c); camera_settings::waiting.store(-1);g_settingsDirty=true;
                        } else camera_settings::status.store(1);
                    }
                }
                if(first) g_uiKeys[physical]=true; // repeats of pre-panel native downs retain their native key-up
                return 1;
            }
            // Key-up belonging to native/injected movement must still be processed below.
        }
    }
    if (key->vkCode==VK_LSHIFT || key->vkCode==VK_RSHIFT || key->vkCode==VK_SHIFT) {
        const unsigned bit=(key->vkCode==VK_RSHIFT || key->scanCode==0x36) ? 2u : 1u;
        const bool wasHeld=g_shift.held();
        const bool own=g_freeLook.load() && g_enabled.load() && g_inGame.load() && GameFocused();
        const bool swallow=g_shift.event(bit,down,own);
        if (wasHeld!=g_shift.held()) ++g_shiftEpoch;
        click_aim::Publish(own && g_mouseHook && !camera_settings::open.load(),g_shift.held());
        if (swallow) return 1; // no native stand-still binding for our temporary cursor modifier
    }
    const auto bindings=camera_settings::Copy();
    int action=-1;
    for(int i=0;i<camera_settings::ActionCount;++i) if(bindings.keys[i]==physical) action=i;
    bool eligible=action>=0 && GameFocused() && !camera_settings::open.load();
    if(action>=0 && action<=camera_settings::Center)
        eligible=eligible && click_aim::context().active && (action!=camera_settings::Elite || click_aim::eliteReady.load());
    if(action==camera_settings::Look) eligible=eligible && g_enabled.load() && g_inGame.load() && g_lifecycleOk;
    const auto choice=g_actionKeys[physical].event(down,eligible);
    if(choice.request) {
        if(action<=camera_settings::Center) click_aim::QueueSelection(action==camera_settings::Previous?-1:1,action==camera_settings::Elite,action==camera_settings::Center);
        else if(action==camera_settings::Look) g_freeLook.store(!g_freeLook.load());
        else if(action==camera_settings::Movement) g_relativeMovement=!g_relativeMovement;
        else if(action==camera_settings::Camera && g_ctx) g_ctx->LogInfo(Toggle());
    }
    // Preserve default F12 passthrough: the separate renderdistance plugin polls it.
    if(choice.consume && !(action==camera_settings::Camera && physical==0x58)) return 1;
    if (down && GameFocused()) for (int releaseKey : kReleaseKeys)
        if (key->vkCode==unsigned(releaseKey)) {
            g_freeLook.store(false);
            click_aim::Publish(false,g_shift.held());
        }
    for (unsigned i = 0; i < 4; ++i) if (key->vkCode == kMovementKeys[i]) {
        const unsigned bit = 1u << i;
        if (down) g_physicalKeys |= bit; else g_physicalKeys &= ~bit;
        // Swallow paired key-up even if capture was released meanwhile.
        if (up && (g_consumedKeys & bit)) { g_consumedKeys &= ~bit; return 1; }
        if (down && (g_consumedKeys & bit)) return 1;
        if (down && g_movementArmed && MovementRequested()) { g_consumedKeys |= bit; return 1; }
        break;
    }
    return CallNextHookEx(g_keyboardHook, code, message, data);
}

void UpdateMovement() {
    if (!MovementRequested()) {
        g_movementArmed = false;
        SetMovementKeys(0);
        return;
    }
    // Do not switch a held physical key from passthrough into synthetic ownership.
    if (!g_movementArmed) {
        if (g_physicalKeys == 0 && g_consumedKeys == 0) g_movementArmed = true;
        return;
    }
    const unsigned held = g_physicalKeys & g_consumedKeys;
    const float right = float(bool(held & 8)) - float(bool(held & 2));
    const float forward = float(bool(held & 1)) - float(bool(held & 4));
    unsigned desired = 0;
    AcquireSRWLockShared(&g_camLock);
    if (g_movementTick)
        desired = third_person::NativeKeyMask(g_movementBase, g_movementCamera, right, forward);
    ReleaseSRWLockShared(&g_camLock);
    if (!SetMovementKeys(desired)) {
        g_freeLook.store(false);
        g_movementArmed = false;
        SetMovementKeys(0);
        if (g_ctx) g_ctx->LogError("3dcam: SendInput failed; movement and mouse look disabled");
    }
}

void RotateCamera(float dx, float dy) {
    const float sensitivity=camera_settings::Copy().sensitivity/100.0f;
    dx*=sensitivity;dy*=sensitivity;
    float yaw = g_yaw.load() + dx * 0.10f;
    yaw -= 360.0f * floorf((yaw + 180.0f) / 360.0f);
    g_yaw.store(yaw);
    g_pitch.store(std::clamp(g_pitch.load() + dy * 0.075f, -85.0f, 60.0f));
    g_gen.fetch_add(1);
}

void __cdecl GameplayChanged(const PluginContext*, const Lifecycle::GameplayEvent* event, void*) noexcept {
    if (!event || event->structSize < sizeof(Lifecycle::GameplayEvent)) return;
    camera_settings::Close(); camera_settings::frameTick.store(0);
    camera_automap::Invalidate();
    click_aim::Invalidate();
    click_aim::Publish(false,false);
    const bool ready = event->kind == Lifecycle::GameplayEventKind::LocalPlayerReady;
    g_inGame.store(ready);
    g_freeLook.store(false);
    AcquireSRWLockExclusive(&g_camLock);
    g_movementTick = 0;
    g_ringFrame.reset();
    ReleaseSRWLockExclusive(&g_camLock);
    if (!ready) { g_master.store(0); g_masterLogged.store(false); }
}

bool RegisterGameplay(const PluginContext* ctx) {
    const LifecycleService* service = nullptr;
    if (ctx->QueryService(&service) != ServiceQueryResult::Success ||
        !HasLifecycleServiceField(service, LifecycleServiceRequiredSize)) return false;
    for (auto kind : {Lifecycle::GameplayEventKind::LocalPlayerReady, Lifecycle::GameplayEventKind::GameLeft}) {
        const Lifecycle::GameplayEventListener listener{Lifecycle::GameplayEventListenerSize, 0, kind, 0, &GameplayChanged, nullptr};
        Lifecycle::ListenerHandle handle = 0;
        if (service->registerGameplayEventListener(ctx, &listener, &handle) != Lifecycle::Result::Success) return false;
    }
    return true;
}

// Only the input thread touches capture state. No ClipCursor or global cursor hiding:
// if the plugin stops responding, the OS cursor is never locked to our window.
void UpdateMouseLook() {
    static third_person::CursorHold capture;
    static POINT previousCenter{};
    static float pendingX = 0.0f, pendingY = 0.0f;
    static ULONGLONG lastTick = 0;
    const ULONGLONG now = GetTickCount64();
    const float dt = lastTick ? std::clamp(float(now - lastTick) / 1000.0f, 0.0f, 0.05f) : 0.01f;
    lastTick = now;
    const bool focused = GameFocused();
    // Conservative safety gate for default UI bindings; custom bindings require F10 first.
    for (int key : kReleaseKeys)
        if (GetAsyncKeyState(key) & 0x8000) g_freeLook.store(false);
    if (!focused || !g_enabled.load() || !g_inGame.load()) { g_freeLook.store(false); camera_settings::Close(); }
    if(camera_settings::open.load() && !camera_settings::Fresh()) { camera_settings::Close();g_freeLook.store(false); }
    static uint64_t previousShiftEpoch=0;
    if (previousShiftEpoch!=g_shiftEpoch) {
        capture.reset(); pendingX=pendingY=0;
        previousShiftEpoch=g_shiftEpoch;
    }
    const bool shiftHeld = g_keyboardHook ? g_shift.held() : (GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;
    const bool panel=camera_settings::open.load();
    click_aim::Publish(!panel && g_mouseHook && g_keyboardHook && g_freeLook.load() && g_enabled.load() && g_inGame.load() && focused,shiftHeld);
    const auto action = capture.update(g_freeLook.load(), shiftHeld || panel);
    if (action == third_person::CaptureAction::Free) {
        pendingX = pendingY = 0.0f;
        return; // no centering/rotation while Shift is held; requested F10 mode persists
    }
    HWND window = GetForegroundWindow();
    RECT rect{};
    POINT cursor{};
    if (!GetClientRect(window, &rect) || rect.right < 64 || rect.bottom < 64 || !GetCursorPos(&cursor)) {
        g_freeLook.store(false); capture.reset(); return;
    }
    POINT center{rect.right / 2, rect.bottom / 2};
    if (!ClientToScreen(window, &center)) { g_freeLook.store(false); capture.reset(); return; }
    if (action == third_person::CaptureAction::Track && center.x == previousCenter.x && center.y == previousCenter.y) {
        const int dx = cursor.x - center.x, dy = cursor.y - center.y;
        pendingX += float(dx);
        pendingY += float(dy);
        // Exponential filtering of mouse displacement, independent of timer frequency.
        // 35 ms time constant: softens sudden input without a long trailing rotation.
        const float alpha = 1.0f - expf(-dt / 0.035f);
        const float stepX = pendingX * alpha, stepY = pendingY * alpha;
        pendingX -= stepX;
        pendingY -= stepY;
        if (fabsf(pendingX) < 0.001f) pendingX = 0.0f;
        if (fabsf(pendingY) < 0.001f) pendingY = 0.0f;
        if (stepX != 0.0f || stepY != 0.0f) RotateCamera(stepX, stepY);
    } else {
        pendingX = pendingY = 0.0f;
    }
    previousCenter = center;
    if (!SetCursorPos(center.x, center.y)) { capture.reset(); g_freeLook.store(false); }
}

// Input-side hit testing uses the last fresh native render viewport. No units read.
bool SettingsMouse(WPARAM message,const MSLLHOOKSTRUCT& ms) {
    const bool down=message==WM_LBUTTONDOWN || message==WM_RBUTTONDOWN || message==WM_MBUTTONDOWN || message==WM_XBUTTONDOWN;
    const bool up=message==WM_LBUTTONUP || message==WM_RBUTTONUP || message==WM_MBUTTONUP || message==WM_XBUTTONUP;
    const int button=(message==WM_LBUTTONDOWN || message==WM_LBUTTONUP)?0:
        (message==WM_RBUTTONDOWN || message==WM_RBUTTONUP)?1:
        (message==WM_MBUTTONDOWN || message==WM_MBUTTONUP)?2:
        (message==WM_XBUTTONDOWN || message==WM_XBUTTONUP)?(HIWORD(ms.mouseData)==XBUTTON1?3:4):-1;
    if(button>=0 && g_uiMouse[button]) { if(up) g_uiMouse[button]=false; return true; }
    if(!GameFocused() || !g_enabled.load() || !g_inGame.load()) return false;
    const bool panel=camera_settings::open.load();
    POINT p=ms.pt; RECT client{}; const auto window=GetForegroundWindow();
    const bool coordinates=camera_settings::Fresh() && ScreenToClient(window,&p) && GetClientRect(window,&client) && client.right>0 && client.bottom>0;
    float x=0,y=0;
    if(coordinates) { x=float(p.x)*camera_settings::width.load()/client.right;y=float(p.y)*camera_settings::height.load()/client.bottom; }
    const camera_settings::Layout layout(camera_settings::width.load(),camera_settings::height.load());
    if(!panel) {
        if(!(down && button==0 && coordinates && (!g_freeLook.load() || g_shift.held()) && layout.gear.contains(x,y))) return false;
        if((GetAsyncKeyState(VK_RBUTTON)&0x8000) || (GetAsyncKeyState(VK_MBUTTON)&0x8000)) return false;
        g_uiMouse[0]=true;g_mmbDown=false;
        camera_settings::open.store(true);camera_settings::waiting.store(-1);
        click_aim::Publish(false,g_shift.held());SetMovementKeys(0);g_movementArmed=false;++g_shiftEpoch;
        return true;
    }
    if(down && button>=0) g_uiMouse[button]=true;
    if(down && button==0 && coordinates) {
        const int hit=layout.hit(x,y);
        if(hit>=0 && hit<camera_settings::ActionCount) { camera_settings::waiting.store(hit);camera_settings::status.store(0); }
        else {
            camera_settings::waiting.store(-1);
            auto c=camera_settings::Copy();bool changed=false;
            switch(hit) {
                case 100: camera_settings::Close();++g_shiftEpoch;break;
                case 101: if(g_ctx) g_ctx->LogInfo(click_aim::Toggle());c.targeting=click_aim::enabled.load();changed=true;break;
                case 102: if(g_ctx) g_ctx->LogInfo(camera_automap::Toggle());c.mapFollow=camera_automap::enabled.load();changed=true;break;
                case 103: c.seal=!c.seal;changed=true;break;
                case 104: c.sensitivity=std::max(25u,c.sensitivity-5);changed=true;break;
                case 105: c.sensitivity=std::min(250u,c.sensitivity+5);changed=true;break;
                case 106: c.radius=std::max(3u,c.radius-1);changed=true;break;
                case 107: c.radius=std::min(18u,c.radius+1);changed=true;break;
                case 108: c={};click_aim::enabled.store(false);click_aim::Invalidate();camera_automap::enabled.store(true);camera_automap::Invalidate();changed=true;break;
            }
            if(changed) { camera_settings::Set(c);g_settingsDirty=true; }
        }
    }
    // Eat all panel button/wheel events, including clicks outside its rectangle.
    return down || message==WM_MOUSEWHEEL || message==WM_MOUSEHWHEEL;
}

// Wheel = zoom (camera distance)
// Middle mouse drag = yaw/pitch around the player.
LRESULT CALLBACK MouseProc(int code, WPARAM w, LPARAM l) {
    if(code==HC_ACTION) {
        const auto& event=*reinterpret_cast<const MSLLHOOKSTRUCT*>(l);
        if(!(event.flags&LLMHF_INJECTED) && SettingsMouse(w,event)) return 1;
        if(camera_settings::open.load()) return CallNextHookEx(g_mouseHook,code,w,l);
    }
    if (code == HC_ACTION && g_enabled.load() && GameFocused() &&
        (!g_lifecycleOk || g_inGame.load())) {
        const auto* ms = (const MSLLHOOKSTRUCT*)l;
        if(w==WM_LBUTTONDOWN && !(ms->flags&LLMHF_INJECTED)) click_aim::PhysicalLeftDown();
        if (w == WM_MOUSEWHEEL) {
            const int steps = (short)HIWORD(ms->mouseData) / WHEEL_DELTA;
            g_dist.store(std::clamp(g_dist.load() / powf(1.12f, (float)steps), 3.5f, 1500.0f));
            g_gen.fetch_add(1);
            if (kWheelSwallow) return 1;
        } else if (w == WM_MBUTTONDOWN) {
            g_mmbDown = true; g_mmbLast = ms->pt;
        }
        else if (w == WM_MBUTTONUP) g_mmbDown = false;
        else if (w == WM_MOUSEMOVE && g_mmbDown && !g_freeLook.load()) {
            const int dx = ms->pt.x - g_mmbLast.x, dy = ms->pt.y - g_mmbLast.y;
            g_mmbLast = ms->pt;
            RotateCamera(float(dx), float(dy));
        }
    } else if (code == HC_ACTION && w == WM_MBUTTONUP) g_mmbDown = false;
    return CallNextHookEx(g_mouseHook, code, w, l);
}

bool g_hooksOk = false;

const char* Toggle() {
    if (!g_hooksOk) return "3dcam: unavailable - the camera hooks did not install (see log)";
    const bool on = !g_enabled.load();
    g_enabled.store(on);
    if (!on) { g_freeLook.store(false); camera_settings::Close(); }
    g_gen.fetch_add(1);
    AcquireSRWLockExclusive(&g_camLock); g_ringFrame.reset(); ReleaseSRWLockExclusive(&g_camLock);
    if (on && !g_master.load()) return "3dcam ON - waiting for the world camera (enter a game)";
    return on ? "3dcam ON (F10 = mouse look, hold Shift = cursor, MMB = orbit outside F10, Esc/Alt = release)" : "3dcam OFF";
}

ConsoleCommandResult __cdecl Cmd3dcam(D2R::Game::Client*, const ConsoleCommandContext*, void*) noexcept {
    Say(Toggle());
    return ConsoleCommandResult::Handled;
}

click_target::NearestResult FindCenterTarget(std::uintptr_t player,std::uint64_t session) {
    if(!camera_settings::Fresh()) return {};
    target_ring::Camera camera;
    AcquireSRWLockShared(&g_camLock);
    const bool valid=g_ringFrame.copy(g_master.load(),GetTickCount64(),camera);
    ReleaseSRWLockShared(&g_camLock);
    if(!valid) return {};
    unsigned id=0;third_person::TargetFrame frame;
    if(!click_aim::get(player+8,id) || !click_aim::nearestProvider.sample(click_aim::memory,g_base,session,id,GetTickCount64(),frame)) return {};
    return click_target::CenterTarget(frame,camera,camera_settings::width.load(),camera_settings::height.load(),camera_settings::Copy().radius/100.0f);
}

bool BuildTargetRing(unsigned width,unsigned height,target_ring::Batch& batch) {
    if(width<640 || height<360 || width>16384 || height>16384) return false;
    batch.count=0;batch.width=width;batch.height=height;
    click_aim::RingPoint target;
    if(camera_settings::Copy().seal && click_aim::CopyRingPoint(target)) {
        target_ring::Camera camera;
        AcquireSRWLockShared(&g_camLock);
        const bool valid=g_ringFrame.copy(g_master.load(),GetTickCount64(),camera);
        ReleaseSRWLockShared(&g_camLock);
        if(valid) target_ring::Build(camera,target.x,target.z,width,height,batch,std::uint32_t(GetTickCount64()));
    }
    camera_settings::Draw(batch,click_aim::enabled.load(),camera_automap::enabled.load());
    return batch.count>0;
}

ConsoleCommandResult __cdecl CmdClickAim(D2R::Game::Client*, const ConsoleCommandContext*, void*) noexcept {
    Say(click_aim::Toggle());
    return ConsoleCommandResult::Handled;
}

ConsoleCommandResult __cdecl CmdMapFollow(D2R::Game::Client*, const ConsoleCommandContext*, void*) noexcept {
    Say(camera_automap::Toggle());
    return ConsoleCommandResult::Handled;
}

// Mouse hook + F12 polling. F12 runs outside the game thread, so it only logs.
DWORD WINAPI MouseThread(void*) {
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, g_self, 0);
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, g_self, 0);
    for(unsigned scan=0;scan<256;++scan) {
        const auto vk=MapVirtualKeyW(scan&127,MAPVK_VSC_TO_VK_EX);
        const bool held=vk && (GetAsyncKeyState(vk)&0x8000);
        g_actionKeys[scan].initialize(held);g_keyHeld[scan]=held;
    }
    g_shift.initialize(((GetAsyncKeyState(VK_LSHIFT)&0x8000)?1u:0u) |
                       ((GetAsyncKeyState(VK_RSHIFT)&0x8000)?2u:0u));
    for (unsigned i = 0; i < 4; ++i)
        if (GetAsyncKeyState(kMovementKeys[i]) & 0x8000) g_physicalKeys |= 1u << i;
    if (g_ctx) g_ctx->LogInfo(g_keyboardHook ? "3dcam: experimental 8-way WASD hook ready" : "3dcam: keyboard hook failed; native movement unchanged");
    const UINT_PTR timer = SetTimer(nullptr, 0, 10, nullptr);
    if (!timer) {
        if (g_keyboardHook) UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
        if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
        return 0;
    }
    MSG m;
    while (!g_quit.load() && GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (m.message == WM_TIMER) {
            if(g_settingsDirty) {
                g_settingsDirty=false;
                if(!camera_settings::Save() && g_ctx) g_ctx->LogError("settings: could not atomically save 3dcam-settings.ini");
            }
            UpdateMouseLook();
            UpdateMovement();
            camera_automap::Flush(g_ctx);
            click_aim::Flush(g_ctx);
            ring_ui::Flush(g_ctx);
            if (g_inGame.load() && g_master.load() && !g_masterLogged.exchange(true) && g_ctx) {
                char b[96];
                snprintf(b, sizeof b, "3dcam: world camera %p", (void*)g_master.load());
                g_ctx->LogInfo(b);
            }
            continue;
        }
        DispatchMessageW(&m);
    }
    SetMovementKeys(0);
    if (g_keyboardHook) UnhookWindowsHookEx(g_keyboardHook);
    g_keyboardHook = nullptr;
    KillTimer(nullptr, timer);
    if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
    g_mouseHook = nullptr;
    return 0;
}

}  // namespace

static const PluginInfo g_info = {
    PluginInfoSize, D2RL_PLUGIN_ABI_VERSION, "d2r-3d-3dcam", "3dcam", "1.1.0-experimental.22", "Tandanu",
    "Perspective camera, camera-relative WASD, Shift cursor and experimental heading-up Tab map. F12 camera, F10 mouse look.",
    PluginFlags::Shared | PluginFlags::NativeHooks, {0, 0, 0, 0},
};

D2RL_PLUGIN_EXPORT const PluginInfo* D2RLoaderGetPluginInfo() noexcept { return &g_info; }

D2RL_PLUGIN_EXPORT bool D2RLoaderLoadPlugin(const PluginContext* ctx) noexcept {
    if (!HasContext(ctx) || !ctx->GetApi()) return false;
    g_ctx = ctx;
    g_quit.store(false);
    g_inGame.store(false);
    g_freeLook.store(false);
    g_base = ctx->exeBase ? ctx->exeBase : (uintptr_t)GetModuleHandleW(nullptr);
    char baseMessage[96];
    snprintf(baseMessage, sizeof baseMessage, "3dcam diagnostic: game image base=%p", (void*)g_base);
    ctx->LogInfo(baseMessage);
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Cmd3dcam, &g_self);
    camera_settings::Load(g_self);
    const bool hc = memcmp((void*)(g_base + RVA_COPY_CALL), kSigCopyCall, sizeof kSigCopyCall) == 0 &&
                    ctx->InstallInlineHook(RVA_CAMCOPY, kSigCamCopy, sizeof kSigCamCopy, (void*)&HookCamCopy, (void**)&OrigCamCopy);
    const bool h1 = ctx->InstallInlineHook(RVA_GETPROJ, kSigGetProj, sizeof kSigGetProj, (void*)&HookGetProj, (void**)&OrigGetProj);
    const bool h2 = ctx->InstallInlineHook(RVA_VIEWREBUILD, kSigViewRebuild, sizeof kSigViewRebuild, (void*)&HookViewRebuild, (void**)&OrigViewRebuild);
    const bool h3 = ctx->InstallInlineHook(RVA_RAYBUILD, kSigRayBuild, sizeof kSigRayBuild, (void*)&HookRayBuild, (void**)&OrigRayBuild);
    g_hooksOk = hc && h1 && h2;
    char b[160];
    snprintf(b, sizeof b, "3dcam loaded: camCopy=%s getProj=%s viewRebuild=%s rayBuild=%s%s", hc ? "ok" : "FAIL", h1 ? "ok" : "FAIL", h2 ? "ok" : "FAIL", h3 ? "ok" : "FAIL",
             h3 ? "" : " (targeting + shadow fit unavailable)");
    ctx->LogInfo(b);
    g_lifecycleOk = RegisterGameplay(ctx);
    camera_automap::Install(ctx,g_base,&MapFollowActive,&MapFollowHeading);
    if (!ctx->RegisterConsoleCommand("mapfollow", &CmdMapFollow, "mapfollow - toggle camera-aligned native Tab map in F10"))
        ctx->LogError("3dcam: could not register mapfollow");
    click_aim::Install(ctx,g_base,+[]() { return MapFollowActive() && GameFocused() && !camera_settings::open.load(); });
    click_aim::centerFinder=&FindCenterTarget;
    if(!ring_ui::Install(ctx,g_base,+[]() { return g_enabled.load() && g_inGame.load() && GameFocused(); },&BuildTargetRing))
        click_aim::ready.store(false); // selection must not silently lose its required ring
    if (!ctx->RegisterConsoleCommand("clickaim", &CmdClickAim, "clickaim - toggle targeting; default F/G cycle, X elites, V center, Shift+LMB select; keys configurable via gear"))
        ctx->LogError("3dcam: could not register clickaim");
    const auto saved=camera_settings::Copy();
    click_aim::enabled.store(saved.targeting && click_aim::ready.load());
    camera_automap::enabled.store(saved.mapFollow);
    // No automatic selection, target steering or passive relation hook.
    ctx->LogInfo("3dcam .22: original loader cog (frameless) + configurable keys + V center selection; clickaim opt-in (saved panel setting); no camera tracking");
    ctx->LogInfo(g_lifecycleOk ? "3dcam: F10 mouse look + experimental camera-relative WASD; native WASD bindings required" : "3dcam: lifecycle unavailable; F10 disabled");
    g_mouseThread = CreateThread(nullptr, 0, MouseThread, nullptr, 0, &g_mouseThreadId);
    if (!g_mouseThread) { ctx->LogError("3dcam: input thread failed"); return false; }
    if (!ctx->RegisterConsoleCommand("3dcam", &Cmd3dcam, "3dcam - switch the perspective 3D camera on/off")) ctx->LogError("3dcam: could not register the console command");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    camera_automap::ready.store(false);
    camera_automap::Invalidate();
    click_aim::enabled.store(false);
    ring_ui::ready.store(false);
    click_aim::ready.store(false);
    click_aim::Invalidate();
    click_aim::Publish(false,false);
    g_enabled.store(false);
    g_freeLook.store(false);
    g_quit.store(true);
    if (g_mouseThreadId) PostThreadMessageW(g_mouseThreadId, WM_QUIT, 0, 0);
    if (g_mouseThread) {
        WaitForSingleObject(g_mouseThread, INFINITE);
        CloseHandle(g_mouseThread);
        g_mouseThread = nullptr;
    }
    g_mouseThreadId = 0;
    // Service registrations, including the overlay, are cleaned up by the loader.
    g_ctx = nullptr;
}
