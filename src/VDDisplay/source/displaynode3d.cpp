#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/bitmath.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Tessa/Context.h>
#include "bicubic.h"
#include "displaynode3d.h"
#include "image_shader.inl"

extern const VDTDataView g_VDDispVPView_RenderFill(g_VDDispVP_RenderFill);
extern const VDTDataView g_VDDispVPView_RenderBlit(g_VDDispVP_RenderBlit);
extern const VDTDataView g_VDDispFPView_RenderFill(g_VDDispFP_RenderFill);
extern const VDTDataView g_VDDispFPView_RenderBlit(g_VDDispFP_RenderBlit);
extern const VDTDataView g_VDDispFPView_RenderBlitRBSwap(g_VDDispFP_RenderBlitRBSwap);
extern const VDTDataView g_VDDispFPView_RenderBlitDirect(g_VDDispFP_RenderBlitDirect);
extern const VDTDataView g_VDDispFPView_RenderBlitDirectRBSwap(g_VDDispFP_RenderBlitDirectRBSwap);
extern const VDTDataView g_VDDispFPView_RenderBlitStencil(g_VDDispFP_RenderBlitStencil);
extern const VDTDataView g_VDDispFPView_RenderBlitStencilRBSwap(g_VDDispFP_RenderBlitStencilRBSwap);
extern const VDTDataView g_VDDispFPView_RenderBlitColor(g_VDDispFP_RenderBlitColor);
extern const VDTDataView g_VDDispFPView_RenderBlitColorRBSwap(g_VDDispFP_RenderBlitColorRBSwap);
extern const VDTDataView g_VDDispFPView_RenderBlitColor2(g_VDDispFP_RenderBlitColor2);
extern const VDTDataView g_VDDispFPView_RenderBlitColor2RBSwap(g_VDDispFP_RenderBlitColor2RBSwap);

///////////////////////////////////////////////////////////////////////////

VDDisplayNodeContext3D::VDDisplayNodeContext3D()
	: mpVPTexture(NULL)
	, mpVPTexture2T(NULL)
	, mpVPTexture3T(NULL)
	, mpVFTexture(NULL)
	, mpVFTexture2T(NULL)
	, mpVFTexture3T(NULL)
	, mpFPBlit(NULL)
	, mpSSPoint(NULL)
	, mpSSBilinear(NULL)
	, mpSSBilinearRepeatMip(NULL)
{
}

VDDisplayNodeContext3D::~VDDisplayNodeContext3D() {
}

bool VDDisplayNodeContext3D::Init(IVDTContext& ctx) {
	if (ctx.IsFormatSupportedTexture2D(kVDTF_B8G8R8A8))
		mBGRAFormat = kVDTF_B8G8R8A8;
	else if (ctx.IsFormatSupportedTexture2D(kVDTF_R8G8B8A8))
		mBGRAFormat = kVDTF_R8G8B8A8;
	else
		return false;

	if (!ctx.CreateVertexProgram(kVDTPF_MultiTarget, VDTDataView(g_VDDispVP_Texture), &mpVPTexture)) {
		Shutdown();
		return false;
	}

	if (!ctx.CreateVertexProgram(kVDTPF_MultiTarget, VDTDataView(g_VDDispVP_Texture2T), &mpVPTexture2T)) {
		Shutdown();
		return false;
	}

	if (!ctx.CreateVertexProgram(kVDTPF_MultiTarget, VDTDataView(g_VDDispVP_Texture3T), &mpVPTexture3T)) {
		Shutdown();
		return false;
	}

	static const VDTVertexElement kVertexFormat[]={
		{ offsetof(VDDisplayVertex3D, x), kVDTET_Float3, kVDTEU_Position, 0 },
		{ offsetof(VDDisplayVertex3D, u), kVDTET_Float2, kVDTEU_TexCoord, 0 },
	};

	if (!ctx.CreateVertexFormat(kVertexFormat, 2, mpVPTexture, &mpVFTexture)) {
		Shutdown();
		return false;
	}

	static const VDTVertexElement kVertexFormat2T[]={
		{ offsetof(VDDisplayVertex2T3D, x), kVDTET_Float3, kVDTEU_Position, 0 },
		{ offsetof(VDDisplayVertex2T3D, u0), kVDTET_Float2, kVDTEU_TexCoord, 0 },
		{ offsetof(VDDisplayVertex2T3D, u1), kVDTET_Float2, kVDTEU_TexCoord, 1 },
	};

	if (!ctx.CreateVertexFormat(kVertexFormat2T, 3, mpVPTexture2T, &mpVFTexture2T)) {
		Shutdown();
		return false;
	}

	static const VDTVertexElement kVertexFormat3T[]={
		{ offsetof(VDDisplayVertex3T3D, x), kVDTET_Float3, kVDTEU_Position, 0 },
		{ offsetof(VDDisplayVertex3T3D, u0), kVDTET_Float2, kVDTEU_TexCoord, 0 },
		{ offsetof(VDDisplayVertex3T3D, u1), kVDTET_Float2, kVDTEU_TexCoord, 1 },
		{ offsetof(VDDisplayVertex3T3D, u2), kVDTET_Float2, kVDTEU_TexCoord, 2 },
	};

	if (!ctx.CreateVertexFormat(kVertexFormat3T, 4, mpVPTexture3T, &mpVFTexture3T)) {
		Shutdown();
		return false;
	}

	if (!ctx.CreateFragmentProgram(kVDTPF_MultiTarget, VDTDataView(g_VDDispFP_Blit), &mpFPBlit)) {
		Shutdown();
		return false;
	}

	VDTSamplerStateDesc ssdesc = {};
	ssdesc.mFilterMode = kVDTFilt_Point;
	ssdesc.mAddressU = kVDTAddr_Clamp;
	ssdesc.mAddressV = kVDTAddr_Clamp;
	ssdesc.mAddressW = kVDTAddr_Clamp;

	if (!ctx.CreateSamplerState(ssdesc, &mpSSPoint)) {
		Shutdown();
		return false;
	}

	ssdesc.mFilterMode = kVDTFilt_Bilinear;

	if (!ctx.CreateSamplerState(ssdesc, &mpSSBilinear)) {
		Shutdown();
		return false;
	}

	ssdesc.mFilterMode = kVDTFilt_BilinearMip;
	ssdesc.mAddressU = kVDTAddr_Wrap;
	ssdesc.mAddressV = kVDTAddr_Wrap;

	if (!ctx.CreateSamplerState(ssdesc, &mpSSBilinearRepeatMip)) {
		Shutdown();
		return false;
	}

	return true;
}

void VDDisplayNodeContext3D::Shutdown() {
	vdsaferelease <<=
		mpFPBlit,
		mpVFTexture,
		mpVFTexture2T,
		mpVFTexture3T,
		mpVPTexture,
		mpVPTexture2T,
		mpVPTexture3T,
		mpSSBilinearRepeatMip,
		mpSSBilinear,
		mpSSPoint;
}

///////////////////////////////////////////////////////////////////////////

