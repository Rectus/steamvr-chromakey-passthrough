

#include "pch.h"
#include "passthrough_renderer.h"
#include "logging.h"
#include <PathCch.h>

#include "lodepng.h"

#include "shaders\fullscreen_quad_vs.h"
#include "shaders\passthrough_vs.h"

#include "shaders\alpha_prepass_ps.h"
#include "shaders\alpha_prepass_masked_ps.h"
#include "shaders\passthrough_ps.h"
#include "shaders\passthrough_masked_ps.h"




struct VSConstantBuffer
{
	Matrix4 cameraUVProjectionFar;
	Matrix4 cameraUVProjectionNear;
};


struct PSPassConstantBuffer
{
	float opacity;
	float brightness;
	float contrast;
	float saturation;
	bool bDoColorAdjustment;
};

struct PSViewConstantBuffer
{
	Vector2 frameUVOffset;
	Vector2 prepassUVFactor;
	Vector2 prepassUVOffset;
	uint32_t rtArrayIndex;
};

struct PSMaskedConstantBuffer
{
	float maskedKey[3];
	float maskedFracChroma;
	float maskedFracLuma;
	float maskedSmooth;
	bool bMaskedUseCamera;
};



PassthroughRenderer::PassthroughRenderer(std::shared_ptr<ConfigManager> configManager, std::shared_ptr<OpenVRManager> openVRManager, int32_t adapterIndex)
	: m_configManager(configManager)
	, m_openVRManager(openVRManager)
	, m_adapterIndex(adapterIndex)
	, m_cameraTextureWidth(0)
	, m_cameraTextureHeight(0)
	, m_cameraFrameBufferSize(0)
	, m_mirrorSRVLeft(nullptr)
	, m_mirrorSRVRight(nullptr)
	, m_fenceValue(0)
{
}

PassthroughRenderer::~PassthroughRenderer()
{
	vr::IVRCompositor* vrCompositor = m_openVRManager->GetVRCompositor();

	if (vrCompositor)
	{
		if (m_mirrorSRVLeft)
		{
			vrCompositor->ReleaseMirrorTextureD3D11(m_mirrorSRVLeft);
		}
		if (m_mirrorSRVRight)
		{
			vrCompositor->ReleaseMirrorTextureD3D11(m_mirrorSRVRight);
		}
	}
}


