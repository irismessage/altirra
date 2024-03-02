//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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
#include "antic.h"
#include "gtia.h"
#include "console.h"
#include "savestate.h"
#include "scheduler.h"

enum {
	kATAnticEvent_UpdateRegisters = 1
};

ATAnticEmulator::ATAnticEmulator()
	: mHaltedCycles(0)
	, mX(0)
	, mY(0)
	, mFrame(0)
	, mAnalysisMode(kAnalyzeOff)
	, mpRegisterUpdateEvent(NULL)
	, mRegisterUpdateHeadIdx(0)
{
	SetPALMode(false);
}

ATAnticEmulator::~ATAnticEmulator() {
}

void ATAnticEmulator::Init(ATAnticEmulatorConnections *mem, ATGTIAEmulator *gtia, ATScheduler *sch) {
	mpConn = mem;
	mpGTIA = gtia;
	mpScheduler = sch;

	memset(mActivityMap, 0, sizeof mActivityMap);
}

void ATAnticEmulator::SetPALMode(bool pal) {
	mScanlineLimit = pal ? 312 : 262;
	mScanlineMax = mScanlineLimit - 1;
	mVSyncStart = pal ? 275 : 251;
}

void ATAnticEmulator::ColdReset() {
	++mFrame;
	mX = 0;
	mY = 0;

	mPFDMAPtr = 0;
	mDLControlPrev = 0;
	mDLControl = 0;
	mRowCounter = 0;
	mbRowStopUseVScroll = false;
	mbDLActive = true;
	mbPFDMAEnabled = false;
	mbPFDMAActive = false;
	mbWSYNCActive = false;
	mWSYNCPending = 0;
	mDMACTL = 0;
	mCHACTL = 0;
	mDLIST = 0;
	mHSCROL = 0;
	mVSCROL = 0;
	mPMBASE = 0;
	mCHBASE = 0;
	mCharBaseAddr128 = 0;
	mCharBaseAddr64 = 0;
	mCharInvert = 0;
	mCharBlink = 0xff;
	mNMIEN = 0;
	mNMIST = 0x1F;
	mVCOUNT = 0;
	mPFWidth = kPFDisabled;
	mPFWidthShift = 0;
	mPFDMAPatternCacheKey = 0xFFFFFFFF;
	mbHScrollEnabled = false;
	mGTIAHSyncOffset = 110;
	mbInBuggedVBlank = false;

	UpdatePlayfieldTiming();

	mRegisterUpdates.clear();
	mRegisterUpdateHeadIdx = 0;

	if (mpRegisterUpdateEvent) {
		mpScheduler->RemoveEvent(mpRegisterUpdateEvent);
		mpRegisterUpdateEvent = NULL;
	}
}

void ATAnticEmulator::WarmReset() {
	mNMIEN = 0;
	mNMIST = 0x1F;
	mWSYNCPending = 0;
	mbWSYNCActive = false;

	mRegisterUpdates.clear();
	mRegisterUpdateHeadIdx = 0;

	if (mpRegisterUpdateEvent) {
		mpScheduler->RemoveEvent(mpRegisterUpdateEvent);
		mpRegisterUpdateEvent = NULL;
	}
}

void ATAnticEmulator::RequestNMI() {
	mNMIST |= 0x20;
	mpConn->AnticAssertNMI();
}

