#include "rt_openxr_input.h"
#include "common/engine/d_eventbase.h"
#include "common/engine/printf.h"
#include "common/console/c_cvars.h"
#include "common/console/keydef.h"
#include "common/engine/m_joy.h"
#include "common/rendering/hwrenderer/data/hw_vrmodes.h"
#include "common/engine/d_gui.h"
#include "common/menu/menustate.h"
#include "common/rendering/v_video.h"
#include <algorithm>
#include <cmath>

EXTERN_CVAR(Bool, vr_switch_sticks)
EXTERN_CVAR(Float, vr_snapTurn)

namespace
{
using GetSnapshot = RgResult (RGAPI_PTR*)(RgOpenXRInputSnapshotEXT*);
GetSnapshot getSnapshot = nullptr;
RgOpenXRInputSnapshotEXT previous{};
bool snapTurnLatched = false;
bool pointerHeld = false;
bool loggedSnapshot = false;

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
    if (menuactive == MENU_Off || current.right.grip <= 0.5f || !current.right.pose.valid || !current.virtualScreenPose.valid || current.virtualScreenSize.data[0] <= 0.01f) {
        if (pointerHeld) { PostGui(EV_GUI_LButtonUp, 0, 0); pointerHeld = false; }
        return;
    }
    RgFloat3D origin{{current.right.pose.position.data[0] - current.virtualScreenPose.position.data[0], current.right.pose.position.data[1] - current.virtualScreenPose.position.data[1], current.right.pose.position.data[2] - current.virtualScreenPose.position.data[2]}};
    const RgQuaternion& screenQ = current.virtualScreenPose.orientation;
    RgQuaternion invScreen{{-screenQ.data[0], -screenQ.data[1], -screenQ.data[2], screenQ.data[3]}};
    RgFloat3D forward{{0, 0, -1}};
    RgFloat3D worldDirection = Rotate(current.right.pose.orientation, forward);
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
    const bool click = current.right.grip > 0.5f && current.right.trigger > 0.5f;
    if (click != pointerHeld) { PostGui(click ? EV_GUI_LButtonDown : EV_GUI_LButtonUp, px, py); pointerHeld = click; }
}void Edge(int key, bool now, bool old)
{
    if (now != old) PostKey(key, now != RG_FALSE);
    old = now;
}

void StickEdges(const RgFloat2D& stick, int right, int left, int down, int up,
                RgFloat2D& old)
{
    constexpr float deadZone = 0.22f;
    const bool r = stick.data[0] > deadZone;
    const bool l = stick.data[0] < -deadZone;
    const bool d = stick.data[1] < -deadZone;
    const bool u = stick.data[1] > deadZone;
    const bool orr = old.data[0] > deadZone;
    const bool oll = old.data[0] < -deadZone;
    const bool od = old.data[1] < -deadZone;
    const bool ou = old.data[1] > deadZone;
    if (r != orr) PostKey(right, r); if (l != oll) PostKey(left, l);
    if (d != od) PostKey(down, d); if (u != ou) PostKey(up, u);
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
#else
    getSnapshot = nullptr;
#endif
    Printf("RT OpenXR input: snapshot export %s\\n", getSnapshot ? "resolved" : "missing");
    RT_OpenXRInputReset();
}

void ReleaseAll()
{
    const bool swap = vr_switch_sticks;
    const auto& move = swap ? previous.right.stick : previous.left.stick;
    const auto& turn = swap ? previous.left.stick : previous.right.stick;
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
    if (pointerHeld) { PostGui(EV_GUI_LButtonUp, 0, 0); pointerHeld = false; }
}

