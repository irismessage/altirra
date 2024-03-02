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
#include "diskdriveindusgt.h"
#include "memorymanager.h"
#include "firmwaremanager.h"
#include "debuggerlog.h"

extern ATLogChannel g_ATLCDiskEmu;

void ATCreateDeviceDiskDriveIndusGT(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveIndusGT> p(new ATDeviceDiskDriveIndusGT);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefDiskDriveIndusGT = { "diskdriveindusgt", "diskdriveindusgt", L"Indus GT disk drive (full emulation)", ATCreateDeviceDiskDriveIndusGT };

ATDeviceDiskDriveIndusGT::ATDeviceDiskDriveIndusGT() {
	mBreakpointsImpl.BindBPHandler(mCoProc);
	mBreakpointsImpl.SetStepHandler(this);

	mDriveScheduler.SetRate(VDFraction(4000000, 1));

	mSerialCmdQueue.SetOnDriveCommandStateChanged(
		[this](bool asserted) {
			if (asserted)
				mCoProc.AssertIrq();
		}
	);

	mTargetProxy.Init(mCoProc);
	InitTargetControl(mTargetProxy, 4000000.0, kATDebugDisasmMode_Z80, &mBreakpointsImpl, this);

	mFirmwareControl.Init(mROM, sizeof mROM, kATFirmwareType_IndusGT);
}

ATDeviceDiskDriveIndusGT::~ATDeviceDiskDriveIndusGT() {
}

void *ATDeviceDiskDriveIndusGT::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceFirmware::kTypeID: return static_cast<IATDeviceFirmware *>(&mFirmwareControl);
		case IATDeviceDiskDrive::kTypeID: return static_cast<IATDeviceDiskDrive *>(this);
		case IATDeviceSIO::kTypeID: return static_cast<IATDeviceSIO *>(this);
		case IATDeviceAudioOutput::kTypeID: return static_cast<IATDeviceAudioOutput *>(this);
		case IATDeviceButtons::kTypeID: return static_cast<IATDeviceButtons *>(this);
		case ATFDCEmulator::kTypeID: return &mFDC;
	}

	void *p = ATDiskDriveDebugTargetControl::AsInterface(iid);
	if (p)
		return p;

	return ATDevice::AsInterface(iid);
}

void ATDeviceDiskDriveIndusGT::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefDiskDriveIndusGT;
}

void ATDeviceDiskDriveIndusGT::GetSettingsBlurb(VDStringW& buf) {
	buf.sprintf(L"D%u:", mDriveId + 1);
}

void ATDeviceDiskDriveIndusGT::GetSettings(ATPropertySet& settings) {
	settings.SetUint32("id", mDriveId);
}

bool ATDeviceDiskDriveIndusGT::SetSettings(const ATPropertySet& settings) {
	uint32 newDriveId = settings.GetUint32("id", mDriveId) & 3;

	if (mDriveId != newDriveId) {
		mDriveId = newDriveId;
		return false;
	}

	return true;
}

