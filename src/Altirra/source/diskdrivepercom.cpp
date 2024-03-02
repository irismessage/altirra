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
#include <at/atcore/deviceprinter.h>
#include <at/atcore/deviceserial.h>
#include "audiosampleplayer.h"
#include "diskdrivepercom.h"
#include "memorymanager.h"
#include "firmwaremanager.h"
#include "debuggerlog.h"

extern ATLogChannel g_ATLCDiskEmu;

template<ATDeviceDiskDrivePercom::HardwareType T_HardwareType>
void ATCreateDeviceDiskDrivePercom(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDrivePercom> p(new ATDeviceDiskDrivePercom(T_HardwareType));
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefDiskDrivePercomRFD = { "diskdrivepercom", "diskdrivepercom", L"Percom RFD disk drive (full emulation)", ATCreateDeviceDiskDrivePercom<ATDeviceDiskDrivePercom::HardwareType::RFD> };
extern const ATDeviceDefinition g_ATDeviceDefDiskDrivePercomAT = { "diskdrivepercomat", "diskdrivepercomat", L"Percom AT-88 disk drive (full emulation)", ATCreateDeviceDiskDrivePercom<ATDeviceDiskDrivePercom::HardwareType::AT88> };
extern const ATDeviceDefinition g_ATDeviceDefDiskDrivePercomATSPD = { "diskdrivepercomatspd", "diskdrivepercomatspd", L"Percom AT88-SPD disk drive (full emulation)", ATCreateDeviceDiskDrivePercom<ATDeviceDiskDrivePercom::HardwareType::AT88SPD> };

///////////////////////////////////////////////////////////////////////////

void ATDeviceDiskDrivePercom::Drive::OnDiskChanged(bool mediaRemoved) {
	mpParent->OnDiskChanged(mIndex, mediaRemoved);
}

void ATDeviceDiskDrivePercom::Drive::OnWriteModeChanged() {
	mpParent->OnWriteModeChanged(mIndex);
}

void ATDeviceDiskDrivePercom::Drive::OnTimingModeChanged() {
	mpParent->OnTimingModeChanged(mIndex);
}

void ATDeviceDiskDrivePercom::Drive::OnAudioModeChanged() {
	mpParent->OnAudioModeChanged(mIndex);
}

bool ATDeviceDiskDrivePercom::Drive::IsImageSupported(const IATDiskImage& image) const {
	return true;
}

///////////////////////////////////////////////////////////////////////////

ATDeviceDiskDrivePercom::ATDeviceDiskDrivePercom(HardwareType hardwareType)
	: mHardwareType(hardwareType)
{
	mBreakpointsImpl.BindBPHandler(mCoProc);
	mBreakpointsImpl.SetStepHandler(this);

	mSerialCmdQueue.SetOnDriveCommandStateChanged(
		[this](bool asserted) {
			mACIA.SetCTS(asserted);
		}
	);

	mTargetHistoryProxy.Init(mCoProc);
	InitTargetControl(mTargetHistoryProxy, 1000000.0, kATDebugDisasmMode_6809, &mBreakpointsImpl, this);

	const uint32 firmwareSize = (hardwareType == HardwareType::AT88SPD ? 4096 : 2048);

	mFirmwareControl.Init(mROM, firmwareSize,
		mHardwareType == HardwareType::AT88SPD ? kATFirmwareType_PercomATSPD
		: mHardwareType == HardwareType::AT88 ? kATFirmwareType_PercomAT
		: kATFirmwareType_Percom
	);
}

ATDeviceDiskDrivePercom::~ATDeviceDiskDrivePercom() {
}

void *ATDeviceDiskDrivePercom::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceScheduling::kTypeID: return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceFirmware::kTypeID: return static_cast<IATDeviceFirmware *>(&mFirmwareControl);
		case IATDeviceDiskDrive::kTypeID: return static_cast<IATDeviceDiskDrive *>(this);
		case IATDeviceSIO::kTypeID: return static_cast<IATDeviceSIO *>(this);
		case IATDeviceAudioOutput::kTypeID: return static_cast<IATDeviceAudioOutput *>(&mAudioPlayer);
		case IATDevicePrinterPort::kTypeID: return static_cast<IATDevicePrinterPort *>(this);
		case IATDeviceParent::kTypeID: return mHardwareType == HardwareType::AT88SPD ? static_cast<IATDeviceParent *>(this) : nullptr;
		case ATFDCEmulator::kTypeID: return &mFDC;
		case ATPIAEmulator::kTypeID: return mHardwareType != HardwareType::RFD ? &mPIA : nullptr;
	}

	return ATDiskDriveDebugTargetControl::AsInterface(iid);
}