VDDisplaySourceNode3D::~VDDisplaySourceNode3D() {
}

///////////////////////////////////////////////////////////////////////////

VDDisplayImageSourceNode3D::VDDisplayImageSourceNode3D()
	: mpImageTex(NULL)
{
}

VDDisplayImageSourceNode3D::~VDDisplayImageSourceNode3D() {
	Shutdown();
}

bool VDDisplayImageSourceNode3D::Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, uint32 w, uint32 h, uint32 format) {
	const VDTDeviceCaps& caps = ctx.GetDeviceCaps();

	uint32 texWidth = w;
	uint32 texHeight = h;

	if (!caps.mbNonPow2Conditional) {
		texWidth = VDCeilToPow2(w);
		texHeight = VDCeilToPow2(h);
	}

	if (texWidth > caps.mMaxTextureWidth || texHeight > caps.mMaxTextureHeight)
		return false;

	switch(format) {
		case nsVDPixmap::kPixFormat_RGB565:
		case nsVDPixmap::kPixFormat_XRGB1555:{
			VDTFormat rgb16Format = (format == nsVDPixmap::kPixFormat_RGB565) ? kVDTF_B5G6R5 : kVDTF_B5G5R5A1;

			if (ctx.IsFormatSupportedTexture2D(rgb16Format)) {
				if (ctx.CreateTexture2D(texWidth, texHeight, rgb16Format, 1, kVDTUsage_Default, NULL, &mpImageTex)) {
					mMapping.Init(w, h, texWidth, texHeight, false);
					return true;
				}
			}
			break;
		}

		case nsVDPixmap::kPixFormat_XRGB8888:
			if (ctx.CreateTexture2D(texWidth, texHeight, dctx.mBGRAFormat, 1, kVDTUsage_Default, NULL, &mpImageTex)) {
				mMapping.Init(w, h, texWidth, texHeight, dctx.mBGRAFormat == kVDTF_R8G8B8A8);
				return true;
			}
			break;

		case nsVDPixmap::kPixFormat_Y8_FR:
			if (ctx.IsFormatSupportedTexture2D(kVDTF_L8)) {
				if (ctx.CreateTexture2D(texWidth, texHeight, kVDTF_L8, 1, kVDTUsage_Default, NULL, &mpImageTex)) {
					mMapping.Init(w, h, texWidth, texHeight, false);
					return true;
				}
			}
			break;
		
		default:
			return false;
	}

	Shutdown();
	return false;
}

void VDDisplayImageSourceNode3D::Shutdown() {
	vdsaferelease <<= mpImageTex;
}

void VDDisplayImageSourceNode3D::Load(const VDPixmap& px) {
	VDTLockData2D lockData;

	if (!mpImageTex->Lock(0, NULL, lockData))
		return;

	VDPixmap dstpx = {};
	dstpx.data = lockData.mpData;
	dstpx.pitch = lockData.mPitch;
	dstpx.format = nsVDPixmap::kPixFormat_XRGB8888;
	dstpx.w = mMapping.mTexWidth;
	dstpx.h = mMapping.mTexHeight;

	VDPixmapBlt(dstpx, px);

	mpImageTex->Unlock(0);
}

VDDisplaySourceTexMapping VDDisplayImageSourceNode3D::GetTextureMapping() const {
	return mMapping;
}

IVDTTexture2D *VDDisplayImageSourceNode3D::Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx) {
	return mpImageTex;
}

///////////////////////////////////////////////////////////////////////////

VDDisplayBufferSourceNode3D::VDDisplayBufferSourceNode3D()
	: mpRTT(NULL)
	, mpChildNode(NULL)
{
}

VDDisplayBufferSourceNode3D::~VDDisplayBufferSourceNode3D() {
	Shutdown();
}

bool VDDisplayBufferSourceNode3D::Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, uint32 w, uint32 h, VDDisplayNode3D *child) {
	if (mpChildNode != child) {
		if (mpChildNode)
			mpChildNode->Release();

		mpChildNode = child;
		child->AddRef();
	}

	const VDTDeviceCaps& caps = ctx.GetDeviceCaps();

	uint32 texWidth = w;
	uint32 texHeight = h;

	if (!caps.mbNonPow2Conditional) {
		texWidth = VDCeilToPow2(texWidth);
		texHeight = VDCeilToPow2(texHeight);
	}

	if (texWidth > caps.mMaxTextureWidth || texHeight > caps.mMaxTextureHeight) {
		Shutdown();
		return false;
	}

	if (mpRTT) {
		VDTTextureDesc desc;

		mpRTT->GetDesc(desc);

		if (desc.mWidth != texWidth || desc.mHeight != texHeight) {
			mpRTT->Release();
			mpRTT = NULL;
		}
	}

	if (!mpRTT) {
		if (!ctx.CreateTexture2D(texWidth, texHeight, dctx.mBGRAFormat, 1, kVDTUsage_Render, NULL, &mpRTT)) {
			Shutdown();
			return false;
		}
	}

	mMapping.Init(w, h, texWidth, texHeight, false);
	return true;
}

void VDDisplayBufferSourceNode3D::Shutdown() {
	vdsaferelease <<= mpRTT, mpChildNode;
}

VDDisplaySourceTexMapping VDDisplayBufferSourceNode3D::GetTextureMapping() const {
	return mMapping;
}

IVDTTexture2D *VDDisplayBufferSourceNode3D::Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx) {
	if (!mpRTT)
		return NULL;

	IVDTSurface *pPrevTarget = ctx.GetRenderTarget(0);
	const VDTViewport& oldvp = ctx.GetViewport();

	IVDTSurface *rttsurf = mpRTT->GetLevelSurface(0);

	VDTSurfaceDesc rttsurfdesc;
	rttsurf->GetDesc(rttsurfdesc);

	VDTViewport vp;
	vp.mX = 0;
	vp.mY = 0;
	vp.mWidth = rttsurfdesc.mWidth;
	vp.mHeight = rttsurfdesc.mHeight;
	vp.mMinZ = 0.0f;
	vp.mMaxZ = 1.0f;
	ctx.SetRenderTarget(0, rttsurf);
	ctx.SetViewport(vp);

	mpChildNode->Draw(ctx, dctx);
	ctx.SetRenderTarget(0, pPrevTarget);
	ctx.SetViewport(oldvp);

	return mpRTT;
}

///////////////////////////////////////////////////////////////////////////

VDDisplayNode3D::~VDDisplayNode3D() {
}

///////////////////////////////////////////////////////////////////////////

VDDisplaySequenceNode3D::VDDisplaySequenceNode3D() {
}

VDDisplaySequenceNode3D::~VDDisplaySequenceNode3D() {
	Shutdown();
}

void VDDisplaySequenceNode3D::Shutdown() {
	while(!mNodes.empty()) {
		VDDisplayNode3D *node = mNodes.back();
		mNodes.pop_back();

		node->Release();
	}
}

void VDDisplaySequenceNode3D::AddNode(VDDisplayNode3D *node) {
	mNodes.push_back(node);
	node->AddRef();
}

