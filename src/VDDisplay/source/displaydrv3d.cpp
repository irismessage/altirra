#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/vdstl.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Tessa/Context.h>
#include <vd2/VDDisplay/compositor.h>
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

//	if (!VDTCreateContextD3D9(640, 480, 0, false, false, hwnd, &mpContext))
//	if (!VDTCreateContextD3D9(&mpContext))
	if (!VDTCreateContextD3D11(&mpContext))
//	if (!VDTCreateContextD3D11(640, 480, 0, false, false, hwnd, &mpContext))
		return false;

	if (!mDisplayNodeContext.Init(*mpContext)) {
		Shutdown();
		return false;
	}

	if (!mRenderer.Init(*mpContext)) {
		Shutdown();
		return false;
	}

	mSource = info;
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
		info.pixmap.format != mSource.pixmap.format)
	{
		rebuildTree = true;
	}

	mSource = info;

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

void VDDisplayDriver3D::SetFullScreen(bool fullscreen, uint32 w, uint32 h, uint32 refresh) {
	if (mbFullScreen != fullscreen) {
		mbFullScreen = fullscreen;
		mFullScreenWidth = w;
		mFullScreenHeight = h;
		mFullScreenRefreshRate = refresh;

		vdsaferelease <<= mpSwapChain;

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

bool VDDisplayDriver3D::IsValid() {
	return true;
}

bool VDDisplayDriver3D::IsFramePending() {
	return mbFramePending;
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

	if (mbCompositionTreeDirty) {
		if (!RebuildTree())
			return;

		mbCompositionTreeDirty = false;
	}

	VDTSwapChainDesc swapDesc;
	if (mpSwapChain) {
		mpSwapChain->GetDesc(swapDesc);

		if (swapDesc.mWidth != w || swapDesc.mHeight != h) {
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

	mpContext->SetRenderTarget(0, surface);

	VDTViewport vp;
	vp.mX = 0;
	vp.mY = 0;
	vp.mWidth = w;
	vp.mHeight = h;
	vp.mMinZ = 0.0f;
	vp.mMaxZ = 1.0f;
	mpContext->SetViewport(vp);
	mpRootNode->Draw(*mpContext, mDisplayNodeContext);

	if (mpCompositor) {
		VDDisplayCompositeInfo info;
		info.mWidth = w;
		info.mHeight = h;

		mRenderer.Begin(w, h);
		mpCompositor->Composite(mRenderer, info);
		mRenderer.End();
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
	swapDesc.mRefreshRateNumerator = mFullScreenRefreshRate;
	swapDesc.mRefreshRateDenominator = mFullScreenRefreshRate ? 1 : 0;

	if (!mpContext->CreateSwapChain(swapDesc, &mpSwapChain))
		return false;

	return true;
}

bool VDDisplayDriver3D::CreateImageNode() {
	if (mpImageNode || mpImageSourceNode)
		return true;

	mpImageSourceNode = new VDDisplayImageSourceNode3D;
	mpImageSourceNode->AddRef();

	if (mpImageSourceNode->Init(*mpContext, mDisplayNodeContext, mSource.pixmap.w, mSource.pixmap.h, mSource.pixmap.format))
		return true;

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
		switch(mFilterMode) {
			case kFilterBicubic:
				{
					vdrefptr<VDDisplaySourceNode3D> src(mpImageSourceNode);

					if (!src) {
						VDDisplayBufferSourceNode3D *bufferNode = new VDDisplayBufferSourceNode3D;
						bufferNode->AddRef();
						if (!bufferNode->Init(*mpContext, mDisplayNodeContext, mSource.pixmap.w, mSource.pixmap.h, mpImageNode)) {
							bufferNode->Release();
							return false;
						}

						src.set(bufferNode);
					}

					VDDisplayStretchBicubicNode3D *p = new VDDisplayStretchBicubicNode3D;
					p->AddRef();
					if (p->Init(*mpContext, mDisplayNodeContext, mSource.pixmap.w, mSource.pixmap.h, dstx, dsty, dstw, dsth, src)) {
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
					vdrefptr<VDDisplaySourceNode3D> src(mpImageSourceNode);

					if (mpImageNode) {
						if (mpImageNode->CanStretch()) {
							mpImageNode->SetBilinear(mFilterMode != kFilterPoint);
							mpImageNode->SetDestArea(dstx, dsty, dstw, dsth);
							mpRootNode = mpImageNode;
							mpRootNode->AddRef();
							break;
						}
					}
				
					if (!src) {
						VDDisplayBufferSourceNode3D *bufferNode = new VDDisplayBufferSourceNode3D;
						bufferNode->AddRef();
						if (!bufferNode->Init(*mpContext, mDisplayNodeContext, mSource.pixmap.w, mSource.pixmap.h, mpImageNode)) {
							bufferNode->Release();
							return false;
						}

						src.set(bufferNode);
					}

					VDDisplayBlitNode3D *p = new VDDisplayBlitNode3D;
					p->AddRef();
					p->SetDestArea(dstx, dsty, dstw, dsth);

					if (!p->Init(*mpContext, mDisplayNodeContext, mSource.pixmap.w, mSource.pixmap.h, mFilterMode != kFilterPoint, mPixelSharpnessX, mPixelSharpnessY, src)) {
						p->Release();
						return false;
					}

					mpRootNode = p;
				}
				break;
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
