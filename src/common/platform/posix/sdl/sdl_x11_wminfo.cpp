#if HAVE_RT

#include <SDL.h>
#include <SDL_syswm.h>
#include <cstdio>

bool I_GetSDLX11WindowInfo(SDL_Window* window,
                           void**      display,
                           unsigned long* xwindow,
                           char*       error,
                           size_t      errorSize)
{
	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);

	if (!SDL_GetWindowWMInfo(window, &wmInfo))
	{
		snprintf(error, errorSize, "%s", SDL_GetError());
		return false;
	}
	if (wmInfo.subsystem != SDL_SYSWM_X11)
	{
		snprintf(error, errorSize, "RT renderer currently requires the SDL x11 video driver.");
		return false;
	}

	*display = wmInfo.info.x11.display;
	*xwindow = wmInfo.info.x11.window;
	return true;
}

#endif
