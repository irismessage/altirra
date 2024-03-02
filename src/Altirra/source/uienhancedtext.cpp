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
#include "uiwidget.h"
#include "gtia.h"
#include "antic.h"
#include "simulator.h"
#include "virtualscreen.h"

class ATUIEnhancedTextEngine : public IATUIEnhancedTextEngine {
	ATUIEnhancedTextEngine(const ATUIEnhancedTextEngine&);
	ATUIEnhancedTextEngine& operator=(const ATUIEnhancedTextEngine&);
public:
	ATUIEnhancedTextEngine();
	~ATUIEnhancedTextEngine();

	void Init(HWND hwnd, ATSimulator *sim);
	void Shutdown();

	bool IsRawInputEnabled() const;

	void SetFont(const LOGFONTW *font);

	void OnSize(uint32 w, uint32 h);
	void OnChar(int ch);
	bool OnKeyDown(uint32 keyCode);
	bool OnKeyUp(uint32 keyCode);

	void Update(bool forceInvalidate);
	void Paint(HDC hdc);

protected:
	void Paint(HDC hdc, const bool *lineRedrawFlags);
	void PaintHWMode(HDC hdc, const bool *lineRedrawFlags);
	void PaintSWMode(HDC hdc, const bool *lineRedrawFlags);

	void AddToHistory(const char *s);

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

	WCHAR	mGlyphIndices[94];

	vdfastvector<WCHAR> mLineBuffer;
	vdfastvector<INT> mGlyphOffsets;

	vdfastvector<char> mInputBuffer;
	uint32 mInputPos;
	uint32 mInputHistIdx;

	vdfastvector<char> mHistoryBuffer;
	vdfastvector<uint8> mLastScreen;
	bool mbLastScreenValid;
	bool mbLastInputLineDirty;
	uint32 mLastCursorX;
	uint32 mLastCursorY;

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
	, mInputPos(0)
	, mbLastScreenValid(false)
	, mbLastInputLineDirty(false)
	, mLastCursorX(0)
	, mLastCursorY(0)
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

	RECT r;
	if (GetClientRect(hwnd, &r))
		OnSize(r.right, r.bottom);
}

void ATUIEnhancedTextEngine::Shutdown() {
	SetFont(NULL);
	mhwnd = NULL;
	mpSim = NULL;
	mpGTIA = NULL;
	mpANTIC = NULL;
}