void ATDeviceDiskDriveIndusGT::Init() {
	mSerialXmitQueue.Init(mpScheduler, mpSIOMgr);
	mSerialCmdQueue.Init(&mDriveScheduler, mpSIOMgr);

	// The Indus GT memory map:
	//
	//	0000-0FFF	ROM
	//	1000-1FFF	Status 1 (read only)
	//	2000-2FFF	Status 2 (read only)
	//	3000-3FFF	Control (write only)
	//	4000-5FFF	LED display
	//	6000-6FFF	FDC
	//	7000-7FFF	RAM (2K mirrored)

	uintptr *readmap = mCoProc.GetReadMap();
	uintptr *writemap = mCoProc.GetWriteMap();

	// preset the maps to NC
	for(int i=0; i<256; ++i) {
		readmap[i] = (uintptr)mDummyRead - (i << 8);
		writemap[i] = (uintptr)mDummyWrite - (i << 8);
	}

	// set up ROM
	std::fill(readmap, readmap + 0x10, (uintptr)mROM);

	// set up status 1 (side effects based on A0)
	mReadNodeStat1.mpThis = this;
	mReadNodeStat1.mpRead = [](uint32 addr, void *thisptr0) { return ((ATDeviceDiskDriveIndusGT *)thisptr0)->OnReadStatus1((uint8)addr); };
	mReadNodeStat1.mpDebugRead = [](uint32 addr, void *thisptr0) { return ((ATDeviceDiskDriveIndusGT *)thisptr0)->OnDebugReadStatus1(); };
	std::fill(readmap + 0x10, readmap + 0x20, mReadNodeStat1.AsBase());

	// set up status 2 (no side effects)
	mReadNodeStat2.mpThis = this;
	mReadNodeStat2.mpRead = [](uint32 addr, void *thisptr0) { return ((ATDeviceDiskDriveIndusGT *)thisptr0)->OnReadStatus2(); };
	mReadNodeStat2.mpDebugRead = mReadNodeStat2.mpRead;
	std::fill(readmap + 0x20, readmap + 0x30, mReadNodeStat2.AsBase());

	// set up control
	mReadNodeControl.mpThis = this;
	mReadNodeControl.mpRead = [](uint32 addr, void *thisptr0) { return ((ATDeviceDiskDriveIndusGT *)thisptr0)->OnReadControl(); };
	mReadNodeControl.mpDebugRead = [](uint32 addr, void *thisptr0) -> uint8 { return 0xFF; };
	std::fill(readmap + 0x30, readmap + 0x40, mReadNodeControl.AsBase());

	mWriteNodeControl.mpThis = this;
	mWriteNodeControl.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) { ((ATDeviceDiskDriveIndusGT *)thisptr0)->OnWriteControl(val); };
	std::fill(writemap + 0x30, writemap + 0x40, mWriteNodeControl.AsBase());

	mReadNodeControlLED1.mpThis = this;
	mReadNodeControlLED1.mpRead = [](uint32 addr, void *thisptr0) { return ((ATDeviceDiskDriveIndusGT *)thisptr0)->OnReadControlLED1(); };
	mReadNodeControlLED1.mpDebugRead = [](uint32 addr, void *thisptr0) -> uint8 { return 0xFF; };
	std::fill(readmap + 0x40, readmap + 0x50, mReadNodeControlLED1.AsBase());

	mWriteNodeControlLED1.mpThis = this;
	mWriteNodeControlLED1.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) { ((ATDeviceDiskDriveIndusGT *)thisptr0)->OnWriteControlLED1(val); };
	std::fill(writemap + 0x40, writemap + 0x50, mWriteNodeControlLED1.AsBase());

	mReadNodeControlLED2.mpThis = this;
	mReadNodeControlLED2.mpRead = [](uint32 addr, void *thisptr0) { return ((ATDeviceDiskDriveIndusGT *)thisptr0)->OnReadControlLED2(); };
	mReadNodeControlLED2.mpDebugRead = [](uint32 addr, void *thisptr0) -> uint8 { return 0xFF; };
	std::fill(readmap + 0x50, readmap + 0x60, mReadNodeControlLED2.AsBase());

	mWriteNodeControlLED2.mpThis = this;
	mWriteNodeControlLED2.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) { ((ATDeviceDiskDriveIndusGT *)thisptr0)->OnWriteControlLED2(val); };
	std::fill(writemap + 0x50, writemap + 0x60, mWriteNodeControlLED2.AsBase());

	// set up FDC
	mReadNodeFDC.mpThis = this;
	mReadNodeFDC.mpRead = [](uint32 addr, void *thisptr0) { return ((ATDeviceDiskDriveIndusGT *)thisptr0)->OnReadFDC(addr); };
	mReadNodeFDC.mpDebugRead = [](uint32 addr, void *thisptr0) { return ((ATDeviceDiskDriveIndusGT *)thisptr0)->OnDebugReadFDC(addr); };
	std::fill(readmap + 0x60, readmap + 0x70, mReadNodeFDC.AsBase());

	mWriteNodeFDC.mpThis = this;
	mWriteNodeFDC.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) { ((ATDeviceDiskDriveIndusGT *)thisptr0)->OnWriteFDC(addr, val); };
	std::fill(writemap + 0x60, writemap + 0x70, mWriteNodeFDC.AsBase());

	// set up RAM
	std::fill(readmap + 0x70, readmap + 0x78, (uintptr)mRAM - 0x7000);
	std::fill(readmap + 0x78, readmap + 0x80, (uintptr)mRAM - 0x7800);
	std::fill(writemap + 0x70, writemap + 0x78, (uintptr)mRAM - 0x7000);
	std::fill(writemap + 0x78, writemap + 0x80, (uintptr)mRAM - 0x7800);

	// set up RAMCharger RAM
	std::fill(readmap + 0x80, readmap + 0x100, (uintptr)mRAM + 0x0800);
	std::fill(writemap + 0x80, writemap + 0x100, (uintptr)mRAM + 0x0800);

	// back up the memory maps for bank switching purposes
	memcpy(mReadMapBackup, readmap, sizeof(mReadMapBackup));
	memcpy(mWriteMapBackup, writemap, sizeof(mWriteMapBackup));

	// set up port mapping
	mCoProc.SetPortReadHandler([this](uint8 port) -> uint8 { OnAccessPort(port); return 0xFF; });
	mCoProc.SetPortWriteHandler([this](uint8 port, uint8 data) { OnAccessPort(port); });

	mFDC.Init(&mDriveScheduler, 288.0f, 1.0f, ATFDCEmulator::kType_2793);
	mFDC.SetDiskInterface(mpDiskInterface);
	mFDC.SetOnDrqChange([](bool drq) { });
	mFDC.SetOnIrqChange([](bool irq) { });

	mDriveScheduler.UnsetEvent(mpEventDriveDiskChange);
	mDiskChangeState = 0;
	OnDiskChanged(false);

	OnWriteModeChanged();
	OnTimingModeChanged();
	OnAudioModeChanged();

	UpdateRotationStatus();
}

