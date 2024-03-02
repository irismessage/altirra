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
#include <vd2/system/binary.h>
#include <windows.h>
#include "uienhancedtext.h"
#include "gtia.h"
#include "antic.h"
#include "simulator.h"

class ATUIEnhancedTextEngine : public IATUIEnhancedTextEngine {
	ATUIEnhancedTextEngine(const ATUIEnhancedTextEngine&);
	ATUIEnhancedTextEngine& operator=(const ATUIEnhancedTextEngine&);
public:
	ATUIEnhancedTextEngine();
	~ATUIEnhancedTextEngine();

	void Init(HWND hwnd, ATSimulator *sim);
	void Shutdown();

	void SetFont(const LOGFONTW *font);

	void Update(bool forceInvalidate);
	void Paint(HDC hdc);

protected:
	void Paint(HDC hdc, const bool *lineRedrawFlags);

	HWND	mhwnd;
	HFONT	mTextModeFont;
	HFONT	mTextModeFont2x;
	HFONT	mTextModeFont4x;
	int		mTextCharW;
	int		mTextCharH;

	uint32	mTextLastForeColor;
	uint32	mTextLastBackColor;
	uint32	mTextLastBorderColor;
	int		mTextLastTotalHeight;
	int		mTextLastLineCount;
	uint8	mTextLineMode[30];
	uint8	mTextLastData[30][40];

	ATGTIAEmulator *mpGTIA;
	ATAnticEmulator *mpANTIC;
	ATSimulator *mpSim;
};

ATUIEnhancedTextEngine::ATUIEnhancedTextEngine()
	: mhwnd(NULL)
	, mTextModeFont(NULL)
	, mTextModeFont2x(NULL)
	, mTextModeFont4x(NULL)
	, mTextCharW(16)
	, mTextCharH(16)
	, mTextLastForeColor(0)
	, mTextLastBackColor(0)
	, mTextLastBorderColor(0)
	, mTextLastTotalHeight(-1)
	, mpGTIA(NULL)
	, mpANTIC(NULL)
	, mpSim(NULL)
{
	memset(mTextLineMode, 0, sizeof mTextLineMode);
	memset(mTextLastData, 0, sizeof mTextLastData);
}

ATUIEnhancedTextEngine::~ATUIEnhancedTextEngine() {
	Shutdown();
}

void ATUIEnhancedTextEngine::Init(HWND hwnd, ATSimulator *sim) {
	mhwnd = hwnd;
	mpSim = sim;
	mpGTIA = &sim->GetGTIA();
	mpANTIC = &sim->GetAntic();
}

void ATUIEnhancedTextEngine::Shutdown() {
	SetFont(NULL);
	mhwnd = NULL;
	mpSim = NULL;
	mpGTIA = NULL;
	mpANTIC = NULL;
}

void ATUIEnhancedTextEngine::SetFont(const LOGFONTW *font) {
	if (mTextModeFont) {
		DeleteObject(mTextModeFont);
		mTextModeFont = NULL;
	}

	if (mTextModeFont2x) {
		DeleteObject(mTextModeFont2x);
		mTextModeFont2x = NULL;
	}

	if (mTextModeFont4x) {
		DeleteObject(mTextModeFont4x);
		mTextModeFont4x = NULL;
	}

	if (!font)
		return;

	mTextModeFont = CreateFontIndirectW(font);
	if (!mTextModeFont)
		mTextModeFont = CreateFontW(16, 0, 0, 0, 0, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_DONTCARE | DEFAULT_PITCH, L"Lucida Console");

	mTextCharW = 16;
	mTextCharH = 16;
	if (HDC hdc = GetDC(mhwnd)) {
		HGDIOBJ hOldFont = SelectObject(hdc, mTextModeFont);
		if (hOldFont) {
			TEXTMETRICW tm;
			if (GetTextMetricsW(hdc, &tm)) {
				mTextCharW = tm.tmAveCharWidth;
				mTextCharH = tm.tmHeight;
			}

			SelectObject(hdc, hOldFont);
		}
		ReleaseDC(mhwnd, hdc);
	}

	LOGFONTW logfont2x4x(*font);
	logfont2x4x.lfWidth = mTextCharW * 2;

	mTextModeFont2x = CreateFontIndirectW(&logfont2x4x);
	if (!mTextModeFont2x)
		mTextModeFont2x = CreateFontW(16, mTextCharW * 2, 0, 0, 0, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_DONTCARE | DEFAULT_PITCH, L"Lucida Console");

	logfont2x4x.lfHeight *= 2;
	mTextModeFont4x = CreateFontIndirectW(&logfont2x4x);
	if (!mTextModeFont4x)
		mTextModeFont4x = CreateFontW(32, mTextCharW * 2, 0, 0, 0, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_DONTCARE | DEFAULT_PITCH, L"Lucida Console");

	InvalidateRect(mhwnd, NULL, TRUE);
}

