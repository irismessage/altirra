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
#include <numeric>
#include <vd2/system/binary.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/time.h>
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

	if (!VDTCreateContextD3D11(&mpContext))
		return false;

	const auto& caps = mpContext->GetDeviceCaps();
	VDDispLogF("Direct3D display driver initialized using adapter: %ls", caps.mDeviceDescription.c_str());
	VDDispLogF("Caps: maxtex %dx%d (%s), minprec(%s)"
		, caps.mMaxTextureWidth
		, caps.mMaxTextureHeight
		, caps.mbNonPow2 ? "nonpow2" : caps.mbNonPow2Conditional ? "nonpow2-cond" : "pow2"
		, caps.mbMinPrecisionPS ? caps.mbMinPrecisionNonPS ? "all" : "ps" : caps.mbMinPrecisionNonPS ? "non-ps" : "none"
	);

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
		mpContext,
		mpDebugFont;

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

void VDDisplayDriver3D::SetDesiredCustomRefreshRate(float hz, float hzmin, float hzmax) {
	mCustomRefreshRate = hz;
	mCustomRefreshRateMin = hzmin;
	mCustomRefreshRateMax = hzmax;

	ApplyCustomRefreshRate();
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
		return systemSupport ? VDDHDRAvailability::NotEnabledOnDisplay : VDDHDRAvailability::NoSystemSupport;

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

	if (mpCompositor || mbDisplayDebugInfo) {

		mRenderer.Begin(w, h, mDisplayNodeContext, mbHDR);

		if (mpCompositor) {
			VDTAutoScope scope(*mpContext, "Compositor");
			mpCompositor->Composite(mRenderer, compInfo);
		}

		if (mbDisplayDebugInfo) {
			VDDisplayTextRenderer *tr = mRenderer.GetTextRenderer();

			tr->Begin();

			if (!mpDebugFont)
				VDCreateDisplaySystemFont(20, false, "Arial", &mpDebugFont);

			tr->SetFont(mpDebugFont);
			tr->SetAlignment(tr->kAlignLeft, tr->kVertAlignTop);
			tr->SetColorRGB(0xFFFFFF);

			VDStringW s;
			s.sprintf(L"Direct3D 11 - %ls", mpContext->GetDeviceCaps().mDeviceDescription.c_str());
			tr->DrawTextLine(10, 10, s.c_str());

			VDTSwapChainDesc desc;
			mpSwapChain->GetDesc(desc);
			s.sprintf(L"Swap chain: %dx%d %hs%hs", desc.mWidth, desc.mHeight, desc.mbWindowed ? "windowed" : "exclusive fullscreen", mbHDR ? " (HDR)" : "");

			switch(mpSwapChain->GetLastCompositionStatus()) {
				case VDTSwapChainCompositionStatus::Unknown:
					break;

				case VDTSwapChainCompositionStatus::ComposedCopy:
					s += L" [Composed: Copy]";
					break;

				case VDTSwapChainCompositionStatus::ComposedFlip:
					s += L" [Composed: Flip]";
					break;

				case VDTSwapChainCompositionStatus::Overlay:
					s += L" [Hardware: Independent Flip]";
					break;
			}

			tr->DrawTextLine(10, 30, s.c_str());
			
			float frameTime = 0;

			if (mTimingLogLength >= 5) {
				const auto& tail = mTimingLog[(mTimingIndex + std::size(mTimingLog) - mTimingLogLength) % std::size(mTimingLog)];
				const auto& head = mTimingLog[(mTimingIndex + std::size(mTimingLog) - 1) % std::size(mTimingLog)];

				if (head.mSyncCount && tail.mSyncCount && head.mSyncCount != tail.mSyncCount) {
					frameTime = (double)(head.mSyncTick - tail.mSyncTick) * VDGetPreciseSecondsPerTick() / (double)(head.mSyncCount - tail.mSyncCount);
				}
			}

			int y = 70;

			s.sprintf(L"VSync offset: [%5.2f, %5.2f, %5.2f]ms  %s(%5.4f ms/frame | %7.4f Hz)"
				, mTimingMin.mVSyncOffset * 1000.0f
				, mTimingAverage.mVSyncOffset * 1000.0f
				, mTimingMax.mVSyncOffset * 1000.0f
				, mbFrameVSyncAdaptive ? L"adaptive " : L""
				, frameTime * 1000.0f
				, frameTime != 0 ? 1.0f / frameTime : 0
			);
			tr->DrawTextLine(10, y, s.c_str());
			y += 20;

			s.sprintf(L"Present wait time: [%5.2f, %5.2f, %5.2f]ms"
				, mTimingMin.mPresentWaitTime * 1000.0f
				, mTimingAverage.mPresentWaitTime * 1000.0f
				, mTimingMax.mPresentWaitTime * 1000.0f
			);
			tr->DrawTextLine(10, y, s.c_str());
			y += 20;

			s.sprintf(L"Present latency: [%5.2f, %5.2f, %5.2f]ms"
				, mTimingMin.mLastPresentDelay * 1000.0f
				, mTimingAverage.mLastPresentDelay * 1000.0f
				, mTimingMax.mLastPresentDelay * 1000.0f
			);

			tr->DrawTextLine(10, y, s.c_str());
			y += 20;

			s.sprintf(L"Last present call time: [%5.2f, %5.2f, %5.2f]ms"
				, mTimingMin.mLastPresentCallTime * 1000.0f
				, mTimingAverage.mLastPresentCallTime * 1000.0f
				, mTimingMax.mLastPresentCallTime * 1000.0f
			);
			tr->DrawTextLine(10, y, s.c_str());
			y += 20;

			s.sprintf(L"Last present frames queued: [%4.1f, %4.1f, %4.1f]"
				, mTimingMin.mLastPresentFramesQueued
				, mTimingAverage.mLastPresentFramesQueued
				, mTimingMax.mLastPresentFramesQueued
			);
			tr->DrawTextLine(10, y, s.c_str());
			y += 20;

			const float ecRefresh = mpSwapChain->GetEffectiveCustomRefreshRate();
			if (ecRefresh > 0) {
				s.sprintf(L"Custom refresh rate requested: %.2f Hz", ecRefresh);
				tr->DrawTextLine(10, y, s.c_str());
			} else if (mCustomRefreshRate > 0) {
				tr->DrawTextLine(10, y, L"Custom refresh rate: Not available");
			}
			y += 20;

			tr->End();
		}

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

	mbFrameVSync = false;

	if (updateMode & kModeVSync) {
		mbFramePending = true;
		mbFrameVSync = true;
		mbFrameVSyncAdaptive = (updateMode & kModeVSyncAdaptive) != 0;

		mpSwapChain->PresentVSync(mhMonitor, mbFrameVSyncAdaptive);
	} else {
		mVSyncStatus = {};

		mbFramePending = false;
		mpSwapChain->Present();

		mTimingLog[mTimingIndex] = TimingEntry{};
		AdvanceTimingLog();
	}
}

bool VDDisplayDriver3D::Paint(HDC hdc, const RECT& rClient, UpdateMode lastUpdateMode) {
	Refresh(lastUpdateMode);
	return true;
}

void VDDisplayDriver3D::PresentQueued() {
	if (mpSwapChain) {
		if (!mpSwapChain->PresentVSyncComplete())
			return;
	}
}

void VDDisplayDriver3D::QueuePresent(bool restarted) {
	if (mSource.mpCB)
		mSource.mpCB->QueuePresent();
}

void VDDisplayDriver3D::OnPresentCompleted(const VDTAsyncPresentStatus& status) {
	mTimingLog[mTimingIndex] = TimingEntry {
		.mVSyncOffset = status.mVSyncOffset,
		.mPresentWaitTime = status.mPresentWaitTime,
		.mLastPresentDelay = status.mLastPresentDelay,
		.mLastPresentCallTime = status.mLastPresentCallTime,
		.mLastPresentFramesQueued = (float)status.mLastPresentFramesQueued,
		.mSyncTick = status.mSyncTick,
		.mSyncCount = status.mSyncCount
	};
	
	AdvanceTimingLog();

	mbFramePending = false;

	const auto& tail = mTimingLog[(mTimingIndex + std::size(mTimingLog) - mTimingLogLength) % std::size(mTimingLog)];
	const auto& head = mTimingLog[(mTimingIndex + std::size(mTimingLog) - 1) % std::size(mTimingLog)];

	mVSyncStatus.mOffset = status.mVSyncOffset;
	mVSyncStatus.mPresentQueueTime = status.mPresentQueueTime;

	if (head.mSyncCount && tail.mSyncCount && head.mSyncCount != tail.mSyncCount && head.mSyncTick != tail.mSyncTick) {
		mVSyncStatus.mRefreshRate = (float)((double)(sint32)(head.mSyncCount - tail.mSyncCount) / ((double)(sint64)(head.mSyncTick - tail.mSyncTick) * VDGetPreciseSecondsPerTick()));
	} else if (status.mRefreshRate > 0)
		mVSyncStatus.mRefreshRate = status.mRefreshRate;
	else
		mVSyncStatus.mRefreshRate = -1.0f;
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

	mpSwapChain->SetPresentCallback(this);

	ApplyCustomRefreshRate();

	// clear timing history
	std::fill(std::begin(mTimingLog), std::end(mTimingLog), TimingEntry{});
	mTimingIndex = 0;
	mTimingLogLength = 0;

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

void VDDisplayDriver3D::AdvanceTimingLog() {
	if (mTimingLogLength < std::size(mTimingLog) && mTimingLog[mTimingIndex].mSyncCount)
		++mTimingLogLength;

	if (++mTimingIndex >= std::size(mTimingLog)) {
		mTimingIndex = 0;

		mTimingAverage = mTimingLog[0];
		mTimingMin = mTimingLog[0];
		mTimingMax = mTimingLog[0];

		for(int i=1; i<std::ssize(mTimingLog); ++i) {
			mTimingAverage += mTimingLog[i];
			mTimingMin.AccumulateMin(mTimingLog[i]);
			mTimingMax.AccumulateMax(mTimingLog[i]);
		}

		mTimingAverage *= 1.0f / (float)std::ssize(mTimingLog);
	}
}

void VDDisplayDriver3D::ApplyCustomRefreshRate() {
	if (mpSwapChain)
		mpSwapChain->SetCustomRefreshRate(mCustomRefreshRate, mCustomRefreshRateMin, mCustomRefreshRateMax);
}

///////////////////////////////////////////////////////////////////////////

IVDVideoDisplayMinidriver *VDCreateDisplayDriver3D() {
	return new VDDisplayDriver3D;
}
