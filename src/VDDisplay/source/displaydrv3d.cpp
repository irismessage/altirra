//	VirtualDub - Video processing and capture application
//	A/V interface library
//	Copyright (C) 1998-2005 Avery Lee
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


#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/vdstl.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Tessa/Context.h>
#include <vd2/VDDisplay/compositor.h>
#include <vd2/VDDisplay/logging.h>
#include "displaydrv3d.h"
#include "image_shader.inl"

#include <vd2/Kasumi/resample.h>

bool VDTCreateContextD3D9(IVDTContext **ppctx);
bool VDTCreateContextD3D9(int width, int height, int refresh, bool fullscreen, bool vsync, void *hwnd, IVDTContext **ppctx);
bool VDTCreateContextD3D11(IVDTContext **ppctx);
bool VDTCreateContextD3D11(int width, int height, int refresh, bool fullscreen, bool vsync, void *hwnd, IVDTContext **ppctx);

///////////////////////////////////////////////////////////////////////////

VDDisplayDriver3D::VDDisplayDriver3D()
	: mhwnd(NULL)
	, mhMonitor(NULL)
	, mpContext(NULL)
	, mpSwapChain(NULL)
	, mpImageNode(NULL)
	, mpImageSourceNode(NULL)
	, mpRootNode(NULL)
	, mFilterMode(kFilterBilinear)
	, mbCompositionTreeDirty(false)
	, mbFramePending(false)
	, mbFullScreen(false)
	, mFullScreenWidth(0)
	, mFullScreenHeight(0)
	, mFullScreenRefreshRate(0)
	, mSource()
{
}

VDDisplayDriver3D::~VDDisplayDriver3D() {
}

bool VDDisplayDriver3D::Init(HWND hwnd, HMONITOR hmonitor, const VDVideoDisplaySourceInfo& info) {
	mhwnd = hwnd;
	mhMonitor = hmonitor;

//	if (!VDTCreateContextD3D9(&mpContext))
	if (!VDTCreateContextD3D11(&mpContext))
		return false;

	if (!mDisplayNodeContext.Init(*mpContext, mbRenderLinear)) {
		Shutdown();
		return false;
	}

	if (!mRenderer.Init(*mpContext)) {
		Shutdown();
		return false;
	}

	VDASSERT(info.pixmap.data);
	mSource = info;
	mbHDR = info.mbHDR;
	RebuildTree();
	return true;
}

void VDDisplayDriver3D::Shutdown() {
	mDisplayNodeContext.Shutdown();
	mRenderer.Shutdown();

	DestroyImageNode();

	vdsaferelease <<= mpRootNode,
		mpSwapChain,
		mpContext;

	mhwnd = NULL;
}

bool VDDisplayDriver3D::ModifySource(const VDVideoDisplaySourceInfo& info) {
	bool rebuildTree = false;

	if (info.pixmap.w != mSource.pixmap.w ||
		info.pixmap.h != mSource.pixmap.h ||
		info.pixmap.format != mSource.pixmap.format ||
		info.mbHDR != mSource.mbHDR)
	{
		rebuildTree = true;
	}

	VDASSERT(info.pixmap.data);
	mSource = info;
	mbHDR = info.mbHDR;

	if (rebuildTree) {
		DestroyImageNode();

		if (!CreateImageNode())
			return false;

		mbCompositionTreeDirty = true;
	}

	return true;
}

void VDDisplayDriver3D::SetFilterMode(FilterMode mode) {
	if (mode == kFilterAnySuitable)
		mode = kFilterBilinear;

	if (mFilterMode == mode)
		return;

	mFilterMode = mode;
	mbCompositionTreeDirty = true;
}

void VDDisplayDriver3D::SetFullScreen(bool fullscreen, uint32 w, uint32 h, uint32 refresh, bool use16bit) {
	if (mbFullScreen != fullscreen) {
		mbFullScreen = fullscreen;
		mFullScreenWidth = w;
		mFullScreenHeight = h;
		mFullScreenRefreshRate = refresh;

		vdsaferelease <<= mpSwapChain;

		if (mpContext)
			CreateSwapChain();
	}
}

void VDDisplayDriver3D::SetDestRect(const vdrect32 *r, uint32 color) {
	VDVideoDisplayMinidriver::SetDestRect(r, color);

	mbCompositionTreeDirty = true;
}

void VDDisplayDriver3D::SetPixelSharpness(float xfactor, float yfactor) {
	VDVideoDisplayMinidriver::SetPixelSharpness(xfactor, yfactor);

	mbCompositionTreeDirty = true;
}