void ATUIEnhancedTextEngine::Update(bool forceInvalidate) {
	COLORREF colorBack = VDSwizzleU32(mpGTIA->GetPlayfieldColor24(2)) >> 8;
	COLORREF colorFore = VDSwizzleU32(mpGTIA->GetPlayfieldColorPF2H()) >> 8;
	COLORREF colorBorder = VDSwizzleU32(mpGTIA->GetBackgroundColor24()) >> 8;

	if (mTextLastBackColor != colorBack) {
		mTextLastBackColor = colorBack;
		forceInvalidate = true;
	}

	if (mTextLastForeColor != colorFore) {
		mTextLastForeColor = colorFore;
		forceInvalidate = true;
	}

	if (mTextLastBorderColor != colorBorder) {
		mTextLastBorderColor = colorBorder;
		forceInvalidate = true;
	}

	// update data from ANTIC
	const ATAnticEmulator::DLHistoryEntry *history = mpANTIC->GetDLHistory();

	int line = 0;
	bool redrawFlags[30] = {false};
	bool linesDirty = false;
	for(int y=8; y<240; ++y) {
		const ATAnticEmulator::DLHistoryEntry& hval = history[y];

		if (!hval.mbValid)
			continue;
		
		const uint8 mode = (hval.mControl & 0x0F);
		
		if (mode != 2 && mode != 6 && mode != 7)
			continue;

		int pfWidth = hval.mDMACTL & 3;

		if (!pfWidth)
			continue;

		int baseWidth = pfWidth == 1 ? 16 : pfWidth == 2 ? 20 : 24;

		uint32 pfAddr = hval.mPFAddress;

		const int width = (mode == 2 ? baseWidth * 2 : baseWidth);
		uint8 *lastData = mTextLastData[line];
		uint8 data[40] = {0};
		for(int i=0; i<width; ++i) {
			uint8 c = mpSim->DebugAnticReadByte(pfAddr + i);

			data[i] = c;
		}

		if (mode != mTextLineMode[line])
			forceInvalidate = true;

		if (forceInvalidate || line >= mTextLastLineCount || memcmp(data, lastData, width)) {
			mTextLineMode[line] = mode;
			memcpy(lastData, data, 40);
			redrawFlags[line] = true;
			linesDirty = true;
		}

		++line;
	}

	mTextLastLineCount = line;

	if (forceInvalidate || linesDirty) {
		if (HDC hdc = GetDC(mhwnd)) {
			Paint(hdc, forceInvalidate ? NULL : redrawFlags);
			ReleaseDC(mhwnd, hdc);
		}
	}
}

void ATUIEnhancedTextEngine::Paint(HDC hdc) {
	Paint(hdc, NULL);
}

void ATUIEnhancedTextEngine::Paint(HDC hdc, const bool *lineRedrawFlags) {
	int saveHandle = SaveDC(hdc);

	if (!saveHandle)
		return;

	const COLORREF colorBack = mTextLastBackColor;
	const COLORREF colorFore = mTextLastForeColor;
	const COLORREF colorBorder = mTextLastBorderColor;

	SetTextAlign(hdc, TA_TOP | TA_LEFT);
	SetBkMode(hdc, OPAQUE);

	RECT rClient;
	GetClientRect(mhwnd, &rClient);

	uint8 lastMode = 0;
	int py = 0;
	for(int line = 0; line < mTextLastLineCount; ++line) {
		uint8 *data = mTextLastData[line];

		static const uint8 kInternalToATASCIIXorTab[4]={
			0x20, 0x60, 0x40, 0x00
		};

		const uint8 mode = mTextLineMode[line];
		int charWidth = (mode == 2 ? mTextCharW : mTextCharW*2);
		int charHeight = (mode != 7 ? mTextCharH : mTextCharH*2);

		if (!lineRedrawFlags || lineRedrawFlags[line]) {
			char buf[41];
			bool inverted[41];

			int N = (mode == 2 ? 40 : 20);

			for(int i=0; i<N; ++i) {
				uint8 c = data[i];

				c ^= kInternalToATASCIIXorTab[(c >> 5) & 3];

				if ((uint8)((c & 0x7f) - 0x20) >= 0x5f)
					c = (c & 0x80) + 0x20;

				buf[i] = c & 0x7f;
				inverted[i] = (c & 0x80) != 0;
			}

			buf[N] = 0;
			inverted[N] = !inverted[N-1];

			if (lastMode != mode) {
				lastMode = mode;

				switch(mode) {
					case 2:
					default:
						SelectObject(hdc, mTextModeFont);
						break;

					case 6:
						SelectObject(hdc, mTextModeFont2x);
						break;

					case 7:
						SelectObject(hdc, mTextModeFont4x);
						break;
				}
			}

			int x = 0;
			while(x < N) {
				bool invertSpan = inverted[x];
				int xe = x + 1;

				while(inverted[xe] == invertSpan)
					++xe;

				if (invertSpan) {
					SetTextColor(hdc, colorBack);
					SetBkColor(hdc, colorFore);
				} else {
					SetTextColor(hdc, colorFore);
					SetBkColor(hdc, colorBack);
				}

				TextOutA(hdc, charWidth * x, py, buf + x, xe - x);

				x = xe;
			}

			RECT rClear = { charWidth * x, py, rClient.right, py + charHeight };
			SetBkColor(hdc, colorBorder);
			ExtTextOutW(hdc, 0, py, ETO_OPAQUE, &rClear, L"", 0, NULL);
		}

		py += charHeight;
	}

	if (mTextLastTotalHeight != py || !lineRedrawFlags) {
		mTextLastTotalHeight = py;

		RECT rClear = { 0, py, rClient.right, rClient.bottom };
		SetBkColor(hdc, colorBorder);
		ExtTextOutW(hdc, 0, py, ETO_OPAQUE, &rClear, L"", 0, NULL);
	}

	RestoreDC(hdc, saveHandle);
}

IATUIEnhancedTextEngine *ATUICreateEnhancedTextEngine() {
	return new ATUIEnhancedTextEngine;
}
