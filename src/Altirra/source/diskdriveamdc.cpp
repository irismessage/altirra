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
#include "audiosampleplayer.h"
#include "diskdriveAMDC.h"
#include "memorymanager.h"
#include "firmwaremanager.h"
#include "debuggerlog.h"

extern ATLogChannel g_ATLCDiskEmu;

void ATCreateDeviceDiskDriveAMDC(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveAMDC> p(new ATDeviceDiskDriveAMDC);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefDiskDriveAMDC = { "diskdriveamdc", "diskdriveamdc", L"Amdek AMDC-I/II disk drive (full emulation)", ATCreateDeviceDiskDriveAMDC };

///////////////////////////////////////////////////////////////////////////

void ATDeviceDiskDriveAMDC::Drive::OnDiskChanged(bool mediaRemoved) {
	mpParent->OnDiskChanged(mIndex, mediaRemoved);
}

void ATDeviceDiskDriveAMDC::Drive::OnWriteModeChanged() {
	mpParent->OnWriteModeChanged(mIndex);
}

void ATDeviceDiskDriveAMDC::Drive::OnTimingModeChanged() {
	mpParent->OnTimingModeChanged(mIndex);
}

void ATDeviceDiskDriveAMDC::Drive::OnAudioModeChanged() {
	mpParent->OnAudioModeChanged(mIndex);
}

bool ATDeviceDiskDriveAMDC::Drive::IsImageSupported(const IATDiskImage& image) const {
	return true;
}

///////////////////////////////////////////////////////////////////////////

// Base clock to the ACIA is 4MHz / 13, with /16 = 19320 baud.
// Firmware uses /1 in fast mode. We assume it's targeting 38.4KHz, so x8.
// Yes, this means that the ACIA clock is actually slower at high speed.
constexpr uint32 ATDeviceDiskDriveAMDC::kACIAClockNormal = 13 * 16;
constexpr uint32 ATDeviceDiskDriveAMDC::kACIAClockFast = 13 * 16 * 8;

ATDeviceDiskDriveAMDC::ATDeviceDiskDriveAMDC() {
	mBreakpointsImpl.BindBPHandler(mCoProc);
	mBreakpointsImpl.SetStepHandler(this);

	mACIA.SetDCD(false);

	mSerialCmdQueue.SetOnDriveCommandStateChanged(
		[this](bool asserted) {
			mACIA.SetCTS(asserted);
		}
	);

	mTargetHistoryProxy.Init(mCoProc);
	InitTargetControl(mTargetHistoryProxy, 1000000.0, kATDebugDisasmMode_6809, &mBreakpointsImpl, this);

	mFirmwareControl.Init(mROM, sizeof mROM, kATFirmwareType_AMDC);

	// Internal drives are always 40 track. The first drive is always present, second only on
	// AMDC-II.
	mDrives[0].mType = kDriveType_40Track;
	mDrives[0].mMaxHalfTrack = 90;
	mDrives[1].mMaxHalfTrack = 90;
}

ATDeviceDiskDriveAMDC::~ATDeviceDiskDriveAMDC() {
}

void *ATDeviceDiskDriveAMDC::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceScheduling::kTypeID: return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceFirmware::kTypeID: return static_cast<IATDeviceFirmware *>(&mFirmwareControl);
		case IATDeviceDiskDrive::kTypeID: return static_cast<IATDeviceDiskDrive *>(this);
		case IATDeviceSIO::kTypeID: return static_cast<IATDeviceSIO *>(this);
		case IATDeviceAudioOutput::kTypeID: return static_cast<IATDeviceAudioOutput *>(&mAudioPlayer);
		case ATFDCEmulator::kTypeID: return &mFDC;
	}

	void *p = ATDiskDriveDebugTargetControl::AsInterface(iid);
	if (p)
		return p;

	return ATDevice::AsInterface(iid);
}

void ATDeviceDiskDriveAMDC::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefDiskDriveAMDC;
}

void ATDeviceDiskDriveAMDC::GetSettingsBlurb(VDStringW& buf) {
	bool first = true;

	for(uint32 i=1; i<4; ++i) {
		if (mDrives[i].mType) {
			if (first)
				first = false;
			else
				buf += ',';

			buf.append_sprintf(L"D%u:", i + mDriveId + 1);
		}
	}
}

