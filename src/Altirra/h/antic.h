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

#ifndef AT_ANTIC_H
#define AT_ANTIC_H

#ifdef _MSC_VER
	#pragma once
#endif

#ifndef VDFORCEINLINE
#define VDFORCEINLINE __forceinline
#endif

#include <vd2/system/vdtypes.h>
#include "scheduler.h"

class ATGTIAEmulator;
class ATSaveStateReader;
class ATSaveStateWriter;
class ATScheduler;

class ATAnticEmulatorConnections {
public:
	VDFORCEINLINE uint8 AnticReadByteFast(uint16 address) {
		const uint8 *page = mpAnticReadPageMap[address >> 8];

		return page ? page[address & 0xff] : AnticReadByte(address);
	}

	virtual uint8 AnticReadByte(uint16 address) = 0;
	virtual void AnticAssertNMI() = 0;
	virtual void AnticEndFrame() = 0;
	virtual void AnticEndScanline() = 0;
	virtual bool AnticIsNextCPUCycleWrite() = 0;

protected:
	const uint8 *const *mpAnticReadPageMap;
};

class ATAnticEmulator : public IATSchedulerCallback {
public:
	ATAnticEmulator();
	~ATAnticEmulator();

	enum AnalysisMode {
		kAnalyzeOff,
		kAnalyzeDMATiming,
		kAnalyzeDLDMAEnabled,
		kAnalyzeModeCount
	};

	uint16	GetDisplayListPointer() const { return mDLIST; }
	uint32	GetTimestamp() const { return (mFrame << 20) + (mY << 8) + mX; }
	uint32	GetFrameCounter() const { return mFrame; }
	uint32	GetBeamX() const { return mX; }
	uint32	GetBeamY() const { return mY; }
	uint32	GetHaltedCycleCount() const { return mHaltedCycles; }

	AnalysisMode	GetAnalysisMode() const { return mAnalysisMode; }
	void			SetAnalysisMode(AnalysisMode mode) { mAnalysisMode = mode; }

	bool IsPlayfieldDMAEnabled() const {
		return (mDMACTL & 0x03) != 0;
	}

	bool IsVBIEnabled() const {
		return (mNMIEN & 0x40) != 0;
	}

	void	SetPALMode(bool pal);

	struct DLHistoryEntry {
		uint16	mDLAddress;
		uint16	mPFAddress;
		uint8	mHVScroll;
		uint8	mDMACTL;
		uint8	mControl;
		bool	mbValid;
	};

	const DLHistoryEntry *GetDLHistory() const { return mDLHistory; }
	const uint8 *GetActivityMap() const { return mActivityMap[0]; }

	void Init(ATAnticEmulatorConnections *conn, ATGTIAEmulator *gtia, ATScheduler *sch);
	void ColdReset();
	void WarmReset();
	void RequestNMI();
	VDFORCEINLINE bool Advance();
	void SyncWithGTIA(int offset);
	void Decode(int offset);

	uint8 ReadByte(uint8 reg) const;
	void WriteByte(uint8 reg, uint8 value);

	void DumpStatus();
	void DumpDMAPattern();

	void	LoadState(ATSaveStateReader& reader);
	void	SaveState(ATSaveStateWriter& writer);

protected:
	template<class T>
	void	ExchangeState(T& io);

	bool	AdvanceSpecial();
	void	AdvanceScanline();
	void	UpdateDMAPattern(int dmaStart, int dmaEnd, uint8 mode);
	void	UpdateCurrentCharRow();
	void	UpdatePlayfieldTiming();
	void	UpdatePlayfieldDataPointers();

	void	OnScheduledEvent(uint32 id);
	void	QueueRegisterUpdate(uint32 delay, uint8 reg, uint8 value);
	void	ExecuteQueuedUpdates();

	uint32	mHaltedCycles;
	uint32	mX;
	uint32	mY;
	uint32	mFrame;
	uint32	mScanlineLimit;
	uint32	mScanlineMax;
	uint32	mVSyncStart;

	bool	mbDLExtraLoadsPending;
	bool	mbDLActive;
	bool	mbDLDMAEnabledInTime;
	int		mPFDisplayCounter;
	int		mPFDecodeCounter;
	int		mPFDMALastCheckX;
	bool	mbPFDMAEnabled;
	bool	mbPFDMAActive;
	bool	mbWSYNCActive;
	bool	mbWSYNCRelease;
	bool	mbHScrollEnabled;
	bool	mbHScrollDelay;
	bool	mbRowStopUseVScroll;
	bool	mbRowAdvance;
	bool	mbLateNMI;
	bool	mbInBuggedVBlank;
	uint8	mEarlyNMIEN;
	uint8	mEarlyNMIEN2;
	uint32	mRowCounter;
	uint32	mRowCount;
	uint32	mLatchedVScroll;		// latched VSCROL at cycle 109 from previous scanline -- used to detect end of vs region

