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

#define ENGINE_D3D9 1

#include "displayd3d9_stddefs.fxh"
#include "utils.fxh"
#include "displayd3d9_ps1.fxh"
#include "displayd3d9_ps2.fxh"
#include "screenfx.fxh"

//	$$technique boxlinear_1_1
//		$$pass
//			$$bumpenv
//			$$clip_pos
//			$$vertex_shader_ext vs11_boxlinear.vsh
//			$$pixel_shader_ext ps11_boxlinear.psh
//			$$texture 0 vd_interptexture clamp clamp point
//			$$texture 1 vd_srctexture clamp clamp bilinear
//
//	$$technique pal8_to_rgb_1_1
//		$$pass
//			$$viewport unclipped unclipped
//			$$vertex_shader_ext vs11_pal8_to_rgb.vsh
//			$$pixel_shader_ext ps11_pal8_to_rgb.psh
//			$$texture 0 vd_srctexture clamp clamp point
//			$$texture 2 vd_srcpaltexture clamp clamp point
//
//	$$technique boxlinear_2_0
//		$$pass
//			$$clip_pos
//			$$vertex_shader vs_2_0 VertexShaderBoxlinear_2_0
//			$$pixel_shader ps_2_0 PixelShaderBoxlinear_2_0
//			$$texture 0 vd_srctexture clamp clamp bilinear
//
//	$$technique bicubic_2_0
//		$$pass
//			$$target temp
//			$$viewport unclipped src
//			$$vertex_shader vs_2_0 VertexShaderBicubic_2_0_A
//			$$pixel_shader ps_2_0 PixelShaderBicubic_2_0_A
//			$$texture 0 vd_interphtexture wrap clamp point		
//			$$texture 1 vd_srctexture clamp clamp point
//			$$texture 2 vd_srctexture clamp clamp bilinear
//		$$pass
//			$$target main
//			$$vertex_shader vs_2_0 VertexShaderBicubic_2_0_B
//			$$pixel_shader ps_2_0 PixelShaderBicubic_2_0_B
//			$$texture 0 vd_interpvtexture wrap clamp point		
//			$$texture 1 vd_temptexture clamp clamp point
//			$$texture 2 vd_temptexture clamp clamp bilinear
//

//	$$technique ycbcr_601_to_rgb_2_0
//		$$pass
//			$$viewport unclipped unclipped
//			$$vertex_shader vs_2_0 VS_YCbCr_to_RGB_2_0
//			$$pixel_shader ps_2_0 PS_YCbCr_to_RGB_2_0_Rec601
//			$$texture 0 vd_srctexture clamp clamp point
//			$$texture 1 vd_src2atexture clamp clamp bilinear
//			$$texture 2 vd_src2btexture clamp clamp bilinear

//	$$technique ycbcr_709_to_rgb_2_0
//		$$pass
//			$$viewport unclipped unclipped
//			$$vertex_shader vs_2_0 VS_YCbCr_to_RGB_2_0
//			$$pixel_shader ps_2_0 PS_YCbCr_to_RGB_2_0_Rec601
//			$$texture 0 vd_srctexture clamp clamp point
//			$$texture 1 vd_src2atexture clamp clamp bilinear
//			$$texture 2 vd_src2btexture clamp clamp bilinear

//	$$technique ycbcr_601fr_to_rgb_2_0
//		$$pass
//			$$viewport unclipped unclipped
//			$$vertex_shader vs_2_0 VS_YCbCr_to_RGB_2_0
//			$$pixel_shader ps_2_0 PS_YCbCr_to_RGB_2_0_Rec601_FR
//			$$texture 0 vd_srctexture clamp clamp point
//			$$texture 1 vd_src2atexture clamp clamp bilinear
//			$$texture 2 vd_src2btexture clamp clamp bilinear

//	$$technique ycbcr_709fr_to_rgb_2_0
//		$$pass
//			$$viewport unclipped unclipped
//			$$vertex_shader vs_2_0 VS_YCbCr_to_RGB_2_0
//			$$pixel_shader ps_2_0 PS_YCbCr_to_RGB_2_0_Rec709_FR
//			$$texture 0 vd_srctexture clamp clamp point
//			$$texture 1 vd_src2atexture clamp clamp bilinear
//			$$texture 2 vd_src2btexture clamp clamp bilinear

//	$$technique pal8_to_rgb_2_0
//		$$pass
//			$$viewport unclipped unclipped
//			$$vertex_shader vs_2_0 VS_Pal8_to_RGB_2_0
//			$$pixel_shader ps_2_0 PS_Pal8_to_RGB_2_0
//			$$texture 0 vd_srctexture clamp clamp point
//			$$texture 1 vd_srcpaltexture clamp clamp point
//

