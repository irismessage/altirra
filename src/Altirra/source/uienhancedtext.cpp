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
#include <vd2/system/binary.h>
#include <vd2/system/color.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/resample.h>
#include <at/atcore/devicevideo.h>
#include <windows.h>
#include "uienhancedtext.h"
#include <at/atcore/ksyms.h>
#include <at/atui/uiwidget.h>
#include "antic.h"
#include "gtia.h"
#include "oshelper.h"
#include "resource.h"
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

	void OnSize(uint32 w, uint32 h) override;
	void OnChar(int ch) override;
	bool OnKeyDown(uint32 keyCode) override;
	bool OnKeyUp(uint32 keyCode) override;

	void Paste(const wchar_t *s, size_t);

	void Update(bool forceInvalidate);

public:
	const char *GetName() const override;
	const wchar_t *GetDisplayName() const override;
	void Tick(uint32 hz300ticks) override;
	void UpdateFrame() override;
	const VDPixmap& GetFrameBuffer() override;
	const ATDeviceVideoInfo& GetVideoInfo() override;
	vdpoint32 PixelToCaretPos(const vdpoint32& pixelPos) override;
	vdrect32 CharToPixelRect(const vdrect32& r) override;
	int ReadRawText(uint8 *dst, int x, int y, int n) override;
	uint32 GetActivityCounter() override;

protected:
	bool OnLineInputKeyDown(uint32 keyCode);

	void Paint(const bool *lineRedrawFlags);
	void PaintHWMode(const bool *lineRedrawFlags);
	void PaintSWMode(const bool *lineRedrawFlags);

	void AddToHistory(const char *s);
	void OnInputReady();
	void OnVirtualScreenResized();
	void OnPassThroughChanged();
	void ProcessPastedInput();
	void ResetAttractTimeout();
	void UpdateFont();
	void ClearFont();
	vdsize32 ComputeDesiredFontSize() const;
	void InvalidateFontSizeCache();

	class CharResampler;

	static constexpr int kMinFontSize = 4;
	static constexpr int kMaxFontSize = 255;

	IATUIEnhancedTextOutput *mpOutput = nullptr;
	bool	mbLineInputEnabled = false;

	HDC		mhdc = nullptr;
	HBITMAP	mhBitmap = nullptr;
	void	*mpBitmap = nullptr;
	HGDIOBJ	mhOldBitmap = nullptr;
	int		mBitmapWidth = 0;
	int		mBitmapHeight = 0;
	int		mTextModeFontHeight = 0;
	HFONT	mTextModeFont = nullptr;
	HFONT	mTextModeFont2x = nullptr;
	HFONT	mTextModeFont4x = nullptr;
	int		mTextCharW = 16;
	int		mTextCharH = 16;

	bool	mbInverseEnabled = true;

	sint32	mViewportWidth = 0;
	sint32	mViewportHeight = 0;

	uint32	mTextLastForeColor = 0;
	uint32	mTextLastBackColor = 0;
	uint32	mTextLastPFColors[4] = {};
	uint32	mTextLastBorderColor = 0;
	int		mTextLastTotalHeight = 0;
	int		mTextLastLineCount = 0;
	uint8	mTextLineMode[30] = {};
	uint8	mTextLineCHBASE[30] = {};
	uint8	mTextLastData[30][40] = {};

	static constexpr WCHAR kInvalidGlyphIndex = -1;
	static constexpr WCHAR kEmptyGlyphIndex = -2;
	WCHAR	mDefaultGlyphIndices[128] = {};
	WCHAR	mCustomGlyphIndices[128] = {};

	uint16	mGlyphLookup[3][128] = {};

	// Only used in HW mode.
	sint32	mActiveLineYStart[31] = {};
	uint32	mActiveLineCount = 0;

	vdfastvector<WCHAR> mLineBuffer;
	vdfastvector<INT> mGlyphOffsets;

	vdfastvector<char> mInputBuffer;
	uint32 mInputPos = 0;
	uint32 mInputHistIdx = 0;

	vdfastvector<char> mHistoryBuffer;
	vdfastvector<uint8> mLastScreen;
	bool mbLastScreenValid = false;
	bool mbLastInputLineDirty = false;
	bool mbLastCursorPresent = false;
	uint32 mLastCursorX = 0;
	uint32 mLastCursorY = 0;

	bool mbUsingCustomFont = false;

	struct PendingSpecialChar {
		uint8 mChar;
		uint8 mX;
		uint8 mY;
	};

	vdfastvector<PendingSpecialChar> mPendingSpecialChars;
	uint32 mLastGammaTableForeColor = 0;
	uint32 mLastGammaTableBackColor = 0;

	vdfastdeque<wchar_t> mPasteBuffer;

	ATGTIAEmulator *mpGTIA = nullptr;
	ATAnticEmulator *mpANTIC = nullptr;
	ATSimulator *mpSim = nullptr;

	LOGFONT mBaseFontDesc {};
	vdsize32 mFontSizeCache[256];

	ATDeviceVideoInfo mVideoInfo = {};
	VDPixmap mFrameBuffer = {};

	VDPixmapBuffer mRescaledDefaultFont;
	VDPixmapBuffer mRescaledCustomFont;
	uint8 mRawDefaultFont[1024] {};
	uint8 mCurrentCustomFont[1024] {};
	uint8 mMirroredCustomFont[1024] {};
	uint32 mGammaTable[256] {};

	static const uint8 kInternalToATASCIIXorTab[4];

	static constexpr size_t kRawDefaultFontHashBuckets = 2111;
	static const sint8 kRawDefaultFontHashTable[kRawDefaultFontHashBuckets];
};

////////////////////////////////////////////////////////////////////////////////

class ATUIEnhancedTextEngine::CharResampler {
public:
	CharResampler(uint32 charWidth, uint32 charHeight);

