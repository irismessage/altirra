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
#include "pokey.h"
#include "pokeyrenderer.h"
#include "pokeytables.h"
#include <float.h>
#include <vd2/system/math.h>
#include <vd2/system/binary.h>

#include <math.h>

#include "console.h"
#include "cpu.h"
#include "savestate.h"
#include "audiooutput.h"
#include "soundboard.h"

namespace {
	enum {
		kATPokeyEvent15KHzTick = 2,
		kATPokeyEventTimer1Borrow = 7,
		kATPokeyEventTimer2Borrow = 8,
		kATPokeyEventTimer3Borrow = 9,
		kATPokeyEventTimer4Borrow = 10,
		kATPokeyEventResetTimers = 11,
		kATPokeyEventSerialOutput = 12,
		kATPokeyEventPot0ScanComplete = 16	// x8
	};

	const bool kForceActiveTimers = false;
}

ATPokeyEmulator::ATPokeyEmulator(bool isSlave)
	: mpRenderer(new ATPokeyRenderer)
	, mbCommandLineState(false)
	, mb5200Mode(false)
	, mbTraceSIO(false)
	, mbNonlinearMixingEnabled(true)
	, mKBCODE(0)
	, mKeyCodeTimer(0)
	, mKeyCooldownTimer(0)
	, mbKeyboardIRQPending(false)
	, mbUseKeyCooldownTimer(true)
	, mbShiftKeyState(false)
	, mbControlKeyState(false)
	, mbBreakKeyState(false)
	, mIRQEN(0)
	, mIRQST(0)
	, mAUDCTL(0)
	, mSERIN(0)
	, mSEROUT(0)
	, mSKSTAT(0)
	, mSKCTL(0)
	, mLastPolyTime(0)
	, mPoly17Counter(0)
	, mPoly9Counter(0)
	, mSerialInputShiftRegister(0)
	, mSerialOutputShiftRegister(0)
	, mSerialInputCounter(0)
	, mSerialOutputCounter(0)
	, mbSerOutValid(false)
	, mbSerialOutputState(false)
	, mbSerialRateChanged(false)
	, mbSerialWaitingForStartBit(true)
	, mbSerInBurstPending(false)
	, mSerBurstMode(kSerialBurstMode_Disabled)
	, mbSpeakerActive(false)
	, mpAudioLog(NULL)
	, mbFastTimer1(false)
	, mbFastTimer3(false)
	, mbLinkedTimers12(false)
	, mbLinkedTimers34(false)
	, mbUse15KHzClock(false)
	, mLast15KHzTime(0)
	, mLast64KHzTime(0)
	, mALLPOT(0)
	, mPotScanStartTime(0)
	, mp15KHzEvent(NULL)
	, mpStartBitEvent(NULL)
	, mpResetTimersEvent(NULL)
	, mpEventSerialOutput(NULL)
	, mpScheduler(NULL)
	, mpConn(NULL)
	, mpSlave(NULL)
	, mbIsSlave(isSlave)
	, mpAudioOut(NULL)
	, mpCassette(NULL)
	, mpSoundBoard(NULL)
{
	for(int i=0; i<8; ++i) {
		mpPotScanEvent[i] = NULL;
		mPOT[i] = 228;
	}

	memset(mpTimerBorrowEvents, 0, sizeof(mpTimerBorrowEvents));
}

ATPokeyEmulator::~ATPokeyEmulator() {
	delete mpRenderer;
}

void ATPokeyEmulator::Init(IATPokeyEmulatorConnections *mem, ATScheduler *sched, IATAudioOutput *output, ATPokeyTables *tables) {
	mpConn = mem;
	mpScheduler = sched;
	mpAudioOut = output;
	mpTables = tables;
	UpdateMixTable();

	mpRenderer->Init(sched, tables);

	VDASSERT(!mp15KHzEvent);

	ColdReset();
}

void ATPokeyEmulator::ColdReset() {
	memset(mbKeyMatrix, 0, sizeof mbKeyMatrix);
	mKeyScanCode = 0;
	mKeyScanState = 0;
	mbKeyboardIRQPending = false;

	memset(&mState, 0, sizeof mState);

	mKBCODE = 0;
	mSKSTAT = mbShiftKeyState ? 0xF7 : 0xFF;
	mSKCTL = 0;
	mKeyCodeTimer = 0;
	mKeyCooldownTimer = 0;
	mIRQEN = 0;
	mIRQST = 0xF7;

	mAUDCTL = 0;
	mbFastTimer1 = false;
	mbFastTimer3 = false;
	mbLinkedTimers12 = false;
	mbLinkedTimers34 = false;
	mbUse15KHzClock = false;

	for(int i=0; i<4; ++i) {
		mCounter[i] = 1;
		mCounterBorrow[i] = 0;
		mAUDFP1[i] = 1;
		mbDeferredTimerEvents[i] = false;

		mpScheduler->UnsetEvent(mpTimerBorrowEvents[i]);
	}

	RecomputeTimerPeriod<0>();
	RecomputeTimerPeriod<1>();
	RecomputeTimerPeriod<2>();
	RecomputeTimerPeriod<3>();
	RecomputeAllowedDeferredTimers();

	mpScheduler->UnsetEvent(mpStartBitEvent);
	mpScheduler->UnsetEvent(mpResetTimersEvent);
	mpScheduler->UnsetEvent(mpEventSerialOutput);

	mpRenderer->ColdReset();

	mLastPolyTime = ATSCHEDULER_GETTIME(mpScheduler);
	mPoly17Counter = 0;
	mPoly9Counter = 0;
	mSerialInputShiftRegister = 0;
	mSerialOutputShiftRegister = 0;
	mSerialOutputCounter = 0;
	mSerialInputCounter = 0;
	mbSerOutValid = false;
	mbSerShiftValid = false;
	mbSerialOutputState = false;
	mbSerialWaitingForStartBit = true;
	mbSerInBurstPending = false;
	mbSerialSimulateInputPort = false;

	memset(mAUDF, 0, sizeof mAUDF);
	memset(mAUDC, 0, sizeof mAUDC);

	mLast15KHzTime = ATSCHEDULER_GETTIME(mpScheduler);
	mLast64KHzTime = ATSCHEDULER_GETTIME(mpScheduler);

	if (mp15KHzEvent) {
		mpScheduler->RemoveEvent(mp15KHzEvent);
		mp15KHzEvent = NULL;
	}

	for(int i=0; i<8; ++i) {
		if (mpPotScanEvent[i]) {
			mpScheduler->RemoveEvent(mpPotScanEvent[i]);
			mpPotScanEvent[i] = NULL;
		}
	}

	mALLPOT = 0;

	mbCommandLineState = false;

	if (mpSlave)
		mpSlave->ColdReset();
}

void ATPokeyEmulator::SetSlave(ATPokeyEmulator *slave) {
	ColdReset();
	if (mpSlave)
		mpSlave->ColdReset();

	mpSlave = slave;

	if (mpSlave)
		mpSlave->ColdReset();

	UpdateMixTable();
}

void ATPokeyEmulator::SetCassette(IATPokeyCassetteDevice *dev) {
	mpCassette = dev;
	mbSerialRateChanged = true;
}

void ATPokeyEmulator::SetSoundBoard(IATSoundBoardEmulator *sbe) {
	mpSoundBoard = sbe;
}

void ATPokeyEmulator::SetAudioLog(ATPokeyAudioLog *log) {
	mpAudioLog = log;
}

void ATPokeyEmulator::Set5200Mode(bool enable) {
	if (mb5200Mode == enable)
		return;

	mb5200Mode = enable;
}

void ATPokeyEmulator::SetSerialBurstMode(SerialBurstMode mode) {
	mSerBurstMode = mode;
	mbSerInBurstPending = false;
}

void ATPokeyEmulator::AddSIODevice(IATPokeySIODevice *device) {
	mDevices.push_back(device);
	device->PokeyAttachDevice(this);
}

void ATPokeyEmulator::RemoveSIODevice(IATPokeySIODevice *device) {
	Devices::iterator it(std::find(mDevices.begin(), mDevices.end(), device));

	if (it != mDevices.end())
		mDevices.erase(it);
}

void ATPokeyEmulator::ReceiveSIOByte(uint8 c, uint32 cyclesPerBit, bool simulateInputPort) {
	if (mbTraceSIO)
		ATConsoleTaggedPrintf("POKEY: Receiving byte (c=%02x; %02x %02x)\n", c, mSERIN, mSerialInputShiftRegister);

	mbSerialSimulateInputPort = simulateInputPort;

	if (simulateInputPort) {
		mSerialSimulateInputBaseTime = ATSCHEDULER_GETTIME(mpScheduler);
		mSerialSimulateInputCyclesPerBit = cyclesPerBit;
		mSerialSimulateInputData = ((uint32)c << 1) + 0x200;
	}

	// check for overrun
	if (!(mIRQST & 0x20)) {
		mSKSTAT &= 0xdf;

		if (mbTraceSIO)
			ATConsoleTaggedPrintf("POKEY: Serial input overrun detected (c=%02x; %02x %02x)\n", c, mSERIN, mSerialInputShiftRegister);

		mSERIN = mSerialInputShiftRegister;
	}

	// check for attempted read in synchronous mode
	if (!(mSKCTL & 0x10)) {
		// blown read -- trash the byte by faking a dropped bit
		c = (c & 0x0f) + ((c & 0xe0) >> 1) + 0x80;

		// set the framing error bit
		mSKSTAT &= 0x7F;

		if (mbTraceSIO)
			ATConsoleTaggedPrintf("POKEY: Trashing byte $%02x and signaling framing error due to asynchronous input mode not being enabled.\n", c);
	}

	// check for mismatched baud rate
	if (cyclesPerBit) {
		uint32 expectedCPB = GetSerialCyclesPerBit();
		uint32 margin = (expectedCPB + 7) >> 3;

		if (cyclesPerBit < expectedCPB - margin || cyclesPerBit > expectedCPB + margin) {
			// blown read -- trash the byte and assert the framing error bit
			c = 0xFF;
			mSKSTAT &= 0x7F;

			if (mbTraceSIO)
				ATConsoleTaggedPrintf("POKEY: Signaling framing error due to receive rate mismatch (expected %d cycles/bit, got %d)\n", expectedCPB, cyclesPerBit);
		}
	}

	if (mSKCTL & 0x10) {
		// Restart timers 3 and 4 immediately.
		UpdateTimerCounter<2>();
		UpdateTimerCounter<3>();

		mCounter[2] = mAUDFP1[2];
		mCounter[3] = mAUDFP1[3];

		mbSerialWaitingForStartBit = false;
		SetupTimers(0x0c);

		if (mpStartBitEvent) {
			mpScheduler->RemoveEvent(mpStartBitEvent);
			mpStartBitEvent = NULL;
		}
	}

	mSerialInputShiftRegister = c;
	mSerialInputCounter = 19;
	mSERIN = c;

	if (mbTraceSIO)
		ATConsoleTaggedPrintf("POKEY: Reasserting serial input IRQ. IRQEN=%02x, IRQST=%02x\n", mIRQEN, mIRQST);

	if (mIRQEN & 0x20) {
		mIRQST &= ~0x20;
		mpConn->PokeyAssertIRQ(false);
	}
}

