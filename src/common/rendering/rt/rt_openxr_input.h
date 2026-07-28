#pragma once

#ifdef _WIN32
#ifndef RG_USE_SURFACE_WIN32
#define RG_USE_SURFACE_WIN32
#endif
#endif
#include <RTGL1/RTGL1.h>
#include "common/engine/m_joy.h"

#ifdef _WIN32
#include <windows.h>
#endif

void RT_OpenXRInputBindModule(
#ifdef _WIN32
    HMODULE module
#else
    void* module
#endif
);
struct RT_OpenXRHandPose
{
    RgFloat3D position{};
    RgQuaternion orientation{{ 0.0f, 0.0f, 0.0f, 1.0f }};
    bool valid = false;
};
struct RT_OpenXRWorldHandPose
{
    RgFloat3D position{};
    RgFloat3D forward{{ 0.0f, 0.0f, -1.0f }};
    RgFloat3D up{{ 0.0f, 1.0f, 0.0f }};
    bool valid = false;
};

void RT_OpenXRInputSetWorldHandPoses(const RT_OpenXRWorldHandPose (&poses)[2]);
void RT_OpenXRInputSetMovementYawRadians(float yaw);
bool RT_OpenXRInputGetWorldHandPose(int hand, RT_OpenXRWorldHandPose* outPose);

bool RT_OpenXRInputGetHandPose(int hand, RT_OpenXRHandPose* outPose);
bool RT_OpenXRInputIsHandGripping(int hand);
bool RT_OpenXRInputGetMainHandPose(RT_OpenXRHandPose* outPose);
bool RT_OpenXRInputHaptic(int hand, float durationSeconds, float amplitude);
void RT_OpenXRHapticWeaponFire();
void RT_OpenXRHapticDamage(float amplitude);
void RT_OpenXRHapticPoison(float amplitude);
void RT_OpenXRHapticHeal(float amplitude);
void RT_OpenXRHapticPickup();
void RT_OpenXRHapticQuake(float leftAmplitude, float rightAmplitude);
bool RT_OpenXRInputGetMainWorldHandPose(RT_OpenXRWorldHandPose* outPose);
void RT_OpenXRInputPoll();
void RT_OpenXRInputReset();
void RT_OpenXRInputAddAxes(float axes[]);
bool RT_OpenXRInputAvailable();
bool RT_OpenXRInputIsActive();
void RT_OpenXRInputConfigure(const float deadzone[4], const float scale[4], const EJoyAxis map[4], float sensitivity);
float RT_OpenXRInputConsumeViewYawDeltaDegrees();
