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
#include <at/atcore/devicevideo.h>
#include <windows.h>
#include "uienhancedtext.h"
#include <at/atui/uiwidget.h>
#include "gtia.h"
#include "antic.h"
#include "simulator.h"
#include "virtualscreen.h"

class ATUIEnhancedTextEngine final : public IATUIEnhancedTextEngine, public IATDeviceVideoOutput {
	ATUIEnhancedTextEngine(const ATUIEnhancedTextEngine&) = delete;
	ATUIEnhancedTextEngine& operator=(const ATUIEnhancedTextEngine&) = delete;
public:
	ATUIEnhancedTextEngine();
	~ATUIEnhancedTextEngine();

	void Init(IATUIEnhancedTextOutput *output, ATSimulator *sim);
	void Shutdown();

	bool IsRawInputEnabled() const;
	IATDeviceVideoOutput *GetVideoOutput();

	void SetFont(const LOGFONTW *font);

	void OnSize(uint32 w, uint32 h);
	void OnChar(int ch);
	bool OnKeyDown(uint32 keyCode);
	bool OnKeyUp(uint32 keyCode);

	void Paste(const char *s, size_t);

	void Update(bool forceInvalidate);

public:
	void Tick(uint32 hz300ticks) override;
	void UpdateFrame() override;
	const VDPixmap& GetFrameBuffer() override;
	const ATDeviceVideoInfo& GetVideoInfo() override;
	vdpoint32 PixelToCaretPos(const vdpoint32& pixelPos) override;
	vdrect32 CharToPixelRect(const vdrect32& r) override;
	int ReadRawText(uint8 *dst, int x, int y, int n) override;
	uint32 GetActivityCounter() override;

protected:
	void Paint(const bool *lineRedrawFlags);
	void PaintHWMode(const bool *lineRedrawFlags);
	void PaintSWMode(const bool *lineRedrawFlags);

	void AddToHistory(const char *s);
	void OnInputReady();
	void ProcessPastedInput();

	IATUIEnhancedTextOutput *mpOutput = nullptr;
	HDC		mhdc = nullptr;
	HBITMAP	mhBitmap = nullptr;
	void	*mpBitmap = nullptr;
	HGDIOBJ	mhOldBitmap = nullptr;
	int		mBitmapWidth = 0;
	int		mBitmapHeight = 0;
	HFONT	mTextModeFont = nullptr;
	HFONT	mTextModeFont2x = nullptr;
	HFONT	mTextModeFont4x = nullptr;
	int		mTextCharW = 16;
	int		mTextCharH = 16;

	uint32	mTextLastForeColor = 0;
	uint32	mTextLastBackColor = 0;
	uint32	mTextLastBorderColor = 0;
	int		mTextLastTotalHeight = 0;
	int		mTextLastLineCount = 0;
	uint8	mTextLineMode[30] = {};
	uint8	mTextLastData[30][40] = {};

	WCHAR	mGlyphIndices[94] = {};

	vdfastvector<WCHAR> mLineBuffer;
	vdfastvector<INT> mGlyphOffsets;

	vdfastvector<char> mInputBuffer;
	uint32 mInputPos = 0;
	uint32 mInputHistIdx = 0;

	vdfastvector<char> mHistoryBuffer;
	vdfastvector<uint8> mLastScreen;
	bool mbLastScreenValid = false;
	bool mbLastInputLineDirty = false;
	uint32 mLastCursorX = 0;
	uint32 mLastCursorY = 0;

	vdfastdeque<uint8> mPasteBuffer;

	ATGTIAEmulator *mpGTIA = nullptr;
	ATAnticEmulator *mpANTIC = nullptr;
	ATSimulator *mpSim = nullptr;

	ATDeviceVideoInfo mVideoInfo = {};
	VDPixmap mFrameBuffer = {};
};

ATUIEnhancedTextEngine::ATUIEnhancedTextEngine() {
	mVideoInfo.mPixelAspectRatio = 1.0f;
	mVideoInfo.mbSignalValid = true;
	mVideoInfo.mHorizScanRate = 15735.0f;
	mVideoInfo.mVertScanRate = 59.94f;
	
	memset(mTextLineMode, 0, sizeof mTextLineMode);
	memset(mTextLastData, 0, sizeof mTextLastData);
}

