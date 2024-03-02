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

#include "stdafx.h"
#include <vd2/system/math.h>
#include <vd2/system/w32assist.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Riza/bitmap.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/uinativewindow.h>
#include "resource.h"
#include "cassette.h"
#include "cassetteimage.h"

///////////////////////////////////////////////////////////////////////////

class ATUITapePeakControl : public ATUINativeWindow {
public:
	ATUITapePeakControl(IATCassetteImage *image);
	~ATUITapePeakControl();

	virtual LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	void SetPosition(float secs);

	VDEvent<ATUITapePeakControl, float>& OnPositionChanged() { return mPositionChangedEvent; }

protected:
	void OnSize();
	void OnClickOrMove(sint32 x);
	void Clear();
	void ClearBitmap();
	void UpdatePositionPx();
	void UpdateImage();
	void DrawPeaks(sint32 x1, sint32 x2, float y1, float y2, const float *data, uint32 c);

	IATCassetteImage *mpImage;
	float mImageLenSecs;
	HDC mhdc;
	HBITMAP mhBitmap;
	HGDIOBJ mhOldBitmap;
	bool mbImageDirty;
	sint32 mWidth;
	sint32 mHeight;
	float mPositionSecs;
	sint32 mPositionPx;

	VDPixmapBuffer mPixmapBuffer;

	VDEvent<ATUITapePeakControl, float> mPositionChangedEvent;
};

ATUITapePeakControl::ATUITapePeakControl(IATCassetteImage *image)
	: mpImage(image)
	, mImageLenSecs(0)
	, mhdc(NULL)
	, mhBitmap(NULL)
	, mhOldBitmap(NULL)
	, mbImageDirty(false)
	, mWidth(0)
	, mHeight(0)
	, mPositionSecs(0)
	, mPositionPx(0)
{
	if (mpImage) {
		const uint32 n = mpImage->GetDataLength();
		mImageLenSecs = (float)n / kDataFrequency;
	}
}

ATUITapePeakControl::~ATUITapePeakControl() {
	Clear();
}

void ATUITapePeakControl::SetPosition(float secs) {
	if (mPositionSecs != secs) {
		mPositionSecs = secs;

		UpdatePositionPx();
	}
}

LRESULT ATUITapePeakControl::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			mWidth = 0;
			mHeight = 0;
			OnSize();
			break;

		case WM_DESTROY:
			Clear();
			break;

		case WM_SIZE:
			OnSize();
			break;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			OnClickOrMove((short)LOWORD(lParam));
			SetCapture(mhwnd);
			return 0;

		case WM_LBUTTONUP:
			if (::GetCapture() == mhwnd)
				::ReleaseCapture();
			return 0;

		case WM_MOUSEMOVE:
			if (wParam & MK_LBUTTON)
				OnClickOrMove((short)LOWORD(lParam));

			return 0;

		case WM_ERASEBKGND:
			return 0;

		case WM_PAINT:
			{
				PAINTSTRUCT ps;
				HDC hdc = BeginPaint(mhwnd, &ps);
				if (hdc) {
					UpdateImage();

					if (mhdc && mhBitmap) {
						if ((uint32)mPositionPx < (uint32)mWidth) {
							VDVERIFY(BitBlt(hdc, 0, 0, mPositionPx, mHeight, mhdc, 0, 0, SRCCOPY));

							RECT rPos = { mPositionPx, 0, mPositionPx + 1, mHeight };
							VDVERIFY(FillRect(hdc, &rPos, (HBRUSH)::GetStockObject(WHITE_BRUSH)));
							
							VDVERIFY(BitBlt(hdc, mPositionPx + 1, 0, mWidth - (mPositionPx + 1), mHeight, mhdc, mPositionPx + 1, 0, SRCCOPY));
						} else {
							VDVERIFY(BitBlt(hdc, 0, 0, mWidth, mHeight, mhdc, 0, 0, SRCCOPY));
						}
					} else {
						RECT r = { 0, 0, mWidth, mHeight };
						VDVERIFY(FillRect(hdc, &r, (HBRUSH)(COLOR_WINDOW + 1)));
					}

					EndPaint(mhwnd, &ps);
				}

				return 0;
			}
			break;
	}

	return ATUINativeWindow::WndProc(msg, wParam, lParam);
}

void ATUITapePeakControl::OnSize() {
	RECT r;

	if (GetClientRect(mhwnd, &r)) {
		if (mWidth != r.right || mHeight != r.bottom) {
			mWidth = r.right;
			mHeight = r.bottom;

			ClearBitmap();
			InvalidateRect(mhwnd, NULL, FALSE);

			UpdatePositionPx();
		}
	}
}

