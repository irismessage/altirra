//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2013 Avery Lee
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
#include <vd2/system/math.h>
#include <vd2/system/memory.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceparentimpl.h>
#include <at/atcore/deviceport.h>
#include <at/atcore/deviceprinter.h>
#include <at/atcore/devicesnapshot.h>
#include <at/atcore/devicevideo.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/snapshotimpl.h>
#include "xep80.h"
#include "pia.h"
#include "debuggerlog.h"
#include "xep80_font_normal.inl"
#include "xep80_font_intl.inl"
#include "xep80_font_internal.inl"

namespace {
	uint8 ReverseBits8(uint8 c) {
		c = ((c & 0xaa) >> 1) + ((c & 0x55) << 1);
		c = ((c & 0xcc) >> 2) + ((c & 0x33) << 2);
		c = ((c & 0xf0) >> 4) + ((c & 0x0f) << 4);

		return c;
	}

	const uint8 kDoubleTableLo[16]={
		0x00, 0x03, 0x0c, 0x0f,
		0x30, 0x33, 0x3c, 0x3f,
		0xc0, 0xc3, 0xcc, 0xcf,
		0xf0, 0xf3, 0xfc, 0xff
	};
}

ATDebuggerLogChannel g_ATLCXEPData(false, false, "XEPDATA", "XEP80 Data Transfer");
ATDebuggerLogChannel g_ATLCXEPCmd(false, false, "XEPCMD", "XEP80 Commands");

struct ATXEP80Emulator::CommandInfo {
	uint8 mCommandLo;
	uint8 mCommandHi;
	void (ATXEP80Emulator::*mpCmd)(uint8 ch);
	const char *mpName;
};

const ATXEP80Emulator::CommandInfo ATXEP80Emulator::kCommands[]={
	{ 0x00, 0x4f, &ATXEP80Emulator::OnCmdSetCursorHPos, "set hpos" },	// set horizontal cursor position (00-4F)
	{ 0x50, 0x5f, &ATXEP80Emulator::OnCmdSetCursorHPosHi, "set hpos high nibble" },	// set horizontal cursor position high nibble
	{ 0x60, 0x6f, &ATXEP80Emulator::OnCmdSetLeftMarginLo, "set left margin" },	// left margin low nibble
	{ 0x70, 0x7f, &ATXEP80Emulator::OnCmdSetLeftMarginHi, "set left margin high nibble" },	// left margin high nibble
	{ 0x80, 0x98, &ATXEP80Emulator::OnCmdSetCursorVPos, "set vpos" },	// vertical cursor position (00-18)
	{ 0x99, 0x99, &ATXEP80Emulator::OnCmdSetGraphics, "set graphics to 60Hz" },	// set graphics to 60Hz
	{ 0x9a, 0x9a, &ATXEP80Emulator::OnCmdModifyGraphics50Hz, "modify graphics to 50Hz" },	// modify graphics to 50Hz
	{ 0xa0, 0xaf, &ATXEP80Emulator::OnCmdSetRightMarginLo, "set right margin" },	// right margin low nibble
	{ 0xb0, 0xbf, &ATXEP80Emulator::OnCmdSetRightMarginHi, "set right margin high nibble" },	// right margin high nibble
	{ 0xc0, 0xc0, &ATXEP80Emulator::OnCmdReadCharAndAdvance, "read char and advance" },	// get character at cursor and advance
	{ 0xc1, 0xc1, &ATXEP80Emulator::OnCmdRequestCursorHPos, "read hpos" },	// request horizontal cursor
	{ 0xc2, 0xc2, &ATXEP80Emulator::OnCmdMasterReset, "reset" },	// master reset
	{ 0xc3, 0xc3, &ATXEP80Emulator::OnCmdPrinterPortStatus, "get printer status" },	// printer port status
	{ 0xc4, 0xc4, &ATXEP80Emulator::OnCmdFillPrevChar, "fill with prev char" },	// fill RAM with previous char
	{ 0xc5, 0xc5, &ATXEP80Emulator::OnCmdFillSpace, "fill with space" },	// fill RAM with space
	{ 0xc6, 0xc6, &ATXEP80Emulator::OnCmdFillEOL, "fill with EOL" },	// fill RAM with EOL
	{ 0xc7, 0xc7, &ATXEP80Emulator::OnCmdReadChar, "read char" },		// read char without advancing
	{ 0xcb, 0xcb, &ATXEP80Emulator::OnCmdReadTimerCounter, "read T register" },// read T register
	{ 0xd0, 0xd0, &ATXEP80Emulator::OnCmdClearListFlag, "clear list flag" },	// clear list flag
	{ 0xd1, 0xd1, &ATXEP80Emulator::OnCmdSetListFlag, "set list flag" },	// set list flag
	{ 0xd2, 0xd2, &ATXEP80Emulator::OnCmdSetNormalMode, "set normal xmit mode" },	// set screen normal mode - cursor returned each char
	{ 0xd3, 0xd3, &ATXEP80Emulator::OnCmdSetBurstMode, "set burst xmit mode" },	// set screen burst mode - no cursor returned
	{ 0xd4, 0xd4, &ATXEP80Emulator::OnCmdSetCharSet, "set ATASCII charset" },	// set character set A - ATASCII
	{ 0xd5, 0xd5, &ATXEP80Emulator::OnCmdSetCharSet, "set int'l charset" },	// set character set B - international
	{ 0xd6, 0xd6, &ATXEP80Emulator::OnCmdSetCharSet, "set internal charset" },	// set XEP80 internal character set
	{ 0xd7, 0xd7, &ATXEP80Emulator::OnCmdSetText50Hz, "modify text to 50Hz" },	// modify text to 50Hz operation
	{ 0xd8, 0xd8, &ATXEP80Emulator::OnCmdCursorOff, "cursor off" },	// cursor off
	{ 0xd9, 0xd9, &ATXEP80Emulator::OnCmdCursorOn, "cursor on" },	// cursor on continuous
	{ 0xda, 0xda, &ATXEP80Emulator::OnCmdCursorOnBlink, "cursor blink" },	// cursor on blink
	{ 0xdb, 0xdb, &ATXEP80Emulator::OnCmdMoveToLogicalStart, "move to logical start" },	// move cursor to start of logical line
	{ 0xdc, 0xdc, &ATXEP80Emulator::OnCmdSetScrollX, "set scroll window" },	// set scroll window to cursor X value
	{ 0xdd, 0xdd, &ATXEP80Emulator::OnCmdSetPrinterOutput, "set printer output" },	// set printer output
	{ 0xde, 0xde, &ATXEP80Emulator::OnCmdSetReverseVideo, "set white on black" },	// select white characters on black background
	{ 0xdf, 0xdf, &ATXEP80Emulator::OnCmdSetReverseVideo, "set black on white" },	// select black characters on white background
	{ 0xe1, 0xe1, &ATXEP80Emulator::OnCmdSetExtraByte },	// set extended byte for test commands
	{ 0xe2, 0xe2, &ATXEP80Emulator::OnCmdSetCursorAddr, "set cursor address" },	// set cursor address
	{ 0xe3, 0xe3, &ATXEP80Emulator::OnCmdWriteByte, "write byte at cursor" },		// write byte at cursor
	{ 0xe4, 0xe4, &ATXEP80Emulator::OnCmdSetExtraByte, "set extended byte" },	// set extended byte for test commands
	{ 0xe5, 0xe5, &ATXEP80Emulator::OnCmdWriteInternalByte, "write internal RAM" },	// write byte into internal RAM
	{ 0xe6, 0xe6, &ATXEP80Emulator::OnCmdSetExtraByte },	// set extended byte for test commands
	{ 0xe7, 0xe7, &ATXEP80Emulator::OnCmdSetHomeAddr, "set HOME addr" },	// set display home address
	{ 0xed, 0xed, &ATXEP80Emulator::OnCmdWriteVCR, "set VCR" },		// write video control register
	{ 0xee, 0xee, &ATXEP80Emulator::OnCmdSetExtraByte },	// set extended byte for test commands
	{ 0xef, 0xef, &ATXEP80Emulator::OnCmdSetBeginAddr, "set BEGD addr" },	// set begin display address
	{ 0xf0, 0xf0, &ATXEP80Emulator::OnCmdSetExtraByte },	// set extended byte for test commands
	{ 0xf1, 0xf1, &ATXEP80Emulator::OnCmdSetEndAddr, "set ENDD addr" },		// set end display address
	{ 0xf2, 0xf2, &ATXEP80Emulator::OnCmdSetExtraByte },	// set extended byte for test commands
	{ 0xf3, 0xf3, &ATXEP80Emulator::OnCmdSetStatusAddr, "set SROW addr" },	// set extended byte for test commands
	{ 0xf4, 0xf5, &ATXEP80Emulator::OnCmdSetAttrLatch, "set attribute latch" },	// set attribute latch 0/1
	{ 0xf6, 0xf6, &ATXEP80Emulator::OnCmdSetTCP, "set TCP" },			// set timing control pointer
	{ 0xf7, 0xf7, &ATXEP80Emulator::OnCmdWriteTCP, "write TCP" },		// write to timing control chain
	{ 0xf9, 0xf9, &ATXEP80Emulator::OnCmdSetExtraByte },	// set extended byte for test commands
	{ 0xfa, 0xfa, &ATXEP80Emulator::OnCmdSetBaudRate, "set baud rate" },		// set baud rate divisor
	{ 0xfc, 0xfc, &ATXEP80Emulator::OnCmdSetUMX, "set UART multiplex register" },	// set UART multiplex register
};

ATXEP80Emulator::ATXEP80Emulator()
	: mCommandState(kState_WaitCommand)
	, mReadBitState(0)
	, mWriteBitState(0)
	, mCurrentData(0)
	, mWriteIndex(0)
	, mWriteLength(0)
	, mX(0)
	, mY(0)
	, mpPIA(NULL)
	, mPIAInput(-1)
	, mPIAOutput(-1)
	, mPIAInputBit(0)
	, mPIAOutputBit(0)
	, mPortIndex(0)
	, mpScheduler(NULL)
	, mpReadBitEvent(NULL)
	, mpWriteBitEvent(NULL)
	, mFrameLayoutChangeCount(0)
	, mFrameChangeCount(0)
	, mDataReceivedCount(0)
{
}

ATXEP80Emulator::~ATXEP80Emulator() {
}

void ATXEP80Emulator::Init(ATScheduler *sched, IATDevicePortManager *pia) {
	mpScheduler = sched;
	mpPIA = pia;

	mPIAInput = pia->AllocInput();
	mPIAOutput = pia->AllocOutput(
		[](void *data, uint32 outputState) { 
			((ATXEP80Emulator *)data)->OnPIAOutputChanged(outputState);
		},
		this,
		0x00010
	);

	memset(mPalette, 0, sizeof mPalette);
	mPalette[1] = 0xFFFFFF;

	ColdReset();
	InitFonts();
	UpdatePIABits();
}

void ATXEP80Emulator::Shutdown() {
	if (mpPIA) {
		mpPIA->FreeInput(mPIAInput);
		mpPIA->FreeOutput(mPIAOutput);
		mpPIA = 0;
	}

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpReadBitEvent);
		mpScheduler->UnsetEvent(mpWriteBitEvent);
		mpScheduler = NULL;
	}
}

void ATXEP80Emulator::ColdReset() {
	// UART settings are not affected by master reset command ($C2).
	mUARTBaud = 0x05;
	mUARTPrescale = 0x90;
	mUARTMultiplex = 0x01;
	RecomputeBaudRate();

	SoftReset();

	mCommandState = kState_WaitCommand;
	mReadBitState = 0;
	mCurrentWriteData = ~UINT32_C(0);
	mWriteBitState = 0;
	UpdatePIAInput();
	mpScheduler->UnsetEvent(mpReadBitEvent);
	mpScheduler->UnsetEvent(mpWriteBitEvent);
}

