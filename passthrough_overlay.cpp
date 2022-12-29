
#include "pch.h"
#include "passthrough_overlay.h"


PassthroughOverlay::PassthroughOverlay(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, ERenderEye eye)
	: m_configManager(configManager)
	, m_openVRManager(openVRManager)
	, m_overlayHandle(vr::k_ulOverlayHandleInvalid)
	, m_eye(eye)
{
	vr::IVROverlay* vrOverlay = m_openVRManager->GetVROverlay();

	if (!vrOverlay)
	{
		return;
	}

	std::string overlayKey = std::format(OVERLAY_KEY, GetCurrentProcessId(), (m_eye == LEFT_EYE) ? "left" : "right");

	vr::EVROverlayError error = vrOverlay->FindOverlay(overlayKey.c_str(), &m_overlayHandle);
	if (error != vr::EVROverlayError::VROverlayError_None && error != vr::EVROverlayError::VROverlayError_UnknownOverlay)
	{
		Log("Warning: SteamVR FindOverlay error (%d)\n", error);
	}

	if (m_overlayHandle != vr::k_ulOverlayHandleInvalid)
	{
		return;
	}

	error = vrOverlay->CreateOverlay(overlayKey.c_str(), "Chroma Key Passthrough", &m_overlayHandle);
	if (error != vr::EVROverlayError::VROverlayError_None)
	{
		ErrorLog("SteamVR overlay init error (%d)\n", error);
	}
	else
	{
		vrOverlay->SetOverlayInputMethod(m_overlayHandle, vr::VROverlayInputMethod_None);

		vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_IsPremultiplied, true);
		vrOverlay->SetOverlayFlag(m_overlayHandle, vr::VROverlayFlags_SortWithNonSceneOverlays, true);

		vr::VRTextureBounds_t bounds;
	
		bounds.uMin = 0.0f;
		bounds.uMax = 1.0f;
		bounds.vMin = 0.0f;
		bounds.vMax = 1.0f;

		vrOverlay->SetOverlayTextureBounds(m_overlayHandle, &bounds);
	}

}

PassthroughOverlay::~PassthroughOverlay()
{
	vr::IVROverlay* vrOverlay = m_openVRManager->GetVROverlay();

	if (vrOverlay && m_overlayHandle != vr::k_ulOverlayHandleInvalid)
	{
		vrOverlay->DestroyOverlay(m_overlayHandle);
	}
}

void PassthroughOverlay::SetOverlayVisible(bool bVisible)
{
	vr::IVROverlay* vrOverlay = m_openVRManager->GetVROverlay();

	if (bVisible)
	{
		vrOverlay->ShowOverlay(m_overlayHandle);
	}
	else
	{
		vrOverlay->HideOverlay(m_overlayHandle);
	}
}

void PassthroughOverlay::SubmitOverlay(RenderFrame& frame)
{
	vr::IVROverlay* vrOverlay = m_openVRManager->GetVROverlay();

	

	ComPtr<ID3D11Texture2D> renderTarget = (m_eye == LEFT_EYE) ? frame.textureLeft : frame.textureRight;


	if (!renderTarget.Get())
	{
		return;
	}

	vr::Texture_t texture;
	texture.eColorSpace = vr::ColorSpace_Auto;
	texture.eType = vr::TextureType_DXGISharedHandle;
	//texture.eType = vr::TextureType_DirectX;
	//texture.handle = renderTarget.Get();

	ComPtr<IDXGIResource> DXGIResource;
	renderTarget->QueryInterface(IID_PPV_ARGS(&DXGIResource));
	DXGIResource->GetSharedHandle(&texture.handle);

	vr::EVROverlayError error = vrOverlay->SetOverlayTexture(m_overlayHandle, &texture);
	if (error != vr::VROverlayError_None)
	{
		ErrorLog("SteamVR had an error on updating the passthrough overlay (%d)\n", error);
	}

	vr::EVREye vrEye = (m_eye == LEFT_EYE) ? vr::Eye_Left : vr::Eye_Right;

	vr::VROverlayProjection_t projection;
	vr::VRSystem()->GetProjectionRaw(vrEye, &projection.fLeft, &projection.fRight, &projection.fTop, &projection.fBottom);

	Matrix4 mat = (m_eye == LEFT_EYE) ? frame.hmdTrackingToViewLeft : frame.hmdTrackingToViewRight;
	vr::HmdMatrix34_t eyePose;

	eyePose.m[0][0] = mat[0]; eyePose.m[1][0] = mat[1]; eyePose.m[2][0] = mat[2];
	eyePose.m[0][1] = mat[4]; eyePose.m[1][1] = mat[5]; eyePose.m[2][1] = mat[6];
	eyePose.m[0][2] = mat[8]; eyePose.m[1][2] = mat[9]; eyePose.m[2][2] = mat[10];
	eyePose.m[0][3] = mat[12]; eyePose.m[1][3] = mat[13]; eyePose.m[2][3] = mat[14];

	error = vr::VROverlay()->SetOverlayTransformProjection(m_overlayHandle, vr::TrackingUniverseStanding, &eyePose, &projection, vrEye);
	if (error != vr::VROverlayError_None)
	{
		ErrorLog("SteamVRerror on projecting overlay (%d)\n", error);

	}

}