void ATDeviceDiskDriveAMDC::GetSettings(ATPropertySet& settings) {
	settings.SetBool("drive2", mDrives[1].mType != kDriveType_None);
	settings.SetUint32("switches", mSwitches);

	VDStringA s;
	for(uint32 i=0; i<2; ++i) {
		s.sprintf("extdrive%u", i);
		settings.SetUint32(s.c_str(), (uint32)mDrives[i + 2].mType);
	}
}

bool ATDeviceDiskDriveAMDC::SetSettings(const ATPropertySet& settings) {
	VDStringA s;
	bool change = false;

	DriveType d2type = settings.GetBool("drive2") ? kDriveType_40Track : kDriveType_None;

	if (mDrives[1].mType != d2type) {
		mDrives[1].mType = d2type;

		change = true;
	}


	for(uint32 i=0; i<2; ++i) {
		s.sprintf("extdrive%u", i);
		const uint32 driveTypeCode = settings.GetUint32(s.c_str(), i ? kDriveType_None : kDriveType_40Track);

		if (driveTypeCode <= kDriveType_80Track) {
			Drive& drive = mDrives[i + 2];

			if (drive.mType != driveTypeCode) {
				drive.mType = (DriveType)driveTypeCode;

				if (drive.mType == kDriveType_80Track)
					drive.mMaxHalfTrack = 180;
				else
					drive.mMaxHalfTrack = 90;

				change = true;
			}
		}
	}

	uint32 switches = settings.GetUint32("switches") & 0x1FF;

	if (mSwitches != switches) {
		mSwitches = switches;
		change = true;
	}

	uint32 newDriveId = (switches >> 4) & 3;

	if (mDriveId != newDriveId) {
		mDriveId = newDriveId;
		change = true;
	}

	return !change;
}