	void ResampleChar(VDPixmap& dst, uint8 ch, const uint8 rawChar[8]);

private:
	vdautoptr<IVDPixmapResampler> mpResampler;
	VDPixmapBuffer mExpand8x8Buffer;

	const uint32 mCharWidth;
	const uint32 mCharHeight;
};

ATUIEnhancedTextEngine::CharResampler::CharResampler(uint32 charWidth, uint32 charHeight)
	: mpResampler(VDCreatePixmapResampler())
	, mCharWidth(charWidth)
	, mCharHeight(charHeight)
{
	mpResampler->SetFilters(IVDPixmapResampler::kFilterSharpLinear, IVDPixmapResampler::kFilterSharpLinear, false);
	mpResampler->SetSharpnessFactors(1.5f, 1.5f);
	VDVERIFY(mpResampler->Init(charWidth, charHeight, nsVDPixmap::kPixFormat_Y8_FR, 8, 8, nsVDPixmap::kPixFormat_Y8_FR));

	mExpand8x8Buffer.init(8, 8, nsVDPixmap::kPixFormat_Y8_FR);
}

void ATUIEnhancedTextEngine::CharResampler::ResampleChar(VDPixmap& dst, uint8 ch, const uint8 rawChar[8]) {
	VDPixmap pxdst = VDPixmapClip(dst, 0, mCharHeight * (uint32)ch, mCharWidth, mCharHeight);
	VDPixmap pxsrc {};
	pxsrc.data = (void *)rawChar;
	pxsrc.pitch = 1;
	pxsrc.w = 8;
	pxsrc.h = 8;
	pxsrc.format = nsVDPixmap::kPixFormat_Pal1;

	static constexpr uint32 kPal[2] { 0, 0xFFFFFF };
	pxsrc.palette = kPal;

	VDPixmapBlt(mExpand8x8Buffer, pxsrc);

	mpResampler->Process(pxdst, mExpand8x8Buffer);
}

////////////////////////////////////////////////////////////////////////////////

constexpr uint8 ATUIEnhancedTextEngine::kInternalToATASCIIXorTab[4]={
	0x20, 0x60, 0x40, 0x00
};

constexpr sint8 ATUIEnhancedTextEngine::kRawDefaultFontHashTable[kRawDefaultFontHashBuckets] = {
	// generated by hashfont.py
	 32, -1, -1, -1, -1, -1, -1, -1,  7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 83, -1, 42, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 13, -1,
	 92, -1, -1, -1, -1, -1,113, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, 34, -1, -1, -1, -1,125, -1, -1, 87, -1, -1, -1, -1, 48, -1, -1, -1, -1, 58, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 51,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 75, -1, 66, -1, -1, -1, -1, -1, -1, -1,114, -1, -1, -1, -1, -1, -1, -1, -1, -1, 52,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,100, -1, -1, -1, -1, -1, -1, -1, -1, -1, 86, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,109, -1,  2, -1, -1, -1, -1, 16, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 50, -1, -1, -1, 27, -1, -1, -1, -1, -1, -1, 29, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,102, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 44, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 43, -1, -1, -1, -1, -1, -1,115, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  6, -1, -1, -1, -1, -1, 91, -1,101, -1, -1, -1, -1, -1, -1,104,  5,
	 -1, -1, -1,  4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 82, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 78, -1, 41, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, 74, -1, -1, 63, -1, -1, -1, -1, -1, 72, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 85, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 53, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 47, -1, -1,  9,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 93, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 89, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 28, -1, -1, -1, -1, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 88, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, 25, -1, -1, -1, -1, -1, -1, 95, -1, -1, -1, -1, 61, -1, -1, -1, -1, -1, -1, -1, 31, -1,111,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 15, -1, -1, -1, -1, -1,110, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, 81, -1, -1,  3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 65, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,119, -1, -1, -1, -1, -1, -1, -1, -1, 55,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, 64, -1, -1, -1, -1, 20, -1, 98, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1,  0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,105, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, 22, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,106, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1,126, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 57, -1, -1,
	 39, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 46, -1, -1, -1, -1, -1, -1, -1, 79, -1,
	 -1, -1, 17, -1, 60, -1,  1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1,117, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 10, -1, -1, -1, -1, -1, -1, -1, 70, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,107, -1, -1,121, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 68, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 94, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 45, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 33, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 21, -1, -1, -1, -1, -1, -1, 56, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, 97, 35, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,123, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 26, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, 54, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	116, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 77, -1, -1, -1, -1, -1, -1, 67, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,118, -1, 76, -1,108, -1, -1, -1, -1, -1, -1, -1, 59, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 23, -1, -1, -1, 19, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,103, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 99, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, -1,
	120, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,127, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, 38, -1, -1, -1, 80, -1, -1, -1, -1, -1, -1, 71, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 96, -1, -1, -1, -1, 37, -1, -1, -1, -1, -1, -1, 30, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 73, -1, -1, -1, -1, -1, -1, -1,112, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, 90, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 40, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,122, -1,124, -1, -1, -1, -1, -1, -1, -1, -1, -1, 18, -1, -1, -1, 24, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 69, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, 84, -1, 49, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 36, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

ATUIEnhancedTextEngine::ATUIEnhancedTextEngine() {
	mVideoInfo.mPixelAspectRatio = 1.0f;
	mVideoInfo.mbSignalValid = true;
	mVideoInfo.mbSignalPassThrough = false;
	mVideoInfo.mHorizScanRate = 15735.0f;
	mVideoInfo.mVertScanRate = 59.94f;
	mVideoInfo.mbForceExactPixels = true;
	
	memset(mTextLineMode, 0, sizeof mTextLineMode);
	memset(mTextLineCHBASE, 0, sizeof mTextLineCHBASE);
	memset(mTextLastData, 0, sizeof mTextLastData);

	InvalidateFontSizeCache();
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

	// load the raw default font from AltirraOS ($E000-E3FF from $D800-FFFF)
	VDVERIFY(ATLoadKernelResource(IDR_KERNEL, mRawDefaultFont, 0x0800, sizeof mRawDefaultFont, true));

	// swizzle font to ATASCII order
	std::rotate(mRawDefaultFont, mRawDefaultFont + 0x200, mRawDefaultFont + 0x300);

	auto *vs = sim->GetVirtualScreenHandler();
	if (vs) {
		vs->SetReadyCallback([this] { OnInputReady(); });
		vs->SetResizeCallback([this] { OnVirtualScreenResized(); });
		vs->SetPassThroughChangedCallback([this] { OnPassThroughChanged(); });
	}

	OnPassThroughChanged();
}

void ATUIEnhancedTextEngine::Shutdown() {
	if (mpSim) {
		IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();
		if (vs) {
			vs->SetReadyCallback(nullptr);
			vs->SetResizeCallback(nullptr);
			vs->SetPassThroughChangedCallback(nullptr);
		}
	}

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
	if (!font) {
		ClearFont();
		return;
	}

	// force font to be recreated
	ClearFont();
	InvalidateFontSizeCache();
	mBaseFontDesc = *font;

	// recreate font
	UpdateFont();

	// reinitialize bitmap
	sint32 w = mViewportWidth;
	sint32 h = mViewportHeight;

	mViewportWidth = 0;
	mViewportHeight = 0;
	OnSize(w, h);
}

void ATUIEnhancedTextEngine::OnSize(uint32 w, uint32 h) {
	if (!w || !h)
		return;

	if (w > 32767)
		w = 32767;

	if (h > 32767)
		h = 32767;

	if (mViewportWidth == w && mViewportHeight == h)
		return;

	mViewportWidth = w;
	mViewportHeight = h;

	uint32 charW;
	uint32 charH;

	if (IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler()) {
		const vdsize32& termSize = vs->GetTerminalSize();
		charW = termSize.w;
		charH = termSize.h;
	} else {
		charW = 40;
		charH = 30;
	}

	// update the font now if we can
	UpdateFont();

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

	mbLastScreenValid = false;
	++mVideoInfo.mFrameBufferChangeCount;

	Update(true);

	if (mpOutput)
		mpOutput->InvalidateTextOutput();
}

void ATUIEnhancedTextEngine::OnChar(int ch) {
	ResetAttractTimeout();

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
		mbLastInputLineDirty = true;
	}
}

