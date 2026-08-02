#include "rt_openxr_input.h"
#include "common/engine/d_eventbase.h"
#include "common/console/c_cvars.h"
#include "common/console/keydef.h"
#include "common/engine/m_joy.h"
#include "common/rendering/hwrenderer/data/hw_vrmodes.h"
#include "common/rendering/hwrenderer/data/hw_vrwheel.h"
#include "common/engine/d_gui.h"
#include "common/menu/menustate.h"
#include "common/menu/menu.h"
#include "common/console/c_dispatch.h"
#include "printf.h"
#include "common/rendering/v_video.h"
#include <algorithm>
#include <chrono>
#include <cmath>

EXTERN_CVAR(Bool, vr_switch_sticks)
EXTERN_CVAR(Bool, vr_teleport)
EXTERN_CVAR(Float, vr_snapTurn)
EXTERN_CVAR(Int, vr_control_scheme)
EXTERN_CVAR(Int, vr_joy_mode)
EXTERN_CVAR(Bool, vr_two_handed_weapons)
EXTERN_CVAR(Bool, vr_secondary_button_mappings)
EXTERN_CVAR(Bool, vr_menu_pointer)
EXTERN_CVAR(Bool, vr_enable_haptics)
EXTERN_CVAR(Float, vr_pickup_haptic_level)
EXTERN_CVAR(Float, vr_quake_haptic_level)
EXTERN_CVAR(Float, vr_missile_haptic_level)
EXTERN_CVAR(Float, ext_haptic_level_global_intensity)
EXTERN_CVAR(Float, ext_haptic_level_damage_projectile)
EXTERN_CVAR(Float, ext_haptic_level_pickup)
EXTERN_CVAR(Float, ext_haptic_level_fire_weapon)
EXTERN_CVAR(Float, ext_haptic_level_poison)
EXTERN_CVAR(Float, ext_haptic_level_healstation)
EXTERN_CVAR(Float, ext_haptic_level_rumble)
EXTERN_CVAR(Bool, vr_mouse_in_menu)
EXTERN_CVAR(Float, vr_weaponRotate)
EXTERN_CVAR(Color, vr_menu_pointer_color)

extern RgInterface rt;
extern bool menu_allow_mouse_override;

