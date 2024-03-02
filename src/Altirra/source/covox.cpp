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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <at/atcore/audiomixer.h>
#include <at/atcore/consoleoutput.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicepia.h>
#include <at/atcore/devicesystemcontrol.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/scheduler.h>
#include "covox.h"
#include "memorymanager.h"
#include "console.h"

ATCovoxEmulator::ATCovoxEmulator() {
	mpEdgeBufferL = new ATSyncAudioEdgeBuffer;
	mpEdgeBufferL->mpDebugLabel = "Covox L";

	mpEdgeBufferR = new ATSyncAudioEdgeBuffer;
	mpEdgeBufferR->mpDebugLabel = "Covox R";
}

ATCovoxEmulator::~ATCovoxEmulator() {
	Shutdown();
}

void ATCovoxEmulator::SetAddressRange(uint32 addrLo, uint32 addrHi, bool passWrites) {
	if (mAddrLo != addrLo || mAddrHi != addrHi || mbPassWrites != passWrites) {
		mAddrLo = addrLo;
		mAddrHi = addrHi;
		mbPassWrites = passWrites;

		InitMapping();
	}
}

void ATCovoxEmulator::SetEnabled(bool enable) {
	if (mbEnabled == enable)
		return;

	mbEnabled = enable;

	if (mpMemLayerControl)
		mpMemMan->EnableLayer(mpMemLayerControl, kATMemoryAccessMode_CPUWrite, mbEnabled);
}

void ATCovoxEmulator::Init(ATMemoryManager *memMan, ATScheduler *sch, IATAudioMixer *mixer) {
	mpMemMan = memMan;
	mpScheduler = sch;
	mpAudioMixer = mixer;

	mixer->AddSyncAudioSource(this);

	InitMapping();

	ColdReset();

	std::fill(std::begin(mVolume), std::end(mVolume), 0x80);
}

void ATCovoxEmulator::Shutdown() {
	if (mpMemMan) {
		if (mpMemLayerControl) {
			mpMemMan->DeleteLayer(mpMemLayerControl);
			mpMemLayerControl = NULL;
		}

		mpMemMan = NULL;
	}

	if (mpAudioMixer) {
		mpAudioMixer->RemoveSyncAudioSource(this);
		mpAudioMixer = nullptr;
	}
}

void ATCovoxEmulator::ColdReset() {
	for(int i=0; i<4; ++i)
		mVolume[i] = 0x80;

	WarmReset();
}

void ATCovoxEmulator::WarmReset() {
	mpEdgeBufferL->mEdges.clear();
	mpEdgeBufferR->mEdges.clear();

	mbUnbalanced = false;
	mbUnbalancedSticky = false;
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

	const int delta = value - mVolume[addr];
	if (delta == 0)
		return;

	mVolume[addr] = value;

	ATSyncAudioEdgeBuffer& edgeBuffer = (addr == 1 || addr == 2) ? *mpEdgeBufferR : *mpEdgeBufferL;

	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	if (edgeBuffer.mEdges.empty() || edgeBuffer.mEdges.back().mTime != t)
		edgeBuffer.mEdges.push_back(ATSyncAudioEdge { .mTime = t, .mDeltaValue = (float)delta });
	else
		edgeBuffer.mEdges.back().mDeltaValue += (float)delta;

	mbUnbalanced = mVolume[0] + mVolume[3] != mVolume[1] + mVolume[2];

	if (mbUnbalanced)
		mbUnbalancedSticky = true;
}

void ATCovoxEmulator::WriteMono(uint8 value) {
	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	const int deltaL = value*2 - (mVolume[0] + mVolume[3]);
	if (deltaL != 0) {
		if (mpEdgeBufferL->mEdges.empty() || mpEdgeBufferL->mEdges.back().mTime != t)
			mpEdgeBufferL->mEdges.push_back(ATSyncAudioEdge { .mTime = t, .mDeltaValue = (float)deltaL });
		else
			mpEdgeBufferL->mEdges.back().mDeltaValue += (float)deltaL;

		mVolume[0] = value;
		mVolume[3] = value;
	}

	const int deltaR = value*2 - (mVolume[1] + mVolume[2]);
	if (deltaR != 0) {
		if (mpEdgeBufferR->mEdges.empty() || mpEdgeBufferR->mEdges.back().mTime != t)
			mpEdgeBufferR->mEdges.push_back(ATSyncAudioEdge { .mTime = t, .mDeltaValue = (float)deltaR });
		else
			mpEdgeBufferR->mEdges.back().mDeltaValue += (float)deltaR;

		mVolume[1] = value;
		mVolume[2] = value;
	}

	mbUnbalanced = false;
}

