#include "sysdefs.fxh"
#include "utils.fxh"

///////////////////////////////////////////////////////////////////////////

extern Texture2D srctex : register(t0);
extern SamplerState srcsamp : register(s0);

///////////////////////////////////////////////////////////////////////////

#include "ycbcr.fxh"
#include "ycbcr_interleaved.fxh"
#include "pal8.fxh"
#include "rgb.fxh"
#include "bicubic.fxh"
#include "render.fxh"
#include "screenfx.fxh"

///////////////////////////////////////////////////////////////////////////

void VP_Texture(
	float2 pos : POSITION,
	float2 uv : TEXCOORD0,
	out float4 oPos : SV_Position,
	out float2 oT0 : TEXCOORD0
)
{
	oPos = float4(pos.xy, 0.5, 1);
	oT0 = uv;
	
	VP_APPLY_VIEWPORT(oPos);
}

void VP_Texture2T(
	float2 pos : POSITION,
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	out float4 oPos : SV_Position,
	out float2 oT0 : TEXCOORD0,
	out float2 oT1 : TEXCOORD1)
{
	oPos = float4(pos.xy, 0.5, 1);
	oT0 = uv0;
	oT1 = uv1;
	
	VP_APPLY_VIEWPORT(oPos);
}

void VP_Texture3T(
	float2 pos : POSITION,
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	float2 uv2 : TEXCOORD2,
	out float4 oPos : SV_Position,
	out float2 oT0 : TEXCOORD0,
	out float2 oT1 : TEXCOORD1,
	out float2 oT2 : TEXCOORD2)
{
	oPos = float4(pos.xy, 0.5, 1);
	oT0 = uv0;
	oT1 = uv1;
	oT2 = uv2;
	
	VP_APPLY_VIEWPORT(oPos);
}

///////////////////////////////////////////////////////////////////////////

half4 FP_Blit(float4 pos : SV_Position, float2 uv0 : TEXCOORD0) : SV_Target0 {
	return half4(SAMPLE2D(srctex, srcsamp, uv0).rgb, 1);
}

///////////////////////////////////////////////////////////////////////////

half4 FP_BlitSharp(float4 pos : SV_Position, float2 uv0 : TEXCOORD0,
	uniform float4 params : register(c0)
) : SV_Target0 {
	
	float2 f = floor(uv0 + 0.5f);
	float2 d = (f - uv0);
	
	float2 uv = f - saturate(d * params.xy + 0.5) + 0.5;

	return half4(SAMPLE2D(srctex, srcsamp, uv * params.wz).rgb, 1);
}

///////////////////////////////////////////////////////////////////////////


void VP_Filter(
	float2 pos : POSITION,
	float2 uv : TEXCOORD0,
	out float4 oPos : SV_Position,
	out float2 oT0 : TEXCOORD0,
	out float2 oT1 : TEXCOORD1,
	out float2 oT2 : TEXCOORD2,
	out float2 oT3 : TEXCOORD3
)
{
	oPos = float4(pos.xy, 0.5, 1);
	oT0 = uv + float2(-0.5f/640.0f, -0.5f/480.0f);
	oT1 = uv + float2(-0.5f/640.0f, +0.5f/480.0f);
	oT2 = uv + float2(+0.5f/640.0f, -0.5f/480.0f);
	oT3 = uv + float2(+0.5f/640.0f, +0.5f/480.0f);
	
	VP_APPLY_VIEWPORT(oPos);
}

///////////////////////////////////////////////////////////////////////////

float4 FP_Filter(
	float4 pos : SV_Position,
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	float2 uv2 : TEXCOORD2,
	float2 uv3 : TEXCOORD3
) : SV_Target0 {
	float3 px0 = SAMPLE2D(srctex, srcsamp, uv0).rgb;
	float3 px1 = SAMPLE2D(srctex, srcsamp, uv1).rgb;
	float3 px2 = SAMPLE2D(srctex, srcsamp, uv2).rgb;
	float3 px3 = SAMPLE2D(srctex, srcsamp, uv3).rgb;
	
	float3 y = {0.30, 0.59, 0.11};
	float dx = dot((px2 + px3) - (px0 + px1), y) * 0.5 + 0.5;
	float dy = dot((px1 + px3) - (px0 + px2), y) * 0.5 + 0.5;
	
	return float4(dx, dy, 0, 1);
}

///////////////////////////////////////////////////////////////////////////

//$$export_shader [vs_4_0_level_9_1] VP_Texture g_VDDispVP_Texture
//$$export_shader [vs_4_0_level_9_1] VP_Texture2T g_VDDispVP_Texture2T
//$$export_shader [vs_4_0_level_9_1] VP_Texture3T g_VDDispVP_Texture3T
//$$export_shader [vs_4_0_level_9_1] VP_Filter g_VDDispVP_Filter

