#pragma once

#include "i_module.h"
#include <cstdint>
#include <string>
#include <vector>

#if defined(HAVE_OPENXR)
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>
#define XR_USE_PLATFORM_WIN32
#endif

#ifdef HAVE_VULKAN
#include <zvulkan/vulkandevice.h>
#define XR_USE_GRAPHICS_API_VULKAN
#else
struct VkInstance_T;
using VkInstance = VkInstance_T*;
struct VkPhysicalDevice_T;
using VkPhysicalDevice = VkPhysicalDevice_T*;
#endif

#ifndef XR_NO_PROTOTYPES
#define XR_NO_PROTOTYPES
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#ifdef DYN_OPENXR
extern FModule OpenXRModule;

#define OXR_PROC(name) extern TReqProc<OpenXRModule, PFN_##name> name;
#define OXR_OPT_PROC(name) extern TOptProc<OpenXRModule, PFN_##name> name;

#include "oxr_procs.h"

#undef OXR_PROC
#undef OXR_OPT_PROC
#endif
#else
#ifdef HAVE_VULKAN
#include <zvulkan/vulkandevice.h>
#else
struct VkInstance_T;
using VkInstance = VkInstance_T*;
struct VkPhysicalDevice_T;
using VkPhysicalDevice = VkPhysicalDevice_T*;
#endif
#endif

struct OpenXRBootstrapInfo
{
	bool available = false;
	bool supportsVulkanEnable = false;
	bool supportsVulkanEnable2 = false;
	uint64_t minApiVersionSupported = 0;
	uint64_t maxApiVersionSupported = 0;
	std::vector<std::string> requiredInstanceExtensions;
	std::vector<std::string> requiredDeviceExtensions;
};

using OpenXRVulkanBootstrapInfo = OpenXRBootstrapInfo;

bool InitializeOpenXRLoader();
bool IsOpenXRRuntimePresent();
bool QueryOpenXRVulkanRequirements(OpenXRBootstrapInfo& outInfo);
bool QueryOpenXRVulkanRequirementsForMode(int& vrMode, OpenXRBootstrapInfo& outInfo);
bool QueryRuntimePreferredPhysicalDevice(VkInstance instance, VkPhysicalDevice& outPhysicalDevice);
std::vector<std::string> GetRequiredVulkanInstanceExtensions();
std::vector<std::string> GetRequiredVulkanDeviceExtensions();
std::string GetLastOpenXRError();

#if !defined(HAVE_OPENXR)
inline bool InitializeOpenXRLoader()
{
	return false;
}

inline bool IsOpenXRRuntimePresent()
{
	return false;
}

inline bool QueryOpenXRVulkanRequirements(OpenXRBootstrapInfo& outInfo)
{
	outInfo = {};
	return false;
}

inline bool QueryOpenXRVulkanRequirementsForMode(int& vrMode, OpenXRBootstrapInfo& outInfo)
{
	if (vrMode == 15)
	{
		vrMode = 0;
	}
	outInfo = {};
	return false;
}

inline bool QueryRuntimePreferredPhysicalDevice(VkInstance, VkPhysicalDevice& outPhysicalDevice)
{
	outPhysicalDevice = nullptr;
	return false;
}

inline std::vector<std::string> GetRequiredVulkanInstanceExtensions()
{
	return {};
}

inline std::vector<std::string> GetRequiredVulkanDeviceExtensions()
{
	return {};
}

inline std::string GetLastOpenXRError()
{
	return {};
}
#endif