void ATUITapePeakControl::OnClickOrMove(sint32 x) {
	if (x >= mWidth)
		x = mWidth - 1;

	if (x < 0)
		x = 0;

	const float pos = x * mImageLenSecs / (float)mWidth;

	if (mPositionSecs != pos) {
		SetPosition(pos);
		mPositionChangedEvent.Raise(this, pos);
	}
}

void ATUITapePeakControl::Clear() {
	ClearBitmap();

	if (mhdc) {
		VDVERIFY(::DeleteDC(mhdc));
		mhdc = NULL;
	}
}

void ATUITapePeakControl::ClearBitmap() {
	if (mhOldBitmap) {
		VDVERIFY(::SelectObject(mhdc, mhOldBitmap));
		mhOldBitmap = NULL;
	}

	if (mhBitmap) {
		VDVERIFY(::DeleteObject(mhBitmap));
		mhBitmap = NULL;
	}
}

void ATUITapePeakControl::UpdatePositionPx() {
	if (!mWidth || !mHeight || mImageLenSecs <= 0)
		return;

	sint32 px = VDFloorToInt(mPositionSecs * mWidth / mImageLenSecs);

	if (mPositionPx != px) {
		RECT r1 = { mPositionPx, 0, mPositionPx + 1, mHeight };
		RECT r2 = { px, 0, px + 1, mHeight };

		mPositionPx = px;

		if (mhwnd) {
			InvalidateRect(mhwnd, &r1, FALSE);
			InvalidateRect(mhwnd, &r2, FALSE);
		}
	}
}

void ATUITapePeakControl::UpdateImage() {
	if (!mWidth)
		return;

	if (!mhdc || !mhBitmap) {
		HDC hdc = GetDC(mhwnd);

		if (hdc) {
			if (!mhdc)
				mhdc = CreateCompatibleDC(hdc);

			if (!mhBitmap) {
				mhBitmap = CreateCompatibleBitmap(hdc, mWidth, mHeight);
				mbImageDirty = true;
			}

			ReleaseDC(mhwnd, hdc);
		}

		if (!mhdc || !mhBitmap)
			return;
	}

	if (!mhOldBitmap) {
		mhOldBitmap = SelectObject(mhdc, mhBitmap);
		if (!mhOldBitmap)
			return;
	}

	if (mbImageDirty) {
		mbImageDirty = false;

		VDPixmapLayout layout;
		VDMakeBitmapCompatiblePixmapLayout(layout, mWidth, mHeight, nsVDPixmap::kPixFormat_XRGB8888, 0);
		VDPixmapLayoutFlipV(layout);
		mPixmapBuffer.init(layout);

		memset(mPixmapBuffer.base(), 0, mPixmapBuffer.size());

		if (mImageLenSecs > 0) {
			const float secsPerPixel = mImageLenSecs / (float)mWidth;

			vdfastvector<float> buf(mWidth * 4);
			mpImage->ReadPeakMap(secsPerPixel * 0.5f, secsPerPixel, mWidth, buf.data(), buf.data() + mWidth*2);

			float hf = (float)mHeight;

			if (mpImage->GetAudioLength()) {
				DrawPeaks(0, mWidth, 0, hf * 0.5f, buf.data(), 0xFF0000FF);
				DrawPeaks(0, mWidth, hf * 0.5f, hf, buf.data() + mWidth * 2, 0xFFFF0000);
			} else {
				DrawPeaks(0, mWidth, 0, hf, buf.data(), 0xFF0000FF);
			}
		}

		BITMAPINFO bi = {0};
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = mWidth;
		bi.bmiHeader.biHeight = mHeight;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		bi.bmiHeader.biSizeImage = 0;
		bi.bmiHeader.biXPelsPerMeter = 0;
		bi.bmiHeader.biYPelsPerMeter = 0;
		bi.bmiHeader.biClrUsed = 0;
		bi.bmiHeader.biClrImportant = 0;

		::SetDIBitsToDevice(mhdc, 0, 0, mWidth, mHeight, 0, 0, 0, mHeight, mPixmapBuffer.data, &bi, DIB_RGB_COLORS);
	}
}