void ATXEP80Emulator::SoftReset() {
	memset(mVRAM, 0x9B, sizeof mVRAM);
	InvalidateFrame();

	mLeftMargin = 0;
	mRightMargin = 79;

	mbEscape = false;
	mbDisplayControl = false;
	mbBurstMode = false;
	mbPrinterMode = false;
	mbGraphicsMode = false;
	mbInternalCharset = false;
	mbCursorEnabled = true;
	mbCursorBlinkEnabled = false;
	mbCursorBlinkState = true;
	mbCursorReverseVideo = true;
	mbCharBlinkState = true;
	mbReverseVideo = false;
	mbReverseVideoBlinkField = false;
	mBlinkRate = 32;
	mBlinkAccum = 0;
	mBlinkDutyCycle = 4;
	mAttrA = 0xFF;
	mAttrB = 0xFF;
	mUnderlineStart = 8;
	mUnderlineEnd = 9;
	mExtraByte = 0;

	mCharWidth = 7;
	mCharHeight = 10;
	mGfxColumns = 0x30;
	mGfxRowMid = 3;
	mGfxRowBot = 6;
	mHorzCount = 105 - 1;
	mHorzBlankStart = 80 - 1;
	mHorzSyncStart = 82;
	mHorzSyncEnd = 95;
	mVertCount = 27 - 1;
	mVertBlankStart = 25 - 1;
	mVertSyncBegin = 0;
	mVertSyncEnd = 2;
	mVertStatusRow = 23;
	mVertExtraScans = 2;
	RecomputeVideoTiming();

	mbInvalidBlockGraphics = true;
	mbInvalidActiveFont = true;

	mTCP = 0;

	mX = 0;
	mY = 0;
	mLastX = 0xFF;
	mLastY = 0xFF;
	mCursorAddr = 0;
	mScrollX = 0;

	mBeginAddr = 0;
	mEndAddr = 0x1900;
	mHomeAddr = 0;
	mStatusAddr = 0;

	for(int i=0; i<25; ++i)
		mRowPtrs[i] = i;

	// set up tabs at 2 and then 7+8N
	memset(&mVRAM[0x1900], 0, 256);
	mVRAM[0x1902] = 1;

	for(uint32 i=0x1907; i<0x2000; i += 8)
		mVRAM[i] = 1;
}

void ATXEP80Emulator::InitFonts() {
	for(int j=0; j<2; ++j) {
		const uint8 *font = j ? g_ATXEP80Font_Intl : g_ATXEP80Font_Normal;
		uint16 *dst = mFonts[j];

		for(int i=0; i<128*16; ++i) {
			uint16 c = (uint16)(((uint32)font[i] << 8) & 0xfe00);

			dst[i] = c;
			dst[i + 128*16] = (uint16)(~c & 0xfe00);
		}

		// EOLs are blank
		memset(&dst[0x9B*16], 0, 32);
	}

	// set up internal font
	uint16 *dst = mFonts[2];

	for(int i=0; i<128*16; ++i) {
		uint16 c = (uint16)(((uint32)g_ATXEP80Font_Internal[i] << 8) & 0xfe00);

		dst[i] = dst[i + 128*16] = c;
	}
}

void ATXEP80Emulator::SetPortIndex(uint8 portIndex) {
	if (mPortIndex != portIndex) {
		mPortIndex = portIndex;

		UpdatePIABits();
	}
}

void ATXEP80Emulator::SetOnPrinterOutput(vdfunction<void(uint8)> fn) {
	mpOnPrinterOutput = std::move(fn);
}

void ATXEP80Emulator::Tick(uint32 ticks300Hz) {
	uint32 accum = mTickAccum + ticks300Hz;
	uint32 frames = mbPAL ? accum / 6 : accum / 5;

	mTickAccum = mbPAL ? accum % 6 : accum % 5;

	uint32 blinkPeriod = 16*mBlinkRate;
	uint32 newBlinkAccum = mBlinkAccum + 8 * frames;

	mBlinkAccum = newBlinkAccum % blinkPeriod;

	if (frames)
		InvalidateFrame();
}

void ATXEP80Emulator::UpdateFrame() {
	uint16 cursorBlinkAccum = mBlinkAccum;
	if (cursorBlinkAccum >= 8*mBlinkRate)
		cursorBlinkAccum -= 8*mBlinkRate;

	bool charBlinkState = true;

	if (!mbGraphicsMode && !((mAttrA & mAttrB) & 0x04)) {
		charBlinkState = mBlinkAccum >= mBlinkRate * mBlinkDutyCycle * 2;

		if (mbCharBlinkState != charBlinkState) {
			mbCharBlinkState = charBlinkState;

			InvalidateFrame();
		}
	}

	bool blinkState = mbCursorEnabled && (!mbCursorBlinkEnabled || cursorBlinkAccum >= mBlinkRate * mBlinkDutyCycle);
	if (mbCursorBlinkState != blinkState) {
		mbCursorBlinkState = blinkState;

		InvalidateFrame();
	}

	if (!(mFrameChangeCount & 1))
		return;

	if (!mbValidSignal)
		return;

	// Given horizontal and vertical rates in the following ranges:
	//
	//	Horizontal: 14.7KHz - 16.7KHz
	//	Vertical: 48-62Hz
	//
	// ...we can display expect sizes up to about 820x350.

	mFrame.init(((int)mHorzBlankStart + 1) * (int)mCharWidth, ((int)mVertBlankStart + 1) * (int)mCharHeight, nsVDPixmap::kPixFormat_Pal1);
	mFrame.palette = mPalette;

	VDMemset8Rect(mFrame.data, mFrame.pitch, 0, (mFrame.w + 7) >> 3, mFrame.h);

	uint8 *VDRESTRICT row = (uint8 *)mFrame.data;
	const uint16 reverseMask = mbReverseVideo ? 0xFFFF : 0x00;

	// Attribute latch format (0 = enabled):
	//
	//	D7	Block graphics (text mode only, internal charset only)
	//	D6	Blank
	//	D5	Underline
	//	D4	Double width
	//	D3	Double height (text mode only, internal charset only)
	//	D2	Blink
	//	D1	Half intensity (not connected)
	//	D0	Reverse video
	//
	const uint8 attrMask = charBlinkState ? 0 : 0x04;
	const uint8 attrs[2] = { (uint8)(mAttrA | attrMask), (uint8)(mAttrB | attrMask) };

	if (mbGraphicsMode) {
		uint32 vramaddr = mHomeAddr;
		uint8 wrapbuf[256 + 1];		// +1 for double width

		// Attribute reverse video does not work in pixel graphic mode, but global RV does.
		const uint8 rvs8 = (uint8)reverseMask;
		const uint32 bytew = (int)mHorzBlankStart + 1;
		const int pixelh = mFrame.h;
		uint32 charScan = 0;
		uint32 charRow = 0;

		for(int y=0; y<pixelh; ++y) {
			uint32 accum = 0;
			int shift = 16;
			uint16 *VDRESTRICT rowdst = (uint16 *)row;

			// read from VRAM -- note that we must translate through the character ROMs
			// if the VRAM address has been altered to be <$4000
			uint16 fetchaddr = vramaddr;
			uint32 fetchidx = 0;

			while(fetchidx < bytew) {
				const uint32 vramoffset = fetchaddr & 0x1fff;
				const uint8 *VDRESTRICT fetchsrc = mVRAM + vramoffset;

				// check if we are going to cross a chargen mode boundary
				uint32 tc = 0x2000 - vramoffset;
				if (tc > bytew - fetchidx)
					tc = bytew - fetchidx;

				// check if the character set is in the data path
				if (fetchaddr & 0x4000) {
					// no -- apply bit reverse to compensate for NS405 displaying LSB first
					// (BOOOOOO!!!) and then apply global reverse video
					for(uint32 i=0; i<tc; ++i) {
						wrapbuf[fetchidx] = ReverseBits8(fetchsrc[i]);
						++fetchidx;
					}
				} else {
					// For unknown reasons, the character row increments at character position 5 when
					// in pixel graphics mode. This is independent of the horizontal sync positions
					// and display width.
					if (fetchidx < 5 && fetchidx + tc > 5)
						tc = 5 - fetchidx;

					uint32 localCharScan = charScan;
					if (fetchidx == 5) {
						if (++localCharScan >= mCharHeight)
							localCharScan = 0;
					}

					// yes -- select character generator and appropriate row. note that our
					// chargen is already bit reversed, but needs to be extended to 8 bits
					const uint16 *VDRESTRICT chargen = &mFonts[fetchaddr & 0x2000 ? 1 : 0][localCharScan];

					for(uint32 i=0; i<tc; ++i) {
						uint8 c = fetchsrc[i];
						uint8 pat = (uint8)(chargen[c << 4] >> 8);

						if (c >= 0x80 && c != 0x9B)
							++pat;

						wrapbuf[fetchidx] = pat;
						++fetchidx;
					}
				}

				// reverse the bits in all bytes, since the NS405 displays LSB first (BOOOO!)
				// and then apply reverse video

				fetchaddr += tc;
			}
			
			// apply attributes and global reverse video
			for(uint32 i=0; i<bytew; ++i) {
				uint8 c = wrapbuf[i];

				// We're testing bit 0 here because we've already bit reversed the data for our frame buffer.
				const uint8 attr = attrs[c & 1];

				// blanking
				if ((attr & 0x48) == 0x08)
					c = 0;

				// blinking
				if (!(attr & 0x04)) {
					// if reverse video is set on this character and rvs blink field mode is
					// active, the whole character is toggled below instead of the character
					// data being inverted here
					if ((attr & 0x01) || !mbReverseVideoBlinkField)
						c = 0;
				}

				// reverse video
				if (!(attr & 0x01)) {
					// check if we also have inversion coming from blinking
					if ((attr & 0x04) || !mbReverseVideoBlinkField)
						c = ~c;
				}

				// global reverse video
				c ^= rvs8;

				// double width
				if (!(attr & 0x10)) {
					uint8 d1 = kDoubleTableLo[c >> 4];
					uint8 d2;

					switch(mCharWidth) {
					case 6:
						d2 = kDoubleTableLo[(c >> 1) & 15];
						break;

					case 7:
						d2 = kDoubleTableLo[(c >> 1) & 15] << 1;
						break;

					case 8:
						d2 = kDoubleTableLo[c & 15];
						break;
					}

					wrapbuf[i] = d1;
					++i;		// this may bump us one greater, but that's fine
					c = d2;
				}


				wrapbuf[i] = c;
			}

			switch(mCharWidth) {
				case 6:
					for(uint32 x=0; x<bytew; ++x) {
						const uint32 c = wrapbuf[x] & 0xfc;

						accum += c << shift;
						shift -= 6;

						if (shift <= 0) {
							*rowdst++ = (uint16)VDSwizzleU32(accum);
							accum <<= 16;
							shift += 16;
						}
					}
					break;

				case 7:
					for(uint32 x=0; x<bytew; ++x) {
						const uint32 c = wrapbuf[x] & 0xfe;

						accum += c << shift;
						shift -= 7;

						if (shift <= 0) {
							*rowdst++ = (uint16)VDSwizzleU32(accum);
							accum <<= 16;
							shift += 16;
						}
					}
					break;

				case 8:
					for(uint32 x=0; x<bytew; ++x) {
						uint8 c = wrapbuf[x];

						row[x] = c;
					}
					break;
			}

			if (shift < 16)
				*rowdst++ = (uint16)VDSwizzleU32(accum);

			row += mFrame.pitch;

			vramaddr += bytew;

			// mVertStatusRow = TC[8], so it's actually the last row before the status row.
			// Once we hit the status row, we stop wrapping. However, the NS405 appears to
			// have a bug where it resets the display address at the end of every scanline
			// on the row specified by TC[8], not just at the end of the row. This
			// behavior is... less than useful.
			if (vramaddr == mEndAddr && charRow <= mVertStatusRow)
				vramaddr = mBeginAddr;

			if (charRow == mVertStatusRow)
				vramaddr = mStatusAddr;

			// advance character scanline counter
			if (++charScan >= mCharHeight) {
				charScan = 0;

				++charRow;
			}

		}
	} else {
		// check if the block graphic set needs to be reinitialized
		if (mbInvalidBlockGraphics && (mAttrA & mAttrB & 0x80))
			RebuildBlockGraphics();

		if (mbInvalidActiveFont)
			RebuildActiveFont();

		int linebuf[256];
		uint16 cursorbuf[256];
		uint16 rvsbuf[256];

		const uint16 charMask = 0x10000 - (0x10000 >> mCharWidth);
		const uint16 cursormask = mbCursorBlinkState ? charMask : 0x00;

		int rows = (int)mVertBlankStart + 1;
		int cols = (int)mHorzBlankStart + 1;

		// The XEP80 uses an external row counter clocked off of horizontal
		// sync (HS). However, the NS405 actually prefetches four bytes into
		// its FIFO shortly after horizontal blank. If horizontal sync is too
		// late within horizontal blank, it results in those prefetched characters
		// using the previous character row index. This only affects ATASCII
		// as the internal charset uses an internal row counter that is unaffected.

		const int incorrectlyPrefetchedChars = std::min<int>(std::clamp<int>(mHorzSyncStart - mHorzBlankStart - 4, 0, 4), cols);
		const int incorrectlyPrefetchedChars2 = std::min<int>(std::clamp<int>(mHorzSyncStart - mHorzBlankStart - 3, 0, 5), cols);

		// crude hack to deal with row pointer count limit for now
		if (rows > 25)
			rows = 25;

		// This EOL check looks like a terrible hack, but it's correct.
		//
		// In external character generator mode, the NS405 never sees the character
		// name bytes -- they are translated through the external chargen and the
		// NS405 only sees the character shape data. This results in unusual behavior
		// that the switching between the AL0/AL1 attribute latches is based on the
		// 8th bit of the character *data* and not the name. Due to an awful hack in
		// the XEP80's character ROM where the $9B (EOL) character is a blank in the
		// middle of the inverted character set, this means that all characters in
		// the $80-FF range select AL1 except for $9B, which still selects AL0.
		//
		// In theory, the character generator ROM could cause the NS405 to switch
		// attribute latches on a per-scan basis. Thankfully, the XEP80's character
		// generator doesn't do that and so we only have to deal with this goofy EOL
		// issue.
		//
		// When the internal character set generator is active, the row addresses are
		// set to bypass the external chargen ROM and the NS405 receives character
		// names on its data bus, so it is bit 7 of the name that does the switching
		// instead.

		for(int y=0; y<rows; ++y) {
			uint32 vramaddr = ((uint32)mRowPtrs[y] << 8) + mScrollX;
			const uint16 *VDRESTRICT rowfont = mActiveFonts[vramaddr & 0x4000 ? 2 : vramaddr & 0x2000 ? 1 : 0];
			const bool internalCharset = (vramaddr & 0x4000) != 0;
			const int eolChar = internalCharset ? -1 : 0x9B;

			memset(cursorbuf, 0, sizeof(cursorbuf));

			for(int x=0; x<cols; ++x) {
				cursorbuf[x] = (mCursorAddr == ((vramaddr + x) & 0xffff)) ? cursormask : 0;

				uint8 c = mVRAM[(vramaddr + x) & 0x1fff];
				const uint8 attr = (c & 0x80) && c != eolChar ? attrs[1] : attrs[0];

				// blanking
				if ((attr & 0x48) == 0x08)
					c = 0x20;

				// blinking
				if (!(attr & 0x04)) {
					// if reverse video is set on this character and rvs blink field mode is
					// active, the whole character is toggled below instead of the character
					// data being inverted here
					if ((attr & 0x01) || !mbReverseVideoBlinkField)
						c = 0x20;
				}

				// reverse video
				uint16 rvs = 0;

				if (!(attr & 0x01)) {
					// check if we also have inversion coming from blinking
					if ((attr & 0x04) || !mbReverseVideoBlinkField)
						rvs = ~rvs;
				}

				// global reverse video
				rvs ^= reverseMask;

				if (mbCursorReverseVideo) {
					rvs ^= cursorbuf[x];
					cursorbuf[x] = 0;
				}

				rvsbuf[x] = rvs & charMask;

				int charoffset = 16*c;

				if (attr & 0x10) {
					linebuf[x] = charoffset;
				} else {
					linebuf[x] = charoffset + 0x1000;

					if (x < cols - 1) {
						++x;
						cursorbuf[x] = cursorbuf[x - 1];
						rvsbuf[x] = rvsbuf[x - 1];
						linebuf[x] = charoffset + 0x2000;
					}
				}
			}

			for(int line=0; line<mCharHeight; ++line) {
				uint32 accum = 0;
				int shift = 16;

				uint16 *rowdst = (uint16 *)row;

				// Compute prefetch timing. The NS405 seems to have a one-char
				// variance in prefetch timing, probably due to the 8048 core;
				// we drive an LFSR to provide the psuedorandom pattern. It's
				// more cyclical and less noisy to better match the actual
				// pattern on real hardware, which is probably from the idle
				// loop in the firmware.
				int prefetch = incorrectlyPrefetchedChars;

				if (mPrefetchLFSR & 1) {
					mPrefetchLFSR ^= 0x2001;
					prefetch = incorrectlyPrefetchedChars2;
				}

				mPrefetchLFSR >>= 1;

				int limit = internalCharset ? 0 : prefetch;
				int x = 0;
				for(int pass=0; pass<2; ++pass) {
					for(; x<limit; ++x) {
						const int charoffset = linebuf[x];

						accum += ((rowfont[charoffset] | cursorbuf[x]) ^ rvsbuf[x]) << shift;
						shift -= mCharWidth;

						if (shift <= 0) {
							*rowdst++ = (uint16)VDSwizzleU32(accum);
							accum <<= 16;
							shift += 16;
						}
					}

					if (!pass) {
						// end of prefetch, do the rest of the line with the
						// intended character row
						limit = cols;

						if (line)
							++rowfont;
					}
				}

				if (shift < 16) {
					*rowdst++ = (uint16)VDSwizzleU32(accum);
				}

				row += mFrame.pitch;
			}
		}
	}

	++mFrameChangeCount;
}