bool ATAnticEmulator::AdvanceSpecial() {
	bool busActive = false;
	if (mX == 0) {		// missile DMA
		if ((mDMACTL & 0x0C) && (uint32)(mY - 8) < 240) {	// player DMA also forces missile DMA (Run For the Money requires this).
			if (mDMACTL & 0x10) {
				uint8 byte = mpConn->AnticReadByte(((mPMBASE & 0xf8) << 8) + 0x0300 + mY);
				busActive = true;
				mpGTIA->UpdateMissile((mY & 1) != 0, byte);
			} else {
				// DMA occurs every scanline even in half-height mode.
				uint8 byte = mpConn->AnticReadByte(((mPMBASE & 0xfc) << 8) + 0x0180 + (mY >> 1));
				busActive = true;
				mpGTIA->UpdateMissile((mY & 1) != 0, byte);
			}
		}

		mbDLDMAEnabledInTime = (mDMACTL & 0x20) != 0;
		mDLISTLatch = mDLIST;
	} else if (mX == 1) {
		mbDLExtraLoadsPending = false;
		mbPFDMAEnabled = false;
		mbPFDMAActive = false;
		mPFDMALatchedStart = 0;
		mPFDMALatchedEnd = 0;
		mPFFetchWidthLatchedStart = kPFDisabled;
		mPFFetchWidthLatchedEnd = kPFDisabled;
		mPFDisplayStart = 110;
		mPFDisplayEnd = 110;
		mDLHistory[mY].mbValid = false;

		// Display start is at scanline 8.
		if (mY == 8) {
			mbDLActive = true;
			mRowCounter = 0;
			mRowCount = 1;
			mbRowStopUseVScroll = false;
			mbRowAdvance = false;

			// Note that we MUST NOT clear mDLControlPrev here. If the display list extends all
			// the way to scan line 248, ANTIC still remembers the status of the vertical scroll
			// bit.
			mDLControl = mDLControlPrev;
		}

		// compute stop line
		uint32 rowStop = mbRowStopUseVScroll ? mLatchedVScroll : ((mRowCount - 1) & 15);

		if (mRowCounter != rowStop) {
			mRowCounter = (mRowCounter + 1) & 15;

			mbPFDMAActive = true;
		} else {
			mRowCounter = 0;
			
			if (mbDLActive) {
				mDLControlPrev = mDLControl;

				DLHistoryEntry& ent = mDLHistory[mY];
				ent.mDLAddress = mDLISTLatch;
				ent.mPFAddress = mPFDMAPtr;
				ent.mHVScroll = mHSCROL + (mVSCROL << 4);
				ent.mDMACTL = mDMACTL;
				ent.mbValid = true;

				if (mbDLDMAEnabledInTime) {
					mDLControl = mpConn->AnticReadByte(mDLISTLatch);

					busActive = true;
					mDLIST = (mDLIST & 0xFC00) + ((mDLIST + 1) & 0x03FF);
				}

				ent.mControl = mDLControl;

				uint8 mode = mDLControl & 0x0f;
				if (mode == 1 || (mode >= 2 && (mDLControl & 0x40)))
					mbDLExtraLoadsPending = true;

				mRowCounter = 0;
				mPFPushCycleMask = 0;
				mPFWidthShift = 0;

				mPFPushMode = k160;
				mPFHiresMode = false;

				switch(mode) {
				case 0:
					mRowCount = ((mDLControl >> 4) & 7) + 1;
					mPFPushMode = kBlank;
					break;
				case 1:
					mRowCount = 1;
					mPFPushMode = kBlank;
					break;
				case 2:						// IR mode 2: 40x8 characters, 2 colors/1 lum
					mRowCount = 8;
					mPFPushCycleMask = 1;	// 320 pixels normal
					mPFPushMode = k320;
					mPFWidthShift = 2;
					mPFHiresMode = true;
					break;
				case 3:						// IR mode 3: 40x10 characters, 2 colors/1 lum
					mRowCount = 10;
					mPFPushCycleMask = 1;	// 320 pixels normal
					mPFPushMode = k320;
					mPFWidthShift = 2;
					mPFHiresMode = true;
					break;
				case 4:						// IR mode 4: 40x8 characters, 5 colors
					mRowCount = 8;
					mPFPushCycleMask = 1;	// 160 pixels normal
					mPFWidthShift = 2;
					break;
				case 5:						// IR mode 5: 40x16 characters, 5 colors
					mRowCount = 16;
					mPFPushCycleMask = 1;	// 160 pixels normal
					mPFWidthShift = 2;
					break;
				case 6:						// IR mode 6: 20x8 characters, 5 colors
					mRowCount = 8;
					mPFPushCycleMask = 3;	// 160 pixels normal
					mPFWidthShift = 1;
					break;
				case 7:						// IR mode 7: 20x16 characters, 5 colors
					mRowCount = 16;
					mPFPushCycleMask = 3;	// 160 pixels normal
					mPFWidthShift = 1;
					break;
				case 8:						// IR mode 8: 40x8 graphics, 4 colors
					mRowCount = 8;
					mPFPushCycleMask = 7;	// 40 pixels normal
					mPFWidthShift = 0;
					break;
				case 9:						// IR mode 9: 80x4 graphics, 2 colors
					mRowCount = 4;
					mPFPushCycleMask = 7;	// 40 pixels normal
					mPFWidthShift = 0;
					break;
				case 10:					// IR mode A: 80x4 graphics, 4 colors
					mRowCount = 4;
					mPFPushCycleMask = 3;	// 80 pixels normal
					mPFWidthShift = 1;
					break;
				case 11:					// IR mode B: 160x2 graphics, 2 colors
					mRowCount = 2;
					mPFPushCycleMask = 3;	// 80 pixels normal
					mPFWidthShift = 1;
					break;
				case 12:					// IR mode C: 160x1 graphics, 2 colors
					mRowCount = 1;
					mPFPushCycleMask = 3;	// 160 pixels normal
					mPFWidthShift = 1;
					break;
				case 13:					// IR mode D: 160x2 graphics, 4 colors
					mRowCount = 2;
					mPFPushCycleMask = 1;	// 160 pixels normal
					mPFWidthShift = 2;
					break;
				case 14:					// IR mode E: 160x1 graphics, 4 colors
					mRowCount = 1;
					mPFPushCycleMask = 1;	// 160 pixels normal
					mPFWidthShift = 2;
					break;
				case 15:					// IR mode F: 320x1 graphics, 2 colors/1 lum
					mRowCount = 1;
					mPFPushCycleMask = 1;	// 320 pixels normal
					mPFPushMode = k320;
					mPFWidthShift = 2;
					mPFHiresMode = true;
					break;
				}

				// check for vertical scrolling
				uint8 scrollPrev = mDLControlPrev;
				uint8 scrollCur = mDLControl;
				if ((scrollPrev & 15) < 2)
					scrollPrev = 0;
				if ((scrollCur & 15) < 2)
					scrollCur = 0;

				mbRowStopUseVScroll = false;
				if (mode != 1 && ((scrollCur ^ scrollPrev) & 0x20)) {
					if (scrollCur & 0x20)
						mRowCounter = mVSCROL;
					else
						mbRowStopUseVScroll = true;
				}

				// check for horizontal scrolling
				mbHScrollEnabled = false;
				if (mode != 1 && (scrollCur & 0x10))
					mbHScrollEnabled = true;

				mbPFDMAEnabled = true;
				mbPFDMAActive = true;
			}
		}
	} else if (mX >= 2 && mX <= 5) {		// player DMA
		if ((mDMACTL & 0x08) && (uint32)(mY - 8) < 240) {
			uint32 index = mX - 2;
			if (mDMACTL & 0x10) {
				uint8 byte = mpConn->AnticReadByte(((mPMBASE & 0xf8) << 8) + 0x400 + (0x0100 * index) + mY);
				busActive = true;
				mpGTIA->UpdatePlayer((mY & 1) != 0, index, byte);
			} else {
				// DMA occurs every scanline even in half-height mode.
				uint8 byte = mpConn->AnticReadByte(((mPMBASE & 0xfc) << 8) + 0x200 + (0x0080 * index) + (mY >> 1));
				busActive = true;
				mpGTIA->UpdatePlayer((mY & 1) != 0, index, byte);
			}
		}
	} else if (mX == 6) {		// address DMA (low)
		if (mbDLExtraLoadsPending && (mDMACTL & 0x20)) {
			mDLNext = mpConn->AnticReadByte(mDLIST);
			busActive = true;
			mDLIST = (mDLIST & 0xFC00) + ((mDLIST + 1) & 0x03FF);
		}
	} else if (mX == 7) {		// address DMA (high)
		if (mbDLExtraLoadsPending && (mDMACTL & 0x20)) {
			uint8 b = mpConn->AnticReadByte(mDLIST);
			busActive = true;
			mDLIST = (mDLIST & 0xFC00) + ((mDLIST + 1) & 0x03FF);

			uint16 addr = mDLNext + ((uint16)b << 8);
			if ((mDLControl & 0x0f) == 1) {
				mDLIST = addr;

				if (mDLControl & 0x40) {
					mbDLActive = false;

					// We have to preserve the DLI bit here, because Race In Space does a DLI on
					// the waitvbl command!
					mDLControl &= ~0x4f;
					mRowCount = 1;
				}
			} else {
				// correct display list history with new address
				DLHistoryEntry& ent = mDLHistory[mY];
				ent.mPFAddress = addr;

				mPFDMAPtr = addr;
			}
		}

		mPFRowDMAPtrBase = mPFDMAPtr & 0xf000;
		mPFRowDMAPtrOffset = mPFDMAPtr & 0x0fff;
		mEarlyNMIEN = mNMIEN;
	} else if (mX == 8) {
		mEarlyNMIEN2 = mNMIEN;
	} else if (mX == 10) {
		mbLateNMI = false;

		uint8 cumulativeNMIEN = mNMIEN & mEarlyNMIEN;
		uint8 cumulativeNMIENLate = mNMIEN & mEarlyNMIEN2 & ~mEarlyNMIEN;

		if (mY == 8) {
			mNMIST &= ~0x40;
		} else if (mY == 248) {
			mNMIST |= 0x40;
			mNMIST &= ~0x80;
			if (cumulativeNMIEN & 0x40) {
				mpConn->AnticAssertNMI();
			} else if (cumulativeNMIENLate & 0x40) {
				mbLateNMI = true;
			}

			// Note that we need to preserve the vertical scroll bit here to handle oversize DLs
			// properly.
			mDLControlPrev = mDLControl;
			mDLControl &= 0x20;

			memset(mPFDataBuffer, 0, sizeof mPFDataBuffer);
		}

		uint32 rowStop = mbRowStopUseVScroll ? mLatchedVScroll : ((mRowCount - 1) & 15);

		if ((mDLControl & 0x80) && mRowCounter == rowStop) {
			mNMIST |= 0x80;
			if (cumulativeNMIEN & 0x80) {
				mpConn->AnticAssertNMI();
			} else if (cumulativeNMIENLate & 0x80) {
				mbLateNMI = true;
			}
		}

		mPFHScrollDMAOffset = 0;
		mbHScrollDelay = false;

		if (mbHScrollEnabled) {
			mPFHScrollDMAOffset = (mHSCROL & 14) >> 1;
			mbHScrollDelay = (mHSCROL & 1) != 0;
		}

		UpdateCurrentCharRow();
		UpdatePlayfieldTiming();

		memset(mPFCharBuffer, 0, sizeof mPFCharBuffer);
		memset(mPFDecodeBuffer, 0x00, sizeof mPFDecodeBuffer);

		if ((unsigned)(mY - 8) >= 240) {
			if (mPFPushMode != k320 || !(mDMACTL & 3)) {
				mpGTIA->SetVBLANK(ATGTIAEmulator::kVBlankModeOn);
			} else {
				mpGTIA->SetVBLANK(ATGTIAEmulator::kVBlankModeBugged);

				memset(mPFDecodeBuffer, 0x0a, sizeof mPFDecodeBuffer);
			}

			if (mY == 248) {
				if (mPFPushMode == k320) {
					mbInBuggedVBlank = true;
					mVSyncShiftTime = 0;
				} else {
					mbInBuggedVBlank = false;
				}
			}
		} else {
			mpGTIA->SetVBLANK(ATGTIAEmulator::kVBlankModeOff);
			mbInBuggedVBlank = false;
		}

		mpGTIA->BeginScanline(mY, mPFPushMode == k320);

		if (mY == 248)
			mGTIAHSyncOffset = 110;

		mPFDecodeCounter = 0;
		mPFDisplayCounter = 0;
		mPFDMALastCheckX = 0;
	} else if (mX == 11) {
		if (mbLateNMI)
			mpConn->AnticAssertNMI();
	} else if (mX == 105) {
		mbWSYNCActive = false;
	} else if (mX == 109) {
		mLatchedVScroll = mVSCROL;
	} else if (mX == 111) {
		// Step row counter. We do this instead of using the incrementally stepped value, because there
		// may be DMA cycles blocked after this point.

		if (mPFDMAEnd < 127 && mbPFDMAEnabled) {
			PFWidthMode effectiveStart	= mPFFetchWidthLatchedStart	? mPFFetchWidthLatchedStart : mbPFDMAActive ? mPFFetchWidth : kPFDisabled;
			PFWidthMode effectiveEnd	= mPFFetchWidthLatchedEnd	? mPFFetchWidthLatchedEnd	: mPFFetchWidth ? mPFFetchWidth : kPFWide;

			if (effectiveStart) {
				// The baseline width in bytes is 8 for narrow, 10 for normal, and 12 for wide. However,
				// it is possible to get into weird cases where the playfield starts and ends with
				// different widths.
				uint32 bytes = 6 + effectiveStart + effectiveEnd;

				mPFDMAPtr = (mPFDMAPtr & 0xf000) + ((mPFDMAPtr + (bytes << mPFWidthShift)) & 0xfff);
			}
		}

		mVCOUNT = (mY + 1 >= mScanlineLimit) ? 0 : (mY + 1) >> 1;
	}

	if (mAnalysisMode) {
		switch(mAnalysisMode) {
		case kAnalyzeDMATiming:
			if (busActive)
				mActivityMap[mY][mX] |= 1;
			break;
		case kAnalyzeDLDMAEnabled:
			if (mDMACTL & 0x20)
				mActivityMap[mY][mX] |= 1;
			break;
		}
	}

	return busActive;
}

