#pragma once

#ifdef _WIN32
#ifndef RG_USE_SURFACE_WIN32
#define RG_USE_SURFACE_WIN32
#endif
#endif
#include <RTGL1/RTGL1.h>

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
// Adds the latest OpenXR controller state to DoomXRT's gameplay joystick axes.
void RT_OpenXRInputAddAxes(float axes[]);