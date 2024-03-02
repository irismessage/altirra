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
#include <vd2/Riza/audioout.h>
#include <vd2/system/math.h>

#include <windows.h>
#include <mmsystem.h>
#include <math.h>

#include "console.h"
#include "cpu.h"
#include "savestate.h"

namespace {
	enum {
		kATPokeyEvent64KHzTick = 1,
		kATPokeyEvent15KHzTick = 2,
		kATPokeyEventTimer1 = 3,
		kATPokeyEventTimer2 = 4,
		kATPokeyEventTimer3 = 5,
		kATPokeyEventTimer4 = 6,
		kATPokeyEventPot0ScanComplete = 7,	// x8
		kATPokeyEventAudio = 16
	};
}

ATAudioFilter::ATAudioFilter()
	: mHiPassAccum(0)
	, mLoPassPrev1(0)
	, mLoPassPrev2(0)
	, mLoPassPrev3(0)
	, mLoPassPrev4(0)
{
}

void ATAudioFilter::Filter(float * VDRESTRICT dst, const float * VDRESTRICT src, uint32 count, float hiCoeff) {
	float hiAccum = mHiPassAccum;

	// Coefficients for an order 4 Chebyshev filter, cutoff @ 8000Hz, 2dB passband ripple
	const float a0 = 0.003094076;
	const float a1 = 0.012376304;
	const float a2 = 0.018564455;
	const float a3 = 0.012376304;
	const float a4 = 0.003094076;
	const float b0 = 1;
	const float b1 = -2.891038;
	const float b2 = 3.6045542;
	const float b3 = -2.222399;
	const float b4 = 0.57120585;

	float y1 = mLoPassPrev1;
	float y2 = mLoPassPrev2;
	float y3 = mLoPassPrev3;
	float y4 = mLoPassPrev4;

	// validate all coefficients and reset the IIR filter if any are bad
	if (_isnan(y1) || _isnan(y2) || _isnan(y3) || _isnan(y4)) {
		y1 = 0;
		y2 = 0;
		y3 = 0;
		y4 = 0;
	}

	// We accumulate 28 cycles worth of output per sample, and each output can
	// be from 0-60 (4*15). We ignore the speaker and cassette inputs in order
	// to get a little more range.
	const float scale = 1.0f / (60.0f * 28.0f);
	do {
		float v = a0*src[0] + a1*src[-1] + a2*src[-2] + a3*src[-3] + a4*src[-4]
							- b1*y1 - b2*y2 - b3*y3 - b4*y4;

		++src;

		y4 = y3;
		y3 = y2;
		y2 = y1;
		y1 = v;

		v -= hiAccum;
		hiAccum += v * hiCoeff;

		*dst++ = v * scale;
	} while(--count);

	mHiPassAccum = hiAccum;
	mLoPassPrev1 = y1;
	mLoPassPrev2 = y2;
	mLoPassPrev3 = y3;
	mLoPassPrev4 = y4;
}

ATPokeyEmulator::ATPokeyEmulator(bool isSlave)
//	: mpAudioOut(VDCreateAudioOutputDirectSoundW32())
	: mpAudioOut(isSlave ? NULL : VDCreateAudioOutputWaveOutW32())
	, mbPal(false)
	, mbTurbo(false)
	, mbTraceSIO(false)
	, mbSerialRateChanged(false)
	, mbSerialWaitingForStartBit(true)
	, mp64KHzEvent(NULL)
	, mp15KHzEvent(NULL)
	, mpStartBitEvent(NULL)
	, mpAudioEvent(NULL)
	, mpSlave(NULL)
	, mbIsSlave(isSlave)
	, mpCassette(NULL)
	, mpAudioTap(NULL)
{
	if (mpAudioOut) {
		WAVEFORMATEX wfex;
		wfex.wFormatTag = WAVE_FORMAT_PCM;
		wfex.nChannels = 2;
		wfex.nAvgBytesPerSec = 44100;
		wfex.nSamplesPerSec = 22050;
		wfex.nBlockAlign = 4;
		wfex.wBitsPerSample = 16;
		mpAudioOut->Init(kBlockSize, kBlockCount, &wfex, NULL);
		mpAudioOut->Start();
	}

	for(int i=0; i<8; ++i) {
		mpPotScanEvent[i] = NULL;
		mPOT[i] = 228;
	}

	mAccum = 0;
	mbSpeakerState = false;
	mbSerialOutputState = false;
	mExternalInput = 0;
	mAudioInput = 0;
	mAudioInput2 = 0;

	memset(mpTimerEvents, 0, sizeof(mpTimerEvents));

	ResamplerReset();

	for(int i=0; i<61; ++i) {
		float x = (float)i / 60.0f;
		float y = ((1.184256f*x - 3.16932f)*x + 2.98506f)*x;

//		mMixTable[i] = y * 63.0f;
		mMixTable[i] = y * 60.0f;
	}
}