void ATAnticEmulator::AdvanceScanline() {
	SyncWithGTIA(0);
	mpGTIA->EndPlayfield();
	mpGTIA->EndScanline(mDLControl);
	mX = 0;

	if (++mY >= mScanlineLimit) {
		mY = 0;
		mbDLActive = false;		// necessary when DL DMA disabled for Joyride ptB

		mpConn->AnticEndFrame();

		// tell GTIA if the next field is an odd field
		mpGTIA->SetFieldPolarity(!mbInBuggedVBlank || mVSyncShiftTime < 20);

		if (mAnalysisMode)
			memset(mActivityMap, 0, sizeof mActivityMap);

		++mFrame;
	} else if (mY >= 248) {
		mbDLActive = false;		// needed because The Empire Strikes Back has a 259-line display list (!)
	}

	mpConn->AnticEndScanline();
}

void ATAnticEmulator::SyncWithGTIA(int offset) {
	Decode(offset);

	int x = mX + offset + 1;

	if (mPFDisplayEnd <= mPFDisplayStart) {
		if (mPFDisplayCounter < x)
			mPFDisplayCounter = x;

		return;
	}

	if (x > (int)mPFDisplayEnd)
		x = (int)mPFDisplayEnd;

	int limit = x;
	int xoff2 = mPFDisplayCounter;

	if (xoff2 < (int)mPFDisplayStart)
		xoff2 = (int)mPFDisplayStart;

	if (xoff2 >= limit)
		return;

	if (mbInBuggedVBlank && mY == mVSyncStart)
		mVSyncShiftTime += limit - xoff2;

	if (mPFWidth == kPFDisabled) {
		xoff2 = limit;
	} else if (!mbPFDMAActive) {
		if (mPFHiresMode) {
			for(; xoff2 < limit; ++xoff2)
				mpGTIA->UpdatePlayfield320(xoff2*2, 0);
		} else {
			for(; xoff2 < limit; ++xoff2)
				mpGTIA->UpdatePlayfield160(xoff2, 0);
		}
	} else {
		const uint8 *src = &mPFDecodeBuffer[xoff2];
		if (mPFHiresMode) {
			if (mbHScrollDelay) {
				for(; xoff2 < limit; ++xoff2) {
					uint8 a = src[-1];
					uint8 b = src[0];
					++src;
					mpGTIA->UpdatePlayfield320(xoff2*2, ((a << 2) + (b >> 2)) & 15);
				}
			} else {
				for(; xoff2 < limit; ++xoff2)
					mpGTIA->UpdatePlayfield320(xoff2*2, *src++);
			}
		} else {
			if (mbHScrollDelay) {
				for(; xoff2 < limit; ++xoff2) {
					uint8 a = src[-1];
					uint8 b = src[0];
					++src;

					mpGTIA->UpdatePlayfield160(xoff2, (a << 4) + (b >> 4));
				}
			} else {
				for(; xoff2 < limit; ++xoff2)
					mpGTIA->UpdatePlayfield160(xoff2, *src++);
			}
		}
	}

	mPFDisplayCounter = xoff2;
}

