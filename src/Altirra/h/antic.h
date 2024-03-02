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

#include <vd2/system/vdtypes.h>
#include "scheduler.h"

class ATGTIAEmulator;
class ATSaveStateReader;
class ATSaveStateWriter;
class ATScheduler;

class ATAnticEmulatorConnections {
public:
	VDFORCEINLINE uint8 AnticReadByteFast(uint32 address) {
		uintptr readPage = mpAnticReadPageMap[address >> 8];
		return *mpAnticBusData = (!(readPage & 1) ? *(const uint8 *)(readPage + address) : AnticReadByte(address));
	}

	virtual uint8 AnticReadByte(uint32 address) = 0;
	virtual void AnticAssertNMI() = 0;
	virtual void AnticEndFrame() = 0;
	virtual void AnticEndScanline() = 0;
	virtual bool AnticIsNextCPUCycleWrite() = 0;
	virtual uint8 AnticGetCPUHeldCycleValue() = 0;

	uint8 *mpAnticBusData;

protected:
	const uintptr *mpAnticReadPageMap;
};

struct ATAnticRegisterState {
	uint8	mDMACTL;
	uint8	mCHACTL;
	uint8	mDLISTL;
	uint8	mDLISTH;
	uint8	mHSCROL;
	uint8	mVSCROL;
	uint8	mPMBASE;
	uint8	mCHBASE;
	uint8	mNMIEN;
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
		uint8	mCHBASE : 7;
		uint8	mbValid : 1;
	};

	const DLHistoryEntry *GetDLHistory() const { return mDLHistory; }
	const uint8 *GetActivityMap() const { return mActivityMap[0]; }

	void Init(ATAnticEmulatorConnections *conn, ATGTIAEmulator *gtia, ATScheduler *sch);
	void ColdReset();
	void WarmReset();
	void RequestNMI();

	void SetLightPenPosition(bool phase);
	void SetLightPenPosition(int x, int y);

	VDFORCEINLINE uint8 Advance();
	void SyncWithGTIA(int offset);
	void Decode(int offset);

	uint8 ReadByte(uint8 reg) const;
	void WriteByte(uint8 reg, uint8 value);

	void DumpStatus();
	void DumpDMAPattern();
	void DumpDMAActivityMap();

	void	BeginLoadState(ATSaveStateReader& reader);
	void	LoadStateArch(ATSaveStateReader& reader);
	void	LoadStatePrivate(ATSaveStateReader& reader);
	void	EndLoadState(ATSaveStateReader& reader);

	void	BeginSaveState(ATSaveStateWriter& writer);
	void	SaveStateArch(ATSaveStateWriter& writer);
	void	SaveStatePrivate(ATSaveStateWriter& writer);

	void	GetRegisterState(ATAnticRegisterState& state) const;

