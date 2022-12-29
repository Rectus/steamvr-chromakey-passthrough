
#include "pch.h"
#include "camera_manager.h"
#include "logging.h"


inline Matrix4 FromHMDMatrix34(vr::HmdMatrix34_t& in)
{
    return Matrix4(
        in.m[0][0], in.m[1][0], in.m[2][0], 0.0f,
        in.m[0][1], in.m[1][1], in.m[2][1], 0.0f,
        in.m[0][2], in.m[1][2], in.m[2][2], 0.0f,
        in.m[0][3], in.m[1][3], in.m[2][3], 1.0f
    );
}


inline Matrix4 FromHMDMatrix44(vr::HmdMatrix44_t& in)
{
    return Matrix4(
        in.m[0][0], in.m[1][0], in.m[2][0], in.m[3][0],
        in.m[0][1], in.m[1][1], in.m[2][1], in.m[3][1],
        in.m[0][2], in.m[1][2], in.m[2][2], in.m[3][2],
        in.m[0][3], in.m[1][3], in.m[2][3], in.m[3][3]
    );
}


CameraManager::CameraManager(std::shared_ptr<PassthroughRenderer> renderer, std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager)
    : m_renderer(renderer)
    , m_configManager(configManager)
    , m_openVRManager(openVRManager)
    , m_frameType(vr::VRTrackedCameraFrameType_MaximumUndistorted)
    , m_frameLayout(EStereoFrameLayout::Mono)
{
    m_projectionDistanceFar = 0.0f;
    m_projectionDistanceNear = 0.0f;

    m_renderFrame = std::make_shared<CameraFrame>();
    m_servedFrame = std::make_shared<CameraFrame>();
    m_underConstructionFrame = std::make_shared<CameraFrame>();

    m_renderFrame->frameUVProjectionLeft.identity();
    m_renderFrame->frameUVProjectionRight.identity();
    m_servedFrame->frameUVProjectionLeft.identity();
    m_servedFrame->frameUVProjectionRight.identity();
    m_underConstructionFrame->frameUVProjectionLeft.identity();
    m_underConstructionFrame->frameUVProjectionRight.identity();
}

CameraManager::~CameraManager()
{
    DeinitCamera();

    if (m_serveThread.joinable())
    {
        m_bRunThread = false;
        m_serveThread.join();
    }
}

bool CameraManager::InitCamera()
{
    if (m_bCameraInitialized) { return true; }

    m_hmdDeviceId = m_openVRManager->GetHMDDeviceId();
    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    if (!trackedCamera) 
    {
        ErrorLog("SteamVR Tracked Camera interface error!\n");
        return false; 
    }

    bool bHasCamera = false;
    vr::EVRTrackedCameraError error = trackedCamera->HasCamera(m_hmdDeviceId, &bHasCamera);
    if (error != vr::VRTrackedCameraError_None)
    {
        ErrorLog("Error %i checking camera on device %i\n", error, m_hmdDeviceId);
        return false;
    }
    else if(!bHasCamera)
    {
        ErrorLog("No passthrough camera found!\n");
        return false;
    }

    UpdateStaticCameraParameters();

    vr::EVRTrackedCameraError cameraError = trackedCamera->AcquireVideoStreamingService(m_hmdDeviceId, &m_cameraHandle);

    if (cameraError != vr::VRTrackedCameraError_None)
    {
        Log("AcquireVideoStreamingService error %i on device %i\n", (int)cameraError, m_hmdDeviceId);
        return false;
    }

    m_bCameraInitialized = true;
    m_bRunThread = true;

    if (!m_serveThread.joinable())
    {
        m_serveThread = std::thread(&CameraManager::ServeFrames, this);
    }

    return true;
}