bool VDDisplayDriver3D::SetScreenFX(const VDVideoDisplayScreenFXInfo *screenFX) {
	if (screenFX) {
		if (!mbUseScreenFX) {
			mbUseScreenFX = true;
			mbCompositionTreeDirty = true;
		}

		if (!mbCompositionTreeDirty) {
			if (mScreenFXInfo.mScanlineIntensity != screenFX->mScanlineIntensity
				|| mScreenFXInfo.mGamma != screenFX->mGamma
				|| mScreenFXInfo.mPALBlendingOffset != screenFX->mPALBlendingOffset
				|| mScreenFXInfo.mbColorCorrectAdobeRGB != screenFX->mbColorCorrectAdobeRGB
				|| memcmp(mScreenFXInfo.mColorCorrectionMatrix, screenFX->mColorCorrectionMatrix, sizeof(mScreenFXInfo.mColorCorrectionMatrix))
				|| mScreenFXInfo.mDistortionX != screenFX->mDistortionX
				|| mScreenFXInfo.mDistortionYRatio != screenFX->mDistortionYRatio
				|| mScreenFXInfo.mBloomThreshold != screenFX->mBloomThreshold
				|| mScreenFXInfo.mBloomRadius != screenFX->mBloomRadius
				|| mScreenFXInfo.mBloomDirectIntensity != screenFX->mBloomDirectIntensity
				|| mScreenFXInfo.mBloomIndirectIntensity != screenFX->mBloomIndirectIntensity
				|| mScreenFXInfo.mbSignedRGBEncoding != screenFX->mbSignedRGBEncoding
				|| mScreenFXInfo.mHDRIntensity != screenFX->mHDRIntensity
				)
			{
				mbCompositionTreeDirty = true;
			}
		}

		mScreenFXInfo = *screenFX;
	} else {
		if (mbUseScreenFX) {
			mbUseScreenFX = false;
			mbCompositionTreeDirty = true;
		}
	}

	return true;
}

bool VDDisplayDriver3D::IsValid() {
	return true;
}

bool VDDisplayDriver3D::IsFramePending() {
	return mbFramePending;
}

bool VDDisplayDriver3D::IsScreenFXSupported() const {
	return true;
}

VDDHDRAvailability VDDisplayDriver3D::IsHDRCapable() const {
	if (!mpContext->IsFormatSupportedTexture2D(kVDTF_R16G16B16A16F))
		return VDDHDRAvailability::NoHardwareSupport;

	bool systemSupport = false;
	if (!mpContext->IsMonitorHDREnabled(mhMonitor, systemSupport))
		return systemSupport ? VDDHDRAvailability::NoDisplaySupport : VDDHDRAvailability::NoSystemSupport;

	return VDDHDRAvailability::Available;
}

bool VDDisplayDriver3D::Resize(int w, int h) {
	bool success = VDVideoDisplayMinidriver::Resize(w, h);

	if (mFilterMode == kFilterBicubic)
		mbCompositionTreeDirty = true;

	return success;
}

bool VDDisplayDriver3D::Update(UpdateMode) {
	if (mpImageNode)
		mpImageNode->Load(mSource.pixmap);
	else if (mpImageSourceNode)
		mpImageSourceNode->Load(mSource.pixmap);

	return true;
}