void ATAnticEmulator::Decode(int offset) {
	int limit = (int)mX + offset + 1;

	if (!(mDLControl & 8))
		limit -= 3;

	if (limit > (int)mPFDMAEnd)
		limit = (int)mPFDMAEnd;

	static const uint8 kExpand160[16]={
		0x00, 0x01, 0x02, 0x04,
		0x10, 0x11, 0x12, 0x14,
		0x20, 0x21, 0x22, 0x24,
		0x40, 0x41, 0x42, 0x44,
	};

	static const uint8 kExpand160Alt[16]={
		0x00, 0x01, 0x02, 0x08,
		0x10, 0x11, 0x12, 0x18,
		0x20, 0x21, 0x22, 0x28,
		0x80, 0x81, 0x82, 0x88,
	};

	static const uint16 kExpandMode6[4][16]={
		{ 0x0000, 0x0100, 0x1000, 0x1100, 0x0001, 0x0101, 0x1001, 0x1101, 0x0010, 0x0110, 0x1010, 0x1110, 0x0011, 0x0111, 0x1011, 0x1111 },
		{ 0x0000, 0x0200, 0x2000, 0x2200, 0x0002, 0x0202, 0x2002, 0x2202, 0x0020, 0x0220, 0x2020, 0x2220, 0x0022, 0x0222, 0x2022, 0x2222 },
		{ 0x0000, 0x0400, 0x4000, 0x4400, 0x0004, 0x0404, 0x4004, 0x4404, 0x0040, 0x0440, 0x4040, 0x4440, 0x0044, 0x0444, 0x4044, 0x4444 },
		{ 0x0000, 0x0800, 0x8000, 0x8800, 0x0008, 0x0808, 0x8008, 0x8808, 0x0080, 0x0880, 0x8080, 0x8880, 0x0088, 0x0888, 0x8088, 0x8888 },
	};

	static const uint32 kExpandMode8[16]={
		0x00000000,	0x11110000,	0x22220000,	0x44440000,
		0x00001111,	0x11111111,	0x22221111,	0x44441111,
		0x00002222,	0x11112222,	0x22222222,	0x44442222,
		0x00004444,	0x11114444,	0x22224444,	0x44444444,
	};

	static const uint32 kExpandMode9[16]={
		0x00000000, 0x11000000, 0x00110000, 0x11110000,
		0x00001100, 0x11001100, 0x00111100, 0x11111100,
		0x00000011, 0x11000011, 0x00110011, 0x11110011,
		0x00001111, 0x11001111, 0x00111111, 0x11111111,
	};

	static const uint16 kExpandModeA[16]={
		0x0000,	0x1100,	0x2200,	0x4400,
		0x0011,	0x1111,	0x2211,	0x4411,
		0x0022,	0x1122,	0x2222,	0x4422,
		0x0044,	0x1144,	0x2244,	0x4444,
	};

	static const uint16 kExpandModeB[16]={
		0x0000, 0x0100, 0x1000, 0x1100, 0x0001, 0x0101, 0x1001, 0x1101, 0x0010, 0x0110, 0x1010, 0x1110, 0x0011, 0x0111, 0x1011, 0x1111,
	};

	int x = mPFDecodeCounter;

	if (x < (int)mPFDMAStart) {
		if (mPFDMAEnd <= mPFDMAStart)
			return;

		x = mPFDMAStart;
	}

	if (x >= limit)
		return;

	const uint8 *src = &mPFDataBuffer[x - mPFDMAStart];
	const uint8 *chdata = &mPFCharBuffer[x - mPFDMAStart];

	uint8 *dst = &mPFDecodeBuffer[x];

	// In text modes, data is fetched 3 clocks in advance.
	// In graphics modes, data is fetched 4 clocks in advance.
	if (mDLControl & 8)
		dst += 4;
	else {
		dst += 6;
		chdata += 3;
	}

	switch(mDLControl & 15) {
		case 2:		// 40 column text, 1.5 colors, 8 scanlines
			for(; x < limit; x += 2) {
				uint8 c = *src;
				uint8 d = *chdata;
				src += 2;
				chdata += 2;

				uint8 himask = (c & 128) ? 0xff : 0;
				uint8 inv = himask & mCharInvert;

				d &= (~himask | mCharBlink);
				d ^= inv;
				
				dst[0] = d >> 4;
				dst[1] = d & 15;
				dst += 2;
			}
			break;

		case 3:		// 40 column text, 1.5 colors, 10 scanlines
			for(; x < limit; ++x) {
				uint8 c = *src;
				uint8 d = *chdata;
				src += 2;
				chdata += 2;

				uint8 himask = (c & 128) ? 0xff : 0;
				uint8 inv = himask & mCharInvert;
				uint8 mask = mRowCounter >= 2 ? 0xff : 0x00;

				if ((mRowCounter & 6) == 0) {
					if ((c & 0x60) != 0x60)
						mask ^= 0xff;
				}

				d &= (~himask | mCharBlink);

				d = inv ^ (mask & d);
				
				dst[0] = d >> 4;
				dst[1] = d & 15;
				dst += 2;
			}
			break;

		case 4:		// 40 column text, 5 colors, 8 scanlines
		case 5:		// 40 column text, 5 colors, 16 scanlines
			for(; x < limit; x += 2) {
				uint8 c = *src;
				uint8 d = *chdata;
				src += 2;
				chdata += 2;

				if (c >= 128) {
					dst[1] = kExpand160Alt[d & 15];
					dst[0] = kExpand160Alt[d >> 4];
				} else {
					dst[1] = kExpand160[d & 15];
					dst[0] = kExpand160[d >> 4];
				}

				dst += 2;
			}
			break;

		case 6:		// 20 column text, 5 colors, 8 scanlines
		case 7:		// 20 column text, 5 colors, 16 scanlines
			for(; x < limit; x += 4) {
				uint8 c = *src;
				src += 4;

				uint8 d = *chdata;
				chdata += 4;

				const uint16 *tbl = kExpandMode6[c >> 6];
				*(uint16 *)(dst+0) = tbl[d >> 4];
				*(uint16 *)(dst+2) = tbl[d & 15];
				dst += 4;
			}
			break;

		case 8:
			for(; x < limit; x += 8) {
				uint8 c = *src;
				src += 8;
				*(uint32 *)(dst + 0) = kExpandMode8[c >> 4];
				*(uint32 *)(dst + 4) = kExpandMode8[c & 15];
				dst += 8;
			}
			break;

		case 9:
			for(; x < limit; x += 8) {
				uint8 c = *src;
				src += 8;
				*(uint32 *)(dst + 0) = kExpandMode9[c >> 4];
				*(uint32 *)(dst + 4) = kExpandMode9[c & 15];
				dst += 8;
			}
			break;

		case 10:
			for(; x < limit; x += 4) {
				uint8 c = *src;
				src += 4;
				*(uint16 *)(dst+0) = kExpandModeA[c >> 4];
				*(uint16 *)(dst+2) = kExpandModeA[c & 15];
				dst += 4;
			}
			break;

		case 11:
		case 12:
			for(; x < limit; x += 4) {
				uint8 c = *src;
				src += 4;
				*(uint16 *)(dst+0) = kExpandModeB[c >> 4];
				*(uint16 *)(dst+2) = kExpandModeB[c & 15];
				dst += 4;
			}
			break;

		case 13:
		case 14:
			for(; x < limit; x += 2) {
				uint8 c = *src;
				src += 2;
				dst[0] = kExpand160[c >> 4];
				dst[1] = kExpand160[c & 15];
				dst += 2;
			}
			break;

		case 15:
			for(; x < limit; x += 2) {
				uint8 c = *src;
				src += 2;
				dst[0] = c >> 4;
				dst[1] = c & 15;
				dst += 2;
			}
			break;
	}

	mPFDecodeCounter = x;
}