protected:
	template<class T>
	void	ExchangeState(T& io);

	uint8	AdvanceSpecial();
	void	AdvanceScanline();
	void	UpdateDMAPattern();
	void	LatchPlayfieldEdges();
	void	UpdateCurrentCharRow();
	void	UpdatePlayfieldTiming();
	void	UpdatePlayfieldDataPointers();

	void	OnScheduledEvent(uint32 id);
	void	QueueRegisterUpdate(uint32 delay, uint8 reg, uint8 value);
	void	ExecuteQueuedUpdates();

	// critical fields written in Advance()
	uint32	mHaltedCycles;
	uint32	mX;
	uint16	mPFRowDMAPtrOffset;

	// critical fields read in Advance()
	ATAnticEmulatorConnections *mpConn;
	uint8	*mpPFDataWrite;
	uint8	*mpPFDataRead;
	uint32	mPFCharFetchPtr;
	uint8	mPFCharMask;
	bool	mbWSYNCActive;
	uint16	mPFRowDMAPtrBase;

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
	bool	mbPFRendered;			// true if any pixels have been rendered this scanline by the playfield
	bool	mbWSYNCRelease;
	uint8	mWSYNCHoldValue;
	bool	mbHScrollEnabled;
	bool	mbHScrollDelay;
	bool	mbRowStopUseVScroll;
	bool	mbRowAdvance;
	bool	mbLateNMI;
	bool	mbInBuggedVBlank;
	bool	mbPhantomPMDMA;
	bool	mbPhantomPMDMAActive;
	uint8	mPendingNMIs;
	uint8	mEarlyNMIEN;
	uint8	mEarlyNMIEN2;
	uint32	mRowCounter;
	uint32	mRowCount;
	uint8	mLatchedVScroll;		// latched VSCROL at cycle 109 from previous scanline -- used to detect end of vs region
	uint8	mLatchedVScroll2;		// latched VSCROL at cycle 6 from current scanline -- used to control DLI

	uint16	mPFDMAPtr;
	uint32	mPFPushCycleMask;

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

	uint32	mPFDisplayStart;
	uint32	mPFDisplayEnd;
	uint32	mPFDMAStart;
	uint8	*mpPFCharFetchPtr;
	uint32	mPFDMAVEnd;
	uint32	mPFDMAVEndWide;
	uint32	mPFDMAEnd;
	uint32	mPFDMALatchedStart;
	uint32	mPFDMALatchedVEnd;
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

	uint8	mPENH;
	uint8	mPENV;

	uint8	mVCOUNT;
	int mWSYNCPending;

	uint32	mGTIAHSyncOffset;
	uint32	mVSyncShiftTime;

	ATGTIAEmulator *mpGTIA;
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
	ATEvent *mpEventWSYNC;

	VDALIGN(16) uint8	mDMAPattern[115 + 13];

	VDALIGN(16) uint8	mPFDataBuffer[114 + 14];
	VDALIGN(16) uint8	mPFCharBuffer[114 + 14];

	VDALIGN(16) uint8	mPFDecodeBuffer[228 + 12];

	DLHistoryEntry	mDLHistory[312];
	uint8	mActivityMap[312][114];
};

VDFORCEINLINE uint8 ATAnticEmulator::Advance() {
	uint8 fetchMode = mDMAPattern[++mX];

	if (fetchMode & 0x80)
		fetchMode = AdvanceSpecial();

	int xoff = mX;

	if (fetchMode) {
		switch(fetchMode) {
		case 1:
			break;
		case 3:
			mpPFDataWrite[xoff >> 1] = mpConn->AnticReadByteFast(mPFRowDMAPtrBase + ((mPFRowDMAPtrOffset++) & 0x0fff));
			break;
		case 5:
			mpPFDataWrite[xoff >> 2] = mpConn->AnticReadByteFast(mPFRowDMAPtrBase + ((mPFRowDMAPtrOffset++) & 0x0fff));
			break;
		case 7:
			mpPFDataWrite[xoff >> 3] = mpConn->AnticReadByteFast(mPFRowDMAPtrBase + ((mPFRowDMAPtrOffset++) & 0x0fff));
			break;
		case 9:
			{
				uint8 c = mpPFDataRead[xoff >> 1];
				mpPFCharFetchPtr[xoff >> 1] = mpConn->AnticReadByteFast(mPFCharFetchPtr + ((uint32)(c & mPFCharMask) << 3));
			}
			break;
		case 11:
			{
				uint8 c = mpPFDataRead[xoff >> 2];
				mpPFCharFetchPtr[xoff >> 2] = mpConn->AnticReadByteFast(mPFCharFetchPtr + ((uint32)(c & mPFCharMask) << 3));
			}
			break;

		case 18:
			mpPFDataWrite[(xoff - 1) >> 1] = *mpConn->mpAnticBusData;
			++mPFRowDMAPtrOffset;
			break;
		case 20:
			mpPFDataWrite[(xoff - 1) >> 2] = *mpConn->mpAnticBusData;
			++mPFRowDMAPtrOffset;
			break;
		case 22:
			mpPFDataWrite[(xoff - 1) >> 3] = *mpConn->mpAnticBusData;
			++mPFRowDMAPtrOffset;
			break;

		case 24:
			mpPFCharFetchPtr[(xoff - 1) >> 1] = *mpConn->mpAnticBusData;
			break;
		case 26:
			mpPFCharFetchPtr[(xoff - 1) >> 2] = *mpConn->mpAnticBusData;
			break;

		default:
			VDNEVERHERE;
		}
	}

	uint8 busActive = fetchMode & 1;
	busActive |= (uint8)mbWSYNCActive;

	mHaltedCycles += busActive;

	return busActive;
}

#endif
