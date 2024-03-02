//	VirtualDub - Video processing and capture application
//	A/V interface library
//	Copyright (C) 1998-2008 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef DISPLAYDX9_PS2_FXH
#define DISPLAYDX9_PS2_FXH

////////////////////////////////////////////////////////////////////////////////////////////////////
//
//	Pixel shader 2.0 paths
//
////////////////////////////////////////////////////////////////////////////////////////////////////

void VertexShaderPointBilinear_2_0(VertexInput IN, out float4 oPos : POSITION, out float2 oT0 : TEXCOORD0, out float2 oT1 : TEXCOORD1) {
	oPos = IN.pos;
	oT0 = IN.uv;
	oT1 = IN.uv2 * vd_vpsize.xy / 16.0f;
}

float4 PixelShaderPointBilinear_2_0(float2 t0 : TEXCOORD0, float2 ditherUV : TEXCOORD1) : COLOR0 {
	float3 c = tex2D(samp0, t0).rgb;
			 	
	return float4(c, 0) * (254.0f / 255.0f) + tex2D(samp1, ditherUV) / 256.0f;
};

technique point_2_0 {
	pass p0 <
		bool vd_clippos = true;
	> {
		VertexShader = compile vs_2_0 VertexShaderPointBilinear_2_0();
		PixelShader = compile ps_2_0 PixelShaderPointBilinear_2_0();

		MinFilter[0] = Point;
		MagFilter[0] = Point;
		MipFilter[0] = Point;
		AddressU[0] = Clamp;
		AddressV[0] = Clamp;
		Texture[0] = <vd_srctexture>;

		MinFilter[1] = Point;
		MagFilter[1] = Point;
		MipFilter[1] = Point;
		AddressU[1] = Wrap;
		AddressV[1] = Wrap;
		Texture[1] = <vd_dithertexture>;
	}
}

technique bilinear_2_0 {
	pass p0 <
		bool vd_clippos = true;
	> {
		VertexShader = compile vs_2_0 VertexShaderPointBilinear_2_0();
		PixelShader = compile ps_2_0 PixelShaderPointBilinear_2_0();

		MinFilter[0] = Linear;
		MagFilter[0] = Linear;
		MipFilter[0] = Linear;
		AddressU[0] = Clamp;
		AddressV[0] = Clamp;
		Texture[0] = <vd_srctexture>;

		MinFilter[1] = Point;
		MagFilter[1] = Point;
		MipFilter[1] = Point;
		AddressU[1] = Wrap;
		AddressV[1] = Wrap;
		Texture[1] = <vd_dithertexture>;
	}
}

///////////////////////////////////////////////////////////////////////////

void VertexShaderBoxlinear_2_0(VertexInput IN, out float4 oPos : POSITION, out float2 oT0 : TEXCOORD0) {
	oPos = IN.pos;
	oT0 = IN.uv2 * vd_srcsize.xy;
}

float4 PixelShaderBoxlinear_2_0(float2 t0 : TEXCOORD0) : COLOR0 {
	float2 f = floor(t0 + 0.5);
	float2 d = (f - t0);
	
	t0 = f - saturate(d * vd_pixelsharpness + 0.5) + 0.5;

	return tex2D(samp0, t0 * vd_texsize.wz);
};

technique boxlinear_2_0 {
	pass p0 <
		bool vd_clippos = true;
	> {
		VertexShader = compile vs_2_0 VertexShaderBoxlinear_2_0();
		PixelShader = compile ps_2_0 PixelShaderBoxlinear_2_0();

		MinFilter[0] = Linear;
		MagFilter[0] = Linear;
		MipFilter[0] = Linear;
		AddressU[0] = Clamp;
		AddressV[0] = Clamp;
		Texture[0] = <vd_srctexture>;
	}
}

///////////////////////////////////////////////////////////////////////////

struct VertexOutputBicubic_2_0 {
	float4	pos		: POSITION;
	float2	uvfilt	: TEXCOORD0;
	float2	uvsrc0	: TEXCOORD1;
	float2	uvsrc1	: TEXCOORD2;
	float2	uvsrc2	: TEXCOORD3;
};

VertexOutputBicubic_2_0 VertexShaderBicubic_2_0_A(VertexInput IN) {
	VertexOutputBicubic_2_0 OUT;
	
	OUT.pos = IN.pos;
	OUT.uvfilt.x = IN.uv2.x * vd_vpsize.x * vd_interphtexsize.w;
	OUT.uvfilt.y = 0;

	OUT.uvsrc0 = IN.uv + float2(-1.5f, vd_fieldinfo.y)*vd_texsize.wz;
	OUT.uvsrc1 = IN.uv + float2( 0.0f, vd_fieldinfo.y)*vd_texsize.wz;
	OUT.uvsrc2 = IN.uv + float2(+1.5f, vd_fieldinfo.y)*vd_texsize.wz;
	
	return OUT;
}