uint32 ATXEP80Emulator::GetFrameLayoutChangeCount() {
	if (mFrameLayoutChangeCount & 1)
		++mFrameLayoutChangeCount;

	return mFrameLayoutChangeCount;
}

uint32 ATXEP80Emulator::GetFrameChangeCount() const {
	return mFrameChangeCount;
}

const vdrect32 ATXEP80Emulator::GetDisplayArea() const {
	if (mbGraphicsMode)
		return vdrect32(0, 0, 320, 200);
	else
		return vdrect32(0, 0, mCharWidth * ((uint32)mHorzBlankStart + 1), mCharHeight * ((uint32)mVertBlankStart + 1));
}

double ATXEP80Emulator::GetPixelAspectRatio() const {
	// The XEP80 normally produces pixels at a dot clock rate of 12MHz and
	// horizontal lines at a rate of 16326Hz (text) or 16129Hz (graphics).
	//
	// Adjusting the horizontal rate back to standard 15735Hz gives
	// equivalent dot clocks of 11.565656MHz and 11.706863MHz.
	//
	// Square pixel rates give us the pixel aspect ratio (PAR):
	//	Non-interlaced NTSC: 6.13535MHz
	//	Non-interlaced PAL:  7.375MHz

	const double squarePixelRate = mbPAL ? 7.375 : 6.13635;
	const double dotClock = 12.0 * 15735.0 / (mbGraphicsMode ? 16129.0 : 16326.0);

	return squarePixelRate / dotClock;
}

uint32 ATXEP80Emulator::GetDataReceivedCount() {
	if (mDataReceivedCount & 1)
		++mDataReceivedCount;

	return mDataReceivedCount;
}

const ATXEP80TextDisplayInfo ATXEP80Emulator::GetTextDisplayInfo() const {
	ATXEP80TextDisplayInfo info;

	if (mbGraphicsMode) {
		info.mColumns = 0;
		info.mRows = 0;
	} else {
		info.mColumns = 80;
		info.mRows = 25;
	}

	return info;
}

const vdpoint32 ATXEP80Emulator::PixelToCaretPos(const vdpoint32& pixelPos) const {
	if (mbGraphicsMode)
		return vdpoint32(0, 0);

	int cx = (pixelPos.x + (pixelPos.x < 0 ? -(int)mCharWidth/2 : (int)mCharWidth/2)) / (int)mCharWidth;
	int cy = pixelPos.y / (int)mCharHeight;

	if (cy < 0) {
		cx = 0;
		cy = 0;
	} else if (cy >= 25) {
		cx = 80;
		cy = 24;
	} else {
		if (cx < 0)
			cx = 0;
		else if (cx > 80)
			cx = 80;
	}

	return vdpoint32(cx, cy);
}

const vdrect32 ATXEP80Emulator::CharToPixelRect(const vdrect32& r) const {
	const int chw = (int)mCharWidth;
	const int chh = (int)mCharHeight;

	return vdrect32(r.left * chw, r.top * chh, r.right * chw, r.bottom * chh);
}

int ATXEP80Emulator::ReadRawText(uint8 *dst, int x, int y, int n) const {
	if ((x|y) < 0)
		return 0;

	if (y > 24)
		return 0;

	if (x >= 255 - mScrollX)
		return 0;

	if (mbGraphicsMode)
		return 0;

	uint32 vramAddr = ((uint32)(mRowPtrs[y] & 0x1f) << 8) + mScrollX + x;
	int avail = 255 - (mScrollX + x);

	if (n > avail)
		n = avail;

	memcpy(dst, &mVRAM[vramAddr], n);
	return n;
}

struct ATSaveStateXEP80 final : public ATSnapExchangeObject<ATSaveStateXEP80, "ATSaveStateXEP80"> {
	template<ATExchanger T>
	void Exchange(T& ex);

	uint8 mX = 0;
	uint8 mY = 0;
	uint8 mScrollX = 0;
	uint8 mLeftMargin = 0;
	uint8 mRightMargin = 0;
	bool mbBurstMode = false;
	bool mbEscapeMode = false;
	bool mbListMode = false;
	bool mbPrinterMode = false;
	uint8 mLastChar = 0;
	uint8 mExtraByte = 0;
	uint8 mAttrLatchA = 0;
	uint8 mAttrLatchB = 0;

	uint8 mVCR = 0;
	uint16 mBEGD = 0;
	uint16 mENDD = 0;
	uint16 mHOME = 0;
	uint16 mCURS = 0;
	uint16 mSROW = 0;
	uint8 mBAUD = 0;
	uint8 mPSR = 0;
	uint8 mUMX = 0;

	uint8 mTCP = 0;
	uint8 mTimingChain[14] {};

	uint8 mRowPtrs[25] {};

	uint16 mRecvShifter = 0;
	uint8 mRecvBitIndex = 0;
	float mRecvCyclesToNextEdge = 0;

	vdfastvector<uint16> mXmitQueue;
	uint16 mXmitShifter = 0;
	uint8 mXmitBitIndex = 0;
	float mXmitCyclesToNextEdge = 0;

	vdrefptr<ATSaveStateMemoryBuffer> mpVRAM;
};

template<ATExchanger T>
void ATSaveStateXEP80::Exchange(T& ex) {
	ex.Transfer("x", &mX);
	ex.Transfer("y", &mY);
	ex.Transfer("scroll_x", &mScrollX);
	ex.Transfer("left_margin", &mLeftMargin);
	ex.Transfer("right_margin", &mRightMargin);
	ex.Transfer("burst_mode", &mbBurstMode);
	ex.Transfer("escape_mode", &mbEscapeMode);
	ex.Transfer("list_mode", &mbListMode);
	ex.Transfer("printer_mode", &mbPrinterMode);
	ex.Transfer("last_char", &mLastChar);
	ex.Transfer("extra_byte", &mExtraByte);
	ex.Transfer("attr_latch_a", &mAttrLatchA);
	ex.Transfer("attr_latch_b", &mAttrLatchB);
	ex.Transfer("vcr", &mVCR);
	ex.Transfer("begd", &mBEGD);
	ex.Transfer("endd", &mENDD);
	ex.Transfer("home", &mHOME);
	ex.Transfer("curs", &mCURS);
	ex.Transfer("srow", &mSROW);
	ex.Transfer("baud", &mBAUD);
	ex.Transfer("psr", &mPSR);
	ex.Transfer("umx", &mUMX);
	
	ex.Transfer("tcp", &mTCP);
	ex.TransferArray("timing_chain", mTimingChain);
	
	ex.TransferArray("row_ptrs", mRowPtrs);

	ex.Transfer("recv_shifter", &mRecvShifter);
	ex.Transfer("recv_bit_index", &mRecvBitIndex);
	ex.Transfer("recv_cycles_to_next_edge", &mRecvCyclesToNextEdge);

	ex.Transfer("xmit_queue", &mXmitQueue);
	ex.Transfer("xmit_shifter", &mXmitShifter);
	ex.Transfer("xmit_bit_index", &mXmitBitIndex);
	ex.Transfer("xmit_cycles_to_next_edge", &mXmitCyclesToNextEdge);

	ex.Transfer("vram", &mpVRAM);
}