bool ATUIEnhancedTextEngine::IsRawInputEnabled() const {
	IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();
	
	return !vs || vs->IsRawInputActive();
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

	memset(mGlyphIndices, 0, sizeof mGlyphIndices);

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

			for(int i=0; i<94; ++i) {
				const WCHAR ch = (WCHAR)(33 + i);

				GCP_RESULTSW results = {sizeof(GCP_RESULTSW)};
				results.lpGlyphs = &mGlyphIndices[i];
				results.nGlyphs = 1;

				if (!GetCharacterPlacementW(hdc, &ch, 1, 0, &results, 0))
					mGlyphIndices[i] = -1;
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

	RECT r;
	if (GetClientRect(mhwnd, &r))
		OnSize(r.right, r.bottom);
}

void ATUIEnhancedTextEngine::OnSize(uint32 w, uint32 h) {
	if (!w || !h)
		return;

	w /= mTextCharW;
	h /= mTextCharH;

	if (!w)
		w = 1;

	if (!h)
		h = 1;

	if (w > 255)
		w = 255;

	if (h > 255)
		h = 255;

	IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();
	if (vs)
		vs->Resize(w, h);

	mbLastScreenValid = false;
	InvalidateRect(mhwnd, NULL, TRUE);
}

void ATUIEnhancedTextEngine::OnChar(int ch) {
	if (ch >= 0x20 && ch < 0x7F) {
		IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();
		if (vs) {
			if (vs->GetShiftLockState()) {
				if (ch >= 'a' && ch <= 'z')
					ch &= 0xdf;
			}
		}

		mInputBuffer.insert(mInputBuffer.begin() + mInputPos, (char)ch);
		++mInputPos;
	}
}

bool ATUIEnhancedTextEngine::OnKeyDown(uint32 keyCode) {
	if (IsRawInputEnabled())
		return false;

	switch(keyCode) {
		case kATUIVK_Left:
			if (mInputPos) {
				--mInputPos;
				mbLastInputLineDirty = true;
			}
			return true;

		case kATUIVK_Right:
			if (mInputPos < mInputBuffer.size()) {
				++mInputPos;
				mbLastInputLineDirty = true;
			}
			return true;

		case kATUIVK_Up:
			if (mInputHistIdx < mHistoryBuffer.size()) {
				const char *base = mHistoryBuffer.data();
				const char *s = (mHistoryBuffer.end() - mInputHistIdx - 1);

				while(s > base && s[-1])
					--s;

				uint32 len = (uint32)strlen(s);
				mInputHistIdx += len + 1;

				mInputBuffer.assign(s, s + len);
				mInputPos = (uint32)mInputBuffer.size();
				mbLastInputLineDirty = true;
			}

			return true;

		case kATUIVK_Down:
			if (mInputHistIdx) {
				mInputHistIdx -= (1 + strlen(&*(mHistoryBuffer.end() - mInputHistIdx)));

				if (mInputHistIdx) {
					const char *s = &*(mHistoryBuffer.end() - mInputHistIdx);

					mInputBuffer.assign(s, s + strlen(s));
				} else {
					mInputBuffer.clear();
				}

				mInputPos = (uint32)mInputBuffer.size();
				mbLastInputLineDirty = true;
			}
			return true;

		case kATUIVK_Back:
			if (mInputPos) {
				--mInputPos;
				mInputBuffer.erase(mInputBuffer.begin() + mInputPos);
				mbLastInputLineDirty = true;
			}
			return true;

		case kATUIVK_Delete:
			if (mInputPos < mInputBuffer.size()) {
				mInputBuffer.erase(mInputBuffer.begin() + mInputPos);
				mbLastInputLineDirty = true;
			}
			return true;

		case kATUIVK_Return:
			{
				IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();

				if (vs) {
					mInputBuffer.push_back(0);
					vs->PushLine(mInputBuffer.data());
					AddToHistory(mInputBuffer.data());

					mInputBuffer.clear();
					mInputPos = 0;
					mInputHistIdx = 0;
					mbLastInputLineDirty = true;
				}
			}
			return true;

		case kATUIVK_CapsLock:
			{
				IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();

				if (vs) {
					if (GetKeyState(VK_SHIFT) < 0) {
						if (GetKeyState(VK_CONTROL) < 0) {
							// ignore Ctrl+Shift+CapsLock
						} else {
							vs->SetShiftControlLockState(true, false);
						}
					} else {
						if (GetKeyState(VK_CONTROL) < 0) {
							vs->SetShiftControlLockState(false, true);
						} else {
							if (vs->GetShiftLockState() || vs->GetControlLockState())
								vs->SetShiftControlLockState(false, false);
							else
								vs->SetShiftControlLockState(true, false);
						}
					}
				}
			}
			return true;
	}

	return false;
}

bool ATUIEnhancedTextEngine::OnKeyUp(uint32 keyCode) {
	if (IsRawInputEnabled())
		return false;

	switch(keyCode) {
		case kATUIVK_Left:
		case kATUIVK_Right:
		case kATUIVK_Up:
		case kATUIVK_Down:
		case kATUIVK_Back:
		case kATUIVK_Delete:
		case kATUIVK_Return:
		case kATUIVK_CapsLock:
			return true;
	}

	return false;
}

void ATUIEnhancedTextEngine::Update(bool forceInvalidate) {
	IATVirtualScreenHandler *const vs = mpSim->GetVirtualScreenHandler();

	if (vs && vs->CheckForBell())
		MessageBeep(MB_ICONASTERISK);

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

	if (vs) {
		bool lineFlags[255] = {false};
		bool *lineFlagsPtr = lineFlags;

		uint32 w, h;
		const uint8 *screen;
		vs->GetScreen(w, h, screen);

		uint32 cursorX, cursorY;
		vs->GetCursorInfo(cursorX, cursorY);

		uint32 n = w * h;
		if (n != mLastScreen.size() || !mbLastScreenValid || forceInvalidate) {
			mLastScreen.resize(n);
			memcpy(mLastScreen.data(), screen, n);

			mbLastScreenValid = true;
			mbLastInputLineDirty = true;
			lineFlagsPtr = NULL;
		} else {
			uint8 *last = mLastScreen.data();

			for(uint32 y=0; y<h; ++y) {
				if (memcmp(last, screen, w)) {
					memcpy(last, screen, w);
					lineFlagsPtr[y] = true;
				}

				last += w;
				screen += w;
			}
		}

		if (cursorX != mLastCursorX || cursorY != mLastCursorY) {
			mbLastInputLineDirty = true;

			if (lineFlagsPtr && mLastCursorY < h)
				lineFlagsPtr[mLastCursorY] = true;
		}

		if (mbLastInputLineDirty) {
			mLastCursorX = cursorX;
			mLastCursorY = cursorY;

			if (cursorY < h && lineFlagsPtr)
				lineFlagsPtr[cursorY] = true;
		}

		if (HDC hdc = GetDC(mhwnd)) {
			Paint(hdc, lineFlagsPtr);
			ReleaseDC(mhwnd, hdc);
		}

		return;
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

	if (mpSim->GetVirtualScreenHandler())
		PaintSWMode(hdc, lineRedrawFlags);
	else
		PaintHWMode(hdc, lineRedrawFlags);

	RestoreDC(hdc, saveHandle);
}

void ATUIEnhancedTextEngine::PaintHWMode(HDC hdc, const bool *lineRedrawFlags) {
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
}

void ATUIEnhancedTextEngine::PaintSWMode(HDC hdc, const bool *lineRedrawFlags) {
	IATVirtualScreenHandler *const vs = mpSim->GetVirtualScreenHandler();
	uint32 w;
	uint32 h;
	const uint8 *screen;

	vs->GetScreen(w, h, screen);

	mGlyphOffsets.resize(w);
	INT *const glyphOffsets = mGlyphOffsets.data();

	mLineBuffer.resize(w + 1);
	mLineBuffer.back() = 0;

	wchar_t *line = mLineBuffer.data();

	const COLORREF colorBack = mTextLastBackColor;
	const COLORREF colorFore = mTextLastForeColor;
	const COLORREF colorBorder = mTextLastBorderColor;

	SetTextAlign(hdc, TA_TOP | TA_LEFT);
	SetBkMode(hdc, OPAQUE);

	RECT rClient = {0};
	GetClientRect(mhwnd, &rClient);

	SelectObject(hdc, mTextModeFont);

	uint32 cursorX, cursorY;
	bool waiting = vs->GetCursorInfo(cursorX, cursorY);

	int py = 0;
	int lastInvert = 2;
	const int charWidth = mTextCharW;
	const int charHeight = mTextCharH;

	uint8 linedata[255];

	for(uint32 y = 0; y < h; ++y, (py += charHeight), (screen += w)) {
		if (lineRedrawFlags && !lineRedrawFlags[y])
			continue;

		for(uint32 x = 0; x < w; ++x) {
			uint8 c = screen[x];

			linedata[x] = c;
		}

		if (cursorY == y) {
			uint32 limit = cursorX + mInputBuffer.size();

			if (limit > w)
				limit = w;

			for(uint32 x = cursorX; x < limit; ++x)
				linedata[x] = (uint8)mInputBuffer[x - cursorX];

			uint32 cx = cursorX + mInputPos;

			if (cx < w)
				linedata[cx] ^= 0x80;
		}

		for(uint32 x = 0; x < w; ++x) {
			uint8 c = linedata[x];

			if ((uint8)((c & 0x7f) - 0x20) >= 0x5f)
				c = (c & 0x80) + 0x20;

			line[x] = (wchar_t)c;
		}

		uint32 x2 = 0;
		while(x2 < w) {
			wchar_t invertSpan = line[x2];
			uint32 xe = x2 + 1;

			while(xe < w && !((line[xe] ^ invertSpan) & 0x80))
				++xe;

			if (invertSpan & 0x80) {
				if (lastInvert != 1) {
					lastInvert = 1;

					SetTextColor(hdc, colorBack);
					SetBkColor(hdc, colorFore);
				}
			} else {
				if (lastInvert != 0) {
					lastInvert = 0;

					SetTextColor(hdc, colorFore);
					SetBkColor(hdc, colorBack);
				}
			}

			uint32 countOut = 0;
			uint32 glyphPos = 0;
			uint32 px = charWidth * x2;
			const RECT rLine = { px, py, charWidth * xe, py + mTextCharH };
			for(uint32 x3 = x2; x3 < xe; ++x3) {
				uint32 idx = (uint32)((line[x3] & 0x7f) - 33);

				if (idx < 94) {
					WCHAR glyphIdx = mGlyphIndices[idx];

					if (glyphIdx >= 0) {
						line[countOut] = glyphIdx;

						if (!countOut)
							px += glyphPos;
						else
							glyphOffsets[countOut - 1] = glyphPos;

						glyphPos = 0;
						++countOut;
					}
				}

				glyphPos += mTextCharW;
			}

			ExtTextOutW(hdc, px, py, ETO_GLYPH_INDEX | ETO_OPAQUE, &rLine, line, countOut, glyphOffsets);

			x2 = xe;
		}
	}

	if (!lineRedrawFlags) {
		SetBkColor(hdc, colorBorder);

		RECT rClear = { mTextCharW * w, 0, rClient.right, py };
		ExtTextOutW(hdc, 0, py, ETO_OPAQUE, &rClear, L"", 0, NULL);

		RECT rClear2 = { 0, py, rClient.right, rClient.bottom };
		ExtTextOutW(hdc, 0, py, ETO_OPAQUE, &rClear2, L"", 0, NULL);
	}
}

void ATUIEnhancedTextEngine::AddToHistory(const char *s) {
	size_t len = strlen(s);
	size_t hsize = mHistoryBuffer.size();

	if (hsize + len > 4096) {
		if (len >= 4096) {
			mHistoryBuffer.clear();
		} else {
			size_t hreduce = hsize + len - 4096;
			size_t cutidx = 0;

			while(cutidx < hreduce) {
				while(mHistoryBuffer[cutidx++])
					;
			}

			mHistoryBuffer.erase(mHistoryBuffer.begin(), mHistoryBuffer.begin() + cutidx);
		}
	}

	mHistoryBuffer.insert(mHistoryBuffer.end(), s, s + len + 1);
}

///////////////////////////////////////////////////////////////////////////

IATUIEnhancedTextEngine *ATUICreateEnhancedTextEngine() {
	return new ATUIEnhancedTextEngine;
}