void ATDeviceDiskDriveAMDC::Init() {
	mSerialXmitQueue.Init(mpScheduler, mpSIOMgr);
	mSerialCmdQueue.Init(&mDriveScheduler, mpSIOMgr);

	uintptr *readmap = mCoProc.GetReadMap();
	uintptr *writemap = mCoProc.GetWriteMap();

	ATCoProcMemoryMapView mmap(readmap, writemap);

	// Initialize memory map (some of this is a guess)
	//
	// $0000-03FF	4K static RAM
	// $0400-07FF	4K static RAM (mirror)
	// $0800-0FFF	Unknown write strobe
	// $1000-17FF	Printer status port + internal jumper (read only)
	// $1800-1FFF	Printer data port (write only)
	// $2000-3FFF	MC6850 ACIA
	// $4000-5FFF	1797 FDC
	// $6000-7FFF	Control port (write only)
	// $8000		SW1-SW4 sense (read only)
	// $8001		SW5-SW8 sense (read only)
	// $8002-BFFF	Switches (mirror; read only)
	// $C000-DFFF	Arm watchdog? (write only)
	// $E000-EFFF	4K ROM (mirror)
	// $F000-FFFF	4K ROM

	mmap.Clear(mDummyRead, mDummyWrite);

	// map RAM to $0000-03FF, mirror to $0400-07FF
	mmap.SetMemory(0x00, 0x04, mRAM);
	mmap.SetMemory(0x04, 0x04, mRAM);

	// map hardware registers
	mReadNodePrinter.mpThis = this;
	mReadNodePrinter.mpRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDriveAMDC *)thisPtr)->OnPrinterRead(addr); };
	mReadNodePrinter.mpDebugRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDriveAMDC *)thisPtr)->OnPrinterRead(addr); };

	mWriteNodePrinter.mpThis = this;
	mWriteNodePrinter.mpWrite = [](uint32 addr, uint8 value, void *thisPtr) { ((ATDeviceDiskDriveAMDC *)thisPtr)->OnPrinterWrite(addr, value); };

	mReadNodeACIA.mpThis = this;
	mReadNodeACIA.mpRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDriveAMDC *)thisPtr)->OnACIARead(addr); };
	mReadNodeACIA.mpDebugRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDriveAMDC *)thisPtr)->OnACIADebugRead(addr); };

	mWriteNodeACIA.mpThis = this;
	mWriteNodeACIA.mpWrite = [](uint32 addr, uint8 value, void *thisPtr) { ((ATDeviceDiskDriveAMDC *)thisPtr)->OnACIAWrite(addr, value); };

	mReadNodeFDC.mpThis = this;
	mReadNodeFDC.mpRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDriveAMDC *)thisPtr)->OnFDCRead(addr); };
	mReadNodeFDC.mpDebugRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDriveAMDC *)thisPtr)->OnFDCDebugRead(addr); };

	mWriteNodeFDC.mpThis = this;
	mWriteNodeFDC.mpWrite = [](uint32 addr, uint8 value, void *thisPtr) { ((ATDeviceDiskDriveAMDC *)thisPtr)->OnFDCWrite(addr, value); };

	mReadNodeSwitches.mpThis = this;
	mReadNodeSwitches.mpRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDriveAMDC *)thisPtr)->OnSwitchRead(addr); };
	mReadNodeSwitches.mpDebugRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDriveAMDC *)thisPtr)->OnSwitchRead(addr); };

	mWriteNodeControl.mpThis = this;
	mWriteNodeControl.mpWrite = [](uint32 addr, uint8 value, void *thisPtr) { ((ATDeviceDiskDriveAMDC *)thisPtr)->OnControlWrite(addr, value); };

	mReadNodeWatchdog.mpThis = this;
	mReadNodeWatchdog.mpRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDriveAMDC *)thisPtr)->OnWatchdogRead(addr); };
	mReadNodeWatchdog.mpDebugRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDriveAMDC *)thisPtr)->OnWatchdogDebugRead(addr); };

	mmap.SetReadHandler(0x10, 0x08, mReadNodePrinter);
	mmap.SetWriteHandler(0x18, 0x08, mWriteNodePrinter);
	mmap.SetReadHandler(0x20, 0x20, mReadNodeACIA);
	mmap.SetWriteHandler(0x20, 0x20, mWriteNodeACIA);
	mmap.SetReadHandler(0x40, 0x40, mReadNodeFDC);
	mmap.SetWriteHandler(0x40, 0x40, mWriteNodeFDC);
	mmap.SetWriteHandler(0x60, 0x20, mWriteNodeControl);
	mmap.SetReadHandler(0x80, 0x40, mReadNodeSwitches);
	mmap.SetReadHandler(0xC0, 0x40, mReadNodeWatchdog);

	// map ROM to $E000-FFFF
	mmap.SetReadMem(0xE0, 0x10, mROM);
	mmap.SetReadMem(0xF0, 0x10, mROM);

	mDriveScheduler.SetRate(VDFraction(1000000, 1));

	mACIA.Init(&mDriveScheduler);
	mACIA.SetMasterClockPeriod(kACIAClockNormal);
	mACIA.SetTransmitFn([this](uint8 v, uint32 cyclesPerBit) { OnACIATransmit(v, cyclesPerBit); });

	// FDC in the AMDC-I/II is a 1797.
	mFDC.Init(&mDriveScheduler, 300.0f, 1.0f, ATFDCEmulator::kType_2797);

	mFDC.SetAutoIndexPulse(true);
	mFDC.SetOnDrqChange([this](bool drq) { OnFDCDataRequest(drq); });
	mFDC.SetOnStep([this](bool inward) { OnFDCStep(inward); });

	int driveIndex = 0;
	for(auto& drive : mDrives) {
		drive.mDiskChangeHandler.Init(mpScheduler);
		drive.mDiskChangeHandler.SetOutputStateFns(
			[driveIndex, this](std::optional<bool> wpState) {
				if (mSelectedDrive == driveIndex)
					mFDC.SetWriteProtectOverride(wpState);
			},
			[driveIndex, this](std::optional<bool> readyState) {
				if (mSelectedDrive == driveIndex)
					mFDC.SetDiskImageReady(readyState);
			}
		);

		drive.OnDiskChanged(false);

		drive.OnWriteModeChanged();
		drive.OnTimingModeChanged();
		drive.OnAudioModeChanged();

		++driveIndex;
	}

	UpdateRotationStatus();
}

void ATDeviceDiskDriveAMDC::Shutdown() {
	mAudioPlayer.Shutdown();
	mSerialXmitQueue.Shutdown();
	mSerialCmdQueue.Shutdown();

	ShutdownTargetControl();
	mFirmwareControl.Shutdown();

	if (mpSIOMgr) {
		mpSIOMgr->RemoveRawDevice(this);
		mpSIOMgr = nullptr;
	}

	for(auto& drive : mDrives) {
		drive.mDiskChangeHandler.Shutdown();

		if (drive.mpDiskInterface) {
			drive.mpDiskInterface->RemoveClient(&drive);
			drive.mpDiskInterface = nullptr;
		}
	}

	mpDiskDriveManager = nullptr;
}