ATPokeyEmulator::~ATPokeyEmulator() {
}

void ATPokeyEmulator::Init(IATPokeyEmulatorConnections *mem, ATScheduler *sched) {
	mpConn = mem;
	mpScheduler = sched;

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

		mPoly17Buffer[i] = (~poly17 & 0xff);
	}
}

void ATPokeyEmulator::ColdReset() {
	mKBCODE = 0;
	mSKSTAT = 0xFF;
	mSKCTL = 0;
	mKeyCodeTimer = 0;
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
	}

	if (mpStartBitEvent) {
		mpScheduler->RemoveEvent(mpStartBitEvent);
		mpStartBitEvent = NULL;
	}

	mLastPolyTime = ATSCHEDULER_GETTIME(mpScheduler);
	mPoly17Counter = 0;
	mPoly9Counter = 0;
	mPoly5Counter = 0;
	mPoly4Counter = 0;
	mSerialInputShiftRegister = 0;
	mbSerInValid = false;
	mbSerShiftInValid = false;
	mSerialOutputShiftRegister = 0;
	mSerialOutputCounter = 0;
	mSerialInputCounter = 0;
	mbSerOutValid = false;
	mbSerialOutputState = false;
	mbSerialWaitingForStartBit = true;

	mAudioDampedError = 0;

	memset(mAUDF, 0, sizeof mAUDF);
	memset(mAUDC, 0, sizeof mAUDC);

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
}

void ATPokeyEmulator::SetCassette(IATPokeyCassetteDevice *dev) {
	mpCassette = dev;
	mbSerialRateChanged = true;
}

void ATPokeyEmulator::SetAudioTap(IATPokeyAudioTap *tap) {
	mpAudioTap = tap;
}

void ATPokeyEmulator::AddSIODevice(IATPokeySIODevice *device) {
	mDevices.push_back(device);
	device->PokeyAttachDevice(this);
}