void ATDeviceDiskDriveIndusGT::Shutdown() {
	mAudioRawSource.Shutdown();
	mAudioPlayer.Shutdown();
	mSerialCmdQueue.Shutdown();
	mSerialXmitQueue.Shutdown();

	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);

	ShutdownTargetControl();

	mFirmwareControl.Shutdown();

	if (mpSIOMgr) {
		mpSIOMgr->RemoveRawDevice(this);
		mpSIOMgr = nullptr;
	}

	if (mpDiskInterface) {
		mpDiskInterface->SetShowLEDReadout(-1);
		mpDiskInterface->RemoveClient(this);
		mpDiskInterface = nullptr;
	}

	mpDiskDriveManager = nullptr;
}

void ATDeviceDiskDriveIndusGT::WarmReset() {
	if (mpSIOMgr->IsSIOReadyAsserted())
		mStatus2 |= 0x10;
	else
		mStatus2 &= ~0x10;

	// If the computer resets, its transmission is interrupted.
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
}

void ATDeviceDiskDriveIndusGT::ComputerColdReset() {
	WarmReset();
}

void ATDeviceDiskDriveIndusGT::PeripheralColdReset() {
	memset(mRAM, 0xFF, sizeof mRAM);

	mFDC.Reset();

	mSerialXmitQueue.Reset();

	mLEDState = 0;
	
	mStatus1 = 0x7C + mDriveId;

	mStatus2 = 0xFF;
	mActiveStepperPhases = 0;

	mbForcedIndexPulse = false;

	mbDirectReceiveOutput = true;
	mbDirectTransmitOutput = true;

	// start the disk drive on a track other than 0/20/39, just to make things interesting
	mCurrentTrack = 20;
	mFDC.SetCurrentTrack(mCurrentTrack, true);

	mbMotorRunning = false;
	mFDC.SetMotorRunning(false);
	mFDC.SetDensity(false);
	mFDC.SetWriteProtectOverride(false);

	mAudioRawSource.SetOutput(ATSCHEDULER_GETTIME(mpScheduler), 0);

	mbExtendedRAMEnabled = false;
	UpdateMemoryMap();

	mCoProc.ColdReset();

	ResetTargetControl();

	WarmReset();
}