ATUIEnhancedTextEngine::~ATUIEnhancedTextEngine() {
	Shutdown();
}

void ATUIEnhancedTextEngine::Init(IATUIEnhancedTextOutput *output, ATSimulator *sim) {
	mpOutput = output;
	mpSim = sim;
	mpGTIA = &sim->GetGTIA();
	mpANTIC = &sim->GetAntic();

	if (HDC hdc = GetDC(NULL)) {
		mhdc = CreateCompatibleDC(hdc);

		mhOldBitmap = SelectObject(mhdc, mhBitmap);
	}

	auto *vs = sim->GetVirtualScreenHandler();
	if (vs)
		vs->SetReadyCallback([this]() { OnInputReady(); });
}

void ATUIEnhancedTextEngine::Shutdown() {
	SetFont(NULL);

	if (mhdc) {
		if (mhOldBitmap) {
			SelectObject(mhdc, mhOldBitmap);
			mhOldBitmap = nullptr;
		}

		DeleteDC(mhdc);
		mhdc = nullptr;
	}

	if (mhBitmap) {
		DeleteObject(mhBitmap);
		mhBitmap = nullptr;
		mpBitmap = nullptr;
	}

	mpSim = NULL;
	mpGTIA = NULL;
	mpANTIC = NULL;
}

bool ATUIEnhancedTextEngine::IsRawInputEnabled() const {
	IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();
	
	return !vs || vs->IsRawInputActive();
}

IATDeviceVideoOutput *ATUIEnhancedTextEngine::GetVideoOutput() {
	return this;
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

	HGDIOBJ hOldFont = SelectObject(mhdc, mTextModeFont);
	if (hOldFont) {
		TEXTMETRICW tm;
		if (GetTextMetricsW(mhdc, &tm)) {
			mTextCharW = tm.tmAveCharWidth;
			mTextCharH = tm.tmHeight;
		}

		for(int i=0; i<94; ++i) {
			const WCHAR ch = (WCHAR)(33 + i);

			GCP_RESULTSW results = {sizeof(GCP_RESULTSW)};
			results.lpGlyphs = &mGlyphIndices[i];
			results.nGlyphs = 1;

			if (!GetCharacterPlacementW(mhdc, &ch, 1, 0, &results, 0))
				mGlyphIndices[i] = -1;
		}

		SelectObject(mhdc, hOldFont);
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
}

void ATUIEnhancedTextEngine::OnSize(uint32 w, uint32 h) {
	if (!w || !h)
		return;

	if (w > 32767)
		w = 32767;

	if (h > 32767)
		h = 32767;

	uint32 charW;
	uint32 charH;

	if (mpSim->GetVirtualScreenHandler()) {
		charW = (uint32)((sint32)w / mTextCharW);
		charH = (uint32)((sint32)h / mTextCharH);

		if (charW < 40)
			charW = 40;

		if (charH < 24)
			charH = 24;

		if (charW > 255)
			charW = 255;

		if (charH > 255)
			charH = 255;
	} else {
		charW = 40;
		charH = 30;
	}

	uint32 adjustedW = (uint32)(charW * mTextCharW);
	uint32 adjustedH = (uint32)(charH * mTextCharH);

	if (mBitmapWidth == adjustedW && mBitmapHeight == adjustedH)
		return;

	mBitmapWidth = adjustedW;
	mBitmapHeight = adjustedH;

	if (mhOldBitmap) {
		SelectObject(mhdc, mhOldBitmap);
		mhOldBitmap = nullptr;
	}

	if (mhBitmap) {
		DeleteObject(mhBitmap);
		mhBitmap = nullptr;
		mpBitmap = nullptr;
	}

	BITMAPINFO bitmapInfo = {};

	bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bitmapInfo.bmiHeader.biWidth = adjustedW;
	bitmapInfo.bmiHeader.biHeight = adjustedH;
	bitmapInfo.bmiHeader.biPlanes = 1;
	bitmapInfo.bmiHeader.biBitCount = 32;
	bitmapInfo.bmiHeader.biSizeImage = adjustedW * adjustedH * 4;

	++mVideoInfo.mFrameBufferLayoutChangeCount;
	++mVideoInfo.mFrameBufferChangeCount;

	mFrameBuffer = VDPixmap();

	mhBitmap = CreateDIBSection(mhdc, &bitmapInfo, DIB_RGB_COLORS, &mpBitmap, NULL, 0);

	if (!mhBitmap) {
		mBitmapWidth = 0;
		mBitmapHeight = 0;
		return;
	}

	mhOldBitmap = SelectObject(mhdc, mhBitmap);

	mFrameBuffer.data = (char *)mpBitmap + adjustedW * 4 * (adjustedH - 1);
	mFrameBuffer.w = adjustedW;
	mFrameBuffer.h = adjustedH;
	mFrameBuffer.format = nsVDPixmap::kPixFormat_XRGB8888;
	mFrameBuffer.pitch = -(ptrdiff_t)adjustedW * 4;

	mVideoInfo.mTextColumns = charW;
	mVideoInfo.mTextRows = charH;
	mVideoInfo.mDisplayArea.set(0, 0, adjustedW, adjustedH);

	IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();
	if (vs)
		vs->Resize(charW, charH);

	mbLastScreenValid = false;
	++mVideoInfo.mFrameBufferChangeCount;

	if (mpOutput)
		mpOutput->InvalidateTextOutput();
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

void ATUIEnhancedTextEngine::Paste(const char *s, size_t len) {
	char skipNext = 0;

	while(len--) {
		char c = *s++;

		if (c == skipNext) {
			skipNext = 0;
			continue;
		}

		if (c == '\r' || c == '\n') {
			skipNext = c ^ ('\r' ^ '\n');
			c = '\n';
		}

		mPasteBuffer.push_back((uint8)c);
	}

	ProcessPastedInput();
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

		mVideoInfo.mBorderColor = VDSwizzleU32(colorBorder) >> 8;

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

		Paint(lineFlagsPtr);
		return;
	}

	// update data from ANTIC
	const ATAnticEmulator::DLHistoryEntry *history = mpANTIC->GetDLHistory();

	if (!mbLastScreenValid) {
		mbLastScreenValid = true;
		forceInvalidate = true;
	}

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
		uint8 data[48] = {0};
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
		Paint(forceInvalidate ? NULL : redrawFlags);
	}
}

