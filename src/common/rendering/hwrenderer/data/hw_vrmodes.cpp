/*
** hw_vrmodes.cpp
** Matrix handling for stereo 3D rendering
**
**---------------------------------------------------------------------------
** Copyright 2015 Christopher Bruns
** Copyright 2016-2021 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
**
*/

#include "vectors.h"
#include "hw_cvars.h"
#include "hw_vrmodes.h"
#include "v_video.h"
#include "printf.h"
#include "common/console/c_dispatch.h"
#include "version.h"
#include "i_interface.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include "zvulkan/vulkanbuilders.h"
#include "vulkan/textures/vk_imagetransition.h"
#include "vulkan/renderer/vk_postprocess.h"
#include "vulkan/system/vk_renderdevice.h"
#include "common/rendering/stereo3d/openxr/oxr_loader.h"

// Set up 3D-specific console variables:
CVAR(Int, vr_mode, 0, CVAR_GLOBALCONFIG|CVAR_ARCHIVE)
CVAR(Float, vr_snapTurn, 45.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, vr_switch_sticks, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

// VR virtual-screen presentation controls.
CVAR(Int, vr_overlayscreen, 2, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)
CVAR(Bool, vr_overlayscreen_always, false, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)
CVAR(Float, vr_overlayscreen_size, 1.0f, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)
CVAR(Float, vr_overlayscreen_dist, 0.0f, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)
CVAR(Float, vr_overlayscreen_vpos, 0.0f, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)

namespace cvar
{
// Native OpenXR presentation controls. These stay with the common VR cvars so
// they are available before the renderer starts.
CVAR(Int, vr_rt_openxr_presentation, 0, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)
CVAR(Int, vr_desktop_view, 1, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)
CVAR(Float, vr_openxr_render_scale, 1.0f, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)
CVAR(Float, vr_openxr_fov_adjust_deg, 0.0f, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)
CVAR(Float, vr_openxr_eye_shift_scale, 1.0f, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)
CVAR(Float, vr_rt_render_scale, 0.75f, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)
CVAR(Int, vr_rt_reflection_depth, 2, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)
CVAR(Float, vr_rt_min_reflection_roughness, 0.20f, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)
CVAR(Bool, vr_rt_indirect_second_bounce, true, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)

}
uint64_t g_vr_virtual_screen_recenter_request = 0;

CCMD(vr_recenter_virtual_screen)
{
	++g_vr_virtual_screen_recenter_request;
}

// switch left and right eye views
CVAR(Bool, vr_swap_eyes, false, CVAR_GLOBALCONFIG | CVAR_ARCHIVE)

// intraocular distance in meters
CVAR(Float, vr_ipd, 0.062f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG) // METERS

// distance between viewer and the display screen
CVAR(Float, vr_screendist, 0.80f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) // METERS

// default conversion between (vertical) DOOM units and meters
CVAR(Float, vr_hunits_per_meter, 41.0f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) // METERS

namespace
{
#define isqrt2 0.7071067812f

	static const VREyeInfo vrmi_mono_eyes[2] = { VREyeInfo(0.f, 1.f), VREyeInfo(0.f, 0.f) };
	static const VREyeInfo vrmi_stereo_eyes[2] = { VREyeInfo(-.5f, 1.f), VREyeInfo(.5f, 1.f) };
	static const VREyeInfo vrmi_sbsfull_eyes[2] = { VREyeInfo(-.5f, .5f), VREyeInfo(.5f, .5f) };
	static const VREyeInfo vrmi_sbssquished_eyes[2] = { VREyeInfo(-.5f, 1.f), VREyeInfo(.5f, 1.f) };
	static const VREyeInfo vrmi_lefteye_eyes[2] = { VREyeInfo(-.5f, 1.f), VREyeInfo(0.f, 0.f) };
	static const VREyeInfo vrmi_righteye_eyes[2] = { VREyeInfo(.5f, 1.f), VREyeInfo(0.f, 0.f) };
	static const VREyeInfo vrmi_topbottom_eyes[2] = { VREyeInfo(-.5f, 1.f), VREyeInfo(.5f, 1.f) };
	static const VREyeInfo vrmi_checker_eyes[2] = { VREyeInfo(-.5f, 1.f), VREyeInfo(.5f, 1.f) };
	static const VREyeInfo vrmi_openxr_eyes[2] = { VREyeInfo(0.f, 1.f), VREyeInfo(0.f, 0.f) };

	#if defined(HAVE_OPENXR)
	class DoomXRMode final : public VRMode
	{
	public:
		DoomXRMode() : VRMode(1, 1.f, 1.f, 1.f, vrmi_openxr_eyes) {}

		bool IsVR() const override { return true; }
		bool GetRecommendedRenderSize(int& outWidth, int& outHeight) const override
		{
			if (!EnsureInitialized())
			{
				outWidth = 0;
				outHeight = 0;
				return false;
			}

			outWidth = (int)mVirtualScreenWidth;
			outHeight = (int)mVirtualScreenHeight;
			return outWidth > 0 && outHeight > 0;
		}
		bool ShouldUseRecommendedRenderSizeThisFrame() const override { return mInitialized && mSession != XR_NULL_HANDLE && mSessionRunning && mFrameInProgress && mSwapchainAcquiredThisFrame && mSwapchain != XR_NULL_HANDLE && !mSwapchainTextures.empty() && !mDisabled; }
		bool SupportsMultiview() const override { return false; }
		bool ShouldUseMultiviewThisFrame() const override { return false; }
		bool ShouldUseScreenLayerForCurrentFrame() const override { return mInitialized && mSession != XR_NULL_HANDLE && mSessionRunning && mFrameInProgress && mSwapchainAcquiredThisFrame && mSwapchain != XR_NULL_HANDLE && !mSwapchainTextures.empty() && !mDisabled; }
		void SetupOverlay() override {}
		void UpdateOverlaySettings() const override {}
		void DrawMountedHud(HWDrawInfo*, FRenderState&) const override {}
		bool IsRenderingVirtualScreen() const override { return mInitialized && mSession != XR_NULL_HANDLE && mSessionRunning && mFrameInProgress && mSwapchainAcquiredThisFrame && !mDisabled && mCurrentSwapchainTexture != nullptr; }
		bool RenderVirtualScreen() const override
		{
			auto* vkfb = dynamic_cast<VulkanRenderDevice*>(screen);
			if (vkfb == nullptr || !mFrameInProgress || !mSwapchainAcquiredThisFrame || mCurrentSwapchainTexture == nullptr)
			{
				DisableForRun("virtual screen copy unavailable");
				return false;
			}

			auto* postprocess = vkfb->GetPostprocess();
			if (postprocess == nullptr)
			{
				DisableForRun("virtual screen postprocess unavailable");
				return false;
			}

			IntRect fullTargetRect = { 0, 0, (int)mVirtualScreenWidth, (int)mVirtualScreenHeight };
			postprocess->DrawPresentTextureToImage(mCurrentSwapchainTexture, (VkFormat)mSwapchainFormat, fullTargetRect, true, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			mVirtualScreenPopulatedThisFrame = true;
			return true;
		}
		void FinalizeEyeImage(VulkanRenderDevice* fb, int eyeIndex) const override
		{
			if (fb == nullptr || eyeIndex != 0 || !mFrameInProgress || mCurrentSwapchainTexture == nullptr)
			{
				return;
			}

			auto* postprocess = fb->GetPostprocess();
			if (postprocess == nullptr)
			{
				return;
			}

			IntRect fullTargetRect = { 0, 0, (int)mVirtualScreenWidth, (int)mVirtualScreenHeight };
			postprocess->DrawPresentTextureToImage(mCurrentSwapchainTexture, (VkFormat)mSwapchainFormat, fullTargetRect, true, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
		void PollXREvents() const override
		{
			if (mDisabled || mInstance == XR_NULL_HANDLE)
			{
				return;
			}

			XrEventDataBuffer event{ XR_TYPE_EVENT_DATA_BUFFER };
			while (XR_SUCCEEDED(xrPollEvent(mInstance, &event)))
			{
				switch (event.type)
				{
				case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
				{
					const auto* changed = reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
					mSessionState = changed->state;
					if (mSessionState == XR_SESSION_STATE_READY)
					{
					}
					else if (mSessionState == XR_SESSION_STATE_STOPPING)
					{
						mSessionRunning = false;
					}
					else if (mSessionState == XR_SESSION_STATE_EXITING || mSessionState == XR_SESSION_STATE_LOSS_PENDING)
					{
						DestroyOpenXR();
						return;
					}
					break;
				}
				case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
					DestroyOpenXR();
					return;
				default:
					break;
				}
				event = { XR_TYPE_EVENT_DATA_BUFFER };
			}
		}
		bool BeginXRFrame() const override
		{
			if (!EnsureInitialized())
			{
				return false;
			}

			PollXREvents();
			if (mDisabled || mSession == XR_NULL_HANDLE)
			{
				return false;
			}

			if (!mSessionRunning)
			{
				if (mSessionState != XR_SESSION_STATE_READY)
				{
					return false;
				}

				XrSessionBeginInfo beginInfo{ XR_TYPE_SESSION_BEGIN_INFO };
				beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				if (XR_FAILED(xrBeginSession(mSession, &beginInfo)))
				{
					DisableForRun("session begin failed");
					return false;
				}
				mSessionRunning = true;
			}

			if (mFrameInProgress)
			{
				return false;
			}

			XrFrameWaitInfo waitInfo{ XR_TYPE_FRAME_WAIT_INFO };
			if (XR_FAILED(xrWaitFrame(mSession, &waitInfo, &mFrameState)))
			{
				DisableForRun("per-frame wait/begin failure disabled XR for this run");
				return false;
			}

			XrFrameBeginInfo frameBeginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
			if (XR_FAILED(xrBeginFrame(mSession, &frameBeginInfo)))
			{
				DisableForRun("per-frame wait/begin failure disabled XR for this run");
				return false;
			}

			mFrameInProgress = true;
			mSwapchainAcquiredThisFrame = false;
			mVirtualScreenPopulatedThisFrame = false;
			mSwapchainImageIndex = UINT32_MAX;
			mCurrentSwapchainTexture = nullptr;
			return true;
		}
		bool AcquireXRSwapchain() const override
		{
			if (!mFrameInProgress || mSwapchain == XR_NULL_HANDLE || mSwapchainAcquiredThisFrame)
			{
				return false;
			}

			if (mSwapchainTextures.empty() && !CreateSwapchain())
			{
				DisableForRun("swapchain creation failed");
				return false;
			}

			XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
			uint32_t imageIndex = 0;
			if (XR_FAILED(xrAcquireSwapchainImage(mSwapchain, &acquireInfo, &imageIndex)))
			{
				DisableForRun("swapchain acquisition failed");
				return false;
			}

			XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
			waitInfo.timeout = XR_INFINITE_DURATION;
			if (XR_FAILED(xrWaitSwapchainImage(mSwapchain, &waitInfo)))
			{
				XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
				xrReleaseSwapchainImage(mSwapchain, &releaseInfo);
				DisableForRun("per-frame wait/begin failure disabled XR for this run");
				return false;
			}

			mSwapchainImageIndex = imageIndex;
			mSwapchainAcquiredThisFrame = true;
			mVirtualScreenPopulatedThisFrame = false;
			mCurrentSwapchainTexture = &mSwapchainTextures[imageIndex];
			return true;
		}
		bool SubmitFrame() const override
		{
			if (mDisabled || mSession == XR_NULL_HANDLE)
			{
				return false;
			}

			if (!mFrameInProgress)
			{
				return true;
			}

			if (!mVirtualScreenPopulatedThisFrame)
			{
				DisableForRun("virtual screen was not populated before submit");
				return false;
			}

			if (mSwapchain != XR_NULL_HANDLE && mSwapchainImageIndex != UINT32_MAX)
			{
				XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
				xrReleaseSwapchainImage(mSwapchain, &releaseInfo);
			}

			XrCompositionLayerBaseHeader* layers[1] = {};
			uint32_t layerCount = 0;
			if (mCurrentSwapchainTexture != nullptr)
			{
				mQuadLayer.space = mSpace;
				mQuadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
				mQuadLayer.pose = mQuadPose;
				mQuadLayer.subImage.swapchain = mSwapchain;
				mQuadLayer.subImage.imageArrayIndex = 0;
				mQuadLayer.subImage.imageRect.offset = { 0, 0 };
				mQuadLayer.subImage.imageRect.extent = { (int32_t)mVirtualScreenWidth, (int32_t)mVirtualScreenHeight };
				layers[0] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&mQuadLayer);
				layerCount = 1;
			}

			XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
			endInfo.displayTime = mFrameState.predictedDisplayTime;
			endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
			endInfo.layerCount = layerCount;
			endInfo.layers = layerCount != 0 ? layers : nullptr;
			if (XR_FAILED(xrEndFrame(mSession, &endInfo)))
			{
				DisableForRun("per-frame wait/begin failure disabled XR for this run");
				return false;
			}

			mFrameInProgress = false;
			mSwapchainAcquiredThisFrame = false;
			mVirtualScreenPopulatedThisFrame = false;
			mSwapchainImageIndex = UINT32_MAX;
			mCurrentSwapchainTexture = nullptr;
			return true;
		}
		bool GetHandTransform(int, VSMatrix*) const override { return false; }
		bool GetWeaponTransform(VSMatrix* out, int hand = VR_MAINHAND) const override { return VRMode::GetWeaponTransform(out, hand); }
		bool RenderPlayerSpritesInScene() const override { return false; }
		bool GetTeleportLocation(DVector3&) const override { return false; }
		bool IsInitialized() const override { return EnsureInitialized(); }
		bool RenderDesktopMirror(VulkanRenderDevice*, VulkanImage*) const override { return false; }
		void Present() const override {}

	private:
		void DisableForRun(const char* reason) const
		{
			if (mDisabled)
			{
				return;
			}

			DestroyOpenXR();
			mDisabled = true;
			V_FallbackOpenXRStartup("OpenXR virtual-screen presentation failed", reason);
		}

		void DestroyOpenXR() const
		{
			mCurrentSwapchainTexture = nullptr;
			mSwapchainImageIndex = UINT32_MAX;
			mSwapchainAcquiredThisFrame = false;
			mVirtualScreenPopulatedThisFrame = false;
			mFrameInProgress = false;
			mSessionRunning = false;
			mSessionState = XR_SESSION_STATE_UNKNOWN;

			mSwapchainTextures.clear();
			mSwapchainImages.clear();
			mViewConfigs.clear();

			if (mSwapchain != XR_NULL_HANDLE)
			{
				xrDestroySwapchain(mSwapchain);
				mSwapchain = XR_NULL_HANDLE;
			}
			if (mSpace != XR_NULL_HANDLE)
			{
				xrDestroySpace(mSpace);
				mSpace = XR_NULL_HANDLE;
			}
			if (mSession != XR_NULL_HANDLE)
			{
				xrDestroySession(mSession);
				mSession = XR_NULL_HANDLE;
			}
			if (mInstance != XR_NULL_HANDLE)
			{
				xrDestroyInstance(mInstance);
				mInstance = XR_NULL_HANDLE;
			}

			mInitialized = false;
			mVirtualScreenWidth = 0;
			mVirtualScreenHeight = 0;
			mSwapchainFormat = VK_FORMAT_UNDEFINED;
		}

		bool EnsureInitialized() const
		{
			if (mDisabled)
			{
				return false;
			}
			if (mInitialized)
			{
				return true;
			}
			if (mInstance != XR_NULL_HANDLE || mSession != XR_NULL_HANDLE || mSpace != XR_NULL_HANDLE || mSwapchain != XR_NULL_HANDLE)
			{
				DestroyOpenXR();
			}
			OpenXRBootstrapInfo xrInfo;
			if (!QueryOpenXRVulkanRequirements(xrInfo))
			{
				const std::string error = GetLastOpenXRError();
				DisableForRun(error.empty() ? "runtime probe failed" : error.c_str());
				return false;
			}
			auto* vkfb = dynamic_cast<VulkanRenderDevice*>(screen);
			if (vkfb == nullptr || vkfb->device == nullptr || vkfb->device->Instance == nullptr)
			{
				DisableForRun("vulkan device unavailable");
				return false;
			}

			std::vector<const char*> enabledExtensions;
			for (const auto& ext : xrInfo.requiredInstanceExtensions)
			{
				enabledExtensions.push_back(ext.c_str());
			}

			XrApplicationInfo appInfo{};
			appInfo.apiVersion = XR_API_VERSION_1_0;
			appInfo.applicationVersion = 1;
			appInfo.engineVersion = 1;
			strncpy(appInfo.applicationName, GAMENAME, sizeof(appInfo.applicationName) - 1);
			strncpy(appInfo.engineName, GAMENAME, sizeof(appInfo.engineName) - 1);

			XrInstanceCreateInfo instanceInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
			instanceInfo.applicationInfo = appInfo;
			instanceInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
			instanceInfo.enabledExtensionNames = enabledExtensions.data();
			if (XR_FAILED(xrCreateInstance(&instanceInfo, &mInstance)))
			{
				DisableForRun("instance creation failed");
				return false;
			}

			XrSystemGetInfo systemInfo{ XR_TYPE_SYSTEM_GET_INFO };
			systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
			if (XR_FAILED(xrGetSystem(mInstance, &systemInfo, &mSystemId)))
			{
				DisableForRun("system selection failed");
				return false;
			}

			XrSessionCreateInfo sessionInfo{ XR_TYPE_SESSION_CREATE_INFO };
			XrGraphicsBindingVulkanKHR binding{ XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR };
			binding.instance = vkfb->device->Instance->Instance;
			binding.physicalDevice = vkfb->device->PhysicalDevice.Device;
			binding.device = vkfb->device->device;
			binding.queueFamilyIndex = (uint32_t)vkfb->device->GraphicsFamily;
			binding.queueIndex = 0;
			sessionInfo.next = &binding;
			sessionInfo.systemId = mSystemId;
			if (XR_FAILED(xrCreateSession(mInstance, &sessionInfo, &mSession)))
			{
				DisableForRun("session creation failed");
				return false;
			}

			XrReferenceSpaceCreateInfo spaceInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
			spaceInfo.poseInReferenceSpace = XrPosef{ {0, 0, 0, 1}, {0, 0, 0} };
			spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
			if (XR_FAILED(xrCreateReferenceSpace(mSession, &spaceInfo, &mSpace)))
			{
				spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
				if (XR_FAILED(xrCreateReferenceSpace(mSession, &spaceInfo, &mSpace)))
				{
					DisableForRun("reference space creation failed");
					return false;
				}
			}

			uint32_t viewCount = 0;
			if (XR_SUCCEEDED(xrEnumerateViewConfigurationViews(mInstance, mSystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr)) && viewCount > 0)
			{
				mViewConfigs.assign(viewCount, XrViewConfigurationView{ XR_TYPE_VIEW_CONFIGURATION_VIEW });
				if (XR_FAILED(xrEnumerateViewConfigurationViews(mInstance, mSystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, mViewConfigs.data())))
				{
					mViewConfigs.clear();
				}
			}

			mVirtualScreenWidth = 1920;
			mVirtualScreenHeight = 1080;
			for (const auto& view : mViewConfigs)
			{
				mVirtualScreenWidth = std::max(mVirtualScreenWidth, view.recommendedImageRectWidth);
				mVirtualScreenHeight = std::max(mVirtualScreenHeight, view.recommendedImageRectHeight);
			}

			if (!CreateSwapchain())
			{
				DisableForRun("swapchain creation failed");
				return false;
			}

			mQuadPose = XrPosef{ {0, 0, 0, 1}, {0.0f, 0.0f, -1.8f} };
			const float aspect = mVirtualScreenHeight != 0 ? (float)mVirtualScreenWidth / (float)mVirtualScreenHeight : 1.7777778f;
			mQuadLayer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
			mQuadLayer.space = mSpace;
			mQuadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			mQuadLayer.pose = mQuadPose;
			mQuadLayer.size = { 1.6f, 1.6f / aspect };
			mQuadLayer.subImage.swapchain = mSwapchain;
			mQuadLayer.subImage.imageArrayIndex = 0;
			mQuadLayer.subImage.imageRect.offset = { 0, 0 };
			mQuadLayer.subImage.imageRect.extent = { (int32_t)mVirtualScreenWidth, (int32_t)mVirtualScreenHeight };

			mInitialized = true;
			mSessionState = XR_SESSION_STATE_UNKNOWN;
            return true;
		}

		bool CreateSwapchain() const
		{
			if (mSession == XR_NULL_HANDLE)
			{
				return false;
			}
			if (mSwapchain != XR_NULL_HANDLE)
			{
				return true;
			}

			uint32_t formatCount = 0;
			if (XR_FAILED(xrEnumerateSwapchainFormats(mSession, 0, &formatCount, nullptr)) || formatCount == 0)
			{
				return false;
			}

			std::vector<int64_t> formats(formatCount);
			if (XR_FAILED(xrEnumerateSwapchainFormats(mSession, formatCount, &formatCount, formats.data())))
			{
				return false;
			}

			const int64_t preferredFormats[] =
			{
				VK_FORMAT_R8G8B8A8_UNORM,
				VK_FORMAT_B8G8R8A8_UNORM,
				VK_FORMAT_R16G16B16A16_SFLOAT
			};
			mSwapchainFormat = VK_FORMAT_UNDEFINED;
			for (int64_t preferred : preferredFormats)
			{
				if (std::find(formats.begin(), formats.end(), preferred) != formats.end())
				{
					mSwapchainFormat = preferred;
					break;
				}
			}
			if (mSwapchainFormat == VK_FORMAT_UNDEFINED)
			{
				mSwapchainFormat = formats[0];
			}

			XrSwapchainCreateInfo swapchainInfo{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
			swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
			swapchainInfo.format = mSwapchainFormat;
			swapchainInfo.sampleCount = 1;
			swapchainInfo.width = mVirtualScreenWidth;
			swapchainInfo.height = mVirtualScreenHeight;
			swapchainInfo.faceCount = 1;
			swapchainInfo.arraySize = 1;
			swapchainInfo.mipCount = 1;
			if (XR_FAILED(xrCreateSwapchain(mSession, &swapchainInfo, &mSwapchain)))
			{
				return false;
			}

			uint32_t imageCount = 0;
			if (XR_FAILED(xrEnumerateSwapchainImages(mSwapchain, 0, &imageCount, nullptr)) || imageCount == 0)
			{
				return false;
			}

			mSwapchainImages.assign(imageCount, XrSwapchainImageVulkanKHR{ XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });
			if (XR_FAILED(xrEnumerateSwapchainImages(mSwapchain, imageCount, &imageCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(mSwapchainImages.data()))))
			{
				return false;
			}

			mSwapchainTextures.resize(imageCount);
			auto* vkfb = dynamic_cast<VulkanRenderDevice*>(screen);
			if (vkfb == nullptr || vkfb->device == nullptr)
			{
				return false;
			}

			for (uint32_t i = 0; i < imageCount; ++i)
			{
				auto& tex = mSwapchainTextures[i];
				tex.Image = std::make_unique<VulkanImage>(vkfb->device.get(), mSwapchainImages[i].image, nullptr, (int)mVirtualScreenWidth, (int)mVirtualScreenHeight, 1, 1);
				tex.View = ImageViewBuilder()
					.Image(tex.Image.get(), (VkFormat)mSwapchainFormat)
					.DebugName("DoomXRMode.XRSwapchainView")
					.Create(vkfb->device.get());
				tex.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
				tex.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			}

			return true;
		}

		mutable bool mInitialized = false;
		mutable bool mDisabled = false;
		mutable XrInstance mInstance = XR_NULL_HANDLE;
		mutable XrSystemId mSystemId = XR_NULL_SYSTEM_ID;
		mutable XrSession mSession = XR_NULL_HANDLE;
		mutable XrSpace mSpace = XR_NULL_HANDLE;
		mutable XrSessionState mSessionState = XR_SESSION_STATE_UNKNOWN;
		mutable bool mSessionRunning = false;
				mutable bool mFrameInProgress = false;
		mutable bool mSwapchainAcquiredThisFrame = false;
		mutable bool mVirtualScreenPopulatedThisFrame = false;
		mutable XrFrameState mFrameState{ XR_TYPE_FRAME_STATE };
		mutable XrSwapchain mSwapchain = XR_NULL_HANDLE;
		mutable int64_t mSwapchainFormat = VK_FORMAT_UNDEFINED;
		mutable uint32_t mSwapchainImageIndex = UINT32_MAX;
		mutable uint32_t mVirtualScreenWidth = 0;
		mutable uint32_t mVirtualScreenHeight = 0;
		mutable XrPosef mQuadPose{};
		mutable XrCompositionLayerQuad mQuadLayer{ XR_TYPE_COMPOSITION_LAYER_QUAD };
		mutable VkTextureImage* mCurrentSwapchainTexture = nullptr;
		mutable std::vector<XrViewConfigurationView> mViewConfigs;
		mutable std::vector<XrSwapchainImageVulkanKHR> mSwapchainImages;
		mutable std::vector<VkTextureImage> mSwapchainTextures;
	};
#else
	class DoomXRMode final : public VRMode
	{
	public:
		DoomXRMode() : VRMode(1, 1.f, 1.f, 1.f, vrmi_openxr_eyes) {}

		bool IsVR() const override { return true; }
		bool GetRecommendedRenderSize(int& outWidth, int& outHeight) const override { outWidth = 0; outHeight = 0; return false; }
		bool ShouldUseRecommendedRenderSizeThisFrame() const override { return false; }
		bool SupportsMultiview() const override { return false; }
		bool ShouldUseMultiviewThisFrame() const override { return false; }
		bool ShouldUseScreenLayerForCurrentFrame() const override { return false; }
		void SetupOverlay() override {}
		void UpdateOverlaySettings() const override {}
		void DrawMountedHud(HWDrawInfo*, FRenderState&) const override {}
		bool IsRenderingVirtualScreen() const override { return false; }
		bool RenderVirtualScreen() const override { return false; }
		void FinalizeEyeImage(VulkanRenderDevice*, int) const override {}
		void PollXREvents() const override {}
		bool BeginXRFrame() const override { return false; }
		bool AcquireXRSwapchain() const override { return false; }
		bool SubmitFrame() const override { return false; }
		bool GetHandTransform(int, VSMatrix*) const override { return false; }
		bool GetWeaponTransform(VSMatrix* out, int hand = VR_MAINHAND) const override { return VRMode::GetWeaponTransform(out, hand); }
		bool RenderPlayerSpritesInScene() const override { return false; }
		bool GetTeleportLocation(DVector3&) const override { return false; }
		bool IsInitialized() const override { return true; }
		bool RenderDesktopMirror(VulkanRenderDevice*, VulkanImage*) const override { return false; }
		void Present() const override {}
	};
#endif

	static DoomXRMode vrmi_openxr;

	static float DEG2RAD(float deg)
	{
		return deg * float(M_PI / 180.0);
	}

	static float RAD2DEG(float rad)
	{
		return rad * float(180. / M_PI);
	}
}

VREyeInfo::VREyeInfo(float shiftFactor, float scaleFactor)
{
	mShiftFactor = shiftFactor;
	mScaleFactor = scaleFactor;
}

float VREyeInfo::getShift() const
{
	auto res = mShiftFactor * vr_ipd;
	return vr_swap_eyes ? -res : res;
}

VSMatrix VREyeInfo::GetProjection(float fov, float aspectRatio, float fovRatio, bool iso_ortho) const
{
	VSMatrix result;

	if (iso_ortho) // Orthographic projection for isometric viewpoint
	{
		double zNear = -3.0 / fovRatio; // screen->GetZNear();
		double zFar = screen->GetZFar();

		double fH = tan(DEG2RAD(fov) / 2) / fovRatio;
		double fW = fH * aspectRatio * mScaleFactor;
		double left = -fW;
		double right = fW;
		double bottom = -fH;
		double top = fH;

		VSMatrix fmat(1);
		fmat.ortho((float)left, (float)right, (float)bottom, (float)top, (float)zNear, (float)zFar);
		return fmat;
	}
	else if (mShiftFactor == 0)
	{
		float fovy = (float)(2 * RAD2DEG(atan(tan(DEG2RAD(fov) / 2) / fovRatio)));
		result.perspective(fovy, aspectRatio, screen->GetZNear(), screen->GetZFar());
		return result;
	}
	else
	{
		double zNear = screen->GetZNear();
		double zFar = screen->GetZFar();

		// For stereo 3D, use asymmetric frustum shift in projection matrix
		// Q: shouldn't shift vary with roll angle, at least for desktop display?
		// A: No. (lab) roll is not measured on desktop display (yet)
		double frustumShift = zNear * getShift() / vr_screendist; // meters cancel, leaving doom units
		double fH = zNear * tan(DEG2RAD(fov) / 2) / fovRatio;
		double fW = fH * aspectRatio * mScaleFactor;
		double left = -fW - frustumShift;
		double right = fW - frustumShift;
		double bottom = -fH;
		double top = fH;

		VSMatrix fmat(1);
		fmat.frustum((float)left, (float)right, (float)bottom, (float)top, (float)zNear, (float)zFar);
		return fmat;
	}
}

VSMatrix VREyeInfo::GetHUDProjection() const
{
	VSMatrix mat;
	int w = screen->GetWidth();
	int h = screen->GetHeight();
	mat.ortho(0.f, (float)w, (float)h, 0.f, -1.0f, 1.0f);
	return mat;
}

/* virtual */
DVector3 VREyeInfo::GetViewShift(float yaw) const
{
	if (mShiftFactor == 0)
	{
		// pass-through for Mono view
		return { 0, 0, 0 };
	}
	else
	{
		double dx = -cos(DEG2RAD(yaw)) * vr_hunits_per_meter * getShift();
		double dy = sin(DEG2RAD(yaw)) * vr_hunits_per_meter * getShift();
		return { dx, dy, 0 };
	}
}

VRMode::VRMode(int eyeCount, float horizontalViewportScale, float verticalViewportScale,
	float weaponProjectionScale, const VREyeInfo* eyes)
{
	mEyeCount = eyeCount;
	mHorizontalViewportScale = horizontalViewportScale;
	mVerticalViewportScale = verticalViewportScale;
	mWeaponProjectionScale = weaponProjectionScale;

	if (eyes != nullptr)
	{
		mEyes[0] = eyes[0];
		mEyes[1] = eyes[1];
	}
	else
	{
		mEyes[0] = VREyeInfo(0.f, 1.f);
		mEyes[1] = VREyeInfo(0.f, 0.f);
	}
}

const VRMode *VRMode::GetVRMode(bool toscreen)
{
	static VRMode vrmi_mono_mode(1, 1.f, 1.f, 1.f, vrmi_mono_eyes);
	static VRMode vrmi_stereo_mode(2, 1.f, 1.f, 1.f, vrmi_stereo_eyes);
	static VRMode vrmi_sbsfull_mode(2, .5f, 1.f, 2.f, vrmi_sbsfull_eyes);
	static VRMode vrmi_sbssquished_mode(2, .5f, 1.f, 1.f, vrmi_sbssquished_eyes);
	static VRMode vrmi_lefteye_mode(1, 1.f, 1.f, 1.f, vrmi_lefteye_eyes);
	static VRMode vrmi_righteye_mode(1, 1.f, 1.f, 1.f, vrmi_righteye_eyes);
	static VRMode vrmi_topbottom_mode(2, 1.f, .5f, 1.f, vrmi_topbottom_eyes);
	static VRMode vrmi_checker_mode(2, isqrt2, isqrt2, 1.f, vrmi_checker_eyes);

	int mode = !toscreen || (sysCallbacks.DisableTextureFilter && sysCallbacks.DisableTextureFilter()) ? 0 : vr_mode;

	switch (mode)
	{
	default:
	case VR_MONO:
		return &vrmi_mono_mode;

	case VR_GREENMAGENTA:
	case VR_REDCYAN:
	case VR_QUADSTEREO:
	case VR_AMBERBLUE:
	case VR_SIDEBYSIDELETTERBOX:
		return &vrmi_stereo_mode;

	case VR_SIDEBYSIDESQUISHED:
	case VR_COLUMNINTERLEAVED:
		return &vrmi_sbssquished_mode;

	case VR_SIDEBYSIDEFULL:
		return &vrmi_sbsfull_mode;

	case VR_TOPBOTTOM:
	case VR_ROWINTERLEAVED:
		return &vrmi_topbottom_mode;

	case VR_LEFTEYEVIEW:
		return &vrmi_lefteye_mode;

	case VR_RIGHTEYEVIEW:
		return &vrmi_righteye_mode;

	case VR_CHECKERINTERLEAVED:
		return &vrmi_checker_mode;

	case VR_OPENXR:
        return &vrmi_openxr;
	}
}

const VRMode *VRMode::GetVRModeCached(bool toscreen)
{
	extern thread_local bool isWorkerThread;
	if (isWorkerThread)
	{
		static VRMode safeMono(1, 1.f, 1.f, 1.f, vrmi_mono_eyes);
		return &safeMono;
	}

	struct CacheEntry
	{
		bool valid = false;
		uint64_t frameTime = 0;
		int vrMode = 0;
		int backend = 0;
		bool disableTextureFilter = false;
		const VRMode* mode = nullptr;
	};

	thread_local CacheEntry cache[2];
	auto& entry = cache[toscreen ? 1 : 0];
	const uint64_t frameTime = screen != nullptr ? screen->FrameTime : 0;
	const int currentVrMode = (int)vr_mode;
	const int currentBackend = V_GetBackend();
	const bool currentDisableTextureFilter = sysCallbacks.DisableTextureFilter && sysCallbacks.DisableTextureFilter();

	if (entry.valid &&
		entry.frameTime == frameTime &&
		entry.vrMode == currentVrMode &&
		entry.backend == currentBackend &&
		entry.disableTextureFilter == currentDisableTextureFilter)
	{
		return entry.mode;
	}

	entry.valid = true;
	entry.frameTime = frameTime;
	entry.vrMode = currentVrMode;
	entry.backend = currentBackend;
	entry.disableTextureFilter = currentDisableTextureFilter;
	entry.mode = GetVRMode(toscreen);
	return entry.mode;
}

void VRMode::AdjustViewport(DFrameBuffer *screen) const
{
	screen->mSceneViewport.height = (int)(screen->mSceneViewport.height * mVerticalViewportScale);
	screen->mSceneViewport.top = (int)(screen->mSceneViewport.top * mVerticalViewportScale);
	screen->mSceneViewport.width = (int)(screen->mSceneViewport.width * mHorizontalViewportScale);
	screen->mSceneViewport.left = (int)(screen->mSceneViewport.left * mHorizontalViewportScale);

	screen->mScreenViewport.height = (int)(screen->mScreenViewport.height * mVerticalViewportScale);
	screen->mScreenViewport.top = (int)(screen->mScreenViewport.top * mVerticalViewportScale);
	screen->mScreenViewport.width = (int)(screen->mScreenViewport.width * mHorizontalViewportScale);
	screen->mScreenViewport.left = (int)(screen->mScreenViewport.left * mHorizontalViewportScale);
}

VSMatrix VRMode::GetHUDSpriteProjection() const
{
	VSMatrix mat;
	int w = screen->GetWidth();
	int h = screen->GetHeight();
	float scaled_w = w / mWeaponProjectionScale;
	float left_ofs = (w - scaled_w) / 2.f;
	mat.ortho(left_ofs, left_ofs + scaled_w, (float)h, 0, -1.0f, 1.0f);
	return mat;
}

VSMatrix VRMode::GetHUDProjection() const
{
	return GetHUDSpriteProjection();
}

void VRMode::Present() const
{
}

bool VRMode::GetWeaponTransform(VSMatrix* out, int hand) const
{
	if (out == nullptr)
	{
		return false;
	}

	return GetHandTransform(hand, out);
}
