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
#include "version.h"
#include "i_interface.h"

// Set up 3D-specific console variables:
CVAR(Int, vr_mode, 0, CVAR_GLOBALCONFIG|CVAR_ARCHIVE)

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