uint32 ATDeviceDiskDriveAMDC::GetComputerPowerOnDelay() const {
	return 20;
}

void ATDeviceDiskDriveAMDC::WarmReset() {
}

void ATDeviceDiskDriveAMDC::ComputerColdReset() {
	WarmReset();
}

void ATDeviceDiskDriveAMDC::PeripheralColdReset() {
	memset(mRAM, 0xA5, sizeof mRAM);

	mACIA.Reset();
	mACIA.SetMasterClockPeriod(kACIAClockNormal);
	mFDC.Reset();

	mSerialXmitQueue.Reset();
	mSerialCmdQueue.Reset();

	mDriveScheduler.UnsetEvent(mpEventDriveTimeout);

	ResetTargetControl();

	SelectDrive(-1);
	
	mbForcedIndexPulse = false;

	// start the disk drive on a track other than 0/20/39, just to make things interesting
	for(Drive& drive : mDrives) {
		drive.mCurrentHalfTrack = 20;
		drive.mDiskChangeHandler.Reset();
	}

	mFDC.SetCurrentTrack(20, false);

	mbMotorRunning = false;
	mFDC.SetMotorRunning(false);
	mFDC.SetDensity(false);
	mFDC.SetWriteProtectOverride(false);
	mFDC.SetAutoIndexPulse(true);
	mFDC.SetSide(false);

	mCoProc.ColdReset();

	WarmReset();
}

void ATDeviceDiskDriveAMDC::InitDiskDrive(IATDiskDriveManager *ddm) {
	mpDiskDriveManager = ddm;
	mAvailableDrives = 0;

	uint32 diskInterfaceId = mDriveId;

	for(uint32 i=0; i<kNumDrives; ++i) {
		Drive& drive = mDrives[i];

		drive.mIndex = i;
		drive.mpParent = this;

		if (drive.mType) {
			drive.mpDiskInterface = ddm->GetDiskInterface(diskInterfaceId++);
			drive.mpDiskInterface->AddClient(&drive);

			mAvailableDrives |= (1 << i);
		}
	}
}

ATDeviceDiskDriveInterfaceClient ATDeviceDiskDriveAMDC::GetDiskInterfaceClient(uint32 index) {
	for(uint32 i=0; i<kNumDrives; ++i) {
		if (mDrives[i].mType && !index--)
			return { &mDrives[i], mDriveId + i };
	}

	return {};
}

void ATDeviceDiskDriveAMDC::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

void ATDeviceDiskDriveAMDC::OnScheduledEvent(uint32 id) {
	if (id == kEventId_DriveTimeout) {
		mpEventDriveTimeout = nullptr;

		mCoProc.AssertNmi();
	} else
		return ATDiskDriveDebugTargetControl::OnScheduledEvent(id);
}

void ATDeviceDiskDriveAMDC::OnCommandStateChanged(bool asserted) {
	if (mbCommandState != asserted) {
		mbCommandState = asserted;

		// Convert computer time to device time.
		//
		// We have a problem here because transmission is delayed by a byte time but we don't
		// necessarily know that delay when the command line is dropped. The XF551 has strict
		// requirements for the command line pulse because it needs about 77 machine cycles
		// from command asserted to start bit, but more importantly requires it to still be
		// asserted after the end of the last byte. To solve this, we assert /COMMAND
		// immediately but stretch the deassert a bit.

		const uint32 commandLatency = asserted ? 0 : 400;

		mSerialCmdQueue.AddCommandEdge(MasterTimeToDriveTime() + commandLatency, asserted);
	}
}

void ATDeviceDiskDriveAMDC::OnMotorStateChanged(bool asserted) {
}

void ATDeviceDiskDriveAMDC::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	Sync();

	mACIA.ReceiveByte(c, (cyclesPerBit * 100 + (179/2)) / 179);
}

void ATDeviceDiskDriveAMDC::OnSendReady() {
}

void ATDeviceDiskDriveAMDC::OnDiskChanged(uint32 index, bool mediaRemoved) {
	Drive& drive = mDrives[index];

	if (mediaRemoved)
		drive.mDiskChangeHandler.ChangeDisk();

	UpdateDiskStatus();
}

void ATDeviceDiskDriveAMDC::OnWriteModeChanged(uint32 index) {
	// FDC polls write protect state directly, so no action needed
}

