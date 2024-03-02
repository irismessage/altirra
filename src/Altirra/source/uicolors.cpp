//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
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
#include <windows.h>
#include <vd2/system/binary.h>
#include <vd2/system/color.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/math.h>
#include <vd2/system/strutil.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/services.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/resample.h>
#include <at/atcore/snapshotimpl.h>
#include <at/atnativeui/canvas_win32.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/messagedispatcher.h>
#include <at/atnativeui/messageloop.h>
#include <at/atnativeui/uinativewindow.h>
#include "resource.h"
#include "gtia.h"
#include "oshelper.h"
#include "savestateio.h"
#include "simulator.h"
#include "uiaccessors.h"
#include "common_png.h"
#include "decode_png.h"
#include "palettesolver.h"

extern ATSimulator g_sim;

///////////////////////////////////////////////////////////////////////////

class ATSaveStateColorParameters final : public ATSnapExchangeObject<ATSaveStateColorParameters> {
public:
	ATSaveStateColorParameters();
	ATSaveStateColorParameters(const ATNamedColorParams& params);

	template<typename T>
	void Exchange(T& rw) {
		rw.Transfer("profile_name", &mParams.mPresetTag);

		rw.Transfer("hue_start", &mParams.mHueStart);
		rw.Transfer("hue_range", &mParams.mHueRange);
		rw.Transfer("brightness", &mParams.mBrightness);
		rw.Transfer("contrast", &mParams.mContrast);
		rw.Transfer("saturation", &mParams.mSaturation);
		rw.Transfer("gamma", &mParams.mGammaCorrect);
		rw.Transfer("intensity_scale", &mParams.mIntensityScale);
		rw.Transfer("artifacting_hue", &mParams.mArtifactHue);	
		rw.Transfer("artifacting_saturation", &mParams.mArtifactSat);
		rw.Transfer("artifacting_sharpness", &mParams.mArtifactSharpness);
		rw.Transfer("matrix_red_shift", &mParams.mRedShift);
		rw.Transfer("matrix_red_scale", &mParams.mRedScale);
		rw.Transfer("matrix_green_shift", &mParams.mGrnShift);
		rw.Transfer("matrix_green_scale", &mParams.mGrnScale);
		rw.Transfer("matrix_blue_shift", &mParams.mBluShift);
		rw.Transfer("matrix_blue_scale", &mParams.mBluScale);
		rw.Transfer("use_pal_quirks", &mParams.mbUsePALQuirks);
		rw.TransferEnum("luma_ramp", &mParams.mLumaRampMode);
		rw.TransferEnum("color_correction", &mParams.mColorMatchingMode);
	}

	ATNamedColorParams mParams;
};

ATSERIALIZATION_DEFINE(ATSaveStateColorParameters);

ATSaveStateColorParameters::ATSaveStateColorParameters() {
	mParams.mPresetTag = ATGetColorPresetTagByIndex(0);
	static_cast<ATColorParams&>(mParams) = ATGetColorPresetByIndex(0);
}

ATSaveStateColorParameters::ATSaveStateColorParameters(const ATNamedColorParams& params) {
	mParams = params;
}

class ATSaveStateColorSettings final : public ATSnapExchangeObject<ATSaveStateColorSettings> {
public:
	ATSaveStateColorSettings() = default;
	ATSaveStateColorSettings(const ATColorSettings& settings);

	template<typename T>
	void Exchange(T& rw) {
		rw.Transfer("ntsc_params", &mpNTSCParams);
		rw.Transfer("pal_params", &mpPALParams);
	}

	vdrefptr<ATSaveStateColorParameters> mpNTSCParams;
	vdrefptr<ATSaveStateColorParameters> mpPALParams;
};

ATSERIALIZATION_DEFINE(ATSaveStateColorSettings);

ATSaveStateColorSettings::ATSaveStateColorSettings(const ATColorSettings& settings) {
	mpNTSCParams = new ATSaveStateColorParameters(settings.mNTSCParams);

	if (settings.mbUsePALParams)
		mpPALParams = new ATSaveStateColorParameters(settings.mPALParams);
}

///////////////////////////////////////////////////////////////////////////

class ATUIColorReferenceControl final : public ATUINativeWindow {
public:
	ATUIColorReferenceControl();

	void UpdateFromPalette(const uint32 *palette);

private:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) override;

	void OnSetFont(HFONT hfont, bool redraw);
	void UpdateMetrics();
	void UpdateScrollBar();
	void OnVScroll(int code);
	void OnMouseWheel(float delta);
	void OnPaint();

	HFONT mhfont = nullptr;
	float mScrollAccum = 0;
	sint32 mScrollY = 0;
	sint32 mScrollMax = 0;
	sint32 mRowHeight = 1;
	sint32 mTextOffsetX = 0;
	sint32 mTextOffsetY = 0;
	sint32 mWidth = 0;
	sint32 mHeight = 0;

	struct ColorEntry {
		VDStringW mLabel;
		uint8 mPaletteIndex;
		uint32 mBgColor;
		uint32 mFgColor;
	};

	vdvector<ColorEntry> mColors;
};

ATUIColorReferenceControl::ATUIColorReferenceControl() {
	mColors.emplace_back(ColorEntry { VDStringW(L"$94: GR.0 background"), 0x94 });
	mColors.emplace_back(ColorEntry { VDStringW(L"$9A: GR.0 foreground"), 0x9A });
	mColors.emplace_back(ColorEntry { VDStringW(L"$72: Ballblazer sky"), 0x72 });
	mColors.emplace_back(ColorEntry { VDStringW(L"$96: Pitfall sky"), 0x96 });
	mColors.emplace_back(ColorEntry { VDStringW(L"$86: Pitfall II sky"), 0x86 });
	mColors.emplace_back(ColorEntry { VDStringW(L"$A0: Star Raiders shields"), 0xA0 });
	mColors.emplace_back(ColorEntry { VDStringW(L"$90: Star Raiders galactic map"), 0x90 });
	mColors.emplace_back(ColorEntry { VDStringW(L"$96: Star Raiders map BG"), 0x96 });
	mColors.emplace_back(ColorEntry { VDStringW(L"$B8: Star Raiders map FG"), 0xB8 });
	mColors.emplace_back(ColorEntry { VDStringW(L"$AA: Pole Position sky"), 0xAA });
	mColors.emplace_back(ColorEntry { VDStringW(L"$D8: Pole Position grass"), 0xD8 });

	for(ColorEntry& ce : mColors)
		ce.mBgColor = ~0U;
}

void ATUIColorReferenceControl::UpdateFromPalette(const uint32 *palette) {
	bool redraw = false;

	for(ColorEntry& ce : mColors) {
		const uint32 c = palette[ce.mPaletteIndex] & 0xFFFFFF;

		if (ce.mBgColor != c) {
			ce.mBgColor = c;
			redraw = true;

			const uint32 luma = (c & 0xFF00FF) * ((19 << 16) + 54) + (c & 0xFF00) * (183 << 8);
			ce.mFgColor = (luma >= UINT32_C(0x80000000)) ? 0 : 0xFFFFFF;
		}
	}

	if (redraw)
		InvalidateRect(mhwnd, nullptr, true);
}

LRESULT ATUIColorReferenceControl::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			{
				RECT r {};
				GetClientRect(mhwnd, &r);

				mWidth = r.right;
				mHeight = r.bottom;
			}
			OnSetFont(nullptr, false);
			break;

		case WM_SIZE:
			mWidth = LOWORD(lParam);
			mHeight = HIWORD(lParam);
			UpdateScrollBar();
			break;

		case WM_MOUSEWHEEL:
			OnMouseWheel((float)(sint16)HIWORD(wParam) / (float)WHEEL_DELTA);
			return 0;

		case WM_PAINT:
			OnPaint();
			return 0;

		case WM_ERASEBKGND:
			return 0;

		case WM_SETFONT:
			OnSetFont((HFONT)wParam, LOWORD(lParam) != 0);
			return 0;

		case WM_VSCROLL:
			OnVScroll(LOWORD(wParam));
			return 0;
	}

	return ATUINativeWindow::WndProc(msg, wParam, lParam);
}

void ATUIColorReferenceControl::OnSetFont(HFONT hfont, bool redraw) {
	if (!hfont)
		hfont = (HFONT)GetStockObject(SYSTEM_FONT);

	mhfont = hfont;

	UpdateMetrics();

	if (redraw)
		InvalidateRect(mhwnd, nullptr, true);
}

void ATUIColorReferenceControl::UpdateScrollBar() {
	SCROLLINFO si {};

	si.cbSize = sizeof(SCROLLINFO);
	si.nMin = 0;
	si.nMax = (sint32)(mRowHeight * mColors.size());
	si.nPage = mHeight;
	si.nPos = mScrollY;
	si.fMask = SIF_RANGE | SIF_POS | SIF_PAGE;

	mScrollMax = std::max<sint32>(0, si.nMax - mHeight);

	if (si.nMax <= (sint32)si.nPage) {
		ShowScrollBar(mhwnd, SB_VERT, false);

		if (mScrollY) {
			sint32 oldScrollY = mScrollY;
			mScrollY = 0;

			ScrollWindow(mhwnd, 0, oldScrollY, nullptr, nullptr);
		}
	} else
		ShowScrollBar(mhwnd, SB_VERT, true);

	SetScrollInfo(mhwnd, SB_VERT, &si, true);
}

void ATUIColorReferenceControl::UpdateMetrics() {
	mRowHeight = 1;
	mTextOffsetX = 0;
	mTextOffsetY = 0;

	if (HDC hdc = GetDC(mhwnd)) {
		if (HGDIOBJ hOldFont = SelectObject(hdc, mhfont)) {
			TEXTMETRICW tm {};

			if (GetTextMetrics(hdc, &tm)) {
				int margin = std::max<int>(2, tm.tmHeight / 5);

				mRowHeight = tm.tmAscent - tm.tmInternalLeading + 2*std::max<int>(tm.tmInternalLeading, tm.tmDescent) + 2*margin;
				mTextOffsetX = margin;
				mTextOffsetY = (mRowHeight - tm.tmAscent) / 2;
			}

			SelectObject(hdc, hOldFont);
		}

		ReleaseDC(mhwnd, hdc);
	}

	UpdateScrollBar();
}

