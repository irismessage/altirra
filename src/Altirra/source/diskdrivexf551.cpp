//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2016 Avery Lee
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
#include <vd2/system/hash.h>
#include <vd2/system/int128.h>
#include <vd2/system/math.h>
#include <at/atcore/audiosource.h>
#include <at/atcore/logging.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/deviceserial.h>
#include <at/atcpu/memorymap.h>
#include "audiosampleplayer.h"
#include "diskdrivexf551.h"
#include "memorymanager.h"
#include "firmwaremanager.h"
#include "debuggerlog.h"

extern ATLogChannel g_ATLCDiskEmu;

void ATCreateDeviceDiskDriveXF551(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveXF551> p(new ATDeviceDiskDriveXF551);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefDiskDriveXF551 = { "diskdrivexf551", "diskdrivexf551", L"XF551 disk drive (full emulation)", ATCreateDeviceDiskDriveXF551 };

/////////////////////////////////////////////////////////////////////////////

ATDeviceDiskDriveXF551::ATDiskDriveDebugTargetProxyXF551::ATDiskDriveDebugTargetProxyXF551(ATDeviceDiskDriveXF551& parent)
	: mParent(parent)
{
}

std::pair<const uintptr *, const uintptr *> ATDeviceDiskDriveXF551::ATDiskDriveDebugTargetProxyXF551::GetReadWriteMaps() const {
	return { mParent.mDebugReadMap, mParent.mDebugWriteMap };
}

void ATDeviceDiskDriveXF551::ATDiskDriveDebugTargetProxyXF551::SetHistoryBuffer(ATCPUHistoryEntry *harray) {
	mParent.mCoProc.SetHistoryBuffer(harray);
}

uint32 ATDeviceDiskDriveXF551::ATDiskDriveDebugTargetProxyXF551::GetHistoryCounter() const {
	return mParent.mCoProc.GetHistoryCounter();
}
	
uint32 ATDeviceDiskDriveXF551::ATDiskDriveDebugTargetProxyXF551::GetTime() const {
	return mParent.mCoProc.GetTime();
}

uint32 ATDeviceDiskDriveXF551::ATDiskDriveDebugTargetProxyXF551::GetStepStackLevel() const {
	return mParent.mCoProc.GetStepStackLevel();
}

void ATDeviceDiskDriveXF551::ATDiskDriveDebugTargetProxyXF551::GetExecState(ATCPUExecState& state) const {
	mParent.mCoProc.GetExecState(state);
}

void ATDeviceDiskDriveXF551::ATDiskDriveDebugTargetProxyXF551::SetExecState(const ATCPUExecState& state) {
	mParent.mCoProc.SetExecState(state);
}

/////////////////////////////////////////////////////////////////////////////

ATDeviceDiskDriveXF551::ATDeviceDiskDriveXF551()
	: mTargetProxy(*this)
{
	mBreakpointsImpl.BindBPHandler(mCoProc);
	mBreakpointsImpl.SetStepHandler(this);

	mCoProc.SetProgramBanks(mROM[0], mROM[1]);

	// The 8048 runs T-states at 8.333MHz with 15 T-states per machine cycle,
	// with the majority of instructions only 1 or 2 machine cycles. To keep
	// accounting simple we run the drive coprocessor clock at 8.333MHz / 15,
	// but need to report 8.333MHz as the displayed clock speed.
	InitTargetControl(mTargetProxy, 8333333.0 / 15.0, kATDebugDisasmMode_8048, &mBreakpointsImpl, this);
	ApplyDisplayCPUClockMultiplier(15.0);
	
	mSerialCmdQueue.SetOnDriveCommandStateChanged(
		[this](bool bit) {
			if (bit)
				mCoProc.AssertIrq();
			else
				mCoProc.NegateIrq();
		}
	);
}

ATDeviceDiskDriveXF551::~ATDeviceDiskDriveXF551() {
}

void *ATDeviceDiskDriveXF551::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceScheduling::kTypeID: return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceFirmware::kTypeID: return static_cast<IATDeviceFirmware *>(this);
		case IATDeviceDiskDrive::kTypeID: return static_cast<IATDeviceDiskDrive *>(this);
		case IATDeviceSIO::kTypeID: return static_cast<IATDeviceSIO *>(this);
		case IATDeviceAudioOutput::kTypeID: return static_cast<IATDeviceAudioOutput *>(&mAudioPlayer);
		case ATFDCEmulator::kTypeID: return &mFDC;
	}

	return ATDiskDriveDebugTargetControl::AsInterface(iid);
}