bool PassthroughRenderer::InitRenderer()
{
	ComPtr<IDXGIFactory4> factory;
	if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
	{
		return false;
	}

	ComPtr<IDXGIAdapter1> adapter;
	if (FAILED(factory->EnumAdapters1(m_adapterIndex, &adapter)))
	{
		return false;
	}

	ComPtr<ID3D11Device> baseDevice;
	ComPtr<ID3D11DeviceContext> baseContext;

	if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &baseDevice, NULL, &baseContext)))
	{
		return false;
	}

	if (FAILED(baseDevice->QueryInterface(IID_PPV_ARGS(&m_d3dDevice))))
	{
		return false;
	}

	if (FAILED(baseContext->QueryInterface(IID_PPV_ARGS(&m_deviceContext))))
	{
		return false;
	}

	//m_d3dDevice->GetImmediateContext(&m_deviceContext);

	if (FAILED(m_d3dDevice->CreateFence(m_fenceValue, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
	{
		return false;
	}


	if (FAILED(m_d3dDevice->CreateVertexShader(g_FullscreenQuadShaderVS, sizeof(g_FullscreenQuadShaderVS), nullptr, &m_quadShader)))
	{
		return false;
	}

	if (FAILED(m_d3dDevice->CreateVertexShader(g_PassthroughShaderVS, sizeof(g_PassthroughShaderVS), nullptr, &m_vertexShader)))
	{
		return false;
	}

	if (FAILED(m_d3dDevice->CreatePixelShader(g_PassthroughShaderPS, sizeof(g_PassthroughShaderPS), nullptr, &m_pixelShader)))
	{
		return false;
	}

	if (FAILED(m_d3dDevice->CreatePixelShader(g_AlphaPrepassShaderPS, sizeof(g_AlphaPrepassShaderPS), nullptr, &m_prepassShader)))
	{
		return false;
	}

	if (FAILED(m_d3dDevice->CreatePixelShader(g_AlphaPrepassMaskedShaderPS, sizeof(g_AlphaPrepassMaskedShaderPS), nullptr, &m_maskedPrepassShader)))
	{
		return false;
	}

	if (FAILED(m_d3dDevice->CreatePixelShader(g_PassthroughMaskedShaderPS, sizeof(g_PassthroughMaskedShaderPS), nullptr, &m_maskedPixelShader)))
	{
		return false;
	}

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.ByteWidth = sizeof(Matrix4) * 2;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	for (int i = 0; i < NUM_SWAPCHAINS * 2; i++) 
	{
		if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr,  &m_vsConstantBuffer[i])))
		{
			return false;
		}
	}

	bufferDesc.ByteWidth = 32;
	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_psPassConstantBuffer)))
	{
		return false;
	}

	bufferDesc.ByteWidth = 32;
	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_psViewConstantBuffer)))
	{
		return false;
	}

	bufferDesc.ByteWidth = 32;
	if (FAILED(m_d3dDevice->CreateBuffer(&bufferDesc, nullptr, &m_psMaskedConstantBuffer)))
	{
		return false;
	}


	D3D11_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(m_d3dDevice->CreateSamplerState(&sampler, m_defaultSampler.GetAddressOf())))
	{
		return false;
	}

	D3D11_BLEND_DESC blendState = {};
	blendState.RenderTarget[0].BlendEnable = true;
	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	blendState.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
	blendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateBase.GetAddressOf())))
	{
		return false;
	}

	/*blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateAlphaPremultiplied.GetAddressOf())))
	{
		return false;
	}

	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStateSrcAlpha.GetAddressOf())))
	{
		return false;
	}

	blendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
	blendState.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	blendState.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStatePrepassUseAppAlpha.GetAddressOf())))
	{
		return false;
	}

	blendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;

	if (FAILED(m_d3dDevice->CreateBlendState(&blendState, m_blendStatePrepassIgnoreAppAlpha.GetAddressOf())))
	{
		return false;
	}*/


	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	rasterizerDesc.DepthClipEnable = false;
	rasterizerDesc.FrontCounterClockwise = true;
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.ScissorEnable = true;
	if (FAILED(m_d3dDevice->CreateRasterizerState(&rasterizerDesc, m_rasterizerState.GetAddressOf())))
	{
		return false;
	}

	SetupTestImage();
	SetupFrameResource();

	for (int i = 0; i < NUM_SWAPCHAINS * 2; i++)
	{
		InitRenderTarget(i);
	}

	return true;
}


void PassthroughRenderer::SetupTestImage()
{
	wchar_t path[MAX_PATH];
	
	if (FAILED(GetModuleFileNameW(NULL, path, MAX_PATH)))
	{
		ErrorLog("Error opening test pattern.\n");
	}

	std::filesystem::path imgPath(path);
	imgPath.remove_filename().append("testpattern.png");

	std::vector<unsigned char> image;
	unsigned width, height;

	unsigned error = lodepng::decode(image, width, height, imgPath.string().c_str());
	if (error)
	{
		ErrorLog("Error decoding test pattern.\n");
	}

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;

	m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &m_testPatternTexture);

	D3D11_TEXTURE2D_DESC uploadTextureDesc = textureDesc;
	uploadTextureDesc.BindFlags = 0;
	uploadTextureDesc.Usage = D3D11_USAGE_STAGING;
	uploadTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	ComPtr<ID3D11Texture2D> uploadTexture;
	m_d3dDevice->CreateTexture2D(&uploadTextureDesc, nullptr, &uploadTexture);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = textureDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;

	m_d3dDevice->CreateShaderResourceView(m_testPatternTexture.Get(), &srvDesc, &m_testPatternSRV);

	D3D11_MAPPED_SUBRESOURCE res = {};
	m_deviceContext->Map(uploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &res);
	memcpy(res.pData, image.data(), image.size());
	m_deviceContext->Unmap(uploadTexture.Get(), 0);

	m_deviceContext->CopyResource(m_testPatternTexture.Get(), uploadTexture.Get());
}


void PassthroughRenderer::SetupFrameResource()
{
	std::vector<uint8_t> image(m_cameraFrameBufferSize);

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	textureDesc.Width = m_cameraTextureWidth;
	textureDesc.Height = m_cameraTextureHeight;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;

	D3D11_TEXTURE2D_DESC uploadTextureDesc = textureDesc;
	uploadTextureDesc.BindFlags = 0;
	uploadTextureDesc.Usage = D3D11_USAGE_STAGING;
	uploadTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = textureDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;

	m_d3dDevice->CreateTexture2D(&uploadTextureDesc, nullptr, &m_cameraFrameUploadTexture);

	D3D11_MAPPED_SUBRESOURCE res = {};
	m_deviceContext->Map(m_cameraFrameUploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &res);
	memcpy(res.pData, image.data(), image.size());
	m_deviceContext->Unmap(m_cameraFrameUploadTexture.Get(), 0);

	for (int i = 0; i < NUM_SWAPCHAINS; i++)
	{
		m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &m_cameraFrameTexture[i]);
		m_d3dDevice->CreateShaderResourceView(m_cameraFrameTexture[i].Get(), &srvDesc, &m_cameraFrameSRV[i]);
		m_deviceContext->CopyResource(m_cameraFrameTexture[i].Get(), m_cameraFrameUploadTexture.Get());
	}
}