void ATXEP80Emulator::LoadState(const IATObjectState *state) {
	if (!state) {
		const ATSaveStateXEP80 kDefaultState {};

		return LoadState(&kDefaultState);
	}

	const auto& xepstate = atser_cast<const ATSaveStateXEP80&>(*state);

	mX = xepstate.mX;
	mY = xepstate.mY <= 24 ? xepstate.mY : 0;
	mScrollX = xepstate.mScrollX;
	mLeftMargin = xepstate.mLeftMargin;
	mRightMargin = xepstate.mRightMargin;
	mbBurstMode = xepstate.mbBurstMode;
	mbEscape = xepstate.mbEscapeMode;
	mbDisplayControl = xepstate.mbListMode;
	mbPrinterMode = xepstate.mbPrinterMode;
	mLastChar = xepstate.mLastChar;
	mExtraByte = xepstate.mExtraByte;
	mAttrA = xepstate.mAttrLatchA;
	mAttrB = xepstate.mAttrLatchB;

	mbReverseVideoBlinkField = (xepstate.mVCR & 0x01) != 0;
	mbCursorBlinkEnabled = (xepstate.mVCR & 0x02) == 0;
	mbCursorReverseVideo = (xepstate.mVCR & 0x04) != 0;
	mbReverseVideo = (xepstate.mVCR & 0x08) != 0;
	mbGraphicsMode = (xepstate.mVCR & 0xC0) == 0xC0;
	mbInternalCharset = (xepstate.mVCR & 0x80) == 0;

	mBeginAddr = xepstate.mBEGD;
	mEndAddr = xepstate.mENDD;
	mHomeAddr = xepstate.mHOME;
	mCursorAddr = xepstate.mCURS;
	mStatusAddr = xepstate.mSROW;

	mUARTBaud = xepstate.mBAUD;
	mUARTMultiplex = xepstate.mUMX;
	mUARTPrescale = xepstate.mPSR;

	mTCP = xepstate.mTCP & 15;

	for(int i=0; i<14; ++i)
		WriteTimingChain((uint8)i, xepstate.mTimingChain[i]);

	// restore row pointers in internal RAM
	static_assert(sizeof(mRowPtrs) == sizeof(xepstate.mRowPtrs));
	memcpy(mRowPtrs, xepstate.mRowPtrs, sizeof mRowPtrs);

	// restore recv/xmit state
	mpScheduler->UnsetEvent(mpReadBitEvent);
	mpScheduler->UnsetEvent(mpWriteBitEvent);

	mCurrentData = xepstate.mRecvShifter;
	mReadBitState = 0;

	if (xepstate.mRecvBitIndex < 12) {
		mReadBitState = xepstate.mRecvBitIndex;

		const uint32 cyclesToNextRead = (uint32)(std::clamp<float>(xepstate.mRecvCyclesToNextEdge, 0.0f, 10000.0f) + 0.5f);

		if (cyclesToNextRead)
			mpScheduler->SetEvent(cyclesToNextRead, this, kEventId_ReadBit, mpReadBitEvent);
	}

	mWriteIndex = 0;
	mWriteLength = std::min<size_t>(xepstate.mXmitQueue.size(), vdcountof(mWriteBuffer));

	std::copy_n(xepstate.mXmitQueue.begin(), mWriteLength, mWriteBuffer);

	mCurrentWriteData = xepstate.mXmitShifter;
	mWriteBitState = 0;

	if (xepstate.mXmitBitIndex < 13) {
		mWriteBitState = xepstate.mXmitBitIndex;

		const float cyclesToNextWriteF = std::clamp<float>(xepstate.mXmitCyclesToNextEdge, 0.0f, 10000.0f);
		const uint32 cyclesToNextWrite = (uint32)(cyclesToNextWriteF + 0.5f);

		// The accumulator contains the rounding constant, so it should be 0x80 when
		// on an exact integer. However, it's possible to round up to 256, which we
		// should fudge slightly.
		mXmitTimingAccum = (uint8)std::clamp<int>((int)((cyclesToNextWriteF + 0.5f) - cyclesToNextWrite) * 256.0f + 0.5f, 0, 255);

		if (cyclesToNextWrite)
			mpScheduler->SetEvent(cyclesToNextWrite, this, kEventId_WriteBit, mpWriteBitEvent);
	}

	// restore video RAM
	VDBitZero(mVRAM);

	if (xepstate.mpVRAM) {
		const auto& readBuffer = xepstate.mpVRAM->GetReadBuffer();
		if (!readBuffer.empty())
			memcpy(mVRAM, readBuffer.data(), std::min<size_t>(sizeof mVRAM, readBuffer.size()));
	}

	// last X/Y are only used for cursor tracking during command processing,
	// so they don't need to be saved separately
	mLastX = mX;
	mLastY = mY;
	
	UpdatePIAInput();
	RecomputeBaudRate();
	InvalidateFrame();

	// execute xmit/recv events immediately if needed
	if (mReadBitState && !mpReadBitEvent)
		OnScheduledEvent(kEventId_ReadBit);

	if (mWriteBitState && !mpWriteBitEvent)
		OnScheduledEvent(kEventId_WriteBit);
}

vdrefptr<IATObjectState> ATXEP80Emulator::SaveState() const {
	vdrefptr xepstate { new ATSaveStateXEP80 };

	xepstate->mX = mX;
	xepstate->mY = mY;
	xepstate->mScrollX = mScrollX;
	xepstate->mLeftMargin = mLeftMargin;
	xepstate->mRightMargin = mRightMargin;
	xepstate->mbBurstMode = mbBurstMode;
	xepstate->mbEscapeMode = mbEscape;
	xepstate->mbListMode = mbDisplayControl;
	xepstate->mbPrinterMode = mbPrinterMode;
	xepstate->mLastChar = mLastChar;
	xepstate->mExtraByte = mExtraByte;
	xepstate->mAttrLatchA = mAttrA;
	xepstate->mAttrLatchB = mAttrB;

	// The default VCR value for the XEP80 is %10000110. Of these, we have
	// all but bits 4 and 5 reflected in internal state.
	xepstate->mVCR
		= (mbReverseVideoBlinkField ? 0x01 : 0x00)
		+ (mbCursorBlinkEnabled ? 0x00 : 0x02)
		+ (mbCursorReverseVideo ? 0x04 : 0x00)
		+ (mbReverseVideo ? 0x08 : 0x00)
		+ (mbGraphicsMode ? 0xC0 : mbInternalCharset ? 0x00 : 0x80)
		;

	xepstate->mBEGD = mBeginAddr;
	xepstate->mENDD = mEndAddr;
	xepstate->mHOME = mHomeAddr;
	xepstate->mCURS = mCursorAddr;
	xepstate->mSROW = mStatusAddr;

	xepstate->mBAUD = mUARTBaud;
	xepstate->mUMX = mUARTMultiplex;
	xepstate->mPSR = mUARTPrescale;

	xepstate->mTCP = mTCP;
	xepstate->mTimingChain[ 0] = mHorzCount;
	xepstate->mTimingChain[ 1] = mHorzBlankStart;
	xepstate->mTimingChain[ 2] = mHorzSyncStart;
	xepstate->mTimingChain[ 3] = mHorzSyncEnd;
	xepstate->mTimingChain[ 4] = ((mCharHeight - 1) << 4) + mVertExtraScans;
	xepstate->mTimingChain[ 5] = mVertCount;
	xepstate->mTimingChain[ 6] = mVertBlankStart;
	xepstate->mTimingChain[ 7] = (mVertSyncBegin << 4) + mVertSyncEnd;
	xepstate->mTimingChain[ 8] = mVertStatusRow;
	xepstate->mTimingChain[ 9] = ((mBlinkRate - 1) << 3) + mBlinkDutyCycle;
	xepstate->mTimingChain[10] = mGfxColumns;
	xepstate->mTimingChain[11] = (mGfxRowMid << 4) + mGfxRowBot;
	xepstate->mTimingChain[12] = (mUnderlineStart << 4) + mUnderlineEnd;
	xepstate->mTimingChain[13] = 0x0F;
	
	static_assert(sizeof(mRowPtrs) == sizeof(xepstate->mRowPtrs));
	memcpy(xepstate->mRowPtrs, mRowPtrs, sizeof xepstate->mRowPtrs);

	xepstate->mRecvShifter = mCurrentData;
	xepstate->mRecvBitIndex = mReadBitState;

	if (mpReadBitEvent)
		xepstate->mRecvCyclesToNextEdge = mpScheduler->GetTicksToEvent(mpReadBitEvent);

	xepstate->mXmitQueue.assign(&mWriteBuffer[mWriteIndex], &mWriteBuffer[mWriteLength]);
	xepstate->mXmitShifter = mCurrentWriteData;
	xepstate->mXmitBitIndex = mWriteBitState;

	if (mpWriteBitEvent) {
		xepstate->mXmitCyclesToNextEdge = mpScheduler->GetTicksToEvent(mpWriteBitEvent)
			+ ((float)mXmitTimingAccum / 256.0f) - 0.5f;
	}

	xepstate->mpVRAM = new ATSaveStateMemoryBuffer;
	xepstate->mpVRAM->mpDirectName = L"xep80-vram.bin";
	xepstate->mpVRAM->GetWriteBuffer().assign(std::begin(mVRAM), std::end(mVRAM));

	return xepstate;
}

void ATXEP80Emulator::OnPIAOutputChanged(uint32 outputState) {
	// The computer sends information to the XEP80 over the joystick
	// ports via PIA port A bits 0 and 4. Data is received in the following
	// form at 15.7KHz:
	//
	//    +--+--+--+--+--+--+--+--+--+---
	//    |D0|D1|D2|D3|D4|D5|D6|D7|D8|
	// ---+--+--+--+--+--+--+--+--+--+...
	//
	// The timing for this reception is first set when the high-to-low
	// transition is detected that signifies the start bit. From there,
	// we delay by one-half bit (57 cycles) to read the middle of D0,
	// then read at 114 bit intervals to capture D1-D8 and the stop bit.
	//
	// Read/receive states:
	//	0: waiting for start bit edge
	//	1: start bit edge seen, waiting for bit center
	//	2: sample and check start bit
	//	3-11: sample data bits
	//	12: sample stop bit (never actually stored)

	const bool data = (outputState & mPIAOutputBit) == mPIAOutputBit;

	mCurrentData = (mCurrentData & 0x1ff) + (data ? 0x200 : 0x000);

	if (mReadBitState == 0 && !data) {
		mReadBitState = 1;

		mpScheduler->SetEvent(mCyclesPerBitRecv >> 1, this, kEventId_ReadBit, mpReadBitEvent);
	}
}

void ATXEP80Emulator::OnScheduledEvent(uint32 id) {
	if (id == kEventId_ReadBit) {
		mpReadBitEvent = NULL;
		++mReadBitState;

		if (mReadBitState == 2) {
			// check if the start bit is correct... if not, we'll just abort here
			if (mCurrentData & 0x200) {
				mReadBitState = 0;
				return;
			}
		} else if (mReadBitState >= 12) {
			mReadBitState = 0;
			if (mCurrentData & 0x200) {
				OnReceiveByte(mCurrentData & 0x1ff);
				return;
			}
		}

		mpReadBitEvent = mpScheduler->AddEvent(mCyclesPerBitRecv, this, kEventId_ReadBit);

		mCurrentData = (mCurrentData >> 1) + (mCurrentData & 0x200);
	} else if (id == kEventId_WriteBit) {
		mpWriteBitEvent = NULL;

		UpdatePIAInput();

		++mWriteBitState;

		// We reach state 2 here at the leading edge of the start bit and thus
		// state 12 at the leading edge of the stop bit. At state 13, the byte
		// is finished (9 data bits + 1 stop bit). The XEP80 actually configures
		// its UART for two stop bits, but we don't wait around for it here.
		if (mWriteBitState >= 13) {
			if (mWriteIndex >= mWriteLength) {
				mCommandState = kState_WaitCommand;
				mWriteBitState = 0;
				return;
			}

			const uint32 data = mWriteBuffer[mWriteIndex++];

			g_ATLCXEPData("Sending byte %03x\n", data);

			mCurrentWriteData = ((uint32)data << 1) + 0x1c00;
			mWriteBitState = 1;

			// At 9 data bits a 2 stop bits, the UART can send a byte every
			// 64*12 = 768us. In practice the XEP80 firmware takes a bit longer
			// and actual measurements show more like 830us from start bit to
			// start bit between report bytes, which is another bit cell time.
			// Fortunately -- this just happens to be what we're set up to
			// do, and falling through here will set up the delay of one bit
			// cell between stop bit TE to start bit LE.
		}

		// The SpartaDOS X XEP80.SYS driver uses suboptimal receive
		// timing and begins sampling very shortly after the leading edge of
		// the start bit, so it is sensitive to slow transmit rates and we
		// must use high precision.

		mXmitTimingAccum += mCyclesPerBitXmitX256;
		uint32 cyclesToNextEdge = mXmitTimingAccum >> 8;
		mXmitTimingAccum &= 0xFF;

		mpWriteBitEvent = mpScheduler->AddEvent(cyclesToNextEdge, this, kEventId_WriteBit);
	}
}

