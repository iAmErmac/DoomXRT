#include "rt_openxr_input.h"
#include "common/engine/d_eventbase.h"
#include "common/console/c_cvars.h"
#include "common/console/keydef.h"
#include "common/engine/m_joy.h"
#include "common/rendering/hwrenderer/data/hw_vrmodes.h"
#include "common/engine/d_gui.h"
#include "common/menu/menustate.h"
#include "common/console/c_dispatch.h"
#include "printf.h"
#include "common/rendering/v_video.h"
#include <algorithm>
#include <chrono>
#include <cmath>

EXTERN_CVAR(Bool, vr_switch_sticks)
EXTERN_CVAR(Bool, vr_move_use_offhand)
EXTERN_CVAR(Float, vr_snapTurn)
EXTERN_CVAR(Int, vr_control_scheme)
EXTERN_CVAR(Bool, vr_menu_pointer)
EXTERN_CVAR(Bool, vr_enable_haptics)
EXTERN_CVAR(Float, vr_pickup_haptic_level)
EXTERN_CVAR(Float, vr_quake_haptic_level)
EXTERN_CVAR(Bool, vr_mouse_in_menu)

namespace
{
using GetSnapshot = RgResult (RGAPI_PTR*)(RgOpenXRInputSnapshotEXT*);
GetSnapshot getSnapshot = nullptr;
using ApplyHaptic = RgResult (RGAPI_PTR*)(uint32_t, float, float);
ApplyHaptic applyHaptic = nullptr;
RgOpenXRInputSnapshotEXT previous{};
bool snapTurnLatched = false;
bool pointerHeld = false;
float configuredDead[4] = { .25f,.25f,.25f,.25f };
float configuredScale[4] = { 1,1,1,1 };
EJoyAxis configuredMap[4] = { JOYAXIS_Side, JOYAXIS_Forward, JOYAXIS_None, JOYAXIS_None };
float configuredSensitivity = 1.0f;
RT_OpenXRWorldHandPose worldHandPoses[2];
float pendingViewYawDeltaDegrees = 0.0f;
float turnRate = 0.0f;
float movementYawRadians = 0.0f;
RT_OpenXRHandPose handPoses[2];
std::chrono::steady_clock::time_point hapticReady[2];
int MainHandIndex() { return vr_control_scheme < 10 ? 1 : 0; }

void EmitHaptic(int hand, float durationSeconds, float amplitude)
{
    if (amplitude <= 0.0f) return;

    const auto now = std::chrono::steady_clock::now();
    if (now < hapticReady[hand]) return;

    if (RT_OpenXRInputHaptic(hand, durationSeconds, amplitude))
        hapticReady[hand] = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(durationSeconds));
}

RgFloat2D FilterMovementStick(const RgFloat2D& stick)
{
	constexpr float deadZone = 0.10f;
	constexpr float power = 2.2f;
	const float length = std::sqrt(stick.data[0] * stick.data[0] + stick.data[1] * stick.data[1]);
	if (length <= deadZone)
	{
		return {{ 0.0f, 0.0f }};
	}

	const float filteredLength = std::pow((length - deadZone) / (1.0f - deadZone), power);
	const float divisor = std::max(length, 1.0f);
	RgFloat2D result{{ filteredLength * stick.data[0] / divisor, filteredLength * stick.data[1] / divisor }};
	if (std::fabs(result.data[0]) + std::fabs(result.data[1]) <= 0.05f)
	{
		result = {{ 0.0f, 0.0f }};
	}
	return result;
}

int64_t previousFrameTime = 0;
bool MovementUsesRight() { return vr_switch_sticks != vr_move_use_offhand; }

void PostKey(int key, bool down)
{
    event_t ev{};
    ev.type = down ? EV_KeyDown : EV_KeyUp;
    ev.data1 = key;
    ev.data2 = key;
    D_PostEvent(&ev);
}

