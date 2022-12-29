#pragma once

#include <vector>
#include <iostream>
#include <d3dcompiler.h>
#include <wrl.h>
#include <winuser.h>
#include "config_manager.h"
#include "openvr_manager.h"
#include "shared_structs.h"


using Microsoft::WRL::ComPtr;

#define NUM_SWAPCHAINS 3






inline Vector2 GetFrameUVOffset(const ERenderEye eye, const EStereoFrameLayout layout)
{
	if (eye == LEFT_EYE)
	{
		switch (layout)
		{
		case StereoHorizontalLayout:
			return Vector2(0, 0);
			break;

			// The vertical layout has left camera below the right
		case StereoVerticalLayout:
			return Vector2(0, 0.5);
			break;

		case Mono:
			return Vector2(0, 0);
			break;
		}
	}
	else
	{
		switch (layout)
		{
		case StereoHorizontalLayout:
			return Vector2(0.5, 0);
			break;

		case StereoVerticalLayout:
			return Vector2(0, 0);
			break;

		case Mono:
			return Vector2(0, 0);
			break;
		}
	}

	return Vector2(0, 0);
}



class PassthroughRenderer
{
public:
	PassthroughRenderer(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, int32_t adapterIndex);

	~PassthroughRenderer();

	bool InitRenderer();
	
	void SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize);

	void RenderPassthroughFrame(std::shared_ptr<CameraFrame> frame, RenderFrame& renderFrame);
	void* GetRenderDevice();

private:

	void SetupTestImage();
	void SetupFrameResource();
	void InitRenderTarget(const uint32_t imageIndex);

	void RenderPassthroughView(const ERenderEye eye, std::shared_ptr<CameraFrame>  frame, EPassthroughBlendMode blendMode);
	void RenderPassthroughViewMasked(const ERenderEye eye, std::shared_ptr<CameraFrame> frame);
	void RenderFrameFinish();

	std::shared_ptr<ConfigManager> m_configManager;
	std::shared_ptr<OpenVRManager> m_openVRManager;
	int32_t m_adapterIndex;

	bool m_bUsingDeferredContext = false;
	int m_frameIndex = 0;

	ComPtr<ID3D11Device5> m_d3dDevice;
	ComPtr<ID3D11DeviceContext4> m_deviceContext;
	ComPtr<ID3D11DeviceContext4> m_renderContext;
	

	ComPtr<ID3D11Texture2D> m_renderTargets[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D11RenderTargetView> m_renderTargetViews[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D11ShaderResourceView> m_renderTargetSRVs[NUM_SWAPCHAINS * 2];

	ComPtr<ID3D11VertexShader> m_quadShader;
	ComPtr<ID3D11VertexShader> m_vertexShader;
	ComPtr<ID3D11PixelShader> m_pixelShader;
	ComPtr<ID3D11PixelShader> m_prepassShader;
	ComPtr<ID3D11PixelShader> m_maskedPrepassShader;
	ComPtr<ID3D11PixelShader> m_maskedPixelShader;

	ComPtr<ID3D11Buffer> m_vsConstantBuffer[NUM_SWAPCHAINS * 2];
	ComPtr<ID3D11Buffer> m_psPassConstantBuffer;
	ComPtr<ID3D11Buffer> m_psMaskedConstantBuffer;
	ComPtr<ID3D11Buffer> m_psViewConstantBuffer;
	ComPtr<ID3D11SamplerState> m_defaultSampler;
	ComPtr<ID3D11RasterizerState> m_rasterizerState;

	ComPtr<ID3D11BlendState> m_blendStateBase;
	ComPtr<ID3D11BlendState> m_blendStateAlphaPremultiplied;
	ComPtr<ID3D11BlendState> m_blendStateSrcAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassUseAppAlpha;
	ComPtr<ID3D11BlendState> m_blendStatePrepassIgnoreAppAlpha;

	ComPtr<ID3D11Texture2D> m_testPatternTexture;
	ComPtr<ID3D11ShaderResourceView> m_testPatternSRV;

	ComPtr<ID3D11Texture2D> m_cameraFrameTexture[NUM_SWAPCHAINS];
	ComPtr<ID3D11Texture2D> m_cameraFrameUploadTexture;
	ComPtr<ID3D11ShaderResourceView> m_cameraFrameSRV[NUM_SWAPCHAINS];

	ID3D11ShaderResourceView* m_mirrorSRVLeft;
	ID3D11ShaderResourceView* m_mirrorSRVRight;

	uint32_t m_cameraTextureWidth;
	uint32_t m_cameraTextureHeight;
	uint32_t m_cameraFrameBufferSize;

	ComPtr<ID3D11Fence> m_fence;
	int m_fenceValue;
};