void VDDisplayDriver3D::Refresh(UpdateMode updateMode) {
	const uint32 w = (uint32)mClientRect.right;
	const uint32 h = (uint32)mClientRect.bottom;

	if (!w || !h)
		return;

	if (mpSwapChain && !mpSwapChain->CheckOcclusion())
		return;

	VDDisplayCompositeInfo compInfo = {};

	if (mpCompositor) {
		compInfo.mWidth = w;
		compInfo.mHeight = h;

		mpCompositor->PreComposite(compInfo);
	}

	if (mbCompositionTreeDirty) {
		if (!RebuildTree())
			return;

		mbCompositionTreeDirty = false;
	}

	VDTSwapChainDesc swapDesc;
	if (mpSwapChain) {
		mpSwapChain->GetDesc(swapDesc);

		if (swapDesc.mbHDR != mbHDR) {
			vdsaferelease <<= mpSwapChain;
		} else if (swapDesc.mWidth != w || swapDesc.mHeight != h) {
			if (mbFullScreen) {
				if (!mpSwapChain->ResizeBuffers(w, h))
					return;
			} else {
				mpSwapChain->Release();
				mpSwapChain = NULL;
			}
		}
	}

	if (!mpSwapChain) {
		if (!CreateSwapChain())
			return;
	}

	IVDTSurface *surface = mpSwapChain->GetBackBuffer();

	mpContext->SetRenderTarget(0, surface, false);

	VDTViewport vp;
	vp.mX = 0;
	vp.mY = 0;
	vp.mWidth = w;
	vp.mHeight = h;
	vp.mMinZ = 0.0f;
	vp.mMaxZ = 1.0f;
	mpContext->SetViewport(vp);

	mDisplayNodeContext.mSDRBrightness = mSDRBrightness;

	const VDDRenderView& renderView = mDisplayNodeContext.CaptureRenderView();
	{
		VDTAutoScope scope(*mpContext, "Node tree");
		mpRootNode->Draw(*mpContext, mDisplayNodeContext, renderView);
	}

	mDisplayNodeContext.ApplyRenderView(renderView);

	if (mpCompositor) {
		VDTAutoScope scope(*mpContext, "Compositor");

		mRenderer.Begin(w, h, mDisplayNodeContext, mbHDR);
		mpCompositor->Composite(mRenderer, compInfo);
		mRenderer.End();
	}

	if (CheckForCapturePending() && mSource.mpCB) {
		bool readbackSucceeded = false;
		VDPixmapBuffer pxbuf;

		VDTSwapChainDesc desc;
		mpSwapChain->GetDesc(desc);

		pxbuf.init(desc.mWidth, desc.mHeight, nsVDPixmap::kPixFormat_XRGB8888);

		vdrefptr<IVDTReadbackBuffer> rbuf;
		if (mpContext->CreateReadbackBuffer(desc.mWidth, desc.mHeight, kVDTF_B8G8R8A8, ~rbuf)) {
			if (mpSwapChain->GetBackBuffer()->Readback(rbuf)) {
				VDTLockData2D lock;

				if (rbuf->Lock(lock)) {
					VDMemcpyRect(pxbuf.data, pxbuf.pitch, lock.mpData, lock.mPitch, desc.mWidth * 4, desc.mHeight);
					rbuf->Unlock();

					readbackSucceeded = true;
				}
			}
		}

		if (readbackSucceeded)
			mSource.mpCB->OnFrameCaptured(&pxbuf);
		else
			mSource.mpCB->OnFrameCaptured(nullptr);
	}

	if (updateMode & kModeVSync) {
		mbFramePending = true;
		mpSwapChain->PresentVSync(mhMonitor, this);
	} else {
		mbFramePending = false;
		mpSwapChain->Present();
	}
}

bool VDDisplayDriver3D::Paint(HDC hdc, const RECT& rClient, UpdateMode lastUpdateMode) {
	Refresh(lastUpdateMode);
	return true;
}

void VDDisplayDriver3D::PresentQueued() {
	if (mpSwapChain)
		mpSwapChain->PresentVSyncComplete();

	mbFramePending = false;
}

void VDDisplayDriver3D::QueuePresent() {
	if (mSource.mpCB)
		mSource.mpCB->QueuePresent();
}

bool VDDisplayDriver3D::CreateSwapChain() {
	if (mpSwapChain)
		return true;

	VDTSwapChainDesc swapDesc = {};
	swapDesc.mWidth = mbFullScreen ? mFullScreenWidth : mClientRect.right;
	swapDesc.mHeight = mbFullScreen ? mFullScreenHeight : mClientRect.bottom;
	swapDesc.mhWindow = mhwnd;
	swapDesc.mbWindowed = !mbFullScreen;
	swapDesc.mbSRGB = mbRenderLinear;
	swapDesc.mbHDR = mbHDR;
	swapDesc.mRefreshRateNumerator = mFullScreenRefreshRate;
	swapDesc.mRefreshRateDenominator = mFullScreenRefreshRate ? 1 : 0;

	if (!mpContext->CreateSwapChain(swapDesc, &mpSwapChain)) {
		VDDispLogF("Swap chain creation FAILED. Parameters: %ux%u, sRGB=%d, HDR=%d", swapDesc.mWidth, swapDesc.mHeight, swapDesc.mbSRGB, swapDesc.mbHDR);
		return false;
	}

	return true;
}