RgFloat3D Rotate(const RgQuaternion& q, const RgFloat3D& v)
{
    const float x=q.data[0], y=q.data[1], z=q.data[2], w=q.data[3];
    return {{(1-2*y*y-2*z*z)*v.data[0] + (2*x*y-2*z*w)*v.data[1] + (2*x*z+2*y*w)*v.data[2],
             (2*x*y+2*z*w)*v.data[0] + (1-2*x*x-2*z*z)*v.data[1] + (2*y*z-2*x*w)*v.data[2],
             (2*x*z-2*y*w)*v.data[0] + (2*y*z+2*x*w)*v.data[1] + (1-2*x*x-2*y*y)*v.data[2]}};
}
void PostGui(EGUIEvent subtype, int x, int y)
{
    event_t ev{}; ev.type = EV_GUI_Event; ev.subtype = subtype; ev.data1 = static_cast<int16_t>(x); ev.data2 = static_cast<int16_t>(y); D_PostEvent(&ev);
}
void PollPointer(const RgOpenXRInputSnapshotEXT& current, const RgOpenXRInputSnapshotEXT&)
{
    const auto& pointer = MainHandIndex() == 1 ? current.right : current.left;
    if (menuactive == MENU_Off || !vr_menu_pointer || (!vr_mouse_in_menu && pointer.grip <= 0.5f) || !pointer.pose.valid || !current.virtualScreenPose.valid || current.virtualScreenSize.data[0] <= 0.01f) {
        if (pointerHeld) { PostGui(EV_GUI_LButtonUp, 0, 0); pointerHeld = false; }
        return;
    }
    RgFloat3D origin{{pointer.pose.position.data[0] - current.virtualScreenPose.position.data[0], pointer.pose.position.data[1] - current.virtualScreenPose.position.data[1], pointer.pose.position.data[2] - current.virtualScreenPose.position.data[2]}};
    const RgQuaternion& screenQ = current.virtualScreenPose.orientation;
    RgQuaternion invScreen{{-screenQ.data[0], -screenQ.data[1], -screenQ.data[2], screenQ.data[3]}};
    RgFloat3D forward{{0, 0, -1}};
    RgFloat3D worldDirection = Rotate(pointer.pose.orientation, forward);
    RgFloat3D localOrigin = Rotate(invScreen, origin);
    RgFloat3D localDirection = Rotate(invScreen, worldDirection);
    if (std::fabs(localDirection.data[2]) < 0.0001f) return;
    const float t = -localOrigin.data[2] / localDirection.data[2];
    const float x = localOrigin.data[0] + t * localDirection.data[0];
    const float y = localOrigin.data[1] + t * localDirection.data[1];
    const float u = x / current.virtualScreenSize.data[0] + 0.5f;
    const float v = 0.5f - y / current.virtualScreenSize.data[1];
    if (t <= 0 || u < 0 || u > 1 || v < 0 || v > 1) {
        if (pointerHeld) { PostGui(EV_GUI_LButtonUp, 0, 0); pointerHeld = false; }
        return;
    }
    const int px = static_cast<int>(u * screen->GetWidth());
    const int py = static_cast<int>(v * screen->GetHeight());
    PostGui(EV_GUI_MouseMove, px, py);
    const bool click = pointer.trigger > 0.5f;
    if (click != pointerHeld) { PostGui(click ? EV_GUI_LButtonDown : EV_GUI_LButtonUp, px, py); pointerHeld = click; }
}void Edge(int key, bool now, bool old)
{
    if (now != old) PostKey(key, now != RG_FALSE);
    old = now;
}

void StickEdges(const RgFloat2D& stick, int right, int left, int down, int up,
                RgFloat2D& old)
{
    auto direction = [](const RgFloat2D& v) {
        const float x = std::fabs(v.data[0]), y = std::fabs(v.data[1]);
        const float xDeadZone = configuredDead[0];
        const float yDeadZone = configuredDead[1];
        const bool xActive = x > xDeadZone;
        const bool yActive = y > yDeadZone;
        const bool xDominant = xActive && (!yActive || x - y >= 0.15f);
        const bool yDominant = yActive && (!xActive || y - x >= 0.15f);
        if (xDominant) return v.data[0] > 0.0f ? 1 : -1;
        if (yDominant) return v.data[1] < 0.0f ? 2 : 3;
        return 0;
    };
    const int now = direction(stick), was = direction(old);
    if (now != was) { if (was) PostKey(was==1?right:was==-1?left:was==2?down:up,false); if (now) PostKey(now==1?right:now==-1?left:now==2?down:up,true); }
    old = stick;
}
}

