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
void RT_OpenXRInputPoll();
void RT_OpenXRInputReset();
void RT_OpenXRInputAddAxes(float axes[]);
bool RT_OpenXRInputAvailable();
void RT_OpenXRInputConfigure(const float deadzone[4], const float scale[4], const EJoyAxis map[4], float sensitivity);
float RT_OpenXRInputConsumeViewYawDeltaDegrees();
