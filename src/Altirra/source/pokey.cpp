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
		kATPokeyEventTimer1 = 2,
		kATPokeyEventTimer2 = 3,
		kATPokeyEventTimer3 = 4,
		kATPokeyEventTimer4 = 5,
		kATPokeyEventPotScanComplete = 6
	};
}

ATPokeyEmulator::ATPokeyEmulator(bool isSlave)
//	: mpAudioOut(VDCreateAudioOutputDirectSoundW32())
	: mpAudioOut(isSlave ? NULL : VDCreateAudioOutputWaveOutW32())
	, mbTraceSIO(false)
	, mp64KHzEvent(NULL)
	, mpPotScanEvent(NULL)
	, mpSlave(NULL)
	, mbIsSlave(isSlave)
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

	mAccum = 0;
	mSampleCounter = 0;
	mTicksAccumulated = 0;
	mDstIndex = 0;
	mLoPassAccum = 0;
	mbSpeakerState = false;
	mbSerialOutputState = false;
	mExternalInput = 0;

	memset(mpTimerEvents, 0, sizeof(mpTimerEvents));
}

ATPokeyEmulator::~ATPokeyEmulator() {
}

void ATPokeyEmulator::Init(IATPokeyEmulatorConnections *mem, ATScheduler *sched) {
	mpConn = mem;
	mpScheduler = sched;

	if (!mbIsSlave) {
		VDASSERT(!mp64KHzEvent);
		mp64KHzEvent = mpScheduler->AddEvent(28, this, kATPokeyEvent64KHzTick);
	}

	ColdReset();

	int poly4 = 1;
	for(int i=0; i<15; ++i) {
		poly4 = (poly4+poly4) + (((poly4 >> 2) ^ (poly4 >> 3)) & 1);

		mPoly4Buffer[i] = (poly4 & 1);
	}

	int poly5 = 1;
	for(int i=0; i<31; ++i) {
		poly5 = (poly5+poly5) + (((poly5 >> 2) ^ (poly5 >> 4)) & 1);

		mPoly5Buffer[i] = (poly5 & 1);
	}

	int poly9 = 1;
	for(int i=0; i<511; ++i) {
		// Note: This one is actually verified against a real Atari.
		// At WSYNC time, the pattern goes: 00 DF EE 16 B9....
		poly9 = (poly9 >> 1) + (((poly9 << 8) ^ (poly9 << 3)) & 0x100);

		mPoly9Buffer[i] = (poly9 & 0xff);
	}

	int poly17 = 1;
	for(int i=0; i<131071; ++i) {
		poly17 = (poly17+poly17) + (((poly17 >> 13) ^ (poly17 >> 16)) & 1);

		mPoly17Buffer[i] = (poly17 & 0xff);
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

	mLastPolyTime = ATSCHEDULER_GETTIME(mpScheduler);
	mPoly17Counter = 1;
	mPoly9Counter = 1;
	mPoly5Counter = 1;
	mPoly4Counter = 1;
	mSerialInputShiftRegister = 0;
	mbSerInValid = false;
	mbSerShiftInValid = false;
	mSerialOutputShiftRegister = 0;
	mSerialOutputCounter = 0;
	mbSerOutValid = false;
	mbSerialOutputState = false;

	mAudioSpaceLast = 0;
	mAudioDampedError = 0;
	mAudioRate = 22050*4*28;

	memset(mAUDF, 0, sizeof mAUDF);
	memset(mAUDC, 0, sizeof mAUDC);

	mOutputs[0] = 1;
	mOutputs[1] = 1;
	mOutputs[2] = 1;
	mOutputs[3] = 1;
	mOutputLevel = 0;
	mLastOutputTime = ATSCHEDULER_GETTIME(mpScheduler);
	mLast15KHzTime = ATSCHEDULER_GETTIME(mpScheduler);
	mLast64KHzTime = ATSCHEDULER_GETTIME(mpScheduler);

	if (!mbIsSlave) {
		if (mp64KHzEvent) {
			mpScheduler->RemoveEvent(mp64KHzEvent);
			mp64KHzEvent = NULL;
		}

		mp64KHzEvent = mpScheduler->AddEvent(28, this, kATPokeyEvent64KHzTick);
	}

	if (mpPotScanEvent) {
		mpScheduler->RemoveEvent(mpPotScanEvent);
		mpPotScanEvent = NULL;
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
		FireTimers(0x0c);
		SetupTimers(0x0c);
	}

	mSerialInputShiftRegister = c;
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
	mExternalInput = v;
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

	bool poly17 = mAUDCTL & 0x80 ? (mPoly9Buffer[mPoly9Counter] & 1) : (mPoly17Buffer[mPoly17Counter] & 1);
	bool poly5 = mPoly5Buffer[mPoly5Counter];
	bool poly4 = mPoly4Buffer[mPoly4Counter];
	bool outputsChanged = false;

	if (activeChannels & 0x01) {
		bool noiseFFInput = (mAUDC[0] & 0x20) ? !mNoiseFF[0] : (mAUDC[0] & 0x40) ? poly4 : poly17;
		bool noiseFFClock = (mAUDC[0] & 0x80) ? true : poly5;

		if (noiseFFClock)
			mNoiseFF[0] = noiseFFInput;

		mOutputs[0] = mNoiseFF[0];
		if (mAUDCTL & 0x04)
			mOutputs[0] ^= mHighPassFF[0] ? 0x01 : 0x00;

		if (mAUDC[0] & 15)
			outputsChanged = true;

		if (mIRQEN & 0x01) {
			mIRQST &= ~0x01;
			mpConn->PokeyAssertIRQ();
		}

		// two tone
		if ((mSKCTL & 8) && mbSerialOutputState) {
			// resync timer 2 to timer 1
			mCounter[1] = (uint32)mAUDF[1] + 1;
			SetupTimers(0x02);
		}
	}

	// count timer 2
	if (activeChannels & 0x02) {
		bool noiseFFInput = (mAUDC[1] & 0x20) ? !mNoiseFF[1] : (mAUDC[1] & 0x40) ? poly4 : poly17;
		bool noiseFFClock = (mAUDC[1] & 0x80) ? true : poly5;

		if (noiseFFClock)
			mNoiseFF[1] = noiseFFInput;

		mOutputs[1] = mNoiseFF[1];
		if (mAUDCTL & 0x02)
			mOutputs[1] ^= mHighPassFF[1] ? 0x01 : 0x00;

		if (mAUDC[1] & 15)
			outputsChanged = true;

		if (mIRQEN & 0x02) {
			mIRQST &= ~0x02;
			mpConn->PokeyAssertIRQ();
		}

		// two tone
		if ((mSKCTL & 8) && !mbSerialOutputState) {
			// resync timer  to timer 2
			mCounter[0] = (uint32)mAUDF[0] + 1;
			SetupTimers(0x01);
		}
	}

	// count timer 3
	if (activeChannels & 0x04) {
		bool noiseFFInput = (mAUDC[2] & 0x20) ? !mNoiseFF[2] : (mAUDC[2] & 0x40) ? poly4 : poly17;
		bool noiseFFClock = (mAUDC[2] & 0x80) ? true : poly5;

		if (noiseFFClock)
			mNoiseFF[2] = noiseFFInput;

		mOutputs[2] = mNoiseFF[2];

		mHighPassFF[0] = mNoiseFF[0];
		if (mAUDCTL & 0x04) {
			mOutputs[0] ^= mHighPassFF[0] ? 0x01 : 0x00;
			if (mAUDC[1] & 15)
				outputsChanged = true;
		}

		if (mAUDC[2] & 15)
			outputsChanged = true;
	}

	// count timer 4
	if (activeChannels & 0x08) {
		bool noiseFFInput = (mAUDC[3] & 0x20) ? !mNoiseFF[3] : (mAUDC[3] & 0x40) ? poly4 : poly17;
		bool noiseFFClock = (mAUDC[3] & 0x80) ? true : poly5;

		if (noiseFFClock)
			mNoiseFF[3] = noiseFFInput;

		mOutputs[3] = mNoiseFF[3];

		mHighPassFF[1] = mNoiseFF[1];
		if (mAUDCTL & 0x02) {
			mOutputs[1] ^= mHighPassFF[1] ? 0x01 : 0x00;
			if (mAUDC[1] & 15)
				outputsChanged = true;
		}

		if (mAUDC[3] & 15)
			outputsChanged = true;

		if (mIRQEN & 0x04) {
			mIRQST &= ~0x04;
			mpConn->PokeyAssertIRQ();
		}

		if (mSerialOutputCounter) {
			--mSerialOutputCounter;

			// We've already transmitted the start bit (low), so we need to do data bits and then
			// stop bit (high).
			mbSerialOutputState = mSerialOutputCounter ? (mSerialOutputShiftRegister & (1 << (9 - mSerialOutputCounter))) != 0 : true;

			if (!mSerialOutputCounter) {
				if (mbTraceSIO)
					ATConsoleTaggedPrintf("POKEY: Transmitted serial byte %02x to SIO bus\n", mSerialOutputShiftRegister);

				for(Devices::const_iterator it(mDevices.begin()), itEnd(mDevices.end()); it!=itEnd; ++it)
					(*it)->PokeyWriteSIO(mSerialOutputShiftRegister);

				if (mbSerOutValid) {
					mSerialOutputCounter = 10;
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

			if (mSerialOutputCounter < 9 && !mbSerOutValid) {
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
	mLast15KHzTime = ATSCHEDULER_GETTIME(mpScheduler);

	if (mpSlave)
		mpSlave->AdvanceScanLine();
}

void ATPokeyEmulator::AdvanceFrame() {
	if (mKeyCodeTimer) {
		if (!--mKeyCodeTimer)
			mSKSTAT |= 0x04;
	}
}

void ATPokeyEmulator::OnScheduledEvent(uint32 id) {
	switch(id) {
		case kATPokeyEvent64KHzTick:
			{
				VDASSERT(!mbIsSlave);
				mp64KHzEvent = mpScheduler->AddEvent(28, this, kATPokeyEvent64KHzTick);

				uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
				mLast64KHzTime = t;

				if (mpSlave)
					mpSlave->SetLast64KHzTime(t);

				mSampleCounter += mAudioRate;
				if (mSampleCounter >= 7159090) {
					mSampleCounter -= 7159090;

					GenerateSample(t);

					if (mpSlave) {
						mpSlave->GenerateSample(t);
						++mpSlave->mDstIndex;
					}

					if (++mDstIndex >= kSamplesPerBlock)
						FlushBlock();
				}
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

		case kATPokeyEventPotScanComplete:
			mALLPOT = 0;
			for(int i=0; i<8; ++i)
				mPOT[i] = 228;
			mpPotScanEvent = NULL;
			break;
	}
}

void ATPokeyEmulator::GenerateSample(uint32 t) {
	int oc = t - mLastOutputTime;
	mTicksAccumulated += oc;
	mAccum += mOutputLevel * oc;
	mLastOutputTime = t;

	float v = 0;
	
	if (mTicksAccumulated) {
		v = mAccum / ((float)mTicksAccumulated * 63.0f) *(127.0f / 255.0f);
		v += (mbSpeakerState ? +1 : -1) * (5.0f / (63.0f) * (127.0f / 255.0f));
		mTicksAccumulated = 0;
	}

	mAccum = 0;

	VDASSERT(mDstIndex < kSamplesPerBlock);
	mRawOutputBuffer[mDstIndex] = v;
}

void ATPokeyEmulator::UpdatePolynomialCounters() {
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
	mTicksAccumulated += oc;
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

	mOutputLevel	= ((mAUDC[0] & 0x10) ? v0 : out0 * v0)
					+ ((mAUDC[1] & 0x10) ? v1 : out1 * v1)
					+ ((mAUDC[2] & 0x10) ? v2 : out2 * v2)
					+ ((mAUDC[3] & 0x10) ? v3 : out3 * v3)
					+ mExternalInput;
}

void ATPokeyEmulator::FlushBlock() {
	mDstIndex = 0;

	sint16 *dst = mOutputBuffer;
	if (mpSlave) {
		mpSlave->mDstIndex = 0;

		const float *src1 = mRawOutputBuffer;
		const float *src2 = mpSlave->mRawOutputBuffer;

		for(int i=0; i<kSamplesPerBlock; ++i) {
			float vL = mRawOutputBuffer[i];

			mHiPassAccum += (vL - mHiPassAccum) * 0.02f;
			vL -= mHiPassAccum;

			mLoPassAccum += (vL - mLoPassAccum) * 0.4f;
			vL = mLoPassAccum;

			float vR = mpSlave->mRawOutputBuffer[i];

			mpSlave->mHiPassAccum += (vR - mpSlave->mHiPassAccum) * 0.02f;
			vR -= mpSlave->mHiPassAccum;

			mpSlave->mLoPassAccum += (vR - mpSlave->mLoPassAccum) * 0.4f;
			vR = mpSlave->mLoPassAccum;

			sint16 outputL = VDClampedRoundFixedToInt16Fast(vL);
			sint16 outputR = VDClampedRoundFixedToInt16Fast(vR);

			dst[0] = outputL;
			dst[1] = outputR;
			dst += 2;
		}
	} else {
		for(int i=0; i<kSamplesPerBlock; ++i) {
			float v = mRawOutputBuffer[i];

			mHiPassAccum += (v - mHiPassAccum) * 0.02f;
			v -= mHiPassAccum;

			mLoPassAccum += (v - mLoPassAccum) * 0.4f;
			v = mLoPassAccum;

			sint16 output = VDClampedRoundFixedToInt16Fast(v);

			dst[0] = output;
			dst[1] = output;
			dst += 2;
		}
	}

	uint32 availSpace = mpAudioOut->GetAvailSpace();
	if (!mbTurbo || availSpace >= kBlockSize) {
		// Check if the output is completely empty. If so, we should just prefill the
		// buffer instead of warping the audio.
		if (availSpace >= kBufferSize) {
			mpAudioOut->Write(NULL, kLatency - kBlockSize);
			mAudioSpaceLast = kBufferSize - kLatency;
			mAudioDampedError = 0;
		}

		mpAudioOut->Write(mOutputBuffer, kBlockSize);
	}

	if (!mbTurbo) {
		uint32 space = mpAudioOut->GetAvailSpace();
		sint32 spaceDelta = space - mAudioSpaceLast;
		mAudioSpaceLast = space;

		if (spaceDelta > 16384)
			spaceDelta -= 32768;
		else if (spaceDelta < -16384)
			spaceDelta += 32768;

		mAudioDampedError += ((float)((sint32)space - (kBufferSize - kLatency)) - mAudioDampedError) * 0.05f;

		int delta = VDRoundToInt(mAudioDampedError);
		if (delta < -3200)
			delta = -3200;
		else if (delta > 3200)
			delta = 3200;
		mAudioRate = (22050*4 + delta)*28;
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

	if (channels & 0x01) {
		if (mpTimerEvents[0]) {
			mpScheduler->RemoveEvent(mpTimerEvents[0]);
			mpTimerEvents[0] = NULL;
		}

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

	if (channels & 0x02) {
		if (mpTimerEvents[1]) {
			mpScheduler->RemoveEvent(mpTimerEvents[1]);
			mpTimerEvents[1] = NULL;
		}

		uint32 ticks;
		if (!mbLinkedTimers12) {
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

		// When asynchronous channel 4 is enabled, the standard '1' bit state of the undriven SIO
		// bus locks timers 3 and 4 into the reset state.
		if (!(mSKCTL & 0x10)) {
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

	if (channels & 0x08) {
		if (mpTimerEvents[3]) {
			mpScheduler->RemoveEvent(mpTimerEvents[3]);
			mpTimerEvents[3] = NULL;
		}

		// When asynchronous channel 4 is enabled, the standard '1' bit state of the undriven SIO
		// bus locks timers 3 and 4 into the reset state.
		if (!(mSKCTL & 0x10)) {
			uint32 ticks;
			if (!mbLinkedTimers34) {
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
			break;
		case 0x05:	// $D205 AUDC3
			if (mAUDC[2] != value) {
				mAUDC[2] = value;
				UpdateOutput();
			}
			break;
		case 0x06:	// $D206 AUDF4
			mAUDF[3] = value;
			break;
		case 0x07:	// $D207 AUDC4
			if (mAUDC[3] != value) {
				mAUDC[3] = value;
				UpdateOutput();
			}
			break;
		case 0x08:	// $D208 AUDCTL
			if (mAUDCTL != value) {
				UpdateTimerCounters(0x0f);
				mAUDCTL = value;
				mbFastTimer1 = (mAUDCTL & 0x40) != 0;
				mbFastTimer3 = (mAUDCTL & 0x20) != 0;
				mbLinkedTimers12 = (mAUDCTL & 0x10) != 0;
				mbLinkedTimers34 = (mAUDCTL & 0x08) != 0;
				mbUse15KHzClock = (mAUDCTL & 0x01) != 0;
				SetupTimers(0x0f);
			}
			break;
		case 0x09:	// $D209 STIMER
			mCounter[0] = (uint32)mAUDF[0] + 1;
			mCounter[1] = (uint32)mAUDF[1] + 1;
			mCounter[2] = (uint32)mAUDF[2] + 1;
			mCounter[3] = (uint32)mAUDF[3] + 1;
			SetupTimers(0x0f);
			break;
		case 0x0A:	// $D20A SKRES
			mSKSTAT |= 0xe0;
			break;
		case 0x0B:	// $D20B POTGO
			for(int i=0; i<8; ++i)
				mPOT[i] = 0;
			mALLPOT = 0xFF;

			if (mSKCTL & 4)
				mPotScanStartTime = ATSCHEDULER_GETTIME(mpScheduler);
			else
				mPotScanStartTime = mLast15KHzTime;

			if (mpPotScanEvent)
				mpScheduler->RemoveEvent(mpPotScanEvent);
			mpPotScanEvent = mpScheduler->AddEvent(mSKCTL & 0x04 ? 228 : 228*114, this, kATPokeyEventPotScanComplete);
			break;
		case 0x0D:	// $D20D SEROUT
			if (mbTraceSIO)
				ATConsoleTaggedPrintf("POKEY: Sending serial byte %02x\n", value);

			mSEROUT = value;
			if (!mSerialOutputCounter) {
				mSerialOutputShiftRegister = value;
				mbSerialOutputState = false;
				mSerialOutputCounter = 10;
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
	mPOT[0] = reader.ReadUint8();
	mPOT[1] = reader.ReadUint8();
	mPOT[2] = reader.ReadUint8();
	mPOT[3] = reader.ReadUint8();
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
	writer.WriteUint8(mPOT[0]);
	writer.WriteUint8(mPOT[1]);
	writer.WriteUint8(mPOT[2]);
	writer.WriteUint8(mPOT[3]);
	writer.WriteUint8(mALLPOT);
	writer.WriteUint8(mIRQEN);
	writer.WriteUint8(mIRQST);
	writer.WriteUint8(mSKCTL);
}

void ATPokeyEmulator::DumpStatus(bool isSlave) {
	for(int i=0; i<4; ++i)
		ATConsolePrintf("AUDF%u: %02x  AUDC%u: %02x\n", i+1, mAUDF[i], i+1, mAUDC[i]);

	ATConsolePrintf("AUDCTL: %02x%s%s%s%s%s%s%s%s\n"
		, mAUDCTL
		, mAUDCTL & 0x80 ? ", 9-bit poly" : ""
		, mAUDCTL & 0x40 ? ", 1.79 ch1" : ""
		, mAUDCTL & 0x20 ? ", 1.79 ch3" : ""
		, mAUDCTL & 0x10 ? ", ch1+ch2" : ""
		, mAUDCTL & 0x08 ? ", ch3+ch4" : ""
		, mAUDCTL & 0x04 ? ", highpass 1+3" : ""
		, mAUDCTL & 0x02 ? ", highpass 2+4" : ""
		, mAUDCTL & 0x01 ? ", 15KHz" : "");
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