uint8 ATAnticEmulator::ReadByte(uint8 reg) const {
	switch(reg) {
		case 0x0B:
			// There is a one cycle delay between the time that VCOUNT increments and when
			// it is reset to zero for the beginning of the next frame. The incremented
			// cycle is seen on cycle 111; it is cleared on cycle 112 if it is wrong and
			// should be zero.
			if (mY == mScanlineMax && mX == 111)
				return (uint8)(mScanlineLimit >> 1);

			return mVCOUNT;

		case 0x0E:
			return 0xFF;		// needed or else Karateka breaks

		case 0x0F:
			return mNMIST;

		default:
//			__debugbreak();
			break;
	}

	return 0xFF;
}

void ATAnticEmulator::WriteByte(uint8 reg, uint8 value) {
	switch(reg) {
		case 0x00:	// DMACTL [D400]
			// Ugh. We need to check whether we have crossed the current start or stop boundaries
			// and latch PF start/end as necessary. This reflects the fact that you can't change
			// the DMA start and stop boundaries once you already cross them.
			if (mPFWidth) {
				uint32 cycleRange = mX - mPFDMALastCheckX;

				const int offset = (mDLControl & 15) < 8 ? 26-23 : 28-23;

				if ((uint32)((mPFDMAStart - offset) - mPFDMALastCheckX) <= cycleRange) {
					mPFDMALatchedStart = mPFDMAStart;
					mPFFetchWidthLatchedStart = mPFFetchWidth;
				}

				if ((uint32)((mPFDMAEnd - offset) - mPFDMALastCheckX) <= cycleRange) {
					mPFDMALatchedEnd = mPFDMAEnd;
					mPFFetchWidthLatchedEnd = mPFFetchWidth;
				}
			}

			mPFDMALastCheckX = mX;

			SyncWithGTIA(0);
			mDMACTL = value;
			switch(mDMACTL & 3) {
			case 0:
				mPFWidth = kPFDisabled;
				break;
			case 1:
				mPFWidth = kPFNarrow;
				break;
			case 2:
				mPFWidth = kPFNormal;
				break;
			case 3:
				mPFWidth = kPFWide;
				break;
			}

			UpdatePlayfieldTiming();
			break;

		case 0x01:
			SyncWithGTIA(0);
			mCHACTL = value;
			mCharInvert = (mCHACTL & 0x02) ? 0xFF : 0x00;
			mCharBlink = (mCHACTL & 0x01) ? 0x00 : 0xFF;
			break;

		case 0x02:
			mDLIST = (mDLIST & 0xff00) + value;
			break;

		case 0x03:
			mDLIST = (mDLIST & 0xff) + (value << 8);
			break;

		case 0x04:
			SyncWithGTIA(0);
			mHSCROL = value & 15;
			break;

		case 0x05:
			mVSCROL = value & 15;
			break;

		case 0x07:
			mPMBASE = value;
			break;

		case 0x09:	// $D409 CHBASE
			QueueRegisterUpdate(2, reg, value);
			break;

		case 0x0A:	// $D40A WSYNC
			if (!mWSYNCPending || (mWSYNCPending == 1 && mX == 104)) {
				mWSYNCPending = 2;
			}
			break;

		case 0x0E:
			mNMIEN = value;
			break;

		case 0x0F:	// NMIRES
			mNMIST = 0x1F;
			break;

		default:
//			__debugbreak();
			break;
	}
}

