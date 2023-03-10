
#pragma once

#include <thread>
#include "config_manager.h"
#include "openvr_manager.h"


using Microsoft::WRL::ComPtr;


#define DASHBOARD_OVERLAY_KEY "steamvr_chromakey_passthrough.{}.dashboard"

#define OVERLAY_RES_WIDTH 800
#define OVERLAY_RES_HEIGHT 420



struct MenuDisplayValues
{
	int frameBufferWidth = 0;
	int frameBufferHeight = 0;

	float frameToRenderLatencyMS = 0.0f;
	float frameToPhotonsLatencyMS = 0.0f;
	float renderTimeMS = 0.0f;
};


class DashboardMenu
{
public:

	DashboardMenu(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager);

	~DashboardMenu();
	
	MenuDisplayValues& GetDisplayValues() { return m_displayValues; }

	inline bool IsPassthroughEnabled() const { return m_bPassthroughEnabled; }
	inline bool IsShutdownSignaled() const { return m_bSignalShutdown; }

	inline bool IsCaptureSignaled()
	{
		if (m_bSignalCapture)
		{
			m_bSignalCapture = false;
			return true;
		}
		return false;
	}

private:

	void CreateOverlay();
	void DestroyOverlay();
	void CreateThumbnail();

	void RunThread();
	void HandleEvents();
	void TickMenu();

	void SetupDX11();

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<OpenVRManager> m_openVRManager;
	HMODULE m_dllModule;

	vr::VROverlayHandle_t m_overlayHandle;
	vr::VROverlayHandle_t m_thumbnailHandle;

	std::thread m_menuThread;
	bool m_bRunThread;

	ComPtr<ID3D11Device> m_d3d11Device;
	ComPtr<ID3D11DeviceContext> m_d3d11DeviceContext;
	ComPtr<ID3D11Texture2D> m_d3d11Texture;
	ComPtr<ID3D11RenderTargetView> m_d3d11RTV;

	bool m_bMenuIsVisible;
	MenuDisplayValues m_displayValues;

	bool m_bPassthroughEnabled;
	bool m_bSignalShutdown;
	bool m_bSignalCapture;
};

