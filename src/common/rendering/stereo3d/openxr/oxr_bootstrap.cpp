#include "oxr_loader.h"
#include "oxr_internal.h"
#include "common/engine/printf.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#if defined(HAVE_OPENXR) && defined(DYN_OPENXR)

namespace
{
	struct OpenXRTmpBootstrapContext
	{
		XrInstance instance = XR_NULL_HANDLE;
		XrSystemId systemId = XR_NULL_SYSTEM_ID;
		bool supportsVulkanEnable = false;
		bool supportsVulkanEnable2 = false;
		PFN_xrGetVulkanInstanceExtensionsKHR getVulkanInstanceExtensionsKHR = nullptr;
		PFN_xrGetVulkanDeviceExtensionsKHR getVulkanDeviceExtensionsKHR = nullptr;
		PFN_xrGetVulkanGraphicsRequirementsKHR getVulkanGraphicsRequirementsKHR = nullptr;
		PFN_xrGetVulkanGraphicsRequirements2KHR getVulkanGraphicsRequirements2KHR = nullptr;
		PFN_xrGetVulkanGraphicsDeviceKHR getVulkanGraphicsDeviceKHR = nullptr;
		PFN_xrGetVulkanGraphicsDevice2KHR getVulkanGraphicsDevice2KHR = nullptr;
	};

	OpenXRBootstrapInfo g_cachedInfo;
	bool g_cachedInfoComputed = false;
	bool g_cachedInfoResult = false;

	std::vector<std::string> SplitSpaceSeparatedList(const char* list)
	{
		std::vector<std::string> result;
		if (list == nullptr)
		{
			return result;
		}

		const char* cursor = list;
		while (*cursor != '\0')
		{
			while (*cursor == ' ')
			{
				++cursor;
			}
			if (*cursor == '\0')
			{
				break;
			}

			const char* end = cursor;
			while (*end != '\0' && *end != ' ')
			{
				++end;
			}
			result.emplace_back(cursor, end);
			cursor = end;
		}
		return result;
	}

	void DestroyBootstrapContext(OpenXRTmpBootstrapContext& ctx)
	{
		if (ctx.instance != XR_NULL_HANDLE)
		{
			xrDestroyInstance(ctx.instance);
		}
		ctx = {};
	}

	bool LoadInstanceProc(XrInstance instance, const char* name, PFN_xrVoidFunction* out)
	{
		*out = nullptr;
		return XR_SUCCEEDED(xrGetInstanceProcAddr(instance, name, out)) && *out != nullptr;
	}