void VDDisplaySequenceNode3D::Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx) {
	for(Nodes::const_iterator it(mNodes.begin()), itEnd(mNodes.end());
		it != itEnd;
		++it)
	{
		VDDisplayNode3D *node = *it;

		node->Draw(ctx, dctx);
	}
}

///////////////////////////////////////////////////////////////////////////

VDDisplayClearNode3D::VDDisplayClearNode3D()
	: mColor(0)
{
}

VDDisplayClearNode3D::~VDDisplayClearNode3D() {
}

void VDDisplayClearNode3D::SetClearColor(uint32 c) {
	mColor = c;
}

void VDDisplayClearNode3D::Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx) {
	ctx.Clear(kVDTClear_Color, mColor, 0, 0);
}

///////////////////////////////////////////////////////////////////////////

VDDisplayImageNode3D::VDDisplayImageNode3D()
	: mpPaletteTex(NULL)
	, mpVF(NULL)
	, mpVP(NULL)
	, mpFP(NULL)
	, mpVB(NULL)
	, mDstX(0)
	, mDstY(0)
	, mDstW(0)
	, mDstH(0)
	, mTexWidth(0)
	, mTexHeight(0)
	, mTex2Width(0)
	, mTex2Height(0)
	, mbBilinear(true)
{
	std::fill(mpImageTex, mpImageTex + 3, (IVDTTexture2D *)NULL);
}

VDDisplayImageNode3D::~VDDisplayImageNode3D() {
	Shutdown();
}

bool VDDisplayImageNode3D::CanStretch() const {
	switch(mRenderMode) {
		case kRenderMode_Blit:
		case kRenderMode_BlitY:
		case kRenderMode_BlitYCbCr:
		case kRenderMode_BlitRGB16Direct:
			return true;

		default:
			return false;
	}
}