void ATDeviceDiskDriveAMDC::OnTimingModeChanged(uint32 index) {
	if (mSelectedDrive == (int)index) {
		const bool accurateTiming = mDrives[index].mpDiskInterface->IsAccurateSectorTimingEnabled();

		mFDC.SetAccurateTimingEnabled(accurateTiming);
	}
}

void ATDeviceDiskDriveAMDC::OnAudioModeChanged(uint32 index) {
	if (mSelectedDrive == (int)index) {
		bool driveSounds = mDrives[index].mpDiskInterface->AreDriveSoundsEnabled();

		mbSoundsEnabled = driveSounds;

		UpdateRotationStatus();
	}
}

void ATDeviceDiskDriveAMDC::Sync() {
	uint32 limit = AccumSubCycles();

	for(;;) {
		if (!mCoProc.GetCyclesLeft()) {
			if (ATSCHEDULER_GETTIME(&mDriveScheduler) - limit < UINT32_C(0x80000000))
				break;

			ATSCHEDULER_ADVANCE(&mDriveScheduler);

			mCoProc.AddCycles(1);
		}

		mCoProc.Run();

		if (mCoProc.GetCyclesLeft()) {
			ScheduleImmediateResume();
			break;
		}
	}

	FlushStepNotifications();
}

uint8 ATDeviceDiskDriveAMDC::OnPrinterRead(uint32 addr) {
	// D7=1: Printer busy
	// D6=0: Printer present
	// D5=1: Second internal drive present
	// D4=0: Test mode?

	uint8 v = 0x10;

	if (mSwitches & 0x100)
		v = 0;

	if (mDrives[1].mType != kDriveType_None)
		v |= 0x20;

	return v;
}

void ATDeviceDiskDriveAMDC::OnPrinterWrite(uint32 addr, uint8 value) {
}

uint8 ATDeviceDiskDriveAMDC::OnACIADebugRead(uint32 addr) {
	return mACIA.DebugReadByte((uint8)addr);
}

uint8 ATDeviceDiskDriveAMDC::OnACIARead(uint32 addr) {
	return mACIA.ReadByte((uint8)addr);
}

void ATDeviceDiskDriveAMDC::OnACIAWrite(uint32 addr, uint8 value) {
	mACIA.WriteByte((uint8)addr, value);
}

uint8 ATDeviceDiskDriveAMDC::OnFDCDebugRead(uint32 addr) {
	return mFDC.DebugReadByte((uint8)addr);
}

uint8 ATDeviceDiskDriveAMDC::OnFDCRead(uint32 addr) {
	return mFDC.ReadByte((uint8)addr);
}

void ATDeviceDiskDriveAMDC::OnFDCWrite(uint32 addr, uint8 value) {
	mFDC.WriteByte((uint8)addr, value);
}

uint8 ATDeviceDiskDriveAMDC::OnSwitchRead(uint32 addr) {
	// $8000 reads switches 1-4 in D3:D0, inverted.
	// $8001 reads switches 5-8 in D3:D0, inverted.

	return (addr & 1) ? (uint8)((~mSwitches >> 4) | 0xF0) : (uint8)(~mSwitches | 0xF0);
}

void ATDeviceDiskDriveAMDC::OnControlWrite(uint32 addr, uint8 value) {
	// D7		Density setting (1 = FM, 0 = MFM)
	// D6		Unknown disk control
	// D5		Printer strobe
	// D4		Drive select / motor on
	// D3		High speed SIO (1 = high speed)
	// D1:D0	Drive index

	if (value & 0x10) {
		SetMotorEnabled(true);
		SelectDrive(value & 3);
	} else {
		SetMotorEnabled(false);
		SelectDrive(-1);
	}

	mFDC.SetDensity(!(value & 0x80));
	mACIA.SetMasterClockPeriod(value & 8 ? kACIAClockFast : kACIAClockNormal);
}

uint8 ATDeviceDiskDriveAMDC::OnWatchdogDebugRead(uint32 addr) const {
	return 0xFF;
}

uint8 ATDeviceDiskDriveAMDC::OnWatchdogRead(uint32 addr) {
	SetNmiTimeout();
	return 0xFF;
}

void ATDeviceDiskDriveAMDC::OnFDCDataRequest(bool asserted) {
	if (asserted)
		mCoProc.AssertFirq();
	else
		mCoProc.NegateFirq();
}