void ATDeviceDiskDriveIndusGT::InitDiskDrive(IATDiskDriveManager *ddm) {
	mpDiskDriveManager = ddm;
	mpDiskInterface = ddm->GetDiskInterface(mDriveId);
	mpDiskInterface->AddClient(this);
}

ATDeviceDiskDriveInterfaceClient ATDeviceDiskDriveIndusGT::GetDiskInterfaceClient(uint32 index) {
	return index ? ATDeviceDiskDriveInterfaceClient{} : ATDeviceDiskDriveInterfaceClient{this, mDriveId};
}

void ATDeviceDiskDriveIndusGT::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

void ATDeviceDiskDriveIndusGT::InitAudioOutput(IATAudioMixer *mixer) {
	mAudioPlayer.InitAudioOutput(mixer);
	mAudioRawSource.Init(mixer);
}

uint32 ATDeviceDiskDriveIndusGT::GetSupportedButtons() const {
	return (1U << kATDeviceButton_IndusGTTrack)
		| (1U << kATDeviceButton_IndusGTId)
		| (1U << kATDeviceButton_IndusGTError)
		| (1U << kATDeviceButton_IndusGTBootCPM)
		| (1U << kATDeviceButton_IndusGTChangeDensity)
		;
}

bool ATDeviceDiskDriveIndusGT::IsButtonDepressed(ATDeviceButton idx) const {
	switch(idx) {
		case kATDeviceButton_IndusGTTrack:
			return (mStatus1ButtonsHeld & 0x10) != 0;

		case kATDeviceButton_IndusGTId:
			return (mStatus1ButtonsHeld & 0x20) != 0;

		case kATDeviceButton_IndusGTError:
			return (mStatus1ButtonsHeld & 0x40) != 0;

		default:
			return false;
	}
}

void ATDeviceDiskDriveIndusGT::ActivateButton(ATDeviceButton idx, bool state) {
	switch(idx) {
		case kATDeviceButton_IndusGTTrack:
			if (state) {
				mStatus1ButtonsHeld |= 0x10;
				mStatus1 |= 0x10;
			} else
				mStatus1ButtonsHeld &= ~0x10;
			break;

		case kATDeviceButton_IndusGTId:
			if (state) {
				mStatus1ButtonsHeld |= 0x20;
				mStatus1 |= 0x20;
			} else
				mStatus1ButtonsHeld &= ~0x20;
			break;

		case kATDeviceButton_IndusGTError:
			if (state) {
				mStatus1ButtonsHeld |= 0x40;
				mStatus1 |= 0x40;
			} else
				mStatus1ButtonsHeld &= ~0x40;
			break;

		case kATDeviceButton_IndusGTBootCPM:
			if (state)
				mStatus1CPMHoldStart = mDriveScheduler.GetTick64();
			break;

		case kATDeviceButton_IndusGTChangeDensity:
			if (state)
				mStatus1ChangeDensityHoldStart = mDriveScheduler.GetTick64();
			break;
	}
}