bool VDDisplayImageNode3D::Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, uint32 w, uint32 h, uint32 format) {
	const VDTDeviceCaps& caps = ctx.GetDeviceCaps();

	if (w > caps.mMaxTextureWidth || h > caps.mMaxTextureHeight)
		return false;

	mTexWidth = w;
	mTexHeight = h;

	VDTFormat bgraFormat = kVDTF_B8G8R8A8;
	mbRenderSwapRB = false;

	if (!ctx.IsFormatSupportedTexture2D(kVDTF_B8G8R8A8)) {
		mbRenderSwapRB = true;
		bgraFormat = kVDTF_R8G8B8A8;

		if (!ctx.IsFormatSupportedTexture2D(kVDTF_R8G8B8A8))
			return false;
	}

	float chromaOffsetU = 0.0f;
	float chromaOffsetV = 0.0f;

	switch(format) {
		case nsVDPixmap::kPixFormat_RGB565:
		case nsVDPixmap::kPixFormat_XRGB1555:{
			VDTFormat rgb16Format = (format == nsVDPixmap::kPixFormat_RGB565) ? kVDTF_B5G6R5 : kVDTF_B5G5R5A1;

			if (ctx.IsFormatSupportedTexture2D(rgb16Format)) {
				if (!ctx.CreateTexture2D(w, h, rgb16Format, 1, kVDTUsage_Default, NULL, &mpImageTex[0])) {
					Shutdown();
					return false;
				}

				if (!ctx.CreateFragmentProgram(kVDTPF_MultiTarget, VDTDataView(g_VDDispFP_Blit), &mpFP)) {
					Shutdown();
					return false;
				}

				mRenderMode = kRenderMode_BlitRGB16Direct;
				mpVP = dctx.mpVPTexture;
				mpVP->AddRef();
				mpVF = dctx.mpVFTexture;
				mpVF->AddRef();
			} else {
				bool l8a8 = ctx.IsFormatSupportedTexture2D(kVDTF_L8A8);
				bool r8g8 = ctx.IsFormatSupportedTexture2D(kVDTF_R8G8);

				if (l8a8 || r8g8) {
					if (!ctx.CreateTexture2D(w, h, l8a8 ? kVDTF_L8A8 : kVDTF_R8G8, 1, kVDTUsage_Default, NULL, &mpImageTex[0])) {
						Shutdown();
						return false;
					}

					if (!ctx.CreateFragmentProgram(kVDTPF_MultiTarget, l8a8 ? VDTDataView(g_VDDispFP_BlitRGB16_L8A8) : VDTDataView(g_VDDispFP_BlitRGB16_R8G8), &mpFP)) {
						Shutdown();
						return false;
					}

					uint8 palette[2][256][4];

					for(uint32 i=0; i<256; ++i) {
						uint8 r0, g0, g1, b1;

						if (rgb16Format == kVDTF_B5G6R5) {
							r0 = (i >> 3) & 31;
							g0 = (i & 7);
							g1 = i >> 5;

							g0 = (g0 << 5) + (g0 >> 1);
							g1 = (g1 << 2);
						} else {
							r0 = (i >> 2) & 31;
							g0 = (i & 3);
							g1 = i >> 5;

							g0 = (g0 << 6) + (g0 << 1);
							g1 = (g1 << 3) + (g1 >> 2);
						}

						r0 = (r0 << 3) + (r0 >> 2);

						b1 = i & 31;
						b1 = (b1 << 3) + (b1 >> 2);

						if (mbRenderSwapRB) {
							palette[1][i][0] = r0;
							palette[1][i][1] = g0;
							palette[1][i][2] = 0;
							palette[1][i][3] = 255;
							palette[0][i][0] = 0;
							palette[0][i][1] = g1;
							palette[0][i][2] = b1;
							palette[0][i][3] = 0;
						} else {
							palette[1][i][0] = 0;
							palette[1][i][1] = g0;
							palette[1][i][2] = r0;
							palette[1][i][3] = 255;
							palette[0][i][0] = b1;
							palette[0][i][1] = g1;
							palette[0][i][2] = 0;
							palette[0][i][3] = 0;
						}
					}

					VDTInitData2D palInitData = { palette, sizeof palette[0] };

					if (!ctx.CreateTexture2D(256, 2, bgraFormat, 1, kVDTUsage_Default, &palInitData, &mpPaletteTex)) {
						Shutdown();
						return false;
					}

					mRenderMode = kRenderMode_BlitRGB16;
					mpVP = dctx.mpVPTexture;
					mpVP->AddRef();
					mpVF = dctx.mpVFTexture;
					mpVF->AddRef();
				}
			}
			break;
		}

		case nsVDPixmap::kPixFormat_RGB888:
			if (w * 3 <= caps.mMaxTextureWidth && ctx.IsFormatSupportedTexture2D(kVDTF_R8)) {
				mTexWidth *= 3;

				if (!ctx.CreateTexture2D(3*w, h, kVDTF_R8, 1, kVDTUsage_Default, NULL, &mpImageTex[0])) {
					Shutdown();
					return false;
				}

				if (!ctx.CreateFragmentProgram(kVDTPF_MultiTarget, VDTDataView(g_VDDispFP_BlitRGB24), &mpFP)) {
					Shutdown();
					return false;
				}

				mRenderMode = kRenderMode_BlitRGB24;
				mpVP = dctx.mpVPTexture3T;
				mpVP->AddRef();
				mpVF = dctx.mpVFTexture3T;
				mpVF->AddRef();
			}
			break;

		case nsVDPixmap::kPixFormat_YUV422_UYVY:
		case nsVDPixmap::kPixFormat_YUV422_UYVY_FR:
		case nsVDPixmap::kPixFormat_YUV422_UYVY_709:
		case nsVDPixmap::kPixFormat_YUV422_UYVY_709_FR:
		case nsVDPixmap::kPixFormat_YUV422_YUYV:
		case nsVDPixmap::kPixFormat_YUV422_YUYV_FR:
		case nsVDPixmap::kPixFormat_YUV422_YUYV_709:
		case nsVDPixmap::kPixFormat_YUV422_YUYV_709_FR:
			mTexWidth >>= 1;

			if (!ctx.CreateTexture2D(w >> 1, h, bgraFormat, 1, kVDTUsage_Default, NULL, &mpImageTex[0])) {
				Shutdown();
				return false;
			}

			VDTData program;

			switch(format) {
				case nsVDPixmap::kPixFormat_YUV422_UYVY:
					program = mbRenderSwapRB ? VDTDataView(g_VDDispFP_BlitUYVY_601_LR) : VDTDataView(g_VDDispFP_BlitUYVYRBSwap_601_LR);
					break;

				case nsVDPixmap::kPixFormat_YUV422_UYVY_FR:
					program = mbRenderSwapRB ? VDTDataView(g_VDDispFP_BlitUYVY_601_FR) : VDTDataView(g_VDDispFP_BlitUYVYRBSwap_601_FR);
					break;

				case nsVDPixmap::kPixFormat_YUV422_UYVY_709:
					program = mbRenderSwapRB ? VDTDataView(g_VDDispFP_BlitUYVY_709_LR) : VDTDataView(g_VDDispFP_BlitUYVYRBSwap_709_LR);
					break;

				case nsVDPixmap::kPixFormat_YUV422_UYVY_709_FR:
					program = mbRenderSwapRB ? VDTDataView(g_VDDispFP_BlitUYVY_709_FR) : VDTDataView(g_VDDispFP_BlitUYVYRBSwap_709_FR);
					break;

				case nsVDPixmap::kPixFormat_YUV422_YUYV:
					program = mbRenderSwapRB ? VDTDataView(g_VDDispFP_BlitYUYV_601_LR) : VDTDataView(g_VDDispFP_BlitYUYVRBSwap_601_LR);
					break;

				case nsVDPixmap::kPixFormat_YUV422_YUYV_FR:
					program = mbRenderSwapRB ? VDTDataView(g_VDDispFP_BlitYUYV_601_FR) : VDTDataView(g_VDDispFP_BlitYUYVRBSwap_601_FR);
					break;

				case nsVDPixmap::kPixFormat_YUV422_YUYV_709:
					program = mbRenderSwapRB ? VDTDataView(g_VDDispFP_BlitYUYV_709_LR) : VDTDataView(g_VDDispFP_BlitYUYVRBSwap_709_LR);
					break;

				case nsVDPixmap::kPixFormat_YUV422_YUYV_709_FR:
					program = mbRenderSwapRB ? VDTDataView(g_VDDispFP_BlitYUYV_709_FR) : VDTDataView(g_VDDispFP_BlitYUYVRBSwap_709_FR);
					break;
			}

			if (!ctx.CreateFragmentProgram(kVDTPF_MultiTarget, program, &mpFP))
			{
				Shutdown();
				return false;
			}

			mRenderMode = kRenderMode_BlitUYVY;

			mpVP = dctx.mpVPTexture3T;
			mpVP->AddRef();
			mpVF = dctx.mpVFTexture3T;
			mpVF->AddRef();
			break;

		case nsVDPixmap::kPixFormat_Y8:
		case nsVDPixmap::kPixFormat_Y8_FR:
			if (ctx.IsFormatSupportedTexture2D(kVDTF_R8)) {
				if (!ctx.CreateTexture2D(w, h, kVDTF_R8, 1, kVDTUsage_Default, NULL, &mpImageTex[0])) {
					Shutdown();
					return false;
				}

				if (!ctx.CreateFragmentProgram(kVDTPF_MultiTarget,
						format == nsVDPixmap::kPixFormat_Y8_FR
							? VDTDataView(g_VDDispFP_BlitY_FR)
							: VDTDataView(g_VDDispFP_BlitY_LR), &mpFP))
				{
					Shutdown();
					return false;
				}

				mRenderMode = kRenderMode_BlitY;

				mpVP = dctx.mpVPTexture;
				mpVP->AddRef();
				mpVF = dctx.mpVFTexture;
				mpVF->AddRef();
			}
			break;

		case nsVDPixmap::kPixFormat_YUV444_Planar:
		case nsVDPixmap::kPixFormat_YUV422_Planar:
		case nsVDPixmap::kPixFormat_YUV420_Planar:
		case nsVDPixmap::kPixFormat_YUV411_Planar:
		case nsVDPixmap::kPixFormat_YUV410_Planar:
		case nsVDPixmap::kPixFormat_YUV444_Planar_709:
		case nsVDPixmap::kPixFormat_YUV422_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420_Planar_709:
		case nsVDPixmap::kPixFormat_YUV411_Planar_709:
		case nsVDPixmap::kPixFormat_YUV410_Planar_709:
		case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV411_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV410_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV411_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV410_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV420it_Planar:
		case nsVDPixmap::kPixFormat_YUV420it_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420it_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV420it_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV420ib_Planar:
		case nsVDPixmap::kPixFormat_YUV420ib_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420ib_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV420ib_Planar_709_FR:
			if (ctx.IsFormatSupportedTexture2D(kVDTF_R8)) {
				const VDPixmapFormatInfo& formatInfo = VDPixmapGetInfo(format);

				if (!ctx.CreateTexture2D(w, h, kVDTF_R8, 1, kVDTUsage_Default, NULL, &mpImageTex[0])) {
					Shutdown();
					return false;
				}

				uint32 w2 = ((w - 1) >> formatInfo.auxwbits) + 1;
				uint32 h2 = ((h - 1) >> formatInfo.auxhbits) + 1;

				if (!ctx.CreateTexture2D(w2, h2, kVDTF_R8, 1, kVDTUsage_Default, NULL, &mpImageTex[1])) {
					Shutdown();
					return false;
				}

				if (!ctx.CreateTexture2D(w2, h2, kVDTF_R8, 1, kVDTUsage_Default, NULL, &mpImageTex[2])) {
					Shutdown();
					return false;
				}

				// 1:1 -> offset 0
				// 2:1 -> offset +1/4
				// 4:1 -> offset +3/8
				float chromaScaleH = (float)(1 << formatInfo.auxwbits);
				chromaOffsetU = (chromaScaleH - 1) / (chromaScaleH * 2.0f * (float)w2);

				switch(format) {
					case nsVDPixmap::kPixFormat_YUV420it_Planar:
					case nsVDPixmap::kPixFormat_YUV420it_Planar_709:
					case nsVDPixmap::kPixFormat_YUV420it_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV420it_Planar_709_FR:
						chromaOffsetV = 0.125f;
						break;

					case nsVDPixmap::kPixFormat_YUV420ib_Planar:
					case nsVDPixmap::kPixFormat_YUV420ib_Planar_709:
					case nsVDPixmap::kPixFormat_YUV420ib_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV420ib_Planar_709_FR:
						chromaOffsetV = -0.125f;
						break;
				}

				switch(format) {
					case nsVDPixmap::kPixFormat_YUV444_Planar:
					case nsVDPixmap::kPixFormat_YUV422_Planar:
					case nsVDPixmap::kPixFormat_YUV420_Planar:
					case nsVDPixmap::kPixFormat_YUV411_Planar:
					case nsVDPixmap::kPixFormat_YUV410_Planar:
					case nsVDPixmap::kPixFormat_YUV420it_Planar:
					case nsVDPixmap::kPixFormat_YUV420ib_Planar:
						if (!ctx.CreateFragmentProgram(kVDTPF_MultiTarget, VDTDataView(g_VDDispFP_BlitYCbCr_601_LR), &mpFP)) {
							Shutdown();
							return false;
						}
						break;

					case nsVDPixmap::kPixFormat_YUV444_Planar_709:
					case nsVDPixmap::kPixFormat_YUV422_Planar_709:
					case nsVDPixmap::kPixFormat_YUV420_Planar_709:
					case nsVDPixmap::kPixFormat_YUV411_Planar_709:
					case nsVDPixmap::kPixFormat_YUV410_Planar_709:
					case nsVDPixmap::kPixFormat_YUV420it_Planar_709:
					case nsVDPixmap::kPixFormat_YUV420ib_Planar_709:
						if (!ctx.CreateFragmentProgram(kVDTPF_MultiTarget, VDTDataView(g_VDDispFP_BlitYCbCr_709_LR), &mpFP)) {
							Shutdown();
							return false;
						}
						break;

					case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV411_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV410_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV420it_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV420ib_Planar_FR:
						if (!ctx.CreateFragmentProgram(kVDTPF_MultiTarget, VDTDataView(g_VDDispFP_BlitYCbCr_601_FR), &mpFP)) {
							Shutdown();
							return false;
						}
						break;

					case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
					case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
					case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
					case nsVDPixmap::kPixFormat_YUV411_Planar_709_FR:
					case nsVDPixmap::kPixFormat_YUV410_Planar_709_FR:
					case nsVDPixmap::kPixFormat_YUV420it_Planar_709_FR:
					case nsVDPixmap::kPixFormat_YUV420ib_Planar_709_FR:
						if (!ctx.CreateFragmentProgram(kVDTPF_MultiTarget, VDTDataView(g_VDDispFP_BlitYCbCr_709_FR), &mpFP)) {
							Shutdown();
							return false;
						}
						break;
				}

				mTex2Width = w2;
				mTex2Height = h2;
				mRenderMode = kRenderMode_BlitYCbCr;

				mpVP = dctx.mpVPTexture2T;
				mpVP->AddRef();
				mpVF = dctx.mpVFTexture2T;
				mpVF->AddRef();
			}
			break;

		case nsVDPixmap::kPixFormat_Pal8:
			if (ctx.IsFormatSupportedTexture2D(kVDTF_R8)) {
				if (!ctx.CreateTexture2D(w, h, kVDTF_R8, 1, kVDTUsage_Default, NULL, &mpImageTex[0])) {
					Shutdown();
					return false;
				}

				if (!ctx.CreateTexture2D(256, 1, bgraFormat, 1, kVDTUsage_Default, NULL, &mpPaletteTex)) {
					Shutdown();
					return false;
				}

				if (!ctx.CreateFragmentProgram(kVDTPF_MultiTarget, mbRenderSwapRB ? VDTDataView(g_VDDispFP_BlitPal8RBSwap) : VDTDataView(g_VDDispFP_BlitPal8), &mpFP)) {
					Shutdown();
					return false;
				}

				mRenderMode = kRenderMode_BlitPal8;
				mpVP = dctx.mpVPTexture;
				mpVP->AddRef();
				mpVF = dctx.mpVFTexture;
				mpVF->AddRef();
			}
			break;
		
		default:
			break;
	}

	if (!mpFP) {
		if (!ctx.CreateTexture2D(w, h, bgraFormat, 1, kVDTUsage_Default, NULL, &mpImageTex[0])) {
			Shutdown();
			return false;
		}

		if (!ctx.CreateFragmentProgram(kVDTPF_MultiTarget, mbRenderSwapRB ? VDTDataView(g_VDDispFP_BlitRBSwap) : VDTDataView(g_VDDispFP_Blit), &mpFP)) {
			Shutdown();
			return false;
		}

		mRenderMode = kRenderMode_Blit;
		mpVP = dctx.mpVPTexture;
		mpVP->AddRef();
		mpVF = dctx.mpVFTexture;
		mpVF->AddRef();
	}

	switch(mRenderMode) {
		case kRenderMode_BlitUYVY: {
			const float u0 = 0.25f / (float)mTexWidth;
			const float u1 = u0 + 1.0f;
			const float w0 = 0.0f;
			const float w1 = (float)mTexWidth;

			const VDDisplayVertex3T3D vx[4]={
				{ -1.0f, +1.0f, 0.0f, 0.0f, 0.0f, u0, 0.0f, w0 },
				{ -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, u0, 1.0f, w0 },
				{ +1.0f, +1.0f, 0.0f, 1.0f, 0.0f, u1, 0.0f, w1 },
				{ +1.0f, -1.0f, 0.0f, 1.0f, 1.0f, u1, 1.0f, w1 },
			};

			if (!ctx.CreateVertexBuffer(sizeof vx, false, vx, &mpVB)) {
				Shutdown();
				return false;
			}
			break;
		}

		case kRenderMode_BlitYCbCr: {
			const float u0 = chromaOffsetU;
			const float u1 = chromaOffsetU + 1.0f;

			const VDDisplayVertex2T3D vx[4]={
				{ -1.0f, +1.0f, 0.0f, 0.0f, 0.0f, u0, 0.0f },
				{ -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, u0, 1.0f },
				{ +1.0f, +1.0f, 0.0f, 1.0f, 0.0f, u1, 0.0f },
				{ +1.0f, -1.0f, 0.0f, 1.0f, 1.0f, u1, 1.0f },
			};

			if (!ctx.CreateVertexBuffer(sizeof vx, false, vx, &mpVB)) {
				Shutdown();
				return false;
			}

			break;
		}

		case kRenderMode_BlitRGB24: {
			const float u0 = -1.0f / (float)mTexWidth;
			const float u1 = u0 + 1.0f;
			const float u2 = 0.0f;
			const float u3 = 1.0f;
			const float u4 = +1.0f / (float)mTexWidth;
			const float u5 = u4 + 1.0f;

			const VDDisplayVertex3T3D vx[4]={
				{ -1.0f, +1.0f, 0.0f, u0, 0.0f, u2, 0.0f, u4, 0.0f },
				{ -1.0f, -1.0f, 0.0f, u0, 1.0f, u2, 1.0f, u4, 1.0f },
				{ +1.0f, +1.0f, 0.0f, u1, 0.0f, u3, 0.0f, u5, 0.0f },
				{ +1.0f, -1.0f, 0.0f, u1, 1.0f, u3, 1.0f, u5, 1.0f },
			};

			if (!ctx.CreateVertexBuffer(sizeof vx, false, vx, &mpVB)) {
				Shutdown();
				return false;
			}
			break;
		}

		default: {
			static const VDDisplayVertex3D vx[4]={
				{ -1.0f, +1.0f, 0.0f, 0.0f, 0.0f },
				{ -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
				{ +1.0f, +1.0f, 0.0f, 1.0f, 0.0f },
				{ +1.0f, -1.0f, 0.0f, 1.0f, 1.0f },
			};

			if (!ctx.CreateVertexBuffer(sizeof vx, false, vx, &mpVB)) {
				Shutdown();
				return false;
			}
			break;
		}
	}

	return true;
}

void VDDisplayImageNode3D::Shutdown() {
	for(int i=0; i<3; ++i)
		vdsaferelease <<= mpImageTex[i];

	vdsaferelease <<= mpPaletteTex, mpVF, mpVP, mpFP, mpVB;
}

void VDDisplayImageNode3D::Load(const VDPixmap& px) {
	VDTLockData2D lockData;

	if (mRenderMode == kRenderMode_BlitPal8) {
		if (mbRenderSwapRB) {
			VDTLockData2D lockData;
			if (mpPaletteTex->Lock(0, NULL, lockData)) {
				const uint32 *VDRESTRICT pal = px.palette;
				uint32 *VDRESTRICT dst = (uint32 *)lockData.mpData;

				for(uint32 i=0; i<256; ++i)
					dst[i] = VDSwizzleU32(pal[i]) >> 8;

				mpPaletteTex->Unlock(0);
			}
		} else {
			const VDTInitData2D initData = { px.palette, 0 };

			mpPaletteTex->Load(0, 0, 0, initData, 256, 1);
		}

		const VDTInitData2D plane0 = { px.data, px.pitch };

		mpImageTex[0]->Load(0, 0, 0, plane0, mTexWidth, mTexHeight);
	} else if (mRenderMode == kRenderMode_BlitY
		|| mRenderMode == kRenderMode_BlitUYVY
		|| mRenderMode == kRenderMode_BlitRGB16
		|| mRenderMode == kRenderMode_BlitRGB16Direct
		|| mRenderMode == kRenderMode_BlitRGB24
		) {
		const VDTInitData2D plane0 = { px.data, px.pitch };

		mpImageTex[0]->Load(0, 0, 0, plane0, mTexWidth, mTexHeight);
	} else if (mRenderMode == kRenderMode_BlitYCbCr) {
		const VDTInitData2D plane0 = { px.data, px.pitch };
		const VDTInitData2D plane1 = { px.data2, px.pitch2 };
		const VDTInitData2D plane2 = { px.data3, px.pitch3 };

		mpImageTex[0]->Load(0, 0, 0, plane0, mTexWidth, mTexHeight);
		mpImageTex[1]->Load(0, 0, 0, plane1, mTex2Width, mTex2Height);
		mpImageTex[2]->Load(0, 0, 0, plane2, mTex2Width, mTex2Height);
	} else {
		if (!mpImageTex[0]->Lock(0, NULL, lockData))
			return;

		VDPixmap dstpx = {};
		dstpx.data = lockData.mpData;
		dstpx.pitch = lockData.mPitch;
		dstpx.format = nsVDPixmap::kPixFormat_XRGB8888;
		dstpx.w = mTexWidth;
		dstpx.h = mTexHeight;

		VDPixmapBlt(dstpx, px);

		mpImageTex[0]->Unlock(0);
	}
}

void VDDisplayImageNode3D::Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx) {
	ctx.SetBlendState(NULL);
	ctx.SetRasterizerState(NULL);

	switch(mRenderMode) {
	case kRenderMode_Blit:
	case kRenderMode_BlitY:
	case kRenderMode_BlitRGB16Direct:
		{
			ctx.SetSamplerStates(0, 1, mbBilinear ? &dctx.mpSSBilinear : &dctx.mpSSPoint);

			IVDTTexture *tex = mpImageTex[0];

			ctx.SetTextures(0, 1, &tex);
			ctx.SetVertexStream(0, mpVB, 0, sizeof(VDDisplayVertex3D));
		}
		break;

	case kRenderMode_BlitPal8:
	case kRenderMode_BlitRGB16:
		{
			IVDTSamplerState *ss[2] = {dctx.mpSSBilinear, dctx.mpSSPoint};
			ctx.SetSamplerStates(0, 2, ss);

			IVDTTexture *const tex[2] = {
				mpImageTex[0],
				mpPaletteTex
			};

			ctx.SetTextures(0, 2, tex);
			ctx.SetVertexStream(0, mpVB, 0, sizeof(VDDisplayVertex3D));
		}
		break;

	case kRenderMode_BlitYCbCr:
		{
			IVDTSamplerState *ss0 = mbBilinear ? dctx.mpSSBilinear : dctx.mpSSPoint;

			IVDTSamplerState *ss[3] = {
				ss0,
				ss0,
				ss0
			};

			ctx.SetSamplerStates(0, 3, ss);

			IVDTTexture *const tex[3] = {
				mpImageTex[0],
				mpImageTex[1],
				mpImageTex[2],
			};

			ctx.SetTextures(0, 3, tex);
			ctx.SetVertexStream(0, mpVB, 0, sizeof(VDDisplayVertex2T3D));
		}
		break;

	case kRenderMode_BlitUYVY:
		{
			IVDTSamplerState *ss[2] = { dctx.mpSSPoint, dctx.mpSSBilinear };
			ctx.SetSamplerStates(0, 2, ss);

			IVDTTexture *tex[2] = { mpImageTex[0], mpImageTex[0] };

			ctx.SetTextures(0, 2, tex);
			ctx.SetVertexStream(0, mpVB, 0, sizeof(VDDisplayVertex3T3D));
		}
		break;

	case kRenderMode_BlitRGB24:
		{
			ctx.SetSamplerStates(0, 1, &dctx.mpSSPoint);

			IVDTTexture *tex = mpImageTex[0];

			ctx.SetTextures(0, 1, &tex);
			ctx.SetVertexStream(0, mpVB, 0, sizeof(VDDisplayVertex3T3D));
		}
		break;
	}

	ctx.SetVertexFormat(mpVF);
	ctx.SetVertexProgram(mpVP);
	ctx.SetFragmentProgram(mpFP);

	ctx.SetIndexStream(NULL);

	VDTViewport vp = ctx.GetViewport();
	VDTViewport newvp = vp;

	newvp.mX += mDstX;
	newvp.mY += mDstY;
	newvp.mWidth = mDstW;
	newvp.mHeight = mDstH;
	ctx.SetViewport(newvp);

	ctx.DrawPrimitive(kVDTPT_TriangleStrip, 0, 2);

	ctx.SetViewport(vp);
}