const ATXEP80Emulator::CommandInfo *ATXEP80Emulator::LookupCommand(uint8 ch) {
	int lo = 0;
	int hi = (int)vdcountof(kCommands);

	while(lo < hi) {
		int mid = (lo + hi) >> 1;
		const CommandInfo& ci = kCommands[mid];

		if (ch < ci.mCommandLo)
			hi = mid;
		else if (ch > ci.mCommandHi)
			lo = mid + 1;
		else
			return &ci;
	}

	return NULL;
}

void ATXEP80Emulator::OnReceiveByte(uint32 ch) {
	const CommandInfo *pci = NULL;
	const char *cmdName = NULL;

	if (ch & 0x100) {
		pci = LookupCommand((uint8)ch);
		cmdName = pci && pci->mpName ? pci->mpName : "?";
	}

	if (pci)
		g_ATLCXEPData("(%3d,%2d) Received byte %03X (%s)\n", mX, mY, ch, cmdName);
	else if ((uint32)((ch & 0x7f) - 0x20) < 0x7d)
		g_ATLCXEPData("(%3d,%2d) Received byte %03X ('%c')\n", mX, mY, ch, (char)(ch & 0x7f));
	else
		g_ATLCXEPData("(%3d,%2d) Received byte %03X\n", mX, mY, ch);

	mDataReceivedCount |= 1;

	if (ch < 0x100) {
		OnChar((uint8)ch);
		return;
	}

	switch(mCommandState) {
		case kState_WaitCommand:
			if (ch >= 0x100) {
				if (pci) {
					g_ATLCXEPCmd("(%5d,%2d) Received command $%02X (%s)\n", mX, mY, ch & 0xff, cmdName);
					(this->*(pci->mpCmd))((uint8)ch);
					return;
				}

				g_ATLCXEPCmd("Received unknown command $%02X\n", ch & 0xff);
			}
			break;
	}
}

void ATXEP80Emulator::SendCursor(uint8 offset, uint32 delay) {
	if (mX != mLastX || mY == mLastY) {
		uint8 x = mX;

		if (x > 0x50)
			x = 0x50;

		if (mY != mLastY) {
			mWriteBuffer[offset+0] = 0x180 + x;		// horiz cursor, vert follows
			mWriteBuffer[offset+1] = 0x1e0 + mY;	// vert cursor
			BeginWrite(offset+2, delay);
		} else {
			mWriteBuffer[offset+0] = 0x100 + x;		// horiz cursor, no vert follows
			BeginWrite(offset+1, delay);
		}
	} else {
		mWriteBuffer[offset+0] = 0x1e0 + mY;		// vert cursor
		BeginWrite(offset+1, delay);
	}

	mLastX = mX;
	mLastY = mY;
}

void ATXEP80Emulator::BeginWrite(uint8 len, uint32 delay) {
	VDASSERT(len <= vdcountof(mWriteBuffer));

	mpScheduler->UnsetEvent(mpWriteBitEvent);

	if (!len) {
		mCommandState = kState_WaitCommand;
		return;
	}

	mCommandState = kState_ReturningData;
	mXmitTimingAccum = 0x80;

	mWriteIndex = 1;
	mWriteLength = len;

	g_ATLCXEPData("Sending byte %03x\n", mWriteBuffer[0]);

	// 1 start bit + 9 data bits + 3 stop bits
	mCurrentWriteData = ((uint32)mWriteBuffer[0] << 1) + 0x1c00;
	mWriteBitState = 1;

	mpWriteBitEvent = mpScheduler->AddEvent(200, this, 2);
}

void ATXEP80Emulator::OnChar(uint8 ch) {
	if (mbPrinterMode) {
		if (mpOnPrinterOutput)
			mpOnPrinterOutput(ch);
		return;
	}

	mLastChar = ch;

	if (mbGraphicsMode) {
		mVRAM[mCursorAddr & 0x1FFF] = ReverseBits8(ch);
		++mCursorAddr;
		InvalidateFrame();

		if (mbBurstMode)
			mpPIA->SetInput(mPIAInput, ~0);
		else
			SendCursor(0);
		return;
	}

	if (mbEscape) {
		mbEscape = false;
		goto not_control;
	} else if (mbDisplayControl)
		goto not_control;

	// Check if we're on the status row; if so, print chars directly except for clear.
	// Note that this must happen AFTER escape processing.
	if (mY == 24 && ch != 0x7D)
		goto not_control;

	// process control characters
	uint32 controlDelay;
	controlDelay = 1;

	switch(ch) {
		case 0x1B:	// escape
			mbEscape = true;
			break;

		case 0x1C:	// up
			if (mY < 24) {
				if (mY)
					--mY;
				else
					mY = 23;
			}

			UpdateCursorAddr();
			InvalidateCursor();
			break;

		case 0x1D:	// down
			if (mY < 24) {
				if (mY < 23)
					++mY;
				else
					mY = 0;
			}

			UpdateCursorAddr();
			InvalidateCursor();
			break;

		case 0x1E:	// left
			if (mX == mLeftMargin)
				mX = mRightMargin;
			else
				--mX;

			UpdateCursorAddr();
			InvalidateCursor();
			break;

		case 0x1F:	// right
			{
				uint8 *row = GetRowPtr(mY);
				if (row[mX] == 0x9B)
					row[mX] = 0x20;
			}

			if (mX == mRightMargin)
				mX = mLeftMargin;
			else
				++mX;

			UpdateCursorAddr();
			InvalidateCursor();
			break;

		case 0x7D:
			// Clearing the screen takes a notably long time -- measured time
			// on an XEP80 is 11.5ms from computer stop bit to XEP80 start bit.
			controlDelay = 20527;
			Clear();
			break;

		case 0x7E:	// backspace
			if (mX == mLeftMargin) {
				if (mY == 0 || GetRowPtr(mY - 1)[mRightMargin] == 0x9B)
					break;

				--mY;
				mX = mRightMargin;
			} else
				--mX;

			GetRowPtr(mY)[mX] = 0x20;
			UpdateCursorAddr();
			InvalidateFrame();
			break;

		case 0x7F:	// tab
			do {
				uint8 *row = GetRowPtr(mY);
				if (row[mX] == 0x9B)
					row[mX] = 0x20;

				if (mX == mRightMargin) {
					Advance(true);
					break;
				}

				++mX;
			} while(!mVRAM[0x1900 + mX]);
			UpdateCursorAddr();
			InvalidateCursor();
			break;

		case 0x9C:	// delete line
			DeleteLine();
			UpdateCursorAddr();
			break;

		case 0x9D:	// insert line
			InsertLine();
			UpdateCursorAddr();
			break;

		case 0x9E:	// clear tab
			mVRAM[0x1900 + mX] = 0;
			break;

		case 0x9F:	// set tab
			mVRAM[0x1900 + mX] = 1;
			break;

		case 0xFD:	// bell (no-op)
			break;

		case 0xFE:	// delete char
			DeleteChar();
			break;

		case 0xFF:	// insert char
			InsertChar();
			break;

		default:
			goto not_control;
	}

	if (mbBurstMode)
		mpPIA->SetInput(mPIAInput, ~0);
	else
		SendCursor(0, controlDelay);
	return;

not_control:

	if (ch == 0x9B) {
		mX = mRightMargin;
		Advance(false);
	} else {
		uint8 *row = GetRowPtr(mY);
		const bool overwroteEOL = row[mX] == 0x9B;

		row[mX] = ch;
		Advance(overwroteEOL);
	}

	UpdateCursorAddr();
	InvalidateFrame();

	if (mbBurstMode)
		mpPIA->SetInput(mPIAInput, ~0);
	else {
		// Measured timing for a regular put byte from leading edge of character
		// stop bit to leading edge of report byte is about 350-440us on a real
		// XEP80. The stop bit is 64us and we get the command mid-bit when the
		// stop bit is sampled, so we need a delay of ~570-730 cycles. For now,
		// we split the difference deterministically.

		SendCursor(0, 650);
	}
}

void ATXEP80Emulator::OnCmdSetCursorHPos(uint8 ch) {
	mX = ch;
	mLastX = mX;
	UpdateCursorAddr();
}

void ATXEP80Emulator::OnCmdSetCursorHPosHi(uint8 ch) {
	mX = (mX & 0x0F) + (ch << 4);
	mLastX = mX;
	UpdateCursorAddr();
}

void ATXEP80Emulator::OnCmdSetLeftMarginLo(uint8 ch) {
	mLeftMargin = (ch & 0x0f);
}

void ATXEP80Emulator::OnCmdSetLeftMarginHi(uint8 ch) {
	mLeftMargin = (mLeftMargin & 0x0f) + (ch << 4);
}

void ATXEP80Emulator::OnCmdSetCursorVPos(uint8 ch) {
	mY = ch - 0x80;
	mLastY = mY;
	UpdateCursorAddr();
}

void ATXEP80Emulator::OnCmdSetGraphics(uint8) {
	mbGraphicsMode = true;
	mCursorAddr = 0x4000;

	// 8x10 character cell
	// 40 cols displayed with 93 total -> 16.129KHz horizontal
	// 20 rows displayed with 26 total + 9 extra -> 59.96Hz vertical
	// 320x200 display

	mCharWidth = 8;
	mCharHeight = 10;
	mHorzCount = 93 - 1;
	mHorzBlankStart = 40 - 1;
	mHorzSyncStart = 59;
	mHorzSyncEnd = 70;
	mVertCount = 26 - 1;
	mVertBlankStart = 20 - 1;
	mVertSyncBegin = 7;
	mVertSyncEnd = 9;
	mVertExtraScans = 9;

	mHomeAddr = 0x4000;
	mBeginAddr = 0x0000;
	mEndAddr = 0xFFFF;

	RecomputeVideoTiming();
}

void ATXEP80Emulator::OnCmdModifyGraphics50Hz(uint8) {
	// 8x12 character cell
	// 16 rows displayed with 26 total + 11 extra -> 49.93 Hz vertical
	// 320x192 display
	mCharHeight = 12;
	mVertBlankStart = 17 - 1;
	mVertExtraScans = 11;

	RecomputeVideoTiming();
}

void ATXEP80Emulator::OnCmdSetRightMarginLo(uint8 ch) {
	// The command byte is $A0-AF, and we want $40-4F.
	mRightMargin = ch - 0xA0 + 0x40;
}

void ATXEP80Emulator::OnCmdSetRightMarginHi(uint8 ch) {
	mRightMargin = (mRightMargin & 0x0f) + (ch << 4);
}

void ATXEP80Emulator::OnCmdReadCharAndAdvance(uint8) {
	mWriteBuffer[0] = GetRowPtr(mY)[mX];

	if (mX == mRightMargin) {
		mX = mLeftMargin;

		if (mY < 24) {
			++mY;
			if (mY >= 24)
				mY = 0;
		}
	} else
		++mX;

	UpdateCursorAddr();
	InvalidateFrame();

	// Measured time on real hardware is about 940us from leading edge of
	// computer start bit to leading edge of XEP80 start bit, or around 594
	// cycles.
	SendCursor(1, 594);
}

void ATXEP80Emulator::OnCmdRequestCursorHPos(uint8) {
	mWriteBuffer[0] = mX;
	BeginWrite(1);
}

void ATXEP80Emulator::OnCmdMasterReset(uint8) {
	SoftReset();

	mWriteBuffer[0] = 0x01;
	BeginWrite(1);
}

void ATXEP80Emulator::OnCmdPrinterPortStatus(uint8) {
	mWriteBuffer[0] = 0x01;	// not busy
	BeginWrite(1);
}

void ATXEP80Emulator::OnCmdFillPrevChar(uint8) {
	memset(mVRAM, ReverseBits8(mLastChar), sizeof mVRAM);

	InvalidateFrame();

	mWriteBuffer[0] = 0x01;
	BeginWrite(1);
}