void ATUIEnhancedTextEngine::Tick(uint32 hz300ticks) {
}

void ATUIEnhancedTextEngine::UpdateFrame() {
}

const VDPixmap& ATUIEnhancedTextEngine::GetFrameBuffer() {
	return mFrameBuffer;
}

const ATDeviceVideoInfo& ATUIEnhancedTextEngine::GetVideoInfo() {
	return mVideoInfo;
}

vdpoint32 ATUIEnhancedTextEngine::PixelToCaretPos(const vdpoint32& pixelPos) {
	if (pixelPos.y < 0)
		return vdpoint32(0, 0);

	if (pixelPos.y >= mVideoInfo.mDisplayArea.bottom)
		return vdpoint32(mVideoInfo.mTextColumns - 1, mVideoInfo.mTextRows - 1);

	return vdpoint32(
		pixelPos.x < 0 ? 0
		: pixelPos.x >= mVideoInfo.mDisplayArea.right ? mVideoInfo.mTextColumns - 1
		: pixelPos.x / mTextCharW,
		pixelPos.y / mTextCharH);
}

vdrect32 ATUIEnhancedTextEngine::CharToPixelRect(const vdrect32& r) {
	return vdrect32(r.left * mTextCharW, r.top * mTextCharH, r.right * mTextCharW, r.bottom * mTextCharH);
}

int ATUIEnhancedTextEngine::ReadRawText(uint8 *dst, int x, int y, int n) {
	IATVirtualScreenHandler *const vs = mpSim->GetVirtualScreenHandler();

	if (!vs)
		return 0;

	return vs->ReadRawText(dst, x, y, n);
}

uint32 ATUIEnhancedTextEngine::GetActivityCounter() {
	return 0;
}

void ATUIEnhancedTextEngine::Paint(const bool *lineRedrawFlags) {
	int saveHandle = SaveDC(mhdc);

	if (!saveHandle)
		return;

	if (mpSim->GetVirtualScreenHandler())
		PaintSWMode(lineRedrawFlags);
	else
		PaintHWMode(lineRedrawFlags);

	RestoreDC(mhdc, saveHandle);

	++mVideoInfo.mFrameBufferChangeCount;

	if (mpOutput)
		mpOutput->InvalidateTextOutput();
}