///////////////////////////////////////////////////////////////////////////

VDDisplayBlitNode3D::VDDisplayBlitNode3D()
	: mpVB(NULL)
	, mpFP(NULL)
	, mpSourceNode(NULL)
	, mbLinear(false)
	, mSharpnessX(0.0f)
	, mSharpnessY(0.0f)
	, mDstX(0)
	, mDstY(0)
	, mDstW(1)
	, mDstH(1)
{
}

VDDisplayBlitNode3D::~VDDisplayBlitNode3D() {
	Shutdown();
}

bool VDDisplayBlitNode3D::Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, uint32 w, uint32 h, bool linear, float sharpnessX, float sharpnessY, VDDisplaySourceNode3D *source) {
	if (mpSourceNode != source) {
		if (mpSourceNode)
			mpSourceNode->Release();

		mpSourceNode = source;
		source->AddRef();

		vdsaferelease <<= mpVB, mpFP;
	}

	mMapping = source->GetTextureMapping();

	const bool sharpBilinear = linear && (sharpnessX != 1.0f || sharpnessY != 1.0f);

	if (!mpVB) {
		const float u0 = 0.0f;
		const float v0 = 0.0f;
		const float u1 = sharpBilinear ? (float)w : mMapping.mUSize;
		const float v1 = sharpBilinear ? (float)h : mMapping.mVSize;

		const VDDisplayVertex3D vx[8]={
			{ -1.0f, +1.0f, 0.0f, u0, v0 },
			{ -1.0f, -1.0f, 0.0f, u0, v1 },
			{ +1.0f, +1.0f, 0.0f, u1, v0 },
			{ +1.0f, -1.0f, 0.0f, u1, v1 },
		};

		if (!ctx.CreateVertexBuffer(sizeof vx, false, vx, &mpVB)) {
			Shutdown();
			return false;
		}
	}

	if (sharpBilinear) {
		if (!mpFP) {
			if (!ctx.CreateFragmentProgram(kVDTPF_MultiTarget, mMapping.mbRBSwap ? VDTDataView(g_VDDispFP_BlitSharpRBSwap) : VDTDataView(g_VDDispFP_BlitSharp), &mpFP)) {
				Shutdown();
				return false;
			}
		}
	} else {
		vdsaferelease <<= mpFP;
	}

	mbLinear = linear;
	mSharpnessX = sharpnessX;
	mSharpnessY = sharpnessY;
	return true;
}

