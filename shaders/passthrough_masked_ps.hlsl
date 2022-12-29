
#include "util.hlsl"

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float3 uvCoords : TEXCOORD0;
	float2 originalUVCoords : TEXCOORD1;
};

cbuffer psPassConstantBuffer : register(b0)
{
	float g_opacity;
	float g_brightness;
	float g_contrast;
	float g_saturation;
	bool g_bDoColorAdjustment;
};

cbuffer psViewConstantBuffer : register(b1)
{
	float2 g_uvOffset;
	float2 g_uvPrepassFactor;
	float2 g_uvPrepassOffset;
	uint g_arrayIndex;
};

cbuffer psMaskedConstantBuffer : register(b2)
{
	float3 g_maskedKey;
	float g_maskedFracChroma;
	float g_maskedFracLuma;
	float g_maskedSmooth;
	bool g_bMaskedUseCamera;
};

SamplerState g_SamplerState : register(s0);
Texture2D g_CameraTexture : register(t0);
Texture2D g_BlendMask : register(t1);


float4 main(VS_OUTPUT input) : SV_TARGET
{
	// Divide to convert back from homogenous coordinates.
	float2 outUvs = input.uvCoords.xy / input.uvCoords.z;

	// Convert from clip space coordinates to 0-1.
	outUvs = outUvs * float2(-0.5, -0.5) + float2(0.5, 0.5);

	// Clamp to half of the frame texture and add the right eye offset.
	outUvs = clamp(outUvs, float2(0.0, 0.0), float2(0.5, 1.0)) + g_uvOffset;

	float4 cameraColor = g_CameraTexture.Sample(g_SamplerState, outUvs.xy);

	float alpha = g_BlendMask.Sample(g_SamplerState, input.originalUVCoords.xy).x;
	alpha = saturate((1.0 - alpha) * g_opacity);

	if (g_bDoColorAdjustment)
	{
		// Using CIELAB D65 to match the EXT_FB_passthrough adjustments.
		float3 labColor = LinearRGBtoLAB_D65(cameraColor.xyz);
		float LPrime = clamp((labColor.x - 50.0) * g_contrast + 50.0, 0.0, 100.0);
		float LBis = clamp(LPrime + g_brightness, 0.0, 100.0);
		float2 ab = labColor.yz * g_saturation;

		cameraColor = float4(LABtoLinearRGB_D65(float3(LBis, ab.xy)).xyz, cameraColor.a);
	}

	return float4(cameraColor.xyz, alpha);
}