void ATUIEnhancedTextEngine::PaintHWMode(const bool *lineRedrawFlags) {
	const COLORREF colorBack = mTextLastBackColor;
	const COLORREF colorFore = mTextLastForeColor;
	const COLORREF colorBorder = mTextLastBorderColor;

	SetTextAlign(mhdc, TA_TOP | TA_LEFT);
	SetBkMode(mhdc, OPAQUE);

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
					c = (c & 0x80) + '.';

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
						SelectObject(mhdc, mTextModeFont);
						break;

					case 6:
						SelectObject(mhdc, mTextModeFont2x);
						break;

					case 7:
						SelectObject(mhdc, mTextModeFont4x);
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
					SetTextColor(mhdc, colorBack);
					SetBkColor(mhdc, colorFore);
				} else {
					SetTextColor(mhdc, colorFore);
					SetBkColor(mhdc, colorBack);
				}

				TextOutA(mhdc, charWidth * x, py, buf + x, xe - x);

				x = xe;
			}

			RECT rClear = { charWidth * x, py, mBitmapWidth, py + charHeight };
			SetBkColor(mhdc, colorBorder);
			ExtTextOutW(mhdc, 0, py, ETO_OPAQUE, &rClear, L"", 0, NULL);
		}

		py += charHeight;
	}

	if (mTextLastTotalHeight != py || !lineRedrawFlags) {
		mTextLastTotalHeight = py;

		RECT rClear = { 0, py, mBitmapWidth, mBitmapHeight };
		SetBkColor(mhdc, colorBorder);
		ExtTextOutW(mhdc, 0, py, ETO_OPAQUE, &rClear, L"", 0, NULL);
	}
}

void ATUIEnhancedTextEngine::PaintSWMode(const bool *lineRedrawFlags) {
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

	SetTextAlign(mhdc, TA_TOP | TA_LEFT);
	SetBkMode(mhdc, OPAQUE);

	SelectObject(mhdc, mTextModeFont);

	uint32 cursorX, cursorY;
	if (!vs->GetCursorInfo(cursorX, cursorY)) {
		cursorX = (uint32)0-1;
		cursorY = (uint32)0-1;
	}

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
				c = (c & 0x80) + '.';

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

					SetTextColor(mhdc, colorBack);
					SetBkColor(mhdc, colorFore);
				}
			} else {
				if (lastInvert != 0) {
					lastInvert = 0;

					SetTextColor(mhdc, colorFore);
					SetBkColor(mhdc, colorBack);
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

			ExtTextOutW(mhdc, px, py, ETO_GLYPH_INDEX | ETO_OPAQUE, &rLine, line, countOut, glyphOffsets);

			x2 = xe;
		}
	}

	if (!lineRedrawFlags) {
		SetBkColor(mhdc, colorBorder);

		RECT rClear = { mTextCharW * w, 0, mBitmapWidth, py };
		ExtTextOutW(mhdc, 0, py, ETO_OPAQUE, &rClear, L"", 0, NULL);

		RECT rClear2 = { 0, py, mBitmapWidth, mBitmapHeight };
		ExtTextOutW(mhdc, 0, py, ETO_OPAQUE, &rClear2, L"", 0, NULL);
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

void ATUIEnhancedTextEngine::OnInputReady() {
	ProcessPastedInput();
}

void ATUIEnhancedTextEngine::ProcessPastedInput() {
	auto *vs = mpSim->GetVirtualScreenHandler();
	if (!vs || !vs->IsReadyForInput())
		return;

	while(!mPasteBuffer.empty()) {
		char c = mPasteBuffer.front();
		mPasteBuffer.pop_front();

		if (c == '\n') {
			OnKeyDown(kATUIVK_Return);
			break;
		} else if (c >= 0x20 && c < 0x7F)
			OnChar(c);
	}
}

///////////////////////////////////////////////////////////////////////////

IATUIEnhancedTextEngine *ATUICreateEnhancedTextEngine() {
	return new ATUIEnhancedTextEngine;
}
