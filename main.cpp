
#include "pch.h"
#include "shared_structs.h"
#include "logging.h"
#include "passthrough_renderer.h"
#include "camera_manager.h"
#include "config_manager.h"
#include "dashboard_menu.h"
#include "openvr_manager.h"
#include "passthrough_overlay.h"

#include "renderdoc_app.h"

#define CONFIG_FILE_DIR L"\\SteamVR Chroma Key Passthrough\\"
#define CONFIG_FILE_NAME L"config.ini"
#define LOG_FILE_NAME L"SteamVR Chroma Key Passthrough.log"

#define PERF_TIME_AVERAGE_VALUES 20



float UpdateAveragePerfTime(std::deque<float>& times, float newTime)
{
	if (times.size() >= PERF_TIME_AVERAGE_VALUES)
	{
		times.pop_front();
	}

	times.push_back(newTime);

	float average = 0;

	for (const float& val : times)
	{
		average += val;
	}
	return average / times.size();
}




int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	InitLogging(LOG_FILE_NAME);

	Log("Starting passthrough system...\n");

	RENDERDOC_API_1_5_0* renderdocAPI = NULL;
	{
		HMODULE module = GetModuleHandleA("renderdoc.dll");
		if (module)
		{
			pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(module, "RENDERDOC_GetAPI");
			if (RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_5_0, (void**)&renderdocAPI) != 1)
			{
				ErrorLog("Error: Failed to load RenderDoc module!\n");
				return 1;
			}
		}
	}
	

	PWSTR path;
	std::wstring filePath(PATHCCH_MAX_CCH, L'\0');

	SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &path);
	lstrcpyW((PWSTR)filePath.c_str(), path);
	PathCchAppend((PWSTR)filePath.data(), PATHCCH_MAX_CCH, CONFIG_FILE_DIR);
	CreateDirectoryW((PWSTR)filePath.data(), NULL);
	PathCchAppend((PWSTR)filePath.data(), PATHCCH_MAX_CCH, CONFIG_FILE_NAME);

	std::shared_ptr<ConfigManager> configManager = std::make_shared<ConfigManager>(filePath);
	configManager->ReadConfigFile();

	std::shared_ptr<OpenVRManager> openVRManager = std::make_shared<OpenVRManager>();
	std::unique_ptr<DashboardMenu> dashboardMenu = std::make_unique<DashboardMenu>(configManager, openVRManager);

	
	vr::IVRSystem* vrSystem = openVRManager->GetVRSystem();
	vr::IVROverlay* vrOverlay = openVRManager->GetVROverlay();

	if (!vrSystem || !vrOverlay)
	{
		ErrorLog("Error: Failed to connect to SteamVR!\n");
		return 1;
	}

	int32_t adapterIndex = 0;
	
	vrSystem->GetDXGIOutputInfo(&adapterIndex);

	std::shared_ptr<PassthroughRenderer> renderer = std::make_shared<PassthroughRenderer>(configManager, openVRManager, adapterIndex);
	std::unique_ptr<CameraManager> cameraManager = std::make_unique<CameraManager>(renderer, configManager, openVRManager);

	if (!cameraManager->InitCamera())
	{
		ErrorLog("Error: Failed to initialize camera!\n");
		return 1;
	}

	{
		uint32_t cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize;
		cameraManager->GetFrameSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize);
		renderer->SetFrameSize(cameraTextureWidth, cameraTextureHeight, cameraFrameBufferSize);
		if (!renderer->InitRenderer())
		{
			ErrorLog("Error: Failed to initialize renderer!\n");
			return 1;
		}
	}

	std::shared_ptr<PassthroughOverlay> passthroughOverlayLeft = std::make_shared<PassthroughOverlay>(configManager, openVRManager, LEFT_EYE);
	std::shared_ptr<PassthroughOverlay> passthroughOverlayRight = std::make_shared<PassthroughOverlay>(configManager, openVRManager, RIGHT_EYE);


	Log("Passthrough system initialized.\n");

	bool bRun = true;
	bool bDoCapture = false;
	
	RenderFrame renderFrame;

	std::deque<float> m_frameToRenderTimes;
	std::deque<float> m_frameToPhotonTimes;
	std::deque<float> m_passthroughRenderTimes;

	int hmdDeviceId = openVRManager->GetHMDDeviceId();

	while (bRun)
	{
		
		float displayFrequency = vrSystem->GetFloatTrackedDeviceProperty(hmdDeviceId, vr::Prop_DisplayFrequency_Float);
		//vrOverlay->WaitFrameSync(1000 / (unsigned int)displayFrequency);

		if (dashboardMenu->IsShutdownSignaled())
		{
			bRun = false;
		}

		

		if (dashboardMenu->IsCaptureSignaled() && renderdocAPI && renderdocAPI->IsTargetControlConnected())
		{
			renderdocAPI->StartFrameCapture(renderer->GetRenderDevice(), NULL);
			bDoCapture = true;
		}

		if (!dashboardMenu->IsPassthroughEnabled())
		{
			passthroughOverlayLeft->SetOverlayVisible(false);
			passthroughOverlayRight->SetOverlayVisible(false);
			continue;
		}

		passthroughOverlayLeft->SetOverlayVisible(true);
		passthroughOverlayRight->SetOverlayVisible(true);

		std::shared_ptr<CameraFrame> frame;

		if (!cameraManager->GetCameraFrame(frame))
		{
			continue;
		}


		LARGE_INTEGER perfFrequency;
		LARGE_INTEGER preRenderTime;

		QueryPerformanceFrequency(&perfFrequency);
		QueryPerformanceCounter(&preRenderTime);

		double frameToRenderTime = (float)(preRenderTime.QuadPart - frame->header.ulFrameExposureTime);
		frameToRenderTime *= 1000.0f;
		frameToRenderTime /= perfFrequency.QuadPart;
		dashboardMenu->GetDisplayValues().frameToRenderLatencyMS = UpdateAveragePerfTime(m_frameToRenderTimes, (float)frameToRenderTime);


		uint64_t currentFrame;
		float timeSinceVsync;
		vrSystem->GetTimeSinceLastVsync(&timeSinceVsync, &currentFrame);
		float vsyncToPhotons = vrSystem->GetFloatTrackedDeviceProperty(hmdDeviceId, vr::Prop_SecondsFromVsyncToPhotons_Float);
		float frameDuration = 1.0f / displayFrequency;
		float displayTime = (2.0f * frameDuration - timeSinceVsync + vsyncToPhotons) * 1000.0f;

		dashboardMenu->GetDisplayValues().frameToPhotonsLatencyMS = UpdateAveragePerfTime(m_frameToPhotonTimes, displayTime);

		cameraManager->CalculateFrameProjection(frame, renderFrame);

		renderer->RenderPassthroughFrame(frame, renderFrame);

		vrOverlay->WaitFrameSync(1000 / (unsigned int)displayFrequency);

		passthroughOverlayLeft->SubmitOverlay(renderFrame);
		passthroughOverlayRight->SubmitOverlay(renderFrame);

		if (bDoCapture)
		{
			renderdocAPI->EndFrameCapture(renderer->GetRenderDevice(), NULL);
			bDoCapture = false;
		}



		LARGE_INTEGER postRenderTime;
		QueryPerformanceCounter(&postRenderTime);

		float renderTime = (float)(postRenderTime.QuadPart - preRenderTime.QuadPart);
		renderTime *= 1000.0f;
		renderTime /= perfFrequency.QuadPart;
		dashboardMenu->GetDisplayValues().renderTimeMS = UpdateAveragePerfTime(m_passthroughRenderTimes, renderTime);



		
	}

	Log("Stopping passthrough system...\n");


	return 0;
}