void ATAnticEmulator::DumpStatus() {
	ATConsolePrintf("DMACTL = %02x  : %s%s%s%s%s\n"
		, mDMACTL
		, (mDMACTL&3) == 0 ? "none"
		: (mDMACTL&3) == 1 ? "narrow"
		: (mDMACTL&3) == 2 ? "normal"
		: "wide"
		, mDMACTL & 0x04 ? " missiles" : ""
		, mDMACTL & 0x08 ? " players" : ""
		, mDMACTL & 0x10 ? " 1-line" : " 2-line"
		, mDMACTL & 0x20 ? " dlist" : ""
		);
	ATConsolePrintf("CHACTL = %02x  :%s%s%s\n"
		, mCHACTL
		, mCHACTL & 0x04 ? " reflect" : ""
		, mCHACTL & 0x02 ? " invert" : ""
		, mCHACTL & 0x01 ? " blank" : ""
		);
	ATConsolePrintf("CHBASE = %02x\n", mCHBASE);
	ATConsolePrintf("DLIST  = %04x\n", mDLIST);
	ATConsolePrintf("HSCROL = %02x\n", mHSCROL);
	ATConsolePrintf("VSCROL = %02x\n", mVSCROL);
	ATConsolePrintf("PMBASE = %02x\n", mPMBASE);
	ATConsolePrintf("CHBASE = %02x\n", mCHBASE);
	ATConsolePrintf("NMIEN  = %02x  :%s%s\n"
		, mNMIEN
		, mNMIEN & 0x80 ? " dli" : ""
		, mNMIEN & 0x40 ? " vbi" : ""
		);
	ATConsolePrintf("NMIST  = %02x  :%s%s%s\n"
		, mNMIST
		, mNMIST & 0x80 ? " dli" : ""
		, mNMIST & 0x40 ? " vbi" : ""
		, mNMIST & 0x20 ? " reset" : ""
		);
}

void ATAnticEmulator::DumpDMAPattern() {
	char buf[116];
	buf[114] = '\n';
	buf[115] = 0;

	for(int i=0; i<114; ++i)
		buf[i] = (i >= 100) && !(i % 10) ? '1' : ' ';

	ATConsoleWrite(buf);

	for(int i=0; i<114; ++i)
		buf[i] = (i % 10) || (i < 10) ? ' ' : '0' + ((i / 10) % 10);

	ATConsoleWrite(buf);

	for(int i=0; i<114; ++i)
		buf[i] = '0' + (i % 10);

	ATConsoleWrite(buf);

	for(int i=0; i<114; ++i) {
		if (mbDMAPattern[i]) {
			switch(mDMAPFFetchPattern[i]) {
				case 0:
					buf[i] = 'R';
					break;
				case 1:
					buf[i] = 'F';
					break;
				case 2:
				case 3:
					buf[i] = 'C';
					break;
			}
		} else {
			buf[i] = '.';
		}
	}

	if (mDMACTL & 0x0C) {
		buf[0] = 'M';

		if (mDMACTL & 0x08) {
			buf[2] = 'P';
			buf[3] = 'P';
			buf[4] = 'P';
			buf[5] = 'P';
		}
	}

	if (mDMACTL & 0x20)
		buf[1] = 'D';

	ATConsoleWrite(buf);
	ATConsoleWrite("\n");
	ATConsoleWrite("Legend: (M)issile (P)layer (D)isplayList (R)efresh Play(F)ield (C)haracter\n");
	ATConsoleWrite("\n");
}

template<class T>
void ATAnticEmulator::ExchangeState(T& io) {
	io != mX;
	io != mY;
	io != mFrame;
	io != mScanlineLimit;
	io != mScanlineMax;

	io != mbDLExtraLoadsPending;
	io != mbDLActive;
	io != mbDLDMAEnabledInTime;
	io != mPFDisplayCounter;
	io != mPFDecodeCounter;
	io != mPFDMALastCheckX;
	io != mbPFDMAEnabled;
	io != mbPFDMAActive;
	io != mbWSYNCActive;
	io != mbWSYNCRelease;
	io != mbHScrollEnabled;
	io != mbHScrollDelay;
	io != mbRowStopUseVScroll;
	io != mbRowAdvance;
	io != mRowCounter;
	io != mRowCount;
	io != mLatchedVScroll;

	io != mPFDMAPtr;
	io != mPFRowDMAPtrBase;
	io != mPFRowDMAPtrOffset;
	io != mPFPushCycleMask;

	io != mPFCharFetchPtr;

	io != mPFWidthShift;
	io != mPFHScrollDMAOffset;

	io != mPFPushMode;

	io != mPFHiresMode;

	io != mPFWidth;
	io != mPFFetchWidth;
	io != mPFFetchWidthLatchedStart;
	io != mPFFetchWidthLatchedEnd;

	io != mPFCharSave;

	io != mPFDisplayStart;
	io != mPFDisplayEnd;
	io != mPFDMAStart;
	io != mPFDMAEnd;
	io != mPFDMALatchedStart;
	io != mPFDMALatchedEnd;
	io != mPFDMAPatternCacheKey;

	io != mDLControlPrev;
	io != mDLControl;
	io != mDLNext;

	io != mbDMAPattern;
	io != mDMAPFFetchPattern;

	io != mPFDataBuffer;
	io != mPFCharBuffer;

	io != mPFDecodeBuffer;

	io != mDLISTLatch;

	io != mWSYNCPending;
}

void ATAnticEmulator::LoadState(ATSaveStateReader& reader) {
	mDMACTL	= reader.ReadUint8();
	mCHACTL	= reader.ReadUint8();
	mDLIST	= reader.ReadUint16();
	mHSCROL	= reader.ReadUint8();
	mVSCROL	= reader.ReadUint8();
	mPMBASE	= reader.ReadUint8();
	mCHBASE	= reader.ReadUint8();
	mNMIEN	= reader.ReadUint8();
	mNMIST	= reader.ReadUint8();

	ExchangeState(reader);

	if (mpRegisterUpdateEvent) {
		mpScheduler->RemoveEvent(mpRegisterUpdateEvent);
		mpRegisterUpdateEvent = NULL;
	}

	mRegisterUpdates.clear();
	mRegisterUpdateHeadIdx = 0;

	uint32 updateCount = reader.ReadUint32();
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	while(updateCount--) {
		QueuedRegisterUpdate ru;
		ru.mTime = t + reader.ReadUint32();
		ru.mReg = reader.ReadUint8();
		ru.mValue = reader.ReadUint8();
	}

	switch(mDMACTL & 3) {
	case 0:
		mPFWidth = kPFDisabled;
		break;
	case 1:
		mPFWidth = kPFNarrow;
		break;
	case 2:
		mPFWidth = kPFNormal;
		break;
	case 3:
		mPFWidth = kPFWide;
		break;
	}

	mCharInvert = (mCHACTL & 0x02) ? 0xFF : 0x00;
	mCharBlink = (mCHACTL & 0x01) ? 0x00 : 0xFF;
	mCharBaseAddr128 = (uint32)(mCHBASE & 0xfc) << 8;
	mCharBaseAddr64 = (uint32)(mCHBASE & 0xfe) << 8;
	UpdateCurrentCharRow();
	UpdatePlayfieldDataPointers();

	ExecuteQueuedUpdates();
}