namespace
{
using GetSnapshot = RgResult (RGAPI_PTR*)(RgOpenXRInputSnapshotEXT*);
GetSnapshot getSnapshot = nullptr;
using ApplyHaptic = RgResult (RGAPI_PTR*)(uint32_t, float, float);
ApplyHaptic applyHaptic = nullptr;
using SetMenuPointerBeam = RgResult (RGAPI_PTR*)(const RgOpenXRMenuPointerBeamEXT*);
SetMenuPointerBeam setMenuPointerBeam = nullptr;
RgOpenXRInputSnapshotEXT previous{};
bool snapTurnLatched = false;
bool pointerHeld = false;
bool pointerRightHeld = false;
bool pointerHadPos = false;
int pointerLastX = 0;
int pointerLastY = 0;
bool pointerWheelNeutral = true;
std::chrono::steady_clock::time_point pointerWheelCooldownUntil{};
bool pointerSuppressTriggerUntilRelease = false;
int pointerX = 0;
int pointerY = 0;
bool pointerActive = false;
bool previousSecondaryButtonMappings = false;
float configuredDead[4] = { .25f,.25f,.25f,.25f };
RT_OpenXRWorldHandPose worldHandPoses[2];
float pendingViewYawDeltaDegrees = 0.0f;
float snapTurnOffsetDegrees = 0.0f;
float pendingRoomScaleX = 0.0f;
float pendingRoomScaleY = 0.0f;
float pendingHeadHeightDelta = 0.0f;
bool teleportReady = false;
bool teleportTrigger = false;
bool teleportTargetValid = false;
float teleportTarget[3]{};
float turnRate = 0.0f;
RT_OpenXRHandPose handPoses[2];
std::chrono::steady_clock::time_point hapticReady[2];
int MainHandIndex() { return vr_control_scheme < 10 ? 1 : 0; }
int OffHandIndex() { return MainHandIndex() == 0 ? 1 : 0; }

float ScaleHaptic(float amplitude, float eventScale)
{
    return std::clamp(amplitude * eventScale * (float)ext_haptic_level_global_intensity, 0.0f, 1.0f);
}

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
bool MovementUsesRight()
{
    // VR OpenXR: movement is on the offhand stick by default, and
    // vr_switch_sticks exchanges the semantic main/offhand stick roles.
    const int movementHand = vr_switch_sticks ? MainHandIndex() : OffHandIndex();
    return movementHand == 1;
}

void PostKey(int key, bool down)
{
    event_t ev{};
    ev.type = down ? EV_KeyDown : EV_KeyUp;
    ev.data1 = key;

    D_PostEvent(&ev);
}

RgFloat3D Rotate(const RgQuaternion& q, const RgFloat3D& v)
{
    const float x=q.data[0], y=q.data[1], z=q.data[2], w=q.data[3];
    return {{(1-2*y*y-2*z*z)*v.data[0] + (2*x*y-2*z*w)*v.data[1] + (2*x*z+2*y*w)*v.data[2],
             (2*x*y+2*z*w)*v.data[0] + (1-2*x*x-2*z*z)*v.data[1] + (2*y*z-2*x*w)*v.data[2],
             (2*x*z-2*y*w)*v.data[0] + (2*y*z+2*x*w)*v.data[1] + (1-2*x*x-2*y*y)*v.data[2]}};
}
RgQuaternion Multiply(const RgQuaternion& a, const RgQuaternion& b)
{
    return {{a.data[3] * b.data[0] + a.data[0] * b.data[3] + a.data[1] * b.data[2] - a.data[2] * b.data[1],
             a.data[3] * b.data[1] - a.data[0] * b.data[2] + a.data[1] * b.data[3] + a.data[2] * b.data[0],
             a.data[3] * b.data[2] + a.data[0] * b.data[1] - a.data[1] * b.data[0] + a.data[2] * b.data[3],
             a.data[3] * b.data[3] - a.data[0] * b.data[0] - a.data[1] * b.data[1] - a.data[2] * b.data[2]}};
}
float Dot(const RgFloat3D& a, const RgFloat3D& b) { return a.data[0] * b.data[0] + a.data[1] * b.data[1] + a.data[2] * b.data[2]; }
RgFloat3D Cross(const RgFloat3D& a, const RgFloat3D& b) { return {{a.data[1] * b.data[2] - a.data[2] * b.data[1], a.data[2] * b.data[0] - a.data[0] * b.data[2], a.data[0] * b.data[1] - a.data[1] * b.data[0]}}; }
RgFloat3D Normalize(const RgFloat3D& value)
{
    const float length = std::sqrt(Dot(value, value));
    return length > 0.00001f ? RgFloat3D{{value.data[0] / length, value.data[1] / length, value.data[2] / length}} : RgFloat3D{{0, 0, -1}};
}
RgQuaternion QuaternionFromBasis(const RgFloat3D& x, const RgFloat3D& y, const RgFloat3D& z)
{
    const float trace = x.data[0] + y.data[1] + z.data[2];
    RgQuaternion q{};
    if (trace > 0.0f) { const float s = std::sqrt(trace + 1.0f) * 2.0f; q = {{(y.data[2] - z.data[1]) / s, (z.data[0] - x.data[2]) / s, (x.data[1] - y.data[0]) / s, 0.25f * s}}; }
    else if (x.data[0] > y.data[1] && x.data[0] > z.data[2]) { const float s = std::sqrt(1.0f + x.data[0] - y.data[1] - z.data[2]) * 2.0f; q = {{0.25f * s, (x.data[1] + y.data[0]) / s, (x.data[2] + z.data[0]) / s, (y.data[2] - z.data[1]) / s}}; }
    else if (y.data[1] > z.data[2]) { const float s = std::sqrt(1.0f + y.data[1] - x.data[0] - z.data[2]) * 2.0f; q = {{(x.data[1] + y.data[0]) / s, 0.25f * s, (y.data[2] + z.data[1]) / s, (z.data[0] - x.data[2]) / s}}; }
    else { const float s = std::sqrt(1.0f + z.data[2] - x.data[0] - y.data[1]) * 2.0f; q = {{(x.data[2] + z.data[0]) / s, (y.data[2] + z.data[1]) / s, 0.25f * s, (x.data[1] - y.data[0]) / s}}; }
    return q;
}
void PostGui(EGUIEvent subtype, int x, int y)
{
    const bool virtualMouseEvent = subtype >= EV_GUI_FirstMouseEvent && subtype <= EV_GUI_LastMouseEvent;
    event_t ev{}; ev.type = EV_GUI_Event; ev.subtype = subtype; ev.data1 = static_cast<int16_t>(x); ev.data2 = static_cast<int16_t>(y); ev.data3 = virtualMouseEvent ? GUI_MOUSE_VIRTUAL : 0; D_PostEvent(&ev);
}
void SubmitPointerBeam(const RgOpenXRMenuPointerBeamEXT& beam)
{
    if (rt.rgSetOpenXRMenuPointerBeamEXT) rt.rgSetOpenXRMenuPointerBeamEXT(&beam);
    else if (setMenuPointerBeam) setMenuPointerBeam(&beam);
}
void PostPointerWheel(const RgOpenXRControllerStateEXT& pointer)
{
    if (vr_joy_mode != 1) return;
    const float y = pointer.stick.data[1];
    const auto now = std::chrono::steady_clock::now();
    if (std::fabs(y) < 0.35f) pointerWheelNeutral = true;
    if (pointerWheelNeutral && now >= pointerWheelCooldownUntil)
    {
        if (y > 0.85f)
        {
            PostGui(EV_GUI_WheelUp, pointerX, pointerY);
            pointerWheelNeutral = false;
            pointerWheelCooldownUntil = now + std::chrono::milliseconds(160);
        }
        else if (y < -0.85f)
        {
            PostGui(EV_GUI_WheelDown, pointerX, pointerY);
            pointerWheelNeutral = false;
            pointerWheelCooldownUntil = now + std::chrono::milliseconds(160);
        }
    }
}
void PollPointer(const RgOpenXRInputSnapshotEXT& current, const RgOpenXRInputSnapshotEXT&)
{
    pointerActive = false;
    menu_allow_mouse_override = false;
    const auto& pointer = current.right;
    const bool pointerEnabled = menuactive != MENU_Off && menuactive != MENU_WaitKey && vr_menu_pointer && (vr_mouse_in_menu || pointer.grip > 0.5f);
    if (!pointerEnabled || !pointer.pose.valid || !current.virtualScreenPose.valid || current.virtualScreenSize.data[0] <= 0.01f)
    {
        if (pointerHeld) { PostGui(EV_GUI_LButtonUp, 0, 0); pointerHeld = false; }
        if (pointerRightHeld) { PostGui(EV_GUI_RButtonUp, 0, 0); pointerRightHeld = false; }
        pointerWheelNeutral = true;
        pointerWheelCooldownUntil = {};
        pointerHadPos = false;
        SubmitPointerBeam({RG_STRUCTURE_TYPE_OPENXR_MENU_POINTER_BEAM_EXT, nullptr, RG_FALSE});
        return;
    }
    const RgFloat3D relativeOrigin{{pointer.pose.position.data[0] - current.virtualScreenPose.position.data[0], pointer.pose.position.data[1] - current.virtualScreenPose.position.data[1], pointer.pose.position.data[2] - current.virtualScreenPose.position.data[2]}};
    const RgQuaternion& screenQ = current.virtualScreenPose.orientation;
    const RgQuaternion inverseScreen{{-screenQ.data[0], -screenQ.data[1], -screenQ.data[2], screenQ.data[3]}};
    constexpr float pi = 3.14159265358979323846f;
    const float pointerRotation = -(vr_weaponRotate * 2.0f) * pi / 180.0f;
    const RgQuaternion pointerAlign{{0, 0, std::sin(pointerRotation * 0.5f), std::cos(pointerRotation * 0.5f)}};
    const RgQuaternion pointerOrientation = Multiply(pointer.pose.orientation, pointerAlign);
    const RgFloat3D worldDirection = Rotate(pointerOrientation, {{0, 0, -1}});
    const RgFloat3D localOrigin = Rotate(inverseScreen, relativeOrigin);
    const RgFloat3D localDirection = Rotate(inverseScreen, worldDirection);
    if (std::fabs(localDirection.data[2]) < 0.0001f) { SubmitPointerBeam({RG_STRUCTURE_TYPE_OPENXR_MENU_POINTER_BEAM_EXT, nullptr, RG_FALSE}); PostPointerWheel(pointer); return; }
    const float t = -localOrigin.data[2] / localDirection.data[2];
    if (t <= 0.0f) { SubmitPointerBeam({RG_STRUCTURE_TYPE_OPENXR_MENU_POINTER_BEAM_EXT, nullptr, RG_FALSE}); PostPointerWheel(pointer); return; }
    const float localX = localOrigin.data[0] + t * localDirection.data[0];
    const float localY = localOrigin.data[1] + t * localDirection.data[1];
    const float unclampedU = localX / current.virtualScreenSize.data[0] + 0.5f;
    const float unclampedV = 0.5f - localY / current.virtualScreenSize.data[1];
    const bool dragging = pointerHeld || pointerRightHeld;
    const bool inside = unclampedU >= 0.0f && unclampedU <= 1.0f && unclampedV >= 0.0f && unclampedV <= 1.0f;
    const int px = static_cast<int>(std::clamp(unclampedU, 0.0f, 1.0f) * (screen->GetWidth() - 1));
    const int py = static_cast<int>(std::clamp(unclampedV, 0.0f, 1.0f) * (screen->GetHeight() - 1));
    const RgFloat3D hitPoint{{pointer.pose.position.data[0] + worldDirection.data[0] * t, pointer.pose.position.data[1] + worldDirection.data[1] * t, pointer.pose.position.data[2] + worldDirection.data[2] * t}};
    const RgFloat3D beamVector{{hitPoint.data[0] - pointer.pose.position.data[0], hitPoint.data[1] - pointer.pose.position.data[1], hitPoint.data[2] - pointer.pose.position.data[2]}};
    const float beamLength = std::sqrt(Dot(beamVector, beamVector));
    if (beamLength > 0.01f)
    {
        const RgFloat3D beamDirection = Normalize(beamVector);
        const RgFloat3D beamCenter{{pointer.pose.position.data[0] + beamDirection.data[0] * beamLength * 0.5f, pointer.pose.position.data[1] + beamDirection.data[1] * beamLength * 0.5f, pointer.pose.position.data[2] + beamDirection.data[2] * beamLength * 0.5f}};
        const RgFloat3D viewer = current.headPose.valid ? current.headPose.position : current.virtualScreenPose.position;
        RgFloat3D xAxis = Normalize(Cross(Normalize({{viewer.data[0] - beamCenter.data[0], viewer.data[1] - beamCenter.data[1], viewer.data[2] - beamCenter.data[2]} }), beamDirection));
        if (Dot(xAxis, xAxis) < 0.0001f) xAxis = Normalize(Cross({{0, 1, 0}}, beamDirection));
        const RgFloat3D zAxis = Normalize(Cross(beamDirection, xAxis));
        const int color = int(vr_menu_pointer_color);
        SubmitPointerBeam({RG_STRUCTURE_TYPE_OPENXR_MENU_POINTER_BEAM_EXT, nullptr, RG_TRUE, {beamCenter, QuaternionFromBasis(beamDirection, xAxis, zAxis), RG_TRUE}, beamLength, {{((color >> 16) & 255) / 255.0f, ((color >> 8) & 255) / 255.0f, (color & 255) / 255.0f, 1.0f}}});
    }
    if (inside || dragging)
    {
        pointerActive = true;
        pointerX = px;
        pointerY = py;
        if (!pointerHadPos || px != pointerLastX || py != pointerLastY)
        {
            PostGui(EV_GUI_MouseMove, px, py);
            pointerLastX = px;
            pointerLastY = py;
        }
        pointerHadPos = true;
        const bool click = pointer.trigger > 0.5f;
        if (click != pointerHeld) { PostGui(click ? EV_GUI_LButtonDown : EV_GUI_LButtonUp, pointerLastX, pointerLastY); pointerHeld = click; if (click) pointerSuppressTriggerUntilRelease = true; }
        const bool rightClick = vr_mouse_in_menu && pointer.grip > 0.5f;
        if (rightClick != pointerRightHeld) { PostGui(rightClick ? EV_GUI_RButtonDown : EV_GUI_RButtonUp, pointerLastX, pointerLastY); pointerRightHeld = rightClick; }
    }
    else
    {
        if (pointerHeld) { PostGui(EV_GUI_LButtonUp, 0, 0); pointerHeld = false; }
        if (pointerRightHeld) { PostGui(EV_GUI_RButtonUp, 0, 0); pointerRightHeld = false; }
        pointerHadPos = false;
    }
    PostPointerWheel(pointer);
}

void Edge(int key, bool now, bool old)
{
    if (now != old) PostKey(key, now != RG_FALSE);
}

void RemappedEdge(bool oldPressed, bool newPressed, bool oldModifier, bool newModifier, int baseKey, int modifiedKey)
{
    const int oldKey = oldModifier ? modifiedKey : baseKey;
    const int newKey = newModifier ? modifiedKey : baseKey;
    if (oldKey == newKey)
    {
        Edge(newKey, newPressed, oldPressed);
        return;
    }
    if (oldPressed) PostKey(oldKey, false);
    if (newPressed) PostKey(newKey, true);
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

void RemappedStickEdges(const RgFloat2D& current, const RgFloat2D& old, bool oldModifier, bool newModifier,
    int baseRight, int baseLeft, int baseDown, int baseUp, int modifiedRight, int modifiedLeft, int modifiedDown, int modifiedUp)
{
    const bool strictRightVertical =
        (baseDown == KEY_JOYAXIS4MINUS && baseUp == KEY_JOYAXIS4PLUS) ||
        (modifiedDown == KEY_JOYAXIS8MINUS && modifiedUp == KEY_JOYAXIS8PLUS);
    const float xDeadZone = 0.25f;
    const float yDeadZone = strictRightVertical ? 0.72f : 0.25f;
    const float horizontalDominanceMargin = strictRightVertical ? 0.00f : 0.15f;
    const float verticalDominanceMargin = strictRightVertical ? 0.30f : 0.15f;
    auto resolveCardinalStates = [&](const RgFloat2D& stick, bool& left, bool& right, bool& down, bool& up) {
        const float x = std::fabs(stick.data[0]), y = std::fabs(stick.data[1]);
        const bool xActive = x > xDeadZone;
        const bool yActive = y > yDeadZone;
        const bool xDominant = xActive && (!yActive || x - y >= horizontalDominanceMargin);
        const bool yDominant = yActive && (!xActive || y - x >= verticalDominanceMargin);
        left = xDominant && stick.data[0] < 0.0f;
        right = xDominant && stick.data[0] > 0.0f;
        down = yDominant && stick.data[1] < 0.0f;
        up = yDominant && stick.data[1] > 0.0f;
    };
    bool oldLeft = false, oldRight = false, oldDown = false, oldUp = false;
    bool newLeft = false, newRight = false, newDown = false, newUp = false;
    resolveCardinalStates(old, oldLeft, oldRight, oldDown, oldUp);
    resolveCardinalStates(current, newLeft, newRight, newDown, newUp);
    RemappedEdge(oldRight, newRight, oldModifier, newModifier, baseRight, modifiedRight);
    RemappedEdge(oldLeft, newLeft, oldModifier, newModifier, baseLeft, modifiedLeft);
    RemappedEdge(oldDown, newDown, oldModifier, newModifier, baseDown, modifiedDown);
    RemappedEdge(oldUp, newUp, oldModifier, newModifier, baseUp, modifiedUp);
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
    setMenuPointerBeam = module ? reinterpret_cast<SetMenuPointerBeam>(GetProcAddress(module, "rgSetOpenXRMenuPointerBeamEXT")) : nullptr;
#else
    getSnapshot = nullptr;
    applyHaptic = nullptr;
    setMenuPointerBeam = nullptr;
#endif
    RT_OpenXRInputReset();
}

void ReleaseAll()
{
    const bool swap = MovementUsesRight();
    const auto& move = swap ? previous.right.stick : previous.left.stick;
    const auto& turn = swap ? previous.left.stick : previous.right.stick;
    pendingViewYawDeltaDegrees = 0.0f;
    snapTurnOffsetDegrees = 0.0f;
    pendingRoomScaleX = 0.0f;
    pendingRoomScaleY = 0.0f;
    pendingHeadHeightDelta = 0.0f;
    teleportReady = false;
    teleportTrigger = false;
    teleportTargetValid = false;
    teleportTarget[0] = teleportTarget[1] = teleportTarget[2] = 0.0f;
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
    for (int key = KEY_JOYAXIS1PLUS; key <= KEY_JOYAXIS8MINUS; ++key) PostKey(key, false);
    PostKey(KEY_PAD_LSHOULDER, false); PostKey(KEY_PAD_RSHOULDER, false);
    PostKey(KEY_LSHIFT, false); PostKey(KEY_LALT, false); PostKey(KEY_ENTER, false); PostKey(KEY_SPACE, false);
    PostKey(KEY_TAB, false); PostKey(KEY_HOME, false); PostKey(KEY_BACKSPACE, false); PostKey(KEY_PGDN, false); PostKey(KEY_PGUP, false); PostKey(KEY_PAD_DPAD_UP, false);
    if (pointerHeld) { PostGui(EV_GUI_LButtonUp, 0, 0); pointerHeld = false; }
    if (pointerRightHeld) { PostGui(EV_GUI_RButtonUp, 0, 0); pointerRightHeld = false; }
    pointerWheelNeutral = true;
    pointerWheelCooldownUntil = {};
    pointerSuppressTriggerUntilRelease = false;
}

void RT_OpenXRInputReset()
{
    VRWheel_Reset();
    ReleaseAll();
    worldHandPoses[0] = {};
    worldHandPoses[1] = {};
    previous = {};
    hapticReady[0] = {};
    hapticReady[1] = {};
    snapTurnLatched = false;
    turnRate = 0.0f;
    handPoses[0] = {};
    handPoses[1] = {};
    previousSecondaryButtonMappings = vr_secondary_button_mappings;
    previousFrameTime = 0;
}

void RT_OpenXRInputClearTrackingDeltas()
{
    pendingRoomScaleX = 0.0f;
    pendingRoomScaleY = 0.0f;
    pendingHeadHeightDelta = 0.0f;
    teleportTargetValid = false;
    teleportTarget[0] = teleportTarget[1] = teleportTarget[2] = 0.0f;
    VRWheel_Reset();
}

// VR supplies OpenXR locomotion directly to G_BuildTiccmd. Do not route
// it through the generic joystick axis layer, which can bypass the ordinary
// player movement command and leave the pawn in PLAYA while moving.
void RT_OpenXRInputAddAxes(float axes[])
{
    (void)axes;
}

bool RT_OpenXRInputGetStickMove(float* forward, float* side)
{
    if (forward == nullptr || side == nullptr || !getSnapshot || !previous.sessionRunning ||
        !previous.focused || menuactive != MENU_Off)
    {
        return false;
    }

    const auto& moveStick = MovementUsesRight() ? previous.right.stick : previous.left.stick;
    const RgFloat2D filteredMove = FilterMovementStick(moveStick);
    *forward = filteredMove.data[1];
    *side = filteredMove.data[0];
    return true;
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
    const bool secondaryMappings = vr_secondary_button_mappings;
    const int mainHand = MainHandIndex();
    const bool dominantGripModifier = secondaryMappings && (mainHand == 0 ? current.left.grip : current.right.grip) > 0.5f;
    if (vr_teleport)
    {
        if (moveStick.data[1] > 0.7f && !teleportReady) teleportReady = true;
        else if (moveStick.data[1] < 0.7f && teleportReady) { teleportReady = false; teleportTrigger = true; }
    }
    else
    {
        teleportReady = false;
        teleportTrigger = false;
    }
    if (!vr_teleport) StickEdges(moveStick, KEY_PAD_LTHUMB_RIGHT, KEY_PAD_LTHUMB_LEFT,
               KEY_PAD_LTHUMB_DOWN, KEY_PAD_LTHUMB_UP, MovementUsesRight() ? previous.right.stick : previous.left.stick);
    if (dominantGripModifier)
    {
        snapTurnLatched = false;
        turnRate = 0.0f;
    }
    else if (vr_snapTurn > 10.0f)
    {
        const bool turnRight = turnStick.data[0] > 0.60f;
        const bool turnLeft = turnStick.data[0] < -0.60f;
        if (!snapTurnLatched && (turnRight || turnLeft))
        {
            const float deltaDegrees = (turnRight ? -1.0f : 1.0f) * vr_snapTurn;
            pendingViewYawDeltaDegrees -= deltaDegrees;
            snapTurnOffsetDegrees += deltaDegrees;
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
        snapTurnOffsetDegrees += deltaDegrees;
        if (magnitude <= 0.05f) turnRate = 0.0f;
    }
    const bool oldModifier = previousSecondaryButtonMappings && (mainHand == 0 ? previous.left.grip : previous.right.grip) > 0.5f;
    const bool newModifier = secondaryMappings && (mainHand == 0 ? current.left.grip : current.right.grip) > 0.5f;
    const int pointerHand = 1;
    const auto& pointer = current.right;
    const bool pointerEnabled = menuactive != MENU_Off && menuactive != MENU_WaitKey && vr_menu_pointer && (vr_mouse_in_menu || pointer.grip > 0.5f);
    const bool pointerConsumesTrigger = menuactive != MENU_Off && menuactive != MENU_WaitKey && vr_menu_pointer && (pointerEnabled || pointerSuppressTriggerUntilRelease || pointerHeld);

    for (int hand = 0; hand < 2; ++hand)
    {
        const auto& now = hand == 0 ? current.left : current.right;
        const auto& old = hand == 0 ? previous.left : previous.right;
        const bool dominant = hand == mainHand;
        const int triggerBase = dominant ? KEY_PAD_RTRIGGER : KEY_LSHIFT;
        const int triggerAlt = dominant ? KEY_PAD_LTRIGGER : KEY_LALT;
        const int gripKey = hand == 0 ? KEY_PAD_LSHOULDER : KEY_PAD_RSHOULDER;
        const int thumbBase = dominant ? KEY_ENTER : KEY_SPACE;
        const int thumbAlt = dominant ? KEY_TAB : KEY_HOME;
        const int face1Base = hand == 0 ? KEY_PAD_X : KEY_PAD_A;
        const int face2Base = hand == 0 ? KEY_PAD_Y : KEY_PAD_B;
        const int face1Alt = dominant ? KEY_PAD_LTHUMB : KEY_PGDN;
        const int face2Alt = dominant ? KEY_BACKSPACE : KEY_PGUP;

        RemappedEdge(old.trigger > 0.5f, pointerConsumesTrigger && hand == pointerHand ? false : now.trigger > 0.5f,
            oldModifier, newModifier, triggerBase, triggerAlt);
        if (dominant)
        {
            RemappedEdge(old.grip > 0.5f, secondaryMappings ? false : now.grip > 0.5f,
                false, false, gripKey, gripKey);
        }
        else if (secondaryMappings)
        {
            // Match VR: offhand grip is a shifted DPAD_UP action only
            // while the dominant grip is held and two-handed stabilization is off.
            const bool oldGripCombo = old.grip > 0.5f && oldModifier && !vr_two_handed_weapons;
            const bool newGripCombo = now.grip > 0.5f && newModifier && !vr_two_handed_weapons;
            RemappedEdge(oldGripCombo, newGripCombo, false, false,
                KEY_PAD_DPAD_UP, KEY_PAD_DPAD_UP);
        }
        else
        {
            RemappedEdge(old.grip > 0.5f, now.grip > 0.5f,
                false, false, gripKey, gripKey);
        }
        RemappedEdge(old.thumbClick, now.thumbClick, oldModifier, newModifier, thumbBase, thumbAlt);
        if (menuactive == MENU_WaitKey) Edge(hand == 0 ? KEY_PAD_LTHUMB : KEY_PAD_RTHUMB, now.thumbClick, old.thumbClick);
        const bool nowFace1 = hand == 0 ? now.faceX : now.faceA;
        const bool oldFace1 = hand == 0 ? old.faceX : old.faceA;
        const bool nowFace2 = hand == 0 ? now.faceY : now.faceB;
        const bool oldFace2 = hand == 0 ? old.faceY : old.faceB;
        RemappedEdge(oldFace1, nowFace1, oldModifier, newModifier, face1Base, face1Alt);
        RemappedEdge(oldFace2, nowFace2, oldModifier, newModifier, face2Base, face2Alt);

        // VR emits virtual stick-direction keys for both controllers while
        // playing. In menus the right stick stays reserved for pointer input,
        // except when the controls menu is capturing a binding.
        const bool emitStickKeys = menuactive == MENU_Off || menuactive == MENU_WaitKey || hand == 0;
        if (vr_joy_mode == 1 && emitStickKeys)
        {
            const int baseRight = hand == 0 ? KEY_JOYAXIS1PLUS : KEY_JOYAXIS3PLUS;
            const int baseLeft = hand == 0 ? KEY_JOYAXIS1MINUS : KEY_JOYAXIS3MINUS;
            const int baseDown = hand == 0 ? KEY_JOYAXIS2MINUS : KEY_JOYAXIS4MINUS;
            const int baseUp = hand == 0 ? KEY_JOYAXIS2PLUS : KEY_JOYAXIS4PLUS;
            const int modifiedRight = hand == 0 ? KEY_JOYAXIS5PLUS : KEY_JOYAXIS7PLUS;
            const int modifiedLeft = hand == 0 ? KEY_JOYAXIS5MINUS : KEY_JOYAXIS7MINUS;
            const int modifiedDown = hand == 0 ? KEY_JOYAXIS6MINUS : KEY_JOYAXIS8MINUS;
            const int modifiedUp = hand == 0 ? KEY_JOYAXIS6PLUS : KEY_JOYAXIS8PLUS;
            RemappedStickEdges(now.stick, old.stick, oldModifier, newModifier,
                baseRight, baseLeft, baseDown, baseUp, modifiedRight, modifiedLeft, modifiedDown, modifiedUp);
        }
    }
    Edge(KEY_PAD_START, current.right.menu, previous.right.menu);
    Edge(KEY_PAD_BACK, current.left.menu, previous.left.menu);
    handPoses[0] = { current.left.pose.position, current.left.pose.orientation, current.left.pose.valid != RG_FALSE };
    handPoses[1] = { current.right.pose.position, current.right.pose.orientation, current.right.pose.valid != RG_FALSE };
    PollPointer(current, previous);
    previous = current;
    previousSecondaryButtonMappings = secondaryMappings;
    previousFrameTime = current.frameTime;
}

bool RT_OpenXRInputAvailable() { return getSnapshot != nullptr; }
bool RT_OpenXRInputIsActive() { return getSnapshot != nullptr && previous.sessionRunning && previous.focused; }
void RT_OpenXRInputConfigure(const float d[4], const float sc[4], const EJoyAxis m[4], float sensitivity) { std::copy(d, d + 2, configuredDead); (void)sc; (void)m; (void)sensitivity; }
void RT_OpenXRInputAddViewYawDeltaDegrees(float delta) { pendingViewYawDeltaDegrees += delta; }
float RT_OpenXRInputGetSnapTurnOffsetDegrees() { return snapTurnOffsetDegrees; }
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

void RT_OpenXRInputAddRoomScaleDelta(float worldX, float worldY)
{
    if (!RT_OpenXRInputIsActive()) return;
    pendingRoomScaleX += worldX;
    pendingRoomScaleY += worldY;
}

void RT_OpenXRInputConsumeRoomScaleDelta(float* worldX, float* worldY)
{
    if (worldX == nullptr || worldY == nullptr) return;
    *worldX = pendingRoomScaleX;
    *worldY = pendingRoomScaleY;
    pendingRoomScaleX = 0.0f;
    pendingRoomScaleY = 0.0f;
}

void RT_OpenXRInputSetHeadHeightDelta(float worldZ)
{
    pendingHeadHeightDelta = worldZ;
}

float RT_OpenXRInputConsumeHeadHeightDelta()
{
    // Keep the latest pose value: render frames need not precede every tic.
    return pendingHeadHeightDelta;
}

bool RT_OpenXRInputGetTeleportState(bool* aiming)
{
    if (aiming != nullptr) *aiming = vr_teleport && teleportReady;
    const bool result = vr_teleport && teleportTrigger;
    teleportTrigger = false;
    return result;
}

void RT_OpenXRInputSetTeleportTarget(bool valid, float x, float y, float z)
{
    teleportTargetValid = valid;
    teleportTarget[0] = x;
    teleportTarget[1] = y;
    teleportTarget[2] = z;
}
bool RT_OpenXRInputGetTeleportLocation(float* x, float* y, float* z)
{
    if (!teleportTargetValid || !vr_teleport || !teleportReady || x == nullptr || y == nullptr || z == nullptr) return false;
    *x = teleportTarget[0];
    *y = teleportTarget[1];
    *z = teleportTarget[2];
    return true;
}

void RT_OpenXRInputSetWorldHandPoses(const RT_OpenXRWorldHandPose (&poses)[2])
{
    worldHandPoses[0] = poses[0];
    worldHandPoses[1] = poses[1];
}

bool RT_OpenXRInputGetMenuPointer(int* x, int* y)
{
    if (!pointerActive || x == nullptr || y == nullptr) return false;
    *x = pointerX;
    *y = pointerY;
    return true;
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
    EmitHaptic(MainHandIndex(), 0.15f, ScaleHaptic(0.8f, (float)ext_haptic_level_fire_weapon));
}

void RT_OpenXRHapticWeaponFireHand(int hand)
{
    EmitHaptic(hand, 0.15f, ScaleHaptic(0.8f, (float)ext_haptic_level_fire_weapon));
}

void RT_OpenXRHapticMissileFire()
{
    EmitHaptic(MainHandIndex(), 0.15f, ScaleHaptic((float)vr_missile_haptic_level, (float)ext_haptic_level_fire_weapon));
}

void RT_OpenXRHapticDamage(float amplitude)
{
    EmitHaptic(0, 0.2f, ScaleHaptic(amplitude, (float)ext_haptic_level_damage_projectile));
    EmitHaptic(1, 0.2f, ScaleHaptic(amplitude, (float)ext_haptic_level_damage_projectile));
}

void RT_OpenXRHapticPoison(float amplitude)
{
    EmitHaptic(0, 0.5f, ScaleHaptic(amplitude, (float)ext_haptic_level_poison));
    EmitHaptic(1, 0.5f, ScaleHaptic(amplitude, (float)ext_haptic_level_poison));
}

void RT_OpenXRHapticHeal(float amplitude)
{
    EmitHaptic(0, 0.1f, ScaleHaptic(amplitude, (float)ext_haptic_level_healstation));
    EmitHaptic(1, 0.1f, ScaleHaptic(amplitude, (float)ext_haptic_level_healstation));
}

void RT_OpenXRHapticPickup()
{
    EmitHaptic(0, 0.05f, ScaleHaptic((float)vr_pickup_haptic_level, (float)ext_haptic_level_pickup));
    EmitHaptic(1, 0.05f, ScaleHaptic((float)vr_pickup_haptic_level, (float)ext_haptic_level_pickup));
}

void RT_OpenXRHapticQuake(float leftAmplitude, float rightAmplitude)
{
    EmitHaptic(0, 0.01f, ScaleHaptic(leftAmplitude * (float)vr_quake_haptic_level, (float)ext_haptic_level_rumble));
    EmitHaptic(1, 0.01f, ScaleHaptic(rightAmplitude * (float)vr_quake_haptic_level, (float)ext_haptic_level_rumble));
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
