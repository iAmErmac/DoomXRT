#include "oxr_loader.h"
#include "oxr_internal.h"
#include "cmdlib.h"
#include "common/engine/printf.h"
#include <string>

#if defined(HAVE_OPENXR)

#ifdef DYN_OPENXR
FModule OpenXRModule{ "OpenXR" };

#define OXR_PROC(name) TReqProc<OpenXRModule, PFN_##name> name{#name};
#define OXR_OPT_PROC(name) TOptProc<OpenXRModule, PFN_##name> name{#name};

#include "oxr_procs.h"

#undef OXR_PROC
#undef OXR_OPT_PROC

#ifdef _WIN32
#define OPENXRLIB "openxr_loader.dll"
#elif defined(__APPLE__)
#define OPENXRLIB "libopenxr_loader.dylib"
#else
#define OPENXRLIB "libopenxr_loader.so"
#endif
#endif

namespace
{
	std::string g_lastOpenXRError;
	bool g_loaderInitTried = false;
	bool g_loaderInitResult = false;

	FString GetLoaderErrorString()
	{
#ifdef _WIN32
		DWORD error = GetLastError();
		if (error == 0)
		{
			return FString("unknown error");
		}

		LPSTR message = nullptr;
		DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
		DWORD len = FormatMessageA(flags, nullptr, error, 0, (LPSTR)&message, 0, nullptr);
		FString result;
		if (len > 0 && message != nullptr)
		{
			result = message;
			LocalFree(message);
		}
		else
		{
			result.AppendFormat("error %lu", error);
		}
		return result;
#else
		return FString("unknown loader error");
#endif
	}
}

namespace oxr
{
	void SetLastError(const std::string& message)
	{
		g_lastOpenXRError = message;
	}

	void ClearLastError()
	{
		g_lastOpenXRError.clear();
	}
}

bool InitializeOpenXRLoader()
{
#if defined(DYN_OPENXR)
	if (g_loaderInitTried)
	{
		return g_loaderInitResult;
	}

	g_loaderInitTried = true;
	FString libname = NicePath("$PROGDIR/" OPENXRLIB);
	Printf("OpenXR loader: loading '%s' then '%s'.\n", libname.GetChars(), OPENXRLIB);
	g_loaderInitResult = OpenXRModule.Load({ libname.GetChars(), OPENXRLIB });
	if (!g_loaderInitResult)
	{
		FString error;
		error.Format("OpenXR loader could not be loaded: %s", GetLoaderErrorString().GetChars());
		oxr::SetLastError(error.GetChars());
		Printf("OpenXR loader: load failed: %s\n", GetLoaderErrorString().GetChars());
	}
	else
	{
		oxr::ClearLastError();
	}
	Printf("OpenXR loader: present=%d.\n", g_loaderInitResult ? 1 : 0);
	return g_loaderInitResult;
#else
	oxr::ClearLastError();
	return true;
#endif
}

std::string GetLastOpenXRError()
{
	return g_lastOpenXRError;
}

#endif