void ATPokeyEmulator::SetAudioLine2(int v) {
	mpRenderer->SetAudioLine2(v);
}

void ATPokeyEmulator::SetDataLine(bool newState) {
	if (newState)
		mSKSTAT |= 0x10;
	else
		mSKSTAT &= ~0x10;
}

void ATPokeyEmulator::SetCommandLine(bool newState) {
	if (newState == mbCommandLineState)
		return;

	if (mbTraceSIO)
		ATConsoleTaggedPrintf("POKEY: %s command line.\n", newState ? "asserting" : "negating");

	mbCommandLineState = newState;
	if (newState) {
		for(Devices::const_iterator it(mDevices.begin()), itEnd(mDevices.end()); it!=itEnd; ++it)
			(*it)->PokeyBeginCommand();
	} else {
		for(Devices::const_iterator it(mDevices.begin()), itEnd(mDevices.end()); it!=itEnd; ++it)
			(*it)->PokeyEndCommand();
	}
}

void ATPokeyEmulator::SetSpeaker(bool newState) {
	if (mpRenderer->SetSpeaker(newState))
		mbSpeakerActive = true;
}

bool ATPokeyEmulator::IsChannelEnabled(uint32 channel) const {
	return mpRenderer->IsChannelEnabled(channel);
}

void ATPokeyEmulator::SetChannelEnabled(uint32 channel, bool enabled) {
	mpRenderer->SetChannelEnabled(channel, enabled);
}

void ATPokeyEmulator::SetNonlinearMixingEnabled(bool enable) {
	if (mbNonlinearMixingEnabled != enable) {
		mbNonlinearMixingEnabled = enable;

		UpdateMixTable();
	}
}

void ATPokeyEmulator::SetShiftKeyState(bool newState) {
	mbShiftKeyState = newState;

	if (newState)
		mSKSTAT &= ~0x08;
	else
		mSKSTAT |= 0x08;
}

void ATPokeyEmulator::SetControlKeyState(bool newState) {
	mbControlKeyState = newState;
}

void ATPokeyEmulator::PushKey(uint8 c, bool repeat, bool allowQueue, bool flushQueue, bool useCooldown) {
	if (mb5200Mode)
		return;

	mbUseKeyCooldownTimer = useCooldown;

	if (allowQueue) {
		if (!mKeyQueue.empty() || mKeyCodeTimer || mKeyCooldownTimer || !(mIRQST & 0x40) || !mpConn->PokeyIsKeyPushOK(c)) {
			mKeyQueue.push_back(c);
			return;
		}
	} else if (flushQueue) {
		mKeyQueue.clear();
	}

	mKBCODE = c;
	mSKSTAT &= ~0x04;

	if (!mKeyCodeTimer || !repeat)
		mbKeyboardIRQPending = true;

	mKeyCodeTimer = 1;
	mKeyCooldownTimer = 0;
}

void ATPokeyEmulator::PushRawKey(uint8 c) {
	if (mb5200Mode)
		return;

	mKeyQueue.clear();

	mKBCODE = c;
	mSKSTAT &= ~0x04;

	if ((mIRQEN & 0x40) && (mSKCTL & 2)) {
		// If keyboard IRQ is already active, set the keyboard overrun bit.
		if (!(mIRQST & 0x40))
			mSKSTAT &= ~0x40;

		mIRQST &= ~0x40;
		mpConn->PokeyAssertIRQ(false);
	}
}

void ATPokeyEmulator::ReleaseRawKey() {
	mSKSTAT |= 0x04;
}

void ATPokeyEmulator::SetBreakKeyState(bool state) {
	mbBreakKeyState = state;

	if (state) {
		if (mIRQEN & mIRQST & 0x80) {
			mIRQST &= ~0x80;
			mpConn->PokeyAssertIRQ(false);
		}
	} else {
		if (!(mIRQST & 0x80)) {
			mIRQST |= 0x80;

			if (!(mIRQEN & ~mIRQST))
				mpConn->PokeyNegateIRQ(false);
		}
	}
}

void ATPokeyEmulator::PushBreak() {
	mKeyQueue.clear();

	if (mIRQEN & 0x80) {
		mIRQST &= ~0x80;
		mpConn->PokeyAssertIRQ(false);
	}
}

void ATPokeyEmulator::UpdateKeyMatrix(int index, bool depressed) {
	mbKeyMatrix[index] = depressed;
}

void ATPokeyEmulator::SetKeyMatrix(const bool matrix[64]) {
	for(int i=0; i<64; ++i)
		mbKeyMatrix[i] = matrix[i];
}

void ATPokeyEmulator::ClearKeyMatrix() {
	memset(mbKeyMatrix, 0, sizeof mbKeyMatrix);
}

template<uint8 activeChannel>
void ATPokeyEmulator::FireTimer() {
	mpRenderer->AddChannelEvent(activeChannel);

	if (activeChannel == 0) {
		if (mIRQEN & 0x01) {
			mIRQST &= ~0x01;
			mpConn->PokeyAssertIRQ(false);
		}

		// two tone
		if ((mSKCTL & 8) && mbSerialOutputState && !(mSKCTL & 0x80)) {
			// resync timer 2 to timer 1
			mCounter[1] = mAUDFP1[1];
			mCounterBorrow[1] = 0;
			SetupTimers(0x02);
		}
	}

	// count timer 2
	if (activeChannel == 1) {
		if (mIRQEN & 0x02) {
			mIRQST &= ~0x02;
			mpConn->PokeyAssertIRQ(false);
		}

		// two tone
		if (mSKCTL & 8) {
			// resync timer 1 to timer 2
			mCounter[0] = mAUDFP1[0];
			mCounterBorrow[0] = 0;
			SetupTimers(0x01);
		}

		if (mSerialOutputCounter) {
			if ((mSKCTL & 0x60) == 0x60)
				mpScheduler->SetEvent(2, this, kATPokeyEventSerialOutput, mpEventSerialOutput);
		}
	}

	// count timer 4
	if (activeChannel == 3) {
		if (mIRQEN & 0x04) {
			mIRQST &= ~0x04;
			mpConn->PokeyAssertIRQ(false);
		}

		if (mSerialInputCounter) {
			--mSerialInputCounter;

			if (!mSerialInputCounter && (mSKCTL & 0x10)) {
				mbSerialWaitingForStartBit = true;

				if (!mbLinkedTimers34) {
					mCounter[2] = mAUDFP1[2];
					SetupTimers(0x04);
				}
			}
		}

		if (mSerialOutputCounter) {
			switch(mSKCTL & 0x60) {
				case 0x20:
				case 0x40:
					mpScheduler->SetEvent(2, this, kATPokeyEventSerialOutput, mpEventSerialOutput);
					break;
			}
		}
	}
}

void ATPokeyEmulator::OnSerialOutputTick() {
	--mSerialOutputCounter;

	// We've already transmitted the start bit (low), so we need to do data bits and then
	// stop bit (high).
	mbSerialOutputState = mSerialOutputCounter ? (mSerialOutputShiftRegister & (1 << (9 - (mSerialOutputCounter >> 1)))) != 0 : true;

	if (!mSerialOutputCounter) {
		if (mbSerShiftValid) {
			mbSerShiftValid = false;

			uint32 cyclesPerBit;
		
			switch(mSKCTL & 0x60) {
				case 0x00:		// external clock
					cyclesPerBit = 0;
					break;

				case 0x20:		// timer 4 as transmit clock
				case 0x40:
					cyclesPerBit = mTimerPeriod[3];
					break;

				case 0x60:		// timer 2 as transmit clock
					cyclesPerBit = mTimerPeriod[1];
					break;

				default:
					VDNEVERHERE;
			}

			cyclesPerBit += cyclesPerBit;

			if (mbTraceSIO) {
				ATConsoleTaggedPrintf("POKEY: Transmitted serial byte %02x to SIO bus at %u cycles/bit (%.1f baud)\n"
					, mSerialOutputShiftRegister
					, cyclesPerBit
					, (7159090.0f / 4.0f) / (float)cyclesPerBit
					);
			}

			for(Devices::const_iterator it(mDevices.begin()), itEnd(mDevices.end()); it!=itEnd; ++it)
				(*it)->PokeyWriteSIO(mSerialOutputShiftRegister, mbCommandLineState, cyclesPerBit);
		}

		if (mbSerOutValid) {
			mSerialOutputCounter = 20;
			mbSerialOutputState = true;
			mSerialOutputShiftRegister = mSEROUT;
			mbSerOutValid = false;
			mbSerShiftValid = true;

			// bit 3 is special and doesn't get cleared by IRQEN
			mIRQST |= 0x08;

			if (mIRQEN & 0x10)
				mIRQST &= ~0x10;
		} else
			mIRQST &= ~0x08;

		if (mIRQEN & ~mIRQST)
			mpConn->PokeyAssertIRQ(false);
		else
			mpConn->PokeyNegateIRQ(false);
	}
}

