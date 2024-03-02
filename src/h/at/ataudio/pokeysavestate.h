//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef f_AT_POKEYSAVESTATE_H
#define f_AT_POKEYSAVESTATE_H

#include <at/atcore/snapshotimpl.h>

struct ATSaveStatePokeyRenderer {
	uint8 mPoly4Offset = 0;
	uint8 mPoly5Offset = 0;
	uint16 mPoly9Offset = 0;
	uint32 mPoly17Offset = 0;
	uint8 mOutputFlipFlops = 0x3F;
};

class ATSaveStatePokeyInternal : public ATSnapExchangeObject<ATSaveStatePokeyInternal> {
public:
	template<typename T>
	void Exchange(T& rw);

	uint32 mClock15Offset = 0;
	uint32 mClock64Offset = 0;
	uint32 mPoly9Offset = 0;
	uint32 mPoly17Offset = 0;
	uint32 mPolyShutOffTime = 0;
	uint32 mTimerCounters[4] {};
	uint32 mTimerBorrowCounters[4] {};
	uint32 mTwoToneResetCounters[2] {};
	uint8 mSerInCounter = 0;
	uint8 mSerInShiftRegister = 0;
	bool mbSerInDeferredLoad = false;
	bool mbSerInWaitingForStartBit = false;

	uint32 mSerOutEventTime = 0;
	uint8 mSerOutCounter = 0;
	uint8 mSerOutShiftRegister = 0;
	bool mbSerOutValid = false;
	bool mbSerOutShiftValid = false;

	uint32 mTraceByteIndex = 0;

	ATSaveStatePokeyRenderer mRendererState;
};

class ATSaveStatePokey : public ATSnapExchangeObject<ATSaveStatePokey> {
public:
	template<typename T>
	void Exchange(T& rw);

	uint8 mAUDF[4] {};
	uint8 mAUDC[4] {};
	uint8 mAUDCTL = 0;
	uint8 mIRQEN = 0;
	uint8 mIRQST = 0xFF;
	uint8 mSKCTL = 0;
	uint8 mSKSTAT = 0;
	uint8 mALLPOT = 0;
	uint8 mKBCODE = 0;
	vdrefptr<ATSaveStatePokeyInternal> mpInternalState;
	vdrefptr<IATObjectState> mpStereoPair;
};

#endif