void ATDeviceDiskDriveIndusGT::OnScheduledEvent(uint32 id) {
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

void ATDeviceDiskDriveIndusGT::OnCommandStateChanged(bool asserted) {
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

void ATDeviceDiskDriveIndusGT::OnMotorStateChanged(bool asserted) {
}

void ATDeviceDiskDriveIndusGT::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	Sync();

	mReceiveShiftRegister = (c + c + 0x200) * 2 + 1;

	// The conversion fraction we need here is 512/229, but that denominator is awkward.
	// Approximate it with 2289/1024.
	mReceiveTimingAccum = 0x200;
	mReceiveTimingStep = cyclesPerBit * 2289;

	// HACK - if we are transmitting at SuperSynchromesh speeds (>50Kbit), stretch the start bit.
	// The SuperSynchromesh software has marginal read code that tends to read between bits
	// instead of in the center of them. Cause is still unknown.
	if (cyclesPerBit < 35)
		mReceiveTimingAccum += mReceiveTimingStep >> 2;

	mDriveScheduler.SetEvent(1, this, kEventId_DriveReceiveBit, mpEventDriveReceiveBit);
}

void ATDeviceDiskDriveIndusGT::OnSendReady() {
}

void ATDeviceDiskDriveIndusGT::OnDiskChanged(bool mediaRemoved) {
	if (mediaRemoved) {
		mDiskChangeState = 0;
		mDriveScheduler.SetEvent(1, this, kEventId_DriveDiskChange, mpEventDriveDiskChange);
	}

	UpdateDiskStatus();
}

void ATDeviceDiskDriveIndusGT::OnWriteModeChanged() {
	UpdateWriteProtectStatus();
}

void ATDeviceDiskDriveIndusGT::OnTimingModeChanged() {
	mFDC.SetAccurateTimingEnabled(mpDiskInterface->IsAccurateSectorTimingEnabled());
}

void ATDeviceDiskDriveIndusGT::OnAudioModeChanged() {
	mbSoundsEnabled = mpDiskInterface->AreDriveSoundsEnabled();

	UpdateRotationStatus();
}

bool ATDeviceDiskDriveIndusGT::IsImageSupported(const IATDiskImage& image) const {
	const auto& geo = image.GetGeometry();

	return geo.mTrackCount <= 40 && geo.mSideCount == 1 && geo.mSectorsPerTrack <= 26 && geo.mSectorSize <= 256 && !geo.mbHighDensity;
}

void ATDeviceDiskDriveIndusGT::Sync() {
	mDriveScheduler.SetStopTime(AccumSubCycles());

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

void ATDeviceDiskDriveIndusGT::AddTransmitEdge(uint32 polarity) {
	mSerialXmitQueue.AddTransmitBit(DriveTimeToMasterTime() + mSerialXmitQueue.kTransmitLatency, polarity);
}

uint8 ATDeviceDiskDriveIndusGT::OnReadStatus1(uint8 addr) {
	uint8 v = mStatus1;

	if (!(addr & 1)) {
		mStatus1 &= 0x0F;
		mStatus1 |= mStatus1ButtonsHeld;
	}

	if (mStatus1CPMHoldStart) {
		uint64 t = mDriveScheduler.GetTick64();

		if (t >= mStatus1CPMHoldStart) {
			uint64 elapsed = t - mStatus1CPMHoldStart;

			if (elapsed > 4000000) {
				mStatus1CPMHoldStart = 0;
			} else {
				// hold drive type
				mStatus1 |= 0x20;
				v |= 0x20;

				if (elapsed >= 1000000 && elapsed < 2000000) {
					// hold error
					mStatus1 |= 0x40;
					v |= 0x40;
				}
			}
		}
	}

	// change density = hold Drive Type + press Track
	if (mStatus1ChangeDensityHoldStart) {
		uint64 t = mDriveScheduler.GetTick64();

		if (t >= mStatus1ChangeDensityHoldStart) {
			uint64 elapsed = t - mStatus1ChangeDensityHoldStart;

			if (elapsed > 4000000) {
				mStatus1CPMHoldStart = 0;
			} else {
				// hold drive type
				mStatus1 |= 0x20;
				v |= 0x20;

				if (elapsed >= 1000000 && elapsed < 2000000) {
					// hold track
					mStatus1 |= 0x10;
					v |= 0x10;
				}
			}
		}
	}

	return v;
}

uint8 ATDeviceDiskDriveIndusGT::OnDebugReadStatus1() const {
	return mStatus1;
}

uint8 ATDeviceDiskDriveIndusGT::OnReadStatus2() const {
	uint8 v = mStatus2 & 0x1A;

	// D7 = FDC IRQ
	// D6 = FDC DRQ
	// D5 = SIO COMMAND
	// D4 = SIO READY
	// D3 = SIO DATA IN (computer <- drive)
	// D2 = SIO DATA OUT (computer -> drive)
	// D1 = SIO CLOCK IN (computer <- drive)
	// D0 = SIO CLOCK OUT (computer -> drive)

	if (mFDC.GetIrqStatus())
		v += 0x80;

	if (mFDC.GetDrqStatus())
		v += 0x40;

	if (!mbCommandState)
		v += 0x20;

	if (mReceiveShiftRegister & 1)
		v += 0x04;

	if (mpEventDriveReceiveBit) {
		const uint32 dt = mDriveScheduler.GetTicksToEvent(mpEventDriveReceiveBit);

		if (dt >= (mReceiveTimingStep >> 11))		// <1/2 bit
			v += 0x01;
	} else {
		v += 0x01;
	}

	return v;
}

uint8 ATDeviceDiskDriveIndusGT::OnReadControl() {
	OnWriteControl(0xF);
	return 0xFF;
}

void ATDeviceDiskDriveIndusGT::OnWriteControl(uint8 val) {
	const uint8 phases = (val & 15);

	if (mActiveStepperPhases == phases)
		return;

	mActiveStepperPhases = phases;

	static const sint8 kOffsetTables[16]={
		// IndusGT (one phase required, non-inverted)
		-1,  0,  1, -1,
		 2, -1, -1, -1,
		 3, -1, -1, -1,
		-1, -1, -1, -1
	};

	const sint8 newOffset = kOffsetTables[phases];

	g_ATLCDiskEmu("Stepper phases now: %X\n", phases);

	if (newOffset >= 0) {
		switch(((uint32)newOffset - mCurrentTrack) & 3) {
			case 1:		// step in (increasing track number)
				if (mCurrentTrack < 90U) {
					++mCurrentTrack;

					// The track 0 sensor actually activates midway between tracks 0 and 2, per the
					// Tandon TM-50 manual. As it turns out, we must signal "track 0" at track 0.5.
					// This works because the manual says that track 0 is encountered when the
					// sensor activates AND the current phase is 0. The disk drive firmware works
					// either way; the DIAGS program, however, requires track 0.5 to activates this
					// sensor to pass the Zero Adj test.
					mFDC.SetCurrentTrack(mCurrentTrack, mCurrentTrack <= 1);
				}

				PlayStepSound();
				break;

			case 3:		// step out (decreasing track number)
				if (mCurrentTrack > 0) {
					--mCurrentTrack;

					mFDC.SetCurrentTrack(mCurrentTrack, mCurrentTrack <= 1);

					PlayStepSound();
				}
				break;

			case 0:
			case 2:
			default:
				// no step or indeterminate -- ignore
				break;
		}
	}
}

uint8 ATDeviceDiskDriveIndusGT::TranslateLED(uint8 val) {
	return ~val & 0x7F;
}

uint8 ATDeviceDiskDriveIndusGT::OnReadControlLED1() {
	OnWriteControlLED1(0xFF);
	return 0xFF;
}

void ATDeviceDiskDriveIndusGT::OnWriteControlLED1(uint8 val) {
	uint32 newState = (mLEDState & 0x7F) + (((uint32)TranslateLED(val)) << 8);

	if (mLEDState != newState) {
		mLEDState = newState;
		mpDiskInterface->SetShowLEDReadout((sint32)newState);
	}
}

uint8 ATDeviceDiskDriveIndusGT::OnReadControlLED2() {
	OnWriteControlLED2(0xFF);
	return 0xFF;
}

void ATDeviceDiskDriveIndusGT::OnWriteControlLED2(uint8 val) {
	uint32 newState = (mLEDState & 0x7F00) + TranslateLED(val);

	if (mLEDState != newState) {
		mLEDState = newState;
		mpDiskInterface->SetShowLEDReadout((sint32)newState);
	}
}

uint8 ATDeviceDiskDriveIndusGT::OnDebugReadFDC(uint32 addr) const {
	return mFDC.DebugReadByte((uint8)addr);
}

uint8 ATDeviceDiskDriveIndusGT::OnReadFDC(uint32 addr) {
	return mFDC.ReadByte((uint8)addr);
}

void ATDeviceDiskDriveIndusGT::OnWriteFDC(uint32 addr, uint8 val) {
	return mFDC.WriteByte((uint8)addr, val);
}

void ATDeviceDiskDriveIndusGT::OnAccessPort(uint8 addr) {
	// A3:A1 = 000: audio
	// A3:A1 = 001: index pulse 1
	// A3:A1 = 010: TxD
	// A3:A1 = 011: RxD
	// A3:A1 = 100: /DDEN
	// A3:A1 = 101: /MOTOR ON
	// A3:A1 = 110: index pulse 2
	// A3:A1 = 111: /BANK

	const bool state = (addr & 1) != 0;

	switch((addr >> 1) & 7) {
		case 0:
			{
				static constexpr uint32 kAudioLatency = 200;

				const uint32 t = DriveTimeToMasterTime() + kAudioLatency;

				const float level = state ? -1.0f : 0.0f;

				mAudioRawSource.SetOutput(t, level);
			}
			break;

		case 1:
			mFDC.SetAutoIndexPulse(!state);
			break;

		case 2:
			mbDirectTransmitOutput = !state;
			AddTransmitEdge(!state);
			break;

		case 3:
			mbDirectReceiveOutput = !state;
			break;

		case 4:	// /DDEN
			mFDC.SetDensity(!state);
			break;

		case 5:	// /MOTOR ON
			mbMotorRunning = state;
			mFDC.SetMotorRunning(state);
			UpdateRotationStatus();
			break;

		case 6:	// forced index pulse
			if (mbForcedIndexPulse != state) {
				mbForcedIndexPulse = state;

				mFDC.OnIndexPulse(state);
			}
			break;

		case 7: // bank switch
			if (mbExtendedRAMEnabled != state) {
				mbExtendedRAMEnabled = state;

				UpdateMemoryMap();
			}
			break;
	}

	mCoProc.NegateIrq();
}

void ATDeviceDiskDriveIndusGT::PlayStepSound() {
	if (!mbSoundsEnabled)
		return;

	const uint32 t = ATSCHEDULER_GETTIME(&mDriveScheduler);
	
	if (t - mLastStepSoundTime > 50000)
		mLastStepPhase = 0;

	mAudioPlayer.PlayStepSound(kATAudioSampleId_DiskStep2H, 0.3f + 0.7f * cosf((float)mLastStepPhase++ * nsVDMath::kfPi));

	mLastStepSoundTime = t;
}

void ATDeviceDiskDriveIndusGT::UpdateMemoryMap() {
	uintptr *VDRESTRICT readmap = mCoProc.GetReadMap();
	uintptr *VDRESTRICT writemap = mCoProc.GetWriteMap();

	if (mbExtendedRAMEnabled) {
		std::fill(readmap, readmap+256, (uintptr)(mRAM + 0x800));
		std::fill(writemap, writemap+256, (uintptr)(mRAM + 0x800));
	} else {
		memcpy(readmap, mReadMapBackup, sizeof(uintptr)*256);
		memcpy(writemap, mWriteMapBackup, sizeof(uintptr)*256);
	}
}

void ATDeviceDiskDriveIndusGT::UpdateRotationStatus() {
	mpDiskInterface->SetShowMotorActive(mbMotorRunning);

	mAudioPlayer.SetRotationSoundEnabled(mbMotorRunning && mbSoundsEnabled);
}

void ATDeviceDiskDriveIndusGT::UpdateDiskStatus() {
	IATDiskImage *image = mpDiskInterface->GetDiskImage();

	mFDC.SetDiskImage(image, true);

	UpdateWriteProtectStatus();
}

void ATDeviceDiskDriveIndusGT::UpdateWriteProtectStatus() {
	const bool wpoverride = (mDiskChangeState & 1) != 0;
	const bool wpsense = mpDiskInterface->GetDiskImage() && !mpDiskInterface->IsDiskWritable();

	// asserting edge of write protect signal sets status 1 bit 7 latch
	if (wpsense || wpoverride)
		mStatus1 |= 0x80;

	mFDC.SetWriteProtectOverride(wpoverride);
}