void ATDeviceDiskDriveXF551::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefDiskDriveXF551;
}

void ATDeviceDiskDriveXF551::GetSettingsBlurb(VDStringW& buf) {
	buf.sprintf(L"D%u:", mDriveId + 1);
}

void ATDeviceDiskDriveXF551::GetSettings(ATPropertySet& settings) {
	settings.SetUint32("id", mDriveId);
}

bool ATDeviceDiskDriveXF551::SetSettings(const ATPropertySet& settings) {
	uint32 newDriveId = settings.GetUint32("id", mDriveId) & 3;

	if (mDriveId != newDriveId) {
		mDriveId = newDriveId;
		return false;
	}

	return true;
}

void ATDeviceDiskDriveXF551::Init() {
	mDriveScheduler.SetRate(VDFraction(8333333, 15));

	// We need to do this early to ensure that the clock divisor is set before we perform init processing.
	ResetTargetControl();

	mSerialXmitQueue.Init(mpScheduler, mpSIOMgr);
	mSerialCmdQueue.Init(&mDriveScheduler, mpSIOMgr);

	mCoProc.SetPortReadHandler([this](uint8 port, uint8 output) -> uint8 { return OnReadPort(port, output); });
	mCoProc.SetPortWriteHandler([this](uint8 port, uint8 data) { OnWritePort(port, data); });
	mCoProc.SetXRAMReadHandler([this](uint8 addr) -> uint8 { return OnReadXRAM(); });
	mCoProc.SetXRAMWriteHandler([this](uint8 addr, uint8 data) { OnWriteXRAM(data); });
	mCoProc.SetT0ReadHandler([this]() { return OnReadT0(); });
	mCoProc.SetT1ReadHandler([this]() { return OnReadT1(); });

	// The XF551 runs its FDC at 300/288 of normal to compensate for the faster rotational speed.
	// This causes all timings to be correspondingly reduced.
	mFDC.Init(&mDriveScheduler, 300.0f, 288.0f/300.0f, ATFDCEmulator::kType_1770);
	mFDC.SetAutoIndexPulse(true);
	mFDC.SetDiskInterface(mpDiskInterface);
	mFDC.SetOnDrqChange([this](bool drq) {  });
	mFDC.SetOnIrqChange([this](bool irq) {  });
	mFDC.SetOnStep([this](bool inward) { OnFDCStep(inward); });
	mFDC.SetOnMotorChange([this](bool active) { OnFDCMotorChange(active); });

	mDriveScheduler.UnsetEvent(mpEventDriveDiskChange);
	mDiskChangeState = 0;
	OnDiskChanged(false);

	OnWriteModeChanged();
	OnTimingModeChanged();
	OnAudioModeChanged();

	UpdateRotationStatus();

	// Unlike the other coprocessor types, we don't actually need read/write maps. This just
	// makes our life easier as we can leverage the default impl.
	ATCoProcMemoryMapView mmap(mDebugReadMap, mDebugWriteMap);

	mmap.Clear(mDummyRead, mDummyWrite);
	mmap.SetReadMem(0x00, 0x08, mROM[0]);
	mmap.SetReadMem(0x08, 0x08, mROM[1]);
	mmap.SetMemory(0x10, 0x01, mCoProc.GetInternalRAM());
}

void ATDeviceDiskDriveXF551::Shutdown() {
	mAudioPlayer.Shutdown();

	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);

	ShutdownTargetControl();

	mpFwMgr = nullptr;

	mSerialCmdQueue.Shutdown();
	mSerialXmitQueue.Shutdown();

	if (mpSIOMgr) {
		mpSIOMgr->RemoveRawDevice(this);
		mpSIOMgr = nullptr;
	}

	if (mpDiskInterface) {
		mpDiskInterface->RemoveClient(this);
		mpDiskInterface = nullptr;
	}

	mpDiskDriveManager = nullptr;
}

void ATDeviceDiskDriveXF551::WarmReset() {
	// If the computer resets, its transmission is interrupted.
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
}