void RT_OpenXRInputBindModule(
#ifdef _WIN32
    HMODULE module
#else
    void* module
#endif
)
{
#ifdef _WIN32
    getSnapshot = module ? reinterpret_cast<GetSnapshot>(GetProcAddress(module, "rgGetOpenXRInputSnapshotEXT")) : nullptr;
    applyHaptic = module ? reinterpret_cast<ApplyHaptic>(GetProcAddress(module, "rgApplyOpenXRHapticFeedbackEXT")) : nullptr;
#else
    getSnapshot = nullptr;
    applyHaptic = nullptr;
#endif
    RT_OpenXRInputReset();
}

void ReleaseAll()
{
    const bool swap = MovementUsesRight();
    const auto& move = swap ? previous.right.stick : previous.left.stick;
    const auto& turn = swap ? previous.left.stick : previous.right.stick;
    pendingViewYawDeltaDegrees = 0.0f;
    const auto releaseStick = [](const RgFloat2D& stick, int right, int left, int down, int up) {
        constexpr float deadZone = 0.22f;
        if (stick.data[0] > deadZone) PostKey(right, false); if (stick.data[0] < -deadZone) PostKey(left, false);
        if (stick.data[1] < -deadZone) PostKey(down, false); if (stick.data[1] > deadZone) PostKey(up, false);
    };
    releaseStick(move, KEY_PAD_LTHUMB_RIGHT, KEY_PAD_LTHUMB_LEFT, KEY_PAD_LTHUMB_DOWN, KEY_PAD_LTHUMB_UP);
    if (vr_snapTurn <= 10.0f) releaseStick(turn, KEY_PAD_RTHUMB_RIGHT, KEY_PAD_RTHUMB_LEFT, KEY_PAD_RTHUMB_DOWN, KEY_PAD_RTHUMB_UP);
    if (previous.left.trigger > 0.5f) PostKey(KEY_PAD_LTRIGGER, false); if (previous.right.trigger > 0.5f) PostKey(KEY_PAD_RTRIGGER, false);
    if (previous.right.menu) PostKey(KEY_PAD_START, false); if (previous.left.menu) PostKey(KEY_PAD_BACK, false);
    if (previous.right.faceA) PostKey(KEY_PAD_A, false); if (previous.right.faceB) PostKey(KEY_PAD_B, false);
    if (previous.left.faceX) PostKey(KEY_PAD_X, false); if (previous.left.faceY) PostKey(KEY_PAD_Y, false);
    if (previous.right.thumbClick) PostKey(KEY_PAD_RTHUMB, false);
    if (previous.left.thumbClick) PostKey(KEY_PAD_LTHUMB, false);
    if (pointerHeld) { PostGui(EV_GUI_LButtonUp, 0, 0); pointerHeld = false; }
}

