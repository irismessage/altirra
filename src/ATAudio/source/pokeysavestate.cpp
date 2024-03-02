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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#include "stdafx.h"
#include <at/atcore/savestate.h>
#include <at/ataudio/pokeysavestate.h>

ATSERIALIZATION_DEFINE(ATSaveStatePokey);
ATSERIALIZATION_DEFINE(ATSaveStatePokeyInternal);

template<typename T>
void ATSaveStatePokeyInternal::Exchange(T& rw) {
	rw.Transfer("clock15_offset", &mClock15Offset);
	rw.Transfer("clock64_offset", &mClock64Offset);
	rw.Transfer("poly9_offset", &mPoly9Offset);
	rw.Transfer("poly17_offset", &mPoly17Offset);
	rw.Transfer("polyoff_offset", &mPolyShutOffTime);
	rw.TransferArray("timer_counters", mTimerCounters);
	rw.TransferArray("timer_borrow_counters", mTimerBorrowCounters);
	rw.TransferArray("two_tone_reset_counters", mTwoToneResetCounters);
	rw.Transfer("serin_counter", &mSerInCounter);
	rw.Transfer("serin_shift_register", &mSerInShiftRegister);
	rw.Transfer("serin_deferred_load", &mbSerInDeferredLoad);
	rw.Transfer("serin_waiting_for_start_bit", &mbSerInWaitingForStartBit);

	rw.Transfer("serout_event_time", &mSerOutEventTime);
	rw.Transfer("serout_counter", &mSerOutCounter);
	rw.Transfer("serout_shift_register", &mSerOutShiftRegister);
	rw.Transfer("serout_valid", &mbSerOutValid);
	rw.Transfer("serout_shift_valid", &mbSerOutShiftValid);

	rw.Transfer("trace_byte_index", &mTraceByteIndex);

	rw.Transfer("renderer_poly4_offset", &mRendererState.mPoly4Offset);
	rw.Transfer("renderer_poly5_offset", &mRendererState.mPoly5Offset);
	rw.Transfer("renderer_poly9_offset", &mRendererState.mPoly9Offset);
	rw.Transfer("renderer_poly17_offset", &mRendererState.mPoly17Offset);
	rw.Transfer("output_flip_flops", &mRendererState.mOutputFlipFlops);

	if constexpr (rw.IsReader) {
		if (mClock15Offset >= 114)
			throw ATInvalidSaveStateException();

		if (mClock64Offset >= 28)
			throw ATInvalidSaveStateException();

		if (mPoly9Offset >= 511)
			throw ATInvalidSaveStateException();

		if (mPoly17Offset >= 131071)
			throw ATInvalidSaveStateException();

		if (mRendererState.mPoly4Offset >= 15)
			throw ATInvalidSaveStateException();

		if (mRendererState.mPoly5Offset >= 31)
			throw ATInvalidSaveStateException();

		if (mRendererState.mPoly9Offset >= 511)
			throw ATInvalidSaveStateException();

		if (mRendererState.mPoly17Offset >= 131071)
			throw ATInvalidSaveStateException();

		for(const auto& c : mTimerCounters) {
			if (!c || c > 256)
				throw ATInvalidSaveStateException();
		}

		for(const auto& c : mTimerBorrowCounters) {
			if (c > 3)
				throw ATInvalidSaveStateException();
		}

		for(const auto& c : mTwoToneResetCounters) {
			if (c > 2)
				throw ATInvalidSaveStateException();
		}

		if (mSerInCounter > 19)
			throw ATInvalidSaveStateException();

		if (mSerOutEventTime > 10000000)
			throw ATInvalidSaveStateException();

		if (mSerOutCounter > 20)
			throw ATInvalidSaveStateException();
	}
}

template<typename T>
void ATSaveStatePokey::Exchange(T& rw) {
	rw.TransferArray("audf", mAUDF);
	rw.TransferArray("audc", mAUDC);
	rw.Transfer("audctl", &mAUDCTL);
	rw.Transfer("irqen", &mIRQEN);
	rw.Transfer("irqst", &mIRQST);
	rw.Transfer("skctl", &mSKCTL);
	rw.Transfer("allpot", &mALLPOT);
	rw.Transfer("kbcode", &mKBCODE);
	rw.Transfer("internal_state", &mpInternalState);
	rw.Transfer("stereo_pair", &mpStereoPair);
}

template void ATSaveStatePokeyInternal::Exchange(ATSerializer&);
template void ATSaveStatePokeyInternal::Exchange(ATDeserializer&);
template void ATSaveStatePokey::Exchange(ATSerializer&);
template void ATSaveStatePokey::Exchange(ATDeserializer&);
