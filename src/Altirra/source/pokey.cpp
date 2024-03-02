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
#include <float.h>
#include <vd2/system/math.h>

#include <math.h>

#include "console.h"
#include "cpu.h"
#include "savestate.h"
#include "audiooutput.h"

namespace {
	enum {
		kATPokeyEvent64KHzTick = 1,
		kATPokeyEvent15KHzTick = 2,
		kATPokeyEventTimer1 = 3,
		kATPokeyEventTimer2 = 4,
		kATPokeyEventTimer3 = 5,
		kATPokeyEventTimer4 = 6,
		kATPokeyEventTimer1Borrow = 7,
		kATPokeyEventTimer2Borrow = 8,
		kATPokeyEventTimer3Borrow = 9,
		kATPokeyEventTimer4Borrow = 10,
		kATPokeyEventResetTimers = 11,
		kATPokeyEventPot0ScanComplete = 16,	// x8
		kATPokeyEventAudio = 32
	};
}

ATPokeyEmulator::ATPokeyEmulator(bool isSlave)
	: mAccum(0)
	, mSampleCounter(0)
	, mOutputLevel(0)
	, mLastOutputTime(0)
	, mAudioInput(0)
	, mAudioInput2(0)
	, mExternalInput(0)
	, mTicksAccumulated(0)
	, mbCommandLineState(false)
	, mb5200Mode(false)
	, mbTraceSIO(false)
	, mbNonlinearMixingEnabled(true)
	, mKBCODE(0)
	, mKeyCodeTimer(0)
	, mKeyCooldownTimer(0)
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
	, mPoly5Counter(0)
	, mPoly4Counter(0)
	, mSerialInputShiftRegister(0)
	, mSerialOutputShiftRegister(0)
	, mSerialInputCounter(0)
	, mSerialOutputCounter(0)
	, mbSerOutValid(false)
	, mbSerialOutputState(false)
	, mbSpeakerState(false)
	, mbSpeakerActive(false)
	, mbSerialRateChanged(false)
	, mbSerialWaitingForStartBit(true)
	, mbSerInBurstPending(false)
	, mSerBurstMode(kSerialBurstMode_Disabled)
	, mbFastTimer1(false)
	, mbFastTimer3(false)
	, mbLinkedTimers12(false)
	, mbLinkedTimers34(false)
	, mbUse15KHzClock(false)
	, mLast15KHzTime(0)
	, mLast64KHzTime(0)
	, mALLPOT(0)
	, mPotScanStartTime(0)
	, mp64KHzEvent(NULL)
	, mp15KHzEvent(NULL)
	, mpAudioEvent(NULL)
	, mpStartBitEvent(NULL)
	, mpResetTimersEvent(NULL)
	, mpScheduler(NULL)
	, mpConn(NULL)
	, mpSlave(NULL)
	, mbIsSlave(isSlave)
	, mpAudioOut(NULL)
	, mpCassette(NULL)
	, mOutputSampleCount(0)
{
	for(int i=0; i<8; ++i) {
		mpPotScanEvent[i] = NULL;
		mPOT[i] = 228;
	}

	memset(mpTimerEvents, 0, sizeof(mpTimerEvents));
	memset(mpTimerBorrowEvents, 0, sizeof(mpTimerBorrowEvents));

	UpdateMixTable();

	for(int i=0; i<4; ++i)
		mbChannelEnabled[i] = true;
}

ATPokeyEmulator::~ATPokeyEmulator() {
}

void ATPokeyEmulator::Init(IATPokeyEmulatorConnections *mem, ATScheduler *sched, IATAudioOutput *output) {
	mpConn = mem;
	mpScheduler = sched;
	mpAudioOut = output;

	if (!mbIsSlave)
		mpAudioEvent = sched->AddEvent(28, this, kATPokeyEventAudio);
	mLastOutputTime = ATSCHEDULER_GETTIME(mpScheduler);

	VDASSERT(!mp64KHzEvent);
	VDASSERT(!mp15KHzEvent);

	ColdReset();

	// The 4-bit and 5-bit polynomial counters are of the XNOR variety, which means
	// that the all-1s case causes a lockup. The INIT mode shifts zeroes into the
	// register.
	static const uint8 kRevBits4[16]={
		0,  8, 4, 12,
		2, 10, 6, 14,
		1,  9, 5, 13,
		3, 11, 7, 15,
	};

	int poly4 = 0;
	for(int i=0; i<15; ++i) {
		poly4 = (poly4+poly4) + (~((poly4 >> 2) ^ (poly4 >> 3)) & 1);

		mPoly4Buffer[i] = kRevBits4[poly4 & 15];
	}

	int poly5 = 0;
	for(int i=0; i<31; ++i) {
		poly5 = (poly5+poly5) + (~((poly5 >> 2) ^ (poly5 >> 4)) & 1);

		mPoly5Buffer[i] = kRevBits4[poly5 & 15];
	}

	// The 17-bit polynomial counter is also of the XNOR variety, but one big
	// difference is that you're allowed to read out 8 bits of it. The RANDOM
	// register actually reports the INVERTED state of these bits (the Q' output
	// of the flip flops is connected to the data bus). This means that even
	// though we clear the register to 0, it reads as FF.
	//
	// From the perspective of the CPU through RANDOM, the LFSR shifts to the
	// right, and new bits appear on the left. The equivalent operation for the
	// 9-bit LFSR would be to set carry equal to (D0 ^ D5) and then execute
	// a ROR.
	int poly9 = 0;
	for(int i=0; i<511; ++i) {
		// Note: This one is actually verified against a real Atari.
		// At WSYNC time, the pattern goes: 00 DF EE 16 B9....
		poly9 = (poly9 >> 1) + (~((poly9 << 8) ^ (poly9 << 3)) & 0x100);

		mPoly9Buffer[i] = (~poly9 & 0xff);
	}

	// The 17-bit mode inserts an additional 8 register bits immediately after
	// the XNOR. The generator polynomial is unchanged.
	int poly17 = 0;
	for(int i=0; i<131071; ++i) {
		poly17 = (poly17 >> 1) + (~((poly17 << 16) ^ (poly17 << 11)) & 0x10000);

		mPoly17Buffer[i] = ((~poly17 >> 8) & 0xff);
	}
}