void ATXEP80Emulator::OnCmdFillSpace(uint8) {
	memset(mVRAM, 0x20, sizeof mVRAM);
	InvalidateFrame();

	mWriteBuffer[0] = 0x01;
	BeginWrite(1);
}

void ATXEP80Emulator::OnCmdFillEOL(uint8) {
	memset(mVRAM, 0x9B, sizeof mVRAM);
	InvalidateFrame();

	mWriteBuffer[0] = 0x01;
	BeginWrite(1);
}

void ATXEP80Emulator::OnCmdReadChar(uint8) {
	mWriteBuffer[0] = mVRAM[mCursorAddr & 0x1FFF];
	BeginWrite(1);
}

void ATXEP80Emulator::OnCmdReadTimerCounter(uint8) {
	// The timer counter (T) register on the XEP80's NS405 holds $00 for a text
	// mode and $03 for a graphics mode (it is used to mask raster interrupts).

	mWriteBuffer[0] = mbGraphicsMode ? 0x03 : 0x00;
	BeginWrite(1);
}

void ATXEP80Emulator::OnCmdClearListFlag(uint8) {
	// The list flag is horribly named. It is actually the analog of the
	// display control characters flag (DSPFLG) in the normal Screen Editor.
	mbDisplayControl = false;
}

void ATXEP80Emulator::OnCmdSetListFlag(uint8) {
	mbDisplayControl = true;
}

void ATXEP80Emulator::OnCmdSetNormalMode(uint8) {
	mbBurstMode = false;
	mbPrinterMode = false;
}

void ATXEP80Emulator::OnCmdSetBurstMode(uint8) {
	mbBurstMode = true;
	mbPrinterMode = false;
}

void ATXEP80Emulator::OnCmdSetCharSet(uint8 ch) {
	if (ch == 0xd6)
		mbInternalCharset = true;
	else
		mbInternalCharset = false;

	// A13 of the address is used as the external charset selector
	// A14 of the address is used to bypass the charset ROM.
	//
	// Therefore, we need:
	//	$00 - ATASCII A
	//	$20 - ATASCII B
	//	$40 - internal charset

	const uint8 chsbits = (ch - 0xd4) << 5;

	for(int i=0; i<25; ++i)
		mRowPtrs[i] = chsbits + (mRowPtrs[i] & 0x9f);

	mStatusAddr = (uint16)((uint32)mRowPtrs[24] << 8);

	mbInvalidActiveFont = true;
	InvalidateFrame();
}

void ATXEP80Emulator::OnCmdSetText50Hz(uint8 ch) {
	// 7x12 characters
	// 105 total chars -> 16.326KHz horizontal
	// 27 rows total + 3 extra scans -> 49.93Hz vertical

	mCharHeight = 12;
	mVertExtraScans = 3;

	RecomputeVideoTiming();
}

void ATXEP80Emulator::OnCmdCursorOff(uint8) {
	if (mbCursorEnabled) {
		mbCursorEnabled = false;
		mbCursorBlinkEnabled = false;

		InvalidateFrame();
	}
}

void ATXEP80Emulator::OnCmdCursorOn(uint8) {
	if (!mbCursorEnabled || mbCursorBlinkEnabled) {
		mbCursorEnabled = true;
		mbCursorBlinkEnabled = false;

		InvalidateFrame();
	}
}

void ATXEP80Emulator::OnCmdCursorOnBlink(uint8) {
	if (!mbCursorEnabled || !mbCursorBlinkEnabled) {
		mbCursorEnabled = true;
		mbCursorBlinkEnabled = true;

		InvalidateFrame();
	}
}

void ATXEP80Emulator::OnCmdMoveToLogicalStart(uint8) {
	while(mY > 0 && GetRowPtr(mY - 1)[mRightMargin] != 0x9B)
		--mY;
}

void ATXEP80Emulator::OnCmdSetScrollX(uint8 ch) {
	if (mScrollX != mX) {
		mScrollX = mX;
		InvalidateFrame();
	}
}

void ATXEP80Emulator::OnCmdSetPrinterOutput(uint8) {
	mbPrinterMode = true;
	mbBurstMode = true;
}

void ATXEP80Emulator::OnCmdSetReverseVideo(uint8 ch) {
	bool rev = (ch & 1) != 0;

	if (mbReverseVideo != rev) {
		mbReverseVideo = rev;

		InvalidateFrame();
	}
}

void ATXEP80Emulator::OnCmdSetExtraByte(uint8) {
	mExtraByte = mLastChar;
}

void ATXEP80Emulator::OnCmdSetCursorAddr(uint8) {
	mCursorAddr = (uint16)(((uint32)mLastChar << 8) + mExtraByte);
}

void ATXEP80Emulator::OnCmdWriteInternalByte(uint8) {
	// We only have partial support for this function -- only the row address table
	// can be written.
	uint8 addr = mExtraByte & 0x3F;

	if (addr >= 0x20 && addr <= 0x38) {
		mRowPtrs[addr - 0x20] = mLastChar;
		InvalidateFrame();
	}

	mExtraByte = mLastChar;
}

void ATXEP80Emulator::OnCmdWriteByte(uint8) {
	mVRAM[mCursorAddr & 0x1FFF] = mLastChar;
}

void ATXEP80Emulator::OnCmdSetHomeAddr(uint8) {
	uint16 addr = (uint16)(mExtraByte + ((uint32)mLastChar << 8));

	if (mHomeAddr != addr) {
		mHomeAddr = addr;

		InvalidateFrame();
	}
}

void ATXEP80Emulator::OnCmdWriteVCR(uint8) {
	// partial support only -- used by demo80.bas

	const uint8 v = mLastChar;

	// reverse video field blink (bit 0)
	bool rvsFieldBlink = (v & 0x01);
	if (mbReverseVideoBlinkField != rvsFieldBlink) {
		mbReverseVideoBlinkField = rvsFieldBlink;

		InvalidateFrame();
	}

	// cursor blink mode (bit 1)
	mbCursorBlinkEnabled = !(v & 0x02);

	// cursor reverse video (bit 2)
	mbCursorReverseVideo = ((v & 0x04) != 0);

	// reverse video (bit 3)
	bool rvid = (v & 0x08) != 0;
	if (mbReverseVideo != rvid) {
		mbReverseVideo = rvid;

		InvalidateFrame();
	}
}

void ATXEP80Emulator::OnCmdSetTCP(uint8) {
	mTCP = mLastChar & 15;
}

void ATXEP80Emulator::OnCmdWriteTCP(uint8) {
	WriteTimingChain(mTCP, mLastChar);
	mTCP = (mTCP + 1) & 15;
}

void ATXEP80Emulator::WriteTimingChain(uint8 reg, uint8 val) {
	switch(reg) {
		case 0:		// horizontal length register
			mHorzCount = val;
			RecomputeVideoTiming();
			break;

		case 1:		// horizontal blank start
			mHorzBlankStart = val;
			RecomputeVideoTiming();
			break;

		case 2:		// horizontal sync start
			mHorzSyncStart = val;
			RecomputeVideoTiming();
			break;

		case 3:		// horizontal sync end
			mHorzSyncEnd = val;
			RecomputeVideoTiming();
			break;

		case 4:		// character scan height / extra scans
			mCharHeight = (val >> 4) + 1;
			mVertExtraScans = val & 15;
			RecomputeVideoTiming();
			break;

		case 5:		// vertical length register
			mVertCount = val & 31;
			RecomputeVideoTiming();
			break;

		case 6:		// vertical blank start
			mVertBlankStart = val & 31;
			RecomputeVideoTiming();
			break;

		case 7:		// vertical sync start/end
			mVertSyncBegin = val >> 4;
			mVertSyncEnd = val & 15;
			RecomputeVideoTiming();
			break;

		case 8:		// status row pos (5 bits)
			mVertStatusRow = val & 31;
			break;

		case 9:		// blink rate / duty cycle
			mBlinkRate = (val >> 3) + 1;
			mBlinkDutyCycle = val & 7;
			break;

		case 10:	// graphics column register
			mGfxColumns = val;
			mbInvalidBlockGraphics = true;
			mbInvalidActiveFont = true;
			InvalidateFrame();
			break;

		case 11:	// graphics row register
			mGfxRowMid = val >> 4;
			mGfxRowBot = val & 15;
			mbInvalidBlockGraphics = true;
			mbInvalidActiveFont = true;
			InvalidateFrame();
			break;

		case 12:	// underline size register
			mUnderlineStart = val >> 4;
			mUnderlineEnd = val & 15;
			mbInvalidActiveFont = true;
			InvalidateFrame();
			break;
	}
}

void ATXEP80Emulator::OnCmdSetBeginAddr(uint8) {
	uint16 addr = (uint16)(mExtraByte + ((uint32)mLastChar << 8));

	if (mBeginAddr != addr) {
		mBeginAddr = addr;
		InvalidateFrame();
	}
}

void ATXEP80Emulator::OnCmdSetEndAddr(uint8) {
	uint16 addr = (uint16)(mExtraByte + ((uint32)mLastChar << 8));

	if (mEndAddr != addr) {
		mEndAddr = addr;
		InvalidateFrame();
	}
}

void ATXEP80Emulator::OnCmdSetStatusAddr(uint8) {
	uint16 addr = (uint16)(mExtraByte + ((uint32)mLastChar << 8));

	if (mStatusAddr != addr) {
		mStatusAddr = addr;
		InvalidateFrame();
	}
}

void ATXEP80Emulator::OnCmdSetAttrLatch(uint8 ch) {
	uint8& latch = ch & 1 ? mAttrB : mAttrA;

	if (latch != mLastChar) {
		latch = mLastChar;

		mbInvalidActiveFont = true;
		InvalidateFrame();
	}
}

void ATXEP80Emulator::OnCmdSetBaudRate(uint8) {
	mUARTPrescale = mLastChar;
	mUARTBaud = mExtraByte;

	RecomputeBaudRate();

	g_ATLCXEPCmd("Baud rate now set to %u cycles/bit receive, %.1f cycles/bit transmit\n", mCyclesPerBitRecv, (float)mCyclesPerBitXmitX256 / 256.0f);
}

void ATXEP80Emulator::OnCmdSetUMX(uint8) {
	mUARTMultiplex = mLastChar;

	RecomputeBaudRate();

	g_ATLCXEPCmd("Baud rate now set to %u cycles/bit receive, %.1f cycles/bit transmit\n", mCyclesPerBitRecv, (float)mCyclesPerBitXmitX256 / 256.0f);
}

void ATXEP80Emulator::Clear() {
	mX = mLeftMargin;
	mY = 0;

	// Clear doesn't clear the status line.
	for(int y=0; y<24; ++y)
		ClearLine(y);

	UpdateCursorAddr();
	InvalidateFrame();
}

void ATXEP80Emulator::ClearLine(int y) {
	// Clear always clears 80 chars in the current scroll window and not
	// within the left-right margins. This does mean that it is possible
	// to overflow into adjacent memory.
	//
	// Normally, the worst that can happen is some status line corruption,
	// but through direct writes to on-chip memory via command $E5 it is
	// possible to address the $1Fxx address and wrap the address space.
	// Therefore, we must catch and handle this situation.

	uint32 vramaddr = ((uint32)(mRowPtrs[y] & 0x1f) << 8) + mScrollX;
	uint8 *dst = &mVRAM[vramaddr];

	if (vramaddr + 80 > 0x2000) {
		memset(dst, 0x9B, 0x2000 - vramaddr);
		memset(mVRAM, 0x9B, vramaddr - 0x1FB0);
	} else {
		memset(dst, 0x9B, 80);
	}
}

void ATXEP80Emulator::InsertChar() {
	if (mY >= 24 || mLeftMargin >= mRightMargin || mX > mRightMargin)
		return;

	uint8 ch_in = 0x20;
	int y = mY;
	int x = mX;

	do {
		// shift current line
		uint8 *row = GetRowPtr(y);
		uint8 ch_out = row[mRightMargin];
		memmove(row + x + 1, row + x, mRightMargin - x);
		row[x] = ch_in;

		// check if we shifted out an EOL
		if (ch_out == 0x9B) {
			// check if the right margin still contains an EOL
			if (row[mRightMargin] == 0x9B) {
				// yes -- not extending logical line, so we're done
				break;
			}

			// oops... well, we need to insert another physical line after this one
			if (y < 23) {
				uint8 tmpptr = mRowPtrs[23];

				memmove(mRowPtrs + y + 2, mRowPtrs + y + 1, 22 - y);
				mRowPtrs[y + 1] = tmpptr;

				ClearLine(y + 1);
			} else 
				Scroll();
		}

		ch_in = ch_out;
		x = mLeftMargin;
	} while(++y < 24);

	// all done
	InvalidateFrame();
}

