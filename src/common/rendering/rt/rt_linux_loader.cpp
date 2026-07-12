#ifndef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define RG_USE_SURFACE_XLIB
#include <RTGL1/RTGL1.h>

#include <cstdlib>
#include <dlfcn.h>
#include <cstdio>
#include <string>

namespace
{
void* g_rt_library{};
}

RgResult RT_DlopenAndCreateXlib(const RgInstanceCreateInfo* pInfo,
                                void*                       display,
                                unsigned long               window,
                                bool                        isdebug,
                                RgInterface*                pOutInterface)
{
    if (g_rt_library)
    {
        dlclose(g_rt_library);
        g_rt_library = nullptr;
    }

    const char* explicitPath = getenv("RTGL1_LIBRARY_PATH");
    std::string runtimeDebugPath;
    std::string runtimePath;
    if (pInfo && pInfo->pOverrideFolderPath)
    {
        runtimeDebugPath = std::string(pInfo->pOverrideFolderPath) + "bin/debug/libRTGL1.so";
        runtimePath = std::string(pInfo->pOverrideFolderPath) + "bin/libRTGL1.so";
    }
    const char* candidates[] = {
        explicitPath,
        isdebug ? "build-rtgl-linux/libRTGL1.so" : nullptr,
        isdebug && !runtimeDebugPath.empty() ? runtimeDebugPath.c_str() : nullptr,
        !runtimePath.empty() ? runtimePath.c_str() : nullptr,
        "rt/bin/debug/libRTGL1.so",
        "rt/bin/libRTGL1.so",
        "libRTGL1.so",
    };

    for (const char* path : candidates)
    {
        if (!path || !path[0])
        {
            continue;
        }

        g_rt_library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!g_rt_library)
        {
            fprintf(stderr, "RTGL: dlopen failed for '%s': %s\n", path, dlerror());
            continue;
        }

        fprintf(stderr, "RTGL: loaded '%s'\n", path);

        auto createFunc =
            reinterpret_cast<PFN_rgCreateInstance>(dlsym(g_rt_library, "rgCreateInstance"));
        if (!createFunc)
        {
            dlclose(g_rt_library);
            g_rt_library = nullptr;
            return RG_RESULT_CANT_FIND_ENTRY_FUNCTION_IN_DYNAMIC_LIBRARY;
        }

        RgXlibSurfaceCreateInfo xlibInfo = {
            .dpy = static_cast<Display*>(display),
            .window = Window(window),
        };
        RgInstanceCreateInfo info = *pInfo;
        info.pXlibSurfaceCreateInfo = &xlibInfo;

        return createFunc(&info, pOutInterface);
    }

    return RG_RESULT_CANT_FIND_DYNAMIC_LIBRARY;
}

void RT_UnloadLibrary()
{
    if (g_rt_library)
    {
        dlclose(g_rt_library);
        g_rt_library = nullptr;
    }
}

#endif