void CameraManager::DeinitCamera()
{
    if (!m_bCameraInitialized) { return; }
    m_bCameraInitialized = false;
    m_bRunThread = false;

    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    if (trackedCamera)
    {
        vr::EVRTrackedCameraError error = trackedCamera->ReleaseVideoStreamingService(m_cameraHandle);

        if (error != vr::VRTrackedCameraError_None)
        {
            Log("ReleaseVideoStreamingService error %i\n", (int)error);
        }
    }

    if (m_serveThread.joinable())
    {
        m_serveThread.join();
    }
}

void CameraManager::GetFrameSize(uint32_t& width, uint32_t& height, uint32_t& bufferSize)
{
    width = m_cameraTextureWidth;
    height = m_cameraTextureHeight;
    bufferSize = m_cameraFrameBufferSize;
}

void CameraManager::GetTrackedCameraEyePoses(Matrix4& LeftPose, Matrix4& RightPose)
{
    vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();

    vr::HmdMatrix34_t Buffer[2];
    vr::TrackedPropertyError error;
    bool bGotLeftCamera = true;
    bool bGotRightCamera = true;

    uint32_t numBytes = vrSystem->GetArrayTrackedDeviceProperty(m_hmdDeviceId, vr::Prop_CameraToHeadTransforms_Matrix34_Array, vr::k_unHmdMatrix34PropertyTag, &Buffer, sizeof(Buffer), &error);
    if (error != vr::TrackedProp_Success || numBytes == 0)
    {
        ErrorLog("Failed to get tracked camera pose array, error [%i]\n",error);
        bGotLeftCamera = false;
        bGotRightCamera = false;
    }

    if (m_frameLayout == EStereoFrameLayout::StereoHorizontalLayout)
    {
        LeftPose = FromHMDMatrix34(Buffer[0]);
        RightPose = FromHMDMatrix34(Buffer[1]);
    }
    else if (m_frameLayout == EStereoFrameLayout::StereoVerticalLayout)
    {
        // Vertical layouts have the right camera at index 0.
        LeftPose = FromHMDMatrix34(Buffer[1]);
        RightPose = FromHMDMatrix34(Buffer[0]);

        // Hack to remove scaling from Vive Pro Eye matrix.
        LeftPose[5] = abs(LeftPose[5]);
        LeftPose[10] = abs(LeftPose[10]);
    }
    else
    {
        LeftPose = FromHMDMatrix34(Buffer[0]);
        RightPose = FromHMDMatrix34(Buffer[0]);
    }
}

