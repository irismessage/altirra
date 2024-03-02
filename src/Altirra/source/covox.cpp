//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2012 Avery Lee
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
#include <vd2/system/binary.h>
#include "covox.h"
#include "scheduler.h"
#include "audiooutput.h"
#include "memorymanager.h"
#include "console.h"

namespace {
	const float kOutputScale = 1.0f / 128.0f / 4.0f / 28.0f * 200.0f;
	const float kOutputBias = -128.0f * 4.0f * kOutputScale;
}

ATCovoxEmulator::ATCovoxEmulator()
	: mpMemLayerControl(NULL)
	, mpScheduler(NULL)
	, mpMemMan(NULL)
	, mpAudioOut(NULL)
{
}

ATCovoxEmulator::~ATCovoxEmulator() {
	Shutdown();
}

void ATCovoxEmulator::Init(ATMemoryManager *memMan, ATScheduler *sch, IATAudioOutput *audioOut) {
	mpMemMan = memMan;
	mpScheduler = sch;
	mpAudioOut = audioOut;

	audioOut->AddSyncAudioSource(this);

	ColdReset();
}

void ATCovoxEmulator::Shutdown() {
	if (mpMemMan) {
		if (mpMemLayerControl) {
			mpMemMan->DeleteLayer(mpMemLayerControl);
			mpMemLayerControl = NULL;
		}

		mpMemMan = NULL;
	}

	if (mpAudioOut) {
		mpAudioOut->RemoveSyncAudioSource(this);
		mpAudioOut = NULL;
	}
}

void ATCovoxEmulator::ColdReset() {
	mLastUpdate = ATSCHEDULER_GETTIME(mpScheduler);

	for(int i=0; i<4; ++i)
		mVolume[i] = 0x80;

	if (mpMemLayerControl) {
		mpMemMan->DeleteLayer(mpMemLayerControl);
		mpMemLayerControl = NULL;
	}

	ATMemoryHandlerTable handlers = {};
	handlers.mpThis = this;

	handlers.mbPassAnticReads = true;
	handlers.mbPassReads = true;
	handlers.mbPassWrites = true;
	handlers.mpDebugReadHandler = StaticReadControl;
	handlers.mpReadHandler = StaticReadControl;
	handlers.mpWriteHandler = StaticWriteControl;
	mpMemLayerControl = mpMemMan->CreateLayer(kATMemoryPri_HardwareOverlay, handlers, 0xD6, 0x01);

	mpMemMan->EnableLayer(mpMemLayerControl, kATMemoryAccessMode_CPUWrite, true);

	WarmReset();
}

void ATCovoxEmulator::WarmReset() {
	memset(mAccumBufferLeft, 0, sizeof mAccumBufferLeft);
	memset(mAccumBufferRight, 0, sizeof mAccumBufferRight);

	mOutputCount = 0;
	mOutputLevel = 0;
	mOutputAccumLeft = 0;
	mOutputAccumRight = 0;
}

void ATCovoxEmulator::DumpStatus() {
	ATConsolePrintf("Channel outputs: $%02X $%02X $%02X $%02X\n"
		, mVolume[0]
		, mVolume[1]
		, mVolume[2]
		, mVolume[3]
	);
}

void ATCovoxEmulator::WriteControl(uint8 addr, uint8 value) {
	addr &= 3;

	const uint8 prevValue = mVolume[addr];
	if (prevValue == value)
		return;

	Flush();
	mVolume[addr] = value;
}

void ATCovoxEmulator::Run(int cycles) {
	float vl = (float)(mVolume[0] + mVolume[3]);
	float vr = (float)(mVolume[1] + mVolume[2]);

	if (mOutputCount) {
		int tc = (int)(28 - mOutputCount);

		if (tc > cycles)
			tc = cycles;

		cycles -= tc;

		mOutputAccumLeft += vl * tc;
		mOutputAccumRight += vr * tc;
		mOutputCount += tc;

		if (mOutputCount < 28)
			return;

		if (mOutputLevel < kAccumBufferSize) {
			mAccumBufferLeft[mOutputLevel] = mOutputAccumLeft * kOutputScale;
			mAccumBufferRight[mOutputLevel] = mOutputAccumRight * kOutputScale;
			++mOutputLevel;
		}

		mOutputAccumLeft = kOutputBias;
		mOutputAccumRight = kOutputBias;
		mOutputCount = 0;
	}

	while(cycles >= 28) {
		if (mOutputLevel >= kAccumBufferSize) {
			cycles %= 28;
			break;
		}

		mAccumBufferLeft[mOutputLevel] = vl * 28 * kOutputScale;
		mAccumBufferRight[mOutputLevel] = vr * 28 * kOutputScale;
		++mOutputLevel;
		cycles -= 28;
	}

	mOutputAccumLeft = kOutputBias;
	mOutputAccumRight = kOutputBias;
	mOutputCount = 0;

	if (cycles) {
		mOutputAccumLeft += vl * cycles;
		mOutputAccumRight += vr * cycles;
		mOutputCount = cycles;
	}
}

void ATCovoxEmulator::WriteAudio(uint32 startTime, float *dstLeft, float *dstRightOpt, uint32 n) {
	Flush();

	VDASSERT(n <= kAccumBufferSize);

	// if we don't have enough samples, pad out; eventually we'll catch up enough
	if (mOutputLevel < n) {
		memset(mAccumBufferLeft + mOutputLevel, 0, sizeof(mAccumBufferLeft[0]) * (n - mOutputLevel));
		memset(mAccumBufferRight + mOutputLevel, 0, sizeof(mAccumBufferRight[0]) * (n - mOutputLevel));

		mOutputLevel = n;
	}

	if (dstRightOpt) {
		for(uint32 i=0; i<n; ++i) {
			dstLeft[i] += mAccumBufferLeft[i];
			dstRightOpt[i] += mAccumBufferRight[i];
		}
	} else {
		for(uint32 i=0; i<n; ++i)
			dstLeft[i] += (mAccumBufferLeft[i] + mAccumBufferRight[i]) * 0.5f;
	}

	// shift down accumulation buffers
	uint32 samplesLeft = mOutputLevel - n;

	if (samplesLeft) {
		memmove(mAccumBufferLeft, mAccumBufferLeft + n, samplesLeft * sizeof(mAccumBufferLeft[0]));
		memmove(mAccumBufferRight, mAccumBufferRight + n, samplesLeft * sizeof(mAccumBufferRight[0]));
	}

	mOutputLevel = samplesLeft;
}

void ATCovoxEmulator::Flush() {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	uint32 dt = t - mLastUpdate;
	mLastUpdate = t;

	Run(dt);
}

sint32 ATCovoxEmulator::StaticReadControl(void *thisptr, uint32 addr) {
	return -1;
}

bool ATCovoxEmulator::StaticWriteControl(void *thisptr, uint32 addr, uint8 value) {
	uint8 addr8 = (uint8)addr;

	((ATCovoxEmulator *)thisptr)->WriteControl(addr8, value);
	return false;
}