void ATUIColorReferenceControl::OnVScroll(int code) {
	SCROLLINFO si {};
	si.cbSize = sizeof(SCROLLINFO);
	si.fMask = SIF_TRACKPOS | SIF_PAGE | SIF_RANGE | SIF_POS;

	if (!GetScrollInfo(mhwnd, SB_VERT, &si))
		return;

	si.cbSize = sizeof(SCROLLINFO);
	si.fMask = SIF_POS;

	switch(code) {
		case SB_TOP:
			si.nPos = 0;
			break;

		case SB_BOTTOM:
			si.nPos = mScrollMax;
			break;

		case SB_LINEUP:
			si.nPos -= mRowHeight;
			break;

		case SB_LINEDOWN:
			si.nPos += mRowHeight;
			break;

		case SB_PAGEUP:
			si.nPos -= mHeight;
			break;

		case SB_PAGEDOWN:
			si.nPos += mHeight;
			break;

		case SB_THUMBPOSITION:
		case SB_THUMBTRACK:
			si.nPos = si.nTrackPos;
			break;
	}

	si.nPos = std::clamp<sint32>(si.nPos, 0, mScrollMax);

	SetScrollInfo(mhwnd, SB_VERT, &si, TRUE);

	sint32 delta = mScrollY - si.nPos;
	if (delta) {
		mScrollY = si.nPos;

		ScrollWindow(mhwnd, 0, delta, nullptr, nullptr);
	}
}

void ATUIColorReferenceControl::OnMouseWheel(float delta) {
	if (!delta)
		return;

	UINT linesPerNotch = 3;
	SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &linesPerNotch, 0);

	if (linesPerNotch == WHEEL_PAGESCROLL)
		linesPerNotch = mRowHeight ? mHeight / mRowHeight : 1;

	mScrollAccum += delta * (float)((sint32)linesPerNotch * mRowHeight);

	sint32 pixels = VDRoundToInt32(mScrollAccum);

	if (pixels) {
		mScrollAccum -= (float)pixels;

		sint32 newScrollY = std::clamp<sint32>(mScrollY - pixels, 0, mScrollMax);
		sint32 delta = mScrollY - newScrollY;

		if (delta) {
			mScrollY = newScrollY;

			ScrollWindow(mhwnd, 0, delta, nullptr, nullptr);
			UpdateScrollBar();
		}
	}
}

void ATUIColorReferenceControl::OnPaint() {
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(mhwnd, &ps);
	if (!hdc)
		return;

	int savedDC = SaveDC(hdc);
	if (savedDC) {
		SelectObject(hdc, mhfont);

		int rawRow1 = (ps.rcPaint.top + mScrollY) / mRowHeight;
		int rawRow2 = (ps.rcPaint.bottom + mScrollY + mRowHeight - 1) / mRowHeight;
		int row1 = std::max(rawRow1, 0);
		int row2 = std::min(rawRow2, (int)mColors.size());

		RECT rClip;
		rClip.left = 0;
		rClip.top = row1 * mRowHeight - mScrollY;
		rClip.right = mWidth;
		rClip.bottom = rClip.top + mRowHeight;

		SetTextAlign(hdc, TA_LEFT | TA_TOP);

		for(int row = row1; row < row2; ++row) {
			const ColorEntry& ce = mColors[row];

			SetBkColor(hdc, VDSwizzleU32(ce.mBgColor) >> 8);
			SetTextColor(hdc, VDSwizzleU32(ce.mFgColor) >> 8);

			ExtTextOutW(hdc, rClip.left + mTextOffsetX, rClip.top + mTextOffsetY, ETO_OPAQUE | ETO_CLIPPED, &rClip, ce.mLabel.c_str(), ce.mLabel.size(), nullptr);
			rClip.top += mRowHeight;
			rClip.bottom += mRowHeight;
		}

		if (rClip.top < ps.rcPaint.bottom) {
			rClip.bottom = ps.rcPaint.bottom;

			SetBkColor(hdc, 0);
			ExtTextOutW(hdc, rClip.left, rClip.top, ETO_OPAQUE | ETO_CLIPPED, &rClip, L"", 0, nullptr);
		}

		RestoreDC(hdc, savedDC);
	}

	EndPaint(mhwnd, &ps);
}

///////////////////////////////////////////////////////////////////////////

class ATUIColorImageReferenceControl final : public ATUINativeWindow, public ATUINativeMouseMessages, public VDAlignedObject<16> {
public:
	ATUIColorImageReferenceControl();

	void SetGain(float gain);

	void SetImage();
	void SetImage(const VDPixmap& px);

	void SetCornerPoints(const vdfloat2& tl, const vdfloat2& br);

	void GetSampledColors(uint32 *colors) const;

	void SetColorOverlayEnabled(bool enabled);
	void SetOverlayColors(const uint32 colors[256]);

private:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	void OnSize();
	void OnPaint();

	void OnMouseMove(sint32 x, sint32 y) override;
	void OnMouseDownL(sint32 x, sint32 y) override;
	void OnMouseUpL(sint32 x, sint32 y) override;
	void OnMouseWheel(sint32 x, sint32 y, float delta) override;
	void OnMouseLeave() override;

	int FindPoint(sint32 x, sint32 y) const;
	void RecomputeAdjustmentTable();
	void RecomputeAdjustedImage();
	void RecomputeImageArea(bool force);
	void RescaleImage();
	void RecomputePointPixelPositions();
	void RecomputeGrid();
	void SampleColors();

	ATUICanvasW32 mCanvas;
	VDPixmapBuffer mImage;

	float mGain = 1.0f;

	VDPixmapBuffer mAdjustedImage;
	VDPixmapBuffer mScaledImage;
	VDDisplayImageView mScaledImageView;

	bool mbDragging = false;
	int mDragPtIndex = -1;
	vdint2 mDragOffset;
	vdfloat2 mDragOffsetF;
	bool mbEnableColorOverlay = false;

	bool mbScaledImageValid = false;
	bool mbProjectionValid = false;
	int mZoomClicks = 0;
	float mWheelAccum = 0.0f;
	vdfloat2 mScrollAreaSize { 0.0f, 0.0f };
	vdfloat2 mScrollOffset { 0.0f, 0.0f };
	vdrect32 mDestArea { 0, 0, 0, 0 };
	vdrect32f mDestAreaF { 0.0f, 0.0f, 0.0f, 0.0f };
	vdfloat2 mPoints[4];
	vdint2 mPointPixelPos[4] {};

	vdfloat32x4 mRawSampledColors[256] {};
	uint32 mOverlaidColors[256];

	vdfloat2 mGridCenters[16][16];
	vdfloat2 mGridCorners[17][17];

	uint8 mAdjustmentTable[256];
};

ATUIColorImageReferenceControl::ATUIColorImageReferenceControl() {
	mPoints[0] = vdfloat2 { 0.1f, 0.1f };
	mPoints[1] = vdfloat2 { 0.9f, 0.1f };
	mPoints[2] = vdfloat2 { 0.1f, 0.9f };
	mPoints[3] = vdfloat2 { 0.9f, 0.9f };

	RecomputeGrid();
	RecomputeAdjustmentTable();
}

void ATUIColorImageReferenceControl::SetGain(float gain) {
	gain = std::clamp(gain, 0.5f, 2.0f);
	if (mGain != gain) {
		mGain = gain;

		RecomputeAdjustmentTable();
		RecomputeAdjustedImage();
		RecomputeImageArea(true);
	}
}

void ATUIColorImageReferenceControl::SetImage() {
	mAdjustedImage.clear();
	mImage.clear();
	mScaledImage.clear();
	mZoomClicks = 0;
	RecomputeAdjustedImage();
	RecomputeImageArea(true);
}

void ATUIColorImageReferenceControl::SetImage(const VDPixmap& px) {
	mZoomClicks = 0;
	mImage.init(px.w, px.h, nsVDPixmap::kPixFormat_XRGB8888);
	VDPixmapBlt(mImage, px);
	RecomputeAdjustedImage();
	RecomputeImageArea(true);
	SampleColors();
}

void ATUIColorImageReferenceControl::SetCornerPoints(const vdfloat2& tl, const vdfloat2& br) {
	mPoints[0] = tl;
	mPoints[1] = vdfloat2 { br.x, tl.y };
	mPoints[2] = vdfloat2 { tl.x, br.y };
	mPoints[3] = br;
	RecomputeImageArea(true);
	SampleColors();
}

void ATUIColorImageReferenceControl::GetSampledColors(uint32 *colors) const {
	for(int i=0; i<256; ++i)
		colors[i] = VDColorRGB(mRawSampledColors[i] * mGain).ToRGB8();
}

void ATUIColorImageReferenceControl::SetColorOverlayEnabled(bool enabled) {
	if (mbEnableColorOverlay != enabled) {
		mbEnableColorOverlay = enabled;

		Invalidate();
	}
}

void ATUIColorImageReferenceControl::SetOverlayColors(const uint32 colors[256]) {
	memcpy(mOverlaidColors, colors, sizeof mOverlaidColors);

	if (mbEnableColorOverlay)
		Invalidate();
}

LRESULT ATUIColorImageReferenceControl::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	auto [result, handled] = ATUIDispatchWndProcMessage<ATUINativeMouseMessages>(mhwnd, msg, wParam, lParam, *this);

	if (handled)
		return result;

	switch(msg) {
		case WM_NCCREATE:
			mCanvas.Init(mhwnd);
			break;

		case WM_PAINT:
			OnPaint();
			return 0;

		case WM_ERASEBKGND:
			return 0;

		case WM_SIZE:
			OnSize();
			break;
	}

	return ATUINativeWindow::WndProc(msg, wParam, lParam);
}

void ATUIColorImageReferenceControl::OnSize() {
	RecomputeImageArea(false);
}

