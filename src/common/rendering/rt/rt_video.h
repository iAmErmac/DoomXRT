#pragma once

#include "v_video.h"

#ifndef _WIN32
void RT_InitXlibSurface(void* display, unsigned long window);
void RT_Shutdown();
DFrameBuffer* RT_CreateFrameBuffer(void* hMonitor, bool fullscreen);
#endif