void ATDeviceDiskDriveAMDC::OnFDCStep(bool inward) {
	if (mSelectedDrive < 0)
		return;

	Drive& drive = mDrives[mSelectedDrive];

	if (inward) {
		// step in (increasing track number)
		if (drive.mCurrentHalfTrack < drive.mMaxHalfTrack) {
			drive.mCurrentHalfTrack += 2;

			mFDC.SetCurrentTrack(drive.mCurrentHalfTrack, drive.mCurrentHalfTrack == 0);
		}

		PlayStepSound();
	} else {
		// step out (decreasing track number)
		if (drive.mCurrentHalfTrack > 0) {
			drive.mCurrentHalfTrack -= 2;

			mFDC.SetCurrentTrack(drive.mCurrentHalfTrack, drive.mCurrentHalfTrack == 0);

			PlayStepSound();
		}
	}
}

void ATDeviceDiskDriveAMDC::OnACIATransmit(uint8 v, uint32 cyclesPerBit) {
	mSerialXmitQueue.AddTransmitByte(DriveTimeToMasterTime() + mSerialXmitQueue.kTransmitLatency, v, (cyclesPerBit * 179 + 50) / 100);
}

void ATDeviceDiskDriveAMDC::SetMotorEnabled(bool enabled) {
	if (mbMotorRunning != enabled) {
		mbMotorRunning = enabled;

		mFDC.SetMotorRunning(enabled);
		UpdateRotationStatus();
	}
}

void ATDeviceDiskDriveAMDC::PlayStepSound() {
	if (!mbSoundsEnabled)
		return;

	const uint32 t = ATSCHEDULER_GETTIME(&mDriveScheduler);
	
	if (t - mLastStepSoundTime > 50000)
		mLastStepPhase = 0;

	mAudioPlayer.PlayStepSound(kATAudioSampleId_DiskStep2, 0.3f + 0.7f * cosf((float)mLastStepPhase++ * nsVDMath::kfPi));

	mLastStepSoundTime = t;
}

void ATDeviceDiskDriveAMDC::UpdateRotationStatus() {
	if (mSelectedDrive >= 0) {
		const Drive& drive = mDrives[mSelectedDrive];

		drive.mpDiskInterface->SetShowMotorActive(mbMotorRunning);

		if (mbMotorRunning && mbSoundsEnabled) {
			mAudioPlayer.SetRotationSoundEnabled(true);
			return;
		}
	}

	mAudioPlayer.SetRotationSoundEnabled(false);
}

void ATDeviceDiskDriveAMDC::UpdateDiskStatus() {
	if (mSelectedDrive >= 0) {
		const Drive& drive = mDrives[mSelectedDrive];
		IATDiskImage *image = drive.mpDiskInterface->GetDiskImage();

		mFDC.SetDiskImage(image);

		drive.mDiskChangeHandler.ForceOutputUpdate();
	} else
		mFDC.SetDiskImage(nullptr, false);
}

void ATDeviceDiskDriveAMDC::SelectDrive(int index) {
	if (!(mAvailableDrives & (1 << index)))
		index = -1;

	if (mSelectedDrive == index)
		return;

	if (mSelectedDrive >= 0) {
		Drive& oldDrive = mDrives[mSelectedDrive];

		oldDrive.mpDiskInterface->SetShowMotorActive(false);
	}

	mSelectedDrive = index;

	if (index >= 0) {
		Drive& drive = mDrives[index];
		mFDC.SetDiskInterface(drive.mpDiskInterface);
		mFDC.SetCurrentTrack(drive.mCurrentHalfTrack, drive.mCurrentHalfTrack == 0);
		mFDC.SetSideMapping(ATFDCEmulator::SideMapping::Side2Forward, drive.mType == kDriveType_80Track ? 80 : 40);

		OnWriteModeChanged(index);
		OnTimingModeChanged(index);
		OnAudioModeChanged(index);
	} else {
		mFDC.SetDiskInterface(nullptr);
		mFDC.SetCurrentTrack(20, false);
	}

	UpdateDiskStatus();
	UpdateRotationStatus();
}

void ATDeviceDiskDriveAMDC::SetNmiTimeout() {
	// No basis for this value, it is a pure guess from the firmware usage
	const uint32 kTimeoutDelay = 1000000;

	mDriveScheduler.SetEvent(kTimeoutDelay, this, kEventId_DriveTimeout, mpEventDriveTimeout);
}