void PassthroughRenderer::InitRenderTarget(const uint32_t index)
{
	ID3D11Texture2D* texture;
	ID3D11RenderTargetView* rtv;

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	textureDesc.Width = m_cameraTextureWidth / 2; //TODO
	textureDesc.Height = m_cameraTextureHeight;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	m_d3dDevice->CreateTexture2D(&textureDesc, nullptr, &texture);
	m_renderTargets[index] = texture;

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	m_d3dDevice->CreateRenderTargetView(m_renderTargets[index].Get(), &rtvDesc, &rtv);

	m_renderTargetViews[index] = rtv;

	/*D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension =  D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	srvDesc.Texture2D.MipLevels = 1;

	m_d3dDevice->CreateShaderResourceView(m_renderTargets[index].Get(), &srvDesc, &m_renderTargetSRVs[index]);*/
}


void PassthroughRenderer::SetFrameSize(const uint32_t width, const uint32_t height, const uint32_t bufferSize)
{
	m_cameraTextureWidth = width;
	m_cameraTextureHeight = height;
	m_cameraFrameBufferSize = bufferSize;
}


void PassthroughRenderer::RenderPassthroughFrame(std::shared_ptr<CameraFrame> frame, RenderFrame& renderFrame)
{
	Config_Main& mainConf = m_configManager->GetConfig_Main();

	renderFrame.textureLeft = m_renderTargets[m_frameIndex];
	renderFrame.textureRight = m_renderTargets[m_frameIndex + NUM_SWAPCHAINS];

	/*if(SUCCEEDED(m_d3dDevice->CreateDeferredContext(0, &m_renderContext)))
	{
		m_bUsingDeferredContext = true;
		m_renderContext->ClearState();
	}
	else*/
	{
		m_bUsingDeferredContext = false;
		m_renderContext = m_deviceContext;
	}

	if (mainConf.ShowTestImage)
	{
		m_renderContext->PSSetShaderResources(0, 1, m_testPatternSRV.GetAddressOf());
	}
	else if (frame->frameTextureResource != nullptr)
	{
		// Use shared texture
		m_renderContext->PSSetShaderResources(0, 1, (ID3D11ShaderResourceView* const*)&frame->frameTextureResource);
	}
	else if(frame->frameBuffer != nullptr)
	{
		// Upload camera frame from CPU
		D3D11_MAPPED_SUBRESOURCE res = {};
		m_deviceContext->Map(m_cameraFrameUploadTexture.Get(), 0, D3D11_MAP_WRITE, 0, &res);
		memcpy(res.pData, frame->frameBuffer->data(), frame->frameBuffer->size());
		m_deviceContext->Unmap(m_cameraFrameUploadTexture.Get(), 0);

		m_deviceContext->CopyResource(m_cameraFrameTexture[m_frameIndex].Get(), m_cameraFrameUploadTexture.Get());

		m_renderContext->PSSetShaderResources(0, 1, m_cameraFrameSRV[m_frameIndex].GetAddressOf());
	}

	vr::IVRCompositor* vrCompositor = m_openVRManager->GetVRCompositor();

	if (m_mirrorSRVLeft)
	{
		vrCompositor->ReleaseMirrorTextureD3D11(m_mirrorSRVLeft);
		m_mirrorSRVLeft = nullptr;
	}
	if (m_mirrorSRVRight)
	{
		vrCompositor->ReleaseMirrorTextureD3D11(m_mirrorSRVRight);
		m_mirrorSRVRight = nullptr;
	}

	if (!mainConf.MaskedUseCameraImage)
	{
		
		vrCompositor->GetMirrorTextureD3D11(vr::Eye_Left, m_d3dDevice.Get(), (void**)&m_mirrorSRVLeft);
		vrCompositor->GetMirrorTextureD3D11(vr::Eye_Right, m_d3dDevice.Get(), (void**)&m_mirrorSRVRight);
	}

	m_renderContext->IASetInputLayout(nullptr);
	m_renderContext->IASetVertexBuffers(0, 0, nullptr, 0, 0);
	m_renderContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	m_renderContext->RSSetState(m_rasterizerState.Get());

	m_renderContext->PSSetSamplers(0, 1, m_defaultSampler.GetAddressOf());

	PSPassConstantBuffer buffer = {};
	buffer.opacity = mainConf.PassthroughOpacity;
	buffer.brightness = mainConf.Brightness;
	buffer.contrast = mainConf.Contrast;
	buffer.saturation = mainConf.Saturation;
	buffer.bDoColorAdjustment = fabsf(mainConf.Brightness) > 0.01f || fabsf(mainConf.Contrast - 1.0f) > 0.01f || fabsf(mainConf.Saturation - 1.0f) > 0.01f;

	m_renderContext->UpdateSubresource(m_psPassConstantBuffer.Get(), 0, nullptr, &buffer, 0, 0);

	if (mainConf.PassthroughMode == Masked)
	{
		PSMaskedConstantBuffer maskedBuffer = {};
		maskedBuffer.maskedKey[0] = powf(mainConf.MaskedKeyColor[0], 2.2f);
		maskedBuffer.maskedKey[1] = powf(mainConf.MaskedKeyColor[1], 2.2f);
		maskedBuffer.maskedKey[2] = powf(mainConf.MaskedKeyColor[2], 2.2f);
		maskedBuffer.maskedFracChroma = mainConf.MaskedFractionChroma * 100.0f;
		maskedBuffer.maskedFracLuma = mainConf.MaskedFractionLuma * 100.0f;
		maskedBuffer.maskedSmooth = mainConf.MaskedSmoothing * 100.0f;
		maskedBuffer.bMaskedUseCamera = mainConf.MaskedUseCameraImage;

		m_renderContext->UpdateSubresource(m_psMaskedConstantBuffer.Get(), 0, nullptr, &maskedBuffer, 0, 0);

		RenderPassthroughViewMasked(LEFT_EYE, frame);
		RenderPassthroughViewMasked(RIGHT_EYE, frame);
	}
	else
	{
		RenderPassthroughView(LEFT_EYE, frame, mainConf.PassthroughMode);
		RenderPassthroughView(RIGHT_EYE, frame, mainConf.PassthroughMode);
	}
	
	RenderFrameFinish();
}