bool VDDisplayDriver3D::CreateImageNode() {
	if (mpImageNode || mpImageSourceNode)
		return true;

	// try to create fast path with direct texture
	mpImageSourceNode = new VDDisplayImageSourceNode3D;
	mpImageSourceNode->AddRef();

	if (mpImageSourceNode->Init(*mpContext, mDisplayNodeContext, mSource.pixmap.w, mSource.pixmap.h, mSource.pixmap.format))
		return true;

	// fast path not possible, use blit path
	vdsaferelease <<= mpImageSourceNode;

	mpImageNode = new VDDisplayImageNode3D;
	mpImageNode->AddRef();

	mpImageNode->SetDestArea(0, 0, mSource.pixmap.w, mSource.pixmap.h);
	if (!mpImageNode->Init(*mpContext, mDisplayNodeContext, mSource.pixmap.w, mSource.pixmap.h, mSource.pixmap.format)) {
		DestroyImageNode();
		return false;
	}

	return true;
}

void VDDisplayDriver3D::DestroyImageNode() {
	vdsaferelease <<= mpRootNode, mpImageNode, mpImageSourceNode;
}

bool VDDisplayDriver3D::BufferNode(VDDisplayNode3D *srcNode, uint32 w, uint32 h, bool hdr, VDDisplaySourceNode3D **ppNode) {
	vdrefptr<VDDisplayBufferSourceNode3D> bufferNode { new VDDisplayBufferSourceNode3D };

	if (!bufferNode->Init(*mpContext, mDisplayNodeContext, w, h, hdr, srcNode))
		return false;

	*ppNode = bufferNode.release();
	return true;
}