void ATAnticEmulator::SaveState(ATSaveStateWriter& writer) {
	writer.WriteUint8(mDMACTL);
	writer.WriteUint8(mCHACTL);
	writer.WriteUint16(mDLIST);
	writer.WriteUint8(mHSCROL);
	writer.WriteUint8(mVSCROL);
	writer.WriteUint8(mPMBASE);
	writer.WriteUint8(mCHBASE);
	writer.WriteUint8(mNMIEN);
	writer.WriteUint8(mNMIST);

	ExchangeState(writer);

	uint32 i = mRegisterUpdateHeadIdx;
	uint32 n = mRegisterUpdates.size();
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	writer.WriteUint32(n - i);
	for(; i<n; ++i) {
		const QueuedRegisterUpdate& ru = mRegisterUpdates[i];

		writer.WriteUint32(ru.mTime - t);
		writer.WriteUint8(ru.mReg);
		writer.WriteUint8(ru.mValue);
	}
}

void ATAnticEmulator::UpdateDMAPattern(int dmaStart, int dmaEnd, uint8 mode) {
	uint32 key = (dmaStart << 16) + (dmaEnd << 8) + mode + (mbPFDMAActive ? 0x8000 : 0x0000) + (mbPFDMAEnabled ? 0x4000 : 0x0000);

	if (key != mPFDMAPatternCacheKey) {
		mPFDMAPatternCacheKey = key;

		uint8 textFetchMode = 0;
		switch(mode) {
			case 2:
			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
				textFetchMode = 2;
				break;
		}

		memset(mbDMAPattern, 0, sizeof(mbDMAPattern));
		memset(mDMAPFFetchPattern, 0, sizeof(mDMAPFFetchPattern));

		// Playfield DMA
		//
		// Modes 2-5: Every 2 from 10/18/26.
		// Modes 6-7: Every 4 from 10/18/26.
		// Modes 8-9: Every 8 from 12/20/28.
		// Modes A-C: Every 4 from 12/20/28.
		// Modes D-F: Every 2 from 12/20/28.
		//
		// DMA is delayed by one clock for every 2 in HSCROL.

		int cycleStep = mPFPushCycleMask + 1;

		if (mbPFDMAActive) {
			if (mbPFDMAEnabled) {
				for(int x=mPFDMAStart; x<(int)mPFDMAEnd; x += cycleStep) {
					mbDMAPattern[x] = true;
					mDMAPFFetchPattern[x] = 1;
				}
			}

			// Character DMA
			//
			// Modes 2-5: Every 2 from 13/21/29.
			// Modes 6-7: Every 4 from 13/21/29.
			//
			// The character fetch always occurs 3 clocks after the playfield fetch.

			if (textFetchMode) {
				int textFetchDMAEnd = mPFDMAEnd + 3;
				if (textFetchDMAEnd > 106)
					textFetchDMAEnd = 106;
				for(int x=mPFDMAStart + 3; x<textFetchDMAEnd; x += cycleStep) {
					mDMAPFFetchPattern[x] = textFetchMode;
					mbDMAPattern[x] = true;
				}
			}
		}

		// Memory refresh
		//
		// This is very simple. Refresh does 9 cycles every 4 starting at cycle 25.
		// If DMA is already occurring, the refresh occurs on the next available cycle.
		// If refresh hasn't gotten a chance to run by the time the next refresh cycle
		// occurs, it is simply dropped. The latest a refresh cycle will ever run is
		// 106, which happens on a wide 40 char badline.
		int r = 24;
		for(int x=25; x<61; x += 4) {
			if (r >= x)
				continue;

			r = x;

			while(r < 107) {
				if (!mbDMAPattern[r++]) {
					mbDMAPattern[r-1] = true;
					break;
				}
			}
		}

		// Mark off special cycles.

		mDMAPFFetchPattern[  0] |= 0x80;	// Missile DMA
		mDMAPFFetchPattern[  1] |= 0x80;	// Display list DMA
		mDMAPFFetchPattern[  2] |= 0x80;	// Player DMA
		mDMAPFFetchPattern[  3] |= 0x80;	// Player DMA
		mDMAPFFetchPattern[  4] |= 0x80;	// Player DMA
		mDMAPFFetchPattern[  5] |= 0x80;	// Player DMA
		mDMAPFFetchPattern[  6] |= 0x80;	// Display list DMA
		mDMAPFFetchPattern[  7] |= 0x80;	// Display list DMA
		mDMAPFFetchPattern[  8] |= 0x80;	// NMI
		mDMAPFFetchPattern[ 10] |= 0x80;	// NMI
		mDMAPFFetchPattern[ 11] |= 0x80;	// NMI
		mDMAPFFetchPattern[105] |= 0x80;	// WSYNC end
		mDMAPFFetchPattern[109] |= 0x80;
		mDMAPFFetchPattern[111] |= 0x80;
	}

	if (mAnalysisMode == kAnalyzeDMATiming) {
		for(int i=16; i<114; ++i) {
			if (mbDMAPattern[i])
				mActivityMap[mY][i] |= 1;
		}
	}
}

void ATAnticEmulator::UpdateCurrentCharRow() {
	mPFCharMask = 0x7f;

	switch(mDLControl & 15) {
	case 2:
	case 3:
	case 4:
		mPFCharFetchPtr = mCharBaseAddr128 + ((mCHACTL & 4 ? 7 : 0) ^ (mRowCounter & 7));
		break;
	case 5:
		mPFCharFetchPtr = mCharBaseAddr128 + ((mCHACTL & 4 ? 7 : 0) ^ (mRowCounter >> 1));
		break;
	case 6:
		mPFCharFetchPtr = mCharBaseAddr64 + ((mCHACTL & 4 ? 7 : 0) ^ (mRowCounter & 7));
		mPFCharMask = 0x3f;
		break;
	case 7:
		mPFCharFetchPtr = mCharBaseAddr64 + ((mCHACTL & 4 ? 7 : 0) ^ (mRowCounter >> 1));
		mPFCharMask = 0x3f;
		break;
	}
}