void ATDeviceDiskDrivePercom::GetDeviceInfo(ATDeviceInfo& info) {
	switch(mHardwareType) {
		case HardwareType::AT88SPD:
			info.mpDef = &g_ATDeviceDefDiskDrivePercomATSPD;
			break;

		case HardwareType::AT88:
			info.mpDef =  &g_ATDeviceDefDiskDrivePercomAT;
			break;

		case HardwareType::RFD:
		default:
			info.mpDef = &g_ATDeviceDefDiskDrivePercomRFD;
			break;
	}
}

void ATDeviceDiskDrivePercom::GetSettingsBlurb(VDStringW& buf) {
	bool first = true;

	for(uint32 i=0; i<kNumDrives; ++i) {
		if (mDrives[i].mType) {
			if (first)
				first = false;
			else
				buf += ',';

			buf.append_sprintf(L"D%u:", i + mDriveId + 1);
		}
	}
}

void ATDeviceDiskDrivePercom::GetSettings(ATPropertySet& settings) {
	if (mHardwareType == HardwareType::AT88)
		settings.SetBool("ddcapable", mbIsAT88DoubleDensity);
	else if (mHardwareType == HardwareType::RFD)
		settings.SetUint32("id", mDriveId);

	VDStringA s;
	for(uint32 i=0; i<kNumDrives; ++i) {
		s.sprintf("drivetype%u", i);
		settings.SetUint32(s.c_str(), (uint32)mDrives[i].mType);
	}
}

bool ATDeviceDiskDrivePercom::SetSettings(const ATPropertySet& settings) {
	VDStringA s;
	bool change = false;

	for(uint32 i=0; i<kNumDrives; ++i) {
		s.sprintf("drivetype%u", i);
		const uint32 driveTypeCode = settings.GetUint32(s.c_str(), i ? kDriveType_None : kDriveType_5_25_40Track);

		if (driveTypeCode <= kDriveType_5_25_80Track) {
			Drive& drive = mDrives[i];

			if (drive.mType != driveTypeCode) {
				drive.mType = (DriveType)driveTypeCode;

				if (drive.mType == kDriveType_5_25_80Track)
					drive.mMaxTrack = 180;
				else
					drive.mMaxTrack = 90;

				change = true;
			}
		}
	}

	if (mHardwareType == HardwareType::AT88 || mHardwareType == HardwareType::AT88SPD) {
		bool use1795 = settings.GetBool("use1795", false);

		if (mbAT1795Mode != use1795) {
			mbAT1795Mode = use1795;
			change = true;
		}
	}

	if (mHardwareType == HardwareType::AT88) {
		bool dd = settings.GetBool("ddcapable", true);

		if (mbIsAT88DoubleDensity != dd) {
			mbIsAT88DoubleDensity = dd;
			change = true;
		}
	} else if (mHardwareType == HardwareType::RFD) {
		uint32 newDriveId = settings.GetUint32("id", mDriveId) & 7;

		if (mDriveId != newDriveId) {
			mDriveId = newDriveId;
			change = true;
		}
	}

	return !change;
}