bool VDDisplayDriver3D::RebuildTree() {
	vdsaferelease <<= mpRootNode;

	if (!mpImageNode && !mpImageSourceNode && !CreateImageNode())
		return false;

	sint32 dstx = 0;
	sint32 dsty = 0;
	uint32 dstw = mClientRect.right;
	uint32 dsth = mClientRect.bottom;

	bool useClear = false;
	if (mbDestRectEnabled && (mDestRect.left != 0 || mDestRect.top != 0 || mDestRect.right != dstw || mDestRect.bottom != dsth)) {
		dstx = mDestRect.left;
		dsty = mDestRect.top;
		dstw = mDestRect.width();
		dsth = mDestRect.height();

		useClear = true;
	}

	if (dstw && dsth) {
		sint32 dstx2 = dstx;
		sint32 dsty2 = dsty;

		bool useBloom = mbUseScreenFX && mScreenFXInfo.mBloomIndirectIntensity > 0;
		if (useBloom) {
			dstx2 = 0;
			dsty2 = 0;
		}

		vdrefptr<VDDisplayNode3D> imgNode(mpImageNode);
		vdrefptr<VDDisplaySourceNode3D> imgSrcNode(mpImageSourceNode);

		if (mbUseScreenFX) {
			if (mScreenFXInfo.mPALBlendingOffset != 0) {
				if (!imgSrcNode) {
					if (!BufferNode(imgNode, mSource.pixmap.w, mSource.pixmap.h, false, ~imgSrcNode))
						return false;
				}

				// If the color correction logic is enabled, we can output extended color to avoid clamping
				// highly saturated colors. However, this is only enabled if the color correction matrix
				// is being used or we're rendering HDR (which forces CC on). If CC is off, then there's no
				// point in producing extended colors here.
				vdrefptr<VDDisplayArtifactingNode3D> p(new VDDisplayArtifactingNode3D);
				if (!p->Init(*mpContext, mDisplayNodeContext, mScreenFXInfo.mPALBlendingOffset, mScreenFXInfo.mColorCorrectionMatrix[0][0] != 0 || mbHDR, imgSrcNode))
					return false;

				imgNode = p;
				imgSrcNode = nullptr;
			} else if (mbHDR) {
				if (!imgSrcNode) {
					if (!BufferNode(imgNode, mSource.pixmap.w, mSource.pixmap.h, false, ~imgSrcNode))
						return false;
				}
			}
		}

		switch(mFilterMode) {
			case kFilterBicubic:
				if (!mbHDR) {
					if (!imgSrcNode) {
						if (!BufferNode(imgNode, mSource.pixmap.w, mSource.pixmap.h, false, ~imgSrcNode))
							return false;
					}

					VDDisplayStretchBicubicNode3D *p = new VDDisplayStretchBicubicNode3D;
					p->AddRef();
					if (p->Init(*mpContext, mDisplayNodeContext, mSource.pixmap.w, mSource.pixmap.h, dstx2, dsty2, dstw, dsth, imgSrcNode)) {
						mpRootNode = p;
						break;
					}
					p->Release();
				}
				// fall through

			case kFilterBilinear:
			case kFilterPoint:
			default:
				{
					// check if we can take the fast path of having the image node paint directly to the screen
					if (imgNode && imgNode == mpImageNode && mpImageNode->CanStretch() && !mbUseScreenFX) {
						mpImageNode->SetBilinear(mFilterMode != kFilterPoint);
						mpImageNode->SetDestArea(dstx2, dsty2, dstw, dsth);
						mpRootNode = mpImageNode;
						mpRootNode->AddRef();
						break;
					}
				
					// if we don't already have a buffered source node, explicitly buffer the incoming
					// image
					if (!imgSrcNode) {
						if (!BufferNode(imgNode, mSource.pixmap.w, mSource.pixmap.h, false, ~imgSrcNode))
							return false;
					}

					if (mbUseScreenFX) {
						vdrefptr<VDDisplayScreenFXNode3D> p(new VDDisplayScreenFXNode3D);

						VDDisplayScreenFXNode3D::Params params {};

						params.mDstX = dstx2;
						params.mDstY = dsty2;
						params.mDstW = dstw;
						params.mDstH = dsth;
						params.mSharpnessX = mPixelSharpnessX;
						params.mSharpnessY = mPixelSharpnessY;
						params.mbLinear = mFilterMode != kFilterPoint;
						params.mbRenderLinear = mbHDR;
						params.mbSignedInput = mScreenFXInfo.mbSignedRGBEncoding;
						params.mHDRScale = mbHDR ? mScreenFXInfo.mHDRIntensity : 1.0f;
						params.mGamma = mScreenFXInfo.mGamma;
						params.mScanlineIntensity = mScreenFXInfo.mScanlineIntensity;
						params.mDistortionX = mScreenFXInfo.mDistortionX;
						params.mDistortionYRatio = mScreenFXInfo.mDistortionYRatio;
						memcpy(params.mColorCorrectionMatrix, mScreenFXInfo.mColorCorrectionMatrix, sizeof params.mColorCorrectionMatrix);

						if (!p->Init(*mpContext, mDisplayNodeContext, params, imgSrcNode))
							return false;

						mpRootNode = p.release();
					} else {
						VDDisplayBlitNode3D *p = new VDDisplayBlitNode3D;
						p->AddRef();
						p->SetDestArea(dstx2, dsty2, dstw, dsth);

						if (!p->Init(*mpContext, mDisplayNodeContext, mSource.pixmap.w, mSource.pixmap.h, mFilterMode != kFilterPoint, mPixelSharpnessX, mPixelSharpnessY, imgSrcNode)) {
							p->Release();
							return false;
						}

						mpRootNode = p;
					}
				}
				break;
		}

		if (useBloom) {
			vdrefptr<VDDisplaySourceNode3D> bufferedRootNode;
			if (!BufferNode(mpRootNode, dstw, dsth, mbHDR, ~bufferedRootNode))
				return false;

			vdrefptr<VDDisplayBloomNode3D> bloomNode(new VDDisplayBloomNode3D);

			VDDisplayBloomNode3D::Params params {};

			params.mDstX = dstx;
			params.mDstY = dsty;
			params.mDstW = dstw;
			params.mDstH = dsth;
			params.mThreshold = mScreenFXInfo.mBloomThreshold;
			params.mDirectIntensity = mScreenFXInfo.mBloomDirectIntensity;
			params.mIndirectIntensity = mScreenFXInfo.mBloomIndirectIntensity;

			// The blur radius is specified in source pixels in the FXInfo, which must be
			// converted to destination pixels.
			params.mBlurRadius = mScreenFXInfo.mBloomRadius * (float)dstw / (float)mSource.pixmap.w;

			params.mbRenderLinear = mbHDR;

			if (!bloomNode->Init(*mpContext, mDisplayNodeContext, params, bufferedRootNode))
				return false;

			vdsaferelease <<= mpRootNode;
			mpRootNode = bloomNode.release();
		}
	}

	if (useClear) {
		vdrefptr<VDDisplaySequenceNode3D> seq(new VDDisplaySequenceNode3D);
		vdrefptr<VDDisplayClearNode3D> clr(new VDDisplayClearNode3D);
		clr->SetClearColor(mBackgroundColor);

		seq->AddNode(clr);

		if (mpRootNode) {
			seq->AddNode(mpRootNode);
			mpRootNode->Release();
		}

		mpRootNode = seq.release();
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////

IVDVideoDisplayMinidriver *VDCreateDisplayDriver3D() {
	return new VDDisplayDriver3D;
}