void ATAnticEmulator::UpdatePlayfieldTiming() {
	mPFFetchWidth = mPFWidth;
	if (mbHScrollEnabled && mPFFetchWidth != kPFDisabled && mPFFetchWidth != kPFWide)
		mPFFetchWidth = (PFWidthMode)((int)mPFFetchWidth + 1);

	bool pfActive = (uint32)(mX - mPFDisplayStart) < (uint32)(mPFDisplayEnd - mPFDisplayStart);

	switch(mPFWidth) {
		case kPFDisabled:
			mPFDisplayStart = 110;
			mPFDisplayEnd = 110;
			break;
		case kPFNarrow:
			mPFDisplayStart = 32;
			mPFDisplayEnd = 96;
			break;
		case kPFNormal:
			mPFDisplayStart = 24;
			mPFDisplayEnd = 104;
			break;
		case kPFWide:
			mPFDisplayStart = 22;
			mPFDisplayEnd = 110;
			break;
	}

	if (pfActive && mX < mPFDisplayStart || mX >= mPFDisplayEnd)
		mGTIAHSyncOffset = mX;

	mPFDMAStart = 127;
	mPFDMAEnd = 127;

	uint8 mode = mDLControl & 15;

	if (mode >= 2) {
		switch(mPFFetchWidth) {
			case kPFDisabled:
				break;
			case kPFNarrow:
				mPFDMAStart = mode < 8 ? 26 : 28;
				mPFDMAEnd = mPFDMAStart + 64;
				break;
			case kPFNormal:
				mPFDMAStart = mode < 8 ? 18 : 20;
				mPFDMAEnd = mPFDMAStart + 80;
				break;
			case kPFWide:
				mPFDMAStart = mode < 8 ? 10 : 12;
				mPFDMAEnd = mPFDMAStart + 96;
				break;
		}

		mPFDMAStart += mPFHScrollDMAOffset;
		mPFDMAEnd += mPFHScrollDMAOffset;

		if (mPFFetchWidth) {
			if (mPFDMALatchedStart)
				mPFDMAStart = mPFDMALatchedStart;

			if (mPFDMALatchedEnd)
				mPFDMAEnd = mPFDMALatchedEnd;
		}

		// Timing in the plasma section of RayOfHope is very critical... it expects to
		// be able to change the DLI pointer between WSYNC and the next DLI.
		//
		// Update: According to Bennet's graph, DMA is not allowed to extend beyond clock
		// cycle 105. Playfield DMA is terminated past that point.
		//
		if (mPFDMAEnd > 106)
			mPFDMAEnd = 106;
	}

	UpdatePlayfieldDataPointers();

	// Check whether playfield DMA should be active. Playfield DMA should be active if it
	// is enabled and if we have already seen or will see the DMA start. Otherwise, either
	// playfield DMA is turned off or we have already missed the start point on the
	// scanline.

	// Note that this check must be <= because we can be hit on cycle 10.
	mbPFDMAActive = mPFDMALatchedStart || mX <= std::max<uint32>(10, mPFDMAStart - 4);

	UpdateDMAPattern(mPFDMAStart, mPFDMAEnd, mDLControl & 15);
}

void ATAnticEmulator::UpdatePlayfieldDataPointers() {
	mpPFDataWrite = mPFDataBuffer - mPFDMAStart;
	mpPFDataRead = mPFDataBuffer - mPFDMAStart - 3;
}

void ATAnticEmulator::OnScheduledEvent(uint32 id) {
	if (id == kATAnticEvent_UpdateRegisters) {
		mpRegisterUpdateEvent = NULL;

		ExecuteQueuedUpdates();
	}
}

void ATAnticEmulator::QueueRegisterUpdate(uint32 delay, uint8 reg, uint8 value) {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	uint32 i = mRegisterUpdateHeadIdx;
	uint32 n = mRegisterUpdates.size();
	uint32 j = n;

	while(j > i) {
		const QueuedRegisterUpdate& ru = mRegisterUpdates[j - 1];

		if (ru.mTime - t <= delay)
			break;
	}

	QueuedRegisterUpdate ru;
	ru.mTime = t + delay;
	ru.mReg = reg;
	ru.mValue = value;
	mRegisterUpdates.insert(mRegisterUpdates.begin() + j, ru);

	if (j == i) {
		if (mpRegisterUpdateEvent)
			mpScheduler->RemoveEvent(mpRegisterUpdateEvent);

		mpRegisterUpdateEvent = mpScheduler->AddEvent(delay, this, kATAnticEvent_UpdateRegisters);
	}
}

void ATAnticEmulator::ExecuteQueuedUpdates() {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	uint32 n = mRegisterUpdates.size();

	while(mRegisterUpdateHeadIdx < n) {
		const QueuedRegisterUpdate& ru = mRegisterUpdates[mRegisterUpdateHeadIdx];

		if ((sint32)(ru.mTime - t) > 0)
			break;

		++mRegisterUpdateHeadIdx;

		const uint8 reg = ru.mReg;
		const uint8 value = ru.mValue;

		switch(reg) {
			case 0x09:		// [D409] CHBASE
				SyncWithGTIA(0);
				mCHBASE = value;
				mCharBaseAddr128 = (uint32)(mCHBASE & 0xfc) << 8;
				mCharBaseAddr64 = (uint32)(mCHBASE & 0xfe) << 8;
				UpdateCurrentCharRow();
				break;
		}
	}

	if (n > 32 && mRegisterUpdateHeadIdx > (n >> 1)) {
		mRegisterUpdates.erase(mRegisterUpdates.begin(), mRegisterUpdates.begin() + mRegisterUpdateHeadIdx);
		n -= mRegisterUpdateHeadIdx;
		mRegisterUpdateHeadIdx = 0;
	}

	if (mRegisterUpdateHeadIdx != n) {
		VDASSERT(!mpRegisterUpdateEvent);
		mpRegisterUpdateEvent = mpScheduler->AddEvent(mRegisterUpdates[mRegisterUpdateHeadIdx].mTime - t, this, kATAnticEvent_UpdateRegisters);
	}
}