void CameraManager::UpdateStaticCameraParameters()
{
    vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();
    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    vr::EVRTrackedCameraError cameraError = trackedCamera->GetCameraFrameSize(m_hmdDeviceId, m_frameType, &m_cameraTextureWidth, &m_cameraTextureHeight, &m_cameraFrameBufferSize);
    if (cameraError != vr::VRTrackedCameraError_None)
    {
        ErrorLog("CameraFrameSize error %i on device Id %i\n", cameraError, m_hmdDeviceId);
    }

    if (m_cameraTextureWidth == 0 || m_cameraTextureHeight == 0 || m_cameraFrameBufferSize == 0)
    {
        ErrorLog("Invalid frame size received:Width = %u, Height = %u, Size = %u\n", m_cameraTextureWidth, m_cameraTextureHeight, m_cameraFrameBufferSize);
    }

    vr::TrackedPropertyError propError;

    int32_t layout = (vr::EVRTrackedCameraFrameLayout)vrSystem->GetInt32TrackedDeviceProperty(m_hmdDeviceId, vr::Prop_CameraFrameLayout_Int32, &propError);

    if (propError != vr::TrackedProp_Success)
    {
        ErrorLog("GetTrackedCameraEyePoses error %i\n", propError);
    }

    if ((layout & vr::EVRTrackedCameraFrameLayout_Stereo) != 0)
    {
        if ((layout & vr::EVRTrackedCameraFrameLayout_VerticalLayout) != 0)
        {
            m_frameLayout = EStereoFrameLayout::StereoVerticalLayout;
        }
        else
        {
            m_frameLayout = EStereoFrameLayout::StereoHorizontalLayout;
        }
    }
    else
    {
        m_frameLayout = EStereoFrameLayout::Mono;
    }

    vr::HmdMatrix44_t vrHMDProjectionLeft = vrSystem->GetProjectionMatrix(vr::Hmd_Eye::Eye_Left, m_projectionDistanceFar * 0.1f, m_projectionDistanceFar * 2.0f);
    m_rawHMDProjectionLeft = FromHMDMatrix44(vrHMDProjectionLeft);

    vr::HmdMatrix34_t vrHMDViewLeft = vrSystem->GetEyeToHeadTransform(vr::Hmd_Eye::Eye_Left);
    m_rawHMDViewLeft = FromHMDMatrix34(vrHMDViewLeft).invert();

    vr::HmdMatrix44_t vrHMDProjectionRight = vrSystem->GetProjectionMatrix(vr::Hmd_Eye::Eye_Right, m_projectionDistanceFar * 0.1f, m_projectionDistanceFar * 2.0f);
    m_rawHMDProjectionRight = FromHMDMatrix44(vrHMDProjectionRight);

    vr::HmdMatrix34_t vrHMDViewRight = vrSystem->GetEyeToHeadTransform(vr::Hmd_Eye::Eye_Right);
    m_rawHMDViewRight = FromHMDMatrix34(vrHMDViewRight).invert();

    Matrix4 LeftCameraPose, RightCameraPose;
    GetTrackedCameraEyePoses(LeftCameraPose, RightCameraPose);

    m_cameraLeftToHMDPose = LeftCameraPose;

    Matrix4 LeftCameraPoseInv = LeftCameraPose;
    LeftCameraPoseInv.invert();
    m_cameraLeftToRightPose = LeftCameraPoseInv * RightCameraPose;
}

bool CameraManager::GetCameraFrame(std::shared_ptr<CameraFrame>& frame)
{
    if (!m_bCameraInitialized) { return false; }

    std::unique_lock<std::mutex> lock(m_serveMutex, std::try_to_lock);
    if (lock.owns_lock() && m_servedFrame->bIsValid)
    {
        m_renderFrame->bIsValid = false;
        m_renderFrame.swap(m_servedFrame);

        frame = m_renderFrame;
        return true;
    }
    else if (m_renderFrame->bIsValid)
    {
        frame = m_renderFrame;
        return true;
    }

    return false;
}

void CameraManager::ServeFrames()
{
    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    if (!trackedCamera)
    {
        return;
    }

    bool bHasFrame = false;
    uint32_t lastFrameSequence = 0;

    while (m_bRunThread)
    {
        std::this_thread::sleep_for(POSTFRAME_SLEEP_INTERVAL);

        if (!m_bRunThread) { return; }

        while (true)
        {
            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamFrameBuffer(m_cameraHandle, m_frameType, nullptr, 0, &m_underConstructionFrame->header, sizeof(vr::CameraVideoStreamFrameHeader_t));

            if (error == vr::VRTrackedCameraError_None)
            {
                if (!bHasFrame)
                {
                    break;
                }
                else if (m_underConstructionFrame->header.nFrameSequence != lastFrameSequence)
                {
                    break;
                }
            }
            else if (error != vr::VRTrackedCameraError_NoFrameAvailable)
            {
                ErrorLog("GetVideoStreamFrameBuffer-header error %i\n", error);
            }

            if (!m_bRunThread) { return; }

            std::this_thread::sleep_for(FRAME_POLL_INTERVAL);

            if (!m_bRunThread) { return; }
        }

        if (!m_bRunThread) { return; }


        if (true)
        {
            std::shared_ptr<PassthroughRenderer> renderer = m_renderer.lock();

            if (!renderer->GetRenderDevice())
            {
                continue;
            }

            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamTextureD3D11(m_cameraHandle, m_frameType, renderer->GetRenderDevice(), (void**)&m_underConstructionFrame->frameTextureResource, nullptr, 0);
            if (error != vr::VRTrackedCameraError_None)
            {
                ErrorLog("GetVideoStreamTextureD3D11 error %i\n", error);
                continue;
            }
        }
        else
        {
            if (m_underConstructionFrame->frameBuffer.get() == nullptr)
            {
                m_underConstructionFrame->frameBuffer = std::make_shared<std::vector<uint8_t>>(m_cameraFrameBufferSize);
            }

            vr::EVRTrackedCameraError error = trackedCamera->GetVideoStreamFrameBuffer(m_cameraHandle, m_frameType, m_underConstructionFrame->frameBuffer->data(), (uint32_t)m_underConstructionFrame->frameBuffer->size(), nullptr, 0);
            if (error != vr::VRTrackedCameraError_None)
            {
                ErrorLog("GetVideoStreamFrameBuffer error %i\n", error);
                continue;
            }
        }

        bHasFrame = true;
        lastFrameSequence = m_underConstructionFrame->header.nFrameSequence;

        m_underConstructionFrame->bIsValid = true;
        m_underConstructionFrame->frameLayout = m_frameLayout;

        {
            std::lock_guard<std::mutex> lock(m_serveMutex);

            m_servedFrame.swap(m_underConstructionFrame);
        }
    }
}