void ATDeviceDiskDriveXF551::ComputerColdReset() {
	WarmReset();
}

void ATDeviceDiskDriveXF551::PeripheralColdReset() {
	mFDC.Reset();

	mSerialXmitQueue.Reset();
	mSerialCmdQueue.Reset();
	
	mActiveStepperPhases = 0;

	mbForcedIndexPulse = false;

	mbDirectReceiveOutput = true;
	mbDirectTransmitOutput = true;

	// start the disk drive on a track other than 0/20/39, just to make things interesting
	mCurrentTrack = 20;
	mFDC.SetCurrentTrack(mCurrentTrack, false);
	mFDC.SetSideMapping(ATFDCEmulator::SideMapping::Side2Reversed, 40);

	mbMotorRunning = false;
	mFDC.SetMotorRunning(false);
	mFDC.SetDensity(false);
	mFDC.SetWriteProtectOverride(false);

	mbExtendedRAMEnabled = false;

	mCoProc.ColdReset();

	ResetTargetControl();

	WarmReset();
}

void ATDeviceDiskDriveXF551::InitFirmware(ATFirmwareManager *fwman) {
	mpFwMgr = fwman;

	ReloadFirmware();
}

bool ATDeviceDiskDriveXF551::ReloadFirmware() {
	const uint64 id = mpFwMgr->GetFirmwareOfType(kATFirmwareType_XF551, true);
	
	const vduint128 oldHash = VDHash128(mROM, sizeof mROM);

	uint8 firmware[4096] = {};

	uint32 len = 0;
	mpFwMgr->LoadFirmware(id, firmware, 0, sizeof firmware, nullptr, &len, nullptr, nullptr, &mbFirmwareUsable);

	memcpy(mROM[0], firmware, 2048);
	memcpy(mROM[1], firmware + 2048, 2048);
	mROM[0][2048] = mROM[0][0];
	mROM[0][2049] = mROM[0][0];
	mROM[1][2048] = mROM[1][0];
	mROM[1][2049] = mROM[1][0];

	const vduint128 newHash = VDHash128(mROM, sizeof mROM);

	return oldHash != newHash;
}

const wchar_t *ATDeviceDiskDriveXF551::GetWritableFirmwareDesc(uint32 idx) const {
	return nullptr;
}

bool ATDeviceDiskDriveXF551::IsWritableFirmwareDirty(uint32 idx) const {
	return false;
}

void ATDeviceDiskDriveXF551::SaveWritableFirmware(uint32 idx, IVDStream& stream) {
}

ATDeviceFirmwareStatus ATDeviceDiskDriveXF551::GetFirmwareStatus() const {
	return mbFirmwareUsable ? ATDeviceFirmwareStatus::OK : ATDeviceFirmwareStatus::Missing;
}

void ATDeviceDiskDriveXF551::InitDiskDrive(IATDiskDriveManager *ddm) {
	mpDiskDriveManager = ddm;
	mpDiskInterface = ddm->GetDiskInterface(mDriveId);
	mpDiskInterface->AddClient(this);
}

ATDeviceDiskDriveInterfaceClient ATDeviceDiskDriveXF551::GetDiskInterfaceClient(uint32 index) {
	return index ? ATDeviceDiskDriveInterfaceClient{} : ATDeviceDiskDriveInterfaceClient{ this, mDriveId };
}

void ATDeviceDiskDriveXF551::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

void ATDeviceDiskDriveXF551::OnScheduledEvent(uint32 id) {
	if (id == kEventId_DriveReceiveBit) {
		mReceiveShiftRegister >>= 1;
		mpEventDriveReceiveBit = nullptr;

		if (mReceiveShiftRegister >= 2) {
			mReceiveTimingAccum += mReceiveTimingStep;
			mpEventDriveReceiveBit = mDriveScheduler.AddEvent(mReceiveTimingAccum >> 10, this, kEventId_DriveReceiveBit);
			mReceiveTimingAccum &= 0x3FF;
		}
	} else if (id == kEventId_DriveDiskChange) {
		mpEventDriveDiskChange = nullptr;

		switch(++mDiskChangeState) {
			case 1:		// disk being removed (write protect covered)
			case 2:		// disk removed (write protect clear)
			case 3:		// disk being inserted (write protect covered)
				mDriveScheduler.SetEvent(kDiskChangeStepMS, this, kEventId_DriveDiskChange, mpEventDriveDiskChange);
				break;

			case 4:		// disk inserted (write protect normal)
				mDiskChangeState = 0;
				break;
		}

		UpdateDiskStatus();
	} else
		return ATDiskDriveDebugTargetControl::OnScheduledEvent(id);
}

