//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef f_VD2_VDDISPLAY_RENDERER3D_H
#define f_VD2_VDDISPLAY_RENDERER3D_H

#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <vd2/Tessa/Context.h>
#include <vd2/VDDisplay/renderer.h>
#include <vd2/VDDisplay/textrenderer.h>
#include "displaynode3d.h"

#ifdef _MSC_VER
#pragma once
#endif

class VDDisplayNodeContext3D;

class VDDisplayCachedImage3D : public vdrefcounted<IVDRefUnknown>, public vdlist_node {
	VDDisplayCachedImage3D(const VDDisplayCachedImage3D&) = delete;
	VDDisplayCachedImage3D& operator=(const VDDisplayCachedImage3D&) = delete;
public:
	enum { kTypeID = 'cim3' };

	VDDisplayCachedImage3D();
	~VDDisplayCachedImage3D();

	void *AsInterface(uint32 iid);

	bool Init(IVDTContext& ctx, void *owner, bool linear, const VDDisplayImageView& imageView);
	void Shutdown();

	void Update(const VDDisplayImageView& imageView);

public:
	void *mpOwner {};
	vdrefptr<IVDTTexture2D> mpTexture;
	sint32	mWidth {};
	sint32	mHeight {};
	sint32	mTexWidth {};
	sint32	mTexHeight {};
	uint32	mUniquenessCounter {};
	bool mbLinear {};

	vdrefptr<VDDisplayBlitNode3D> mpHiBltNode;
	float	mHiBltSharpnessX {};
	float	mHiBltSharpnessY {};
	vdrect32 mHiBltSrcRect {};
};

class VDDisplayRenderer3D final : public IVDDisplayRenderer {
public:
	VDDisplayRenderer3D();

	bool Init(IVDTContext& ctx);
	void Shutdown();

	void Begin(int w, int h, VDDisplayNodeContext3D& dctx, bool renderLinear);
	void End();

public:
	virtual const VDDisplayRendererCaps& GetCaps();
	VDDisplayTextRenderer *GetTextRenderer() { return &mTextRenderer; }

	virtual void SetColorRGB(uint32 color);
	virtual void FillRect(sint32 x, sint32 y, sint32 w, sint32 h);
	virtual void MultiFillRect(const vdrect32 *rects, uint32 n);

	virtual void AlphaFillRect(sint32 x, sint32 y, sint32 w, sint32 h, uint32 alphaColor);
	virtual void AlphaTriStrip(const vdfloat2 *pts, uint32 numPts, uint32 alphaColor);

	void FillTriStripHDR(const vdfloat2 *pts, const vdfloat4 *colors, uint32 numPts, bool alphaBlend) override;
	void FillTriStripHDR(const vdfloat2 *pts, const vdfloat4 *colors, const vdfloat2 *uv, uint32 numPts, bool alphaBlend, bool filter, VDDisplayImageView& brush) override;

	virtual void Blt(sint32 x, sint32 y, VDDisplayImageView& imageView);
	virtual void Blt(sint32 x, sint32 y, VDDisplayImageView& imageView, sint32 sx, sint32 sy, sint32 w, sint32 h);
	virtual void StretchBlt(sint32 dx, sint32 dy, sint32 dw, sint32 dh, VDDisplayImageView& imageView, sint32 sx, sint32 sy, sint32 sw, sint32 sh, const VDDisplayBltOptions& opts);

	virtual void MultiBlt(const VDDisplayBlt *blts, uint32 n, VDDisplayImageView& imageView, BltMode bltMode);

	void PolyLine(const vdpoint32 *points, uint32 numLines) override;
	void PolyLineF(const vdfloat2 *points, uint32 numLines, bool antialiased) override;

	virtual bool PushViewport(const vdrect32& r, sint32 x, sint32 y);
	virtual void PopViewport();

	virtual IVDDisplayRenderer *BeginSubRender(const vdrect32& r, VDDisplaySubRenderCache& cache);
	virtual void EndSubRender();

protected:
	struct FillVertex {
		float x;
		float y;
		uint32 c;
	};

	struct FillVertexF {
		float x;
		float y;
		float r;
		float g;
		float b;
		float a;
	};

	struct FillVertexFT {
		float x;
		float y;
		float r;
		float g;
		float b;
		float a;
		float u;
		float v;
	};

	struct BlitVertex {
		float x;
		float y;
		uint32 c;
		float u;
		float v;
	};

	static constexpr uint32 kVBSize = 65536;

	void AddLines(const FillVertex *p, uint32 n, bool alpha);
	void AddLineStrip(const FillVertex *p, uint32 n, bool alpha);
	void AddQuads(const FillVertex *p, uint32 n, bool alpha);

	void AddQuads(const BlitVertex *p, uint32 n, BltMode bltMode);
	VDDisplayCachedImage3D *GetCachedImage(VDDisplayImageView& imageView);
	void ApplyBaselineState();

	uint32 mColor {};
	uint32 mNativeColor {};
	uint32	mVBOffset {};
	sint32	mWidth {};
	sint32	mHeight {};
	vdrect32 mClipRect {};
	sint32	mOffsetX {};
	sint32	mOffsetY {};
	bool mbRenderLinear {};
	float mSDRIntensity {};

	IVDTContext *mpContext {};
	IVDTVertexProgram *mpVPFillLinear {};
	IVDTVertexProgram *mpVPFillGamma {};
	IVDTVertexProgram *mpVPBlitLinear {};
	IVDTVertexProgram *mpVPBlitLinearColor {};
	IVDTVertexProgram *mpVPBlitLinearColor2 {};
	IVDTVertexProgram *mpVPBlitGamma {};
	IVDTVertexFormat *mpVFFill {};
	IVDTVertexFormat *mpVFFillF {};
	IVDTVertexFormat *mpVFFillFT {};
	IVDTVertexFormat *mpVFBlit {};
	IVDTFragmentProgram *mpFPFill {};
	IVDTFragmentProgram *mpFPFillLinearToGamma {};
	IVDTFragmentProgram *mpFPBlit {};
	IVDTFragmentProgram *mpFPBlitLinearToGamma {};
	IVDTFragmentProgram *mpFPBlitLinear {};
	IVDTFragmentProgram *mpFPBlitDirect {};
	IVDTFragmentProgram *mpFPBlitStencil {};
	IVDTFragmentProgram *mpFPBlitColor {};
	IVDTFragmentProgram *mpFPBlitColor2 {};
	IVDTFragmentProgram *mpFPBlitColorLinear {};
	IVDTVertexBuffer *mpVB {};
	IVDTIndexBuffer *mpIB {};
	IVDTSamplerState *mpSS {};
	IVDTSamplerState *mpSSPoint {};
	IVDTSamplerState *mpSSWrap {};
	IVDTSamplerState *mpSSPointWrap {};
	IVDTBlendState *mpBS {};
	IVDTBlendState *mpBSStencil {};
	IVDTBlendState *mpBSColor {};
	IVDTRasterizerState *mpRS {};

	VDDisplayNodeContext3D *mpDCtx {};

	vdlist<VDDisplayCachedImage3D> mCachedImages;

	struct Context {
		uint32 mColor;
	};

	typedef vdfastvector<Context> ContextStack;
	ContextStack mContextStack;
	
	struct Viewport {
		vdrect32 mScissor;
		sint32 mOffsetX;
		sint32 mOffsetY;
	};

	vdfastvector<Viewport> mViewportStack;

	VDDisplayRendererCaps mCaps;

	VDDisplayTextRenderer mTextRenderer;
};

#endif