void ATUIColorImageReferenceControl::OnPaint() {
	if (!mbScaledImageValid)
		RescaleImage();

	PAINTSTRUCT ps;
	IVDDisplayRenderer *r = mCanvas.Begin(ps, true);
	if (!r)
		return;

	r->SetColorRGB(0x181818);
	r->FillRect(0, 0, ps.rcPaint.right, ps.rcPaint.bottom);

	if (!mDestArea.empty()) {
		r->SetColorRGB(0xFFFFFF);
		r->Blt(mDestArea.left, mDestArea.top, mScaledImageView, 0, 0, mScaledImage.w, mScaledImage.h);

		const vdfloat2 scale { mDestAreaF.width(), mDestAreaF.height() };
		const vdfloat2 offset { mDestAreaF.left, mDestAreaF.top };

		if (mbProjectionValid) {
			for(int y=0; y<16; ++y) {
				bool bigY = (y == 0) || (y == 15);

				for(int x=0; x<16; ++x) {
					bool big = ((x == 0) || (x == 15)) && bigY;
					vdfloat2 pt = mGridCenters[y][x] * scale + offset;

					sint32 ptX = VDFloorToInt(pt.x);
					sint32 ptY = VDFloorToInt(pt.y);

					uint32 c = VDColorRGB(mRawSampledColors[y*16+x] * mGain).ToRGB8();

					if (mbEnableColorOverlay) {
						r->SetColorRGB(mOverlaidColors[y*16+x]);
						r->FillRect(ptX - 4, ptY - 4, 9, 9);
					} else if (big) {
						r->SetColorRGB(0x00C000);
						r->FillRect(ptX - 4, ptY - 4, 9, 9);
					} else if (mDragPtIndex >= 0) {
						r->SetColorRGB(0x00C000);
						r->FillRect(ptX - 2, ptY - 2, 5, 5);
					} else {
						r->SetColorRGB(c);
						r->FillRect(ptX - 2, ptY - 2, 5, 5);
					}
				}
			}
		} else {
			for(int y=0; y<16; y += 15) {
				for(int x=0; x<16; x += 15) {
					vdfloat2 pt = mGridCenters[y][x] * scale + offset;

					sint32 ptX = VDFloorToInt(pt.x);
					sint32 ptY = VDFloorToInt(pt.y);

					r->SetColorRGB(0xFF0000);
					r->FillRect(ptX - 4, ptY - 4, 9, 9);
				}
			}
		}
	}

	mCanvas.End(ps);
}

void ATUIColorImageReferenceControl::OnMouseMove(sint32 x, sint32 y) {
	if (!mbDragging)
		return;

	if (mDragPtIndex >= 0) {
		vdint2 newPos = vdint2{x, y} + mDragOffset;
		vdint2 clampedNewPos;

		clampedNewPos.x = std::clamp(newPos.x, VDFloorToInt(mDestAreaF.left + 0.5f), VDFloorToInt(mDestAreaF.right  + 0.5f));
		clampedNewPos.y = std::clamp(newPos.y, VDFloorToInt(mDestAreaF.top  + 0.5f), VDFloorToInt(mDestAreaF.bottom + 0.5f));

		if (mPointPixelPos[mDragPtIndex] != newPos) {
			mPointPixelPos[mDragPtIndex] = newPos;

			vdfloat2 rawPt{vdfloat2{(float)(newPos.x + 0.5f) - mDestAreaF.left, (float)(newPos.y + 0.5f) - mDestAreaF.top} / vdfloat2{std::max(1.0f, mDestAreaF.width()), std::max(1.0f, mDestAreaF.height())}};
			mPoints[mDragPtIndex] = nsVDMath::max(vdfloat2{0.0f, 0.0f}, nsVDMath::min(rawPt, vdfloat2{1.0f, 1.0f}));

			RecomputeGrid();
			Invalidate();
		}
	} else {
		mScrollOffset = mDragOffsetF + vdfloat2{(float)x, (float)y};

		RecomputeImageArea(false);
	}
}

void ATUIColorImageReferenceControl::OnMouseDownL(sint32 x, sint32 y) {
	if (mDestArea.empty())
		return;

	int pti = FindPoint(x, y);

	mDragPtIndex = pti;
	mbDragging = true;

	if (pti >= 0) {
		mDragOffset = mPointPixelPos[pti] - vdint2{x, y};
		Invalidate();
	} else {
		mDragOffsetF = mScrollOffset - vdfloat2{(float)x, (float)y};
	}

	SetCapture(mhwnd);
}

void ATUIColorImageReferenceControl::OnMouseUpL(sint32 x, sint32 y) {
	if (mbDragging) {
		mbDragging = false;

		ReleaseCapture();

		if (mDragPtIndex >= 0) {
			mDragPtIndex = -1;

			SampleColors();
			Invalidate();
		}
	}
}

void ATUIColorImageReferenceControl::OnMouseWheel(sint32 x, sint32 y, float delta) {
	mWheelAccum += delta;

	int clicks = (int)mWheelAccum;

	if (clicks) {
		mWheelAccum -= (float)clicks;

		int newZoomClicks = std::clamp(mZoomClicks + clicks, 0, 8);

		if (mZoomClicks != newZoomClicks) {
			mZoomClicks = newZoomClicks;

			RecomputeImageArea(true);
			Invalidate();
		}
	}
}

void ATUIColorImageReferenceControl::OnMouseLeave() {
	if (mbDragging) {
		mbDragging = false;
		ReleaseCapture();
	}
}

int ATUIColorImageReferenceControl::FindPoint(sint32 x, sint32 y) const {
	for(int i=0; i<4; ++i) {
		int dx = abs(mPointPixelPos[i].x - x);
		int dy = abs(mPointPixelPos[i].y - y);

		if (std::max(dx, dy) <= 4)
			return i;
	}

	return -1;
}

void ATUIColorImageReferenceControl::RecomputeAdjustmentTable() {
	uint32 scale = VDRoundToInt32(mGain * 0x1.0p20f);

	for(int i=0; i<256; ++i) {
		uint32 y = ((uint32)i * scale) >> 20;

		if (y > 255)
			y = 255;

		mAdjustmentTable[i] = y;
	}
}

void ATUIColorImageReferenceControl::RecomputeAdjustedImage() {
	const uint8 *srcRow = (const uint8 *)mImage.data;
	if (!srcRow)
		return;

	if (!mAdjustedImage.data || mAdjustedImage.w != mImage.w || mAdjustedImage.h != mImage.h)
		mAdjustedImage.init(mImage.w, mImage.h, nsVDPixmap::kPixFormat_XRGB8888);
	
	uint8 *dstRow = (uint8 *)mAdjustedImage.data;

	sint32 w = mAdjustedImage.w;
	sint32 h = mAdjustedImage.h;
	const uint8 *VDRESTRICT table = mAdjustmentTable;
	for(sint32 y = 0; y < h; ++y) {
		const uint8 *VDRESTRICT src = srcRow;
		uint8 *VDRESTRICT dst = dstRow;

		for(sint32 x = 0; x < w; ++x) {
			dst[0] = table[src[0]];
			dst[1] = table[src[1]];
			dst[2] = table[src[2]];
			dst[3] = src[3];
			dst += 4;
			src += 4;
		}

		srcRow += mImage.pitch;
		dstRow += mAdjustedImage.pitch;
	}
}

void ATUIColorImageReferenceControl::RecomputeImageArea(bool force) {
	using namespace nsVDMath;

	vdrect32 area { 0, 0, 0, 0 };
	vdrect32f areaF { 0.0f, 0.0f, 1.0f, 1.0f };

	if (mImage.format && mImage.w > 0 && mImage.h > 0) {
		// viewport size is the size of the client area
		const vdsize32& csz = GetClientSize();
		const vdfloat2 viewportSize { (float)csz.w, (float)csz.h };

		// image size is from the image we're going to scale
		const vdfloat2 imageSize { (float)mImage.w, (float)mImage.h };

		// compute ratio to inscribe image within viewport
		const vdfloat2 xyratio = viewportSize / imageSize;
		const float ratio = std::min(xyratio.x, xyratio.y);
		const vdfloat2 baseSize = imageSize * ratio;

		// compute zoomed size from base size
		const float zoomFactor = powf(2.0f, 0.5f * (float)mZoomClicks);
		const vdfloat2 scaledSize = baseSize * zoomFactor;

		// scroll area is excess of zoomed size over viewport size, if any
		mScrollAreaSize = max(vdfloat2{0.0f, 0.0f}, scaledSize - viewportSize);

		// clamp scroll offset to be within +/- half the scroll area size in either direction
		mScrollOffset = max(-0.5f * mScrollAreaSize, min(mScrollOffset, 0.5f * mScrollAreaSize));

		// set the destination rect as the scaled rect with the center offset from the
		// viewport rect by the scroll offset (this will naturally center)
		vdfloat2 destPos = mScrollOffset + (viewportSize - scaledSize) * 0.5f;
		areaF.set(destPos.x, destPos.y, destPos.x + scaledSize.x, destPos.y + scaledSize.y);

		// integer version encompasses pixel centers covered by float rect, clipped to viewport
		area.left	= std::clamp<sint32>(VDCeilToInt(areaF.left		- 0.5f), 0.0f, csz.w);
		area.top	= std::clamp<sint32>(VDCeilToInt(areaF.top		- 0.5f), 0.0f, csz.h);
		area.right	= std::clamp<sint32>(VDCeilToInt(areaF.right	- 0.5f), 0.0f, csz.w);
		area.bottom	= std::clamp<sint32>(VDCeilToInt(areaF.bottom	- 0.5f), 0.0f, csz.h);
	}

	if (force || mDestAreaF != areaF || mDestArea != area) {
		mDestAreaF = areaF;
		mDestArea = area;

		RecomputePointPixelPositions();
		RecomputeGrid();

		mbScaledImageValid = false;

		Invalidate();
	}
}

void ATUIColorImageReferenceControl::RescaleImage() {
	if (mDestArea.empty()) {
		mScaledImageView.SetImage();
		mScaledImage.clear();
	} else {
		bool reinitNeeded = false;

		if (!mScaledImage.data || mScaledImage.w != mDestArea.width() || mScaledImage.h != mDestArea.height())
			reinitNeeded = true;

		if (reinitNeeded) {
			mScaledImageView.SetImage();
			mScaledImage.init(mDestArea.width(), mDestArea.height(), nsVDPixmap::kPixFormat_XRGB8888);
		}

		vdautoptr resampler { VDCreatePixmapResampler() };

		resampler->SetFilters(IVDPixmapResampler::kFilterCubic, IVDPixmapResampler::kFilterCubic, false);

		vdrect32f surfaceRelDestArea = mDestAreaF;
		surfaceRelDestArea.translate(-(float)mDestArea.left, -(float)mDestArea.top);
		if (resampler->Init(
			surfaceRelDestArea,
			mDestArea.width(),
			mDestArea.height(),
			mAdjustedImage.format,
			vdrect32f(0.0f, 0.0f, mAdjustedImage.w, mAdjustedImage.h),
			mAdjustedImage.w,
			mAdjustedImage.h,
			mAdjustedImage.format))
		{
			resampler->Process(mScaledImage, mAdjustedImage);
		}

		if (reinitNeeded)
			mScaledImageView.SetImage(mScaledImage, false);
		else
			mScaledImageView.Invalidate();
	}

	mbScaledImageValid = true;
}