bool ATUIEnhancedTextEngine::OnKeyDown(uint32 keyCode) {
	if (IsRawInputEnabled())
		return false;

	ResetAttractTimeout();

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
				mInputHistIdx -= (1 + (uint32)strlen(&*(mHistoryBuffer.end() - mInputHistIdx)));

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

void ATUIEnhancedTextEngine::Paste(const wchar_t *s, size_t len) {
	char skipNext = 0;

	while(len--) {
		wchar_t c = *s++;

		if (c == skipNext) {
			skipNext = 0;
			continue;
		}

		if (c == L'\r' || c == L'\n') {
			skipNext = c ^ (L'\r' ^ L'\n');
			c = L'\n';
		}

		mPasteBuffer.push_back(c);
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

	for(int i=0; i<4; ++i) {
		COLORREF colorPF = VDSwizzleU32(mpGTIA->GetPlayfieldColor24(i)) >> 8;

		if (mTextLastPFColors[i] != colorPF) {
			mTextLastPFColors[i] = colorPF;
			forceInvalidate = true;
		}
	}

	if (vs) {
		if (vs->IsPassThroughEnabled())
			return;

		// check if a custom font is being used
		ATMemoryManager *mem = mpSim->GetMemoryManager();
		const uint8 chbase = mem->DebugReadByte(ATKernelSymbols::CHBAS);

		if (chbase != 0xE0) {
			bool forceRematchAllChars = false;

			if (!mbUsingCustomFont) {
				mbUsingCustomFont = true;

				forceInvalidate = true;
				forceRematchAllChars = true;
			}

			const uint16 chbase16 = (uint16)(chbase & 0xFC) << 8;
			mem->DebugAnticReadMemory(mMirroredCustomFont+0x000, chbase16+0x200, 256);
			mem->DebugAnticReadMemory(mMirroredCustomFont+0x100, chbase16+0x000, 256);
			mem->DebugAnticReadMemory(mMirroredCustomFont+0x200, chbase16+0x100, 256);
			mem->DebugAnticReadMemory(mMirroredCustomFont+0x300, chbase16+0x300, 256);

			if (memcmp(mCurrentCustomFont, mMirroredCustomFont, 1024)) {
				forceInvalidate = true;

				CharResampler cr(mTextCharW, mTextCharH);

				for(int i=0; i<128; ++i) {
					const uint8 *src = &mMirroredCustomFont[i*8];
					uint8 *dst = &mCurrentCustomFont[i*8];
					if (forceRematchAllChars || memcmp(dst, src, 8)) {
						memcpy(dst, src, 8);

						// check if we can match this to a standard glyph
						sint8 maybeChar = kRawDefaultFontHashTable[VDReadUnalignedLEU64(src) % kRawDefaultFontHashBuckets];
						if (maybeChar >= 0 && !memcmp(src, &mRawDefaultFont[maybeChar * 8], 8))
							mCustomGlyphIndices[i] = mDefaultGlyphIndices[(size_t)maybeChar];
						else
							mCustomGlyphIndices[i] = kInvalidGlyphIndex;

						if (mCustomGlyphIndices[i] == kInvalidGlyphIndex) {
							cr.ResampleChar(mRescaledCustomFont, i, src);
							forceInvalidate = true;
						}
					}
				}
			}
		} else {
			if (mbUsingCustomFont) {
				mbUsingCustomFont = false;

				forceInvalidate = true;
			}
		}

		// check CHACTL shadow to see if inverse video is enabled (required by SPACEWAY.BAS)
		const uint8 chact = mem->DebugReadByte(ATKernelSymbols::CHACT);
		const bool inverseEnabled = (chact & 2) != 0;

		if (mbInverseEnabled != inverseEnabled) {
			mbInverseEnabled = inverseEnabled;

			forceInvalidate = true;
		}

		bool lineFlags[255] = {false};
		bool *lineFlagsPtr = lineFlags;

		uint32 w, h;
		const uint8 *screen;
		vs->GetScreen(w, h, screen);

		uint32 cursorX, cursorY;
		bool cursorPresent = vs->GetCursorInfo(cursorX, cursorY) && inverseEnabled;

		bool dirty = false;
		uint32 n = w * h;
		if (n != mLastScreen.size() || !mbLastScreenValid || forceInvalidate) {
			mLastScreen.resize(n);
			memcpy(mLastScreen.data(), screen, n);

			mbLastScreenValid = true;
			mbLastInputLineDirty = true;
			lineFlagsPtr = NULL;
			dirty = true;
		} else {
			uint8 *last = mLastScreen.data();

			for(uint32 y=0; y<h; ++y) {
				if (memcmp(last, screen, w)) {
					memcpy(last, screen, w);
					lineFlagsPtr[y] = true;
					dirty = true;
				}

				last += w;
				screen += w;
			}
		}

		if (cursorX != mLastCursorX || cursorY != mLastCursorY || cursorPresent != mbLastCursorPresent) {
			mbLastInputLineDirty = true;

			if (lineFlagsPtr && mLastCursorY < h && mbLastCursorPresent) {
				lineFlagsPtr[mLastCursorY] = true;
				dirty = true;
			}
		}

		if (mbLastInputLineDirty) {
			mbLastInputLineDirty = false;

			mLastCursorX = cursorX;
			mLastCursorY = cursorY;
			mbLastCursorPresent = cursorPresent;

			if (cursorPresent && cursorY < h && lineFlagsPtr)
				lineFlagsPtr[cursorY] = true;

			dirty = true;
		}

		if (dirty)
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

		const uint8 chbase = hval.mCHBASE << 1;
		if (chbase != mTextLineCHBASE[line])
			forceInvalidate = true;

		if (forceInvalidate || line >= mTextLastLineCount || memcmp(data, lastData, width)) {
			mTextLineMode[line] = mode;
			mTextLineCHBASE[line] = chbase;
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

const char *ATUIEnhancedTextEngine::GetName() const {
	return "enhtext";
}

const wchar_t *ATUIEnhancedTextEngine::GetDisplayName() const {
	return L"Enhanced Text";
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

	IATVirtualScreenHandler *const vs = mpSim->GetVirtualScreenHandler();
	if (vs) {
		return vdpoint32(
			pixelPos.x < 0 ? 0
			: pixelPos.x >= mVideoInfo.mDisplayArea.right ? mVideoInfo.mTextColumns - 1
			: ((pixelPos.x * 2 / mTextCharW) + 1) >> 1,
			pixelPos.y / mTextCharH);
	} else {
		auto itBegin = std::begin(mActiveLineYStart);
		auto itEnd = std::begin(mActiveLineYStart) + mTextLastLineCount;
		auto it = std::upper_bound(itBegin, itEnd, pixelPos.y);

		if (it == itBegin)
			return vdpoint32(0, 0);

		if (it == itEnd)
			return vdpoint32(mVideoInfo.mTextColumns - 1, mVideoInfo.mTextRows - 1);

		int row = (int)(it - itBegin) - 1;

		int px = pixelPos.x;
		int col = 0;

		if (px >= 0) {
			if (mTextLineMode[row] == 2)
				col = std::min<sint32>(40, ((px * 2) / mTextCharW + 1) >> 1);
			else
				col = std::min<sint32>(20, (px / mTextCharW + 1) >> 1);
		}

		return vdpoint32(col, row);
	}
}

vdrect32 ATUIEnhancedTextEngine::CharToPixelRect(const vdrect32& r) {
	IATVirtualScreenHandler *const vs = mpSim->GetVirtualScreenHandler();
	if (vs) {
		return vdrect32(r.left * mTextCharW, r.top * mTextCharH, r.right * mTextCharW, r.bottom * mTextCharH);
	} else {
		if (!mTextLastLineCount)
			return vdrect32(0, 0, 0, 0);

		vdrect32 rp;

		if (r.top < 0) {
			rp.left = 0;
			rp.top = mActiveLineYStart[0];
		} else {
			const int charCount = (mTextLineMode[mTextLastLineCount - 1] == 2 ? 40 : 20);

			if (r.top >= mTextLastLineCount) {
				rp.left = charCount * mTextCharW;
				rp.top = mActiveLineYStart[mTextLastLineCount - 1];
			} else {
				rp.left = std::min<int>(charCount, r.left) * (mTextLineMode[r.top] == 2 ? mTextCharW : mTextCharW * 2);
				rp.top = mActiveLineYStart[r.top];
			}
		}

		if (r.bottom <= 0) {
			rp.right = 0;
			rp.bottom = mActiveLineYStart[0];
		} else {
			const int charCount = (mTextLineMode[mTextLastLineCount - 1] == 2 ? 40 : 20);
			if (r.bottom > mTextLastLineCount) {
				rp.right = charCount * mTextCharW;
				rp.bottom = mActiveLineYStart[mTextLastLineCount];
			} else {
				rp.right = std::min<int>(charCount, r.right) * (mTextLineMode[r.bottom - 1] == 2 ? mTextCharW : mTextCharW * 2);
				rp.bottom = mActiveLineYStart[r.bottom];
			}
		}

		return rp;
	}
}

int ATUIEnhancedTextEngine::ReadRawText(uint8 *dst, int x, int y, int n) {
	IATVirtualScreenHandler *const vs = mpSim->GetVirtualScreenHandler();

	if (vs)
		return vs->ReadRawText(dst, x, y, n);

	if (x < 0 || y < 0 || y >= mTextLastLineCount)
		return 0;

	const uint8 mode = mTextLineMode[y];
	int nc = (mode == 2) ? 40 : 20;

	if (x >= nc)
		return 0;

	if (n > nc - x)
		n = nc - x;

	const uint8 *src = mTextLastData[y] + x;
	const uint8 chmask = (mode != 2) ? 0x3F : 0x7F;
	const uint8 choffset = (mode != 2 && (mTextLineCHBASE[y] & 2)) ? 0x40 : 0;
	for(int i=0; i<n; ++i) {
		uint8 c = (src[i] & chmask) + choffset;

		c ^= kInternalToATASCIIXorTab[(c >> 5) & 3];

		if ((uint8)((c & 0x7f) - 0x20) >= 0x5f)
			c = (c & 0x80) + '.';

		dst[i] = c;
	}

	return n;
}

uint32 ATUIEnhancedTextEngine::GetActivityCounter() {
	return 0;
}

void ATUIEnhancedTextEngine::Paint(const bool *lineRedrawFlags) {
	// if the foreground or background colors have changed, recompute the gamma
	// table
	if (mLastGammaTableForeColor != mTextLastForeColor || 
		mLastGammaTableBackColor != mTextLastBackColor)
	{
		mLastGammaTableForeColor = mTextLastForeColor;
		mLastGammaTableBackColor = mTextLastBackColor;

		using namespace nsVDVecMath;
		const vdfloat32x3 foreColor = vdfloat32x3(VDColorRGB::FromBGR8(mTextLastForeColor).SRGBToLinear());
		const vdfloat32x3 backColor = vdfloat32x3(VDColorRGB::FromBGR8(mTextLastBackColor).SRGBToLinear());

		for(int i=0; i<256; ++i) {
			float rawAlpha = (float)i / 255.0f;

			// apply contrast enhancement
			float alpha;
			if (rawAlpha < 0.25f)
				alpha = 0.0f;
			else if (rawAlpha > 0.75f)
				alpha = 1.0f;
			else
				alpha = (rawAlpha - 0.25f) * 2.0f;

			mGammaTable[i] = VDColorRGB(saturate(lerp(backColor, foreColor, alpha))).LinearToSRGB().ToRGB8();
		}
	}

	if (mpSim->GetVirtualScreenHandler())
		PaintSWMode(lineRedrawFlags);
	else
		PaintHWMode(lineRedrawFlags);

	++mVideoInfo.mFrameBufferChangeCount;

	if (mpOutput)
		mpOutput->InvalidateTextOutput();
}

void ATUIEnhancedTextEngine::PaintHWMode(const bool *lineRedrawFlags) {
	int saveHandle = SaveDC(mhdc);

	if (!saveHandle)
		return;

	const COLORREF colorBack = mTextLastBackColor;
	const COLORREF colorFore = mTextLastForeColor;
	const COLORREF colorBorder = mTextLastBorderColor;

	SetTextAlign(mhdc, TA_TOP | TA_LEFT);

	// We used to use OPAQUE here, but have run into problems with CreateFont() returning
	// font heights that don't exactly match as requested for double-height mode. To work
	// around this, we fill the backgrounds first and then draw all text.
	SetBkMode(mhdc, TRANSPARENT);

	uint8 lastMode = 0;
	int py = 0;
	const uint16 *glyphTable = nullptr;
	uint16 glyphs[40];

	for(int line = 0; line < mTextLastLineCount; ++line) {
		uint8 *data = mTextLastData[line];

		const uint8 mode = mTextLineMode[line];
		const uint8 chbase = mTextLineCHBASE[line];
		int charWidth = (mode == 2 ? mTextCharW : mTextCharW*2);
		int charHeight = (mode != 7 ? mTextCharH : mTextCharH*2);

		if (!lineRedrawFlags || lineRedrawFlags[line]) {
			uint8 buf[41];
			bool inverted[41];

			int N = (mode == 2 ? 40 : 20);

			for(int i=0; i<N; ++i) {
				uint8 c = data[i];

				if (mode != 2) {
					c &= 0x3f;

					if (chbase & 0x02)
						c |= 0x40;
				}

				c ^= kInternalToATASCIIXorTab[(c >> 5) & 3];

				if ((uint8)((c & 0x7f) - 0x20) >= 0x5f)
					c = (c & 0x80) + '.';

				buf[i] = c & 0x7f;
				inverted[i] = (c & 0x80) != 0;
			}

			inverted[N] = !inverted[N-1];
			buf[N] = 0;

			if (lastMode != mode) {
				lastMode = mode;

				switch(mode) {
					case 2:
					default:
						SelectObject(mhdc, mTextModeFont);
						glyphTable = mGlyphLookup[0];
						break;

					case 6:
						SelectObject(mhdc, mTextModeFont2x);
						glyphTable = mGlyphLookup[1];
						break;

					case 7:
						SelectObject(mhdc, mTextModeFont4x);
						glyphTable = mGlyphLookup[2];
						break;
				}
			}

			// translate text to glyphs
			for(int i=0; i<40; ++i)
				glyphs[i] = glyphTable[buf[i]];

			RECT rTextBack { 0, py, 0, py + charHeight };

			if (mode == 2) {
				for(int x = 0; x < N; ) {
					bool invertSpan = inverted[x];
					int xe = x + 1;

					while(inverted[xe] == invertSpan)
						++xe;

					if (invertSpan)
						SetBkColor(mhdc, colorFore);
					else
						SetBkColor(mhdc, colorBack);

					rTextBack.left = charWidth * x;
					rTextBack.right = charWidth * xe;
					ExtTextOutW(mhdc, charWidth * x, py, ETO_OPAQUE, &rTextBack, L"", 0, NULL);

					x = xe;
				}

				for(int x = 0; x < N; ) {
					bool invertSpan = inverted[x];
					int xe = x + 1;

					while(inverted[xe] == invertSpan)
						++xe;

					if (invertSpan)
						SetTextColor(mhdc, colorBack);
					else
						SetTextColor(mhdc, colorFore);

					ExtTextOutW(mhdc, charWidth * x, py, ETO_GLYPH_INDEX, NULL, (LPCWSTR)(glyphs + x), xe - x, NULL);

					x = xe;
				}

				RECT rClear = { charWidth * N, py, mBitmapWidth, py + charHeight };
				SetBkColor(mhdc, colorBorder);
				ExtTextOutW(mhdc, 0, py, ETO_OPAQUE, &rClear, L"", 0, NULL);
			} else {
				// Modes 6 and 7 don't invert chars... which makes the background fill easy.
				rTextBack.right = mBitmapWidth;
				SetBkColor(mhdc, colorBorder);
				ExtTextOutW(mhdc, 0, py, ETO_OPAQUE, &rTextBack, L"", 0, NULL);

				// We need to force the character spacing as sometimes the 2x/4x font doesn't quite have the right width.
				INT dxArray[40];

				for(INT& dx : dxArray)
					dx = charWidth;

				for(int x = 0; x < N; ) {
					uint8 c = data[x];
					int xe = x + 1;

					while(xe < N && !((data[xe] ^ c) & 0xc0))
						++xe;

					SetTextColor(mhdc, mTextLastPFColors[c >> 6]);

					ExtTextOutW(mhdc, charWidth * x, py, ETO_GLYPH_INDEX, NULL, (LPCWSTR)(glyphs + x), xe - x, dxArray);

					x = xe;
				}
			}
		}

		mActiveLineYStart[line] = py;
		py += charHeight;
	}

	mActiveLineYStart[mTextLastLineCount] = py;

	if (mTextLastTotalHeight != py || !lineRedrawFlags) {
		mTextLastTotalHeight = py;
		++mVideoInfo.mFrameBufferLayoutChangeCount;
		mVideoInfo.mTextRows = mTextLastLineCount;
		mVideoInfo.mDisplayArea.bottom = std::max<sint32>(py, mTextCharH * 24);
		mFrameBuffer.h = mVideoInfo.mDisplayArea.bottom;

		RECT rClear = { 0, py, mBitmapWidth, mBitmapHeight };
		SetBkColor(mhdc, colorBorder);
		ExtTextOutW(mhdc, 0, py, ETO_OPAQUE, &rClear, L"", 0, NULL);
	}

	RestoreDC(mhdc, saveHandle);
	GdiFlush();
}

void ATUIEnhancedTextEngine::PaintSWMode(const bool *lineRedrawFlags) {
	int saveHandle = SaveDC(mhdc);

	if (!saveHandle)
		return;

	mPendingSpecialChars.clear();

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
	const uint8 invertMask = mbInverseEnabled ? 0xFF : 0x7F;

	uint8 linedata[255];

	for(uint32 y = 0; y < h; ++y, (py += charHeight), (screen += w)) {
		if (lineRedrawFlags && !lineRedrawFlags[y])
			continue;

		for(uint32 x = 0; x < w; ++x) {
			uint8 c = screen[x];

			linedata[x] = c & invertMask;
		}

		if (cursorY == y) {
			uint32 limit = cursorX + (uint32)mInputBuffer.size();

			if (limit > w)
				limit = w;

			for(uint32 x = cursorX; x < limit; ++x)
				linedata[x] = (uint8)mInputBuffer[x - cursorX];

			uint32 cx = cursorX + mInputPos;

			if (cx < w)
				linedata[cx] ^= 0x80;
		}

		const WCHAR *const glyphIndexTable = mbUsingCustomFont ? mCustomGlyphIndices : mDefaultGlyphIndices;
		for(uint32 x = 0; x < w; ++x) {
			uint8 c = linedata[x];

			// Replace non-printable characters with spaces. We'll overwrite
			// them with glyphs later.
			uint8 c2 = c & 0x7F;
			if (glyphIndexTable[c2] == kInvalidGlyphIndex) {
				mPendingSpecialChars.emplace_back(
					PendingSpecialChar {
						.mChar = c,
						.mX = (uint8)x,
						.mY = (uint8)y
					}
				);

				c = (c & 0x80) + ' ';
			}

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
			const RECT rLine = { (LONG)px, py, (LONG)(charWidth * xe), (LONG)(py + mTextCharH) };

			for(uint32 x3 = x2; x3 < xe; ++x3) {
				WCHAR glyphIdx = glyphIndexTable[line[x3] & 0x7F];

				if (glyphIdx != kEmptyGlyphIndex && glyphIdx != kInvalidGlyphIndex) {
					line[countOut] = glyphIdx;

					if (!countOut)
						px += glyphPos;
					else
						glyphOffsets[countOut - 1] = glyphPos;

					glyphPos = 0;
					++countOut;
				}

				glyphPos += mTextCharW;
			}

			if (countOut) {
				// We need to zero out this last entry even though it has no meaning.
				// Otherwise, GDI spends an ungodly amount of time doing a memset()
				// in kernel mode on a huge buffer.
				glyphOffsets[countOut-1] = 0;
			}

			ExtTextOutW(mhdc, px, py, ETO_GLYPH_INDEX | ETO_OPAQUE, &rLine, line, countOut, glyphOffsets);

			x2 = xe;
		}
	}

	if (!lineRedrawFlags) {
		SetBkColor(mhdc, colorBorder);

		RECT rClear = { (LONG)(mTextCharW * w), 0, mBitmapWidth, py };
		ExtTextOutW(mhdc, 0, py, ETO_OPAQUE, &rClear, L"", 0, NULL);

		RECT rClear2 = { 0, py, mBitmapWidth, mBitmapHeight };
		ExtTextOutW(mhdc, 0, py, ETO_OPAQUE, &rClear2, L"", 0, NULL);
	}

	RestoreDC(mhdc, saveHandle);
	GdiFlush();

	// Now that GDI is finished, we can draw any needed special characters.
	VDPixmap charSrc = mbUsingCustomFont ? mRescaledCustomFont : mRescaledDefaultFont;
	for(const PendingSpecialChar& psc : mPendingSpecialChars) {
		if (psc.mX >= mVideoInfo.mTextColumns || psc.mY >= mVideoInfo.mTextRows) {
			VDFAIL("Character out of bounds");
			continue;
		}

		const uint8 *src = (const uint8 *)charSrc.data + charSrc.pitch * (psc.mChar & 0x7F) * mTextCharH;
		uint32 *dst = (uint32 *)((char *)mFrameBuffer.data + mFrameBuffer.pitch * (psc.mY * mTextCharH)) + psc.mX * mTextCharW;
		const uint8 invert = psc.mChar & 0x80 ? 0xFF : 0;

		for(int row = 0; row < mTextCharH; ++row) {
			for(int col = 0; col < mTextCharW; ++col)
				dst[col] = mGammaTable[src[col] ^ invert];

			src += charSrc.pitch;
			dst = (uint32 *)((char *)dst + mFrameBuffer.pitch);
		}
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

void ATUIEnhancedTextEngine::OnVirtualScreenResized() {
	// reinitialize bitmap
	sint32 w = mViewportWidth;
	sint32 h = mViewportHeight;

	mViewportWidth = 0;
	mViewportHeight = 0;
	OnSize(w, h);
}

void ATUIEnhancedTextEngine::OnPassThroughChanged() {
	auto *vs = mpSim->GetVirtualScreenHandler();

	mVideoInfo.mbSignalPassThrough = !vs || vs->IsPassThroughEnabled();
}

void ATUIEnhancedTextEngine::ProcessPastedInput() {
	auto *vs = mpSim->GetVirtualScreenHandler();
	if (!vs || !vs->IsReadyForInput())
		return;

	while(!mPasteBuffer.empty()) {
		wchar_t c = mPasteBuffer.front();
		mPasteBuffer.pop_front();

		if (c == '\n') {
			OnKeyDown(kATUIVK_Return);
			break;
		} else if (c >= 0x20 && c < 0x7F)
			OnChar(c);
	}
}

void ATUIEnhancedTextEngine::ResetAttractTimeout() {
	mpSim->GetMemoryManager()->WriteByte(ATKernelSymbols::ATRACT, 0);
}

// updates font if needed for viewport size, or just because it hasn't been
// instantiated yet
void ATUIEnhancedTextEngine::UpdateFont() {
	LOGFONT font = mBaseFontDesc;

	if (mBaseFontDesc.lfFaceName[0]) {
		const vdsize32 desiredFontSize = ComputeDesiredFontSize();
		if (desiredFontSize.w < 0) {
			// viewport or terminal size is invalid, don't try to update font yet
			return;
		}

		int fontHeight = desiredFontSize.h;

		while(fontHeight > kMinFontSize) {
			// try to look up pre-cached metrics
			vdsize32& cachedFontSize = mFontSizeCache[fontHeight];

			// check if cache entry is valid
			if (cachedFontSize.w < 0) {
				// no -- we'll have to create the font. mark the entry as tried
				// first
				cachedFontSize = vdsize32(0, 0);

				// try to instantiate the font and pull its metrics
				font.lfHeight = fontHeight;

				ClearFont();

				mTextModeFont = CreateFontIndirectW(&font);
				if (mTextModeFont) {
					HGDIOBJ hOldFont = SelectObject(mhdc, mTextModeFont);
					if (hOldFont) {
						TEXTMETRICW tm;
						if (GetTextMetricsW(mhdc, &tm)) {
							cachedFontSize = vdsize32(tm.tmAveCharWidth, tm.tmHeight);
						}

						SelectObject(mhdc, hOldFont);
					}

					// leave the font created -- if we succeed with this size
					// then we can avoid recreating the font
				}
			}

			// if the cached entry is valid and fits, then use this font height
			if (cachedFontSize.w > 0 && cachedFontSize.w <= desiredFontSize.w && cachedFontSize.h <= desiredFontSize.h)
				break;

			// font's too wide -- release the font, decrement the height and try again
			ClearFont();
			--fontHeight;
		}

		// if the font has already been created and is the correct height, quick exit;
		// note that if we newly created it in the above loop, it'll fail because the
		// cached font height will be 0, as we still need to do init below
		if (mTextModeFont && mTextModeFontHeight == fontHeight) {
			// selected font height is the same as that which was already created,
			// quick exit
			return;
		}

		// attempt to create font now
		ClearFont();

		font.lfHeight = fontHeight;
		mTextModeFont = CreateFontIndirectW(&font);
		if (mTextModeFont)
			mTextModeFontHeight = fontHeight;
	} else {
		// no font was specified, so we're using the fallback font -- it is never sized, so we
		// always succeed if it's been instantiated
		if (mTextModeFont)
			return;
	}

	std::fill(std::begin(mDefaultGlyphIndices), std::end(mDefaultGlyphIndices), kInvalidGlyphIndex);
	std::fill(std::begin(mCustomGlyphIndices), std::end(mCustomGlyphIndices), kInvalidGlyphIndex);

	mTextCharW = 16;
	mTextCharH = 16;

	// if we failed to create the font, try to create fallback
	if (!mTextModeFont) {
		mTextModeFont = CreateFontW(16, 0, 0, 0, 0, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_DONTCARE | DEFAULT_PITCH, L"Lucida Console");
		if (!mTextModeFont)
			return;
	}

	HGDIOBJ hOldFont = SelectObject(mhdc, mTextModeFont);
	if (hOldFont) {
		TEXTMETRICW tm;
		if (GetTextMetricsW(mhdc, &tm)) {
			mTextCharW = tm.tmAveCharWidth;
			mTextCharH = tm.tmHeight;
		}

		// special-case space
		mDefaultGlyphIndices[0x20] = kEmptyGlyphIndex;

		// map 21-7A / 7C to the font; 7B/7D-7F differ between ASCII and ATASCII
		for(int i=0x21; i<0x7C; ++i) {
			if (i == 0x7B)
				continue;

			const WCHAR ch = (WCHAR)i;

			GCP_RESULTSW results = {sizeof(GCP_RESULTSW)};
			results.lpGlyphs = &mDefaultGlyphIndices[i];
			results.nGlyphs = 1;

			if (!GetCharacterPlacementW(mhdc, &ch, 1, 0, &results, 0))
				mDefaultGlyphIndices[i] = kInvalidGlyphIndex;
		}

		SelectObject(mhdc, hOldFont);
	}

	LOGFONTW logfont2x4x(mBaseFontDesc);
	logfont2x4x.lfHeight = mTextCharH;
	logfont2x4x.lfWidth = mTextCharW * 2;

	VDASSERT(!mTextModeFont2x);
	mTextModeFont2x = CreateFontIndirectW(&logfont2x4x);
	if (!mTextModeFont2x)
		mTextModeFont2x = CreateFontW(16, mTextCharW * 2, 0, 0, 0, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_DONTCARE | DEFAULT_PITCH, L"Lucida Console");

	logfont2x4x.lfHeight *= 2;
	mTextModeFont4x = CreateFontIndirectW(&logfont2x4x);
	if (!mTextModeFont4x)
		mTextModeFont4x = CreateFontW(32, mTextCharW * 2, 0, 0, 0, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_DONTCARE | DEFAULT_PITCH, L"Lucida Console");

	// initialize font glyphs
	HFONT hfonts[3] = { mTextModeFont, mTextModeFont2x, mTextModeFont4x };

	for(int i=0; i<3; ++i) {
		SelectObject(mhdc, hfonts[i]);

		auto& glyphTable = mGlyphLookup[i];
		memset(&glyphTable, 0, sizeof glyphTable);

		WCHAR cspace = (WCHAR)0x20;
		for(int i=32; i<127; ++i) {
			WCHAR c = (WCHAR)i;
			WORD glyphIndex = 0;

			if (1 == GetGlyphIndicesW(mhdc, &c, 1, &glyphIndex, GGI_MARK_NONEXISTING_GLYPHS) ||
				1 == GetGlyphIndicesW(mhdc, &cspace, 1, &glyphIndex, GGI_MARK_NONEXISTING_GLYPHS))
			{
				glyphTable[i] = glyphIndex;
			}
		}

		// backfill other chars with period
		for(int i=0; i<32; ++i)
			glyphTable[i] = glyphTable[0x21];

		glyphTable[0x7F] = glyphTable[0x21];
	}

	// reinitialize default font fallbacks
	mRescaledDefaultFont.init(mTextCharW, mTextCharH * 128, nsVDPixmap::kPixFormat_Y8);
	memset(mRescaledDefaultFont.base(), 0, mRescaledDefaultFont.size());

	mRescaledCustomFont.init(mTextCharW, mTextCharH * 128, nsVDPixmap::kPixFormat_Y8);
	memset(mRescaledCustomFont.base(), 0, mRescaledCustomFont.size());
	memset(mCurrentCustomFont, 0, sizeof mCurrentCustomFont);

	CharResampler charResampler(mTextCharW, mTextCharH);
	for(int i=0; i<128; ++i)
		charResampler.ResampleChar(mRescaledDefaultFont, i, &mRawDefaultFont[8 * i]);
}

void ATUIEnhancedTextEngine::ClearFont() {
	if (mTextModeFont) {
		DeleteObject(mTextModeFont);
		mTextModeFont = nullptr;
		mTextModeFontHeight = 0;
	}

	if (mTextModeFont2x) {
		DeleteObject(mTextModeFont2x);
		mTextModeFont2x = nullptr;
	}

	if (mTextModeFont4x) {
		DeleteObject(mTextModeFont4x);
		mTextModeFont4x = nullptr;
	}
}

// Compute the initial font size given the current viewport size and
// terminal size. The actual font size used will be this or smaller, depending
// on width constraints, and then min/max size constraints. -1,-1 means
// indeterminate.
vdsize32 ATUIEnhancedTextEngine::ComputeDesiredFontSize() const {
	if (!mViewportWidth || !mViewportHeight)
		return vdsize32(-1, -1);

	auto *vs = mpSim->GetVirtualScreenHandler();
	if (!vs)
		return vdsize32(-1, -1);

	const vdsize32& termSize = vs->GetTerminalSize();

	// The height is the axis that drives font creation, so it's the one that's
	// clamped; the width is just used to verify that the font is small enough
	// to fit.
	return vdsize32(
		mViewportWidth / termSize.w,
		std::clamp<int>(mViewportHeight / termSize.h, kMinFontSize, kMaxFontSize)
	);
}

void ATUIEnhancedTextEngine::InvalidateFontSizeCache() {
	for(auto& v : mFontSizeCache)
		v = vdsize32(-1, -1);
}

///////////////////////////////////////////////////////////////////////////

IATUIEnhancedTextEngine *ATUICreateEnhancedTextEngine() {
	return new ATUIEnhancedTextEngine;
}