void RT_OpenXRInputReset()
{
    ReleaseAll();
    previous = {};
    snapTurnLatched = false;
}
void RT_OpenXRInputAddAxes(float axes[])
{
    if (!getSnapshot || !previous.sessionRunning || !previous.focused) return;

    const auto& moveStick = vr_switch_sticks ? previous.right.stick : previous.left.stick;
    const auto& turnStick = vr_switch_sticks ? previous.left.stick : previous.right.stick;
    // Match XInput's axis convention: joystick sources subtract their values
    // from the engine accumulator, and OpenXR's +Y is forward.
    axes[JOYAXIS_Side] -= moveStick.data[0];
    axes[JOYAXIS_Forward] += moveStick.data[1];
    axes[JOYAXIS_Yaw] -= turnStick.data[0];
    axes[JOYAXIS_Pitch] -= turnStick.data[1] * 0.75f;
}
void RT_OpenXRInputPoll()
{
    if (!getSnapshot) return;
    RgOpenXRInputSnapshotEXT current{};
    current.structSize = sizeof(current);
    current.version = RG_OPENXR_INPUT_SNAPSHOT_EXT_VERSION;
    const RgResult snapshotResult = getSnapshot(&current);
    const bool hasControllerActivity = current.left.stick.data[0] != 0.0f || current.left.stick.data[1] != 0.0f ||
        current.right.stick.data[0] != 0.0f || current.right.stick.data[1] != 0.0f ||
        current.left.trigger != 0.0f || current.right.trigger != 0.0f || current.left.grip != 0.0f || current.right.grip != 0.0f;
    if (!loggedSnapshot || current.focused != previous.focused || current.capabilities != previous.capabilities || hasControllerActivity)
    {
        Printf("RT OpenXR input: result=%d size=%u version=%u session=%d focused=%d caps=%u left=(%.3f,%.3f) right=(%.3f,%.3f) trigger=(%.3f,%.3f) grip=(%.3f,%.3f) pose=(%d,%d)\n",
            (int)snapshotResult, current.structSize, current.version, current.sessionRunning ? 1 : 0,
            current.focused ? 1 : 0, current.capabilities, current.left.stick.data[0], current.left.stick.data[1],
            current.right.stick.data[0], current.right.stick.data[1], current.left.trigger, current.right.trigger, current.left.grip, current.right.grip, current.left.pose.valid ? 1 : 0, current.right.pose.valid ? 1 : 0);
        loggedSnapshot = true;
    }
    if (snapshotResult != RG_RESULT_SUCCESS || current.structSize < sizeof(current) ||
        current.version != RG_OPENXR_INPUT_SNAPSHOT_EXT_VERSION ||
        !current.sessionRunning)
    {
        RT_OpenXRInputReset();
        return;
    }
    const auto& moveStick = vr_switch_sticks ? current.right.stick : current.left.stick;
    const auto& turnStick = vr_switch_sticks ? current.left.stick : current.right.stick;
    StickEdges(moveStick, KEY_PAD_LTHUMB_RIGHT, KEY_PAD_LTHUMB_LEFT,
               KEY_PAD_LTHUMB_DOWN, KEY_PAD_LTHUMB_UP, vr_switch_sticks ? previous.right.stick : previous.left.stick);
    if (vr_snapTurn > 10.0f)
    {
        const bool turnRight = turnStick.data[0] > 0.7f;
        const bool turnLeft = turnStick.data[0] < -0.7f;
        if (!snapTurnLatched && (turnRight || turnLeft))
        {
            PostKey(turnRight ? KEY_PAD_RTHUMB_RIGHT : KEY_PAD_RTHUMB_LEFT, true);
            PostKey(turnRight ? KEY_PAD_RTHUMB_RIGHT : KEY_PAD_RTHUMB_LEFT, false);
            snapTurnLatched = true;
        }
        if (!turnRight && !turnLeft) snapTurnLatched = false;
    }
    else    StickEdges(turnStick, KEY_PAD_RTHUMB_RIGHT, KEY_PAD_RTHUMB_LEFT,
               KEY_PAD_RTHUMB_DOWN, KEY_PAD_RTHUMB_UP, vr_switch_sticks ? previous.left.stick : previous.right.stick);
    Edge(KEY_PAD_LTRIGGER, current.left.trigger > 0.5f, previous.left.trigger > 0.5f);
    Edge(KEY_PAD_RTRIGGER, current.right.trigger > 0.5f, previous.right.trigger > 0.5f);
    Edge(KEY_PAD_START, current.right.menu, previous.right.menu);
    Edge(KEY_PAD_BACK, current.left.menu, previous.left.menu);
    Edge(KEY_PAD_A, current.right.faceA, previous.right.faceA);
    Edge(KEY_PAD_B, current.right.faceB, previous.right.faceB);
    Edge(KEY_PAD_X, current.left.faceX, previous.left.faceX);
    Edge(KEY_PAD_Y, current.left.faceY, previous.left.faceY);
    Edge(KEY_PAD_RTHUMB, current.right.thumbClick, previous.right.thumbClick);
    PollPointer(current, previous);
    previous = current;
}