void ATPokeyEmulator::ColdReset() {
	memset(mbKeyMatrix, 0, sizeof mbKeyMatrix);
	mKeyScanCode = 0;
	mKeyScanState = 0;

	mKBCODE = 0;
	mSKSTAT = 0xFF;
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

		if (mpTimerEvents[i]) {
			mpScheduler->RemoveEvent(mpTimerEvents[i]);
			mpTimerEvents[i] = NULL;
		}

		if (mpTimerBorrowEvents[i]) {
			mpScheduler->RemoveEvent(mpTimerBorrowEvents[i]);
			mpTimerBorrowEvents[i] = NULL;
		}
	}

	if (mpStartBitEvent) {
		mpScheduler->RemoveEvent(mpStartBitEvent);
		mpStartBitEvent = NULL;
	}

	if (mpResetTimersEvent) {
		mpScheduler->RemoveEvent(mpResetTimersEvent);
		mpResetTimersEvent = NULL;
	}

	mLastPolyTime = ATSCHEDULER_GETTIME(mpScheduler);
	mPoly17Counter = 0;
	mPoly9Counter = 0;
	mPoly5Counter = 0;
	mPoly4Counter = 0;
	mSerialInputShiftRegister = 0;
	mSerialOutputShiftRegister = 0;
	mSerialOutputCounter = 0;
	mSerialInputCounter = 0;
	mbSerOutValid = false;
	mbSerialOutputState = false;
	mbSerialWaitingForStartBit = true;
	mbSerInBurstPending = false;

	memset(mAUDF, 0, sizeof mAUDF);
	memset(mAUDC, 0, sizeof mAUDC);

	for(int i=0; i<4; ++i)
		UnpackAUDCx(i);

	mOutputs[0] = 1;
	mOutputs[1] = 1;
	mOutputs[2] = 1;
	mOutputs[3] = 1;
	mOutputLevel = 0;
	mLast15KHzTime = ATSCHEDULER_GETTIME(mpScheduler);
	mLast64KHzTime = ATSCHEDULER_GETTIME(mpScheduler);

	if (mp64KHzEvent) {
		mpScheduler->RemoveEvent(mp64KHzEvent);
		mp64KHzEvent = NULL;
	}

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

	mNoiseFF[0] = false;
	mNoiseFF[1] = false;
	mNoiseFF[2] = false;
	mNoiseFF[3] = false;

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

