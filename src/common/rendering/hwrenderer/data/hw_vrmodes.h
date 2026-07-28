#pragma once

#include "matrix.h"

class DFrameBuffer;
class HWDrawInfo;
class FRenderState;
class VulkanRenderDevice;
class VulkanImage;

enum
{
	VR_MAINHAND = 0,
	VR_OFFHAND = 1
};

enum
{
	VR_MONO = 0,
	VR_GREENMAGENTA = 1,
	VR_REDCYAN = 2,
	VR_SIDEBYSIDEFULL = 3,
	VR_SIDEBYSIDESQUISHED = 4,
	VR_LEFTEYEVIEW = 5,
	VR_RIGHTEYEVIEW = 6,
	VR_QUADSTEREO = 7,
	VR_SIDEBYSIDELETTERBOX = 8,
	VR_AMBERBLUE = 9,
	VR_TOPBOTTOM = 11,
	VR_ROWINTERLEAVED = 12,
	VR_COLUMNINTERLEAVED = 13,
	VR_CHECKERINTERLEAVED = 14,
	VR_OPENXR = 15
};

struct VREyeInfo
{
	float mShiftFactor;
	float mScaleFactor;

	VREyeInfo(float shiftFactor = 0.f, float scaleFactor = 1.f);
	virtual ~VREyeInfo() = default;

	virtual VSMatrix GetProjection(float fov, float aspectRatio, float fovRatio, bool iso_ortho) const;
	virtual VSMatrix GetHUDProjection() const;
	virtual DVector3 GetViewShift(float yaw) const;
private:
	float getShift() const;

};

struct VRMode
{
	int mEyeCount;
	float mHorizontalViewportScale;
	float mVerticalViewportScale;
	float mWeaponProjectionScale;
	VREyeInfo mEyes[2];

	VRMode(int eyeCount = 1, float horizontalViewportScale = 1.f, float verticalViewportScale = 1.f,
		float weaponProjectionScale = 1.f, const VREyeInfo* eyes = nullptr);
	virtual ~VRMode() = default;

	static const VRMode *GetVRMode(bool toscreen = true);
	static const VRMode *GetVRModeCached(bool toscreen = true);
	virtual void AdjustViewport(DFrameBuffer *fb) const;
	virtual VSMatrix GetHUDProjection() const;
	VSMatrix GetHUDSpriteProjection() const;
	virtual void AdjustPlayerSprites(FRenderState& state, int hand = VR_MAINHAND) const {}
	virtual void UnAdjustPlayerSprites(FRenderState& state) const {}
	virtual bool IsVR() const { return false; }
	virtual bool GetRecommendedRenderSize(int& outWidth, int& outHeight) const { outWidth = 0; outHeight = 0; return false; }
	virtual bool ShouldUseRecommendedRenderSizeThisFrame() const { return false; }
	virtual bool SupportsMultiview() const { return false; }
	virtual bool ShouldUseMultiviewThisFrame() const { return false; }
	virtual bool ShouldUseScreenLayerForCurrentFrame() const { return false; }
	virtual void SetupOverlay() {}
	virtual void UpdateOverlaySettings() const {}
	virtual void DrawMountedHud(HWDrawInfo* di, FRenderState& state) const {}
	virtual bool IsRenderingVirtualScreen() const { return false; }
	virtual bool RenderVirtualScreen() const { return false; }
	virtual void FinalizeEyeImage(VulkanRenderDevice* fb, int eyeIndex) const {}
	virtual bool RenderDesktopMirror(VulkanRenderDevice* fb, VulkanImage* dstImage) const { return false; }
	virtual void Present() const;
	virtual void PollXREvents() const {}
	virtual bool BeginXRFrame() const { return false; }
	virtual bool AcquireXRSwapchain() const { return false; }
	virtual bool SubmitFrame() const { return false; }
	virtual bool GetHandTransform(int hand, VSMatrix* out) const { return false; }
	virtual bool GetWeaponTransform(VSMatrix* out, int hand = VR_MAINHAND) const;
	virtual bool RenderPlayerSpritesInScene() const { return false; }
	virtual bool GetTeleportLocation(DVector3& out) const { return false; }
	virtual bool IsInitialized() const { return true; }
};
bool RT_OpenXRGetWeaponTransform(VSMatrix* out, int hand = VR_MAINHAND);
bool RT_OpenXRGetWeaponAim(DVector3* outOrigin, DVector3* outDirection);
void RT_OpenXRAdjustPlayerSprites(FRenderState& state, int hand = VR_MAINHAND);
void RT_OpenXRUnAdjustPlayerSprites(FRenderState& state);