uint32 ATPokeyEmulator::GetSerialCyclesPerBit() const {
	const uint32 divisor = mTimerPeriod[3];

	return divisor + divisor;
}

void ATPokeyEmulator::AdvanceScanLine() {
	if (mbSerialRateChanged) {
		mbSerialRateChanged = false;

		if (mpCassette) {
			uint32 divisor = GetSerialCyclesPerBit() >> 1;

			mpCassette->PokeyChangeSerialRate(divisor);
		}
	}

	if (mpAudioLog) {
		if (mpAudioLog->mRecordedCount < mpAudioLog->mMaxCount)
			GetAudioState(mpAudioLog->mpStates[mpAudioLog->mRecordedCount++]);
	}
}

void ATPokeyEmulator::AdvanceFrame(bool pushAudio) {
	if (mKeyCodeTimer) {
		if (!--mKeyCodeTimer) {
			mSKSTAT |= 0x04;

			mKeyCooldownTimer = 60;
		}
	} else if (mbSpeakerActive) {
		mKeyCooldownTimer = 60;
	} else {
		if (mKeyCooldownTimer)
			--mKeyCooldownTimer;

		if ((mIRQST & mIRQEN & 0x40) && !mKeyQueue.empty() && ((mbUseKeyCooldownTimer && !mKeyCooldownTimer) || mpConn->PokeyIsKeyPushOK(mKeyQueue.front())))
			TryPushNextKey();
	}

	mbSpeakerActive = false;

	FlushAudio(pushAudio);

	// Scan all of the deferred timers and push any that are too far behind. We need to do this to
	// prevent any of the timers from getting too far behind the clock (>30 bits). This calculation
	// is wrong for the low timer of a linked timer pair, but it turns out we don't use the start
	// value in that case; this module uses the high timer delay, and only the renderer does the
	// funky linked step.
	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	for(int i=0; i<4; ++i) {
		if (!mbDeferredTimerEvents[i])
			continue;

		// Determine what kind of lag we want to check for on the deferred timer start. This must be
		// at least 8893, which is the number of times that a 1.79MHz frequency 0 timer can count in
		// a PAL frame. However, a slow 15KHz linked timer can take as long as 7.4M clocks to cycle.
		// We keep shifting up the value until it's a bit more than a second. We do need to keep this
		// value a multiple of the original, though.
		uint32 bigPeriod = mDeferredTimerPeriods[i];

		while(bigPeriod < 2097152) {
			bigPeriod <<= 4;
		}

		// The deferred timers may start in the future for one that hasn't hit the first tick,
		// so we must check for that case.
		if ((sint32)(t - mDeferredTimerStarts[i]) <= (sint32)bigPeriod)
			continue;

		// Bump the timer up by a number of periods so it catches up. 
		mDeferredTimerStarts[i] += bigPeriod;
	}

	if (t - mLast64KHzTime > 28*65536)
		mLast64KHzTime += 28*65536;
}

void ATPokeyEmulator::OnScheduledEvent(uint32 id) {
	switch(id) {
		case kATPokeyEvent15KHzTick:
			{
				mp15KHzEvent = mpScheduler->AddEvent(114, this, kATPokeyEvent15KHzTick);

				uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
				mLast15KHzTime = t;

				if (mbKeyboardIRQPending) {
					mbKeyboardIRQPending = false;

					if ((mIRQEN & 0x40) && (mSKCTL & 2)) {
						// If keyboard IRQ is already active, set the keyboard overrun bit.
						if (!(mIRQST & 0x40))
							mSKSTAT &= ~0x40;

						mIRQST &= ~0x40;
						mpConn->PokeyAssertIRQ(false);
					}
				}

				if (mb5200Mode && (mSKCTL & 2)) {
					++mKeyScanCode;

					uint8 kc = mKeyScanCode & 0x3F;

					// POKEY's keyboard circuitry is a two-bit state machine with a keyboard line and
					// a comparator as input, and result/comparator latch signals as output. The state
					// machine works as follows:
					//
					//	state 0 (00): waiting for key
					//		key pressed -> state 1, load compare latch
					//
					//	state 1 (01): waiting for key bounce
					//		key pressed, not same as compare latch -> state 0
					//		key not pressed, same as compare latch -> state 0
					//		key pressed, same as compare latch -> state 2
					//
					//	state 2 (11): waiting for key release
					//		key not pressed, same as compare latch -> state 3
					//
					//	state 3 (10): waiting for key debounce
					//		key pressed, same as compare latch -> state 2
					//		key not pressed, same as compare latch -> state 0
					//
					// If keyboard debounce (SKCTL bit 1) is disabled, the compare signal is always
					// true. SKSTAT bit 2 reads 1 for states 0 and 1, and 0 for states 2 and 3.
					//
					// The state machine and the binary counter that run the external keyboard logic
					// both run at HBLANK (15KHz) rate; this logic therefore cycles through the entire
					// keyboard 245.3 times a second (NTSC), or 4.1 times a frame. It takes two HBLANKs
					// for a key to be recognized, and since keys are mirrored four times on the 5200
					// keypad, this causes IRQs to be sent 8 times a frame.

					switch(mKeyScanState) {
						case 0:		// waiting for key
							if (mbKeyMatrix[kc]) {
								mKeyScanLatch = kc;
								mKeyScanState = 1;
							}
							break;

						case 1:		// waiting for key bounce
							if (mbKeyMatrix[kc]) {
								if (kc == mKeyScanLatch || !(mSKCTL & 1)) {
									// same key down -- fire IRQ, save key, and continue
									mKeyScanState = 2;
									mKBCODE = kc | (mbControlKeyState ? 0x40 : 0x00) | (mbShiftKeyState ? 0x80 : 0x00);
									mSKSTAT &= ~0x04;
									if (mIRQEN & 0x40) {
										// If keyboard IRQ is already active, set the keyboard overrun bit.
										if (!(mIRQST & 0x40))
											mSKSTAT &= ~0x40;
										else {
											mIRQST &= ~0x40;
											mpConn->PokeyAssertIRQ(false);
										}
									}						
								} else {
									// different key down -- restart
									mKeyScanState = 0;
								}
							} else if (kc == mKeyScanLatch || !(mSKCTL & 1)) {
								// latched key no longer down -- restart
								mKeyScanState = 0;
							}
							break;

						case 2:		// waiting for key up
							if (kc == mKeyScanLatch || !(mSKCTL & 1)) {
								if (!mbKeyMatrix[kc]) {
									mKeyScanState = 3;
								}
							}
							break;

						case 3:		// waiting for debounce
							if (kc == mKeyScanLatch || !(mSKCTL & 1)) {
								if (mbKeyMatrix[kc])
									mKeyScanState = 2;
								else {
									mSKSTAT |= 0x04;
									mKeyScanState = 0;
								}
							}
							break;
					}
				}
			}
			break;

		case kATPokeyEventTimer1Borrow:
			mpTimerBorrowEvents[0] = NULL;
			FireTimer<0>();

			if (!mbLinkedTimers12) {
				mCounter[0] = mAUDFP1[0];
			} else {
				// If we are operating at 1.79MHz, three cycles have already elapsed from the underflow to
				// when the borrow goes through.
				mCounter[0] = mbFastTimer1 ? 253 : 256;
			}

			mCounterBorrow[0] = 0;
			SetupTimers(0x01);
			break;

		case kATPokeyEventTimer2Borrow:
			mpTimerBorrowEvents[1] = NULL;
			FireTimer<1>();

			mCounterBorrow[1] = 0;
			mCounter[1] = mAUDFP1[1];
			if (mbLinkedTimers12) {
				mCounter[0] = mAUDFP1[0];
				mCounterBorrow[0] = 0;
				SetupTimers(0x03);
			} else
				SetupTimers(0x02);
			break;

		case kATPokeyEventTimer3Borrow:
			mpTimerBorrowEvents[2] = NULL;
			FireTimer<2>();

			if (!mbLinkedTimers34) {
				mCounter[2] = mAUDFP1[2];
			} else {
				// If we are operating at 1.79MHz, three cycles have already elapsed from the underflow to
				// when the borrow goes through.
				mCounter[2] = mbFastTimer3 ? 253 : 256;
			}

			mCounterBorrow[2] = 0;
			SetupTimers(0x04);
			break;

		case kATPokeyEventTimer4Borrow:
			mpTimerBorrowEvents[3] = NULL;
			FireTimer<3>();

			mCounter[3] = mAUDFP1[3];
			mCounterBorrow[3] = 0;
			if (mbLinkedTimers34) {
				mCounter[2] = mAUDFP1[2];
				mCounterBorrow[2] = 0;
				SetupTimers(0x0C);
			} else
				SetupTimers(0x08);
			break;

		case kATPokeyEventResetTimers:
			mpResetTimersEvent = NULL;
			mCounter[0] = mAUDFP1[0];
			mCounter[1] = mAUDFP1[1];
			mCounter[2] = mAUDFP1[2];
			mCounter[3] = mAUDFP1[3];

			mpRenderer->ResetTimers();

			for(int i=0; i<4; ++i) {
				mCounterBorrow[0] = 0;

				if (mpTimerBorrowEvents[i]) {
					mpScheduler->RemoveEvent(mpTimerBorrowEvents[i]);
					mpTimerBorrowEvents[i] = NULL;
				}
			}

			SetupTimers(0x0f);
			break;

		case kATPokeyEventSerialOutput:
			mpEventSerialOutput = NULL;

			if (mSerialOutputCounter)
				OnSerialOutputTick();
			break;

		default:
			{
				unsigned idx = id - kATPokeyEventPot0ScanComplete;

				if (idx < 8) {
					mALLPOT &= ~(1 << idx);

					VDASSERT(mpPotScanEvent[idx]);
					mpPotScanEvent[idx] = NULL;
				}
			}
			break;
	}
}