void ATDeviceDiskDriveXF551::OnCommandStateChanged(bool asserted) {
	if (mbCommandState != asserted) {
		mbCommandState = asserted;

		AddCommandEdge(asserted);
	}
}

void ATDeviceDiskDriveXF551::OnMotorStateChanged(bool asserted) {
}

void ATDeviceDiskDriveXF551::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	Sync();

	mReceiveShiftRegister = (c + c + 0x200) * 2 + 1;

	// The conversion fraction we need here is 512/1649, but that denominator is awkward.
	// Approximate it with 318/1024.
	mReceiveTimingAccum = 0x200;
	mReceiveTimingStep = cyclesPerBit * 318;

	mDriveScheduler.SetEvent(1, this, kEventId_DriveReceiveBit, mpEventDriveReceiveBit);
}

void ATDeviceDiskDriveXF551::OnSendReady() {
}

void ATDeviceDiskDriveXF551::OnDiskChanged(bool mediaRemoved) {
	if (mediaRemoved) {
		mDiskChangeState = 0;
		mDriveScheduler.SetEvent(1, this, kEventId_DriveDiskChange, mpEventDriveDiskChange);
	}

	UpdateDiskStatus();
}

void ATDeviceDiskDriveXF551::OnWriteModeChanged() {
	UpdateWriteProtectStatus();
}

void ATDeviceDiskDriveXF551::OnTimingModeChanged() {
	mFDC.SetAccurateTimingEnabled(mpDiskInterface->IsAccurateSectorTimingEnabled());
}

void ATDeviceDiskDriveXF551::OnAudioModeChanged() {
	mbSoundsEnabled = mpDiskInterface->AreDriveSoundsEnabled();

	UpdateRotationStatus();
}

bool ATDeviceDiskDriveXF551::IsImageSupported(const IATDiskImage& image) const {
	const auto& geo = image.GetGeometry();

	return geo.mTrackCount <= 80 && geo.mSectorSize <= 256 && geo.mSectorsPerTrack <= 26 && !geo.mbHighDensity;
}

void ATDeviceDiskDriveXF551::Sync() {
	const uint32 limit = AccumSubCycles();

	mDriveScheduler.SetStopTime(limit);

	for(;;) {
		if (!mCoProc.GetCyclesLeft()) {
			uint32 tc = ATSCHEDULER_GETTIMETONEXT(&mDriveScheduler);
			if (tc <= 0)
				break;

			uint32 tc2 = mCoProc.GetTStatesPending();

			if (!tc2)
				tc2 = 1;

			if (tc > tc2)
				tc = tc2;

			ATSCHEDULER_ADVANCE_N(&mDriveScheduler, tc);
			mCoProc.AddCycles(tc);
		}

		mCoProc.Run();

		if (mCoProc.GetCyclesLeft()) {
			ScheduleImmediateResume();
			break;
		}
	}

	FlushStepNotifications();
}

void ATDeviceDiskDriveXF551::AddTransmitEdge(bool polarity) {
	mSerialXmitQueue.AddTransmitBit(DriveTimeToMasterTime() + mSerialXmitQueue.kTransmitLatency, polarity);
}

void ATDeviceDiskDriveXF551::AddCommandEdge(uint32 polarity) {
	// Convert computer time to device time.
	//
	// We have a problem here because transmission is delayed by a byte time but we don't
	// necessarily know that delay when the command line is dropped. The XF551 has strict
	// requirements for the command line pulse because it needs about 77 machine cycles
	// from command asserted to start bit, but more importantly requires it to still be
	// asserted after the end of the last byte. To solve this, we assert /COMMAND
	// immediately but stretch the deassert a bit.

	const uint32 commandLatency = polarity ? 0 : 400;

	mSerialCmdQueue.AddCommandEdge(MasterTimeToDriveTime() + commandLatency, polarity);
}