void ATXEP80Emulator::DeleteChar() {
	if (mY >= 24 || mLeftMargin >= mRightMargin)
		return;

	// find bottom of logical line
	int y = mY;
	while(y < 23 && GetRowPtr(y)[mRightMargin] != 0x9B)
		++y;

	// shift chars upward
	uint8 ch_in = 0x9B;

	while(y > mY) {
		uint8 *row = GetRowPtr(y);

		uint8 ch_out = row[mLeftMargin];
		memmove(row + mLeftMargin, row + mLeftMargin + 1, mRightMargin - mLeftMargin);
		row[mRightMargin] = ch_in;

		ch_in = ch_out;
		--y;
	}

	// shift chars on current row
	uint8 *rowcur = GetRowPtr(mY);

	if (mX < mRightMargin)
		memmove(rowcur + mX, rowcur + mX + 1, mRightMargin - mX);
	
	rowcur[mRightMargin] = ch_in;

	InvalidateFrame();
}

void ATXEP80Emulator::InsertLine() {
	if (mY >= 24)
		return;

	if (mY < 23) {
		// rotate physical lines
		uint8 tmprow = mRowPtrs[23];
		memmove(mRowPtrs + mY + 1, mRowPtrs + mY, 23 - mY);
		mRowPtrs[mY] = tmprow;
	}

	mX = mLeftMargin;

	// clear inserted line
	ClearLine(mY);
	InvalidateFrame();
}

void ATXEP80Emulator::DeleteLine() {
	if (mY >= 24)
		return;

	// get size of logical line from this line -- note that we may be in the
	// middle of a logical line, and it's intentional that we only delete from
	// this point and not the whole line, as that's what the XEP80 does
	uint8 y2 = mY;
	while(y2 < 24 && GetRowPtr(y2)[mRightMargin] != 0x9B)
		++y2;

	// rotate physical lines
	const int rows = (y2 - mY) + 1;
	if (mY < 23) {
		uint8 tmp[24];

		memcpy(tmp, mRowPtrs + mY, rows);
		memmove(mRowPtrs + mY, mRowPtrs + mY + rows, 23 - y2);
		memcpy(mRowPtrs + 24 - rows, tmp, rows);
	}

	// clear new logical lines
	for(int y = 24 - rows; y < 24; ++y)
		ClearLine(y);

	InvalidateFrame();
}	

void ATXEP80Emulator::Advance(bool extendLine) {
	if (mX != mRightMargin) {
		++mX;
		return;
	}

	mX = mLeftMargin;

	if (mY >= 24)
		return;

	++mY;
	if (mY >= 24) {
		mY = 24;
		Scroll();
	} else if (extendLine)
		InsertLine();
}

void ATXEP80Emulator::Scroll() {
	// The XEP80 always scrolls by a single line, even if the top logical line
	// has multiple physical lines. This may truncate a logical line.
	int rows = 1;

	// rotate physical lines
	if (rows < 24) {
		uint8 tmp[24];

		memcpy(tmp, mRowPtrs, rows);
		memmove(mRowPtrs, mRowPtrs+rows, 24 - rows);
		memcpy(mRowPtrs + (24 - rows), tmp, rows);
	}

	// clear new logical lines at bottom
	for(int y=24 - rows; y<24; ++y)
		ClearLine(y);

	// adjust cursor
	if (mY < rows)
		mY = 0;
	else
		mY -= rows;

	InvalidateFrame();
}

void ATXEP80Emulator::UpdateCursorAddr() {
	mCursorAddr = (uint16)((uint32)(mRowPtrs[mY] << 8) + mX);
}

void ATXEP80Emulator::InvalidateCursor() {
	InvalidateFrame();
}

void ATXEP80Emulator::InvalidateFrame() {
	mFrameChangeCount |= 1;
}

void ATXEP80Emulator::RebuildBlockGraphics() {
	mbInvalidBlockGraphics = false;

	// compute horizontal divisions
	uint16 leftMask = 0;
	uint16 rightMask = 0;
	uint16 centerMask = 0;

	switch(mCharWidth) {
		case 6:
			leftMask = (uint16)((~mGfxColumns & 0xe0) << 8);
			rightMask = (uint16)(~(mGfxColumns & 0x18) << 7);
			centerMask = (uint16)(~(leftMask + rightMask) & 0xfc00);
			break;

		case 7:
			leftMask = (uint16)((~mGfxColumns & 0xe0) << 8);
			rightMask = (uint16)((~mGfxColumns & 0x1c) << 7);
			centerMask = (uint16)(~(leftMask + rightMask) & 0xfe00);
			break;

		case 8:
			leftMask = (uint16)((~mGfxColumns & 0xe0) << 8);
			rightMask = (uint16)((~mGfxColumns & 0x1e) << 7);
			centerMask = (uint16)(~(leftMask + rightMask) & 0xff00);
			break;

		case 9:
			leftMask = (uint16)((~mGfxColumns & 0xf0) << 8);
			rightMask = (uint16)((~mGfxColumns & 0x0f) << 7);
			centerMask = (uint16)(~(leftMask + rightMask) & 0xff80);
			break;

		case 10:
			leftMask = (uint16)((~mGfxColumns & 0xf0) << 8);
			rightMask = (uint16)(((~mGfxColumns & 0x0f) << 7) + ((~mGfxColumns & 1) << 6));
			centerMask = (uint16)(~(leftMask + rightMask) & 0xffc0);
			break;
	}

	// compute vertical divisions
	int rowTable[16];

	for(int i=0, maskIdx = 0; i<16; ++i) {
		if (i == mGfxRowMid)
			maskIdx = 1;

		if (i == mGfxRowBot)
			maskIdx = 2;

		rowTable[i] = maskIdx;
	}

	uint16 *VDRESTRICT dst = mFonts[3];
	for(int c=0; c<256; ++c) {
		const uint16 masks[3] = {
			(uint16)(((c & 0x01) ? leftMask + rightMask : 0) + ((c & 0x02) ? centerMask : 0)),
			(uint16)(((c & 0x04) ? leftMask : 0) + ((c & 0x08) ? centerMask : 0) + ((c & 0x10) ? rightMask : 0)),
			(uint16)(((c & 0x20) ? leftMask + rightMask : 0) + ((c & 0x40) ? centerMask : 0))
		};

		for(int i=0; i<16; ++i) {
			*dst++ = masks[rowTable[i]];
		}
	}
}

void ATXEP80Emulator::RebuildActiveFont() {
	mbInvalidActiveFont = false;

	static const uint16 kDoubleTable4[16]={
		0x0000, 0x0300, 0x0c00, 0x0f00,
		0x3000, 0x3300, 0x3c00, 0x3f00,
		0xc000, 0xc300, 0xcc00, 0xcf00,
		0xf000, 0xf300, 0xfc00, 0xff00
	};

	const uint16 *src;

	// Because the XEP80 allows switching of the charset on a line basis via A13
	// and bypassing of the charset ROM via A14, we need to generate three derived
	// charsets: normal, int'l, and internal.

	for(int charSet = 0; charSet < 3; ++charSet) {
		uint16 *dstbase = mActiveFonts[charSet];
		uint16 *dst = dstbase;
		
		const uint16 charMask = (uint16)(0x10000 - (0x10000 >> mCharWidth));
		for(int attrLatch = 0; attrLatch < 2; ++attrLatch) {
			uint8 attr = attrLatch ? mAttrB : mAttrA;

			// select source font (not used for charSet==2)
			src = (charSet == 2) ? mFonts[attr & 0x80 ? 2 : 3] : mFonts[charSet & 1];
			
			if (attrLatch)
				src += 0x80 * 16;

			// compute underline locations
			uint8 underlineMasks[16] = {0};

			if (!(attr & 0x20)) {
				int underlineStart = (mUnderlineStart + 1) & 15;
				int underlineEnd = (mUnderlineEnd + 1) & 15;

				while(underlineStart != underlineEnd) {
					underlineMasks[underlineStart] = 0xFF;

					underlineStart = (underlineStart + 1) & 15;
				}
			}

			// Generate half of a font.
			//
			// A couple of tricky considerations here:
			//
			// - Double-height only works with the internal charsets. This is because it works
			//   by stepping the internal line counter at half rate, and in external charset
			//   mode the row address is stepped by an external counter instead, so the
			//   charset ROM doesn't see the double height addressing. However, we still have
			//   to act on it if block graphics is enabled.
			//
			// - Block graphics requires the external charset ROM to be bypassed since the
			//   internal block graphics generator within the NS405 needs to activate instead.
			//   Problem is, the way the NS405 knows to go into block graphic mode is via the
			//   attribute latch selection through bit 7, which has already either gone or
			//   not gone through the charset ROM based on A14. Therefore, block graphics only
			//   works properly in internal charset mode; in external charset mode you get
			//   the charset graphics translated instead which is useless. It is possible to
			//   fix this via internal commands to move selected rows to bypass mode, but we
			//   do not cover that here.
			//
			// This means we have the following cases to deal with:
			// - Normal lookup
			//   * External charset enabled
			//   * External charset bypassed + internal graphics
			//   * External charset bypassed + block graphics
			// - Double lookup
			//   * External charset enabled + block graphics
			//   * External charset enabled + internal graphics
			// - Direct graphics
			//   * External charset bypassed

			if (charSet == 2 && !mbInternalCharset && (attr & 0x80)) {
				for(int choffset = 0; choffset < 128; ++choffset) {
					// external and internal chargens both bypassed -- direct graphics
					const uint16 bits = (uint16)(((uint32)ReverseBits8(choffset) + attrLatch) << 8);

					for(int line=0; line<16; ++line)
						*dst++ = (bits | underlineMasks[line]) & charMask;
				}
			} else if (charSet < 2 && (mbInternalCharset || !(attr & 0x80))) {
				//
				// Both external and internal chargen active.
				//
				// Extending the cluster*#$ here is that because the NS405 serializes bits
				// in LSB-to-MSB order instead of MSB-to-LSB as ANTIC does, the XEP80 has
				// the data bus for the character ROM hooked backwards. That would be fine
				// except that we store the charset in M2L order for speed, which is backwards
				// for looking up the block graphics or internal charset here.
				//

				const uint16 *VDRESTRICT intGen = mFonts[mbInternalCharset ? 2 : 3];

				for(int choffset = 0; choffset < 128; ++choffset) {
					for(int line=0; line<16; ++line) {
						uint8 c = ReverseBits8((uint8)(src[line] >> 8));

						const uint16 *VDRESTRICT intbits = &intGen[c << 4];

						uint16 bits;
						if (attr & 0x08) {
							bits = intbits[line];
						} else if (!(attr & 0x40))	// double height, lower half
							bits = intbits[(line + mCharHeight) >> 1];
						else						// double height, upper half
							bits = intbits[line >> 1];

						// underline
						bits |= underlineMasks[line];

						*dst++ = bits & charMask;
					}

					src += 16;
				}
			} else if (mbInternalCharset || !(attr & 0x80)) {
				// only internal chargen active -- double height functioning
				for(int choffset = 0; choffset < 128; ++choffset) {
					for(int line=0; line<16; ++line) {
						uint16 bits;
						if (attr & 0x08) {
							bits = src[line];
						} else if (!(attr & 0x40))	// double height, lower half
							bits = src[(line + mCharHeight) >> 1];
						else						// double height, upper half
							bits = src[line >> 1];

						// underline
						bits |= underlineMasks[line];

						*dst++ = bits & charMask;
					}

					src += 16;
				}
			} else {
				// only external chargen active -- double height non-functional
				for(int choffset = 0; choffset < 128; ++choffset) {
					for(int line=0; line<16; ++line) {
						uint16 bits = src[line];

						// underline
						bits |= underlineMasks[line];

						*dst++ = bits & charMask;
					}

					src += 16;
				}
			}
		}

		// generate double-width charsets
		switch(mCharWidth) {
			case 6:
				src = dstbase;
				for(int i=0; i<4096; ++i)
					*dst++ = (uint16)(kDoubleTable4[*src++ >> 12] & 0xfc00);

				src = dstbase;
				for(int i=0; i<4096; ++i)
					*dst++ = (uint16)(kDoubleTable4[(*src++ >> 9) & 14] & 0xfc00);

				break;

			case 7:
				src = dstbase;
				for(int i=0; i<4096; ++i)
					*dst++ = (uint16)(kDoubleTable4[*src++ >> 12] & 0xfe00);

				src = dstbase;
				for(int i=0; i<4096; ++i)
					*dst++ = (uint16)((kDoubleTable4[(*src++ >> 9) & 15] << 1) & 0xfe00);

				break;

			case 8:
				src = dstbase;
				for(int i=0; i<4096; ++i)
					*dst++ = kDoubleTable4[*src++ >> 12];

				src = dstbase;
				for(int i=0; i<4096; ++i)
					*dst++ = kDoubleTable4[(*src++ >> 8) & 15];

				break;

			case 9:
				src = dstbase;
				for(int i=0; i<4096; ++i)
					dst[i] = (uint16)((kDoubleTable4[src[i] >> 12] + kDoubleTableLo[(src[i] >> 8) & 15]) & 0xff80);

				src = dstbase;
				for(int i=0; i<4096; ++i)
					dst[i] = (uint16)(((kDoubleTable4[src[i] >> 8] + kDoubleTableLo[(src[i] >> 4) & 15]) << 1) & 0xff80);

				break;

			case 10:
				src = dstbase;
				for(int i=0; i<4096; ++i)
					dst[i] = (uint16)((kDoubleTable4[src[i] >> 12] + kDoubleTableLo[(src[i] >> 8) & 15]) & 0xffc0);

				src = dstbase;
				for(int i=0; i<4096; ++i)
					dst[i] = (uint16)((kDoubleTable4[src[i] >> 7] + kDoubleTableLo[(src[i] >> 3) & 15]) & 0xffc0);

				break;
		}
	}
}