void VDDisplayBlitNode3D::Shutdown() {
	vdsaferelease <<= mpFP, mpVB, mpSourceNode;
}

void VDDisplayBlitNode3D::Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx) {
	IVDTTexture2D *src = mpSourceNode->Draw(ctx, dctx);

	if (!src)
		return;

	ctx.SetBlendState(NULL);
	ctx.SetRasterizerState(NULL);

	ctx.SetSamplerStates(0, 1, mbLinear ? &dctx.mpSSBilinear : &dctx.mpSSPoint);

	ctx.SetVertexProgram(dctx.mpVPTexture);

	if (mpFP) {
		float params[4]={
			mSharpnessX,
			mSharpnessY,
			1.0f / (float)mMapping.mTexHeight,
			1.0f / (float)mMapping.mTexWidth,
		};

		ctx.SetFragmentProgramConstF(0, 1, params);
		ctx.SetFragmentProgram(mpFP);
	} else
		ctx.SetFragmentProgram(dctx.mpFPBlit);

	ctx.SetVertexFormat(dctx.mpVFTexture);
	ctx.SetVertexStream(0, mpVB, 0, sizeof(VDDisplayVertex3D));
	ctx.SetIndexStream(NULL);

	IVDTTexture *tex = src;
	ctx.SetTextures(0, 1, &tex);

	VDTViewport oldvp = ctx.GetViewport();
	VDTViewport vp = oldvp;

	vp.mX += mDstX;
	vp.mY += mDstY;
	vp.mWidth = mDstW;
	vp.mHeight = mDstH;

	ctx.SetViewport(vp);
	ctx.DrawPrimitive(kVDTPT_TriangleStrip, 0, 2);
	ctx.SetViewport(oldvp);

	IVDTTexture *texnull = NULL;
	ctx.SetTextures(0, 1, &texnull);
}