void RT_OpenXRInputReset()
{
    ReleaseAll();
    worldHandPoses[0] = {};
    worldHandPoses[1] = {};
    previous = {};
    hapticReady[0] = {};
    hapticReady[1] = {};
    snapTurnLatched = false;
    turnRate = 0.0f;
    movementYawRadians = 0.0f;
    handPoses[0] = {};
    handPoses[1] = {};
    previousFrameTime = 0;
}
void RT_OpenXRInputAddAxes(float axes[])
{
    if (!getSnapshot || !previous.sessionRunning || !previous.focused) return;

    const auto& moveStick = MovementUsesRight() ? previous.right.stick : previous.left.stick;
    const auto& turnStick = MovementUsesRight() ? previous.left.stick : previous.right.stick;
    const RgFloat2D filteredMove = FilterMovementStick(moveStick);
    const float localSide = -filteredMove.data[0];
    const float localForward = filteredMove.data[1];
    const float yawCos = std::cos(movementYawRadians);
    const float yawSin = std::sin(movementYawRadians);
    const float values[4] = {
        localSide * yawCos + localForward * yawSin,
        localForward * yawCos - localSide * yawSin,
        turnStick.data[0], turnStick.data[1] };
    for (int i=0; i<4; ++i) { float v=values[i], a=std::fabs(v); if (a <= configuredDead[i]) v=0; else v=(v>0?1:-1)*(a-configuredDead[i])/(1-configuredDead[i]); if (configuredMap[i] != JOYAXIS_None) axes[configuredMap[i]] += v*configuredScale[i]*configuredSensitivity; }
}
void RT_OpenXRInputPoll()
{
    if (!getSnapshot) return;
    RgOpenXRInputSnapshotEXT current{};
    current.structSize = sizeof(current);
    current.version = RG_OPENXR_INPUT_SNAPSHOT_EXT_VERSION;
    const RgResult snapshotResult = getSnapshot(&current);
    if (snapshotResult != RG_RESULT_SUCCESS || current.structSize < sizeof(current) ||
        current.version != RG_OPENXR_INPUT_SNAPSHOT_EXT_VERSION ||
        !current.focused ||
        !current.sessionRunning)
    {
        RT_OpenXRInputReset();
        return;
    }
    const auto& moveStick = MovementUsesRight() ? current.right.stick : current.left.stick;
    const auto& turnStick = MovementUsesRight() ? current.left.stick : current.right.stick;
    StickEdges(moveStick, KEY_PAD_LTHUMB_RIGHT, KEY_PAD_LTHUMB_LEFT,
               KEY_PAD_LTHUMB_DOWN, KEY_PAD_LTHUMB_UP, MovementUsesRight() ? previous.right.stick : previous.left.stick);
    if (vr_snapTurn > 10.0f)
    {
        const bool turnRight = turnStick.data[0] > 0.60f;
        const bool turnLeft = turnStick.data[0] < -0.60f;
        if (!snapTurnLatched && (turnRight || turnLeft))
        {
            const float deltaDegrees = (turnRight ? -1.0f : 1.0f) * vr_snapTurn;
            pendingViewYawDeltaDegrees -= deltaDegrees;
            snapTurnLatched = true;
        }
        if (turnStick.data[0] < 0.40f && turnStick.data[0] > -0.40f) snapTurnLatched = false;
    }
    else
    {
        const float dt = previousFrameTime ? std::clamp(float(current.frameTime - previousFrameTime) * 1.0e-9f, 0.0f, 0.1f) : 0.0f;
        constexpr float deadZone = 0.10f, maxRate = 210.0f, response = 8.0f;
        const float magnitude = std::fabs(turnStick.data[0]);
        float targetRate = 0.0f;
        if (magnitude > deadZone)
        {
            const float t = std::clamp((magnitude - deadZone) / (1.0f - deadZone), 0.0f, 1.0f);
            const float eased = t * t * (3.0f - 2.0f * t);
            targetRate = (turnStick.data[0] > 0.0f ? -1.0f : 1.0f) * maxRate * eased;
        }
        const float setting = std::clamp(float(vr_snapTurn), 0.0f, 10.0f);
        const float responseScale = setting <= 0.0f ? 15.0f : 1.0f + (10.0f - setting);
        turnRate += (targetRate - turnRate) * (1.0f - std::exp(-response * responseScale * dt));
        const float deltaDegrees = turnRate * dt;
        pendingViewYawDeltaDegrees -= deltaDegrees;
        if (magnitude <= 0.05f) turnRate = 0.0f;
    }
    Edge(KEY_PAD_LTRIGGER, current.left.trigger > 0.5f, previous.left.trigger > 0.5f);
    Edge(KEY_PAD_RTRIGGER, current.right.trigger > 0.5f, previous.right.trigger > 0.5f);
    Edge(KEY_PAD_START, current.right.menu, previous.right.menu);
    Edge(KEY_PAD_BACK, current.left.menu, previous.left.menu);
    Edge(KEY_PAD_A, current.right.faceA, previous.right.faceA);
    Edge(KEY_PAD_B, current.right.faceB, previous.right.faceB);
    Edge(KEY_PAD_X, current.left.faceX, previous.left.faceX);
    Edge(KEY_PAD_Y, current.left.faceY, previous.left.faceY);
    Edge(KEY_PAD_RTHUMB, current.right.thumbClick, previous.right.thumbClick);
    Edge(KEY_PAD_LTHUMB, current.left.thumbClick, previous.left.thumbClick);
    handPoses[0] = { current.left.pose.position, current.left.pose.orientation, current.left.pose.valid != RG_FALSE };
    handPoses[1] = { current.right.pose.position, current.right.pose.orientation, current.right.pose.valid != RG_FALSE };
    PollPointer(current, previous);
    previous = current;
    previousFrameTime = current.frameTime;
}