	bool CreateBootstrapContext(OpenXRTmpBootstrapContext& ctx, OpenXRBootstrapInfo* outInfo = nullptr)
	{
		if (!InitializeOpenXRLoader())
		{
			oxr::SetLastError("OpenXR loader is not present");
			return false;
		}

		uint32_t extensionCount = 0;
		if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr)) || extensionCount == 0)
		{
			oxr::SetLastError("OpenXR runtime did not enumerate instance extensions");
			return false;
		}

		std::vector<XrExtensionProperties> extensions(extensionCount);
		for (auto& ext : extensions)
		{
			ext.type = XR_TYPE_EXTENSION_PROPERTIES;
		}
		if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data())))
		{
			oxr::SetLastError("OpenXR runtime extension enumeration failed");
			return false;
		}

		auto hasExtension = [&](const char* name)
		{
			for (const auto& ext : extensions)
			{
				if (strcmp(ext.extensionName, name) == 0)
				{
					return true;
				}
			}
			return false;
		};

		ctx.supportsVulkanEnable = hasExtension(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
		ctx.supportsVulkanEnable2 = hasExtension(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
		if (!ctx.supportsVulkanEnable && !ctx.supportsVulkanEnable2)
		{
			oxr::SetLastError("OpenXR runtime does not advertise XR_KHR_vulkan_enable or XR_KHR_vulkan_enable2");
			return false;
		}

		std::vector<const char*> enabledExtensions;
		if (ctx.supportsVulkanEnable)
		{
			enabledExtensions.push_back(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
		}
		if (ctx.supportsVulkanEnable2)
		{
			enabledExtensions.push_back(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
		}

		XrApplicationInfo appInfo{};
		appInfo.apiVersion = XR_API_VERSION_1_0;
		appInfo.applicationVersion = 1;
		appInfo.engineVersion = 1;
		strncpy(appInfo.applicationName, GAMENAME, sizeof(appInfo.applicationName) - 1);
		strncpy(appInfo.engineName, GAMENAME, sizeof(appInfo.engineName) - 1);

		XrInstanceCreateInfo createInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
		createInfo.applicationInfo = appInfo;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
		createInfo.enabledExtensionNames = enabledExtensions.data();
		if (XR_FAILED(xrCreateInstance(&createInfo, &ctx.instance)))
		{
			oxr::SetLastError("OpenXR bootstrap instance creation failed");
			return false;
		}

		XrSystemGetInfo systemInfo{ XR_TYPE_SYSTEM_GET_INFO };
		systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
		if (XR_FAILED(xrGetSystem(ctx.instance, &systemInfo, &ctx.systemId)))
		{
			oxr::SetLastError("OpenXR bootstrap could not resolve a head mounted display system");
			DestroyBootstrapContext(ctx);
			return false;
		}

		LoadInstanceProc(ctx.instance, "xrGetVulkanInstanceExtensionsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&ctx.getVulkanInstanceExtensionsKHR));
		LoadInstanceProc(ctx.instance, "xrGetVulkanDeviceExtensionsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&ctx.getVulkanDeviceExtensionsKHR));
		LoadInstanceProc(ctx.instance, "xrGetVulkanGraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&ctx.getVulkanGraphicsRequirementsKHR));
		LoadInstanceProc(ctx.instance, "xrGetVulkanGraphicsRequirements2KHR", reinterpret_cast<PFN_xrVoidFunction*>(&ctx.getVulkanGraphicsRequirements2KHR));
		LoadInstanceProc(ctx.instance, "xrGetVulkanGraphicsDeviceKHR", reinterpret_cast<PFN_xrVoidFunction*>(&ctx.getVulkanGraphicsDeviceKHR));
		LoadInstanceProc(ctx.instance, "xrGetVulkanGraphicsDevice2KHR", reinterpret_cast<PFN_xrVoidFunction*>(&ctx.getVulkanGraphicsDevice2KHR));

		if (outInfo != nullptr)
		{
			outInfo->available = true;
			outInfo->supportsVulkanEnable = ctx.supportsVulkanEnable;
			outInfo->supportsVulkanEnable2 = ctx.supportsVulkanEnable2;

			XrGraphicsRequirementsVulkanKHR requirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
			if (ctx.getVulkanGraphicsRequirements2KHR && XR_SUCCEEDED(ctx.getVulkanGraphicsRequirements2KHR(ctx.instance, ctx.systemId, &requirements)))
			{
				outInfo->minApiVersionSupported = requirements.minApiVersionSupported;
				outInfo->maxApiVersionSupported = requirements.maxApiVersionSupported;
			}
			else if (ctx.getVulkanGraphicsRequirementsKHR && XR_SUCCEEDED(ctx.getVulkanGraphicsRequirementsKHR(ctx.instance, ctx.systemId, &requirements)))
			{
				outInfo->minApiVersionSupported = requirements.minApiVersionSupported;
				outInfo->maxApiVersionSupported = requirements.maxApiVersionSupported;
			}

			if (ctx.getVulkanInstanceExtensionsKHR)
			{
				uint32_t size = 0;
				if (XR_SUCCEEDED(ctx.getVulkanInstanceExtensionsKHR(ctx.instance, ctx.systemId, 0, &size, nullptr)) && size > 0)
				{
					std::vector<char> buffer(size, '\0');
					if (XR_SUCCEEDED(ctx.getVulkanInstanceExtensionsKHR(ctx.instance, ctx.systemId, size, &size, buffer.data())))
					{
						outInfo->requiredInstanceExtensions = SplitSpaceSeparatedList(buffer.data());
					}
				}
			}

			if (ctx.getVulkanDeviceExtensionsKHR)
			{
				uint32_t size = 0;
				if (XR_SUCCEEDED(ctx.getVulkanDeviceExtensionsKHR(ctx.instance, ctx.systemId, 0, &size, nullptr)) && size > 0)
				{
					std::vector<char> buffer(size, '\0');
					if (XR_SUCCEEDED(ctx.getVulkanDeviceExtensionsKHR(ctx.instance, ctx.systemId, size, &size, buffer.data())))
					{
						outInfo->requiredDeviceExtensions = SplitSpaceSeparatedList(buffer.data());
					}
				}
			}
		}

		oxr::ClearLastError();
		return true;
	}

	void PrintBootstrapSummary(const OpenXRBootstrapInfo& info)
	{
		if (info.available)
		{
			Printf("OpenXR bootstrap: runtime present.\n");
			Printf("OpenXR bootstrap: Vulkan enable support: %s%s\n",
				info.supportsVulkanEnable ? "XR_KHR_vulkan_enable" : "",
				info.supportsVulkanEnable2 ? (info.supportsVulkanEnable ? " XR_KHR_vulkan_enable2" : "XR_KHR_vulkan_enable2") : "");

			if (info.requiredInstanceExtensions.empty())
			{
				Printf("OpenXR bootstrap: required Vulkan instance extensions: none\n");
			}
			else
			{
				Printf("OpenXR bootstrap: required Vulkan instance extensions:\n");
				for (const auto& ext : info.requiredInstanceExtensions)
				{
					Printf("  %s\n", ext.c_str());
				}
			}

			if (info.requiredDeviceExtensions.empty())
			{
				Printf("OpenXR bootstrap: required Vulkan device extensions: none\n");
			}
			else
			{
				Printf("OpenXR bootstrap: required Vulkan device extensions:\n");
				for (const auto& ext : info.requiredDeviceExtensions)
				{
					Printf("  %s\n", ext.c_str());
				}
			}

			if (info.minApiVersionSupported != 0 || info.maxApiVersionSupported != 0)
			{
				Printf("OpenXR bootstrap: Vulkan API requirements min=%u.%u.%u max=%u.%u.%u\n",
					VK_VERSION_MAJOR(static_cast<uint32_t>(info.minApiVersionSupported)),
					VK_VERSION_MINOR(static_cast<uint32_t>(info.minApiVersionSupported)),
					VK_VERSION_PATCH(static_cast<uint32_t>(info.minApiVersionSupported)),
					VK_VERSION_MAJOR(static_cast<uint32_t>(info.maxApiVersionSupported)),
					VK_VERSION_MINOR(static_cast<uint32_t>(info.maxApiVersionSupported)),
					VK_VERSION_PATCH(static_cast<uint32_t>(info.maxApiVersionSupported)));
			}
			else
			{
				Printf("OpenXR bootstrap: Vulkan API requirements unavailable.\n");
			}
		}
		else
		{
			const std::string error = GetLastOpenXRError();
			Printf("OpenXR bootstrap unavailable: %s\n", error.empty() ? "unknown error" : error.c_str());
		}
	}
}

bool QueryOpenXRVulkanRequirements(OpenXRBootstrapInfo& outInfo)
{
	if (!g_cachedInfoComputed)
	{
		OpenXRTmpBootstrapContext ctx;
		g_cachedInfo = {};
		g_cachedInfoResult = CreateBootstrapContext(ctx, &g_cachedInfo);
		DestroyBootstrapContext(ctx);
		g_cachedInfoComputed = true;
		PrintBootstrapSummary(g_cachedInfo);
	}

	outInfo = g_cachedInfo;
	return g_cachedInfoResult;
}

bool QueryOpenXRVulkanRequirementsForMode(int& vrMode, OpenXRBootstrapInfo& outInfo)
{
	if (QueryOpenXRVulkanRequirements(outInfo))
	{
		return true;
	}

	if (vrMode == VR_OPENXR)
	{
		const std::string error = GetLastOpenXRError();
		Printf("OpenXR bootstrap failed for vr_mode 15; falling back to vr_mode 0: %s\n",
			error.empty() ? "unknown error" : error.c_str());
		vrMode = VR_MONO;
	}
	return false;
}

bool IsOpenXRRuntimePresent()
{
	OpenXRBootstrapInfo info;
	return QueryOpenXRVulkanRequirements(info);
}

std::vector<std::string> GetRequiredVulkanInstanceExtensions()
{
	OpenXRBootstrapInfo info;
	if (QueryOpenXRVulkanRequirements(info))
	{
		return info.requiredInstanceExtensions;
	}
	return {};
}

std::vector<std::string> GetRequiredVulkanDeviceExtensions()
{
	OpenXRBootstrapInfo info;
	if (QueryOpenXRVulkanRequirements(info))
	{
		return info.requiredDeviceExtensions;
	}
	return {};
}

bool QueryRuntimePreferredPhysicalDevice(VkInstance instance, VkPhysicalDevice& outPhysicalDevice)
{
	outPhysicalDevice = VK_NULL_HANDLE;

	OpenXRTmpBootstrapContext ctx;
	if (!CreateBootstrapContext(ctx, nullptr))
	{
		return false;
	}

	bool ok = false;
	if (ctx.getVulkanGraphicsDevice2KHR)
	{
		XrVulkanGraphicsDeviceGetInfoKHR getInfo{ XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
		getInfo.systemId = ctx.systemId;
		getInfo.vulkanInstance = instance;
		ok = XR_SUCCEEDED(ctx.getVulkanGraphicsDevice2KHR(ctx.instance, &getInfo, &outPhysicalDevice));
	}
	else if (ctx.getVulkanGraphicsDeviceKHR)
	{
		ok = XR_SUCCEEDED(ctx.getVulkanGraphicsDeviceKHR(ctx.instance, ctx.systemId, instance, &outPhysicalDevice));
	}
	else
	{
		oxr::SetLastError("OpenXR runtime does not expose a Vulkan graphics device query");
	}

	DestroyBootstrapContext(ctx);
	if (!ok || outPhysicalDevice == VK_NULL_HANDLE)
	{
		oxr::SetLastError("OpenXR runtime did not return a preferred Vulkan physical device");
		return false;
	}

	oxr::ClearLastError();
	return true;
}

#endif