//	$$technique screenfx_ptlinear_noscanlines_linear
//		$$pass
//			$$vertex_shader vs_2_0 VP_ScreenFX
//			$$pixel_shader ps_2_0 FP_ScreenFX_PtLinear_NoScanlines_Linear
//			$$texture 0 vd_srctexture clamp clamp autobilinear
//
//	$$technique screenfx_ptlinear_noscanlines_gamma
//		$$pass
//			$$vertex_shader vs_2_0 VP_ScreenFX
//			$$pixel_shader ps_2_0 FP_ScreenFX_PtLinear_NoScanlines_Gamma
//			$$texture 0 vd_srctexture clamp clamp autobilinear
//			$$texture 1 vd_src2atexture clamp clamp bilinear
//
//	$$technique screenfx_ptlinear_noscanlines_cc
//		$$pass
//			$$vertex_shader vs_2_0 VP_ScreenFX
//			$$pixel_shader ps_2_0 FP_ScreenFX_PtLinear_NoScanlines_GammaCC
//			$$texture 0 vd_srctexture clamp clamp autobilinear
//			$$texture 1 vd_src2atexture clamp clamp bilinear
//
//	$$technique screenfx_sharp_noscanlines_linear
//		$$pass
//			$$vertex_shader vs_2_0 VP_ScreenFX
//			$$pixel_shader ps_2_0 FP_ScreenFX_Sharp_NoScanlines_Linear
//			$$texture 0 vd_srctexture clamp clamp bilinear
//
//	$$technique screenfx_sharp_noscanlines_gamma
//		$$pass
//			$$vertex_shader vs_2_0 VP_ScreenFX
//			$$pixel_shader ps_2_0 FP_ScreenFX_Sharp_NoScanlines_Gamma
//			$$texture 0 vd_srctexture clamp clamp bilinear
//			$$texture 1 vd_src2atexture clamp clamp bilinear
//
//	$$technique screenfx_sharp_noscanlines_cc
//		$$pass
//			$$vertex_shader vs_2_0 VP_ScreenFX
//			$$pixel_shader ps_2_0 FP_ScreenFX_Sharp_NoScanlines_GammaCC
//			$$texture 0 vd_srctexture clamp clamp bilinear
//			$$texture 1 vd_src2atexture clamp clamp bilinear
//
//	$$technique screenfx_ptlinear_scanlines_linear
//		$$pass
//			$$vertex_shader vs_2_0 VP_ScreenFX
//			$$pixel_shader ps_2_0 FP_ScreenFX_PtLinear_Scanlines_Linear
//			$$texture 0 vd_srctexture clamp clamp autobilinear
//			$$texture 2 vd_src2btexture clamp clamp point
//
//	$$technique screenfx_ptlinear_scanlines_gamma
//		$$pass
//			$$vertex_shader vs_2_0 VP_ScreenFX
//			$$pixel_shader ps_2_0 FP_ScreenFX_PtLinear_Scanlines_Gamma
//			$$texture 0 vd_srctexture clamp clamp autobilinear
//			$$texture 1 vd_src2atexture clamp clamp bilinear
//			$$texture 2 vd_src2btexture clamp clamp point
//
//	$$technique screenfx_ptlinear_scanlines_cc
//		$$pass
//			$$vertex_shader vs_2_0 VP_ScreenFX
//			$$pixel_shader ps_2_0 FP_ScreenFX_PtLinear_Scanlines_GammaCC
//			$$texture 0 vd_srctexture clamp clamp autobilinear
//			$$texture 1 vd_src2atexture clamp clamp bilinear
//			$$texture 2 vd_src2btexture clamp clamp point
//
//	$$technique screenfx_sharp_scanlines_linear
//		$$pass
//			$$vertex_shader vs_2_0 VP_ScreenFX
//			$$pixel_shader ps_2_0 FP_ScreenFX_Sharp_Scanlines_Linear
//			$$texture 0 vd_srctexture clamp clamp bilinear
//			$$texture 2 vd_src2btexture clamp clamp point
//
//	$$technique screenfx_sharp_scanlines_gamma
//		$$pass
//			$$vertex_shader vs_2_0 VP_ScreenFX
//			$$pixel_shader ps_2_0 FP_ScreenFX_Sharp_Scanlines_Gamma
//			$$texture 0 vd_srctexture clamp clamp bilinear
//			$$texture 1 vd_src2atexture clamp clamp bilinear
//			$$texture 2 vd_src2btexture clamp clamp point
//
//	$$technique screenfx_sharp_scanlines_cc
//		$$pass
//			$$vertex_shader vs_2_0 VP_ScreenFX
//			$$pixel_shader ps_2_0 FP_ScreenFX_Sharp_Scanlines_GammaCC
//			$$texture 0 vd_srctexture clamp clamp bilinear
//			$$texture 1 vd_src2atexture clamp clamp bilinear
//			$$texture 2 vd_src2btexture clamp clamp point

//	$$technique screenfx_bloomv2_conv
//		$$pass
//			$$vertex_shader vs_2_0 VP_BloomGamma
//			$$pixel_shader ps_2_0 FP_BloomGamma
//			$$texture 0 vd_srctexture clamp clamp point
//
//	$$technique screenfx_bloomv2_down
//		$$pass
//			$$vertex_shader vs_2_0 VP_BloomDown
//			$$pixel_shader ps_2_0 FP_BloomDown
//			$$texture 0 vd_srctexture clamp clamp bilinear
//
//	$$technique screenfx_bloomv2_up_noblend
//		$$pass
//			$$vertex_shader vs_2_0 VP_BloomUpNoBlend
//			$$pixel_shader ps_2_0 FP_BloomUpNoBlend
//			$$texture 0 vd_srctexture clamp clamp bilinear
//			$$texture 1 vd_src2atexture clamp clamp point
//
//	$$technique screenfx_bloomv2_final
//		$$pass
//			$$vertex_shader vs_2_0 VP_BloomFinal
//			$$pixel_shader ps_2_0 FP_BloomFinal
//			$$texture 0 vd_srctexture clamp clamp point
//			$$texture 1 vd_src2atexture clamp clamp bilinear

//	$$technique screenfx_palartifacting
//		$$pass
//			$$vertex_shader vs_2_0 VP_PALArtifacting
//			$$pixel_shader ps_2_0 FP_PALArtifacting_NormalOutput
//			$$texture 0 vd_srctexture clamp clamp bilinear

// $$emit_defs