void ATDeviceDiskDriveXF551::OnFDCStep(bool inward) {
	if (inward) {
		// step in (increasing track number)
		if (mCurrentTrack < 90U) {
			mCurrentTrack += 2;

			mFDC.SetCurrentTrack(mCurrentTrack, mCurrentTrack == 0);
		}

		PlayStepSound();
	} else {
		// step out (decreasing track number)
		if (mCurrentTrack > 0) {
			mCurrentTrack -= 2;

			mFDC.SetCurrentTrack(mCurrentTrack, mCurrentTrack == 0);

			PlayStepSound();
		}
	}
}

void ATDeviceDiskDriveXF551::OnFDCMotorChange(bool enabled) {
	if (mbMotorRunning != enabled) {
		mbMotorRunning = enabled;

		mFDC.SetMotorRunning(enabled);
		UpdateRotationStatus();
	}
}

bool ATDeviceDiskDriveXF551::OnReadT0() const {
	// T0 = inverted SIO DATA OUT (computer -> peripheral).
	return !(mReceiveShiftRegister & 1);
}

bool ATDeviceDiskDriveXF551::OnReadT1() const {
	// T1 = DRQ
	return mFDC.GetDrqStatus();
}

uint8 ATDeviceDiskDriveXF551::OnReadPort(uint8 addr, uint8 output) {
	if (addr == 1)
		return output & (0x3F | (mDriveId << 6));

	uint8 v = output;

	if (!mFDC.GetIrqStatus())
		v &= 0xFB;

	return v;
}

void ATDeviceDiskDriveXF551::OnWritePort(uint8 addr, uint8 output) {
	if (addr == 0) {
		// P1:
		//	D1:D0	Output: FDC address
		//	D2		Input: FDC interrupt
		//	D3		Output: FDC density (1 = FM)
		//	D4		Output: FDC reset
		//	D5		Output: FDC read/write (1 = read)
		//	D6		Output: FDC side select
		//	D7		Output: SIO DATA IN

		mFDC.SetDensity(!(output & 8));
		mFDC.SetSide((output & 0x40) != 0);

		if (!(output & 0x10))
			mFDC.Reset();

		bool directTransmitOutput = !(output & 0x80);
		if (mbDirectTransmitOutput != directTransmitOutput) {
			mbDirectTransmitOutput = directTransmitOutput;
			AddTransmitEdge(directTransmitOutput);
		}
	} else {
		// P2:
		//	D7:D6	Input: Drive select
	}
}

uint8 ATDeviceDiskDriveXF551::OnReadXRAM() {
	// check if write line on FDC is inactive
	const uint8 p1 = mCoProc.GetPort1Output();

	if (!(p1 & 0x20))
		return 0xFF;

	return mFDC.ReadByte(p1 & 0x03);
}

void ATDeviceDiskDriveXF551::OnWriteXRAM(uint8 val) {
	// check if write line on FDC is active
	const uint8 p1 = mCoProc.GetPort1Output();
	if (p1 & 0x20)
		return;

	mFDC.WriteByte(p1 & 0x03, val);
}

void ATDeviceDiskDriveXF551::PlayStepSound() {
	if (!mbSoundsEnabled)
		return;

	const uint32 t = ATSCHEDULER_GETTIME(&mDriveScheduler);
	
	if (t - mLastStepSoundTime > 50000)
		mLastStepPhase = 0;

	mAudioPlayer.PlayStepSound(kATAudioSampleId_DiskStep2, 0.3f + 0.7f * cosf((float)mLastStepPhase++ * nsVDMath::kfPi));

	mLastStepSoundTime = t;
}

void ATDeviceDiskDriveXF551::UpdateRotationStatus() {
	mpDiskInterface->SetShowMotorActive(mbMotorRunning);

	mAudioPlayer.SetRotationSoundEnabled(mbMotorRunning && mbSoundsEnabled);
}

void ATDeviceDiskDriveXF551::UpdateDiskStatus() {
	IATDiskImage *image = mpDiskInterface->GetDiskImage();

	mFDC.SetDiskImage(image, (image != nullptr && mDiskChangeState == 0));

	UpdateWriteProtectStatus();
}

void ATDeviceDiskDriveXF551::UpdateWriteProtectStatus() {
	const bool wpoverride = (mDiskChangeState & 1) != 0;

	mFDC.SetWriteProtectOverride(wpoverride);
}