void ATCovoxEmulator::WriteAudio(const ATSyncAudioMixInfo& mixInfo) {
	auto& edgePlayer = mpAudioMixer->GetEdgePlayer();

	// 1/128 for mapping unsigned 8-bit PCM to [-1, 1] (well, almost)
	// 1/2 for two source channels summed on each output channel
	const float kOutputScale = 1.0f / 128.0f / 2.0f;
	if (mbUnbalancedSticky) {
		edgePlayer.AddEdgeBuffer(mpEdgeBufferR);

		mpEdgeBufferL->mLeftVolume = kOutputScale;
		mpEdgeBufferL->mRightVolume = 0;
		mpEdgeBufferR->mLeftVolume = 0;
		mpEdgeBufferR->mRightVolume = kOutputScale;
	} else {
		mpEdgeBufferL->mLeftVolume = kOutputScale;
		mpEdgeBufferL->mRightVolume = kOutputScale;
	}

	edgePlayer.AddEdgeBuffer(mpEdgeBufferL);

	mbUnbalancedSticky = mbUnbalanced;
}

void ATCovoxEmulator::InitMapping() {
	if (mpMemMan) {
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

		const uint8 pageLo = (uint8)(mAddrLo >> 8);
		const uint8 pageHi = (uint8)(mAddrHi >> 8);

		mpMemLayerControl = mpMemMan->CreateLayer(kATMemoryPri_HardwareOverlay, handlers, pageLo, pageHi - pageLo + 1);

		mpMemMan->EnableLayer(mpMemLayerControl, kATMemoryAccessMode_CPUWrite, mbEnabled);
		mpMemMan->SetLayerName(mpMemLayerControl, "Covox");
	}
}

sint32 ATCovoxEmulator::StaticReadControl(void *thisptr, uint32 addr) {
	return -1;
}

bool ATCovoxEmulator::StaticWriteControl(void *thisptr0, uint32 addr, uint8 value) {
	auto *thisptr = (ATCovoxEmulator *)thisptr0;

	if (addr >= thisptr->mAddrLo && addr <= thisptr->mAddrHi) {
		uint8 addr8 = (uint8)addr;

		if (thisptr->mbFourCh)
			thisptr->WriteControl(addr8, value);
		else
			thisptr->WriteMono(value);

		return !thisptr->mbPassWrites;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////

class ATDeviceCovox final : public VDAlignedObject<16>
					, public ATDevice
					, public IATDeviceMemMap
					, public IATDeviceScheduling
					, public IATDeviceAudioOutput
					, public IATDeviceDiagnostics
					, public IATDeviceCovoxControl
{
public:
	virtual void *AsInterface(uint32 id) override;

	virtual void GetDeviceInfo(ATDeviceInfo& info) override;
	virtual void WarmReset() override;
	virtual void ColdReset() override;
	virtual void GetSettings(ATPropertySet& settings) override;
	virtual bool SetSettings(const ATPropertySet& settings) override;
	virtual void Init() override;
	virtual void Shutdown() override;

public: // IATDeviceMemMap
	virtual void InitMemMap(ATMemoryManager *memmap) override;
	virtual bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const override;

public:	// IATDeviceScheduling
	virtual void InitScheduling(ATScheduler *sch, ATScheduler *slowsch) override;

public:	// IATDeviceAudioOutput
	virtual void InitAudioOutput(IATAudioMixer *mixer) override;

public:	// IATDeviceDiagnostics
	virtual void DumpStatus(ATConsoleOutput& output) override;

public:	// IATDeviceCovoxControl
	virtual void InitCovoxControl(IATCovoxController& controller) override;

private:
	void OnCovoxEnabled(bool enabled);

	ATMemoryManager *mpMemMan = nullptr;
	ATScheduler *mpScheduler = nullptr;
	IATAudioMixer *mpAudioMixer = nullptr;

	uint32 mAddrLo = 0xD600;
	uint32 mAddrHi = 0xD6FF;

	IATCovoxController *mpCovoxController = nullptr;
	vdfunction<void(bool)> mCovoxCallback;

	ATCovoxEmulator mCovox;
};

void ATCreateDeviceCovox(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceCovox> p(new ATDeviceCovox);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefCovox = { "covox", "covox", L"Covox", ATCreateDeviceCovox };

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

		case IATDeviceCovoxControl::kTypeID:
			return static_cast<IATDeviceCovoxControl *>(this);

		default:
			return ATDevice::AsInterface(id);
	}
}

void ATDeviceCovox::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefCovox;
}

void ATDeviceCovox::WarmReset() {
	mCovox.WarmReset();
}

void ATDeviceCovox::ColdReset() {
	mCovox.ColdReset();
}

void ATDeviceCovox::GetSettings(ATPropertySet& settings) {
	settings.SetUint32("base", mAddrLo);
	settings.SetUint32("channels", mCovox.IsFourChannels() ? 4 : 1);
}