// Constructs a matrix from the roomscale origin to the HMD eye space.
Matrix4 CameraManager::GetHMDViewToTrackingMatrix(const ERenderEye eye)
{
    vr::IVRSystem* vrSystem = m_openVRManager->GetVRSystem();

    std::vector<vr::TrackedDevicePose_t> poses;
    poses.resize(m_hmdDeviceId + 1);

    uint64_t currentFrame;
    float timeSinceVsync;
    vrSystem->GetTimeSinceLastVsync(&timeSinceVsync, &currentFrame);
    float displayFrequency = vrSystem->GetFloatTrackedDeviceProperty(m_hmdDeviceId, vr::Prop_DisplayFrequency_Float);
    float vsyncToPhotons = vrSystem->GetFloatTrackedDeviceProperty(m_hmdDeviceId, vr::Prop_SecondsFromVsyncToPhotons_Float);
    float frameDuration = 1.0f / displayFrequency;

    float displayTime = frameDuration * 2.0f - timeSinceVsync + vsyncToPhotons;

    vrSystem->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, displayTime, &poses[0], m_hmdDeviceId + 1);

    Matrix4 poseMatrix;

    if (poses[0].bPoseIsValid)
    {
        poseMatrix = FromHMDMatrix34(poses[0].mDeviceToAbsoluteTracking).invert();
    }
    else
    {
        poseMatrix.identity();
    }

    if (eye == LEFT_EYE)
    {
        return m_rawHMDViewLeft * poseMatrix;
    }
    else
    {
        return m_rawHMDViewRight * poseMatrix;
    }
}

void CameraManager::CalculateFrameProjection(std::shared_ptr<CameraFrame>& frame, RenderFrame& renderFrame)
{
    vr::IVRTrackedCamera* trackedCamera = m_openVRManager->GetVRTrackedCamera();

    if (m_configManager->GetConfig_Main().ProjectionDistanceFar != m_projectionDistanceFar || 
        m_configManager->GetConfig_Main().ProjectionDistanceNear != m_projectionDistanceNear)
    {
        m_projectionDistanceFar = m_configManager->GetConfig_Main().ProjectionDistanceFar;
        m_projectionDistanceNear = m_configManager->GetConfig_Main().ProjectionDistanceNear;

        vr::HmdMatrix44_t vrProjection;
        vr::EVRTrackedCameraError error = trackedCamera->GetCameraProjection(m_hmdDeviceId, 0, m_frameType, m_projectionDistanceFar * 0.5f, m_projectionDistanceFar, &vrProjection);

        if (error != vr::VRTrackedCameraError_None)
        {
            ErrorLog("CameraProjection error %i on device %i\n", error, m_hmdDeviceId);
            return;
        }

        m_cameraProjectionInvFarLeft = FromHMDMatrix44(vrProjection).invert();

        if (m_frameLayout != EStereoFrameLayout::Mono)
        {
            error = trackedCamera->GetCameraProjection(m_hmdDeviceId, 1, m_frameType, m_projectionDistanceFar * 0.5f, m_projectionDistanceFar, &vrProjection);

            if (error != vr::VRTrackedCameraError_None)
            {
                ErrorLog("CameraProjection error %i on device %i\n", error, m_hmdDeviceId);
                return;
            }

            m_cameraProjectionInvFarRight = FromHMDMatrix44(vrProjection).invert();
        }
    }
    
    CalculateFrameProjectionForEye(LEFT_EYE, frame, renderFrame);
    CalculateFrameProjectionForEye(RIGHT_EYE, frame, renderFrame);
}

