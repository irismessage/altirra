//
// BUG:
// Do not use half-precision interpolants. Passing a min16float4/half4 interpolant
// through to half4 output in the pixel shader crashes the Intel Iris Xe 30.0.101.1069
// driver.
//

cbuffer VSRendererConstants {
	float4 vsXform2d;
	float4 vsRdrHDRInfo;
};

cbuffer PSRendererConstants {
	float4 psRdrHDRInfo;
};

void VP_RenderBlitGamma(
	float2 pos : POSITION,
	float4 c : COLOR0,
	float2 uv : TEXCOORD0,
	out float4 oPos : SV_Position,
	out float4 oD0 : COLOR0,
	out float2 oT0 : TEXCOORD0
)
{
	oPos = float4(pos.xy * vsXform2d.xy + vsXform2d.zw, 0.5, 1);
	oD0 = c;
	oT0 = uv;
	
	VP_APPLY_VIEWPORT(oPos);
}

void VP_RenderBlitLinear(
	float2 pos : POSITION,
	float4 c : COLOR0,
	float2 uv : TEXCOORD0,
	out float4 oPos : SV_Position,
	out float4 oD0 : COLOR0,
	out float2 oT0 : TEXCOORD0
)
{
	oPos = float4(pos.xy * vsXform2d.xy + vsXform2d.zw, 0.5, 1);
	oD0.rgb = SrgbToLinear(c.rgb) * vsRdrHDRInfo.x;
	oD0.a = c.a;
	oT0 = uv;
	
	VP_APPLY_VIEWPORT(oPos);
}

void VP_RenderFillGamma(
	float2 pos : POSITION,
	float4 c : COLOR0,
	out float4 oPos : SV_Position,
	out float4 oD0 : COLOR0
)
{
	oPos = float4(pos.xy * vsXform2d.xy + vsXform2d.zw, 0.5, 1);

	oD0 = c;
	
	VP_APPLY_VIEWPORT(oPos);
}

void VP_RenderFillLinear(
	float2 pos : POSITION,
	float4 c : COLOR0,
	out float4 oPos : SV_Position,
	out float4 oD0 : COLOR0
)
{
	oPos = float4(pos.xy * vsXform2d.xy + vsXform2d.zw, 0.5, 1);
	oD0.rgb = SrgbToLinear(c.rgb) * vsRdrHDRInfo.x;
	oD0.a = c.a;
	
	VP_APPLY_VIEWPORT(oPos);
}

float4 FP_RenderFill(float4 pos : SV_Position,
		float4 c : COLOR0) : SV_Target
{	
	return c;
}

float4 FP_RenderBlit(float4 pos : SV_Position,
		float4 c : COLOR0,
		float2 uv : TEXCOORD0) : SV_Target
{	
	return (half4)SAMPLE2D(srctex, srcsamp, (half2)uv) * c;
}

half4 FP_RenderBlitLinear(float4 pos : SV_Position,
		float4 c : COLOR0,
		float2 uv : TEXCOORD0) : SV_Target
{	
	half4 tc = (half4)SAMPLE2D(srctex, srcsamp, (half2)uv);
	tc.rgb = SrgbToLinear(tc.rgb);
	return tc * c;
}

half4 FP_RenderBlitDirect(float4 pos : SV_Position,
		float4 c : COLOR0,
		float2 uv : TEXCOORD0) : SV_Target
{	
	return (half4)SAMPLE2D(srctex, srcsamp, (half2)uv);
}

half4 FP_RenderBlitStencil(float4 pos : SV_Position,
		float4 c : COLOR0,
		float2 uv : TEXCOORD0) : SV_Target
{	
	half4 px = (half4)SAMPLE2D(srctex, srcsamp, (half2)uv);
	
	px.a = px.b;
	px.rgb *= c.rgb;
	
	return px;
}

half4 FP_RenderBlitColor(float4 pos : SV_Position,
		float4 c : COLOR0,
		float2 uv : TEXCOORD0) : SV_Target
{	
	half4 px = (half4)SAMPLE2D(srctex, srcsamp, (half2)uv);
	
	return half4(px.rgb * c.rgb, c.a);
}

half4 FP_RenderBlitColor2(float4 pos : SV_Position,
		float4 c : COLOR0,
		float2 uv : TEXCOORD0) : SV_Target
{	
	half4 px1 = (half4)SAMPLE2D(srctex, srcsamp, (half2)uv);
	half4 px2 = (half4)SAMPLE2D(srctex, srcsamp, (half2)uv + half2(0.5, 0.0));
	half4 px = lerp(px1, px2, c.a);
	
	return half4(px.rgb * c.rgb, c.a);
}
