#pragma once

#ifndef _WIN32

#include <RTGL1/RTGL1.h>

RgResult RT_DlopenAndCreateXlib(const RgInstanceCreateInfo* pInfo,
                                void*                       display,
                                unsigned long               window,
                                bool                        isdebug,
                                RgInterface*                pOutInterface);
void RT_UnloadLibrary();

#endif