void ATPokeyEmulator::ReceiveSIOByte(uint8 c) {
	if (mbTraceSIO)
		ATConsoleTaggedPrintf("POKEY: Receiving byte (c=%02x; %02x %02x)\n", c, mSERIN, mSerialInputShiftRegister);

	// check for overrun
	if (mbSerInValid) {
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
	mbSerInValid = true;
	mSERIN = c;

	if (mbTraceSIO)
		ATConsoleTaggedPrintf("POKEY: Reasserting serial input IRQ. IRQEN=%02x, IRQST=%02x\n", mIRQEN, mIRQST);

	if (mIRQEN & 0x20) {
		mIRQST &= ~0x20;
		mpConn->PokeyAssertIRQ();
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

void ATPokeyEmulator::SetShiftKeyState(bool newState) {
	if (newState)
		mSKSTAT &= ~0x08;
	else
		mSKSTAT |= 0x08;
}

void ATPokeyEmulator::PushKey(uint8 c, bool repeat) {
	mKBCODE = c;
	mSKSTAT &= ~0x04;

	if (!mKeyCodeTimer || !repeat) {
		if (mIRQEN & 0x40) {
			mIRQST &= ~0x40;
			mpConn->PokeyAssertIRQ();
		}
	}

	mKeyCodeTimer = 4;
}

void ATPokeyEmulator::PushBreak() {
	if (mIRQEN & 0x80) {
		mIRQST &= ~0x80;
		mpConn->PokeyAssertIRQ();
	}
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

		if (mAUDC[0] & 15)
			outputsChanged = true;

		if (mIRQEN & 0x01) {
			mIRQST &= ~0x01;
			mpConn->PokeyAssertIRQ();
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

		if (mAUDC[1] & 15)
			outputsChanged = true;

		if (mIRQEN & 0x02) {
			mIRQST &= ~0x02;
			mpConn->PokeyAssertIRQ();
		}

		// two tone
		if ((mSKCTL & 8) && (!mbSerialOutputState || (mSKCTL & 0x80))) {
			// resync timer  to timer 2
			mCounter[0] = (uint32)mAUDF[0] + 1;
			SetupTimers(0x01);
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
			if (mAUDC[0] & 15)
				outputsChanged = true;
		}

		if (mAUDC[2] & 15)
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
			if (mAUDC[1] & 15)
				outputsChanged = true;
		}

		if (mAUDC[3] & 15)
			outputsChanged = true;

		if (mIRQEN & 0x04) {
			mIRQST &= ~0x04;
			mpConn->PokeyAssertIRQ();
		}

		if (mSerialInputCounter) {
			--mSerialInputCounter;

			if (!mSerialInputCounter && (mSKCTL & 0x10)) {
				mbSerialWaitingForStartBit = true;
			}
		}

		if (mSerialOutputCounter) {
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
					mpConn->PokeyAssertIRQ();
				}

				// bit 3 is special and doesn't get cleared by IRQEN
				if (!mSerialOutputCounter) {
					mIRQST &= ~0x08;
					if (mIRQEN & 0x08) {
						mpConn->PokeyAssertIRQ();
					}
				}
			}

			if (!mbSerOutValid) {
				if (mIRQEN & 0x10) {
					mIRQST &= ~0x10;
					mpConn->PokeyAssertIRQ();
				}
			}
		}
	}

	if (outputsChanged)
		UpdateOutput();
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

#if 0
	float g_arhist[256];
	float g_arhist2[256];
#endif

void ATPokeyEmulator::AdvanceFrame() {
	if (mKeyCodeTimer) {
		if (!--mKeyCodeTimer)
			mSKSTAT |= 0x04;
	}

	if (mbTurbo) {
		mbResampleWaitForLatencyDrain = true;
	} else if (!mbTurbo) {
		uint32 space = mpAudioOut->GetAvailSpace();

		if (mbResampleWaitForLatencyDrain) {
			if (space > kBufferSize - kLatency - kBlockSize)
				mbResampleWaitForLatencyDrain = false;
		} else {
#if 0
			static int c = 0;

			g_arhist[c++ & 255] = mAudioDampedError;
			g_arhist2[c++ & 255] = space;
#endif

			mAudioDampedError += ((float)((sint32)space - (kBufferSize - kLatency)) - mAudioDampedError) * 0.03f;

			if (mAudioDampedError < -1600.0f)
				mAudioDampedError = -1600.0f;
			else if (mAudioDampedError > 1600.0f)
				mAudioDampedError = 1600.0f;

			if (!mResampleRestabilizeCounter) {
				int delta = VDRoundToInt(mAudioDampedError);

				ResamplerSetRate(1.0f - (float)delta / 64000.0f);
			}
		}
	}
}

void ATPokeyEmulator::OnScheduledEvent(uint32 id) {
	switch(id) {
		case kATPokeyEventAudio:
			{
				mpAudioEvent = mpScheduler->AddEvent(28, this, kATPokeyEventAudio);

				uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
				GenerateSample(mResampleSamplesPresent, t);

				if (mpSlave)
					mpSlave->GenerateSample(mResampleSamplesPresent, t);

				if (++mResampleSamplesPresent >= mResampleSamplesNeeded)
					FlushBlock();
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
			}
			break;

		case kATPokeyEventTimer1:
			mpTimerEvents[0] = NULL;
			mCounter[0] = (uint32)mAUDF[0] + 1;
			if (mbLinkedTimers12) {
				if (--mCounter[1] <= 0) {
					mCounter[1] = (uint32)mAUDF[1] + 1;
					FireTimers(0x03);
					SetupTimers(0x03);
				} else {
					mCounter[0] = 256;
					FireTimers(0x01);
					SetupTimers(0x01);
				}
			} else {
				FireTimers(0x01);
				SetupTimers(0x01);
			}
			break;
		case kATPokeyEventTimer2:
			mpTimerEvents[1] = NULL;
			VDASSERT(!mbLinkedTimers12);
			mCounter[1] = (uint32)mAUDF[1] + 1;
			FireTimers(0x02);
			SetupTimers(0x02);
			break;
		case kATPokeyEventTimer3:
			mpTimerEvents[2] = NULL;
			mCounter[2] = (uint32)mAUDF[2] + 1;
			if (mbLinkedTimers34) {
				if (--mCounter[3] <= 0) {
					mCounter[3] = (uint32)mAUDF[3] + 1;
					FireTimers(0x0c);
					SetupTimers(0x0c);
				} else {
					mCounter[2] = 256;
					FireTimers(0x04);
					SetupTimers(0x04);
				}
			} else {
				FireTimers(0x04);
				SetupTimers(0x04);
			}
			break;
		case kATPokeyEventTimer4:
			mpTimerEvents[3] = NULL;
			VDASSERT(!mbLinkedTimers34);
			mCounter[3] = (uint32)mAUDF[3] + 1;
			FireTimers(0x08);
			SetupTimers(0x08);
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

void ATPokeyEmulator::UpdatePolynomialCounters() {
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
	int v0 = (mAUDC[0] & 15);
	int v1 = (mAUDC[1] & 15);
	int v2 = (mAUDC[2] & 15);
	int v3 = (mAUDC[3] & 15);
	int vpok	= ((mAUDC[0] & 0x10) ? v0 : out0 * v0)
				+ ((mAUDC[1] & 0x10) ? v1 : out1 * v1)
				+ ((mAUDC[2] & 0x10) ? v2 : out2 * v2)
				+ ((mAUDC[3] & 0x10) ? v3 : out3 * v3);

	mOutputLevel	= mMixTable[vpok] 
					+ mExternalInput
					+ (mbSpeakerState ? +12 : -12);		// The XL speaker is approximately 80% as loud as a full volume channel.
					;
}

extern "C" __declspec(align(16)) const float gVDCaptureAudioResamplingKernel[32][8] = {
	{+0x0000,+0x0000,+0x0000,+0x4000,+0x0000,+0x0000,+0x0000,+0x0000 },
	{-0x000a,+0x0052,-0x0179,+0x3fe2,+0x019f,-0x005b,+0x000c,+0x0000 },
	{-0x0013,+0x009c,-0x02cc,+0x3f86,+0x0362,-0x00c0,+0x001a,+0x0000 },
	{-0x001a,+0x00dc,-0x03f9,+0x3eef,+0x054a,-0x012c,+0x002b,+0x0000 },
	{-0x001f,+0x0113,-0x0500,+0x3e1d,+0x0753,-0x01a0,+0x003d,+0x0000 },
	{-0x0023,+0x0141,-0x05e1,+0x3d12,+0x097c,-0x021a,+0x0050,-0x0001 },
	{-0x0026,+0x0166,-0x069e,+0x3bd0,+0x0bc4,-0x029a,+0x0066,-0x0001 },
	{-0x0027,+0x0182,-0x0738,+0x3a5a,+0x0e27,-0x031f,+0x007d,-0x0002 },
	{-0x0028,+0x0197,-0x07b0,+0x38b2,+0x10a2,-0x03a7,+0x0096,-0x0003 },
	{-0x0027,+0x01a5,-0x0807,+0x36dc,+0x1333,-0x0430,+0x00af,-0x0005 },
	{-0x0026,+0x01ab,-0x083f,+0x34db,+0x15d5,-0x04ba,+0x00ca,-0x0007 },
	{-0x0024,+0x01ac,-0x085b,+0x32b3,+0x1886,-0x0541,+0x00e5,-0x0008 },
	{-0x0022,+0x01a6,-0x085d,+0x3068,+0x1b40,-0x05c6,+0x0101,-0x000b },
	{-0x001f,+0x019c,-0x0846,+0x2dfe,+0x1e00,-0x0644,+0x011c,-0x000d },
	{-0x001c,+0x018e,-0x0819,+0x2b7a,+0x20c1,-0x06bb,+0x0136,-0x0010 },
	{-0x0019,+0x017c,-0x07d9,+0x28e1,+0x2380,-0x0727,+0x014f,-0x0013 },
	{-0x0016,+0x0167,-0x0788,+0x2637,+0x2637,-0x0788,+0x0167,-0x0016 },
	{-0x0013,+0x014f,-0x0727,+0x2380,+0x28e1,-0x07d9,+0x017c,-0x0019 },
	{-0x0010,+0x0136,-0x06bb,+0x20c1,+0x2b7a,-0x0819,+0x018e,-0x001c },
	{-0x000d,+0x011c,-0x0644,+0x1e00,+0x2dfe,-0x0846,+0x019c,-0x001f },
	{-0x000b,+0x0101,-0x05c6,+0x1b40,+0x3068,-0x085d,+0x01a6,-0x0022 },
	{-0x0008,+0x00e5,-0x0541,+0x1886,+0x32b3,-0x085b,+0x01ac,-0x0024 },
	{-0x0007,+0x00ca,-0x04ba,+0x15d5,+0x34db,-0x083f,+0x01ab,-0x0026 },
	{-0x0005,+0x00af,-0x0430,+0x1333,+0x36dc,-0x0807,+0x01a5,-0x0027 },
	{-0x0003,+0x0096,-0x03a7,+0x10a2,+0x38b2,-0x07b0,+0x0197,-0x0028 },
	{-0x0002,+0x007d,-0x031f,+0x0e27,+0x3a5a,-0x0738,+0x0182,-0x0027 },
	{-0x0001,+0x0066,-0x029a,+0x0bc4,+0x3bd0,-0x069e,+0x0166,-0x0026 },
	{-0x0001,+0x0050,-0x021a,+0x097c,+0x3d12,-0x05e1,+0x0141,-0x0023 },
	{+0x0000,+0x003d,-0x01a0,+0x0753,+0x3e1d,-0x0500,+0x0113,-0x001f },
	{+0x0000,+0x002b,-0x012c,+0x054a,+0x3eef,-0x03f9,+0x00dc,-0x001a },
	{+0x0000,+0x001a,-0x00c0,+0x0362,+0x3f86,-0x02cc,+0x009c,-0x0013 },
	{+0x0000,+0x000c,-0x005b,+0x019f,+0x3fe2,-0x0179,+0x0052,-0x000a },
};

namespace {
	uint64 ResampleMono(sint16 *d, const float *s, uint32 count, uint64 accum, sint64 inc) {
		do {
			const float *s2 = s + (accum >> 32);
			const float *f = gVDCaptureAudioResamplingKernel[(uint32)accum >> 27];

			accum += inc;

			float v = s2[0]*f[0]
					+ s2[1]*f[1]
					+ s2[2]*f[2]
					+ s2[3]*f[3]
					+ s2[4]*f[4]
					+ s2[5]*f[5]
					+ s2[6]*f[6]
					+ s2[7]*f[7];

			d[0] = d[1] = VDClampedRoundFixedToInt16Fast(v * (1.0f / 16384.0f));
			d += 2;
		} while(--count);

		return accum;
	}

	uint64 ResampleStereo(sint16 *d, const float *s1, const float *s2, uint32 count, uint64 accum, sint64 inc) {
		do {
			const float *r1 = s1 + (accum >> 32);
			const float *r2 = s2 + (accum >> 32);
			const float *f = gVDCaptureAudioResamplingKernel[(uint32)accum >> 27];

			accum += inc;

			float a = r1[0]*f[0]
					+ r1[1]*f[1]
					+ r1[2]*f[2]
					+ r1[3]*f[3]
					+ r1[4]*f[4]
					+ r1[5]*f[5]
					+ r1[6]*f[6]
					+ r1[7]*f[7];

			float b = r2[0]*(sint32)f[0]
					+ r2[1]*(sint32)f[1]
					+ r2[2]*(sint32)f[2]
					+ r2[3]*(sint32)f[3]
					+ r2[4]*(sint32)f[4]
					+ r2[5]*(sint32)f[5]
					+ r2[6]*(sint32)f[6]
					+ r2[7]*(sint32)f[7];

			d[0] = VDClampedRoundFixedToInt16Fast(a * (1.0f / 16384.0f));
			d[1] = VDClampedRoundFixedToInt16Fast(b * (1.0f / 16384.0f));
			d += 2;
		} while(--count);

		return accum;
	}
}

void ATPokeyEmulator::FlushBlock() {
	VDASSERT(mResampleSamplesFiltered < kRawBlockSize);
	VDASSERT(mResampleSamplesPresent < kRawBlockSize);

	uint32 newSamples = mResampleSamplesPresent - mResampleSamplesFiltered;

	// The POKEY output decays to 3dB in about 1.25ms. We are running at 63.920KHz.
//	const float kHiPass = 0.008f;
	const float kHiPass = 0.0123609f;

	if (newSamples) {
		mFilter.Filter(mFilteredOutputBuffer + mResampleSamplesFiltered, mRawOutputBuffer + mResampleSamplesFiltered, newSamples, kHiPass);
		
		if (mpSlave)
			mpSlave->mFilter.Filter(mpSlave->mFilteredOutputBuffer + mResampleSamplesFiltered, mpSlave->mRawOutputBuffer + mResampleSamplesFiltered, newSamples, kHiPass);

		mResampleSamplesFiltered = mResampleSamplesPresent;

		if (mpAudioTap) {
			uint32 offset = mResampleSamplesFiltered - newSamples;

			if (mpSlave)
				mpAudioTap->WriteRawAudio(mFilteredOutputBuffer + offset, mpSlave->mFilteredOutputBuffer + offset, newSamples);
			else
				mpAudioTap->WriteRawAudio(mFilteredOutputBuffer + offset, NULL, newSamples);
		}
	}

	if (mpSlave)
		mResampleAccum = ResampleStereo(mOutputBuffer, mFilteredOutputBuffer, mpSlave->mFilteredOutputBuffer, kSamplesPerBlock, mResampleAccum, mResampleRate);
	else
		mResampleAccum = ResampleMono(mOutputBuffer, mFilteredOutputBuffer, kSamplesPerBlock, mResampleAccum, mResampleRate);

	VDASSERT(mResampleSamplesPresent >= (uint32)((mResampleAccum - mResampleRate) >> 32));

	ResamplerShift();

	uint32 availSpace = mpAudioOut->GetAvailSpace();
	if (mResampleRestabilizeCounter) {
		--mResampleRestabilizeCounter;

		// if we have too much data, drop this block
		if (availSpace <= kBufferSize - kLatency)
			return;

		// if we don't have enough data, repeat this block
		if (availSpace > kBufferSize - kLatency + kBlockSize*2)
			mpAudioOut->Write(mOutputBuffer, kBlockSize);
	}

	if (mbTurbo ? availSpace >= kBlockSize : mbResampleWaitForLatencyDrain ? availSpace >= kBufferSize - kLatency - kBlockSize : true) {
		// Check if the output is completely empty. If so, we should just prefill the
		// buffer instead of warping the audio.
		if (availSpace >= kBufferSize) {
			mpAudioOut->Write(NULL, kLatency - kBlockSize);
			mResampleRestabilizeCounter = 10;
		}

		mpAudioOut->Write(mOutputBuffer, kBlockSize);
	}
}

void ATPokeyEmulator::UpdateTimerCounters(uint8 channels) {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	int slowTickOffset = (mbUse15KHzClock ? t - mLast15KHzTime : t - mLast64KHzTime);

	if (channels & 0x01) {
		if (mpTimerEvents[0]) {
			int ticksLeft = mpScheduler->GetTicksToEvent(mpTimerEvents[0]);

			if (mbFastTimer1) {
				if (mbLinkedTimers12)
					mCounter[0] = mCounter[1] > 1 ? ticksLeft : ticksLeft <= 7 ? 1 : ticksLeft - 7;
				else
					mCounter[0] = ticksLeft <= 4 ? 1 : ticksLeft - 4;
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
				if (mbLinkedTimers34)
					mCounter[2] = mCounter[3] > 1 ? ticksLeft : ticksLeft <= 7 ? 1 : ticksLeft - 7;
				else
					mCounter[2] = ticksLeft <= 4 ? 1 : ticksLeft - 4;
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
				if (mbFastTimer1)
					ticks = mCounter[1] > 1 ? mCounter[0] : (mCounter[0] + 6);
				else if (mbUse15KHzClock)
					ticks = mCounter[0] * 114 + cyclesToNextSlowTick;
				else
					ticks = mCounter[0] * 28 + cyclesToNextSlowTick;
			} else {
				if (mbFastTimer1)
					ticks = mCounter[0] + 4;
				else if (mbUse15KHzClock)
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
					if (mbFastTimer3)
						ticks = mCounter[3] > 1 ? mCounter[2] : (mCounter[2] + 6);
					else if (mbUse15KHzClock)
						ticks = mCounter[2] * 114 + cyclesToNextSlowTick;
					else
						ticks = mCounter[2] * 28 + cyclesToNextSlowTick;
				} else {
					if (mbFastTimer3)
						ticks = mCounter[2] + 4;
					else if (mbUse15KHzClock)
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

uint8 ATPokeyEmulator::DebugReadByte(uint8 reg) {
	switch(reg) {
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

	return 0;
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

					delay += val;
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
					ATConsoleTaggedPrintf("POKEY: Reading SERIN value %02x (shiftreg: %02x; valid=%d)\n", c, mSerialInputShiftRegister, mbSerShiftInValid);

				mbSerInValid = false;

				mIRQST |= 0x20;
				if (!(mIRQEN & ~mIRQST))
					mpConn->PokeyNegateIRQ();

				for(Devices::const_iterator it(mDevices.begin()), itEnd(mDevices.end()); it!=itEnd && !mbSerInValid; ++it)
					(*it)->PokeySerInReady();
				return c;
			}
			break;

		case 0x0E:
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

	return 0;
}

void ATPokeyEmulator::WriteByte(uint8 reg, uint8 value) {
	switch(reg) {
		case 0x00:	// $D200 AUDF1
			mAUDF[0] = value;
			break;
		case 0x01:	// $D201 AUDC1
			if (mAUDC[0] != value) {
				mAUDC[0] = value;
				UpdateOutput();
			}
			break;
		case 0x02:	// $D202 AUDF2
			mAUDF[1] = value;
			break;
		case 0x03:	// $D203 AUDC2
			if (mAUDC[1] != value) {
				mAUDC[1] = value;
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
					if (mAUDC[0] & 15)
						outputsChanged = true;
				}

				if (!(mAUDCTL & 0x02) && !mHighPassFF[1]) {
					mHighPassFF[1] = true;
					mOutputs[1] = mNoiseFF[1] ^ 0x01;
					if (mAUDC[1] & 15)
						outputsChanged = true;
				}

				if (outputsChanged)
					UpdateOutput();

				SetupTimers(0x0f);
			}
			break;
		case 0x09:	// $D209 STIMER
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
			UpdateOutput();
			SetupTimers(0x0f);
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
			} else if (!mbSerOutValid) {
				mbSerOutValid = true;
				mIRQST |= 0x18;

				if (!(mIRQEN & ~mIRQST))
					mpConn->PokeyNegateIRQ();
			} else {
				if (mbTraceSIO)
					ATConsoleTaggedPrintf("POKEY: Serial output overrun detected.\n");
			}
			break;
		case 0x0E:
			mIRQEN = value;

			mIRQST |= ~value & 0xF7;

			if (!(mIRQEN & ~mIRQST))
				mpConn->PokeyNegateIRQ();
			else
				mpConn->PokeyAssertIRQ();
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
						mp64KHzEvent = mpScheduler->AddEvent(11, this, kATPokeyEvent64KHzTick);

						// The 15KHz polynomial counter is a 7-bit LFSR that is reset to all 0s
						// on init and XNORs bits 7 and 6, if seen as shifting left. In order to
						// achieve a 114 cycle period, a one is force fed when the register
						// equals XXXXXX. [PROBLEM: LFSR in schematic doesn't appear to work!]
						mp15KHzEvent = mpScheduler->AddEvent(89, this, kATPokeyEvent15KHzTick);

						mLast15KHzTime = ATSCHEDULER_GETTIME(mpScheduler) + 89 - 114;
						mLast64KHzTime = ATSCHEDULER_GETTIME(mpScheduler) + 11 - 28;
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
					mbSerInValid = false;
					mbSerShiftInValid = false;
					mbSerOutValid = false;
					mbSerialOutputState = false;
					mSKSTAT |= 0xC0;

					mIRQST |= 0x20;
					if (mIRQEN & 0x10)
						mIRQST &= ~0x10;
					mIRQST &= ~0x08;

					if (!(mIRQEN & ~mIRQST))
						mpConn->PokeyNegateIRQ();
					else
						mpConn->PokeyAssertIRQ();

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
	ATConsolePrintf("SERIN: %02x (%s)\n", mSERIN, mbSerInValid ? "pending" : "done");
	ATConsolePrintf("SEROUT: %02x (%s)\n", mSEROUT, mbSerOutValid ? "pending" : "done");
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

	ATConsolePrintf("Command line: %s\n", mbCommandLineState ? "asserted" : "negated");
}

void ATPokeyEmulator::ResamplerReset() {
	memset(mRawOutputBuffer, 0, sizeof mRawOutputBuffer);
	memset(mFilteredOutputBuffer, 0, sizeof mFilteredOutputBuffer);

	if (mpSlave) {
		memset(mpSlave->mRawOutputBuffer, 0, sizeof mpSlave->mRawOutputBuffer);
		memset(mpSlave->mFilteredOutputBuffer, 0, sizeof mpSlave->mFilteredOutputBuffer);
	}

	mResampleAccum	= 0x400000000;

	ResamplerSetRate(1.0f);

	mResampleSamplesPresent = 4;
	mResampleSamplesNeeded	= 4;
	mResampleSamplesFiltered = 4;
	mbResampleWaitForLatencyDrain = false;
	mResampleRestabilizeCounter = 0;

	ResamplerShift();
}

void ATPokeyEmulator::ResamplerShift() {
	uint32 samplePos = (uint32)(mResampleAccum >> 32);
	if (samplePos >= (kRawBlockSize >> 2)) {
		VDASSERT(mResampleSamplesPresent >= samplePos);

		uint32 samplesToRemove = samplePos - 4;

		memmove(mRawOutputBuffer, mRawOutputBuffer + samplesToRemove, sizeof(float) * mResampleSamplesPresent);
		memmove(mFilteredOutputBuffer, mFilteredOutputBuffer + samplesToRemove, sizeof(float) * mResampleSamplesPresent);

		if (mpSlave) {
			memmove(mpSlave->mRawOutputBuffer, mpSlave->mRawOutputBuffer + samplesToRemove, sizeof(float) * mResampleSamplesPresent);
			memmove(mpSlave->mFilteredOutputBuffer, mpSlave->mFilteredOutputBuffer + samplesToRemove, sizeof(float) * mResampleSamplesPresent);
		}

		mResampleAccum -= (uint64)samplesToRemove << 32;
		mResampleSamplesPresent -= samplesToRemove;
		mResampleSamplesFiltered -= samplesToRemove;
		VDASSERT(mResampleSamplesPresent >= (uint32)(mResampleAccum >> 32));
	}

	uint64 lastPointRequired = mResampleAccum + mResampleRate * (kSamplesPerBlock - 1);

	mResampleSamplesNeeded = (uint32)(lastPointRequired >> 32) + 5;

	VDASSERT(mResampleSamplesNeeded < kRawBlockSize);
}

void ATPokeyEmulator::ResamplerSetRate(float rate) {
	mResampleRateF	= rate;

	if (mbPal)
		mResampleRate	= VDRoundToInt64(4294967296.0 * rate * (63337.4f / 22050.0f));
	else
		mResampleRate	= VDRoundToInt64(4294967296.0 * rate * (63920.4f / 22050.0f));

	uint64 lastPointRequired = mResampleAccum + mResampleRate * (kSamplesPerBlock - 1);

	mResampleSamplesNeeded = (uint32)(lastPointRequired >> 32) + 5;

	VDASSERT(mResampleSamplesNeeded < kRawBlockSize);
}