	uint16	mPFDMAPtr;
	uint16	mPFRowDMAPtrBase;
	uint16	mPFRowDMAPtrOffset;
	uint32	mPFPushCycleMask;

	uint32	mPFCharFetchPtr;
	uint8	mPFCharMask;
	uint8	*mpPFDataWrite;
	uint8	*mpPFDataRead;

	int		mPFWidthShift;
	int		mPFHScrollDMAOffset;
	enum PFPushMode {
		kBlank,
		k160,
		k160Alt,
		k320
	} mPFPushMode;

	bool	mPFHiresMode;

	enum PFWidthMode {
		kPFDisabled,
		kPFNarrow,
		kPFNormal,
		kPFWide
	};
	
	PFWidthMode	mPFWidth;
	PFWidthMode	mPFFetchWidth;
	PFWidthMode	mPFFetchWidthLatchedStart;
	PFWidthMode	mPFFetchWidthLatchedEnd;

	uint8	mPFCharSave;

	uint32	mPFDisplayStart;
	uint32	mPFDisplayEnd;
	uint32	mPFDMAStart;
	uint32	mPFDMAEnd;
	uint32	mPFDMALatchedStart;
	uint32	mPFDMALatchedEnd;
	uint32	mPFDMAPatternCacheKey;

	AnalysisMode	mAnalysisMode;

	uint8	mDMACTL;	// bit 5 = enable display list DMA
						// bit 4 = single line player resolution
						// bit 3 = enable player DMA
						// bit 2 = enable missile DMA
						// bit 1,0 = playfield width (00 = disabled, 01 = narrow, 10 = normal, 11 = wide)

	uint8	mCHACTL;	//

	uint16	mDLIST;		// display list pointer
	uint16	mDLISTLatch;// latched display list pointer
	uint8	mDLControlPrev;
	uint8	mDLControl;
	uint8	mDLNext;

	uint8	mHSCROL;	// horizontal scroll enable

	uint8	mVSCROL;	// vertical scroll enable

	uint8	mPMBASE;	// player missile base MSB

	uint8	mCHBASE;	// character base address
	uint32	mCharBaseAddr128;
	uint32	mCharBaseAddr64;
	uint8	mCharInvert;
	uint8	mCharBlink;

	uint8	mNMIEN;		// bit 7 = DLI enabled
						// bit 6 = VBI enabled

	uint8	mNMIST;		// bit 7 = DLI pending
						// bit 6 = VBI pending
						// bit 5 = RESET key pending

	uint8	mVCOUNT;
	int mWSYNCPending;

	uint32	mGTIAHSyncOffset;
	uint32	mVSyncShiftTime;

	ATGTIAEmulator *mpGTIA;
	ATAnticEmulatorConnections *mpConn;
	ATScheduler *mpScheduler;

	struct QueuedRegisterUpdate {
		uint32 mTime;
		uint8 mReg;
		uint8 mValue;
	};

	typedef vdfastvector<QueuedRegisterUpdate> RegisterUpdates;
	RegisterUpdates mRegisterUpdates;
	uint32	mRegisterUpdateHeadIdx;

	ATEvent *mpRegisterUpdateEvent;

	bool	mbDMAPattern[114];
	uint8	mDMAPFFetchPattern[114];

	uint8	mPFDataBuffer[114];
	uint8	mPFCharBuffer[114];

	uint8	mPFDecodeBuffer[228];

	DLHistoryEntry	mDLHistory[312];
	uint8	mActivityMap[312][114];
};

VDFORCEINLINE bool ATAnticEmulator::Advance() {
	bool busActive = false;

	if (mWSYNCPending && !--mWSYNCPending) {
		// The 6502 doesn't respond to RDY for write cycles, so if the next CPU cycle is a write,
		// we cannot pull RDY yet.
		if (mpConn->AnticIsNextCPUCycleWrite())
			++mWSYNCPending;
		else
			mbWSYNCActive = true;
	}

	if (++mX >= 114)
		AdvanceScanline();

	busActive = mbDMAPattern[mX];
	uint8 fetchMode = mDMAPFFetchPattern[mX];

	if (fetchMode & 0x80) {
		if (AdvanceSpecial())
			busActive = true;

		fetchMode = mDMAPFFetchPattern[mX];
	}

	int xoff = mX;
	switch(fetchMode & 0x7f) {
	case 1:
		mpPFDataWrite[xoff] = mpConn->AnticReadByteFast(mPFRowDMAPtrBase + ((mPFRowDMAPtrOffset++) & 0x0fff));
		break;
	case 2:
		{
			uint8 c = mpPFDataRead[xoff];
			mPFCharBuffer[xoff - mPFDMAStart] = mpConn->AnticReadByteFast(mPFCharFetchPtr + ((uint32)(c & mPFCharMask) << 3));
		}
		break;
	}

	busActive |= mbWSYNCActive;

	if (busActive)
		++mHaltedCycles;

	return busActive;
}

#endif