VertexOutputBicubic_2_0 VertexShaderBicubic_2_0_B(VertexInput IN, out float2 ditherUV : TEXCOORD4) {
	VertexOutputBicubic_2_0 OUT;
	
	OUT.pos = IN.pos;
	OUT.uvfilt.x = IN.uv2.y * vd_vpsize.y * vd_interpvtexsize.w;
	OUT.uvfilt.y = 0;
	
	float2 uv = IN.uv2 * float2(vd_vpsize.x, vd_srcsize.y) * vd_tempsize.wz;
	OUT.uvsrc0 = uv + float2(0, -1.5f)*vd_tempsize.wz;
	OUT.uvsrc1 = uv + float2(0,  0.0f)*vd_tempsize.wz;
	OUT.uvsrc2 = uv + float2(0, +1.5f)*vd_tempsize.wz;
	
	ditherUV = IN.uv2 * vd_vpsize.xy / 16.0f;
	
	return OUT;
}

float4 PixelShaderBicubic_2_0_Filter(VertexOutputBicubic_2_0 IN) {
	float4 weights = tex2D(samp0, IN.uvfilt);

	float4 p1 = tex2D(samp1, IN.uvsrc0);
	float4 p2 = tex2D(samp2, IN.uvsrc1);
	float4 p3 = tex2D(samp1, IN.uvsrc1);
	float4 p4 = tex2D(samp1, IN.uvsrc2);
	
	float4 c1 = lerp(p4, p1, weights.b);
	float4 c2 = lerp(p3, p2, weights.g);
	return (c2 - c1) * weights.r + c2;
}

float4 PixelShaderBicubic_2_0_A(VertexOutputBicubic_2_0 IN) : COLOR0 {		
	return float4(PixelShaderBicubic_2_0_Filter(IN).rgb, 0);
}

float4 PixelShaderBicubic_2_0_B(VertexOutputBicubic_2_0 IN, float2 ditherUV : TEXCOORD4) : COLOR0 {
	float3 c = PixelShaderBicubic_2_0_Filter(IN).rgb;
			 			 
	return float4(c, 0);
}

float4 PixelShaderBicubic_2_0_B_Dither(VertexOutputBicubic_2_0 IN, float2 ditherUV : TEXCOORD4) : COLOR0 {
	float3 c = PixelShaderBicubic_2_0_Filter(IN).rgb;
			 			 
	return float4(c * (254.0f / 255.0f), 0) + tex2D(samp3, ditherUV) / 256.0f;
}

