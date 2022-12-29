#pragma once


enum ERenderEye
{
	LEFT_EYE,
	RIGHT_EYE
};

enum EPassthroughBlendMode
{
	Masked = 0,
	Additive,
	Opaque
};

enum EStereoFrameLayout
{
	Mono = 0,
	StereoVerticalLayout = 1, // Stereo frames are Bottom/Top (for left/right respectively)
	StereoHorizontalLayout = 2 // Stereo frames are Left/Right
};


struct CameraFrame
{
	CameraFrame()
		: header()
		, frameTextureResource(nullptr)
		, frameUVProjectionLeft()
		, frameUVProjectionRight()
		, frameLayout(Mono)
		, bIsValid(false)
	{
	}

	vr::CameraVideoStreamFrameHeader_t header;
	ID3D11ShaderResourceView* frameTextureResource;
	std::shared_ptr<std::vector<uint8_t>> frameBuffer;
	Matrix4 frameUVProjectionLeft;
	Matrix4 frameUVProjectionRight;
	EStereoFrameLayout frameLayout;
	bool bIsValid;
};

struct RenderFrame
{
	RenderFrame()
		: textureLeft()
		, textureRight()
		, hmdTrackingToViewLeft()
		, hmdTrackingToViewRight()
	{
	}

	ComPtr<ID3D11Texture2D> textureLeft;
	ComPtr<ID3D11Texture2D> textureRight;
	Matrix4 hmdTrackingToViewLeft;
	Matrix4 hmdTrackingToViewRight;
};