void PassthroughRenderer::RenderPassthroughView(const ERenderEye eye, std::shared_ptr<CameraFrame> frame, EPassthroughBlendMode blendMode)
{
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + m_frameIndex;

	float clearColor[4] = { 0 };
	m_renderContext->ClearRenderTargetView(m_renderTargetViews[bufferIndex].Get(), clearColor);

	m_renderContext->OMSetRenderTargets(1, m_renderTargetViews[bufferIndex].GetAddressOf(), nullptr);

	D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)m_cameraTextureWidth / 2, (float)m_cameraTextureHeight, 0.0f, 1.0f };
	D3D11_RECT scissor = { 0, 0, m_cameraTextureWidth / 2, m_cameraTextureHeight };

	/*XrRect2Di rect = layer->views[viewIndex].subImage.imageRect;

	D3D11_VIEWPORT viewport = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0.0f, 1.0f };
	D3D11_RECT scissor = { rect.offset.x, rect.offset.y, rect.offset.x + rect.extent.width, rect.offset.y + rect.extent.height };*/

	m_renderContext->RSSetViewports(1, &viewport);
	m_renderContext->RSSetScissorRects(1, &scissor);

	VSConstantBuffer buffer = {};
	buffer.cameraUVProjectionFar = (eye == LEFT_EYE) ? frame->frameUVProjectionLeft : frame->frameUVProjectionRight;
	
	m_renderContext->UpdateSubresource(m_vsConstantBuffer[bufferIndex].Get(), 0, nullptr, &buffer, 0, 0);

	m_renderContext->VSSetConstantBuffers(0, 1, m_vsConstantBuffer[bufferIndex].GetAddressOf());
	m_renderContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
	
	PSViewConstantBuffer viewBuffer = {};
	viewBuffer.frameUVOffset = GetFrameUVOffset(eye, frame->frameLayout);

	m_renderContext->UpdateSubresource(m_psViewConstantBuffer.Get(), 0, nullptr, &viewBuffer, 0, 0);

	ID3D11Buffer* psBuffers[2] = { m_psPassConstantBuffer.Get(), m_psViewConstantBuffer.Get() };
	m_renderContext->PSSetConstantBuffers(0, 2, psBuffers);

	/*if (blendMode == Additive)
	{
		m_renderContext->OMSetBlendState(m_blendStateAlphaPremultiplied.Get(), nullptr, UINT_MAX);
	}
	else*/
	{
		m_renderContext->OMSetBlendState(m_blendStateBase.Get(), nullptr, UINT_MAX);
	}

	m_renderContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
	
	m_renderContext->Draw(3, 0);
}