technique bicubic_2_0 {
	pass horiz <
		string vd_target="temp";
		string vd_viewport="out, src";
	> {
		VertexShader = compile vs_2_0 VertexShaderBicubic_2_0_A();
		PixelShader = compile ps_2_0 PixelShaderBicubic_2_0_A();
		
		Texture[0] = <vd_interphtexture>;
		AddressU[0] = Wrap;
		AddressV[0] = Clamp;
		MipFilter[0] = None;
		MinFilter[0] = Point;
		MagFilter[0] = Point;

		Texture[1] = <vd_srctexture>;
		AddressU[1] = Clamp;
		AddressV[1] = Clamp;
		MipFilter[1] = None;
		MinFilter[1] = Point;
		MagFilter[1] = Point;
		
		Texture[2] = <vd_srctexture>;
		AddressU[2] = Clamp;
		AddressV[2] = Clamp;
		MipFilter[2] = None;
		MinFilter[2] = Linear;
		MagFilter[2] = Linear;
	}
	
	pass vert <
		string vd_target="";
		string vd_viewport="out,out";
	> {
		VertexShader = compile vs_2_0 VertexShaderBicubic_2_0_B();
		PixelShader = compile ps_2_0 PixelShaderBicubic_2_0_B();
		Texture[0] = <vd_interpvtexture>;
		Texture[1] = <vd_temptexture>;
		Texture[2] = <vd_temptexture>;
		
		Texture[3] = <vd_dithertexture>;
		AddressU[3] = Wrap;
		AddressV[3] = Wrap;
		MipFilter[3] = None;
		MinFilter[3] = Point;
		MagFilter[3] = Point;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//
//	YCbCr to RGB -- pixel shader 2.0
//
////////////////////////////////////////////////////////////////////////////////////////////////////

void VS_YCbCr_to_RGB_2_0(
	float4 pos : POSITION,
	float2 uv : TEXCOORD0,
	float2 uv2 : TEXCOORD1,
	out float4 oPos : POSITION,
	out float2 oT0 : TEXCOORD0,
	out float2 oT1 : TEXCOORD1)
{
	oPos = pos;
	oT0 = uv;
	oT1 = (uv2 * vd_chromauvscale * vd_srcsize.xy + vd_chromauvoffset) * vd_tex2size.wz;
}

float4 PS_YCbCr_to_RGB_2_0(float2 uvY : TEXCOORD0, float2 uvC : TEXCOORD1, uniform int colorSpace) : COLOR0 {
	float y = tex2D(samp0, uvY).b;
	float cb = tex2D(samp1, uvC).b;
	float cr = tex2D(samp2, uvC).b;
		
	return ConvertYCbCrToRGB(y, cb, cr, colorSpace);
}

float4 PS_YCbCr_to_RGB_2_0_Rec601(float2 uvY : TEXCOORD0, float2 uvC : TEXCOORD1) : COLOR0 {
	return PS_YCbCr_to_RGB_2_0(uvY, uvC, COLOR_SPACE_REC601);
}

float4 PS_YCbCr_to_RGB_2_0_Rec709(float2 uvY : TEXCOORD0, float2 uvC : TEXCOORD1) : COLOR0 {
	return PS_YCbCr_to_RGB_2_0(uvY, uvC, COLOR_SPACE_REC709);
}

float4 PS_YCbCr_to_RGB_2_0_Rec601_FR(float2 uvY : TEXCOORD0, float2 uvC : TEXCOORD1) : COLOR0 {
	return PS_YCbCr_to_RGB_2_0(uvY, uvC, COLOR_SPACE_REC601_FR);
}

float4 PS_YCbCr_to_RGB_2_0_Rec709_FR(float2 uvY : TEXCOORD0, float2 uvC : TEXCOORD1) : COLOR0 {
	return PS_YCbCr_to_RGB_2_0(uvY, uvC, COLOR_SPACE_REC709_FR);
}

technique ycbcr_601_to_rgb_2_0 {
	pass < string vd_viewport = "unclipped,unclipped"; > {
		VertexShader = compile vs_2_0 VS_YCbCr_to_RGB_2_0();
		PixelShader = compile ps_2_0 PS_YCbCr_to_RGB_2_0_Rec601();
		
		Sampler[0] = <vd_srcsampler_clamp_point>;
		Sampler[1] = <vd_src2asampler_clamp_linear>;
		Sampler[2] = <vd_src2bsampler_clamp_linear>;
	}
}

technique ycbcr_709_to_rgb_2_0 {
	pass < string vd_viewport = "unclipped,unclipped"; > {
		VertexShader = compile vs_2_0 VS_YCbCr_to_RGB_2_0();
		PixelShader = compile ps_2_0 PS_YCbCr_to_RGB_2_0_Rec709();
		
		Sampler[0] = <vd_srcsampler_clamp_point>;
		Sampler[1] = <vd_src2asampler_clamp_linear>;
		Sampler[2] = <vd_src2bsampler_clamp_linear>;
	}
}

technique ycbcr_601fr_to_rgb_2_0 {
	pass < string vd_viewport = "unclipped,unclipped"; > {
		VertexShader = compile vs_2_0 VS_YCbCr_to_RGB_2_0();
		PixelShader = compile ps_2_0 PS_YCbCr_to_RGB_2_0_Rec601_FR();
		
		Sampler[0] = <vd_srcsampler_clamp_point>;
		Sampler[1] = <vd_src2asampler_clamp_linear>;
		Sampler[2] = <vd_src2bsampler_clamp_linear>;
	}
}

technique ycbcr_709fr_to_rgb_2_0 {
	pass < string vd_viewport = "unclipped,unclipped"; > {
		VertexShader = compile vs_2_0 VS_YCbCr_to_RGB_2_0();
		PixelShader = compile ps_2_0 PS_YCbCr_to_RGB_2_0_Rec709_FR();
		
		Sampler[0] = <vd_srcsampler_clamp_point>;
		Sampler[1] = <vd_src2asampler_clamp_linear>;
		Sampler[2] = <vd_src2bsampler_clamp_linear>;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//
//	Pal8 to RGB -- pixel shader 2.0
//
////////////////////////////////////////////////////////////////////////////////////////////////////

void VS_Pal8_to_RGB_2_0(
	float4 pos : POSITION,
	float2 uv : TEXCOORD0,
	out float4 oPos : POSITION,
	out float2 oT0 : TEXCOORD0)
{
	oPos = pos;
	oT0 = uv;
}

float4 PS_Pal8_to_RGB_2_0(float2 uv : TEXCOORD0) : COLOR0
{
	half index = tex2D(samp0, uv).r;

	return tex2D(samp1, half2(index * 255.0h/256.0h + 0.5h/256.0h, 0));
}

technique pal8_to_rgb_2_0 {
	pass < string vd_viewport = "unclipped,unclipped"; > {
		VertexShader = compile vs_2_0 VS_Pal8_to_RGB_2_0();
		PixelShader = compile ps_2_0 PS_Pal8_to_RGB_2_0();
		
		Texture[0] = <vd_srctexture>;
		AddressU[0] = Clamp;
		AddressV[0] = Clamp;
		MinFilter[0] = Point;
		MagFilter[0] = Point;
		
		Texture[1] = <vd_srcpaltexture>;
		AddressU[1] = Clamp;
		AddressV[1] = Clamp;
		MinFilter[1] = Point;
		MagFilter[1] = Point;
	}
}

#endif
