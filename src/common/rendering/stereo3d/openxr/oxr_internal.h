#pragma once

#include <string>

#if defined(HAVE_OPENXR)
namespace oxr
{
	void SetLastError(const std::string& message);
	void ClearLastError();
}
#endif