void ATDeviceDiskDrivePercom::Init() {
	mParallelBus.Init(this, 0, IATPrinterOutput::kTypeID, "parallel", L"Parallel Printer Port", "parport");

	mSerialCmdQueue.Init(&mDriveScheduler, mpSIOMgr);
	mSerialXmitQueue.Init(mpScheduler, mpSIOMgr);

	uintptr *readmap = mCoProc.GetReadMap();
	uintptr *writemap = mCoProc.GetWriteMap();

	ATCoProcMemoryMapView mmap(readmap, writemap);

	// initialize memory map
	mmap.Clear(mDummyRead, mDummyWrite);

	if (mHardwareType == HardwareType::AT88SPD) {
		// AT88-SPD memory map:
		//
		//	$0000-0FFF		Hardware registers
		//		$04-07			1771/1795 FDC
		//		$08-0B			6821 PIA
		//		$0C-0F			6851 ACIA
		//	$1000-13FF		1K static RAM
		//	$3000-3FFF		4K ROM
		//	$4000-FFFF		Mirror of $0000-3FFF

		mReadNodeHardware.mpThis = this;
		mReadNodeHardware.mpRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDrivePercom *)thisPtr)->OnHardwareReadAT(addr); };
		mReadNodeHardware.mpDebugRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDrivePercom *)thisPtr)->OnHardwareDebugReadAT(addr); };

		mWriteNodeHardware.mpThis = this;
		mWriteNodeHardware.mpWrite = [](uint32 addr, uint8 value, void *thisPtr) { ((ATDeviceDiskDrivePercom *)thisPtr)->OnHardwareWriteAT(addr, value); };

		mmap.SetReadHandler(0x00, 0x10, mReadNodeHardware);
		mmap.SetWriteHandler(0x00, 0x10, mWriteNodeHardware);
		mmap.SetMemory(0x10, 0x04, mRAM);
		mmap.MirrorFwd(0x14, 0x0C, 0x10);
		mmap.SetReadMem(0x30, 0x10, mROM);
		mmap.MirrorFwd(0x40, 0xC0, 0x00);

		mPIA.Init(&mDriveScheduler);
		mPIA.AllocInput();
		mPIA.AllocOutput(
			[](void *thisptr, uint32 state) {
				((ATDeviceDiskDrivePercom *)thisptr)->OnPIAPortBChangedATSPD(state);
			},
			this, 0xFF00
		);
		mPIA.AllocOutput(
			[](void *thisptr, uint32 state) {
				((ATDeviceDiskDrivePercom *)thisptr)->OnPIACB2ChangedATSPD((state & kATPIAOutput_CB2) != 0);
			},
			this, kATPIAOutput_CB2
		);
	} else if (mHardwareType == HardwareType::AT88) {
		// AT-88 memory map:
		//
		//	$0000-0FFF		Hardware registers
		//		$04-07			1771/1795 FDC
		//		$08-0B			6821 PIA
		//		$0C-0F			6851 ACIA
		//	$5000-53FF		1K static RAM
		//	$F800-FFFF		2K ROM

		mReadNodeHardware.mpThis = this;
		mReadNodeHardware.mpRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDrivePercom *)thisPtr)->OnHardwareReadAT(addr); };
		mReadNodeHardware.mpDebugRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDrivePercom *)thisPtr)->OnHardwareDebugReadAT(addr); };

		mWriteNodeHardware.mpThis = this;
		mWriteNodeHardware.mpWrite = [](uint32 addr, uint8 value, void *thisPtr) { ((ATDeviceDiskDrivePercom *)thisPtr)->OnHardwareWriteAT(addr, value); };

		mmap.SetReadHandler(0x00, 0x10, mReadNodeHardware);
		mmap.SetWriteHandler(0x00, 0x10, mWriteNodeHardware);
		mmap.SetMemory(0x10, 0x04, mRAM);
		mmap.MirrorFwd(0x14, 0x0C, 0x10);
		mmap.SetReadMem(0x30, 0x08, mROM);
		mmap.SetReadMem(0x38, 0x08, mROM);
		mmap.MirrorFwd(0x40, 0xC0, 0x00);

		mPIA.SetIRQHandler(
			[this](uint32 mask, bool asserted) {
				if (mask & kATIRQSource_PIAA2) {
					if (asserted)
						mCoProc.AssertIrq();
					else
						mCoProc.NegateIrq();
				}
			}
		);
		mPIA.Init(&mDriveScheduler);
		mPIA.AllocInput();
		mPIA.AllocOutput(
			[](void *thisptr, uint32 state) {
				((ATDeviceDiskDrivePercom *)thisptr)->OnPIAPortBChanged(state);
			},
			this, 0xFF00
		);

		mPIA.SetCA2(false);
	} else {
		// RFD memory map:
		//
		//	$D000-D3FF		Hardware registers
		//	$DC00-DFFF		1K static RAM
		//	$F000-F7FF		2K ROM (mirror)
		//	$F800-FFFF		2K ROM

		// Map hardware registers to $D000-D3FF. The individual sections are selected
		// by A2-A5, so all page mappings are the same here.
		mReadNodeHardware.mpThis = this;
		mReadNodeHardware.mpRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDrivePercom *)thisPtr)->OnHardwareReadRFD(addr); };
		mReadNodeHardware.mpDebugRead = [](uint32 addr, void *thisPtr) { return ((ATDeviceDiskDrivePercom *)thisPtr)->OnHardwareDebugReadRFD(addr); };

		mWriteNodeHardware.mpThis = this;
		mWriteNodeHardware.mpWrite = [](uint32 addr, uint8 value, void *thisPtr) { ((ATDeviceDiskDrivePercom *)thisPtr)->OnHardwareWriteRFD(addr, value); };

		mmap.SetReadHandler(0xD0, 0x04, mReadNodeHardware);
		mmap.SetWriteHandler(0xD0, 0x04, mWriteNodeHardware);

		// map RAM to $DC00-DFFF
		mmap.SetMemory(0xDC, 0x04, mRAM);

		// map ROM to $F000-FFFF (mirrored)
		mmap.SetReadMem(0xF0, 0x08, mROM);
		mmap.SetReadMem(0xF8, 0x08, mROM);
	}

	mDriveScheduler.SetRate(VDFraction(1000000, 1));

	// Base clock to the ACIA is 4MHz / 13.
	mACIA.Init(&mDriveScheduler);
	mACIA.SetMasterClockPeriod(13 * 16);
	mACIA.SetTransmitFn([this](uint8 v, uint32 cyclesPerBit) { OnACIATransmit(v, cyclesPerBit); });

	mFDC.Init(&mDriveScheduler, 300.0f, 1.0f, mbAT1795Mode ? ATFDCEmulator::kType_2797 : ATFDCEmulator::kType_2793);
	mFDC.SetAutoIndexPulse(true);
	mFDC.SetOnDrqChange(
		[this](bool drq) {
			if (!mbSelectFDC2)
				OnFDCDataRequest(drq);
		}
	);
	mFDC.SetOnIrqChange(
		[this](bool irq) {
			if (!mbSelectFDC2)
				OnFDCInterruptRequest(irq);
		}
	);

	mFDC.SetOnStep([this](bool inward) { OnFDCStep(inward); });

	if (mHardwareType == HardwareType::AT88) {
		mFDC.SetDensity(true);
		mFDC.SetAutoIndexPulse(false);

		mFDC2.Init(&mDriveScheduler, 288.0f, 1.0f, ATFDCEmulator::kType_1771);
		mFDC2.SetAutoIndexPulse(false);
		mFDC2.SetOnDrqChange(
			[this](bool drq) {
				if (mbSelectFDC2)
					OnFDCDataRequest(drq);
			}
		);
		mFDC2.SetOnIrqChange(
			[this](bool irq) {
				if (mbSelectFDC2)
					OnFDCInterruptRequest(irq);
			}
		);

		mFDC2.SetOnStep([this](bool inward) { OnFDCStep(inward); });
	}

	int driveIndex = 0;
	for(auto& drive : mDrives) {
		drive.mDiskChangeHandler.Init(mpScheduler);
		drive.mDiskChangeHandler.SetOutputStateFns(
			[&drive, driveIndex, this](std::optional<bool> wpState) {
				if (mSelectedDrive == driveIndex) {
					mFDC.SetWriteProtectOverride(wpState);

					if (mHardwareType == HardwareType::AT88) {
						mFDC2.SetWriteProtectOverride(wpState);

						// write protect -> PA6
						mPIA.SetInput(0, wpState.value_or(false) ? ~UINT32_C(0x40) : 0);
					}
				}
			},
			[&drive, driveIndex, this](std::optional<bool> readyState) {
				if (mSelectedDrive == driveIndex) {
					mFDC.SetDiskImageReady(readyState);

					if (mHardwareType == HardwareType::AT88)
						mFDC2.SetDiskImageReady(readyState);
				}
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

void ATDeviceDiskDrivePercom::Shutdown() {
	mParallelBus.Shutdown();

	mAudioPlayer.Shutdown();
	mSerialCmdQueue.Shutdown();
	mSerialXmitQueue.Shutdown();

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

	vdsaferelease <<= mpPrinter;
}

uint32 ATDeviceDiskDrivePercom::GetComputerPowerOnDelay() const {
	return 20;
}

void ATDeviceDiskDrivePercom::WarmReset() {
}

void ATDeviceDiskDrivePercom::ComputerColdReset() {
	WarmReset();
}

void ATDeviceDiskDrivePercom::PeripheralColdReset() {
	memset(mRAM, 0xA5, sizeof mRAM);

	mACIA.Reset();
	mFDC.Reset();

	if (mHardwareType == HardwareType::AT88)
		mFDC2.Reset();

	mSerialCmdQueue.Reset();
	mSerialXmitQueue.Reset();

	mbNmiState = false;
	mbNmiTimeout = false;
	mbNmiTimeoutEnabled = false;
	mDriveScheduler.UnsetEvent(mpEventDriveTimeout);

	ResetTargetControl();

	SelectDrive(-1);

	// start the disk drive on a track other than 0/20/39, just to make things interesting
	for(Drive& drive : mDrives) {
		drive.mCurrentTrack = 20;
		drive.mDiskChangeHandler.Reset();
	}

	mbMotorRunning = false;

	mFDC.SetCurrentTrack(20, false);
	mFDC.SetMotorRunning(false);
	mFDC.SetWriteProtectOverride(false);
	mFDC.SetSide(false);

	if (mHardwareType == HardwareType::AT88SPD) {
		mFDC.OnIndexPulse(true);
		mFDC.SetAutoIndexPulse(false);

		mbLastPrinterStrobe = true;
		mPIA.ColdReset();
	} else if (mHardwareType == HardwareType::AT88) {
		mFDC.OnIndexPulse(true);

		mFDC2.OnIndexPulse(true);
		mFDC2.SetCurrentTrack(20, false);
		mFDC2.SetMotorRunning(false);
		mFDC2.SetWriteProtectOverride(false);
		mFDC2.SetSide(false);

		mPIA.ColdReset();
	} else {
		mFDC.SetAutoIndexPulse(true);
		mFDC.SetDensity(false);
	}

	mCoProc.ColdReset();

	WarmReset();
}

void ATDeviceDiskDrivePercom::InitDiskDrive(IATDiskDriveManager *ddm) {
	mpDiskDriveManager = ddm;
	mAvailableDrives = 0;

	for(uint32 i=0; i<kNumDrives; ++i) {
		Drive& drive = mDrives[i];

		drive.mIndex = i;
		drive.mpParent = this;

		if (drive.mType) {
			drive.mpDiskInterface = ddm->GetDiskInterface(i + mDriveId);
			drive.mpDiskInterface->AddClient(&drive);

			mAvailableDrives |= (1 << i);
		}
	}
}

ATDeviceDiskDriveInterfaceClient ATDeviceDiskDrivePercom::GetDiskInterfaceClient(uint32 index) {
	for(uint32 i=0; i<kNumDrives; ++i) {
		if (mDrives[i].mType && !index--)
			return { &mDrives[i], mDriveId + i };
	}

	return {};
}

void ATDeviceDiskDrivePercom::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

void ATDeviceDiskDrivePercom::OnScheduledEvent(uint32 id) {
	if (id == kEventId_DriveTimeout) {
		mpEventDriveTimeout = nullptr;

		if (!mbNmiTimeout) {
			mbNmiTimeout = true;

			UpdateNmi();
		}
	} else
		return ATDiskDriveDebugTargetControl::OnScheduledEvent(id);
}

void ATDeviceDiskDrivePercom::OnCommandStateChanged(bool asserted) {
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

void ATDeviceDiskDrivePercom::OnMotorStateChanged(bool asserted) {
}

void ATDeviceDiskDrivePercom::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	Sync();

	mACIA.ReceiveByte(c, (cyclesPerBit * 100 + (179/2)) / 179);
}

void ATDeviceDiskDrivePercom::OnSendReady() {
}

void ATDeviceDiskDrivePercom::SetPrinterDefaultOutput(IATPrinterOutput *out) {
	if (mpPrinter != out) {
		vdsaferelease <<= mpPrinter;

		mpPrinter = out;

		if (out)
			out->AddRef();
	}
}

IATDeviceBus *ATDeviceDiskDrivePercom::GetDeviceBus(uint32 index) {
	return index ? nullptr : &mParallelBus;
}

void ATDeviceDiskDrivePercom::OnDiskChanged(uint32 index, bool mediaRemoved) {
	Drive& drive = mDrives[index];

	if (mediaRemoved)
		drive.mDiskChangeHandler.ChangeDisk();

	UpdateDiskStatus();
}

void ATDeviceDiskDrivePercom::OnWriteModeChanged(uint32 index) {
	// FDC polls write protect state directly, so no action needed
}

void ATDeviceDiskDrivePercom::OnTimingModeChanged(uint32 index) {
	if (mSelectedDrive == (int)index) {
		const bool accurateTiming = mDrives[index].mpDiskInterface->IsAccurateSectorTimingEnabled();

		mFDC.SetAccurateTimingEnabled(accurateTiming);

		if (mHardwareType == HardwareType::AT88)
			mFDC2.SetAccurateTimingEnabled(accurateTiming);
	}
}

void ATDeviceDiskDrivePercom::OnAudioModeChanged(uint32 index) {
	if (mSelectedDrive == (int)index) {
		bool driveSounds = mDrives[index].mpDiskInterface->AreDriveSoundsEnabled();

		mbSoundsEnabled = driveSounds;

		UpdateRotationStatus();
	}
}

void ATDeviceDiskDrivePercom::Sync() {
	const uint32 limit = AccumSubCycles();

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

uint8 ATDeviceDiskDrivePercom::OnHardwareDebugReadRFD(uint32 addr) {
	// To access the hardware registers, A4 must be high and A5 must be low.
	// A2-A3 select the unit and A0-A1 the subunit (ACIA/FDC). A6-A9 don't
	// matter.
	switch(addr & 0x3C) {
		case 0x10:
			return ~mFDC.DebugReadByte(addr);

		case 0x14:
			return (mDriveId & 3) + (mDriveId & 4 ? 0x10 : 0x00) + 0xE0;

		case 0x30:
			return mACIA.DebugReadByte(addr);
	}

	return 0xFF;
}

uint8 ATDeviceDiskDrivePercom::OnHardwareReadRFD(uint32 addr) {
	// To access the hardware registers, A4 must be high and A5 must be low.
	// A2-A3 select the unit and A0-A1 the subunit (ACIA/FDC). A6-A9 don't
	// matter.
	switch(addr & 0x3C) {
		case 0x10:
			if (mbNmiTimeoutEnabled)
				SetNmiTimeout();
			return ~mFDC.ReadByte(addr);

		case 0x14:
			return OnHardwareDebugReadRFD(addr);

		case 0x30:
			return mACIA.ReadByte(addr);
	}

	return 0xFF;
}

// The drive uses conventional PIA addressing, not Atari's permuted addressing.
const uint8 ATDeviceDiskDrivePercom::kPIALookup[4] = { 0, 2, 1, 3 };

void ATDeviceDiskDrivePercom::OnHardwareWriteAT(uint32 addr, uint8 value) {
	switch(addr & 0x0C) {
		case 0x00:
		case 0x04:
			if (mbSelectFDC2)
				mFDC2.WriteByte(addr, ~value);
			else
				mFDC.WriteByte(addr, ~value);
			break;

		case 0x08:
			mPIA.WriteByte(kPIALookup[addr & 3], value);
			break;

		case 0x0C:
			mACIA.WriteByte(addr, value);
			break;
	}
}

uint8 ATDeviceDiskDrivePercom::OnHardwareDebugReadAT(uint32 addr) {
	switch(addr & 0x0C) {
		case 0x00:
		case 0x04:
			if (mbSelectFDC2)
				return ~mFDC2.DebugReadByte(addr);
			else
				return ~mFDC.DebugReadByte(addr);

		case 0x08:
			return mPIA.DebugReadByte(kPIALookup[addr & 3]);

		case 0x0C:
			return mACIA.DebugReadByte(addr);

		default:
			VDNEVERHERE;
			return 0xFF;
	}
}

uint8 ATDeviceDiskDrivePercom::OnHardwareReadAT(uint32 addr) {
	switch(addr & 0x0C) {
		case 0x00:
		case 0x04:
			if (mbSelectFDC2)
				return ~mFDC2.ReadByte(addr);
			else
				return ~mFDC.ReadByte(addr);

		case 0x08:
			return mPIA.ReadByte(kPIALookup[addr & 3]);

		case 0x0C:
			return mACIA.ReadByte(addr);

		default:
			VDNEVERHERE;
			return 0xFF;
	}
}

void ATDeviceDiskDrivePercom::OnHardwareWriteRFD(uint32 addr, uint8 value) {
	// To access the hardware registers, A4 must be high and A5 must be low.
	// A2-A3 select the unit and A0-A1 the subunit (ACIA/FDC). A6-A9 don't
	// matter.
	switch(addr & 0x3C) {
		case 0x10:
			mFDC.WriteByte(addr, ~value);
			if (mbNmiTimeoutEnabled)
				SetNmiTimeout();
			break;

		case 0x14:
			if (value & 8) {
				SetMotorEnabled(true);
				SelectDrive((value >> 1) & 3);
			} else {
				SetMotorEnabled(false);
				SelectDrive(-1);
			}

			mFDC.SetSide((value & 1) != 0);
			break;

		case 0x18: {
			const bool nmiTimeoutEnabled = (value & 1) != 0;

			if (mbNmiTimeoutEnabled != nmiTimeoutEnabled) {
				mbNmiTimeoutEnabled = nmiTimeoutEnabled;

				if (!mbNmiTimeoutEnabled)
					mDriveScheduler.UnsetEvent(mpEventDriveTimeout);

				UpdateNmi();
			}

			mFDC.SetDensity((value & 4) != 0);
			mFDC.SetAutoIndexPulse((value & 8) == 0);
			mFDC.SetDoubleClock((value & 2) != 0);
			break;
		}

		case 0x30:
			mACIA.WriteByte(addr, value);
			break;
	}
}

void ATDeviceDiskDrivePercom::OnFDCDataRequest(bool asserted) {
	if (asserted)
		mCoProc.AssertFirq();
	else
		mCoProc.NegateFirq();
}

void ATDeviceDiskDrivePercom::OnFDCInterruptRequest(bool asserted) {
	switch(mHardwareType) {
		case HardwareType::AT88SPD:
			if (asserted)
				mCoProc.AssertIrq();
			else
				mCoProc.NegateIrq();
			break;

		case HardwareType::AT88:
			mPIA.SetCA2(asserted);
			break;

		case HardwareType::RFD:
			UpdateNmi();
			break;
	}
}

void ATDeviceDiskDrivePercom::OnFDCStep(bool inward) {
	if (mSelectedDrive < 0)
		return;

	Drive& drive = mDrives[mSelectedDrive];

	if (inward) {
		// step in (increasing track number)
		if (drive.mCurrentTrack < drive.mMaxTrack) {
			drive.mCurrentTrack += 2;

			mFDC.SetCurrentTrack(drive.mCurrentTrack, drive.mCurrentTrack == 0);

			if (mHardwareType == HardwareType::AT88)
				mFDC2.SetCurrentTrack(drive.mCurrentTrack, drive.mCurrentTrack == 0);
		}

		PlayStepSound();
	} else {
		// step out (decreasing track number)
		if (drive.mCurrentTrack > 0) {
			drive.mCurrentTrack -= 2;

			mFDC.SetCurrentTrack(drive.mCurrentTrack, drive.mCurrentTrack == 0);

			if (mHardwareType == HardwareType::AT88)
				mFDC2.SetCurrentTrack(drive.mCurrentTrack, drive.mCurrentTrack == 0);

			PlayStepSound();
		}
	}
}

void ATDeviceDiskDrivePercom::OnACIATransmit(uint8 v, uint32 cyclesPerBit) {
	mSerialXmitQueue.AddTransmitByte(DriveTimeToMasterTime() + mSerialXmitQueue.kTransmitLatency, v, (cyclesPerBit * 179 + 50) / 100);
}

void ATDeviceDiskDrivePercom::OnPIAPortBChanged(uint32 outputState) {
	//	PB7			Index control
	//	PB6			Density select (1771/1795 select)
	//	PB5			Side select (1=side2)
	//	PB4			Motor enable (1=on)
	//	PB0-PB3		Drive select signals (1 = selected)

	SetMotorEnabled((outputState & 0x1000) != 0);

	static const sint8 kDriveLookup[] = { -1, 0, 1, -1, 2, -1, -1, -1, 3, -1, -1, -1, -1, -1, -1, -1 };
	static_assert(vdcountof(kDriveLookup) == 16);

	SelectDrive(kDriveLookup[(outputState >> 8) & 15]);

	mbSelectFDC2 = !mbIsAT88DoubleDensity || (outputState & 0x4000) != 0;

	auto& fdc = mbSelectFDC2 ? mFDC2 : mFDC;

	fdc.SetSide((outputState & 0x2000) != 0);
	fdc.OnIndexPulse((outputState & 0x8000) == 0);
}

void ATDeviceDiskDrivePercom::OnPIAPortBChangedATSPD(uint32 outputState) {
	//	PB7			Printer /BUSY
	//	PB6			Printer FAULT
	//	PB5			Index control
	//	PB4			Density select (0=MFM)
	//	PB3			Side select (1=side2)
	//	PB2			Motor enable (1=on)
	//	PB0-PB1		Drive select signals (00/10/01/11 -> D1-D4:)

	SetMotorEnabled((outputState & 0x0400) != 0);

	static const uint8 kDriveLookup[] = { 0, 2, 1, 3 };

	SelectDrive(kDriveLookup[(outputState >> 8) & 3]);

	auto& fdc = mbSelectFDC2 ? mFDC2 : mFDC;

	fdc.SetDensity((outputState & 0x1000) == 0);
	fdc.SetSide((outputState & 0x0800) != 0);
	fdc.OnIndexPulse((outputState & 0x2000) == 0);
}

void ATDeviceDiskDrivePercom::OnPIACB2ChangedATSPD(bool value) {
	if (mbLastPrinterStrobe == value)
		return;

	mbLastPrinterStrobe = value;

	// shift out a byte on rising edge of CB2
	if (value) {
		// port A outputs inverted byte
		const uint8 c = ~mPIA.GetPortAOutput();

		if (auto *printer = mParallelBus.GetChild<IATPrinterOutput>())
			printer->WriteASCII(&c, 1);
		else if (mpPrinter)
			mpPrinter->WriteASCII(&c, 1);
	}
}


void ATDeviceDiskDrivePercom::SetMotorEnabled(bool enabled) {
	if (mbMotorRunning != enabled) {
		mbMotorRunning = enabled;

		mFDC.SetMotorRunning(enabled);

		if (mHardwareType == HardwareType::AT88)
			mFDC2.SetMotorRunning(enabled);

		UpdateRotationStatus();
	}
}

void ATDeviceDiskDrivePercom::PlayStepSound() {
	if (!mbSoundsEnabled)
		return;

	const uint32 t = ATSCHEDULER_GETTIME(&mDriveScheduler);
	
	if (t - mLastStepSoundTime > 50000)
		mLastStepPhase = 0;

	mAudioPlayer.PlayStepSound(kATAudioSampleId_DiskStep2, 0.3f + 0.7f * cosf((float)mLastStepPhase++ * nsVDMath::kfPi));

	mLastStepSoundTime = t;
}

void ATDeviceDiskDrivePercom::UpdateRotationStatus() {
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

void ATDeviceDiskDrivePercom::UpdateDiskStatus() {
	if (mSelectedDrive >= 0) {
		const Drive& drive = mDrives[mSelectedDrive];
		IATDiskImage *image = drive.mpDiskInterface->GetDiskImage();

		mFDC.SetDiskImage(image);

		if (mHardwareType == HardwareType::AT88)
			mFDC2.SetDiskImage(image);

		drive.mDiskChangeHandler.ForceOutputUpdate();
	} else {
		mFDC.SetDiskImage(nullptr, false);

		if (mHardwareType == HardwareType::AT88)
			mFDC2.SetDiskImage(nullptr, false);
	}
}

void ATDeviceDiskDrivePercom::SelectDrive(int index) {
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
		mFDC.SetCurrentTrack(drive.mCurrentTrack, drive.mCurrentTrack == 0);
		mFDC.SetSideMapping(ATFDCEmulator::SideMapping::Side2ReversedOffByOne, drive.mType == kDriveType_5_25_80Track ? 80 : 40);

		if (mHardwareType == HardwareType::AT88) {
			mFDC2.SetDiskInterface(drive.mpDiskInterface);
			mFDC2.SetCurrentTrack(drive.mCurrentTrack, drive.mCurrentTrack == 0);
			mFDC2.SetSideMapping(ATFDCEmulator::SideMapping::Side2ReversedOffByOne, drive.mType == kDriveType_5_25_80Track ? 80 : 40);
		}

		OnWriteModeChanged(index);
		OnTimingModeChanged(index);
		OnAudioModeChanged(index);
	} else {
		mFDC.SetDiskInterface(nullptr);
		mFDC.SetCurrentTrack(20, false);

		if (mHardwareType == HardwareType::AT88) {
			mFDC2.SetDiskInterface(nullptr);
			mFDC2.SetCurrentTrack(20, false);

			mPIA.SetInput(0, ~UINT32_C(0));
		}
	}

	UpdateDiskStatus();
	UpdateRotationStatus();
}

void ATDeviceDiskDrivePercom::UpdateNmi() {
	bool nmiState = true;

	// If the FDC is requesting an interrupt, the 74LS122 is kept in cleared
	// state pulling /NMI low. $DC14-17 bit 0 can also hold this low if set
	// to 0.
	if (!mFDC.GetIrqStatus() && mbNmiTimeoutEnabled) {
		// FDC is not requesting an interrupt and the NMI is enabled. See if
		// the timeout has expired.

		nmiState = mbNmiTimeout;
	}

	if (mbNmiState != nmiState) {
		mbNmiState = nmiState;

		if (nmiState)
			mCoProc.AssertNmi();
	}
}

void ATDeviceDiskDrivePercom::SetNmiTimeout() {
	// This timeout delay is determined by an R/C network connected to
	// the 74LS123. Not being able to interpret said circuit, use a timeout
	// that is enough for at least four disk rotations.
	const uint32 kTimeoutDelay = 1000000;

	mDriveScheduler.SetEvent(kTimeoutDelay, this, kEventId_DriveTimeout, mpEventDriveTimeout);

	// trigger pulse on /NMI
	mbNmiTimeout = false;
	UpdateNmi();
}