void PassthroughRenderer::RenderPassthroughViewMasked(const ERenderEye eye, std::shared_ptr<CameraFrame> frame)
{
	int viewIndex = (eye == LEFT_EYE) ? 0 : 1;
	int bufferIndex = viewIndex * NUM_SWAPCHAINS + m_frameIndex;

	float clearColor[4] = { 0 };
	m_renderContext->ClearRenderTargetView(m_renderTargetViews[bufferIndex].Get(), clearColor);


	D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)m_cameraTextureWidth / 2, (float)m_cameraTextureHeight, 0.0f, 1.0f };
	D3D11_RECT scissor = { 0, 0, m_cameraTextureWidth  / 2, m_cameraTextureHeight };

	m_renderContext->RSSetViewports(1, &viewport);
	m_renderContext->RSSetScissorRects(1, &scissor);

	VSConstantBuffer buffer = {};
	buffer.cameraUVProjectionFar = (eye == LEFT_EYE) ? frame->frameUVProjectionLeft : frame->frameUVProjectionRight;

	m_renderContext->UpdateSubresource(m_vsConstantBuffer[bufferIndex].Get(), 0, nullptr, &buffer, 0, 0);

	m_renderContext->VSSetConstantBuffers(0, 1, m_vsConstantBuffer[bufferIndex].GetAddressOf());

	m_renderContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);


	ID3D11ShaderResourceView* cameraFrameSRV = nullptr;

	if (m_configManager->GetConfig_Main().ShowTestImage)
	{
		cameraFrameSRV = m_testPatternSRV.Get();
	}
	else if (frame->frameTextureResource != nullptr)
	{
		cameraFrameSRV = (ID3D11ShaderResourceView*)frame->frameTextureResource;
	}
	else
	{
		cameraFrameSRV = m_cameraFrameSRV[m_frameIndex].Get();
	}


	if (m_configManager->GetConfig_Main().MaskedUseCameraImage)
	{
		m_renderContext->PSSetShaderResources(0, 1, &cameraFrameSRV);
	}
	else
	{
		ID3D11ShaderResourceView* views[2] = { cameraFrameSRV, ((eye == LEFT_EYE) ?  m_mirrorSRVLeft : m_mirrorSRVRight) };
		m_renderContext->PSSetShaderResources(0, 2, views);
	}

	PSViewConstantBuffer viewBuffer = {};
	viewBuffer.frameUVOffset = GetFrameUVOffset(eye, frame->frameLayout);

	m_renderContext->UpdateSubresource(m_psViewConstantBuffer.Get(), 0, nullptr, &viewBuffer, 0, 0);

	ID3D11Buffer* psBuffers[3] = { m_psPassConstantBuffer.Get(), m_psViewConstantBuffer.Get(), m_psMaskedConstantBuffer.Get() };
	m_renderContext->PSSetConstantBuffers(0, 3, psBuffers);

	m_renderContext->PSSetShader(m_maskedPixelShader.Get(), nullptr, 0);
	
	m_renderContext->OMSetRenderTargets(1, m_renderTargetViews[bufferIndex].GetAddressOf(), nullptr);
	m_renderContext->OMSetBlendState(m_blendStateBase.Get(), nullptr, UINT_MAX);

	m_renderContext->Draw(3, 0);
}


void PassthroughRenderer::RenderFrameFinish()
{
	HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	m_renderContext->Signal(m_fence.Get(), ++m_fenceValue);
	m_fence->SetEventOnCompletion(m_fenceValue, &fenceEvent);

	m_renderContext->Flush();

	if (m_bUsingDeferredContext)
	{
		ComPtr<ID3D11CommandList> commandList;
		m_renderContext->FinishCommandList(false, commandList.GetAddressOf());
		m_deviceContext->ExecuteCommandList(commandList.Get(), true);
		m_renderContext.Reset();
	}

	WaitForSingleObject(fenceEvent, 11);

	m_frameIndex = (m_frameIndex + 1) % NUM_SWAPCHAINS;
}


void* PassthroughRenderer::GetRenderDevice()
{
	return m_d3dDevice.Get();
}