void ATXEP80Emulator::RecomputeBaudRate() {
	// Base clock is 12MHz.
	// Master clock divisor is /1.
	// UART divisor is /16.
	// Standard settings are /8 prescale and /6 divisor, for a baud rate of 15625Hz.

	const double baserate = 12000000.0 / 16.0;
	const double prescale = (double)(int)(mUARTPrescale >> 4) / 2.0 + 3.5;
	const double divisor = (double)(((mUARTPrescale & 7) << 4) + mUARTBaud + 1);

	// The fastest possible clock is 214KHz, so this will never be 0. Note that we
	// are using the NTSC clock rate here; PAL shouldn't be far enough off to matter
	// here, though.
	double cyclesPerBit = mpScheduler->GetRate().asDouble() / baserate * prescale * divisor;
	double cyclesPerBit2 = cyclesPerBit;

	for(int i=5; i>0; --i) {
		if (mUARTMultiplex & (1 << i)) {
			cyclesPerBit2 *= (float)(1 << i);
			break;
		}
	}

	if (mUARTMultiplex & 0x80) {
		mCyclesPerBitXmitX256 = VDRoundToInt32(cyclesPerBit * 256);
		mCyclesPerBitRecv = VDRoundToInt32(cyclesPerBit2);
	} else {
		mCyclesPerBitXmitX256 = VDRoundToInt32(cyclesPerBit2 * 256);
		mCyclesPerBitRecv = VDRoundToInt32(cyclesPerBit);
	}
}

void ATXEP80Emulator::RecomputeVideoTiming() {
	const int cols = (int)mHorzCount + 1;
	const int rows = (int)mVertCount + 1;
	const int w = (int)mCharWidth * cols;
	const int h = rows * (int)mCharHeight + (mVertExtraScans < mCharHeight ? mVertExtraScans + 1 : 0);

	const float sysclock = 12000000.0f;

	mHorzRate = sysclock / (float)w;
	mVertRate = mHorzRate / (float)h;

	// check for invalid parameters
	mbValidSignal = true;

	if (mHorzBlankStart >= mHorzCount || mVertBlankStart >= mVertCount)
		mbValidSignal = false;

	// check for valid horizontal and vertical rates
	if (mHorzRate < 14700 || mHorzRate > 16700)
		mbValidSignal = false;

	if (mVertRate >= 48 && mVertRate <= 52)
		mbPAL = true;
	else if (mVertRate >= 58 && mVertRate <= 62)
		mbPAL = false;
	else
		mbValidSignal = false;

	mFrameLayoutChangeCount |= 1;
	InvalidateFrame();
}

void ATXEP80Emulator::UpdatePIABits() {
	mPIAInputBit = 0x02 << (4 * mPortIndex);
	mPIAOutputBit = 0x01 << (4 * mPortIndex);

	if (mpPIA && mPIAOutput >= 0)
		mpPIA->ModifyOutputMask(mPIAOutput, mPIAOutputBit);

	UpdatePIAInput();
}

void ATXEP80Emulator::UpdatePIAInput() {
	if (mpPIA)
		mpPIA->SetInput(mPIAInput, (mWriteBitState == 0 || (mCurrentWriteData & (1 << (mWriteBitState-1))) ? mPIAInputBit : 0) + ~mPIAInputBit);
}

///////////////////////////////////////////////////////////////////////////

class ATDeviceXEP80 : public ATDevice
					, public IATDeviceScheduling
					, public IATDeviceVideoOutput
					, public IATDeviceDiagnostics
					, public IATDevicePrinterPort
					, public IATDeviceParent
					, public IATDeviceSnapshot
{
public:
	ATDeviceXEP80();

	virtual void *AsInterface(uint32 id) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void GetSettings(ATPropertySet& pset) override;
	bool SetSettings(const ATPropertySet& pset) override;
	void ColdReset() override;
	void Init() override;
	void Shutdown() override;

public:	// IATDeviceScheduling
	virtual void InitScheduling(ATScheduler *sch, ATScheduler *slowsch) override;

public:	// IATDeviceVideoOutput
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

public:	// IATDeviceDiagnostics
	void DumpStatus(ATConsoleOutput& output) override;

public:	// IATDevicePrinterPort
	void SetPrinterDefaultOutput(IATPrinterOutput *out) override;

public:	// IATDeviceParent
	IATDeviceBus *GetDeviceBus(uint32 index) override;

public:	// IATDeviceSnapshot
	void LoadState(const IATObjectState *state, ATSnapshotContext& ctx) override;
	vdrefptr<IATObjectState> SaveState(ATSnapshotContext& ctx) const override;

private:
	static sint32 ReadByte(void *thisptr0, uint32 addr);
	static bool WriteByte(void *thisptr0, uint32 addr, uint8 value);

	ATScheduler *mpScheduler = nullptr;
	IATDevicePortManager *mpPIA = nullptr;
	vdrefptr<IATPrinterOutput> mpDefaultPrinter;

	ATDeviceVideoInfo mVideoInfo;
	ATDeviceBusSingleChild mParallelBus;

	ATXEP80Emulator mXEP80;
	uint8 mPortIndex = 1;
};

void ATCreateDeviceXEP80(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceXEP80> p(new ATDeviceXEP80);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefXEP80 = { "xep80", "xep80", L"XEP80", ATCreateDeviceXEP80 };

ATDeviceXEP80::ATDeviceXEP80() {
	mXEP80.SetOnPrinterOutput(
		[this](uint8 c) {
			if (auto *printer = mParallelBus.GetChild<IATPrinterOutput>())
				printer->WriteASCII(&c, 1);
			else if (mpDefaultPrinter)
				mpDefaultPrinter->WriteASCII(&c, 1);
		}
	);
}

void *ATDeviceXEP80::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceScheduling::kTypeID:
			return static_cast<IATDeviceScheduling *>(this);

		case IATDeviceVideoOutput::kTypeID:
			return static_cast<IATDeviceVideoOutput *>(this);

		case IATDeviceDiagnostics::kTypeID:
			return static_cast<IATDeviceDiagnostics *>(this);

		case IATDevicePrinterPort::kTypeID:
			return static_cast<IATDevicePrinterPort *>(this);

		case IATDeviceParent::kTypeID:
			return static_cast<IATDeviceParent *>(this);

		case IATDeviceSnapshot::kTypeID:
			return static_cast<IATDeviceSnapshot *>(this);

		default:
			return ATDevice::AsInterface(id);
	}
}

void ATDeviceXEP80::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefXEP80;
}

void ATDeviceXEP80::GetSettingsBlurb(VDStringW& buf) {
	buf.sprintf(L"Port %u", mPortIndex + 1);
}

void ATDeviceXEP80::GetSettings(ATPropertySet& pset) {
	pset.SetUint32("port", mPortIndex + 1);
}

bool ATDeviceXEP80::SetSettings(const ATPropertySet& pset) {
	const uint8 portIndex = (uint8)((pset.GetUint32("port") - 1) & 3);

	if (mPortIndex != portIndex) {
		mPortIndex = portIndex;

		mXEP80.SetPortIndex(portIndex);
	}

	return true;
}

void ATDeviceXEP80::ColdReset() {
	mXEP80.ColdReset();
}

void ATDeviceXEP80::Init() {
	mpPIA = GetService<IATDevicePortManager>();

	mParallelBus.Init(this, 0, IATPrinterOutput::kTypeID, "parallel", L"Parallel Printer Port", "parport");

	mXEP80.SetPortIndex(mPortIndex);
	mXEP80.Init(mpScheduler, mpPIA);

	GetService<IATDeviceVideoManager>()->AddVideoOutput(this);
}

void ATDeviceXEP80::Shutdown() {
	mParallelBus.Shutdown();

	GetService<IATDeviceVideoManager>()->RemoveVideoOutput(this);

	mXEP80.Shutdown();

	mpPIA = nullptr;
	mpScheduler = nullptr;
}

void ATDeviceXEP80::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
}

const char *ATDeviceXEP80::GetName() const {
	return "xep80";
}

const wchar_t *ATDeviceXEP80::GetDisplayName() const {
	return L"XEP80";
}

void ATDeviceXEP80::Tick(uint32 ticks300Hz) {
	mXEP80.Tick(ticks300Hz);
}

void ATDeviceXEP80::UpdateFrame() {
	mXEP80.UpdateFrame();
}

const VDPixmap& ATDeviceXEP80::GetFrameBuffer() {
	return mXEP80.GetFrameBuffer();
}

const ATDeviceVideoInfo& ATDeviceXEP80::GetVideoInfo() {
	mVideoInfo.mbSignalPassThrough = false;
	mVideoInfo.mbSignalValid = mXEP80.IsVideoSignalValid();
	mVideoInfo.mHorizScanRate = mXEP80.GetVideoHorzRate();
	mVideoInfo.mVertScanRate = mXEP80.GetVideoVertRate();
	mVideoInfo.mFrameBufferLayoutChangeCount = mXEP80.GetFrameLayoutChangeCount();
	mVideoInfo.mFrameBufferChangeCount = mXEP80.GetFrameChangeCount();

	const auto textDisplayInfo = mXEP80.GetTextDisplayInfo();
	mVideoInfo.mTextRows = textDisplayInfo.mRows;
	mVideoInfo.mTextColumns = textDisplayInfo.mColumns;

	mVideoInfo.mPixelAspectRatio = mXEP80.GetPixelAspectRatio();
	mVideoInfo.mDisplayArea = mXEP80.GetDisplayArea();

	mVideoInfo.mBorderColor = 0;
	mVideoInfo.mbForceExactPixels = false;

	return mVideoInfo;
}

vdpoint32 ATDeviceXEP80::PixelToCaretPos(const vdpoint32& pixelPos) {
	return mXEP80.PixelToCaretPos(pixelPos);
}

vdrect32 ATDeviceXEP80::CharToPixelRect(const vdrect32& r) {
	return mXEP80.CharToPixelRect(r);
}

int ATDeviceXEP80::ReadRawText(uint8 *dst, int x, int y, int n) {
	return mXEP80.ReadRawText(dst, x, y, n);
}

uint32 ATDeviceXEP80::GetActivityCounter() {
	return mXEP80.GetDataReceivedCount();
}

void ATDeviceXEP80::DumpStatus(ATConsoleOutput& output) {
}

void ATDeviceXEP80::SetPrinterDefaultOutput(IATPrinterOutput *out) {
	mpDefaultPrinter = out;
}

IATDeviceBus *ATDeviceXEP80::GetDeviceBus(uint32 index) {
	return index ? nullptr : &mParallelBus;
}

void ATDeviceXEP80::LoadState(const IATObjectState *state, ATSnapshotContext& ctx) {
	mXEP80.LoadState(state);
}

vdrefptr<IATObjectState> ATDeviceXEP80::SaveState(ATSnapshotContext& ctx) const {
	return mXEP80.SaveState();
}