void ATUIColorImageReferenceControl::RecomputePointPixelPositions() {
	const vdfloat2 scale { (float)mDestAreaF.width(), (float)mDestAreaF.height() };
	const vdfloat2 offset { (float)mDestAreaF.left, (float)mDestAreaF.top };

	for(int i=0; i<4; ++i) {
		vdfloat2 pt = mPoints[i] * scale + offset;

		mPointPixelPos[i] = vdint2{ VDRoundToInt32(pt.x), VDRoundToInt32(pt.y) };
	}
}

void ATUIColorImageReferenceControl::RecomputeGrid() {
	// Compute homogeneous transformation from normalized 0-1 coordinates to
	// four points. See Jim Blinn's Corner 3 p.182 (derived from Heckbert).
	vdfloat3x3 M;
	float U0 = mPoints[0].x;
	float U1 = mPoints[1].x;
	float U2 = mPoints[3].x;	// 2/3 swapped since are book and not circular order
	float U3 = mPoints[2].x;
	float V0 = mPoints[0].y;
	float V1 = mPoints[1].y;
	float V2 = mPoints[3].y;
	float V3 = mPoints[2].y;

	M.x.z = (U3-U2)*(V1-V0) - (U1-U0)*(V3-V2);
	M.y.z = (U3-U0)*(V1-V2) - (U1-U2)*(V3-V0);
	M.z.z = (U1-U2)*(V3-V2) - (U3-U2)*(V1-V2);
	M.z.x = M.z.z*U0;
	M.z.y = M.z.z*V0;
	M.x.x = (M.x.z + M.z.z)*U1 - M.z.x;
	M.y.x = (M.y.z + M.z.z)*U3 - M.z.x;
	M.x.y = (M.x.z + M.z.z)*V1 - M.z.y;
	M.y.y = (M.y.z + M.z.z)*V3 - M.z.y;

	mbProjectionValid = false;

	// If any of the w-coordinates are close to zero or positive, the projection
	// is invalid as it intersects the view plane (they are negated because our
	// handedness is flipped).
	const float eps = 1e-5f;

	if (M.z.z >= -eps || M.x.z + M.z.z >= -eps || M.y.z + M.z.z >= -eps || M.x.z + M.y.z + M.z.z >= -eps) {
		// just copy over the corner centers and return
		mGridCenters[ 0][ 0] = mPoints[0];
		mGridCenters[ 0][15] = mPoints[1];
		mGridCenters[15][ 0] = mPoints[2];
		mGridCenters[15][15] = mPoints[3];
		return;
	}

	mbProjectionValid = true;

	// The points that we are mapping to 0-1 are at the corner cell centers, so we only have 15 divisions
	// between them.

	for(int y=0; y<17; ++y) {
		for(int x=0; x<17; ++x)
			mGridCorners[y][x] = (vdfloat3{((float)x - 0.5f) / 15.0f, ((float)y - 0.5f) / 15.0f, 1.0f} * M).project();
	}

	for(int y=0; y<16; ++y) {
		for(int x=0; x<16; ++x)
			mGridCenters[y][x] = (vdfloat3{(float)x / 15.0f, (float)y / 15.0f, 1.0f} * M).project();
	}
}

void ATUIColorImageReferenceControl::SampleColors() {
	using namespace nsVDMath;
	using namespace nsVDVecMath;

	if (!mImage.format || !mbProjectionValid)
		return;

	const sint32 sourceW = mImage.w;
	const sint32 sourceH = mImage.h;
	const vdfloat2 sourceSize { (float)sourceW, (float)sourceH };

	vdfloat2 cornerPts[4];

	for(int i=0; i<4; ++i)
		cornerPts[i] = mPoints[i] * sourceSize;

	for(int y=0; y<16; ++y) {
		float yf = (float)y / 15.0f;
		vdfloat2 above		= cornerPts[0] + (cornerPts[2] - cornerPts[0]) * (yf - 0.5f / 15.0f);
		vdfloat2 below		= cornerPts[0] + (cornerPts[2] - cornerPts[0]) * (yf + 0.5f / 15.0f);
		vdfloat2 centr		= cornerPts[0] + (cornerPts[2] - cornerPts[0]) * yf;
		vdfloat2 aboveDelta	= ((cornerPts[1] + (cornerPts[3] - cornerPts[1]) * (yf - 0.5f / 15.0f)) - above) / 15.0f;
		vdfloat2 belowDelta	= ((cornerPts[1] + (cornerPts[3] - cornerPts[1]) * (yf + 0.5f / 15.0f)) - below) / 15.0f;
		vdfloat2 centrDelta	= ((cornerPts[1] + (cornerPts[3] - cornerPts[1]) * yf) - centr) / 15.0f;

		for(int x=0; x<16; ++x) {
			// compute boundary corners
			vdfloat2 pt1 = mGridCorners[y  ][x  ] * sourceSize;
			vdfloat2 pt2 = mGridCorners[y  ][x+1] * sourceSize;
			vdfloat2 pt3 = mGridCorners[y+1][x  ] * sourceSize;
			vdfloat2 pt4 = mGridCorners[y+1][x+1] * sourceSize;

			// compute center point 
			vdfloat2 ptc = mGridCenters[y][x] * sourceSize;

			// pull all points inward 10% toward the center
			pt1 += (ptc - pt1) * 0.1f;
			pt2 += (ptc - pt2) * 0.1f;
			pt3 += (ptc - pt3) * 0.1f;
			pt4 += (ptc - pt4) * 0.1f;

			// compute transposed planes so that dot(plane, (p, 1)) >= 0 for inside
			vdfloat4 planeNX { pt1.y - pt2.y, pt2.y - pt4.y, pt4.y - pt3.y, pt3.y - pt1.y };
			vdfloat4 planeNY { pt2.x - pt1.x, pt4.x - pt2.x, pt3.x - pt4.x, pt1.x - pt3.x };
			vdfloat4 planeNW
				= -planeNX * vdfloat4{ pt1.x, pt2.x, pt4.x, pt3.x }
				-  planeNY * vdfloat4{ pt1.y, pt2.y, pt4.y, pt3.y }
				;

			// convert to floating point bounding box
			vdfloat2 minF = min(min(pt1, pt2), min(pt3, pt4));
			vdfloat2 maxF = max(max(pt1, pt2), max(pt3, pt4));

			// convert to integer bounding box based on pixel centers and clamp to surface;
			// note that we are using an inclusive fill convention
			sint32 minX = VDCeilToInt (minF.x - 0.5f);
			sint32 minY = VDCeilToInt (minF.y - 0.5f);
			sint32 maxX = VDFloorToInt(maxF.x + 0.5f);
			sint32 maxY = VDFloorToInt(maxF.y + 0.5f);

			// dropout control
			sint32 cenX = VDFloorToInt(ptc.x);
			sint32 cenY = VDFloorToInt(ptc.y);

			minX = std::min(minX, cenX);
			minY = std::min(minY, cenY);
			maxX = std::max(maxX, cenX + 1);
			maxY = std::max(maxY, cenY + 1);

			// clamp to surface
			minX = std::max<sint32>(minX, 0); 
			minY = std::max<sint32>(minY, 0); 
			maxX = std::min<sint32>(maxX, sourceW); 
			maxY = std::min<sint32>(maxY, sourceH); 

			vdfloat4 planeCheck = planeNX * ((float)cenX + 0.5f) + planeNY * ((float)cenY + 0.5f) + planeNW;

			planeNW += max(vdfloat4::splat(0.1f) - planeCheck, vdfloat4::zero());

			// pixel scan (finally)
			const uint32 *srcRow = (const uint32 *)((const char *)mImage.data + mImage.pitch * minY);

			vdfloat32x4 accum = vdfloat32x4::zero();
			vdfloat32x4 planeNX2 = loadu(planeNX);
			vdfloat32x4 planeNY2 = loadu(planeNY);
			vdfloat32x4 planeNW2 = loadu(planeNW);
			vdfloat32x4 planeV = planeNX2 * (minX + 0.5f) + planeNY2 * (minY + 0.5f) + planeNW2;

			int count = 0;
			for(sint32 py = minY; py < maxY; ++py) {
				vdfloat32x4 planeV2 = planeV;

				for(sint32 px = minX; px < maxX; ++px) {
					if (all_bool(planeV2 >= vdfloat32x4::zero())) {
						accum += vdfloat32x4(VDColorRGB::FromRGB8(srcRow[px]).SRGBToLinear());
						++count;
					}

					planeV2 += planeNX2;
				}

				planeV += planeNY2;

				srcRow = (const uint32 *)((const char *)srcRow + mImage.pitch);
			}

			if (count)
				mRawSampledColors[y*16+x] = (vdfloat32x4)VDColorRGB(accum * (1.0f / (float)count)).LinearToSRGB();
			else
				mRawSampledColors[y*16+x] = vdfloat32x4::zero();
		}
	}
}

///////////////////////////////////////////////////////////////////////////

class ATUIColorImageReferenceWindow final : public VDResizableDialogFrameW32 {
public:
	ATUIColorImageReferenceWindow();

	void SetOnMatch(vdfunction<void()> fn) {
		mpFnOnMatch = fn;
	}

	void UpdateComputedColors(const uint32 colors[256]);

private:
	bool OnLoaded() override;
	void OnDestroy() override;
	void OnHScroll(uint32 id, int code) override;
	void OnCommandLoad();
	void OnCommandMatch();
	void OnCommandResetGain();
	void OnCommandOverlayComputedColors();
	void UpdateFromSolver();
	void OnSolverFinished();

	VDUIProxyButtonControl mLoadButton;
	VDUIProxyButtonControl mMatchButton;
	VDUIProxyButtonControl mResetGainButton;
	VDUIProxyButtonControl mLockHueStartCheckBox;
	VDUIProxyButtonControl mLockGammaCheckBox;
	VDUIProxyButtonControl mNormalizeContrastCheckBox;
	VDUIProxyButtonControl mOverlayComputedColorsCheckBox;
	VDUIProxyControl mStatusLabel;