template<int channel>
void ATPokeyEmulator::RecomputeTimerPeriod() {

	const bool fastTimer = (channel == 0) ? mbFastTimer1 : (channel == 2) ? mbFastTimer3 : false;
	const bool hiLinkedTimer = channel == 1 ? mbLinkedTimers12 : channel == 3 ? mbLinkedTimers34 : false;
	const bool loLinkedTimer = channel == 0 ? mbLinkedTimers12 : channel == 2 ? mbLinkedTimers34 : false;

	uint32 period;
	if (hiLinkedTimer) {
		const bool fastLinkedTimer = (channel == 1) ? mbFastTimer1 : (channel == 3) ? mbFastTimer3 : false;

		period = ((uint32)mAUDF[channel] << 8) + mAUDFP1[channel - 1];

		if (fastLinkedTimer)
			period += 6;
		else if (mbUse15KHzClock)
			period *= 114;
		else
			period *= 28;
	} else {
		period = mAUDFP1[channel];

		if (fastTimer)
			period += 3;
		else if (mbUse15KHzClock)
			period *= 114;
		else
			period *= 28;

		if (loLinkedTimer) {
			uint32 fullPeriod;

			if (fastTimer)
				fullPeriod = 256;
			else if (mbUse15KHzClock)
				fullPeriod = 256 * 114;
			else
				fullPeriod = 256 * 28;

			mTimerFullPeriod[channel >> 1] = fullPeriod;
		}
	}

	mTimerPeriod[channel] = period;
}

void ATPokeyEmulator::RecomputeAllowedDeferredTimers() {
	// Timer 2 IRQ, two-tone mode, or timer 2 as serial output clock prohibits deferring timer 2.
	mbAllowDeferredTimer[1] = !kForceActiveTimers
		&& !(mIRQEN & 0x02)
		&& !(mSKCTL & 8)
		&& (mSKCTL & 0x60) != 0x60;

	// Timer 1 IRQ or two-tone mode prohibits deferring timer 1.
	mbAllowDeferredTimer[0] = !kForceActiveTimers
		&& !(mIRQEN & 0x01)
		&& !(mSKCTL & 8);

	// Timer 4 IRQ, asynchronous receive mode, or using timer 4 as transmit clock prohibits deferring timer 4.
	mbAllowDeferredTimer[3] = !kForceActiveTimers
		&& !(mIRQEN & 0x04)
		&& !(mSKCTL & 0x10)
		&& (mSKCTL & 0x60) != 0x20
		&& (mSKCTL & 0x60) != 0x40;

	// Timer 3 deferring is ordinarily always possible.
	mbAllowDeferredTimer[2] = !kForceActiveTimers;

	// Check if timers are linked; both timers must be able to defer together.
	if (mbLinkedTimers12) {
		if (!mbAllowDeferredTimer[0] || !mbAllowDeferredTimer[1]) {
			mbAllowDeferredTimer[0] = false;
			mbAllowDeferredTimer[1] = false;
		}
	}

	if (mbLinkedTimers34) {
		if (!mbAllowDeferredTimer[2] || !mbAllowDeferredTimer[3]) {
			mbAllowDeferredTimer[2] = false;
			mbAllowDeferredTimer[3] = false;
		}
	}
}

template<int channel>
void ATPokeyEmulator::UpdateTimerCounter() {
	VDASSERT(mCounter[0] > 0);
	VDASSERT(mCounterBorrow[0] >= 0);

	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	mCounterBorrow[channel] = 0;

	const bool fastTimer = channel == 0 || (channel == 1 && mbLinkedTimers12) ? mbFastTimer1
		: channel == 2 || (channel == 3 && mbLinkedTimers34) ? mbFastTimer3 : false;
	const bool hiLinkedTimer = channel == 1 ? mbLinkedTimers12 : channel == 3 ? mbLinkedTimers34 : false;
	const bool loLinkedTimer = channel == 0 ? mbLinkedTimers12 : channel == 2 ? mbLinkedTimers34 : false;

	if (loLinkedTimer) {
		const int hiChannel = channel + 1;

		// Compute the number of ticks until the next borrow event. The borrow occurs three cycles after
		// the timer channel underflows; 16-bit channels take 6 cycles because the borrows from the two
		// channels are cascaded. If we have a borrow event, we can simply use that time directly.

		int ticksLeft;
		if (mbDeferredTimerEvents[hiChannel]) {
			if ((sint32)(t - mDeferredTimerStarts[hiChannel]) < 0)
				ticksLeft = mDeferredTimerStarts[hiChannel] - t;
			else
				ticksLeft = mDeferredTimerPeriods[hiChannel] - (t - mDeferredTimerStarts[hiChannel]) % mDeferredTimerPeriods[hiChannel];
		} else if (mpTimerBorrowEvents[hiChannel])
			ticksLeft = mpScheduler->GetTicksToEvent(mpTimerBorrowEvents[hiChannel]);
		else
			return;

		// Compute the low timer offset from the high timer offset.
		if (ticksLeft <= 3) {
			// Three ticks or less means that the high timer is borrowing, so we're free running. We've
			// already run through the three ticks of low timer borrow.
			if (fastTimer)
				mCounter[channel] = 253 - ticksLeft;
			else
				mCounter[channel] = 256;
		} else {
			ticksLeft = (uint32)(ticksLeft - 3) % mTimerFullPeriod[channel >> 1];

			if (ticksLeft <= 3) {
				// Low timer borrow is in progress.
				mCounterBorrow[channel] = ticksLeft;

				if (fastTimer)
					mCounter[channel] = 256 - ticksLeft;
				else
					mCounter[channel] = 256;
			} else {
				ticksLeft -= 3;

				if (!fastTimer) {
					if (mbUse15KHzClock)
						ticksLeft = (ticksLeft + 113) / 114;
					else
						ticksLeft = (ticksLeft + 27) / 28;
				}

				mCounter[channel] = ticksLeft;
			}
		}
	} else {
		// Compute the number of ticks until the next borrow event. The borrow occurs three cycles after
		// the timer channel underflows; 16-bit channels take 6 cycles because the borrows from the two
		// channels are cascaded. If we have a borrow event, we can simply use that time directly.

		int ticksLeft;
		if (mbDeferredTimerEvents[channel]) {
			if ((sint32)(t - mDeferredTimerStarts[channel]) < 0)
				ticksLeft = mDeferredTimerStarts[channel] - t;
			else
				ticksLeft = mDeferredTimerPeriods[channel] - (t - mDeferredTimerStarts[channel]) % mDeferredTimerPeriods[channel];
		} else if (mpTimerBorrowEvents[channel])
			ticksLeft = mpScheduler->GetTicksToEvent(mpTimerBorrowEvents[channel]);
		else
			return;

		// If we're three ticks or less away, we have a borrow pending. At fast clock the timer will begin
		// counting down immediately during the borrow; this is important to maintain 16-bit, 1.79MHz accuracy.
		// At slow clock this can only happen once if the slow clock just happens to fall at the right time
		// (can happen if 1.79MHz clocking is disabled).
		//
		// TODO: Handle slow tick happening in borrow land.

		if (ticksLeft <= 3) {
			mCounterBorrow[channel] = ticksLeft;

			if (fastTimer && !hiLinkedTimer)
				mCounter[channel] = 256 - ticksLeft;
			else
				mCounter[channel] = 256;
		} else {
			if (hiLinkedTimer) {
				if (fastTimer)
					mCounter[channel] = (ticksLeft + 255) >> 8;
				else if (mbUse15KHzClock)
					mCounter[channel] = (ticksLeft + 114*256 - 1) / (114*256);
				else
					mCounter[channel] = (ticksLeft + 28*256 - 1) / (28*256);
			} else {
				ticksLeft -= 3;

				if (fastTimer)
					mCounter[channel] = ticksLeft;
				else if (mbUse15KHzClock)
					mCounter[channel] = (ticksLeft + 113) / 114;
				else
					mCounter[channel] = (ticksLeft + 27) / 28;
			}
		}
	}

	VDASSERT(mCounter[channel] > 0 && mCounter[channel] <= 256);
	VDASSERT(mCounterBorrow[channel] >= 0);
}