bool RT_OpenXRInputAvailable() { return getSnapshot != nullptr; }
bool RT_OpenXRInputIsActive() { return getSnapshot != nullptr && previous.sessionRunning && previous.focused; }
void RT_OpenXRInputConfigure(const float d[4], const float sc[4], const EJoyAxis m[4], float sensitivity) { std::copy(d, d+4, configuredDead); std::copy(sc, sc+4, configuredScale); std::copy(m, m+4, configuredMap); configuredSensitivity=sensitivity; }
float RT_OpenXRInputConsumeViewYawDeltaDegrees()
{
    const float result = pendingViewYawDeltaDegrees;
    pendingViewYawDeltaDegrees = 0.0f;
    return result;
}
bool RT_OpenXRInputGetHandPose(int hand, RT_OpenXRHandPose* outPose)
{
    if (outPose == nullptr || hand < 0 || hand > 1)
        return false;
    *outPose = handPoses[hand];
    return outPose->valid;
}

void RT_OpenXRInputSetMovementYawRadians(float yaw)
{
    movementYawRadians = yaw;
}

void RT_OpenXRInputSetWorldHandPoses(const RT_OpenXRWorldHandPose (&poses)[2])
{
    worldHandPoses[0] = poses[0];
    worldHandPoses[1] = poses[1];
}

bool RT_OpenXRInputGetWorldHandPose(int hand, RT_OpenXRWorldHandPose* outPose)
{
    if (outPose == nullptr || hand < 0 || hand > 1)
        return false;
    *outPose = worldHandPoses[hand];
    return outPose->valid;
}

bool RT_OpenXRInputGetMainWorldHandPose(RT_OpenXRWorldHandPose* outPose)
{
    return RT_OpenXRInputGetWorldHandPose(MainHandIndex(), outPose);
}

bool RT_OpenXRInputIsHandGripping(int hand)
{
    if (hand < 0 || hand > 1) return false;
    return (hand == 0 ? previous.left.grip : previous.right.grip) > 0.5f;
}

bool RT_OpenXRInputGetMainHandPose(RT_OpenXRHandPose* outPose)
{
    return RT_OpenXRInputGetHandPose(MainHandIndex(), outPose);
}
bool RT_OpenXRInputHaptic(int hand, float durationSeconds, float amplitude)
{
    if (applyHaptic == nullptr || hand < 0 || hand > 1)
        return false;
    if (!vr_enable_haptics)
    {
        applyHaptic(static_cast<uint32_t>(hand), 0.0f, 0.0f);
        return false;
    }
    return applyHaptic(static_cast<uint32_t>(hand), durationSeconds, amplitude) == RG_RESULT_SUCCESS;
}

void RT_OpenXRHapticWeaponFire()
{
    EmitHaptic(MainHandIndex(), 0.15f, 0.8f);
}

void RT_OpenXRHapticDamage(float amplitude)
{
    EmitHaptic(0, 0.2f, amplitude);
    EmitHaptic(1, 0.2f, amplitude);
}

void RT_OpenXRHapticPoison(float amplitude)
{
    EmitHaptic(0, 0.5f, amplitude);
    EmitHaptic(1, 0.5f, amplitude);
}

void RT_OpenXRHapticHeal(float amplitude)
{
    EmitHaptic(0, 0.1f, amplitude);
    EmitHaptic(1, 0.1f, amplitude);
}

void RT_OpenXRHapticPickup()
{
    EmitHaptic(0, 0.05f, vr_pickup_haptic_level);
    EmitHaptic(1, 0.05f, vr_pickup_haptic_level);
}

void RT_OpenXRHapticQuake(float leftAmplitude, float rightAmplitude)
{
    EmitHaptic(0, 0.01f, leftAmplitude * vr_quake_haptic_level);
    EmitHaptic(1, 0.01f, rightAmplitude * vr_quake_haptic_level);
}
CCMD(vr_pose_status)
{
    RT_OpenXRHandPose raw;
    RT_OpenXRWorldHandPose world;
    const bool rawValid = RT_OpenXRInputGetMainHandPose(&raw);
    const bool worldValid = RT_OpenXRInputGetMainWorldHandPose(&world);
    Printf("RT OpenXR pose: module=%d raw=%d world=%d\n", RT_OpenXRInputAvailable(), rawValid, worldValid);
    if (rawValid)
        Printf("  raw position: %.3f %.3f %.3f\n", raw.position.data[0], raw.position.data[1], raw.position.data[2]);
    if (worldValid)
        Printf("  world position: %.3f %.3f %.3f\n", world.position.data[0], world.position.data[1], world.position.data[2]);
}


CCMD(vr_haptic_test)
{
    RT_OpenXRInputHaptic(MainHandIndex(), 0.05f, 0.5f);
}