	vdfunction<void()> mpFnOnMatch;
	ATUIColorImageReferenceControl *mpImageView = nullptr;

	class WorkerThread final : public VDThread, public vdrefcount {
	public:
		WorkerThread(ATUIColorImageReferenceWindow& parent, vdautoptr<IATColorPaletteSolver>&& solver) : mpParent(&parent), mpSolver(std::move(solver)) {}

		void Detach() {
			mMutex.Lock();
			mpParent = nullptr;
			mMutex.Unlock();
		}

		void ThreadRun() override;

		std::pair<ATColorParams, uint32> GetLastSolution() {
			mMutex.Lock();
			auto params = mImprovedSolution;
			auto error = mCurrentError;
			mMutex.Unlock();

			return std::pair(params, error);
		}

		std::optional<std::pair<ATColorParams, uint32>> TakeImprovedSolution() {
			mMutex.Lock();
			auto improved = mbHaveImprovedSolution;
			auto params = mImprovedSolution;
			auto error = mCurrentError;
			mbHaveImprovedSolution = false;
			mMutex.Unlock();

			return improved ? std::optional(std::pair(params, error)) : std::optional<std::pair<ATColorParams, uint32>>();
		}

	private:
		VDCriticalSection mMutex;
		ATUIColorImageReferenceWindow *mpParent;

		vdautoptr<IATColorPaletteSolver> mpSolver;

		bool mbHaveImprovedSolution;
		ATColorParams mImprovedSolution;
		uint32 mCurrentError;
	};

	vdrefptr<WorkerThread> mpWorkerThread;
};

void ATUIColorImageReferenceWindow::WorkerThread::ThreadRun() {
	int pass = 0;

	mMutex.Lock();
	for(;;) {
		bool valid = mpParent != nullptr;

		if (!valid)
			break;

		mMutex.Unlock();

		auto status = mpSolver->Iterate();

		mMutex.Lock();

		if (status == IATColorPaletteSolver::Status::RunningImproved) {
			mbHaveImprovedSolution = true;
			mpSolver->GetCurrentSolution(mImprovedSolution);
			mCurrentError = mpSolver->GetCurrentError().value_or(0);

			if (mpParent)
				mpParent->PostCall(
					[p = mpParent] {
						p->UpdateFromSolver();
					}
				);
		}

		if (status == IATColorPaletteSolver::Status::Finished) {
			++pass;

			if (pass == 1) {
				ATColorParams newInitialSolution = mImprovedSolution;

				newInitialSolution.mColorMatchingMode = ATColorMatchingMode::SRGB;

				mpSolver->Reinit(newInitialSolution);
			} else {
				break;
			}
		}
	}

	mpSolver->GetCurrentSolution(mImprovedSolution);
	mCurrentError = mpSolver->GetCurrentError().value_or(0);

	if (mpParent)
		mpParent->PostCall(
			[p = mpParent] {
				p->OnSolverFinished();
			}
		);

	mMutex.Unlock();

	this->Release();
}

ATUIColorImageReferenceWindow::ATUIColorImageReferenceWindow()
	: VDResizableDialogFrameW32(IDD_ADJUST_COLORS_REFERENCE)
{
	ATUINativeWindow::RegisterForClass<ATUIColorImageReferenceControl>(L"ATColorReference");

	mLoadButton.SetOnClicked([this] { OnCommandLoad(); });
	mMatchButton.SetOnClicked([this] { OnCommandMatch(); });
	mResetGainButton.SetOnClicked([this] { OnCommandResetGain(); });
	mOverlayComputedColorsCheckBox.SetOnClicked([this] { OnCommandOverlayComputedColors(); });
}

void ATUIColorImageReferenceWindow::UpdateComputedColors(const uint32 colors[256]) {
	if (mpImageView)
		mpImageView->SetOverlayColors(colors);
}

bool ATUIColorImageReferenceWindow::OnLoaded() {
	ATUIRegisterModelessDialog(mhdlg);

	AddProxy(&mLoadButton, IDC_LOAD);
	AddProxy(&mMatchButton, IDC_MATCH);
	AddProxy(&mResetGainButton, IDC_RESET_GAIN);
	AddProxy(&mStatusLabel, IDC_STATIC_STATUS);
	AddProxy(&mLockHueStartCheckBox, IDC_LOCKHUESTART);
	AddProxy(&mLockGammaCheckBox, IDC_LOCKGAMMA);
	AddProxy(&mNormalizeContrastCheckBox, IDC_NORMALIZECONTRAST);
	AddProxy(&mOverlayComputedColorsCheckBox, IDC_OVERLAYCOMPUTEDCOLORS);

	mResizer.Add(IDC_REFERENCE_IMAGE, mResizer.kMC);
	mResizer.Add(IDC_STATIC_GAIN, mResizer.kBL);
	mResizer.Add(IDC_GAIN, mResizer.kBL);
	mResizer.Add(IDC_STATIC_STATUS, mResizer.kBL);
	mResizer.Add(mLoadButton.GetHandle(), mResizer.kBL);
	mResizer.Add(mMatchButton.GetHandle(), mResizer.kBL);
	mResizer.Add(mResetGainButton.GetHandle(), mResizer.kBL);

	TBSetRange(IDC_GAIN, -100, 100);
	TBSetPageStep(IDC_GAIN, 10);
	TBSetValue(IDC_GAIN, 0);

	mpImageView = ATUINativeWindow::FromHandle<ATUIColorImageReferenceControl>(GetControl(IDC_REFERENCE_IMAGE));

	return VDResizableDialogFrameW32::OnLoaded();
}

void ATUIColorImageReferenceWindow::OnDestroy() {
	if (mpWorkerThread) {
		mpWorkerThread->Detach();
		mpWorkerThread = nullptr;
	}

	mpImageView = nullptr;

	VDResizableDialogFrameW32::OnDestroy();

	ATUIUnregisterModelessDialog(mhdlg);
}

void ATUIColorImageReferenceWindow::OnHScroll(uint32 id, int code) {
	int rawValue = TBGetValue(IDC_GAIN);

	mpImageView->SetGain(powf(2.0f, (float)rawValue / 100.0f));
}

void ATUIColorImageReferenceWindow::OnCommandLoad() {
	try {
		const VDStringW& path = VDGetLoadFileName('cref', (VDGUIHandle)mhdlg, L"Select Reference Image", L"PNG/JPEG images\0*.png;*.jpg;*.jpeg;*.pal;*.act\0", nullptr);
		if (path.empty())
			return;

		VDFile f(path.c_str());

		sint64 size = f.size();
		if (size > 256*1024*1024)
			throw MyError("File is too large to load.");

		vdblock<unsigned char> buf((uint32)size);
		f.read(buf.data(), (long)size);
		f.close();

		static constexpr uint8 kJPEGSig[4] = { 0xFF, 0xD8, 0xFF, 0xE0 };

		if (size == 768 && path.size() >= 4 && (!vdwcsicmp(&*(path.end() - 4), L".pal") || !vdwcsicmp(&*(path.end() - 4), L".act"))) {
			// convert the palette from 24-bit RGB to 32-bit BGR
			vdblock<uint8> pal32(1024);

			uint8 *dst = pal32.data();
			const uint8 *src = buf.data();
			for(int i=0; i<256; ++i) {
				dst[0] = src[2];
				dst[1] = src[1];
				dst[2] = src[0];
				dst[3] = 0;
				dst += 4;
				src += 3;
			}

			// map the palette as a 16x16 32-bit RGB bitmap and stretchblt 16x to 256x256
			VDPixmap px;
			px.w = 16;
			px.h = 16;
			px.data = pal32.data();
			px.pitch = 16 * 4;
			px.format = nsVDPixmap::kPixFormat_XRGB8888;

			VDPixmapBuffer px2(256, 256, nsVDPixmap::kPixFormat_XRGB8888);
			VDPixmapStretchBltNearest(px2, px);

			mpImageView->SetImage(px2);
			mpImageView->SetCornerPoints(vdfloat2 { 0.5f / 16.0f, 0.5f / 16.0f }, vdfloat2 { 15.5f / 16.0f, 15.5f / 16.0f });
		} else if (size >= 4 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF && (buf[3] & 0xF0) == 0xE0) {
			VDPixmapBuffer pxbuf;
			ATLoadFrameFromMemory(pxbuf, buf.data(), size);
			mpImageView->SetImage(pxbuf);
		} else if (size >= 8 && !memcmp(buf.data(), nsVDPNG::kPNGSignature, 8)){
			vdautoptr decoder(VDCreateImageDecoderPNG());
			if (kPNGDecodeOK == decoder->Decode(buf.data(), (uint32)buf.size())) {
				const VDPixmap& px = decoder->GetFrameBuffer();

				mpImageView->SetImage(px);
			} else
				throw MyError("Error decoding PNG image.");
		} else {
			throw MyError("Image file is not of a supported image type.");
		}
	} catch(const MyError& e) {
		ShowError(e);
	}
}

void ATUIColorImageReferenceWindow::OnCommandMatch() {
	if (!mpImageView)
		return;

	if (mpWorkerThread) {
		mpWorkerThread->Detach();
		mpWorkerThread = nullptr;

		mMatchButton.SetCaption(L"Match");
	} else {
		mMatchButton.SetCaption(L"Stop");

		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		ATColorSettings settings = gtia.GetColorSettings();
		ATColorParams initialParams = settings.mbUsePALParams && gtia.IsPALMode() ? settings.mPALParams : settings.mNTSCParams;

		vdautoptr solver(ATCreateColorPaletteSolver());

		uint32 pal[256];
		mpImageView->GetSampledColors(pal);

		solver->Init(initialParams, pal, mLockHueStartCheckBox.GetChecked(), mLockGammaCheckBox.GetChecked());

		initialParams.mColorMatchingMode = ATColorMatchingMode::None;
		solver->Reinit(initialParams);

		mpWorkerThread = new WorkerThread(*this, std::move(solver));
		mpWorkerThread->AddRef();
		if (!mpWorkerThread->ThreadStart()) {
			mpWorkerThread->Release();
			mpWorkerThread = nullptr;
		}
	}
}

void ATUIColorImageReferenceWindow::OnCommandResetGain() {
	TBSetValue(IDC_GAIN, 0);
	mpImageView->SetGain(1.0f);
}