///////////////////////////////////////////////////////////////////////////

struct VDDisplayStretchBicubicNode3D::Vertex {
	float x, y, z;
	float u0, v0;
	float u1, v1;
	float u2, v2;
	float uf, vf;
};

VDDisplayStretchBicubicNode3D::VDDisplayStretchBicubicNode3D()
	: mpFP(NULL)
	, mpVP(NULL)
	, mpVF(NULL)
	, mpVB(NULL)
	, mpRTTHoriz(NULL)
	, mpFilterTex(NULL)
	, mpSourceNode(NULL)
	, mSrcW(0)
	, mSrcH(0)
	, mDstX(0)
	, mDstY(0)
	, mDstW(0)
	, mDstH(0)
{
}

VDDisplayStretchBicubicNode3D::~VDDisplayStretchBicubicNode3D() {
	Shutdown();
}

bool VDDisplayStretchBicubicNode3D::Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, uint32 srcw, uint32 srch, sint32 dstx, sint32 dsty, uint32 dstw, uint32 dsth, VDDisplaySourceNode3D *src) {
	uint32 srctexh = srch;
	uint32 dsttexw = dstw;

	const VDTDeviceCaps& caps = ctx.GetDeviceCaps();
	if (!caps.mbNonPow2Conditional) {
		srctexh = VDCeilToPow2(srctexh);
		dsttexw = VDCeilToPow2(dsttexw);
	}

	if (dsttexw > caps.mMaxTextureWidth ||
		srctexh > caps.mMaxTextureHeight)
	{
		Shutdown();
		return false;
	}

	mpSourceNode = src;
	src->AddRef();

	if (!mpVP) {
		if (!ctx.CreateVertexProgram(kVDTPF_MultiTarget, VDTDataView(g_VDDispVP_StretchBltCubic), &mpVP)) {
			Shutdown();
			return false;
		}
	}

	if (!mpFP) {
		if (!ctx.CreateFragmentProgram(kVDTPF_MultiTarget, VDTDataView(g_VDDispFP_StretchBltCubic), &mpFP)) {
			Shutdown();
			return false;
		}
	}

	if (!mpVF) {
		static const VDTVertexElement kVertexFormat[]={
			{ offsetof(Vertex, x), kVDTET_Float3, kVDTEU_Position, 0 },
			{ offsetof(Vertex, u0), kVDTET_Float2, kVDTEU_TexCoord, 0 },
			{ offsetof(Vertex, u1), kVDTET_Float2, kVDTEU_TexCoord, 1 },
			{ offsetof(Vertex, u2), kVDTET_Float2, kVDTEU_TexCoord, 2 },
			{ offsetof(Vertex, uf), kVDTET_Float2, kVDTEU_TexCoord, 3 },
		};

		if (!ctx.CreateVertexFormat(kVertexFormat, 5, mpVP, &mpVF)) {
			Shutdown();
			return false;
		}
	}

	if (mSrcW != srcw || mSrcH != srch || mDstW != dstw || mDstH != dsth) {
		vdsaferelease <<= mpVB, mpRTTHoriz, mpFilterTex;

		mSrcW = srcw;
		mSrcH = srch;
		mDstW = dstw;
		mDstH = dsth;
	}

	mDstX = dstx;
	mDstY = dsty;

	const VDDisplaySourceTexMapping mapping = src->GetTextureMapping();

	const uint32 filterTexWidth = std::max<uint32>(dstw, dsth);

	const float hdu = srcw / (float)mapping.mTexWidth;
	const float hdv = srch / (float)mapping.mTexHeight;

	const float u0 = -1.5f / (float)mapping.mTexWidth;
	const float u1 = u0 + hdu;
	const float u2 = 0.0f;
	const float u3 = hdu;
	const float u4 = +1.5f / (float)mapping.mTexWidth;
	const float u5 = u4 + hdu;

	const float vdu = (float)dstw / (float)dsttexw;
	const float vdv = (float)srch / (float)srctexh;

	const float v0 = -1.5f / (float)srch;
	const float v1 = v0 + vdv;
	const float v2 = 0.0f;
	const float v3 = vdv;
	const float v4 = +1.5f / (float)srch;
	const float v5 = v4 + vdv;

	const float hf0 = 0;
	const float hf1 = dstw / (float)filterTexWidth;
	const float vf0 = 0;
	const float vf1 = dsth / (float)filterTexWidth;

	const Vertex vx[8]={
		// horizontal pass
		{ -1.0f, +1.0f, 0.0f, u0, 0.0f, u2, 0.0f, u4, 0.0f, hf0, 0.0f },
		{ -1.0f, -1.0f, 0.0f, u0, hdv,  u2, hdv,  u4, hdv,  hf0, 0.0f },
		{ +1.0f, +1.0f, 0.0f, u1, 0.0f, u3, 0.0f, u5, 0.0f, hf1, 0.0f },
		{ +1.0f, -1.0f, 0.0f, u1, hdv,  u3, hdv,  u5, hdv,  hf1, 0.0f },

		// vertical pass
		{ -1.0f, +1.0f, 0.0f, 0.0f, v0, 0.0f, v2, 0.0f, v4, vf0, 1.0f },
		{ -1.0f, -1.0f, 0.0f, 0.0f, v1, 0.0f, v3, 0.0f, v5, vf1, 1.0f },
		{ +1.0f, +1.0f, 0.0f, vdu,  v0, vdu,  v2, vdu,  v4, vf0, 1.0f },
		{ +1.0f, -1.0f, 0.0f, vdu,  v1, vdu,  v3, vdu,  v5, vf1, 1.0f },
	};

	if (!ctx.CreateVertexBuffer(sizeof vx, false, vx, &mpVB)) {
		Shutdown();
		return false;
	}

	if (!ctx.CreateTexture2D(dsttexw, srctexh, dctx.mBGRAFormat, 1, kVDTUsage_Render, NULL, &mpRTTHoriz)) {
		Shutdown();
		return false;
	}

	vdfastvector<uint32> texData(filterTexWidth * 2, 0);

	const bool swapRB = (dctx.mBGRAFormat == kVDTF_R8G8B8A8);
	VDDisplayCreateBicubicTexture(texData.data(), dstw, srcw, swapRB);
	VDDisplayCreateBicubicTexture(texData.data() + filterTexWidth, dsth, srch, swapRB);

	VDTInitData2D texFiltInitData = { texData.data(), filterTexWidth * sizeof(uint32) };
	if (!ctx.CreateTexture2D(filterTexWidth, 2, dctx.mBGRAFormat, 1, kVDTUsage_Default, &texFiltInitData, &mpFilterTex)) {
		Shutdown();
		return false;
	}

	return true;
}