void ATPokeyEmulator::SetupTimers(uint8 channels) {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	int cyclesToNextSlowTick;

	if (mbUse15KHzClock) {
		cyclesToNextSlowTick = 114 - (t - mLast15KHzTime);

		if (cyclesToNextSlowTick)
			cyclesToNextSlowTick -= 114;
	} else {
		int slowTickOffset = t - mLast64KHzTime;

		if (slowTickOffset >= 28) {
			mLast64KHzTime += 28;
			slowTickOffset -= 28;

			if (slowTickOffset >= 28) {
				int largeDelta = slowTickOffset - slowTickOffset % 28;

				mLast64KHzTime += largeDelta;
				slowTickOffset -= largeDelta;
			}
		}

		cyclesToNextSlowTick = (28 - slowTickOffset);

		if (cyclesToNextSlowTick)
			cyclesToNextSlowTick -= 28;
	}

	const bool slowTickValid = (mSKCTL & 3) != 0;

	VDASSERT(!slowTickValid || (cyclesToNextSlowTick >= -114 && cyclesToNextSlowTick <= 0));

	if (channels & 0x01) {
		mpScheduler->UnsetEvent(mpTimerBorrowEvents[0]);
		FlushDeferredTimerEvents(0);

		if (!mbFastTimer1 && !slowTickValid) {
			// 15/64KHz clock is stopped and we're not running at 1.79MHz. In this case, we still
			// fire the borrow event if we have one pending.
			if (mCounterBorrow[0])
				mpTimerBorrowEvents[0] = mpScheduler->AddEvent(mCounterBorrow[0], this, kATPokeyEventTimer1Borrow);
		} else {
			// Computer number of ticks until the next borrow event. If we have a borrow pending,
			// that's easy; otherwise, we have to look at the current counter value, slow tick offset,
			// and mode.
			uint32 ticks = mCounterBorrow[0];
			
			if (!ticks) {
				ticks = mCounter[0];

				if (!mbFastTimer1) {
					if (mbUse15KHzClock)
						ticks = ticks * 114;
					else
						ticks = ticks * 28;

					ticks += cyclesToNextSlowTick;
				}

				// Borrow takes place three cycles after underflow.
				ticks += 3;
			}

			VDASSERT((sint32)ticks > 0);

			if (!mbAllowDeferredTimer[0])
				mpTimerBorrowEvents[0] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer1Borrow);
			else if (mbLinkedTimers12) {
				const uint32 loTime = t + ticks;
				const uint32 hiTime = loTime + 3 + mTimerFullPeriod[0] * (mCounter[1] - 1);

				SetupDeferredTimerEventsLinked(0, loTime, mTimerFullPeriod[0], hiTime, mTimerPeriod[1], mTimerPeriod[0]);
			} else
				SetupDeferredTimerEvents(0, t + ticks, mTimerPeriod[0]);
		}
	}

	if (channels & 0x02) {
		mpScheduler->UnsetEvent(mpTimerBorrowEvents[1]);
		FlushDeferredTimerEvents(1);

		if (mbLinkedTimers12) {
			if (!mbFastTimer1 && !slowTickValid) {
				// 15/64KHz clock is stopped and we're not running at 1.79MHz. In this case, we still
				// fire the borrow event if we have one pending.
				if (mCounterBorrow[1])
					mpTimerBorrowEvents[1] = mpScheduler->AddEvent(mCounterBorrow[1], this, kATPokeyEventTimer2Borrow);
			} else {
				// Computer number of ticks until the next borrow event. If we have a borrow pending,
				// that's easy; otherwise, we have to look at the current counter value, slow tick offset,
				// and mode.
				uint32 ticks = mCounterBorrow[1];
				
				if (!ticks) {
					ticks = mCounterBorrow[0];

					if (!ticks) {
						ticks = mCounter[0];

						if (!mbFastTimer1) {
							if (mbUse15KHzClock)
								ticks = ticks * 114;
							else
								ticks = ticks * 28;

							ticks += cyclesToNextSlowTick;
						}

						ticks += 3;
					}

					if (mbFastTimer1)
						ticks += (mCounter[1] - 1) << 8;
					else if (mbUse15KHzClock)
						ticks += (mCounter[1] - 1) * (256 * 114);
					else
						ticks += (mCounter[1] - 1) * (256 * 28);

					// Borrow takes place three cycles after underflow.
					ticks += 3;
				}

				VDASSERT((sint32)ticks > 0);

				if (!mbAllowDeferredTimer[1])
					mpTimerBorrowEvents[1] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer2Borrow);
				else
					SetupDeferredTimerEvents(1, t + ticks, mTimerPeriod[1]);

			}
		} else {
			if (!slowTickValid) {
				// 15/64KHz clock is stopped and we're not running at 1.79MHz. In this case, we still
				// fire the borrow event if we have one pending.
				if (mCounterBorrow[1])
					mpTimerBorrowEvents[1] = mpScheduler->AddEvent(mCounterBorrow[1], this, kATPokeyEventTimer2Borrow);
			} else {
				// Computer number of ticks until the next borrow event. If we have a borrow pending,
				// that easy; otherwise, we have to look at the current counter value, slow tick offset,
				// and mode.
				uint32 ticks = mCounterBorrow[1];
				
				if (!ticks) {
					ticks = mCounter[1];

					if (mbUse15KHzClock)
						ticks = ticks * 114;
					else
						ticks = ticks * 28;

					ticks += cyclesToNextSlowTick;

					// Borrow takes place three cycles after underflow.
					ticks += 3;
				}

				VDASSERT((sint32)ticks > 0);

				if (!mbAllowDeferredTimer[1]) {
					mpTimerBorrowEvents[1] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer2Borrow);
				} else {
					SetupDeferredTimerEvents(1, t + ticks, mTimerPeriod[1]);
				}
			}
		}
	}

	if ((mSKCTL & 0x10) && mbSerialWaitingForStartBit) {
		if (channels & 0x04) {
			mpScheduler->UnsetEvent(mpTimerBorrowEvents[2]);
			FlushDeferredTimerEvents(2);

			if (mCounterBorrow[2])
				mpTimerBorrowEvents[2] = mpScheduler->AddEvent(mCounterBorrow[2], this, kATPokeyEventTimer3Borrow);
		}

		if (channels & 0x08) {
			mpScheduler->UnsetEvent(mpTimerBorrowEvents[3]);
			FlushDeferredTimerEvents(3);

			if (mCounterBorrow[3])
				mpTimerBorrowEvents[3] = mpScheduler->AddEvent(mCounterBorrow[3], this, kATPokeyEventTimer4Borrow);
		}
	} else {
		if (channels & 0x04) {
			mpScheduler->UnsetEvent(mpTimerBorrowEvents[2]);
			FlushDeferredTimerEvents(2);

			if (!mbFastTimer3 && !slowTickValid) {
				// 15/64KHz clock is stopped and we're not running at 1.79MHz. In this case, we still
				// fire the borrow event if we have one pending.
				if (mCounterBorrow[2])
					mpTimerBorrowEvents[2] = mpScheduler->AddEvent(mCounterBorrow[2], this, kATPokeyEventTimer3Borrow);
			} else {
				// Computer number of ticks until the next borrow event. If we have a borrow pending,
				// that's easy; otherwise, we have to look at the current counter value, slow tick offset,
				// and mode.
				uint32 ticks = mCounterBorrow[2];
				
				if (!ticks) {
					ticks = mCounter[2];

					if (!mbFastTimer3) {
						if (mbUse15KHzClock)
							ticks = ticks * 114;
						else
							ticks = ticks * 28;

						ticks += cyclesToNextSlowTick;
					}

					// Borrow takes place three cycles after underflow.
					ticks += 3;
				}

				VDASSERT((sint32)ticks > 0);

				// Check if we need an active event or if we can go to deferred mode. We only need an
				// active event if timers are linked, timer 1 IRQ is enabled, or two-tone mode is active.
				if (!mbAllowDeferredTimer[2])
					mpTimerBorrowEvents[2] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer3Borrow);
				else if (mbLinkedTimers34) {
					const uint32 loTime = t + ticks;
					const uint32 hiTime = loTime + 3 + mTimerFullPeriod[1] * (mCounter[3] - 1);

					SetupDeferredTimerEventsLinked(2, loTime, mTimerFullPeriod[1], hiTime, mTimerPeriod[3], mTimerPeriod[2]);
				} else
					SetupDeferredTimerEvents(2, t + ticks, mTimerPeriod[2]);
			}
		}

		if (channels & 0x08) {
			mpScheduler->UnsetEvent(mpTimerBorrowEvents[3]);
			FlushDeferredTimerEvents(3);

			if (mbLinkedTimers34) {
				if (!mbFastTimer3 && !slowTickValid) {
					// 15/64KHz clock is stopped and we're not running at 1.79MHz. In this case, we still
					// fire the borrow event if we have one pending.
					if (mCounterBorrow[3])
						mpTimerBorrowEvents[3] = mpScheduler->AddEvent(mCounterBorrow[3], this, kATPokeyEventTimer4Borrow);
				} else {
					// Computer number of ticks until the next borrow event. If we have a borrow pending,
					// that's easy; otherwise, we have to look at the current counter value, slow tick offset,
					// and mode.
					uint32 ticks = mCounterBorrow[3];
					
					if (!ticks) {
						ticks = mCounterBorrow[2];

						if (!ticks) {
							ticks = mCounter[2];

							if (!mbFastTimer3) {
								if (mbUse15KHzClock)
									ticks = ticks * 114;
								else
									ticks = ticks * 28;

								ticks += cyclesToNextSlowTick;
							}

							ticks += 3;
						}

						if (mbFastTimer3)
							ticks += (mCounter[3] - 1) << 8;
						else if (mbUse15KHzClock)
							ticks += (mCounter[3] - 1) * (114 * 256);
						else
							ticks += (mCounter[3] - 1) * (28 * 256);

						// Borrow takes place three cycles after underflow.
						ticks += 3;
					}

					VDASSERT(ticks > 0);

					if (!mbAllowDeferredTimer[3])
						mpTimerBorrowEvents[3] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer4Borrow);
					else
						SetupDeferredTimerEvents(3, t + ticks, mTimerPeriod[3]);
				}
			} else {
				if (!slowTickValid) {
					// 15/64KHz clock is stopped and we're not running at 1.79MHz. In this case, we still
					// fire the borrow event if we have one pending.
					if (mCounterBorrow[3])
						mpTimerBorrowEvents[3] = mpScheduler->AddEvent(mCounterBorrow[3], this, kATPokeyEventTimer4Borrow);
				} else {
					// Computer number of ticks until the next borrow event. If we have a borrow pending,
					// that easy; otherwise, we have to look at the current counter value, slow tick offset,
					// and mode.
					uint32 ticks = mCounterBorrow[3];
					
					if (!ticks) {
						ticks = mCounter[3];

						if (mbUse15KHzClock)
							ticks = ticks * 114;
						else
							ticks = ticks * 28;

						ticks += cyclesToNextSlowTick;

						// Borrow takes place three cycles after underflow.
						ticks += 3;
					}

					VDASSERT(ticks > 0);

					// We only need the timer 4 event if the timers are linked, the timer 4 IRQ is enabled,
					// asynchronous receive mode is enabled, or timer 4 is being used as the output clock.
					if (!mbAllowDeferredTimer[3]) {
						mpTimerBorrowEvents[3] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer4Borrow);
					} else {
						SetupDeferredTimerEvents(3, t + ticks, mTimerPeriod[3]);
					}
				}
			}
		}
	}
}