//$$export_shader [ps_4_0_level_9_1] FP_Filter g_VDDispFP_Filter
//$$export_shader [ps_4_0_level_9_1] FP_Blit g_VDDispFP_Blit
//$$export_shader [ps_4_0_level_9_1] FP_BlitSharp g_VDDispFP_BlitSharp
//$$export_shader [ps_4_0_level_9_1] FP_BlitPal8 g_VDDispFP_BlitPal8
//$$export_shader [ps_4_0_level_9_1] FP_BlitYCbCr_601_LR g_VDDispFP_BlitYCbCr_601_LR
//$$export_shader [ps_4_0_level_9_1] FP_BlitYCbCr_601_FR g_VDDispFP_BlitYCbCr_601_FR
//$$export_shader [ps_4_0_level_9_1] FP_BlitYCbCr_709_LR g_VDDispFP_BlitYCbCr_709_LR
//$$export_shader [ps_4_0_level_9_1] FP_BlitYCbCr_709_FR g_VDDispFP_BlitYCbCr_709_FR
//$$export_shader [ps_4_0_level_9_1] FP_BlitY_LR g_VDDispFP_BlitY_LR
//$$export_shader [ps_4_0_level_9_1] FP_BlitY_FR g_VDDispFP_BlitY_FR
//$$export_shader [ps_4_0_level_9_1] FP_BlitUYVY_601_LR g_VDDispFP_BlitUYVY_601_LR
//$$export_shader [ps_4_0_level_9_1] FP_BlitUYVY_601_FR g_VDDispFP_BlitUYVY_601_FR
//$$export_shader [ps_4_0_level_9_1] FP_BlitUYVY_709_LR g_VDDispFP_BlitUYVY_709_LR
//$$export_shader [ps_4_0_level_9_1] FP_BlitUYVY_709_FR g_VDDispFP_BlitUYVY_709_FR
//$$export_shader [ps_4_0_level_9_1] FP_BlitYUYV_601_LR g_VDDispFP_BlitYUYV_601_LR
//$$export_shader [ps_4_0_level_9_1] FP_BlitYUYV_601_FR g_VDDispFP_BlitYUYV_601_FR
//$$export_shader [ps_4_0_level_9_1] FP_BlitYUYV_709_LR g_VDDispFP_BlitYUYV_709_LR
//$$export_shader [ps_4_0_level_9_1] FP_BlitYUYV_709_FR g_VDDispFP_BlitYUYV_709_FR

//$$export_shader [ps_4_0_level_9_1] FP_BlitRGB16_L8A8 g_VDDispFP_BlitRGB16_L8A8
//$$export_shader [ps_4_0_level_9_1] FP_BlitRGB16_R8G8 g_VDDispFP_BlitRGB16_R8G8
//$$export_shader [ps_4_0_level_9_1] FP_BlitRGB24 g_VDDispFP_BlitRGB24

//$$export_shader [vs_4_0_level_9_1] VP_StretchBltCubic g_VDDispVP_StretchBltCubic
//$$export_shader [ps_4_0_level_9_1] FP_StretchBltCubic g_VDDispFP_StretchBltCubic

//$$export_shader [vs_4_0_level_9_1] VP_RenderFillLinear g_VDDispVP_RenderFillLinear
//$$export_shader [vs_4_0_level_9_1] VP_RenderFillGamma g_VDDispVP_RenderFillGamma
//$$export_shader [ps_4_0_level_9_1] FP_RenderFill g_VDDispFP_RenderFill
//$$export_shader [ps_4_0_level_9_1] FP_RenderFillLinearToGamma g_VDDispFP_RenderFillLinearToGamma
//$$export_shader [vs_4_0_level_9_1] VP_RenderBlitLinear g_VDDispVP_RenderBlitLinear
//$$export_shader [vs_4_0_level_9_1] VP_RenderBlitLinearColor g_VDDispVP_RenderBlitLinearColor
//$$export_shader [vs_4_0_level_9_1] VP_RenderBlitLinearColor2 g_VDDispVP_RenderBlitLinearColor2
//$$export_shader [vs_4_0_level_9_1] VP_RenderBlitGamma g_VDDispVP_RenderBlitGamma
//$$export_shader [ps_4_0_level_9_1] FP_RenderBlit g_VDDispFP_RenderBlit
//$$export_shader [ps_4_0_level_9_1] FP_RenderBlitLinear g_VDDispFP_RenderBlitLinear
//$$export_shader [ps_4_0_level_9_1] FP_RenderBlitLinearToGamma g_VDDispFP_RenderBlitLinearToGamma
//$$export_shader [ps_4_0_level_9_1] FP_RenderBlitDirect g_VDDispFP_RenderBlitDirect
//$$export_shader [ps_4_0_level_9_1] FP_RenderBlitStencil g_VDDispFP_RenderBlitStencil
//$$export_shader [ps_4_0_level_9_1] FP_RenderBlitColor g_VDDispFP_RenderBlitColor
//$$export_shader [ps_4_0_level_9_1] FP_RenderBlitColor2 g_VDDispFP_RenderBlitColor2
//$$export_shader [ps_4_0_level_9_1] FP_RenderBlitColorLinear g_VDDispFP_RenderBlitColorLinear

//$$export_shader [ps_4_0_level_9_1] FP_PALArtifacting_NormalOutput g_VDDispFP_PALArtifacting_NormalOutput
//$$export_shader [ps_4_0_level_9_1] FP_PALArtifacting_ExtendedOutput g_VDDispFP_PALArtifacting_ExtendedOutput