void ATPokeyEmulator::ReceiveSIOByte(uint8 c) {
	if (mbTraceSIO)
		ATConsoleTaggedPrintf("POKEY: Receiving byte (c=%02x; %02x %02x)\n", c, mSERIN, mSerialInputShiftRegister);

	// check for overrun
	if (!(mIRQST & 0x20)) {
		mSKSTAT &= 0xbf;

		if (mbTraceSIO)
			ATConsoleTaggedPrintf("POKEY: Serial input overrun detected (c=%02x; %02x %02x)\n", c, mSERIN, mSerialInputShiftRegister);

		mSERIN = mSerialInputShiftRegister;
	}

	if (mSKCTL & 0x10) {
		// Restart timers 3 and 4 immediately.
		mCounter[2] = (uint32)mAUDF[2] + 1;
		mCounter[3] = (uint32)mAUDF[3] + 1;

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

void ATPokeyEmulator::SetAudioLine(int v) {
	mAudioInput = v;
	mExternalInput = mAudioInput + mAudioInput2;
	UpdateOutput();
}

void ATPokeyEmulator::SetAudioLine2(int v) {
	mAudioInput2 = v;
	mExternalInput = mAudioInput + mAudioInput2;
	UpdateOutput();
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

void ATPokeyEmulator::SetChannelEnabled(uint32 channel, bool enabled) {
	VDASSERT(channel < 4);
	if (mbChannelEnabled[channel] != enabled) {
		mbChannelEnabled[channel] = enabled;

		UnpackAUDCx(channel);
		UpdateOutput();
	}
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

	if (!mKeyCodeTimer || !repeat) {
		if ((mIRQEN & 0x40) && (mSKCTL & 2)) {
			// If keyboard IRQ is already active, set the keyboard overrun bit.
			if (!(mIRQST & 0x40))
				mSKSTAT &= ~0x40;

			mIRQST &= ~0x40;
			mpConn->PokeyAssertIRQ(false);
		}
	}

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

void ATPokeyEmulator::FireTimers(uint8 activeChannels) {
	UpdatePolynomialCounters();

	uint8 poly17 = mAUDCTL & 0x80 ? mPoly9Buffer[mPoly9Counter] : mPoly17Buffer[mPoly17Counter];
	uint8 poly5 = mPoly5Buffer[mPoly5Counter];
	uint8 poly4 = mPoly4Buffer[mPoly4Counter];
	bool outputsChanged = false;

	if (activeChannels & 0x01) {
		bool noiseFFInput = (mAUDC[0] & 0x20) ? !mNoiseFF[0] : ((((mAUDC[0] & 0x40) ? poly4 : poly17) & 8) != 0);
		bool noiseFFClock = (mAUDC[0] & 0x80) ? true : ((poly5 & 8) != 0);

		if (noiseFFClock)
			mNoiseFF[0] = noiseFFInput;

		mOutputs[0] = mNoiseFF[0];
		if (mAUDCTL & 0x04)
			mOutputs[0] ^= mHighPassFF[0] ? 0x01 : 0x00;
		else
			mOutputs[0] ^= 0x01;

		if (mChannelVolume[0])
			outputsChanged = true;

		if (mIRQEN & 0x01) {
			mIRQST &= ~0x01;
			mpConn->PokeyAssertIRQ(false);
		}

		// two tone
		if ((mSKCTL & 8) && mbSerialOutputState && !(mSKCTL & 0x80)) {
			// resync timer 2 to timer 1
			mCounter[1] = (uint32)mAUDF[1] + 1;
			SetupTimers(0x02);
		}
	}

	// count timer 2
	if (activeChannels & 0x02) {
		bool noiseFFInput = (mAUDC[1] & 0x20) ? !mNoiseFF[1] : ((((mAUDC[1] & 0x40) ? poly4 : poly17) & 4) != 0);
		bool noiseFFClock = (mAUDC[1] & 0x80) ? true : ((poly5 & 4) != 0);

		if (noiseFFClock)
			mNoiseFF[1] = noiseFFInput;

		mOutputs[1] = mNoiseFF[1];
		if (mAUDCTL & 0x02)
			mOutputs[1] ^= mHighPassFF[1] ? 0x01 : 0x00;
		else
			mOutputs[1] ^= 0x01;

		if (mChannelVolume[1])
			outputsChanged = true;

		if (mIRQEN & 0x02) {
			mIRQST &= ~0x02;
			mpConn->PokeyAssertIRQ(false);
		}

		// two tone
		if (mSKCTL & 8) {
			// resync timer  to timer 2
			mCounter[0] = (uint32)mAUDF[0] + 1;
			SetupTimers(0x01);
		}

		if (mSerialOutputCounter) {
			if ((mSKCTL & 0x60) == 0x60)
				OnSerialOutputTick(false);
		}
	}

	// count timer 3
	if (activeChannels & 0x04) {
		bool noiseFFInput = (mAUDC[2] & 0x20) ? !mNoiseFF[2] : ((((mAUDC[2] & 0x40) ? poly4 : poly17) & 2) != 0);
		bool noiseFFClock = (mAUDC[2] & 0x80) ? true : ((poly5 & 2) != 0);

		if (noiseFFClock)
			mNoiseFF[2] = noiseFFInput;

		mOutputs[2] = mNoiseFF[2];

		mHighPassFF[0] = mNoiseFF[0];
		if (mAUDCTL & 0x04) {
			mOutputs[0] = mNoiseFF[0] ^ mHighPassFF[0];
			if (mChannelVolume[0])
				outputsChanged = true;
		}

		if (mChannelVolume[2])
			outputsChanged = true;
	}

	// count timer 4
	if (activeChannels & 0x08) {
		bool noiseFFInput = (mAUDC[3] & 0x20) ? !mNoiseFF[3] : ((((mAUDC[3] & 0x40) ? poly4 : poly17) & 1) != 0);
		bool noiseFFClock = (mAUDC[3] & 0x80) ? true : ((poly5 & 1) != 0);

		if (noiseFFClock)
			mNoiseFF[3] = noiseFFInput;

		mOutputs[3] = mNoiseFF[3];

		mHighPassFF[1] = mNoiseFF[1];
		if (mAUDCTL & 0x02) {
			mOutputs[1] = mNoiseFF[1] ^ mHighPassFF[1];
			if (mChannelVolume[1])
				outputsChanged = true;
		}

		if (mChannelVolume[3])
			outputsChanged = true;

		if (mIRQEN & 0x04) {
			mIRQST &= ~0x04;
			mpConn->PokeyAssertIRQ(false);
		}

		if (mSerialInputCounter) {
			--mSerialInputCounter;

			if (!mSerialInputCounter && (mSKCTL & 0x10)) {
				mbSerialWaitingForStartBit = true;
			}
		}

		if (mSerialOutputCounter) {
			switch(mSKCTL & 0x60) {
				case 0x20:
				case 0x40:
					OnSerialOutputTick(false);
					break;
			}
		}
	}

	if (outputsChanged)
		UpdateOutput();
}

void ATPokeyEmulator::OnSerialOutputTick(bool cpuBased) {
	--mSerialOutputCounter;

	// We've already transmitted the start bit (low), so we need to do data bits and then
	// stop bit (high).
	mbSerialOutputState = mSerialOutputCounter ? (mSerialOutputShiftRegister & (1 << (9 - (mSerialOutputCounter >> 1)))) != 0 : true;

	if (!mSerialOutputCounter) {
		if (mbTraceSIO)
			ATConsoleTaggedPrintf("POKEY: Transmitted serial byte %02x to SIO bus\n", mSerialOutputShiftRegister);

		for(Devices::const_iterator it(mDevices.begin()), itEnd(mDevices.end()); it!=itEnd; ++it)
			(*it)->PokeyWriteSIO(mSerialOutputShiftRegister);

		if (mbSerOutValid) {
			mSerialOutputCounter = 20;
			mbSerialOutputState = true;
			mSerialOutputShiftRegister = mSEROUT;
			mbSerOutValid = false;
		}

		if (mIRQEN & 0x10) {
			mIRQST &= ~0x10;
			mpConn->PokeyAssertIRQ(cpuBased);
		}

		// bit 3 is special and doesn't get cleared by IRQEN
		if (!mSerialOutputCounter) {
			mIRQST &= ~0x08;
			if (mIRQEN & 0x08) {
				mpConn->PokeyAssertIRQ(cpuBased);
			}
		}
	}

	if (!mbSerOutValid) {
		if (mIRQEN & 0x10) {
			mIRQST &= ~0x10;
			mpConn->PokeyAssertIRQ(cpuBased);
		}
	}
}

void ATPokeyEmulator::AdvanceScanLine() {
	if (mbSerialRateChanged) {
		mbSerialRateChanged = false;

		if (mpCassette) {
			uint32 divisor;
			if (mbLinkedTimers34) {
				divisor = ((uint32)mAUDF[3] << 8) + mAUDF[2] + 1;
				if (mbFastTimer3) {
					divisor += 6;
				} else if (mbUse15KHzClock) {
					divisor *= 114;
				} else {
					divisor *= 28;
				}

			} else if (mbUse15KHzClock) {
				// 15KHz clock
				divisor = ((uint32)mAUDF[3] + 1) * 114;
			} else {
				// 64KHz clock
				divisor = ((uint32)mAUDF[3] + 1) * 28;
			}

			mpCassette->PokeyChangeSerialRate(divisor);
		}
	}
}

void ATPokeyEmulator::AdvanceFrame(bool pushAudio) {
	if (mKeyCodeTimer) {
		if (!--mKeyCodeTimer) {
			mSKSTAT |= 0x04;

			mKeyCooldownTimer = 20;
		}
	} else if (mbSpeakerActive) {
		mKeyCooldownTimer = 20;
	} else {
		if (mKeyCooldownTimer)
			--mKeyCooldownTimer;

		if ((mIRQST & mIRQEN & 0x40) && !mKeyQueue.empty() && ((mbUseKeyCooldownTimer && !mKeyCooldownTimer) || mpConn->PokeyIsKeyPushOK(mKeyQueue.front())))
			TryPushNextKey();
	}

	mbSpeakerActive = false;

	FlushAudio(pushAudio);
}

void ATPokeyEmulator::OnScheduledEvent(uint32 id) {
	switch(id) {
		case kATPokeyEventAudio:
			{
				mpAudioEvent = mpScheduler->AddEvent(28, this, kATPokeyEventAudio);

				uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
				GenerateSample(mOutputSampleCount, t);

				if (mpSlave)
					mpSlave->GenerateSample(mOutputSampleCount, t);

				if (++mOutputSampleCount >= kBufferSize)
					mOutputSampleCount = kBufferSize - 1;
			}
			break;

		case kATPokeyEvent64KHzTick:
			{
				mp64KHzEvent = mpScheduler->AddEvent(28, this, kATPokeyEvent64KHzTick);

				uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
				mLast64KHzTime = t;
			}
			break;

		case kATPokeyEvent15KHzTick:
			{
				mp15KHzEvent = mpScheduler->AddEvent(114, this, kATPokeyEvent15KHzTick);

				uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
				mLast15KHzTime = t;

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

		case kATPokeyEventTimer1:
			mpTimerEvents[0] = NULL;
			mCounter[0] = (uint32)mAUDF[0] + 1;
			if (mbLinkedTimers12) {
				mCounter[0] = 256;
				SetupTimers(0x01);
			}

			mpScheduler->SetEvent(3, this, kATPokeyEventTimer1Borrow, mpTimerBorrowEvents[0]);
			break;

		case kATPokeyEventTimer1Borrow:
			mpTimerBorrowEvents[0] = NULL;
			FireTimers(0x01);
			if (!mbLinkedTimers12) {
				SetupTimers(0x01);
				break;
			}

			if (--mCounter[1])
				break;

			if (mpTimerEvents[1])
				mpScheduler->RemoveEvent(mpTimerEvents[1]);
			// fall through
		case kATPokeyEventTimer2:
			mpTimerEvents[1] = NULL;
			mCounter[1] = (uint32)mAUDF[1] + 1;

			if (mbLinkedTimers12)
				mCounter[0] = (uint32)mAUDF[0] + 1;

			mpScheduler->SetEvent(3, this, kATPokeyEventTimer2Borrow, mpTimerBorrowEvents[1]);
			break;

		case kATPokeyEventTimer2Borrow:
			mpTimerBorrowEvents[1] = NULL;
			FireTimers(0x02);
			if (mbLinkedTimers12)
				SetupTimers(0x03);
			else
				SetupTimers(0x02);
			break;

		case kATPokeyEventTimer3:
			mpTimerEvents[2] = NULL;
			mCounter[2] = (uint32)mAUDF[2] + 1;
			if (mbLinkedTimers34) {
				mCounter[2] = 256;
				SetupTimers(0x04);
			}

			mpScheduler->SetEvent(3, this, kATPokeyEventTimer3Borrow, mpTimerBorrowEvents[2]);
			break;

		case kATPokeyEventTimer3Borrow:
			mpTimerBorrowEvents[2] = NULL;
			FireTimers(0x04);
			if (!mbLinkedTimers34) {
				SetupTimers(0x04);
				break;
			}

			if (--mCounter[3])
				break;

			if (mpTimerEvents[3])
				mpScheduler->RemoveEvent(mpTimerEvents[3]);
			// fall through
		case kATPokeyEventTimer4:
			mpTimerEvents[3] = NULL;
			mCounter[3] = (uint32)mAUDF[3] + 1;
			if (mbLinkedTimers34)
				mCounter[2] = (uint32)mAUDF[2] + 1;

			mpScheduler->SetEvent(3, this, kATPokeyEventTimer4Borrow, mpTimerBorrowEvents[3]);
			break;

		case kATPokeyEventTimer4Borrow:
			mpTimerBorrowEvents[3] = NULL;
			FireTimers(0x08);
			if (mbLinkedTimers34)
				SetupTimers(0x0C);
			else
				SetupTimers(0x08);
			break;

		case kATPokeyEventResetTimers:
			mpResetTimersEvent = NULL;
			mCounter[0] = (uint32)mAUDF[0] + 1;
			mCounter[1] = (uint32)mAUDF[1] + 1;
			mCounter[2] = (uint32)mAUDF[2] + 1;
			mCounter[3] = (uint32)mAUDF[3] + 1;
			mOutputs[0] = 0;
			mOutputs[1] = 0;
			mOutputs[2] = 1;
			mOutputs[3] = 1;
			mNoiseFF[0] = 1;
			mNoiseFF[1] = 1;
			mNoiseFF[2] = 1;
			mNoiseFF[3] = 1;

			for(int i=0; i<4; ++i) {
				if (mpTimerBorrowEvents[i]) {
					mpScheduler->RemoveEvent(mpTimerBorrowEvents[i]);
					mpTimerBorrowEvents[i] = NULL;
				}
			}

			UpdateOutput();
			SetupTimers(0x0f);
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

void ATPokeyEmulator::GenerateSample(uint32 pos, uint32 t) {
	int oc = t - mLastOutputTime;
	mAccum += mOutputLevel * oc;
	mLastOutputTime = t;

	float v = mAccum;

	mAccum = 0;
	mRawOutputBuffer[pos] = (float)v;
}

void ATPokeyEmulator::UpdatePolynomialCounters() const {
	if (!(mSKCTL & 3))
		return;

	int t = ATSCHEDULER_GETTIME(mpScheduler);
	int polyDelta = t - mLastPolyTime;
	mPoly4Counter += polyDelta;
	mPoly5Counter += polyDelta;
	mPoly9Counter += polyDelta;
	mPoly17Counter += polyDelta;
	mLastPolyTime = t;

	if (mPoly4Counter >= 15)
		mPoly4Counter %= 15;

	if (mPoly5Counter >= 31)
		mPoly5Counter %= 31;

	if (mPoly9Counter >= 511)
		mPoly9Counter %= 511;

	if (mPoly17Counter >= 131071)
		mPoly17Counter %= 131071;
}

void ATPokeyEmulator::UpdateOutput() {
	int t = ATSCHEDULER_GETTIME(mpScheduler);
	int oc = t - mLastOutputTime;
	mAccum += mOutputLevel * oc;
	mLastOutputTime = t;

	int out0 = mOutputs[0];
	int out1 = mOutputs[1];
	int out2 = mOutputs[2];
	int out3 = mOutputs[3];

	int v0 = mChannelVolume[0];
	int v1 = mChannelVolume[1];
	int v2 = mChannelVolume[2];
	int v3 = mChannelVolume[3];
	int vpok	= ((mAUDC[0] & 0x10) ? v0 : out0 * v0)
				+ ((mAUDC[1] & 0x10) ? v1 : out1 * v1)
				+ ((mAUDC[2] & 0x10) ? v2 : out2 * v2)
				+ ((mAUDC[3] & 0x10) ? v3 : out3 * v3);

	mOutputLevel	= mMixTable[vpok] 
					+ mExternalInput
					+ (mbSpeakerState ? +24 : 0);		// The XL speaker is approximately 80% as loud as a full volume channel.
					;
}

void ATPokeyEmulator::UpdateTimerCounters(uint8 channels) {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	int slowTickOffset = (mbUse15KHzClock ? t - mLast15KHzTime : t - mLast64KHzTime);

	if (channels & 0x01) {
		if (mpTimerEvents[0]) {
			int ticksLeft = mpScheduler->GetTicksToEvent(mpTimerEvents[0]);

			if (mbFastTimer1) {
				mCounter[0] = ticksLeft;
			} else if (mbUse15KHzClock)
				mCounter[0] = (ticksLeft + slowTickOffset) / 114;
			else
				mCounter[0] = (ticksLeft + slowTickOffset) / 28;

			VDASSERT(mCounter[0] > 0);
		}
	}

	if (channels & 0x02) {
		if (mpTimerEvents[1]) {
			int ticksLeft = mpScheduler->GetTicksToEvent(mpTimerEvents[1]);

			if (mbUse15KHzClock)
				mCounter[1] = (ticksLeft + slowTickOffset) / 114;
			else
				mCounter[1] = (ticksLeft + slowTickOffset) / 28;
		}
	}

	if (channels & 0x04) {
		if (mpTimerEvents[2]) {
			int ticksLeft = mpScheduler->GetTicksToEvent(mpTimerEvents[2]);

			if (mbFastTimer3) {
				mCounter[2] = ticksLeft;
			} else if (mbUse15KHzClock)
				mCounter[2] = (ticksLeft + slowTickOffset) / 114;
			else
				mCounter[2] = (ticksLeft + slowTickOffset) / 28;
		}
	}

	if (channels & 0x08) {
		if (mpTimerEvents[3]) {
			int ticksLeft = mpScheduler->GetTicksToEvent(mpTimerEvents[3]);

			if (mbUse15KHzClock)
				mCounter[3] = (ticksLeft + slowTickOffset) / 114;
			else
				mCounter[3] = (ticksLeft + slowTickOffset) / 28;
		}
	}
}

void ATPokeyEmulator::SetupTimers(uint8 channels) {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	int cyclesToNextSlowTick = (mbUse15KHzClock ? 114 - (t - mLast15KHzTime) : 28 - (t - mLast64KHzTime));

	if (cyclesToNextSlowTick)
		cyclesToNextSlowTick -= (mbUse15KHzClock ? 114 : 28);

	bool slowTickValid = (mSKCTL & 3) != 0;

	if (channels & 0x01) {
		if (mpTimerEvents[0]) {
			mpScheduler->RemoveEvent(mpTimerEvents[0]);
			mpTimerEvents[0] = NULL;
		}

		if (mbFastTimer1 || slowTickValid) {
			uint32 ticks;
			if (mbLinkedTimers12) {
				if (mbFastTimer1) {
					ticks = mCounter[0];
				} else if (mbUse15KHzClock)
					ticks = mCounter[0] * 114 + cyclesToNextSlowTick;
				else
					ticks = mCounter[0] * 28 + cyclesToNextSlowTick;
			} else {
				if (mbFastTimer1) {
					ticks = mCounter[0];
				} else if (mbUse15KHzClock)
					ticks = mCounter[0] * 114 + cyclesToNextSlowTick;
				else
					ticks = mCounter[0] * 28 + cyclesToNextSlowTick;
			}

			VDASSERT(ticks > 0);
			mpTimerEvents[0] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer1);
		}
	}

	if (channels & 0x02) {
		if (mpTimerEvents[1]) {
			mpScheduler->RemoveEvent(mpTimerEvents[1]);
			mpTimerEvents[1] = NULL;
		}

		uint32 ticks;
		if (!mbLinkedTimers12 && slowTickValid) {
			if (mbUse15KHzClock)
				ticks = mCounter[1] * 114 + cyclesToNextSlowTick;
			else
				ticks = mCounter[1] * 28 + cyclesToNextSlowTick;

			VDASSERT(ticks > 0);
			mpTimerEvents[1] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer2);
		}
	}

	if (channels & 0x04) {
		if (mpTimerEvents[2]) {
			mpScheduler->RemoveEvent(mpTimerEvents[2]);
			mpTimerEvents[2] = NULL;
		}

		if (mbFastTimer3 || slowTickValid) {
			if (!(mSKCTL & 0x10) || !mbSerialWaitingForStartBit) {
				uint32 ticks;
				if (mbLinkedTimers34) {
					if (mbFastTimer3) {
						ticks = mCounter[2];
					} else if (mbUse15KHzClock)
						ticks = mCounter[2] * 114 + cyclesToNextSlowTick;
					else
						ticks = mCounter[2] * 28 + cyclesToNextSlowTick;
				} else {
					if (mbFastTimer3) {
						ticks = mCounter[2];
					} else if (mbUse15KHzClock)
						ticks = mCounter[2] * 114 + cyclesToNextSlowTick;
					else
						ticks = mCounter[2] * 28 + cyclesToNextSlowTick;
				}

				VDASSERT(ticks > 0);
				mpTimerEvents[2] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer3);
			}
		}
	}

	if (channels & 0x08) {
		if (mpTimerEvents[3]) {
			mpScheduler->RemoveEvent(mpTimerEvents[3]);
			mpTimerEvents[3] = NULL;
		}

		if (!(mSKCTL & 0x10) || !mbSerialWaitingForStartBit) {
			uint32 ticks;
			if (!mbLinkedTimers34 && slowTickValid) {
				if (mbUse15KHzClock)
					ticks = mCounter[3] * 114 + cyclesToNextSlowTick;
				else
					ticks = mCounter[3] * 28 + cyclesToNextSlowTick;

				VDASSERT(ticks > 0);
				mpTimerEvents[3] = mpScheduler->AddEvent(ticks, this, kATPokeyEventTimer4);
			}
		}
	}
}

uint8 ATPokeyEmulator::DebugReadByte(uint8 reg) const {
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
			UpdatePolynomialCounters();
			return mAUDCTL & 0x80 ? (uint8)(mPoly9Buffer[mPoly9Counter]) : (uint8)(mPoly17Buffer[mPoly17Counter]);
		case 0x0D:	// $D20D SERIN
			return mSERIN;
		case 0x0E:
			return mIRQST;
		case 0x0F:
			return mSKSTAT;
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
			UpdatePolynomialCounters();
			return mAUDCTL & 0x80 ? (uint8)(mPoly9Buffer[mPoly9Counter]) : (uint8)(mPoly17Buffer[mPoly17Counter]);
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
			return mSKSTAT;
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
	switch(reg) {
		case 0x00:	// $D200 AUDF1
			mAUDF[0] = value;
			break;
		case 0x01:	// $D201 AUDC1
			if (mAUDC[0] != value) {
				mAUDC[0] = value;
				UnpackAUDCx(0);
				UpdateOutput();
			}
			break;
		case 0x02:	// $D202 AUDF2
			mAUDF[1] = value;
			break;
		case 0x03:	// $D203 AUDC2
			if (mAUDC[1] != value) {
				mAUDC[1] = value;
				UnpackAUDCx(1);
				UpdateOutput();
			}
			break;
		case 0x04:	// $D204 AUDF3
			mAUDF[2] = value;
			mbSerialRateChanged = true;
			break;
		case 0x05:	// $D205 AUDC3
			if (mAUDC[2] != value) {
				mAUDC[2] = value;
				UnpackAUDCx(2);
				UpdateOutput();
			}
			break;
		case 0x06:	// $D206 AUDF4
			mAUDF[3] = value;
			mbSerialRateChanged = true;
			break;
		case 0x07:	// $D207 AUDC4
			if (mAUDC[3] != value) {
				mAUDC[3] = value;
				UnpackAUDCx(3);
				UpdateOutput();
			}
			break;
		case 0x08:	// $D208 AUDCTL
			if (mAUDCTL != value) {
				if ((mAUDCTL ^ value) & 0x29)
					mbSerialRateChanged = true;

				UpdateTimerCounters(0x0f);
				mAUDCTL = value;
				mbFastTimer1 = (mAUDCTL & 0x40) != 0;
				mbFastTimer3 = (mAUDCTL & 0x20) != 0;
				mbLinkedTimers12 = (mAUDCTL & 0x10) != 0;
				mbLinkedTimers34 = (mAUDCTL & 0x08) != 0;
				mbUse15KHzClock = (mAUDCTL & 0x01) != 0;

				bool outputsChanged = false;
				if (!(mAUDCTL & 0x04) && !mHighPassFF[0]) {
					mHighPassFF[0] = true;
					mOutputs[0] = mNoiseFF[0] ^ 0x01;
					if (mChannelVolume[0])
						outputsChanged = true;
				}

				if (!(mAUDCTL & 0x02) && !mHighPassFF[1]) {
					mHighPassFF[1] = true;
					mOutputs[1] = mNoiseFF[1] ^ 0x01;
					if (mChannelVolume[1])
						outputsChanged = true;
				}

				if (outputsChanged)
					UpdateOutput();

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

			mSEROUT = value;
			if (!mSerialOutputCounter) {
				mSerialOutputShiftRegister = value;
				mbSerialOutputState = false;
				mSerialOutputCounter = 20;
				mIRQST |= 0x08;

				if (!(mIRQEN & ~mIRQST))
					mpConn->PokeyNegateIRQ(true);
			} else if (!mbSerOutValid) {
				mbSerOutValid = true;
				mIRQST |= 0x18;

				if (!(mIRQEN & ~mIRQST))
					mpConn->PokeyNegateIRQ(true);
			} else {
				if (mbTraceSIO)
					ATConsoleTaggedPrintf("POKEY: Serial output overrun detected.\n");
			}
			break;
		case 0x0E:
			{
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
			}
			break;
		case 0x0F:
			if (value != mSKCTL) {
				UpdateTimerCounters(0x0f);

				bool prvInit = (mSKCTL & 3) == 0;
				bool newInit = (value & 3) == 0;

				if (newInit != prvInit) {
					if (newInit) {
						if (mp64KHzEvent) {
							mpScheduler->RemoveEvent(mp64KHzEvent);
							mp64KHzEvent = NULL;
						}

						if (mp15KHzEvent) {
							mpScheduler->RemoveEvent(mp15KHzEvent);
							mp15KHzEvent = NULL;
						}
					} else {
						VDASSERT(!mp64KHzEvent);
						VDASSERT(!mp15KHzEvent);

						// The 64KHz polynomial counter is a 5-bit LFSR that is reset to all 1s
						// on init and XORs bits 4 and 2, if seen as shifting left. In order to
						// achieve a 28 cycle period, a one is force fed when the register
						// equals 00010. A side effect of this mechanism is that resetting the
						// register actually starts it 19 cycles in. There is, however, an
						// additional two clock delay from the shift register comparator to
						// the timers.
						mp64KHzEvent = mpScheduler->AddEvent(22, this, kATPokeyEvent64KHzTick);

						// The 15KHz polynomial counter is a 7-bit LFSR that is reset to all 0s
						// on init and XNORs bits 7 and 6, if seen as shifting left. In order to
						// achieve a 114 cycle period, a one is force fed when the register
						// equals XXXXXX. [PROBLEM: LFSR in schematic doesn't appear to work!]
						mp15KHzEvent = mpScheduler->AddEvent(81, this, kATPokeyEvent15KHzTick);

						mLast15KHzTime = ATSCHEDULER_GETTIME(mpScheduler) + 81 - 114;
						mLast64KHzTime = ATSCHEDULER_GETTIME(mpScheduler) + 22 - 28;
					}

					mPoly4Counter = 0;
					mPoly5Counter = 0;
					mPoly9Counter = 0;
					mPoly17Counter = 0;
					mLastPolyTime = ATSCHEDULER_GETTIME(mpScheduler) + 1;

					mSerialInputShiftRegister = 0;
					mSerialOutputShiftRegister = 0;
					mSerialOutputCounter = 0;
					mSerialInputCounter = 0;
					mbSerOutValid = false;
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
				SetupTimers(0x0f);
			}
			break;
		default:
//			__debugbreak();
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
		DumpStatus(true);
	} else {
		DumpStatus(false);
	}
}

void ATPokeyEmulator::LoadState(ATSaveStateReader& reader) {
	mAUDF[0] = reader.ReadUint8();
	mAUDF[1] = reader.ReadUint8();
	mAUDF[2] = reader.ReadUint8();
	mAUDF[3] = reader.ReadUint8();
	mAUDC[0] = reader.ReadUint8();
	mAUDC[1] = reader.ReadUint8();
	mAUDC[2] = reader.ReadUint8();
	mAUDC[3] = reader.ReadUint8();
	mAUDCTL = reader.ReadUint8();
	mCounter[0] = reader.ReadUint32();
	mCounter[1] = reader.ReadUint32();
	mCounter[2] = reader.ReadUint32();
	mCounter[3] = reader.ReadUint32();
	mALLPOT = reader.ReadUint8();
	mIRQEN = reader.ReadUint8();
	mIRQST = reader.ReadUint8();
	mSKCTL = reader.ReadUint8();

	mbFastTimer1 = (mAUDCTL & 0x40) != 0;
	mbFastTimer3 = (mAUDCTL & 0x20) != 0;
	mbLinkedTimers12 = (mAUDCTL & 0x10) != 0;
	mbLinkedTimers34 = (mAUDCTL & 0x08) != 0;
	mbUse15KHzClock = (mAUDCTL & 0x01) != 0;

	for(int i=0; i<4; ++i)
		UnpackAUDCx(i);

	UpdateOutput();
	SetupTimers(0x0f);
}

void ATPokeyEmulator::SaveState(ATSaveStateWriter& writer) {
	UpdateTimerCounters(0x0f);

	writer.WriteUint8(mAUDF[0]);
	writer.WriteUint8(mAUDF[1]);
	writer.WriteUint8(mAUDF[2]);
	writer.WriteUint8(mAUDF[3]);
	writer.WriteUint8(mAUDC[0]);
	writer.WriteUint8(mAUDC[1]);
	writer.WriteUint8(mAUDC[2]);
	writer.WriteUint8(mAUDC[3]);
	writer.WriteUint8(mAUDCTL);
	writer.WriteUint32(mCounter[0]);
	writer.WriteUint32(mCounter[1]);
	writer.WriteUint32(mCounter[2]);
	writer.WriteUint32(mCounter[3]);
	writer.WriteUint8(mALLPOT);
	writer.WriteUint8(mIRQEN);
	writer.WriteUint8(mIRQST);
	writer.WriteUint8(mSKCTL);
}

void ATPokeyEmulator::DumpStatus(bool isSlave) {
	for(int i=0; i<4; ++i) {
		if (mpTimerEvents[i])
			ATConsolePrintf("AUDF%u: %02x  AUDC%u: %02x  Output: %d  (%u cycles until fire)\n", i+1, mAUDF[i], i+1, mAUDC[i], mOutputs[i], (int)mpScheduler->GetTicksToEvent(mpTimerEvents[i]));
		else
			ATConsolePrintf("AUDF%u: %02x  AUDC%u: %02x  Output: %d\n", i+1, mAUDF[i], i+1, mAUDC[i], mOutputs[i]);
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
	if (mpAudioOut) {
		uint32 timestamp = mpConn->PokeyGetTimestamp() - 28 * mOutputSampleCount;
		mpAudioOut->WriteAudio(mRawOutputBuffer, mpSlave ? mpSlave->mRawOutputBuffer : NULL, mOutputSampleCount, pushAudio, timestamp);
	}

	mOutputSampleCount = 0;
}

void ATPokeyEmulator::UpdateMixTable() {
	if (mbNonlinearMixingEnabled) {
		for(int i=0; i<61; ++i) {
			float x = (float)i / 60.0f;
			float y = ((1.30f*x - 3.3f)*x + 3.0f)*x;

			mMixTable[i] = y * 60.0f;
		}
	} else {
		for(int i=0; i<61; ++i)
			mMixTable[i] = (float)i;
	}

	if (mpSlave)
		memcpy(mpSlave->mMixTable, mMixTable, sizeof mMixTable);
}

void ATPokeyEmulator::UnpackAUDCx(int index) {
	mChannelVolume[index] = mbChannelEnabled[index] ? mAUDC[index] & 15 : 0;
}

void ATPokeyEmulator::TryPushNextKey() {
	uint8 c = mKeyQueue.front();
	mKeyQueue.pop_front();

	PushKey(c, false, false, false, mbUseKeyCooldownTimer);
}