void ATPokeyEmulator::FlushDeferredTimerEvents(int channel) {
	if (!mbDeferredTimerEvents[channel])
		return;

	// get current time
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	mbDeferredTimerEvents[channel] = false;

	mpRenderer->ClearChannelDeferredEvents(channel, t);
}

void ATPokeyEmulator::SetupDeferredTimerEvents(int channel, uint32 t0, uint32 period) {
	VDASSERT(!mbDeferredTimerEvents[channel]);
	mbDeferredTimerEvents[channel] = true;
	mDeferredTimerStarts[channel] = t0;
	mDeferredTimerPeriods[channel] = period;

	mpRenderer->SetChannelDeferredEvents(channel, t0, period);
}

void ATPokeyEmulator::SetupDeferredTimerEventsLinked(int channel, uint32 t0, uint32 period, uint32 hit0, uint32 hiperiod, uint32 hilooffset) {
	VDASSERT(!mbDeferredTimerEvents[channel]);
	mbDeferredTimerEvents[channel] = true;
	mDeferredTimerStarts[channel] = t0;
	mDeferredTimerPeriods[channel] = period;

	mpRenderer->SetChannelDeferredEventsLinked(channel, t0, period, hit0, hiperiod, hilooffset);
}

uint8 ATPokeyEmulator::DebugReadByte(uint8 reg) const {
	reg &= 0x1f;

	switch(reg) {
		case 0x00:	// $D200 POT0
		case 0x01:	// $D201 POT1
		case 0x02:	// $D202 POT2
		case 0x03:	// $D203 POT3
		case 0x04:	// $D204 POT4
		case 0x05:	// $D205 POT5
		case 0x06:	// $D206 POT6
		case 0x07:	// $D207 POT7
			return const_cast<ATPokeyEmulator *>(this)->ReadByte(reg);
		case 0x08:	// $D208 ALLPOT
			return mALLPOT;
		case 0x09:	// $D209 KBCODE
			return mKBCODE;
		case 0x0A:	// $D20A RANDOM
			return const_cast<ATPokeyEmulator *>(this)->ReadByte(reg);
		case 0x0D:	// $D20D SERIN
			return mSERIN;
		case 0x0E:
			return mIRQST;
		case 0x0F:
			return const_cast<ATPokeyEmulator *>(this)->ReadByte(reg);
	}

	if (reg & 0x10) {
		if (mpSlave)
			return mpSlave->DebugReadByte(reg & 0x0f);
		else
			return DebugReadByte(reg & 0x0f);
	}

	return 0xFF;
}

uint8 ATPokeyEmulator::ReadByte(uint8 reg) {
	reg &= 0x1f;

	switch(reg) {
		case 0x00:	// $D200 POT0
		case 0x01:	// $D201 POT1
		case 0x02:	// $D202 POT2
		case 0x03:	// $D203 POT3
		case 0x04:	// $D204 POT4
		case 0x05:	// $D205 POT5
		case 0x06:	// $D206 POT6
		case 0x07:	// $D207 POT7
			{
				int index = reg & 7;
				uint8 val = mPOT[index];

				if (mALLPOT & (1 << index)) {
					uint32 delay = (uint32)ATSCHEDULER_GETTIME(mpScheduler) - mPotScanStartTime;

					if (!(mSKCTL & 0x04))
						delay /= 114;
					else if (delay < 2)
						delay = 0;
					else
						delay -= 2;

					if (delay > 228)
						val = 228;
					else
						val = (uint8)delay;
				}

				return val;
			}

		case 0x08:	// $D208 ALLPOT
			return mALLPOT;
		case 0x09:	// $D209 KBCODE
			return mKBCODE;
		case 0x0A:	// $D20A RANDOM
			if (mSKCTL & 3) {
				int t = ATSCHEDULER_GETTIME(mpScheduler);
				int polyDelta = t - mLastPolyTime;
				mPoly9Counter += polyDelta;
				mPoly17Counter += polyDelta;
				mLastPolyTime = t;

				if (mPoly9Counter >= 511)
					mPoly9Counter %= 511;

				if (mPoly17Counter >= 131071)
					mPoly17Counter %= 131071;

			}
			return mAUDCTL & 0x80 ? (uint8)(mpTables->mPoly9Buffer[mPoly9Counter]) : (uint8)(mpTables->mPoly17Buffer[mPoly17Counter]);
		case 0x0D:	// $D20D SERIN
			{
				uint8 c = mSERIN;

				if (mbTraceSIO)
					ATConsoleTaggedPrintf("POKEY: Reading SERIN value %02x (shiftreg: %02x)\n", c, mSerialInputShiftRegister);

				if ((mIRQST & 0x20) && mSerBurstMode == kSerialBurstMode_Standard) {
					for(Devices::const_iterator it(mDevices.begin()), itEnd(mDevices.end()); it!=itEnd && (mIRQST & 0x20); ++it)
						(*it)->PokeySerInReady();
				}

				return c;
			}
			break;

		case 0x0E:	// $D20E IRQST
			if (mbSerInBurstPending) {
				if (mSerBurstMode == kSerialBurstMode_Polled) {
					for(Devices::const_iterator it(mDevices.begin()), itEnd(mDevices.end()); it!=itEnd && (mIRQST & 0x20); ++it)
						(*it)->PokeySerInReady();
				}

				mbSerInBurstPending = false;
			}

			return mIRQST;

		case 0x0F:
			{
				uint8 c = mSKSTAT;

				if (mbSerialSimulateInputPort) {
					uint32 dt = ATSCHEDULER_GETTIME(mpScheduler) - mSerialSimulateInputBaseTime;
					uint32 bitidx = dt / mSerialSimulateInputCyclesPerBit;

					if (bitidx >= 10)
						mbSerialSimulateInputPort = false;
					else {
						c &= 0xef;

						if (mSerialSimulateInputData & (1 << bitidx))
							c |= 0x10;
					}
				}

				return c;
			}
			break;

		default:
//			__debugbreak();
			break;
	}

	if (reg & 0x10) {
		if (mpSlave)
			return mpSlave->ReadByte(reg & 0x0f);
		else
			return ReadByte(reg & 0x0f);
	}

	return 0xFF;
}

