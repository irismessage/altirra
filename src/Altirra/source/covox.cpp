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
#include <at/atcore/consoleoutput.h>
#include <at/atcore/deviceimpl.h>
#include "covox.h"
#include "scheduler.h"
#include "audiooutput.h"
#include "memorymanager.h"
#include "devicemanager.h"
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

void ATCovoxEmulator::DumpStatus(ATConsoleOutput& output) {
	output("Channel outputs: $%02X $%02X $%02X $%02X"
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

///////////////////////////////////////////////////////////////////////////

class ATDeviceCovox : public VDAlignedObject<16>
					, public ATDevice
					, public IATDeviceMemMap
					, public IATDeviceScheduling
					, public IATDeviceAudioOutput
					, public IATDeviceDiagnostics
{
public:
	ATDeviceCovox();

	virtual void *AsInterface(uint32 id) override;

	virtual void GetDeviceInfo(ATDeviceInfo& info) override;
	virtual void WarmReset() override;
	virtual void ColdReset() override;
	virtual void Init() override;
	virtual void Shutdown() override;

public: // IATDeviceMemMap
	virtual void InitMemMap(ATMemoryManager *memmap) override;
	virtual bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const override;

public:	// IATDeviceScheduling
	virtual void InitScheduling(ATScheduler *sch, ATScheduler *slowsch) override;

public:	// IATDeviceAudioOutput
	virtual void InitAudioOutput(IATAudioOutput *out) override;

public:	// IATDeviceDiagnostics
	virtual void DumpStatus(ATConsoleOutput& output) override;

private:
	static sint32 ReadByte(void *thisptr0, uint32 addr);
	static bool WriteByte(void *thisptr0, uint32 addr, uint8 value);

	ATMemoryManager *mpMemMan;
	ATScheduler *mpScheduler;
	IATAudioOutput *mpAudioOutput;

	ATCovoxEmulator mCovox;
};

ATDeviceCovox::ATDeviceCovox()
	: mpMemMan(nullptr)
	, mpScheduler(nullptr)
	, mpAudioOutput(nullptr)
{
}

void *ATDeviceCovox::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceMemMap::kTypeID:
			return static_cast<IATDeviceMemMap *>(this);

		case IATDeviceScheduling::kTypeID:
			return static_cast<IATDeviceScheduling *>(this);

		case IATDeviceAudioOutput::kTypeID:
			return static_cast<IATDeviceAudioOutput *>(this);

		case IATDeviceDiagnostics::kTypeID:
			return static_cast<IATDeviceDiagnostics *>(this);

		default:
			return ATDevice::AsInterface(id);
	}
}

void ATDeviceCovox::GetDeviceInfo(ATDeviceInfo& info) {
	info.mTag = "covox";
	info.mName = L"Covox";
	info.mConfigTag = "covox";
}

void ATDeviceCovox::WarmReset() {
	mCovox.WarmReset();
}

void ATDeviceCovox::ColdReset() {
	mCovox.ColdReset();
}

void ATDeviceCovox::Init() {
	mCovox.Init(mpMemMan, mpScheduler, mpAudioOutput);
}

void ATDeviceCovox::Shutdown() {
	mCovox.Shutdown();

	mpAudioOutput = nullptr;
	mpScheduler = nullptr;
	mpMemMan = nullptr;
}

void ATDeviceCovox::InitMemMap(ATMemoryManager *memmap) {
	mpMemMan = memmap;
}

bool ATDeviceCovox::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	if (index == 0) {
		lo = 0xD600;
		hi = 0xD700;
		return true;
	}

	return false;
}

void ATDeviceCovox::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
}

void ATDeviceCovox::InitAudioOutput(IATAudioOutput *out) {
	mpAudioOutput = out;
}

void ATDeviceCovox::DumpStatus(ATConsoleOutput& output) {
	mCovox.DumpStatus(output);
}

void ATCreateDeviceCovox(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceCovox> p(new ATDeviceCovox);

	*dev = p.release();
}

void ATRegisterDeviceCovox(ATDeviceManager& dev) {
	dev.AddDeviceFactory("covox", ATCreateDeviceCovox);
}