void ATUIColorImageReferenceWindow::OnCommandOverlayComputedColors() {
	mpImageView->SetColorOverlayEnabled(mOverlayComputedColorsCheckBox.GetChecked());
}

void ATUIColorImageReferenceWindow::UpdateFromSolver() {
	if (!mpWorkerThread)
		return;

	const auto& params = mpWorkerThread->TakeImprovedSolution();

	if (!params.has_value())
		return;

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	ATColorSettings settings = gtia.GetColorSettings();
	ATColorParams& activeParams = settings.mbUsePALParams && gtia.IsPALMode() ? settings.mPALParams : settings.mNTSCParams;

	activeParams = params.value().first;

	if (mNormalizeContrastCheckBox.GetChecked())
		activeParams.mContrast = 1.0f - activeParams.mBrightness;

	// convert raw sum of squared byte errors to standard error
	const uint32 rawError = params.value().second;
	const float stdError = sqrtf((float)rawError / 719.0f);		// 240 colors x 3 RGB channels, -1 for unbiased estimator

	const wchar_t *errorMatchQuality = L"";

	if (stdError < 2.5f)
		errorMatchQuality = L"excellent";
	else if (stdError < 5.0f)
		errorMatchQuality = L"very good";
	else if (stdError < 10.0f)
		errorMatchQuality = L"good";
	else if (stdError < 15.0f)
		errorMatchQuality = L"poor";
	else
		errorMatchQuality = L"very poor";

	VDStringW label;
	label.sprintf(L"Error = %.6g (%ls)", stdError, errorMatchQuality);
	mStatusLabel.SetCaption(label.c_str());

	g_sim.GetGTIA().SetColorSettings(settings);

	if (mpFnOnMatch)
		mpFnOnMatch();
}

void ATUIColorImageReferenceWindow::OnSolverFinished() {
	mpWorkerThread = nullptr;
	mMatchButton.SetCaption(L"Match");
}

///////////////////////////////////////////////////////////////////////////

class ATAdjustColorsDialog final : public VDResizableDialogFrameW32 {
public:
	ATAdjustColorsDialog();

protected:
	bool OnLoaded() override;
	void OnDestroy() override;
	void OnDataExchange(bool write) override;
	void OnEnable(bool enable) override;
	bool OnCommand(uint32 id, uint32 extcode) override;
	void OnInitMenu(VDZHMENU hmenu) override;
	void OnHScroll(uint32 id, int code) override;
	VDZINT_PTR DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) override;
	void ForcePresetToCustom();
	void OnParamUpdated(uint32 id);
	void UpdateLabel(uint32 id);
	void UpdateColorImage();
	void UpdateGammaWarning();
	void ExportPalette(const wchar_t *s);
	void OnPresetChanged(int sel);
	void OnLumaRampChanged(VDUIProxyComboBoxControl *sender, int sel);
	void OnColorModeChanged(VDUIProxyComboBoxControl *sender, int sel);
	void OnGammaRampHelp();

	bool mbShowRelativeOffsets = false;

	ATColorSettings mSettings;
	ATNamedColorParams *mpParams;
	ATNamedColorParams *mpOtherParams;

	VDUIProxyComboBoxControl mPresetCombo;
	VDUIProxyComboBoxControl mLumaRampCombo;
	VDUIProxyComboBoxControl mColorModeCombo;
	VDDelegate mDelLumaRampChanged;
	VDDelegate mDelColorModeChanged;

	VDUIProxySysLinkControl mGammaWarning;

	vdrefptr<ATUIColorReferenceControl> mpSamplesControl;
	ATUIColorImageReferenceWindow mReferenceWindow;
};

ATAdjustColorsDialog g_adjustColorsDialog;

ATAdjustColorsDialog::ATAdjustColorsDialog()
	: VDResizableDialogFrameW32(IDD_ADJUST_COLORS)
	, mpSamplesControl(new ATUIColorReferenceControl)
{
	mGammaWarning.SetOnClicked([this] { OnGammaRampHelp(); });
	mPresetCombo.SetOnSelectionChanged([this](int sel) { OnPresetChanged(sel); });
	mLumaRampCombo.OnSelectionChanged() += mDelLumaRampChanged.Bind(this, &ATAdjustColorsDialog::OnLumaRampChanged);
	mColorModeCombo.OnSelectionChanged() += mDelColorModeChanged.Bind(this, &ATAdjustColorsDialog::OnColorModeChanged);

	mReferenceWindow.SetOnMatch([this] {
		OnDataExchange(false);

		if (!mpParams->mPresetTag.empty()) {
			ForcePresetToCustom();
			OnDataExchange(true);
		}
	});
}

bool ATAdjustColorsDialog::OnLoaded() {
	ATUIRegisterModelessDialog(mhwnd);

	HWND hwndPlaceholderRef = GetControl(IDC_REFERENCE_VIEW);
	if (hwndPlaceholderRef) {
		ATUINativeWindowProxy proxy(hwndPlaceholderRef);
		const vdrect32 r = proxy.GetArea();

		mpSamplesControl->CreateChild(mhdlg, IDC_REFERENCE_VIEW, r.left, r.top, r.width(), r.height(), WS_VISIBLE | WS_CHILD, WS_EX_CLIENTEDGE);

		if (mpSamplesControl->IsValid()) {
			mResizer.AddAlias(mpSamplesControl->GetHandleW32(), hwndPlaceholderRef, mResizer.kMC | mResizer.kAvoidFlicker);
			mResizer.Remove(hwndPlaceholderRef);

			DestroyWindow(hwndPlaceholderRef);

			ApplyFontToControl(IDC_REFERENCE_VIEW);
		}
	}

	mResizer.Add(IDC_GAMMA_WARNING, mResizer.kBC);
	mResizer.Add(IDC_COLORS, mResizer.kTL);

	AddProxy(&mGammaWarning, IDC_GAMMA_WARNING);
	AddProxy(&mPresetCombo, IDC_PRESET);

	AddProxy(&mLumaRampCombo, IDC_LUMA_RAMP);
	mLumaRampCombo.AddItem(L"Linear");
	mLumaRampCombo.AddItem(L"XL/XE");

	AddProxy(&mColorModeCombo, IDC_COLORMATCHING_MODE);
	mColorModeCombo.AddItem(L"None");
	mColorModeCombo.AddItem(L"NTSC/PAL to sRGB");
	mColorModeCombo.AddItem(L"NTSC/PAL to Adobe RGB");

	TBSetRange(IDC_HUESTART, -120, 360);
	TBSetRange(IDC_HUERANGE, 0, 540);
	TBSetRange(IDC_BRIGHTNESS, -50, 50);
	TBSetRange(IDC_CONTRAST, 0, 200);
	TBSetRange(IDC_SATURATION, 0, 100);
	TBSetRange(IDC_GAMMACORRECT, 50, 260);
	TBSetRange(IDC_INTENSITYSCALE, 50, 200 + 20);
	TBSetRange(IDC_ARTPHASE, -60, 360);
	TBSetRange(IDC_ARTSAT, 0, 400);
	TBSetRange(IDC_ARTSHARP, 0, 100);
	TBSetRange(IDC_RED_SHIFT, -225, 225);
	TBSetRange(IDC_RED_SCALE, 0, 400);
	TBSetRange(IDC_GRN_SHIFT, -225, 225);
	TBSetRange(IDC_GRN_SCALE, 0, 400);
	TBSetRange(IDC_BLU_SHIFT, -225, 225);
	TBSetRange(IDC_BLU_SCALE, 0, 400);

	UpdateGammaWarning();

	mPresetCombo.Clear();
	mPresetCombo.AddItem(L"Custom");

	const uint32 n = ATGetColorPresetCount();
	for(uint32 i = 0; i < n; ++i) {
		mPresetCombo.AddItem(ATGetColorPresetNameByIndex(i));
	}

	OnDataExchange(false);
	SetFocusToControl(IDC_HUESTART);
	return true;
}

void ATAdjustColorsDialog::OnDestroy() {
	mReferenceWindow.Destroy();

	ATUIUnregisterModelessDialog(mhwnd);

	VDDialogFrameW32::OnDestroy();
}

void ATAdjustColorsDialog::OnDataExchange(bool write) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	if (write) {
		if (!mSettings.mbUsePALParams)
			*mpOtherParams = *mpParams;

		g_sim.GetGTIA().SetColorSettings(mSettings);
	} else {
		mSettings = gtia.GetColorSettings();

		if (gtia.IsPALMode()) {
			mpParams = &mSettings.mPALParams;
			mpOtherParams = &mSettings.mNTSCParams;
		} else {
			mpParams = &mSettings.mNTSCParams;
			mpOtherParams = &mSettings.mPALParams;
		}

		mPresetCombo.SetSelection(ATGetColorPresetIndexByTag(mpParams->mPresetTag.c_str()) + 1);

		CheckButton(IDC_SHARED, !mSettings.mbUsePALParams);
		CheckButton(IDC_PALQUIRKS, mpParams->mbUsePALQuirks);

		TBSetValue(IDC_HUESTART, VDRoundToInt(mpParams->mHueStart));
		TBSetValue(IDC_HUERANGE, VDRoundToInt(mpParams->mHueRange));
		TBSetValue(IDC_BRIGHTNESS, VDRoundToInt(mpParams->mBrightness * 100.0f));
		TBSetValue(IDC_CONTRAST, VDRoundToInt(mpParams->mContrast * 100.0f));
		TBSetValue(IDC_SATURATION, VDRoundToInt(mpParams->mSaturation * 100.0f));
		TBSetValue(IDC_GAMMACORRECT, VDRoundToInt(mpParams->mGammaCorrect * 100.0f));

		// apply dead zone
		int adjustedIntensityScale = VDRoundToInt(mpParams->mIntensityScale * 100.0f);
		if (adjustedIntensityScale > 100)
			adjustedIntensityScale += 20;
		else if (adjustedIntensityScale == 100)
			adjustedIntensityScale += 10;
		TBSetValue(IDC_INTENSITYSCALE, adjustedIntensityScale);

		sint32 adjustedHue = VDRoundToInt(mpParams->mArtifactHue);
		if (adjustedHue < -60)
			adjustedHue += 360;
		else if (adjustedHue > 360)
			adjustedHue -= 360;
		TBSetValue(IDC_ARTPHASE, adjustedHue);

		TBSetValue(IDC_ARTSAT, VDRoundToInt(mpParams->mArtifactSat * 100.0f));
		TBSetValue(IDC_ARTSHARP, VDRoundToInt(mpParams->mArtifactSharpness * 100.0f));
		TBSetValue(IDC_RED_SHIFT, VDRoundToInt(mpParams->mRedShift * 10.0f));
		TBSetValue(IDC_RED_SCALE, VDRoundToInt(mpParams->mRedScale * 100.0f));
		TBSetValue(IDC_GRN_SHIFT, VDRoundToInt(mpParams->mGrnShift * 10.0f));
		TBSetValue(IDC_GRN_SCALE, VDRoundToInt(mpParams->mGrnScale * 100.0f));
		TBSetValue(IDC_BLU_SHIFT, VDRoundToInt(mpParams->mBluShift * 10.0f));
		TBSetValue(IDC_BLU_SCALE, VDRoundToInt(mpParams->mBluScale * 100.0f));

		mLumaRampCombo.SetSelection(mpParams->mLumaRampMode);
		mColorModeCombo.SetSelection((int)mpParams->mColorMatchingMode);

		UpdateLabel(IDC_HUESTART);
		UpdateLabel(IDC_HUERANGE);
		UpdateLabel(IDC_BRIGHTNESS);
		UpdateLabel(IDC_CONTRAST);
		UpdateLabel(IDC_SATURATION);
		UpdateLabel(IDC_GAMMACORRECT);
		UpdateLabel(IDC_INTENSITYSCALE);
		UpdateLabel(IDC_ARTPHASE);
		UpdateLabel(IDC_ARTSAT);
		UpdateLabel(IDC_ARTSHARP);
		UpdateLabel(IDC_RED_SHIFT);
		UpdateLabel(IDC_RED_SCALE);
		UpdateLabel(IDC_GRN_SHIFT);
		UpdateLabel(IDC_GRN_SCALE);
		UpdateLabel(IDC_BLU_SHIFT);
		UpdateLabel(IDC_BLU_SCALE);
		UpdateColorImage();
	}
}