void VDDisplayStretchBicubicNode3D::Shutdown() {
	vdsaferelease <<= mpRTTHoriz, mpFilterTex, mpVF, mpVP, mpFP, mpVB, mpSourceNode;
}

void VDDisplayStretchBicubicNode3D::Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx) {
	if (!mpFilterTex)
		return;

	IVDTTexture2D *srctex = mpSourceNode->Draw(ctx, dctx);

	if (!srctex)
		return;

	IVDTSurface *pPrevTarget = ctx.GetRenderTarget(0);
	const VDTViewport& oldvp = ctx.GetViewport();

	ctx.SetBlendState(NULL);
	ctx.SetRasterizerState(NULL);

	IVDTSamplerState *samplers[3] = { dctx.mpSSBilinear, dctx.mpSSPoint, dctx.mpSSPoint };
	ctx.SetSamplerStates(0, 3, samplers);

	ctx.SetVertexProgram(mpVP);
	ctx.SetFragmentProgram(mpFP);

	ctx.SetVertexFormat(mpVF);
	ctx.SetVertexStream(0, mpVB, 0, sizeof(Vertex));
	ctx.SetIndexStream(NULL);

	// do horizontal blit
	IVDTSurface *rttsurfh = mpRTTHoriz->GetLevelSurface(0);

	VDTSurfaceDesc rttsurfdesc;
	rttsurfh->GetDesc(rttsurfdesc);

	VDTViewport vp;
	vp.mX = 0;
	vp.mY = 0;
	vp.mWidth = mDstW;
	vp.mHeight = mSrcH;
	vp.mMinZ = 0.0f;
	vp.mMaxZ = 1.0f;
	ctx.SetRenderTarget(0, rttsurfh);
	ctx.SetViewport(vp);

	IVDTTexture *texh[3] = { srctex, srctex, mpFilterTex };
	ctx.SetTextures(0, 3, texh);

	ctx.DrawPrimitive(kVDTPT_TriangleStrip, 0, 2);

	// do vertical blit
	ctx.SetRenderTarget(0, pPrevTarget);

	VDTViewport finalvp = oldvp;
	finalvp.mX += mDstX;
	finalvp.mY += mDstY;
	finalvp.mWidth = mDstW;
	finalvp.mHeight = mDstH;
	ctx.SetViewport(finalvp);

	IVDTTexture *texv[3] = { mpRTTHoriz, mpRTTHoriz, mpFilterTex };
	ctx.SetTextures(0, 3, texv);

	ctx.DrawPrimitive(kVDTPT_TriangleStrip, 4, 2);

	ctx.SetViewport(oldvp);

	IVDTTexture *texc[3] = {};
	ctx.SetTextures(0, 3, texc);
}