void ATPokeyEmulator::WriteByte(uint8 reg, uint8 value) {
	reg &= 0x1f;

	mState.mReg[reg] = value;

	switch(reg) {
		case 0x00:	// $D200 AUDF1
			if (mAUDF[0] != value) {
				mAUDF[0] = value;
				mAUDFP1[0] = (int)value + 1;

				RecomputeTimerPeriod<0>();
				
				if (mbLinkedTimers12) {
					RecomputeTimerPeriod<1>();

					// Both counters must be updated before we run SetupTimers().
					UpdateTimerCounter<0>();
					UpdateTimerCounter<1>();
					SetupTimers(0x03);
				} else {
					UpdateTimerCounter<0>();
					SetupTimers(0x01);
				}
			}
			break;
		case 0x01:	// $D201 AUDC1
			if (mAUDC[0] != value) {
				mAUDC[0] = value;
				mpRenderer->SetAUDCx(0, value);
			}
			break;
		case 0x02:	// $D202 AUDF2
			if (mAUDF[1] != value) {
				mAUDF[1] = value;
				mAUDFP1[1] = (int)value + 1;

				RecomputeTimerPeriod<1>();

				if (mbLinkedTimers12) {
					// Both counters must be updated before we run SetupTimers().
					UpdateTimerCounter<0>();
					UpdateTimerCounter<1>();
					SetupTimers(0x03);
				} else {
					UpdateTimerCounter<1>();
					SetupTimers(0x02);
				}
			}
			break;
		case 0x03:	// $D203 AUDC2
			if (mAUDC[1] != value) {
				mAUDC[1] = value;
				mpRenderer->SetAUDCx(1, value);
			}
			break;
		case 0x04:	// $D204 AUDF3
			if (mAUDF[2] != value) {
				mAUDF[2] = value;
				mAUDFP1[2] = (int)value + 1;

				RecomputeTimerPeriod<2>();

				if (mbLinkedTimers34) {
					RecomputeTimerPeriod<3>();

					// Both counters must be updated before we run SetupTimers().
					UpdateTimerCounter<2>();
					UpdateTimerCounter<3>();
					SetupTimers(0x0C);
				} else {
					UpdateTimerCounter<2>();
					SetupTimers(0x04);
				}
			}
			mbSerialRateChanged = true;
			break;
		case 0x05:	// $D205 AUDC3
			if (mAUDC[2] != value) {
				mAUDC[2] = value;
				mpRenderer->SetAUDCx(2, value);
			}
			break;
		case 0x06:	// $D206 AUDF4
			if (mAUDF[3] != value) {
				mAUDF[3] = value;
				mAUDFP1[3] = (int)value + 1;

				RecomputeTimerPeriod<3>();

				if (mbLinkedTimers34) {
					// Both counters must be updated before we run SetupTimers().
					UpdateTimerCounter<2>();
					UpdateTimerCounter<3>();
					SetupTimers(0x0C);
				} else {
					UpdateTimerCounter<3>();
					SetupTimers(0x08);
				}
			}
			mbSerialRateChanged = true;
			break;
		case 0x07:	// $D207 AUDC4
			if (mAUDC[3] != value) {
				mAUDC[3] = value;
				mpRenderer->SetAUDCx(3, value);
			}
			break;
		case 0x08:	// $D208 AUDCTL
			if (mAUDCTL != value) {
				uint8 delta = mAUDCTL ^ value;
				if (delta & 0x29)
					mbSerialRateChanged = true;

				UpdateTimerCounter<0>();
				UpdateTimerCounter<1>();
				UpdateTimerCounter<2>();
				UpdateTimerCounter<3>();

				FlushDeferredTimerEvents(0);
				FlushDeferredTimerEvents(1);
				FlushDeferredTimerEvents(2);
				FlushDeferredTimerEvents(3);

				mAUDCTL = value;
				mbFastTimer1 = (mAUDCTL & 0x40) != 0;
				mbFastTimer3 = (mAUDCTL & 0x20) != 0;
				mbLinkedTimers12 = (mAUDCTL & 0x10) != 0;
				mbLinkedTimers34 = (mAUDCTL & 0x08) != 0;
				mbUse15KHzClock = (mAUDCTL & 0x01) != 0;

				mpRenderer->SetAUDCTL(value);

				if (delta & 0x18)
					RecomputeAllowedDeferredTimers();

				if (delta & 0x51) {
					RecomputeTimerPeriod<0>();
					RecomputeTimerPeriod<1>();
				}

				if (delta & 0x29) {
					RecomputeTimerPeriod<2>();
					RecomputeTimerPeriod<3>();
				}

				SetupTimers(0x0f);
			}
			break;
		case 0x09:	// $D209 STIMER
			mpScheduler->SetEvent(4, this, kATPokeyEventResetTimers, mpResetTimersEvent);
			break;
		case 0x0A:	// $D20A SKRES
			mSKSTAT |= 0xe0;
			break;
		case 0x0B:	// $D20B POTGO
			mALLPOT = 0xFF;

			if (mSKCTL & 4)
				mPotScanStartTime = ATSCHEDULER_GETTIME(mpScheduler);
			else
				mPotScanStartTime = mLast15KHzTime;

			for(int i=0; i<8; ++i) {
				if (mpPotScanEvent[i])
					mpScheduler->RemoveEvent(mpPotScanEvent[i]);

				int delta = mPOT[i];

				if (!(mSKCTL & 0x04))
					delta *= 114;

				if (!delta)
					++delta;

				mpPotScanEvent[i] = mpScheduler->AddEvent(delta, this, kATPokeyEventPot0ScanComplete + i);
			}
			break;
		case 0x0D:	// $D20D SEROUT
			if (mbTraceSIO)
				ATConsoleTaggedPrintf("POKEY: Sending serial byte %02x\n", value);

			// The only thing that writing to SEROUT does is load the register and set a latch
			// indicating that a byte is ready. The actual load into the output shift register
			// cannot occur until another serial clock pulse arrives.
			mSEROUT = value;
			if (mbSerOutValid && mbTraceSIO)
				ATConsoleTaggedPrintf("POKEY: Serial output overrun detected.\n");

			if (!mSerialOutputCounter)
				mSerialOutputCounter = 1;

			mbSerOutValid = true;
			break;
		case 0x0E:
			if (mIRQEN != value) {
				const uint8 delta = (mIRQEN ^ value);

				mIRQEN = value;

				mIRQST |= ~value & 0xF7;

				if (mbBreakKeyState && (mIRQEN & mIRQST & 0x80)) {
					mIRQST |= 0x80;
				}

				if (!(mIRQEN & ~mIRQST))
					mpConn->PokeyNegateIRQ(true);
				else
					mpConn->PokeyAssertIRQ(true);

				if ((mIRQST & 0x20) && mSerBurstMode == kSerialBurstMode_Polled)
					mbSerInBurstPending = true;

				// Check if any of the IRQ bits are being turned on and we are currently running that timer
				// in deferred mode. If so, we need to yank it out of deferred mode. We don't do the
				// opposite here; we wait until the existing timer expires to reinit the timer into
				// deferred mode so as to not trigger on momentary clears.
				if (delta & 0x07) {
					RecomputeAllowedDeferredTimers();

					if (delta & mIRQEN & 0x07) {
						uint8 timersToChange = 0;

						// timer 1
						if ((delta & 0x01) && mbDeferredTimerEvents[0])
							timersToChange |= 0x01;

						// timer 2
						if ((delta & 0x02) && mbDeferredTimerEvents[1])
							timersToChange |= 0x02;

						// timer 4
						if ((delta & 0x04) && mbDeferredTimerEvents[3])
							timersToChange |= 0x08;

						if (timersToChange) {
							UpdateTimerCounter<0>();
							UpdateTimerCounter<1>();
							UpdateTimerCounter<2>();
							UpdateTimerCounter<3>();

							SetupTimers(timersToChange);
						}
					}
				}
			}
			break;
		case 0x0F:
			if (value != mSKCTL) {
				UpdateTimerCounter<0>();
				UpdateTimerCounter<1>();
				UpdateTimerCounter<2>();
				UpdateTimerCounter<3>();

				if (!(mSKCTL & 0x10) && (value & 0x10) && mbSerialWaitingForStartBit) {
					// Restart timers 3 and 4 immediately.
					mCounter[2] = mAUDFP1[2];
					mCounter[3] = mAUDFP1[3];
				}

				bool prvInit = (mSKCTL & 3) == 0;
				bool newInit = (value & 3) == 0;

				if (newInit != prvInit) {
					if (newInit) {
						if (mp15KHzEvent) {
							mpScheduler->RemoveEvent(mp15KHzEvent);
							mp15KHzEvent = NULL;
						}
					} else {
						VDASSERT(!mp15KHzEvent);

						// The 64KHz polynomial counter is a 5-bit LFSR that is reset to all 1s
						// on init and XORs bits 4 and 2, if seen as shifting left. In order to
						// achieve a 28 cycle period, a one is force fed when the register
						// equals 00010. A side effect of this mechanism is that resetting the
						// register actually starts it 19 cycles in. There is, however, an
						// additional two clock delay from the shift register comparator to
						// the timers.

						// The 15KHz polynomial counter is a 7-bit LFSR that is reset to all 0s
						// on init and XNORs bits 7 and 6, if seen as shifting left. In order to
						// achieve a 114 cycle period, a one is force fed when the register
						// equals 1001001.
						mp15KHzEvent = mpScheduler->AddEvent(81, this, kATPokeyEvent15KHzTick);

						mLast15KHzTime = ATSCHEDULER_GETTIME(mpScheduler) + 81 - 114;
						mLast64KHzTime = ATSCHEDULER_GETTIME(mpScheduler) + 22 - 28;
					}

					mpRenderer->SetInitMode(newInit);

					mPoly9Counter = 0;
					mPoly17Counter = 0;
					mLastPolyTime = ATSCHEDULER_GETTIME(mpScheduler) + 1;

					mSerialInputShiftRegister = 0;
					mSerialOutputShiftRegister = 0;
					mSerialOutputCounter = 0;
					mSerialInputCounter = 0;
					mbSerOutValid = false;
					mbSerShiftValid = false;
					mbSerialOutputState = false;
					mSKSTAT |= 0xC0;

					mIRQST |= 0x20;
					if (mIRQEN & 0x10)
						mIRQST &= ~0x10;
					mIRQST &= ~0x08;

					if (!(mIRQEN & ~mIRQST))
						mpConn->PokeyNegateIRQ(true);
					else
						mpConn->PokeyAssertIRQ(true);

					if (mpCassette)
						mpCassette->PokeyResetSerialInput();
				}

				mSKCTL = value;
				RecomputeAllowedDeferredTimers();
				SetupTimers(0x0f);
			}
			break;

		default:
			if (reg & 0x10) {
				if (mpSlave)
					mpSlave->WriteByte(reg & 0x0f, value);
				else
					WriteByte(reg & 0x0f, value);
			}
			break;
	}
}

void ATPokeyEmulator::DumpStatus() {
	if (mpSlave) {
		ATConsolePrintf("Primary POKEY:\n");
		DumpStatus(false);
		ATConsolePrintf("\nSecondary POKEY:\n");
		mpSlave->DumpStatus(true);
	} else {
		DumpStatus(false);
	}
}

void ATPokeyEmulator::BeginLoadState(ATSaveStateReader& reader) {
	reader.RegisterHandlerMethod(kATSaveStateSection_Arch, VDMAKEFOURCC('P', 'O', 'K', 'Y'), this, &ATPokeyEmulator::LoadStateArch);
	reader.RegisterHandlerMethod(kATSaveStateSection_Private, VDMAKEFOURCC('P', 'O', 'K', 'Y'), this, &ATPokeyEmulator::LoadStatePrivate);
	reader.RegisterHandlerMethod(kATSaveStateSection_ResetPrivate, 0, this, &ATPokeyEmulator::LoadStateResetPrivate);
	reader.RegisterHandlerMethod(kATSaveStateSection_End, 0, this, &ATPokeyEmulator::EndLoadState);

	if (mpSlave)
		mpSlave->BeginLoadState(reader);
}

void ATPokeyEmulator::LoadStateArch(ATSaveStateReader& reader) {
	for(int i=0; i<4; ++i) {
		mAUDF[i] = reader.ReadUint8();
		mAUDC[i] = reader.ReadUint8();
	}

	mAUDCTL = reader.ReadUint8();
	mIRQEN = reader.ReadUint8();
	mIRQST = reader.ReadUint8();
	mSKCTL = reader.ReadUint8();
}

void ATPokeyEmulator::LoadStatePrivate(ATSaveStateReader& reader) {
	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	mALLPOT = reader.ReadUint8();
	mKBCODE = reader.ReadUint8();
	
	for(int i=0; i<4; ++i)
		reader != mCounter[i];

	for(int i=0; i<4; ++i)
		reader != mCounterBorrow[i];

	mLast15KHzTime = t - reader.ReadUint8();
	mLast64KHzTime = t - reader.ReadUint8();

	mPoly9Counter = reader.ReadUint16() % 511;
	mPoly17Counter = reader.ReadUint32() % 131071;

	mpRenderer->LoadState(reader);
}