void ATAdjustColorsDialog::OnEnable(bool enable) {
	ATUISetGlobalEnableState(enable);
}

bool ATAdjustColorsDialog::OnCommand(uint32 id, uint32 extcode) {
	if (id == ID_OPTIONS_SHAREDPALETTES) {
		if (mSettings.mbUsePALParams) {
			if (!Confirm(L"Enabling palette sharing will overwrite the other profile with the current colors. Proceed?", L"Altirra Warning")) {
				return true;
			}

			mSettings.mbUsePALParams = false;
			OnDataExchange(true);
		}

		return true;
	} else if (id == ID_OPTIONS_SEPARATEPALETTES) {
		if (!mSettings.mbUsePALParams) {
			mSettings.mbUsePALParams = true;
			OnDataExchange(true);
		}

		return true;
	} else if (id == ID_OPTIONS_USEPALQUIRKS) {
		mpParams->mbUsePALQuirks = !mpParams->mbUsePALQuirks;

		OnDataExchange(true);
		UpdateColorImage();
		return true;
	} else if (id == ID_VIEW_SHOWRGBSHIFTS) {
		if (mbShowRelativeOffsets) {
			mbShowRelativeOffsets = false;

			UpdateLabel(IDC_RED_SHIFT);
			UpdateLabel(IDC_RED_SCALE);
			UpdateLabel(IDC_GRN_SHIFT);
			UpdateLabel(IDC_GRN_SCALE);
		}
		return true;
	} else if (id == ID_VIEW_SHOWRGBRELATIVEOFFSETS) {
		if (!mbShowRelativeOffsets) {
			mbShowRelativeOffsets = true;

			UpdateLabel(IDC_RED_SHIFT);
			UpdateLabel(IDC_RED_SCALE);
			UpdateLabel(IDC_GRN_SHIFT);
			UpdateLabel(IDC_GRN_SCALE);
		}
		return true;
	} else if (id == ID_FILE_EXPORTPALETTE) {
		const VDStringW& fn = VDGetSaveFileName('pal ', (VDGUIHandle)mhdlg, L"Export palette", L"Atari800 palette (*.pal)\0*.pal", L"pal");

		if (!fn.empty()) {
			ExportPalette(fn.c_str());
		}
		return true;
	} else if (id == ID_FILE_LOAD) {
		const VDStringW& fn = VDGetLoadFileName('colr', (VDGUIHandle)mhdlg, L"Load color settings", L"Altirra color settings (*.atcolors)\0*.atcolors", L"atcolors");

		if (!fn.empty()) {
			vdrefptr<IATSerializable> rawData;

			{
				vdautoptr<IATSaveStateDeserializer> deser{ATCreateSaveStateDeserializer()};
				VDFileStream fs(fn.c_str());
				deser->Deserialize(fs, ~rawData);
			}

			ATSaveStateColorSettings *cs = atser_cast<ATSaveStateColorSettings *>(rawData);
			if (!cs || !cs->mpNTSCParams)
				throw MyError("File is not a supported color settings file.");

			mSettings.mNTSCParams = cs->mpNTSCParams->mParams;

			if (cs->mpPALParams) {
				mSettings.mPALParams = cs->mpPALParams->mParams;
				mSettings.mbUsePALParams = true;
			} else {
				mSettings.mPALParams = mSettings.mNTSCParams;
				mSettings.mbUsePALParams = false;
			}

			// write into GTIA and then read back into UI
			OnDataExchange(true);
			OnDataExchange(false);
		}
	} else if (id == ID_FILE_SAVE) {
		const VDStringW& fn = VDGetSaveFileName('colr', (VDGUIHandle)mhdlg, L"Save color settings", L"Altirra color settings (*.atcolors)\0*.atcolors", L"atcolors");

		if (!fn.empty()) {
			vdrefptr<ATSaveStateColorSettings> cs(new ATSaveStateColorSettings(mSettings));

			vdautoptr<IATSaveStateSerializer> ser(ATCreateSaveStateSerializer());
			VDFileStream fs(fn.c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kSequential);
			VDBufferedWriteStream bs(&fs, 4096);

			ser->Serialize(bs, *cs, L"ATColorSettings");
			bs.Flush();
			fs.close();
		}
		return true;
	} else if (id == ID_FILE_IMPORTREFERENCEPICTURE) {
		if (!mReferenceWindow.IsValid()) {
			mReferenceWindow.Create(this);

			uint32 pal[256] {};
			g_sim.GetGTIA().GetPalette(pal);
			mpSamplesControl->UpdateFromPalette(pal);

			mReferenceWindow.UpdateComputedColors(pal);
		}
		return true;
	}

	return false;
}

void ATAdjustColorsDialog::OnInitMenu(VDZHMENU hmenu) {
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_OPTIONS_SEPARATEPALETTES, mSettings.mbUsePALParams);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_OPTIONS_SHAREDPALETTES, !mSettings.mbUsePALParams);
	VDCheckMenuItemByCommandW32(hmenu, ID_OPTIONS_USEPALQUIRKS, mpParams->mbUsePALQuirks);
	VDEnableMenuItemByCommandW32(hmenu, ID_OPTIONS_USEPALQUIRKS, g_sim.GetGTIA().IsPALMode());
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIEW_SHOWRGBSHIFTS, !mbShowRelativeOffsets);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIEW_SHOWRGBRELATIVEOFFSETS, mbShowRelativeOffsets);
}

