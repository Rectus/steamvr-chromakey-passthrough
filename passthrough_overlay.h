#pragma once

#include "shared_structs.h"
#include "logging.h"
#include "config_manager.h"
#include "openvr_manager.h"
#include "camera_manager.h"

#define OVERLAY_KEY "steamvr_chromakey_passthrough.{}.projected.{}"

class PassthroughOverlay
{
public:

	PassthroughOverlay(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, ERenderEye eye);
	~PassthroughOverlay();

	void SetOverlayVisible(bool bVisible);
	void SubmitOverlay(RenderFrame& frame);

private:

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<OpenVRManager> m_openVRManager;

	vr::VROverlayHandle_t m_overlayHandle;
	ERenderEye m_eye;
};