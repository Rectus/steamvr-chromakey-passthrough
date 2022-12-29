
#pragma once

#include <thread>
#include <mutex>
#include <atomic>
#include "passthrough_renderer.h"
#include "openvr_manager.h"
#include "shared_structs.h"

enum ETrackedCameraFrameType
{
	VRFrameType_Distorted = 0,
	VRFrameType_Undistorted,
	VRFrameType_MaximumUndistorted
};

#define POSTFRAME_SLEEP_INTERVAL (std::chrono::milliseconds(10))
#define FRAME_POLL_INTERVAL (std::chrono::microseconds(100))


class CameraManager
{
public:

	CameraManager(std::shared_ptr<PassthroughRenderer> renderer, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager);
	~CameraManager();

	bool InitCamera();
	void DeinitCamera();

	void GetFrameSize(uint32_t& width, uint32_t& height, uint32_t& bufferSize);
	void UpdateStaticCameraParameters();
	bool GetCameraFrame(std::shared_ptr<CameraFrame>& frame);
	void CalculateFrameProjection(std::shared_ptr<CameraFrame>& frame, RenderFrame& renderFrame);

private:
	void ServeFrames();
	void GetTrackedCameraEyePoses(Matrix4& LeftPose, Matrix4& RightPose);
	Matrix4 GetHMDViewToTrackingMatrix(const ERenderEye eye);
	void CalculateFrameProjectionForEye(const ERenderEye eye, std::shared_ptr<CameraFrame>& frame, RenderFrame& renderFrame);

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<OpenVRManager> m_openVRManager;

	bool m_bCameraInitialized = false;

	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameBufferSize;

	float m_projectionDistanceFar;
	float m_projectionDistanceNear;

	std::weak_ptr<PassthroughRenderer> m_renderer;
	std::thread m_serveThread;
	std::atomic_bool m_bRunThread = true;
	std::mutex m_serveMutex;

	std::shared_ptr<CameraFrame> m_renderFrame;
	std::shared_ptr<CameraFrame> m_servedFrame;
	std::shared_ptr<CameraFrame> m_underConstructionFrame;

	int m_hmdDeviceId = -1;
	vr::EVRTrackedCameraFrameType m_frameType;
	vr::TrackedCameraHandle_t m_cameraHandle;
	EStereoFrameLayout m_frameLayout;

	Matrix4 m_rawHMDProjectionLeft{};
	Matrix4 m_rawHMDViewLeft{};
	Matrix4 m_rawHMDProjectionRight{};
	Matrix4 m_rawHMDViewRight{};

	Matrix4 m_cameraProjectionInvFarLeft{};
	Matrix4 m_cameraProjectionInvFarRight{};

	Matrix4 m_cameraLeftToHMDPose{};
	Matrix4 m_cameraLeftToRightPose{};
};