void ATAdjustColorsDialog::OnHScroll(uint32 id, int code) {
	if (id == IDC_HUESTART) {
		float v = (float)TBGetValue(IDC_HUESTART);

		if (mpParams->mHueStart != v) {
			mpParams->mHueStart = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_HUERANGE) {
		float v = (float)TBGetValue(IDC_HUERANGE);

		if (mpParams->mHueRange != v) {
			mpParams->mHueRange = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_BRIGHTNESS) {
		float v = (float)TBGetValue(IDC_BRIGHTNESS) / 100.0f;

		if (mpParams->mBrightness != v) {
			mpParams->mBrightness = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_CONTRAST) {
		float v = (float)TBGetValue(IDC_CONTRAST) / 100.0f;

		if (mpParams->mContrast != v) {
			mpParams->mContrast = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_SATURATION) {
		float v = (float)TBGetValue(IDC_SATURATION) / 100.0f;

		if (mpParams->mSaturation != v) {
			mpParams->mSaturation = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_GAMMACORRECT) {
		float v = (float)TBGetValue(IDC_GAMMACORRECT) / 100.0f;

		if (mpParams->mGammaCorrect != v) {
			mpParams->mGammaCorrect = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_INTENSITYSCALE) {
		int rawValue = TBGetValue(IDC_INTENSITYSCALE);

		if (rawValue >= 120)
			rawValue -= 20;
		else if (rawValue >= 100)
			rawValue = 100;

		float v = (float)rawValue / 100.0f;

		if (mpParams->mIntensityScale != v) {
			mpParams->mIntensityScale = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_ARTPHASE) {
		float v = (float)TBGetValue(IDC_ARTPHASE);

		if (mpParams->mArtifactHue != v) {
			mpParams->mArtifactHue = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_ARTSAT) {
		float v = (float)TBGetValue(IDC_ARTSAT) / 100.0f;

		if (mpParams->mArtifactSat != v) {
			mpParams->mArtifactSat = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_ARTSHARP) {
		float v = (float)TBGetValue(IDC_ARTSHARP) / 100.0f;

		if (mpParams->mArtifactSharpness != v) {
			mpParams->mArtifactSharpness = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_RED_SHIFT) {
		float v = (float)TBGetValue(IDC_RED_SHIFT) / 10.0f;

		if (mpParams->mRedShift != v) {
			mpParams->mRedShift = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_RED_SCALE) {
		float v = (float)TBGetValue(IDC_RED_SCALE) / 100.0f;

		if (mpParams->mRedScale != v) {
			mpParams->mRedScale = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_GRN_SHIFT) {
		float v = (float)TBGetValue(IDC_GRN_SHIFT) / 10.0f;

		if (mpParams->mGrnShift != v) {
			mpParams->mGrnShift = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_GRN_SCALE) {
		float v = (float)TBGetValue(IDC_GRN_SCALE) / 100.0f;

		if (mpParams->mGrnScale != v) {
			mpParams->mGrnScale = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_BLU_SHIFT) {
		float v = (float)TBGetValue(IDC_BLU_SHIFT) / 10.0f;

		if (mpParams->mBluShift != v) {
			mpParams->mBluShift = v;

			OnParamUpdated(id);
		}
	} else if (id == IDC_BLU_SCALE) {
		float v = (float)TBGetValue(IDC_BLU_SCALE) / 100.0f;

		if (mpParams->mBluScale != v) {
			mpParams->mBluScale = v;

			OnParamUpdated(id);
		}
	}
}

VDZINT_PTR ATAdjustColorsDialog::DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	if (msg == WM_DRAWITEM) {
		if (wParam == IDC_COLORS) {
			const DRAWITEMSTRUCT& drawInfo = *(const DRAWITEMSTRUCT *)lParam;

			BITMAPINFO bi = {
				{
					sizeof(BITMAPINFOHEADER),
					16,
					19,
					1,
					32,
					BI_RGB,
					16*19*4,
					0,
					0,
					0,
					0
				}
			};

			uint32 image[256 + 48] = {0};
			uint32 *pal = image + 48;
			g_sim.GetGTIA().GetPalette(pal);

			// last three rows are used for 'text' screen
			image[0x10] = pal[0];
			image[0x00] = pal[0];
			for(int i=0; i<10; ++i) {
				image[0x11 + i] = pal[0x94];
				image[0x01 + i] = pal[0x94];
			}
			image[0x10+2] = pal[0x9A];
			image[0x10+11] = pal[0];
			image[0x00+11] = pal[0];

			// add NTSC artifacting colors
			uint32 ntscac[2];
			g_sim.GetGTIA().GetNTSCArtifactColors(ntscac);

			image[0x1C] = image[0x1D] = ntscac[0];
			image[0x1E] = image[0x1F] = ntscac[1];

			// flip palette section
			for(int i=0; i<128; i += 16)
				VDSwapMemory(&pal[i], &pal[240-i], 16*sizeof(uint32));

			StretchDIBits(drawInfo.hDC,
				drawInfo.rcItem.left,
				drawInfo.rcItem.top,
				drawInfo.rcItem.right - drawInfo.rcItem.left,
				drawInfo.rcItem.bottom - drawInfo.rcItem.top,
				0, 0, 16, 19, image, &bi, DIB_RGB_COLORS, SRCCOPY);

			SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}
	}

	return VDDialogFrameW32::DlgProc(msg, wParam, lParam);
}

void ATAdjustColorsDialog::ForcePresetToCustom() {
	// force preset to custom
	if (!mpParams->mPresetTag.empty()) {
		mpParams->mPresetTag.clear();

		mPresetCombo.SetSelection(0);
	}
}

void ATAdjustColorsDialog::OnParamUpdated(uint32 id) {
	ForcePresetToCustom();

	OnDataExchange(true);
	UpdateColorImage();
	UpdateLabel(id);
}

void ATAdjustColorsDialog::UpdateLabel(uint32 id) {
	switch(id) {
		case IDC_HUESTART:
			SetControlTextF(IDC_STATIC_HUESTART, L"%.0f\u00B0", mpParams->mHueStart);
			break;
		case IDC_HUERANGE:
			SetControlTextF(IDC_STATIC_HUERANGE, L"%.1f\u00B0", mpParams->mHueRange / 15.0f);
			break;
		case IDC_BRIGHTNESS:
			SetControlTextF(IDC_STATIC_BRIGHTNESS, L"%+.0f%%", mpParams->mBrightness * 100.0f);
			break;
		case IDC_CONTRAST:
			SetControlTextF(IDC_STATIC_CONTRAST, L"%.0f%%", mpParams->mContrast * 100.0f);
			break;
		case IDC_SATURATION:
			SetControlTextF(IDC_STATIC_SATURATION, L"%.0f%%", mpParams->mSaturation * 100.0f);
			break;
		case IDC_INTENSITYSCALE:
			SetControlTextF(IDC_STATIC_INTENSITYSCALE, L"%.2f", mpParams->mIntensityScale);
			break;
		case IDC_GAMMACORRECT:
			SetControlTextF(IDC_STATIC_GAMMACORRECT, L"%.2f", mpParams->mGammaCorrect);
			break;
		case IDC_ARTPHASE:
			SetControlTextF(IDC_STATIC_ARTPHASE, L"%.0f\u00B0", mpParams->mArtifactHue);
			break;
		case IDC_ARTSAT:
			SetControlTextF(IDC_STATIC_ARTSAT, L"%.0f%%", mpParams->mArtifactSat * 100.0f);
			break;
		case IDC_ARTSHARP:
			SetControlTextF(IDC_STATIC_ARTSHARP, L"%.2f", mpParams->mArtifactSharpness);
			break;

		case IDC_RED_SHIFT:
			// The shifts are defined as deviations from the standard R-Y and B-Y axes, so the
			// biases here come from the angles in the standard matrix.
			if (mbShowRelativeOffsets)
				SetControlTextF(IDC_STATIC_RED_SHIFT, L"B%+.1f\u00B0", 90.0f - mpParams->mRedShift);
			else
				SetControlTextF(IDC_STATIC_RED_SHIFT, L"%.1f\u00B0", mpParams->mRedShift);
			break;
		case IDC_RED_SCALE:
			if (mbShowRelativeOffsets)
				SetControlTextF(IDC_STATIC_RED_SCALE, L"B\u00D7%.2f", mpParams->mRedScale * 0.560949f);
			else
				SetControlTextF(IDC_STATIC_RED_SCALE, L"%.2f", mpParams->mRedScale);
			break;

		case IDC_GRN_SHIFT:
			if (mbShowRelativeOffsets)
				SetControlTextF(IDC_STATIC_GRN_SHIFT, L"B%+.1f\u00B0", 235.80197f - mpParams->mGrnShift);
			else
				SetControlTextF(IDC_STATIC_GRN_SHIFT, L"%.1f\u00B0", mpParams->mGrnShift);
			break;
		case IDC_GRN_SCALE:
			if (mbShowRelativeOffsets)
				SetControlTextF(IDC_STATIC_GRN_SCALE, L"B\u00D7%.2f", mpParams->mGrnScale * 0.3454831f);
			else
				SetControlTextF(IDC_STATIC_GRN_SCALE, L"%.2f", mpParams->mGrnScale);
			break;

		case IDC_BLU_SHIFT:
			SetControlTextF(IDC_STATIC_BLU_SHIFT, L"%.1f\u00B0", mpParams->mBluShift);
			break;
		case IDC_BLU_SCALE:
			SetControlTextF(IDC_STATIC_BLU_SCALE, L"%.2f", mpParams->mBluScale);
			break;
	}
}

void ATAdjustColorsDialog::UpdateColorImage() {
	// update image
	HWND hwndColors = GetDlgItem(mhdlg, IDC_COLORS);
	InvalidateRect(hwndColors, NULL, FALSE);

	uint32 pal[256] = {};
	g_sim.GetGTIA().GetPalette(pal);
	mpSamplesControl->UpdateFromPalette(pal);

	mReferenceWindow.UpdateComputedColors(pal);
}

void ATAdjustColorsDialog::UpdateGammaWarning() {
	bool gammaNonIdentity = false;

	HMONITOR hmon = MonitorFromWindow((HWND)ATUIGetMainWindow(), MONITOR_DEFAULTTOPRIMARY);
	if (hmon) {
		MONITORINFOEXW mi { sizeof(MONITORINFOEXW) };

		if (GetMonitorInfoW(hmon, &mi)) {
			HDC hdc = CreateICW(mi.szDevice, mi.szDevice, nullptr, nullptr);

			if (hdc) {
				WORD gammaRamp[3][256] {};

				if (GetDeviceGammaRamp(hdc, gammaRamp)) {
					for(uint32 i=0; i<256; ++i) {
						sint32 expected = i;

						for(uint32 j=0; j<3; ++j) {
							sint32 actual = gammaRamp[j][i] >> 8;

							if (abs(actual - expected) > 1) {
								gammaNonIdentity = true;
								goto stop_search;
							}
						}
					}
stop_search:
					;
				}

				DeleteDC(hdc);
			}
		}
	}

	if (gammaNonIdentity)
		mGammaWarning.Show();
	else
		mGammaWarning.Hide();
}

void ATAdjustColorsDialog::ExportPalette(const wchar_t *s) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	uint32 pal[256];
	gtia.GetPalette(pal);

	uint8 pal8[768];
	for(int i=0; i<256; ++i) {
		const uint32 c = pal[i];

		pal8[i*3+0] = (uint8)(c >> 16);
		pal8[i*3+1] = (uint8)(c >>  8);
		pal8[i*3+2] = (uint8)(c >>  0);
	}

	VDFile f(s, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	f.write(pal8, sizeof pal8);
}

void ATAdjustColorsDialog::OnPresetChanged(int sel) {
	if (sel == 0) {
		mpParams->mPresetTag.clear();
		OnDataExchange(true);
	} else if (sel > 0 && (uint32)sel <= ATGetColorPresetCount()) {
		mpParams->mPresetTag = ATGetColorPresetTagByIndex(sel - 1);
		static_cast<ATColorParams&>(*mpParams) = ATGetColorPresetByIndex(sel - 1);

		OnDataExchange(true);
		OnDataExchange(false);
	}
}

void ATAdjustColorsDialog::OnLumaRampChanged(VDUIProxyComboBoxControl *sender, int sel) {
	ATLumaRampMode newMode = (ATLumaRampMode)sel;

	if (mpParams->mLumaRampMode != newMode) {
		mpParams->mLumaRampMode = newMode;

		OnParamUpdated(0);
	}
}

void ATAdjustColorsDialog::OnColorModeChanged(VDUIProxyComboBoxControl *sender, int sel) {
	ATColorMatchingMode newMode = (ATColorMatchingMode)sel;

	if (mpParams->mColorMatchingMode != newMode) {
		mpParams->mColorMatchingMode = newMode;

		OnParamUpdated(0);
	}
}

void ATAdjustColorsDialog::OnGammaRampHelp() {
	ATShowHelp(mhdlg, L"colors.html#contexthelp-gamma-ramp");
}

void ATUIOpenAdjustColorsDialog(VDGUIHandle hParent) {
	g_adjustColorsDialog.Create(hParent);
}

void ATUICloseAdjustColorsDialog() {
	g_adjustColorsDialog.Destroy();
}