void ATUITapePeakControl::DrawPeaks(sint32 x1, sint32 x2, float y1, float y2, const float *data, uint32 c) {
	float ymid = 0.5f*(y2 + y1);
	float ydel = 0.5f*(y2 - y1);

	for(sint32 x = x1; x < x2; ++x) {
		float v2 = ymid - ydel*(*data++);
		float v1 = ymid - ydel*(*data++);
		sint32 iy2 = VDCeilToInt(v2 - 0.5f);
		sint32 iy1 = VDCeilToInt(v1 - 0.5f);

		if (iy1 < 0)
			iy1 = 0;

		if (iy2 > mPixmapBuffer.h)
			iy2 = mPixmapBuffer.h;

		if (iy1 != iy2) {
			uint32 *p = vdptroffset((uint32 *)mPixmapBuffer.data, mPixmapBuffer.pitch * iy1) + x;

			for(sint32 y = iy1; y < iy2; ++y) {
				*p = c;

				vdptrstep(p, mPixmapBuffer.pitch);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////

class ATTapeControlDialog : public VDDialogFrameW32 {
public:
	ATTapeControlDialog(ATCassetteEmulator& tape);

public:
	void OnPositionChanged(ATUITapePeakControl *sender, float pos);

protected:
	bool OnLoaded();
	void OnHScroll(uint32 id, int code);
	void UpdateLabelText();
	void AppendTime(VDStringW& s, float t);

	ATCassetteEmulator& mTape;
	VDStringW mLabel;
	float mPos;
	float mLength;
	float mSecondsPerTick;

	vdrefptr<ATUITapePeakControl> mpPeakControl;
	VDDelegate mDelPositionChanged;
};

ATTapeControlDialog::ATTapeControlDialog(ATCassetteEmulator& tape)
	: VDDialogFrameW32(IDD_TAPE_CONTROL)
	, mTape(tape)
{
}

void ATTapeControlDialog::OnPositionChanged(ATUITapePeakControl *sender, float pos) {
	if (mPos != pos) {
		mPos = pos;

		mTape.SeekToTime(mPos);

		if (mSecondsPerTick > 0)
			TBSetValue(IDC_POSITION, VDRoundToInt(pos / mSecondsPerTick));

		UpdateLabelText();
	}
}

bool ATTapeControlDialog::OnLoaded() {
	HWND hwndPeakRect = GetControl(IDC_PEAK_IMAGE);
	if (hwndPeakRect) {
		const vdrect32 r = GetControlPos(IDC_PEAK_IMAGE);

		if (!r.empty()) {
			mpPeakControl = new ATUITapePeakControl(mTape.GetImage());

			::DestroyWindow(hwndPeakRect);

			::CreateWindowEx(WS_EX_CLIENTEDGE, MAKEINTATOM(ATUINativeWindow::Register()), _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP, r.left, r.top, r.width(), r.height(), mhdlg, (HMENU)IDC_PEAK_IMAGE, VDGetLocalModuleHandleW32(),
				static_cast<ATUINativeWindow *>(&*mpPeakControl));

			mpPeakControl->OnPositionChanged() += mDelPositionChanged.Bind(this, &ATTapeControlDialog::OnPositionChanged);
		}
	}

	mPos = mTape.GetPosition();
	mLength = ceilf(mTape.GetLength());

	if (mpPeakControl)
		mpPeakControl->SetPosition(mPos);

	float r = mLength < 1e-5f ? 0.0f : mPos / mLength;
	int ticks = 100000;

	if (mLength < 10000.0f)
		ticks = VDCeilToInt(mLength * 10.0f);

	mSecondsPerTick = mLength / (float)ticks;

	TBSetRange(IDC_POSITION, 0, ticks);
	TBSetValue(IDC_POSITION, VDRoundToInt(r * (float)ticks));
	
	UpdateLabelText();
	SetFocusToControl(IDC_POSITION);
	return true;
}

void ATTapeControlDialog::OnHScroll(uint32 id, int code) {
	float pos = (float)TBGetValue(IDC_POSITION) * mSecondsPerTick;

	if (pos != mPos) {
		mPos = pos;

		mTape.SeekToTime(mPos);

		if (mpPeakControl)
			mpPeakControl->SetPosition(mPos);

		UpdateLabelText();
	}
}

void ATTapeControlDialog::UpdateLabelText() {
	mLabel.clear();
	AppendTime(mLabel, mTape.GetPosition());
	mLabel += L'/';
	AppendTime(mLabel, mTape.GetLength());

	SetControlText(IDC_STATIC_POSITION, mLabel.c_str());
}

void ATTapeControlDialog::AppendTime(VDStringW& s, float t) {
	sint32 ticks = VDRoundToInt32(t*10.0f);
	int minutesWidth = 1;

	if (ticks >= 36000) {
		sint32 hours = ticks / 36000;
		ticks %= 36000;

		s.append_sprintf(L"%d:", hours);
		minutesWidth = 2;
	}

	sint32 minutes = ticks / 600;
	ticks %= 600;

	sint32 seconds = ticks / 10;
	ticks %= 10;

	s.append_sprintf(L"%0*d:%02d.%d", minutesWidth, minutes, seconds, ticks);
}

void ATUIShowTapeControlDialog(VDGUIHandle hParent, ATCassetteEmulator& cassette) {
	ATTapeControlDialog dlg(cassette);

	dlg.ShowDialog(hParent);
}