void CameraManager::CalculateFrameProjectionForEye(const ERenderEye eye, std::shared_ptr<CameraFrame>& frame, RenderFrame& renderFrame)
{
    bool bIsStereo = m_frameLayout != EStereoFrameLayout::Mono;
    uint32_t CameraId = (eye == RIGHT_EYE && bIsStereo) ? 1 : 0;

    

    Matrix4 hmdModelViewMatrix = GetHMDViewToTrackingMatrix(eye);
    Matrix4 hmdMVPMatrix = ((eye == LEFT_EYE) ? m_rawHMDProjectionLeft : m_rawHMDProjectionRight) * hmdModelViewMatrix;
    Matrix4 leftCameraToTrackingPose = FromHMDMatrix34(frame->header.trackedDevicePose.mDeviceToAbsoluteTracking);

    Matrix4 transformToCamera;

    if (CameraId == 0)
    {
        transformToCamera = hmdMVPMatrix * leftCameraToTrackingPose * m_cameraProjectionInvFarLeft;
    }
    else
    {
        transformToCamera = hmdMVPMatrix * leftCameraToTrackingPose * m_cameraLeftToRightPose * m_cameraProjectionInvFarRight;
    }

    // Calculate matrix for transforming the clip space quad to the quad output by the camera transform
    // as per: https://mrl.cs.nyu.edu/~dzorin/ug-graphics/lectures/lecture7/

    Vector4 P1 = Vector4(-1, -1, 1, 1);
    Vector4 P2 = Vector4(1, -1, 1, 1);
    Vector4 P3 = Vector4(1, 1, 1, 1);
    Vector4 P4 = Vector4(-1, 1, 1, 1);

    Vector4 Q1 = transformToCamera * P1;
    Vector4 Q2 = transformToCamera * P2;
    Vector4 Q3 = transformToCamera * P3;
    Vector4 Q4 = transformToCamera * P4;

    Vector3 R1 = Vector3(Q1.x, Q1.y, Q1.w);
    Vector3 R2 = Vector3(Q2.x, Q2.y, Q2.w);
    Vector3 R3 = Vector3(Q3.x, Q3.y, Q3.w);
    Vector3 R4 = Vector3(Q4.x, Q4.y, Q4.w);


    Vector3 H1 = R2.cross(R1).cross(R3.cross(R4));
    Vector3 H2 = R1.cross(R4).cross(R2.cross(R3));
    Vector3 H3 = R1.cross(R3).cross(R2.cross(R4));

    Matrix4 T = Matrix4(
        H1.x, H2.x, H3.x, 0.0f,
        H1.y, H2.y, H3.y, 0.0f,
        H1.z, H2.z, H3.z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);

    T.invert();
    T.transpose();

    if (eye == LEFT_EYE)
    {
        frame->frameUVProjectionLeft = T;
        renderFrame.hmdTrackingToViewLeft = hmdModelViewMatrix;
    }
    else
    {
        frame->frameUVProjectionRight = T;
        renderFrame.hmdTrackingToViewRight = hmdModelViewMatrix;
    }
}