void ATPokeyEmulator::LoadStateResetPrivate(ATSaveStateReader& reader) {
	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	mALLPOT = 0xFF;
	mKBCODE = 0xFF;

	for(int i=0; i<4; ++i) {
		mCounter[i] = 1;
		mCounterBorrow[i] = 0;
	}

	mLast15KHzTime = t;
	mLast64KHzTime = t;

	mPoly9Counter = 0;
	mPoly17Counter = 0;

	mpRenderer->ResetState();
}

void ATPokeyEmulator::EndLoadState(ATSaveStateReader& reader) {
	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	mbFastTimer1 = (mAUDCTL & 0x40) != 0;
	mbFastTimer3 = (mAUDCTL & 0x20) != 0;
	mbLinkedTimers12 = (mAUDCTL & 0x10) != 0;
	mbLinkedTimers34 = (mAUDCTL & 0x08) != 0;
	mbUse15KHzClock = (mAUDCTL & 0x01) != 0;

	mpRenderer->SetInitMode((mSKCTL & 3) == 0);
	mpRenderer->SetAUDCTL(mAUDCTL);
	for(int i=0; i<4; ++i) {
		mAUDFP1[i] = mAUDF[i] + 1;
		mpRenderer->SetAUDCx(i, mAUDC[i]);
	}

	mLastPolyTime = t;

	mpScheduler->SetEvent(114 - (t - mLast15KHzTime), this, kATPokeyEvent15KHzTick, mp15KHzEvent);

	RecomputeTimerPeriod<0>();
	RecomputeTimerPeriod<1>();
	RecomputeTimerPeriod<2>();
	RecomputeTimerPeriod<3>();
	RecomputeAllowedDeferredTimers();

	SetupTimers(0x0f);

	if (mpSlave)
		mpSlave->EndLoadState(reader);
}

void ATPokeyEmulator::BeginSaveState(ATSaveStateWriter& writer) {
	UpdateTimerCounter<0>();
	UpdateTimerCounter<1>();
	UpdateTimerCounter<2>();
	UpdateTimerCounter<3>();

	writer.RegisterHandlerMethod(kATSaveStateSection_Arch, this, &ATPokeyEmulator::SaveStateArch);
	writer.RegisterHandlerMethod(kATSaveStateSection_Private, this, &ATPokeyEmulator::SaveStatePrivate);
}

void ATPokeyEmulator::SaveStateArch(ATSaveStateWriter& writer) {
	writer.BeginChunk(VDMAKEFOURCC('P', 'O', 'K', 'Y'));

	for(int i=0; i<4; ++i) {
		writer.WriteUint8(mAUDF[i]);
		writer.WriteUint8(mAUDC[i]);
	}

	writer.WriteUint8(mAUDCTL);
	writer.WriteUint8(mIRQEN);
	writer.WriteUint8(mIRQST);
	writer.WriteUint8(mSKCTL);

	writer.EndChunk();

	if (mpSlave)
		mpSlave->SaveStateArch(writer);
}

void ATPokeyEmulator::SaveStatePrivate(ATSaveStateWriter& writer) {
	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	writer.BeginChunk(VDMAKEFOURCC('P', 'O', 'K', 'Y'));

	writer.WriteUint8(mALLPOT);
	writer.WriteUint8(mKBCODE);

	for(int i=0; i<4; ++i)
		writer != mCounter[i];

	for(int i=0; i<4; ++i)
		writer != mCounterBorrow[i];

	writer.WriteUint8(t - mLast15KHzTime);
	writer.WriteUint8(t - mLast64KHzTime);

	int polyDelta = t - mLastPolyTime;
	writer.WriteUint16((mPoly9Counter + polyDelta) % 511);
	writer.WriteUint32((mPoly17Counter + polyDelta) % 131071);

	mpRenderer->SaveState(writer);

	writer.EndChunk();

	if (mpSlave)
		mpSlave->SaveStatePrivate(writer);
}

void ATPokeyEmulator::GetRegisterState(ATPokeyRegisterState& state) const {
	state = mState;
}

void ATPokeyEmulator::GetAudioState(ATPokeyAudioState& state) const {
	mpRenderer->GetAudioState(state);
}

void ATPokeyEmulator::DumpStatus(bool isSlave) {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	VDStringA s;
	for(int i=0; i<4; ++i) {
		s.sprintf("AUDF%u: %02x  AUDC%u: %02x  Output: %d", i+1, mAUDF[i], i+1, mAUDC[i], mpRenderer->GetChannelOutput(i));

		if (mbDeferredTimerEvents[i]) {
			uint32 delay;

			if (mDeferredTimerStarts[i] > t)
				delay = mDeferredTimerStarts[i] - t;
			else
				delay = mDeferredTimerPeriods[i] - (t - mDeferredTimerStarts[i]) % mDeferredTimerPeriods[i];

			s.append_sprintf("  (%u cycles until fire) (passive: %d cycles)", delay, mDeferredTimerPeriods[i]);
		} else if (mpTimerBorrowEvents[i])
			s.append_sprintf("  (%u cycles until fire) (active)", (int)mpScheduler->GetTicksToEvent(mpTimerBorrowEvents[i]));

		s += '\n';
		ATConsoleWrite(s.c_str());
	}

	ATConsolePrintf("AUDCTL: %02x%s%s%s%s%s%s%s%s\n"
		, mAUDCTL
		, mAUDCTL & 0x80 ? ", 9-bit poly" : ", 17-bit poly"
		, mAUDCTL & 0x40 ? ", 1.79 ch1" : ""
		, mAUDCTL & 0x20 ? ", 1.79 ch3" : ""
		, mAUDCTL & 0x10 ? ", ch1+ch2" : ""
		, mAUDCTL & 0x08 ? ", ch3+ch4" : ""
		, mAUDCTL & 0x04 ? ", highpass 1+3" : ""
		, mAUDCTL & 0x02 ? ", highpass 2+4" : ""
		, mAUDCTL & 0x01 ? ", 15KHz" : ", 64KHz");
	ATConsolePrintf("SKCTL: %02x\n", mSKCTL);
	ATConsolePrintf("SERIN: %02x\n", mSERIN);
	ATConsolePrintf("SEROUT: %02x (%s)\n", mSEROUT, mbSerOutValid ? "pending" : "done");
	ATConsolePrintf("        shift register %02x (%d: %s)\n", mSerialOutputShiftRegister, mSerialOutputCounter, mSerialOutputCounter ? "pending" : "done");
	ATConsolePrintf("IRQEN:  %02x%s%s%s%s%s%s%s%s\n"
		, mIRQEN
		, mIRQEN & 0x80 ? ", break key" : ""
		, mIRQEN & 0x40 ? ", keyboard" : ""
		, mIRQEN & 0x20 ? ", serin" : ""
		, mIRQEN & 0x10 ? ", serout" : ""
		, mIRQEN & 0x08 ? ", sertrans" : ""
		, mIRQEN & 0x04 ? ", timer4" : ""
		, mIRQEN & 0x02 ? ", timer2" : ""
		, mIRQEN & 0x01 ? ", timer1" : ""
		);
	ATConsolePrintf("IRQST:  %02x%s%s%s%s%s%s%s%s\n"
		, mIRQST
		, mIRQST & 0x80 ? "" : ", break key"
		, mIRQST & 0x40 ? "" : ", keyboard"
		, mIRQST & 0x20 ? "" : ", serin"
		, mIRQST & 0x10 ? "" : ", serout"
		, mIRQST & 0x08 ? "" : ", sertrans"
		, mIRQST & 0x04 ? "" : ", timer4"
		, mIRQST & 0x02 ? "" : ", timer2"
		, mIRQST & 0x01 ? "" : ", timer1"
		);
	ATConsolePrintf("ALLPOT: %02x\n", mALLPOT);

	for(int i=0; i<8; ++i) {
		if (mpPotScanEvent[i])
			ATConsolePrintf("  Pot %d will finish in %d cycles\n", i, mpScheduler->GetTicksToEvent(mpPotScanEvent[i]));
	}
	

	ATConsolePrintf("\nCommand line: %s\n", mbCommandLineState ? "asserted" : "negated");
}

void ATPokeyEmulator::FlushAudio(bool pushAudio) {
	const uint32 outputSampleCount = mpRenderer->EndBlock();

	if (mpSlave) {
		const uint32 slaveOutputSampleCount = mpSlave->mpRenderer->EndBlock();

		VDASSERT(outputSampleCount == slaveOutputSampleCount);
	}

	if (mpAudioOut) {
		uint32 timestamp = mpConn->PokeyGetTimestamp() - 28 * outputSampleCount;

		if (mpSoundBoard)
			mpSoundBoard->WriteAudio(mpRenderer->GetOutputBuffer(), mpSlave ? mpSlave->mpRenderer->GetOutputBuffer() : NULL, outputSampleCount, pushAudio, timestamp);
		else
			mpAudioOut->WriteAudio(mpRenderer->GetOutputBuffer(), mpSlave ? mpSlave->mpRenderer->GetOutputBuffer() : NULL, outputSampleCount, pushAudio, timestamp);
	}
}

void ATPokeyEmulator::UpdateMixTable() {
	if (mbNonlinearMixingEnabled) {
		for(int i=0; i<61; ++i) {
			float x = (float)i / 60.0f;
			float y = ((1.30f*x - 3.3f)*x + 3.0f)*x;

			mpTables->mMixTable[i] = y * 60.0f;
		}
	} else {
		for(int i=0; i<61; ++i)
			mpTables->mMixTable[i] = (float)i;
	}
}

void ATPokeyEmulator::TryPushNextKey() {
	uint8 c = mKeyQueue.front();
	mKeyQueue.pop_front();

	PushKey(c, false, false, false, mbUseKeyCooldownTimer);
}