bool ATDeviceCovox::SetSettings(const ATPropertySet& settings) {
	uint32 baseAddr = settings.GetUint32("base", 0xD600);

	switch(baseAddr) {
		case 0xD280:
			mAddrLo = baseAddr;
			mAddrHi = baseAddr + 0x7F;
			mCovox.SetAddressRange(0xD280, 0xD2FF, false);
			break;

		case 0xD100:
		case 0xD500:
		case 0xD600:
		case 0xD700:
			mAddrLo = baseAddr;
			mAddrHi = baseAddr + 0xFF;
			mCovox.SetAddressRange(baseAddr, baseAddr + 0xFF, true);
			break;
	}

	uint32 channels = settings.GetUint32("channels", 4);
	mCovox.SetFourChannels(channels > 1);

	return true;
}

void ATDeviceCovox::Init() {
	mCovox.SetAddressRange(mAddrLo, mAddrHi, mAddrLo != 0xD280);
	mCovox.Init(mpMemMan, mpScheduler, mpAudioMixer);
}

void ATDeviceCovox::Shutdown() {
	if (mpCovoxController) {
		mpCovoxController->GetCovoxEnableNotifyList().Remove(&mCovoxCallback);
		mpCovoxController = nullptr;
	}

	mCovox.Shutdown();

	mpAudioMixer = nullptr;
	mpScheduler = nullptr;
	mpMemMan = nullptr;
}

void ATDeviceCovox::InitMemMap(ATMemoryManager *memmap) {
	mpMemMan = memmap;
}

bool ATDeviceCovox::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	if (index == 0) {
		lo = mAddrLo;
		hi = mAddrHi + 1;
		return true;
	}

	return false;
}

void ATDeviceCovox::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
}

void ATDeviceCovox::InitAudioOutput(IATAudioMixer *mixer) {
	mpAudioMixer = mixer;
}

void ATDeviceCovox::DumpStatus(ATConsoleOutput& output) {
	mCovox.DumpStatus(output);
}

void ATDeviceCovox::InitCovoxControl(IATCovoxController& controller) {
	mpCovoxController = &controller;
	mpCovoxController->GetCovoxEnableNotifyList().Add(&mCovoxCallback);

	OnCovoxEnabled(mpCovoxController->IsCovoxEnabled());
	mCovoxCallback = [this](bool enable) { OnCovoxEnabled(enable); };
}

void ATDeviceCovox::OnCovoxEnabled(bool enabled) {
	mCovox.SetEnabled(enabled);
}

///////////////////////////////////////////////////////////////////////////

class ATDeviceSimCovox final : public VDAlignedObject<16>
					, public ATDevice
					, public IATDeviceDiagnostics
{
public:
	virtual void *AsInterface(uint32 id) override;

	virtual void GetDeviceInfo(ATDeviceInfo& info) override;
	virtual void ColdReset() override;
	virtual void Init() override;
	virtual void Shutdown() override;

public:	// IATDeviceDiagnostics
	virtual void DumpStatus(ATConsoleOutput& output) override;

private:
	void UpdateFromPIAOutput();

	IATDevicePIA *mpPIA = nullptr;
	int mPIAOutput = -1;

	ATCovoxEmulator mCovox;
};

void ATCreateDeviceSimCovox(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceSimCovox> p(new ATDeviceSimCovox);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefSimCovox = { "simcovox", nullptr, L"SimCovox", ATCreateDeviceSimCovox };

void *ATDeviceSimCovox::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceDiagnostics::kTypeID:
			return static_cast<IATDeviceDiagnostics *>(this);

		default:
			return ATDevice::AsInterface(id);
	}
}

void ATDeviceSimCovox::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefSimCovox;
}

void ATDeviceSimCovox::ColdReset() {
	mCovox.ColdReset();
}

void ATDeviceSimCovox::Init() {
	mCovox.Init(nullptr, GetService<IATDeviceSchedulingService>()->GetMachineScheduler(), GetService<IATAudioMixer>());

	if (!mpPIA) {
		mpPIA = GetService<IATDevicePIA>();
		mPIAOutput = mpPIA->AllocOutput(
			[](void *data, uint32 outputState) {
				((ATDeviceSimCovox *)data)->UpdateFromPIAOutput();
			},
			this,
			0xFF
		);

		UpdateFromPIAOutput();
	}
}

void ATDeviceSimCovox::Shutdown() {
	if (mpPIA) {
		mpPIA->FreeOutput(mPIAOutput);
		mpPIA = nullptr;
	}

	mCovox.Shutdown();
}

void ATDeviceSimCovox::DumpStatus(ATConsoleOutput& output) {
	mCovox.DumpStatus(output);
}

void ATDeviceSimCovox::UpdateFromPIAOutput() {
	mCovox.WriteMono(mpPIA->GetOutputState() & 0xFF);
}