//$$export_shader [vs_4_0_level_9_1] VP_ScreenFX g_VDDispVP_ScreenFX
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_PtLinear_NoScanlines_Linear		g_VDDispFP_ScreenFX_PtLinear_NoScanlines_Linear
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_PtLinear_NoScanlines_Gamma			g_VDDispFP_ScreenFX_PtLinear_NoScanlines_Gamma
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_PtLinear_NoScanlines_CC_SRGB		g_VDDispFP_ScreenFX_PtLinear_NoScanlines_CC_SRGB
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_PtLinear_NoScanlines_CC_Gamma22	g_VDDispFP_ScreenFX_PtLinear_NoScanlines_CC_Gamma22
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_PtLinear_NoScanlines_GammaCC		g_VDDispFP_ScreenFX_PtLinear_NoScanlines_GammaCC
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_PtLinear_Scanlines_Linear			g_VDDispFP_ScreenFX_PtLinear_Scanlines_Linear
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_PtLinear_Scanlines_Gamma			g_VDDispFP_ScreenFX_PtLinear_Scanlines_Gamma
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_PtLinear_Scanlines_CC_SRGB			g_VDDispFP_ScreenFX_PtLinear_Scanlines_CC_SRGB
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_PtLinear_Scanlines_CC_Gamma22		g_VDDispFP_ScreenFX_PtLinear_Scanlines_CC_Gamma22
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_PtLinear_Scanlines_GammaCC			g_VDDispFP_ScreenFX_PtLinear_Scanlines_GammaCC
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_Sharp_NoScanlines_Linear			g_VDDispFP_ScreenFX_Sharp_NoScanlines_Linear
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_Sharp_NoScanlines_Gamma			g_VDDispFP_ScreenFX_Sharp_NoScanlines_Gamma
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_Sharp_NoScanlines_CC_SRGB			g_VDDispFP_ScreenFX_Sharp_NoScanlines_CC_SRGB
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_Sharp_NoScanlines_CC_Gamma22		g_VDDispFP_ScreenFX_Sharp_NoScanlines_CC_Gamma22
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_Sharp_NoScanlines_GammaCC			g_VDDispFP_ScreenFX_Sharp_NoScanlines_GammaCC
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_Sharp_Scanlines_Linear				g_VDDispFP_ScreenFX_Sharp_Scanlines_Linear
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_Sharp_Scanlines_Gamma				g_VDDispFP_ScreenFX_Sharp_Scanlines_Gamma
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_Sharp_Scanlines_CC_SRGB			g_VDDispFP_ScreenFX_Sharp_Scanlines_CC_SRGB
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_Sharp_Scanlines_CC_Gamma22			g_VDDispFP_ScreenFX_Sharp_Scanlines_CC_Gamma22
//$$export_shader [ps_4_0_level_9_1] FP_ScreenFX_Sharp_Scanlines_GammaCC			g_VDDispFP_ScreenFX_Sharp_Scanlines_GammaCC

//$$export_shader [vs_4_0_level_9_1] VP_Bloom1         	g_VDDispVP_Bloom1
//$$export_shader [ps_4_0_level_9_1] FP_Bloom1_Linear  	g_VDDispFP_Bloom1_Linear
//$$export_shader [ps_4_0_level_9_1] FP_Bloom1_Gamma  	g_VDDispFP_Bloom1_Gamma
//$$export_shader [ps_4_0_level_9_1] FP_Bloom1A         	g_VDDispFP_Bloom1A
//$$export_shader [vs_4_0_level_9_1] VP_Bloom2         	g_VDDispVP_Bloom2
//$$export_shader [ps_4_0_level_9_1] FP_Bloom2         	g_VDDispFP_Bloom2
//$$export_shader [vs_4_0_level_9_1] VP_Bloom3         	g_VDDispVP_Bloom3
//$$export_shader [ps_4_0_level_9_1] FP_Bloom3_Linear  	g_VDDispFP_Bloom3_Linear
//$$export_shader [ps_4_0_level_9_1] FP_Bloom3_Gamma  	g_VDDispFP_Bloom3_Gamma

//$$export_shader [vs_4_0_level_9_1] VP_BloomGamma		g_VDDispVP_BloomGamma
//$$export_shader [ps_4_0_level_9_1] FP_BloomGamma		g_VDDispFP_BloomGamma
//$$export_shader [vs_4_0_level_9_1] VP_BloomDown		g_VDDispVP_BloomDown
//$$export_shader [ps_4_0_level_9_1,ps_4_0] FP_BloomDown		g_VDDispFP_BloomDown
//$$export_shader [vs_4_0_level_9_1] VP_BloomUp			g_VDDispVP_BloomUp
//$$export_shader [ps_4_0_level_9_1,ps_4_0] FP_BloomUp			g_VDDispFP_BloomUp
//$$export_shader [vs_4_0_level_9_1] VP_BloomFinal		g_VDDispVP_BloomFinal
//$$export_shader [ps_4_0_level_9_1,ps_4_0] FP_BloomFinal		g_VDDispFP_BloomFinal
