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
#include <at/atcore/configvar.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/logging.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/wraptime.h>
#include "audiosampleplayer.h"
#include "diskdrivefull.h"
#include "memorymanager.h"
#include "firmwaremanager.h"
#include "debuggerlog.h"

ATLogChannel g_ATLCDiskEmu(true, false, "DISKEMU", "Disk drive emulation");

ATConfigVarBool g_ATCVFullDisk1050TurboForceDensityDetect("full_disk.1050turbo.force_density_detect", true);

void ATCreateDeviceDiskDrive810(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(false, ATDeviceDiskDriveFull::kDeviceType_810));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDeviceDiskDrive810Archiver(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(false, ATDeviceDiskDriveFull::kDeviceType_810Archiver));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDeviceDiskDriveHappy810(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(false, ATDeviceDiskDriveFull::kDeviceType_Happy810));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDeviceDiskDrive1050(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(true, ATDeviceDiskDriveFull::kDeviceType_1050));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDeviceDiskDriveUSDoubler(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(true, ATDeviceDiskDriveFull::kDeviceType_USDoubler));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDeviceDiskDriveSpeedy1050(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(true, ATDeviceDiskDriveFull::kDeviceType_Speedy1050));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDeviceDiskDriveHappy1050(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(true, ATDeviceDiskDriveFull::kDeviceType_Happy1050));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDeviceDiskDriveSuperArchiver(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(true, ATDeviceDiskDriveFull::kDeviceType_SuperArchiver));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDeviceDiskDriveTOMS1050(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(true, ATDeviceDiskDriveFull::kDeviceType_TOMS1050));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDeviceDiskDrive1050Duplicator(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(true, ATDeviceDiskDriveFull::kDeviceType_1050Duplicator));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDeviceDiskDriveTygrys1050(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(true, ATDeviceDiskDriveFull::kDeviceType_Tygrys1050));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDeviceDiskDrive1050Turbo(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(true, ATDeviceDiskDriveFull::kDeviceType_1050Turbo));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDeviceDiskDrive1050TurboII(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(true, ATDeviceDiskDriveFull::kDeviceType_1050TurboII));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDeviceDiskDriveISPlate(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(true, ATDeviceDiskDriveFull::kDeviceType_ISPlate));
	p->SetSettings(pset);

	*dev = p.release();
}

void ATCreateDeviceDiskDrive810Turbo(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveFull> p(new ATDeviceDiskDriveFull(false, ATDeviceDiskDriveFull::kDeviceType_810Turbo));
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefDiskDrive810				= { "diskdrive810",				"diskdrive810",				L"810 disk drive (full emulation)", ATCreateDeviceDiskDrive810 };
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive810Archiver		= { "diskdrive810archiver",		"diskdrive810archiver",		L"810 Archiver disk drive (full emulation)", ATCreateDeviceDiskDrive810Archiver };
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveHappy810			= { "diskdrivehappy810",		"diskdrivehappy810",		L"Happy 810 disk drive (full emulation)", ATCreateDeviceDiskDriveHappy810 };
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive1050				= { "diskdrive1050",			"diskdrive1050",			L"1050 disk drive (full emulation)", ATCreateDeviceDiskDrive1050 };
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveUSDoubler			= { "diskdriveusdoubler",		"diskdriveusdoubler",		L"US Doubler disk drive (full emulation)", ATCreateDeviceDiskDriveUSDoubler };
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveSpeedy1050		= { "diskdrivespeedy1050",		"diskdrivespeedy1050",		L"Speedy 1050 disk drive (full emulation)", ATCreateDeviceDiskDriveSpeedy1050 };
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveHappy1050			= { "diskdrivehappy1050",		"diskdrivehappy1050",		L"Happy 1050 disk drive (full emulation)", ATCreateDeviceDiskDriveHappy1050 };
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveSuperArchiver		= { "diskdrivesuperarchiver",	"diskdrivesuperarchiver",	L"Super Archiver disk drive (full emulation)", ATCreateDeviceDiskDriveSuperArchiver };
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveTOMS1050			= { "diskdrivetoms1050",		"diskdrivetoms1050",		L"TOMS 1050 disk drive (full emulation)", ATCreateDeviceDiskDriveTOMS1050 };
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive1050Duplicator	= { "diskdrive1050duplicator",	"diskdrive1050duplicator",	L"1050 Duplicator disk drive (full emulation)", ATCreateDeviceDiskDrive1050Duplicator };
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveTygrys1050		= { "diskdrivetygrys1050",		"diskdrivetygrys1050",		L"Tygrys 1050 disk drive (full emulation)", ATCreateDeviceDiskDriveTygrys1050 };
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive1050Turbo			= { "diskdrive1050turbo",		"diskdrive1050turbo",		L"1050 Turbo disk drive (full emulation)", ATCreateDeviceDiskDrive1050Turbo };
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive1050TurboII		= { "diskdrive1050turboii",		"diskdrive1050turboii",		L"1050 Turbo II disk drive (full emulation)", ATCreateDeviceDiskDrive1050TurboII };
extern const ATDeviceDefinition g_ATDeviceDefDiskDriveISPlate			= { "diskdriveisplate",			"diskdriveisplate",			L"I.S. Plate disk drive (full emulation)", ATCreateDeviceDiskDriveISPlate };
extern const ATDeviceDefinition g_ATDeviceDefDiskDrive810Turbo			= { "diskdrive810turbo",		"diskdrive810turbo",		L"810 Turbo disk drive (full emulation)", ATCreateDeviceDiskDrive810Turbo };

ATDeviceDiskDriveFull::ATDeviceDiskDriveFull(bool is1050, DeviceType deviceType)
	: mb1050(is1050)
	, mDeviceType(deviceType)
	, mCoProc(deviceType == kDeviceType_Speedy1050, deviceType == kDeviceType_810 || deviceType == kDeviceType_1050)
{
	mBreakpointsImpl.BindBPHandler(mCoProc);
	mBreakpointsImpl.SetStepHandler(this);
	mBreakpointsImpl.SetBPsChangedHandler([this](const uint16 *pc) { mCoProc.OnBreakpointsChanged(pc); });

	const VDFraction& clockRate = is1050 || deviceType == kDeviceType_810Turbo ? VDFraction(1000000, 1) : VDFraction(500000, 1);
	mDriveScheduler.SetRate(clockRate);

	memset(&mDummyRead, 0xFF, sizeof mDummyRead);

	mTargetProxy.mpDriveScheduler = &mDriveScheduler;
	mTargetProxy.Init(mCoProc);
	InitTargetControl(mTargetProxy, clockRate.asDouble(), deviceType == kDeviceType_Speedy1050 ? kATDebugDisasmMode_65C02 : kATDebugDisasmMode_6502, &mBreakpointsImpl);
}

ATDeviceDiskDriveFull::~ATDeviceDiskDriveFull() {
}

void *ATDeviceDiskDriveFull::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceFirmware::kTypeID: return static_cast<IATDeviceFirmware *>(this);
		case IATDeviceDiskDrive::kTypeID: return static_cast<IATDeviceDiskDrive *>(this);
		case IATDeviceSIO::kTypeID: return static_cast<IATDeviceSIO *>(this);
		case IATDeviceAudioOutput::kTypeID: return static_cast<IATDeviceAudioOutput *>(&mAudioPlayer);
		case IATDeviceButtons::kTypeID: return static_cast<IATDeviceButtons *>(this);
		case ATRIOT6532Emulator::kTypeID: return &mRIOT;
		case ATFDCEmulator::kTypeID: return &mFDC;
	}

	return ATDiskDriveDebugTargetControl::AsInterface(iid);
}

void ATDeviceDiskDriveFull::GetDeviceInfo(ATDeviceInfo& info) {
	static const ATDeviceDefinition *const kDeviceDefs[]={
		&g_ATDeviceDefDiskDrive810,
		&g_ATDeviceDefDiskDriveHappy810,
		&g_ATDeviceDefDiskDrive810Archiver,
		&g_ATDeviceDefDiskDrive1050,
		&g_ATDeviceDefDiskDriveUSDoubler,
		&g_ATDeviceDefDiskDriveSpeedy1050,
		&g_ATDeviceDefDiskDriveHappy1050,
		&g_ATDeviceDefDiskDriveSuperArchiver,
		&g_ATDeviceDefDiskDriveTOMS1050,
		&g_ATDeviceDefDiskDriveTygrys1050,
		&g_ATDeviceDefDiskDrive1050Duplicator,
		&g_ATDeviceDefDiskDrive1050Turbo,
		&g_ATDeviceDefDiskDrive1050TurboII,
		&g_ATDeviceDefDiskDriveISPlate,
		&g_ATDeviceDefDiskDrive810Turbo,
	};

	static_assert(vdcountof(kDeviceDefs) == kDeviceTypeCount, "Device def array missized");
	info.mpDef = kDeviceDefs[mDeviceType];
}

void ATDeviceDiskDriveFull::GetSettingsBlurb(VDStringW& buf) {
	buf.sprintf(L"D%u:", mDriveId + 1);
}

void ATDeviceDiskDriveFull::GetSettings(ATPropertySet& settings) {
	settings.SetUint32("id", mDriveId);

	if (mDeviceType == kDeviceType_Happy810) {
		settings.SetBool("autospeed", mbHappy810AutoSpeed);
		settings.SetFloat("autospeedrate", mHappy810AutoSpeedRate);
	}

	if (mDeviceType == kDeviceType_Happy1050 || mDeviceType == kDeviceType_Happy810) {
		settings.SetBool("slow", mbSlowSwitch);
	}

	if (mDeviceType == kDeviceType_Happy1050) {
		settings.SetBool("wpenable", mbWPEnable);
		settings.SetBool("wpdisable", mbWPDisable);
	}
}

bool ATDeviceDiskDriveFull::SetSettings(const ATPropertySet& settings) {
	if (mDeviceType == kDeviceType_Happy1050 || mDeviceType == kDeviceType_Happy810) {
		mbSlowSwitch = settings.GetBool("slow", false);
	}
	
	if (mDeviceType == kDeviceType_Happy1050) {
		// read WP switches, but disallow tea and no tea
		mbWPEnable = settings.GetBool("wpenable", false);
		mbWPDisable = settings.GetBool("wpdisable", false);

		if (mbWPEnable && mbWPDisable) {
			mbWPEnable = false;
			mbWPDisable = false;
		}
	}
	
	if (mDeviceType == kDeviceType_Happy810) {
		mbHappy810AutoSpeed = settings.GetBool("autospeed", false);

		float rate = settings.GetFloat("autospeedrate", kDefaultAutoSpeedRate);
		if (!isfinite(rate) || rate < 200.0f || rate > 400.0f)
			rate = kDefaultAutoSpeedRate;

		mHappy810AutoSpeedRate = rate;
	}

	uint32 newDriveId = settings.GetUint32("id", mDriveId) & 3;

	if (mDriveId != newDriveId) {
		mDriveId = newDriveId;
		return false;
	}

	if (mpScheduler)
		UpdateAutoSpeed();
	return true;
}

void ATDeviceDiskDriveFull::Init() {
	mSerialXmitQueue.Init(mpScheduler, mpSIOMgr);

	// The 810's memory map:
	//
	//	000-07F  FDC
	//	080-0FF  6810 memory
	//	100-17F  RIOT memory (mirror)
	//	180-1FF  RIOT memory
	//	200-27F  FDC (mirror)
	//	280-2FF  6810 memory
	//	300-37F  RIOT registers (mirror)
	//	300-3FF  RIOT registers
	//	400-47F  FDC (mirror)
	//	480-4FF  6810 memory (mirror)
	//	500-5FF  RIOT memory (mirror)
	//	600-67F  FDC (mirror)
	//	680-6FF  6810 memory (mirror)
	//	700-7FF  RIOT registers (mirror)
	//	800-FFF  ROM

	uintptr *readmap = mCoProc.GetReadMap();
	uintptr *writemap = mCoProc.GetWriteMap();

	// set up FDC/6810 handlers
	mReadNodeFDCRAM.mpThis = this;
	mWriteNodeFDCRAM.mpThis = this;

	if (mb1050) {
		mReadNodeFDCRAM.mpRead = [](uint32 addr, void *thisptr0) -> uint8 {
			auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

			return thisptr->mFDC.ReadByte((uint8)addr);
		};

		mReadNodeFDCRAM.mpDebugRead = [](uint32 addr, void *thisptr0) -> uint8 {
			auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

			return thisptr->mFDC.DebugReadByte((uint8)addr);
		};

		mWriteNodeFDCRAM.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) {
			auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

			thisptr->mFDC.WriteByte((uint8)addr, val);
		};
	} else {
		mReadNodeFDCRAM.mpRead = [](uint32 addr, void *thisptr0) -> uint8 {
			auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

			addr &= 0xFF;

			if (addr >= 0x80)
				return thisptr->mRAM[addr];
			else
				return ~thisptr->mFDC.ReadByte((uint8)addr);
		};

		mReadNodeFDCRAM.mpDebugRead = [](uint32 addr, void *thisptr0) -> uint8 {
			auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

			addr &= 0xFF;

			if (addr >= 0x80)
				return thisptr->mRAM[addr];
			else
				return ~thisptr->mFDC.DebugReadByte((uint8)addr);
		};

		mWriteNodeFDCRAM.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) {
			auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;
			
			addr &= 0xFF;

			if (addr >= 0x80)
				thisptr->mRAM[addr] = val;
			else
				thisptr->mFDC.WriteByte((uint8)addr, ~val);
		};
	}

	// set up RIOT memory handlers
	mReadNodeRIOTRAM.mpRead = [](uint32 addr, void *thisptr0) -> uint8 {
		auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

		return thisptr->mRAM[addr & 0x7F];
	};

	mReadNodeRIOTRAM.mpDebugRead = mReadNodeRIOTRAM.mpRead;
	mReadNodeRIOTRAM.mpThis = this;

	mWriteNodeRIOTRAM.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) {
		auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

		thisptr->mRAM[addr & 0x7F] = val;
	};

	mWriteNodeRIOTRAM.mpThis = this;

	// set up RIOT register handlers
	mReadNodeRIOTRegisters.mpThis = this;
	mWriteNodeRIOTRegisters.mpThis = this;

	if (mDeviceType == kDeviceType_USDoubler) {
		mReadNodeRIOTRegisters.mpRead = [](uint32 addr, void *thisptr0) -> uint8 {
			auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

			if (addr & 0x80)
				return thisptr->mRIOT.ReadByte((uint8)addr);
			else
				return thisptr->mRAM[0x100 + (addr & 0x7F)];
		};

		mReadNodeRIOTRegisters.mpDebugRead = [](uint32 addr, void *thisptr0) -> uint8 {
			auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

			if (addr & 0x80)
				return thisptr->mRIOT.DebugReadByte((uint8)addr);
			else
				return thisptr->mRAM[0x100 + (addr & 0x7F)];
		};


		mWriteNodeRIOTRegisters.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) {
			auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

			if (addr & 0x80)
				thisptr->OnRIOTRegisterWrite(addr, val);
			else
				thisptr->mRAM[0x100 + (addr & 0x7F)] = val;
		};
	} else {
		mReadNodeRIOTRegisters.mpRead = [](uint32 addr, void *thisptr0) -> uint8 {
			auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

			return thisptr->mRIOT.ReadByte((uint8)addr);
		};

		mReadNodeRIOTRegisters.mpDebugRead = [](uint32 addr, void *thisptr0) -> uint8 {
			auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

			return thisptr->mRIOT.DebugReadByte((uint8)addr);
		};


		mWriteNodeRIOTRegisters.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) {
			auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

			thisptr->OnRIOTRegisterWrite(addr, val);
		};
	}

	// initialize memory map
	ATCoProcMemoryMapView mmapView(readmap, writemap, mCoProc.GetTraceMap());

	mmapView.Clear(mDummyRead, mDummyWrite);

	if (mb1050) {
		// == 1050 base hardware ==
		//
		// 6532 RIOT: A12=0, A10=0, A7=1
		//	A9=1 for registers ($280-29F)
		//	A9=0 for RAM ($80-FF, $180-1FF)
		// 2793 FDC: A10=1, A12=0 ($400-403)
		// 6810 RAM: A12=0, A10=0, A9=0, A7=0 ($00-7F, $100-17F)

		mmapView.RepeatPage(0x00, 0x02, mRAM);
		mmapView.SetHandlers(0x02, 0x02, mReadNodeRIOTRegisters, mWriteNodeRIOTRegisters);
		mmapView.SetHandlers(0x04, 0x04, mReadNodeFDCRAM, mWriteNodeFDCRAM);
		mmapView.MirrorFwd(0x08, 0x08, 0x00);

		if (mDeviceType == kDeviceType_TOMS1050 || mDeviceType == kDeviceType_1050Duplicator) {
			// The TOMS 1050 uses a 6502 instead of a 6507, so no 8K mirroring. Additional mappings:
			//	$2000-3FFF	RAM
			//	$E000-FFFF	ROM

			// map RAM to $2000-3FFF
			mmapView.SetMemory(0x20, 0x20, mRAM + 0x100);

			// map ROM to $E000-FFFF
			mmapView.SetReadMem(0xE0, 0x20, mROM);
		} else if (mDeviceType == kDeviceType_Happy1050) {
			// The Happy 1050 uses a 6502 instead of a 6507, so no 8K mirroring.

			// mirror lower 8K to $2000-3FFF
			mmapView.MirrorFwd(0x20, 0x20, 0x00);

			// mirror lower 16K to $4000-7FFF through write protect toggle shim
			mReadNodeWriteProtectToggle.mpThis = this;
			mReadNodeWriteProtectToggle.mpDebugRead = [](uint32 addr, void *thisptr0) {
				auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;
				uintptr r = thisptr->mCoProc.GetReadMap()[((addr >> 8) - 0x40) & 0xFF];

				if (r & 1) {
					const auto& readNode = *(const ATCoProcReadMemNode *)(r - 1);

					return readNode.mpDebugRead(addr, readNode.mpThis);
				} else {
					return *(const uint8 *)(r + addr);
				}
			};

			mReadNodeWriteProtectToggle.mpRead = [](uint32 addr, void *thisptr0) {
				auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;
				uintptr r = thisptr->mCoProc.GetReadMap()[((addr >> 8) - 0x40) & 0xFF];

				thisptr->OnToggleWriteProtect();

				if (r & 1) {
					const auto& readNode = *(const ATCoProcReadMemNode *)(r - 1);

					return readNode.mpRead(addr, readNode.mpThis);
				} else {
					return *(const uint8 *)(r + addr);
				}
			};

			mWriteNodeWriteProtectToggle.mpThis = this;
			mWriteNodeWriteProtectToggle.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) {
				auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

				thisptr->OnToggleWriteProtect();

				uintptr w = thisptr->mCoProc.GetWriteMap()[((addr >> 8) - 0x40) & 0xFF];

				if (w & 1) {
					const auto& writeNode = *(const ATCoProcWriteMemNode *)(w - 1);

					writeNode.mpWrite(addr, val, writeNode.mpThis);
				} else {
					*(uint8 *)(w + addr) = val;
				}
			};

			mmapView.SetHandlers(0x40, 0x40, mReadNodeWriteProtectToggle, mWriteNodeWriteProtectToggle);

			// map RAM to $8000-9FFF and $A000-BFFF
			mmapView.SetMemory(0x80, 0x20, mRAM + 0x100);
			mmapView.SetMemory(0xA0, 0x20, mRAM + 0x100);

			// hook $9800-9FFF and $B800-BFFF for slow/fast switch reading
			mReadNodeFastSlowToggle.mpDebugRead = [](uint32 addr, void *thisptr0) {
				auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;
			
				return thisptr->mRAM[0x100 + ((addr - 0x8000) & 0x1FFF)];
			};

			mReadNodeFastSlowToggle.mpRead = [](uint32 addr, void *thisptr0) {
				auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

				thisptr->OnToggleFastSlow();
			
				return thisptr->mRAM[0x100 + ((addr - 0x8000) & 0x1FFF)];
			};

			mReadNodeFastSlowToggle.mpThis = this;

			mWriteNodeFastSlowToggle.mpThis = this;
			mWriteNodeFastSlowToggle.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) {
				auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

				thisptr->OnToggleFastSlow();			
				thisptr->mRAM[0x100 + ((addr - 0x8000) & 0x1FFF)] = val;
			};

			mmapView.SetHandlers(0x98, 0x08, mReadNodeFastSlowToggle, mWriteNodeFastSlowToggle);
			mmapView.SetHandlers(0xB8, 0x08, mReadNodeFastSlowToggle, mWriteNodeFastSlowToggle);

			// map ROM to $3xxx, $7xxx, $Bxxx, $Fxxx
			mmapView.SetReadMem(0x30, 0x10, mROM);
			mmapView.SetReadMem(0x70, 0x10, mROM);
			mmapView.SetReadMem(0xB0, 0x10, mROM);
			mmapView.SetReadMem(0xF0, 0x10, mROM);

			// set up ROM banking registers
			mReadNodeROMBankSwitch.mpThis = this;
			mWriteNodeROMBankSwitch.mpThis = this;

			mReadNodeROMBankSwitch.mpRead = [](uint32 addr, void *thisptr0) -> uint8 {
				auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;
				const uint8 v = thisptr->mbROMBankAlt ? thisptr->mROM[0x1000 + (addr & 0xFFF)] : thisptr->mROM[addr & 0xFFF];

				if ((addr & 0x0FFE) == 0x0FF8) {
					bool altBank = (addr & 1) != 0;

					if (thisptr->mbROMBankAlt != altBank) {
						thisptr->mbROMBankAlt = altBank;

						thisptr->UpdateROMBankHappy1050();
					}
				}

				return v;
			};

			mReadNodeROMBankSwitch.mpDebugRead = [](uint32 addr, void *thisptr0) -> uint8 {
				auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

				return thisptr->mbROMBankAlt ? thisptr->mROM[0x1000 + (addr & 0xFFF)] : thisptr->mROM[addr & 0xFFF];
			};


			mWriteNodeROMBankSwitch.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) {
				auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

				if ((addr & 0x0FFE) == 0x0FF8) {
					bool altBank = (addr & 1) != 0;

					if (thisptr->mbROMBankAlt != altBank) {
						thisptr->mbROMBankAlt = altBank;

						thisptr->UpdateROMBankHappy1050();
					}
				}
			};

			mmapView.SetHandlers(0x2F, 0x01, mReadNodeROMBankSwitch, mWriteNodeROMBankSwitch);
			mmapView.SetHandlers(0x3F, 0x01, mReadNodeROMBankSwitch, mWriteNodeROMBankSwitch);
			mmapView.SetHandlers(0x6F, 0x01, mReadNodeROMBankSwitch, mWriteNodeROMBankSwitch);
			mmapView.SetHandlers(0x7F, 0x01, mReadNodeROMBankSwitch, mWriteNodeROMBankSwitch);
			mmapView.SetHandlers(0xAF, 0x01, mReadNodeROMBankSwitch, mWriteNodeROMBankSwitch);
			mmapView.SetHandlers(0xBF, 0x01, mReadNodeROMBankSwitch, mWriteNodeROMBankSwitch);
			mmapView.SetHandlers(0xEF, 0x01, mReadNodeROMBankSwitch, mWriteNodeROMBankSwitch);
			mmapView.SetHandlers(0xFF, 0x01, mReadNodeROMBankSwitch, mWriteNodeROMBankSwitch);
		} else if (mDeviceType == kDeviceType_ISPlate) {
			// The I.S. Plate uses a 6502 instead of a 6507, so no 8K mirroring:
			//	$0000-7FFF	I/O (6507 $0000-0FFF mirrored 8x)
			//	$8000-9FFF	RAM (4K #1)
			//	$C000-DFFF	RAM (4K #2)
			//	$E000-FFFF	ROM (4K mirrored)

			mmapView.MirrorFwd(0x10, 0x70, 0x00);

			// Map 8K of RAM to $8000-9FFF and another 8K to $C000-DFFF.
			mmapView.SetMemory(0x80, 0x20, mRAM + 0x100);
			mmapView.SetMemory(0xC0, 0x20, mRAM + 0x2100);

			// map 8K ROM to $E000-FFFF
			mmapView.SetReadMem(0xE0, 0x20, mROM);
		} else if (mDeviceType == kDeviceType_Speedy1050) {
			// The Speedy 1050 uses a 65C02 instead of a 6507, so no 8K mirroring.
			// Fortunately, its hardware map is well documented.

			// unmapped $1000-DFFF

			// map RAM to $8000-9FFF
			mmapView.SetMemory(0x80, 0x20, mRAM + 0x100);

			// map ROM to $C000-FFFF
			mmapView.SetReadMem(0xC0, 0x40, mROM);
		} else {
			// 2332 ROM: A12=1 ($1000-1FFF / $F000-FFFF)
			mmapView.SetReadMem	(0x10, 0x10, mROM);

			if (mDeviceType == kDeviceType_1050Turbo) {
				// The ROM entries for $1800-1FFF will be overwritten with the correct ones when
				// we apply the initial ROM banking, but we need to set up the $1000-17FF control
				// entries here.
				mmapView.SetReadHandler(0x10, 0x08, mReadNodeROMBankSwitch);

				mReadNodeROMBankSwitch.mpThis = this;
				mReadNodeROMBankSwitch.mpDebugRead = [](uint32 addr, void *thisptr0) -> uint8 {
					auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;
				
					return thisptr->mROM[thisptr->mROMBank * 0x0800 + (addr & 0x07FF)];
				};

				mReadNodeROMBankSwitch.mpRead = [](uint32 addr, void *thisptr0) {
					auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;
				
					const uint8 v = thisptr->mROM[thisptr->mROMBank * 0x0800 + (addr & 0x07FF)];

					// This definitely needs explanation.
					//
					// Bank switching, and the Centronics interface, is controlled by a GAL16V8 sitting
					// between the data bus and the ROM chip. When the ROM is accessed with $1000-17FF
					// instead of $1800-1FFF, the *data* coming from the ROM chip is interpreted to either
					// switch the 2K ROM bank if D7=0 or drive the Centronics printer port if D7=1.
					//
					// See Engl, Bernhard, "Technische Dokumentation zur 1050 Turbo", ABBUC, 2004.

					if (!(v & 0x80)) {
						const uint8 newBank = (v & 3);

						if (thisptr->mROMBank != newBank) {
							thisptr->mROMBank = newBank;

							thisptr->UpdateROMBank1050Turbo();
						}
					}

					return v;
				};
			} else if (mDeviceType == kDeviceType_1050TurboII) {
				// The ROM entries for $1800-1FFF will be overwritten with the correct ones when
				// we apply the initial ROM banking, but we need to set up the $1000-17FF control
				// entries here.
				mmapView.SetReadHandler(0x10, 0x08, mReadNodeROMBankSwitch);

				mReadNodeROMBankSwitch.mpThis = this;
				mReadNodeROMBankSwitch.mpDebugRead = [](uint32 addr, void *thisptr0) -> uint8 {
					auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;
				
					return thisptr->mROM[thisptr->mROMBank * 0x0800 + (addr & 0x07FF)];
				};

				mReadNodeROMBankSwitch.mpRead = [](uint32 addr, void *thisptr0) {
					auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;
				
					const uint8 v = thisptr->mROM[thisptr->mROMBank * 0x0800 + (addr & 0x07FF)];

					// This definitely needs explanation.
					//
					// Bank switching, and the Centronics interface, is controlled by a GAL16V8 sitting
					// between the data bus and the ROM chip. When the ROM is accessed with $1000-17FF
					// instead of $1800-1FFF, the *data* coming from the ROM chip is interpreted to either
					// switch the 2K ROM bank if D7=0 or drive the Centronics printer port if D7=1.
					//
					// See Engl, Bernhard, "Technische Dokumentation zur 1050 Turbo II", ABBUC, 2004.

					if (!(v & 0x80)) {
						const uint8 newBank = ((v & 0x50) ? 0x01 : 0x00) + (v & 0x20 ? 0x00 : 0x02);

						if (thisptr->mROMBank != newBank) {
							thisptr->mROMBank = newBank;

							thisptr->UpdateROMBank1050Turbo();
						}
					}

					return v;
				};
			} else if (mDeviceType == kDeviceType_SuperArchiver) {
				// Map 2K of RAM at $1000-17FF, with write through at $1800-1FFF.
				// Explanation: http://atariage.com/forums/topic/228814-searching-for-super-archiver-rom-dumps
				mmapView.SetReadMem(0x10, 0x08, mRAM + 0x100);
				mmapView.SetWriteMem(0x10, 0x08, mRAM + 0x100);
				mmapView.SetWriteMem(0x18, 0x08, mRAM + 0x100);
			} else if (mDeviceType == kDeviceType_Tygrys1050) {
				// Map 2K of RAM at $0800-$0FFF.
				mmapView.SetMemory(0x08, 0x08, mRAM + 0x100);
			} else if (mDeviceType == kDeviceType_1050) {
				// Remap the ROM as traceable since it has a fixed mapping.
				mmapView.SetReadMemTraceable(0x10, 0x10, mROM);
			}

			mmapView.MirrorFwd(0x20, 0xE0, 0x00);
		}
	} else {
		// == 810 base hardware ==
		//
		// set up lower half of memory map
		writemap[0] = (uintptr)&mWriteNodeFDCRAM + 1;
		writemap[2] = (uintptr)&mWriteNodeFDCRAM + 1;
		writemap[4] = (uintptr)&mWriteNodeFDCRAM + 1;
		writemap[6] = (uintptr)&mWriteNodeFDCRAM + 1;
		writemap[1] = (uintptr)&mWriteNodeRIOTRAM + 1;
		writemap[5] = (uintptr)&mWriteNodeRIOTRAM + 1;
		writemap[3] = (uintptr)&mWriteNodeRIOTRegisters + 1;
		writemap[7] = (uintptr)&mWriteNodeRIOTRegisters + 1;

		readmap[0] = (uintptr)&mReadNodeFDCRAM + 1;
		readmap[2] = (uintptr)&mReadNodeFDCRAM + 1;
		readmap[4] = (uintptr)&mReadNodeFDCRAM + 1;
		readmap[6] = (uintptr)&mReadNodeFDCRAM + 1;
		readmap[1] = (uintptr)&mReadNodeRIOTRAM + 1;
		readmap[5] = (uintptr)&mReadNodeRIOTRAM + 1;
		readmap[3] = (uintptr)&mReadNodeRIOTRegisters + 1;
		readmap[7] = (uintptr)&mReadNodeRIOTRegisters + 1;

		// Replicate read and write maps to $1000-17FF since A12 is ignored
		mmapView.MirrorFwd(0x10, 0x08, 0x00);

		if (mDeviceType == kDeviceType_Happy810) {
			// setup RAM entries ($0800-13FF)
			mmapView.SetMemory(0x08, 0x0C, mRAM + 0x100);

			// setup ROM entries ($1400-1FFF), ignore first 1K of ROM (not visible to CPU)
			mmapView.SetReadMem(0x14, 0x0C, mROM + 0x400);
			mmapView.RepeatWritePage(0x14, 0x0C, mDummyWrite);

			// setup bank switching
			mmapView.SetHandlers(0x1F, 0x01, mReadNodeROMBankSwitch, mWriteNodeROMBankSwitch);

			mReadNodeROMBankSwitch.mpThis = this;
			mWriteNodeROMBankSwitch.mpThis = this;

			mReadNodeROMBankSwitch.mpRead = [](uint32 addr, void *thisptr0) -> uint8 {
				auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;
				const uint8 v = thisptr->mbROMBankAlt ? thisptr->mROM[0x1000 + (addr & 0xFFF)] : thisptr->mROM[addr & 0xFFF];

				if ((addr & 0x1FFE) == 0x1FF8) {
					bool altBank = (addr & 1) != 0;

					if (thisptr->mbROMBankAlt != altBank) {
						thisptr->mbROMBankAlt = altBank;

						thisptr->UpdateROMBankHappy810();
					}
				}

				return v;
			};

			mReadNodeROMBankSwitch.mpDebugRead = [](uint32 addr, void *thisptr0) -> uint8 {
				auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

				return thisptr->mbROMBankAlt ? thisptr->mROM[0x1000 + (addr & 0xFFF)] : thisptr->mROM[addr & 0xFFF];
			};


			mWriteNodeROMBankSwitch.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) {
				auto *thisptr = (ATDeviceDiskDriveFull *)thisptr0;

				if ((addr & 0x1FFE) == 0x1FF8) {
					bool altBank = (addr & 1) != 0;

					if (thisptr->mbROMBankAlt != altBank) {
						thisptr->mbROMBankAlt = altBank;

						thisptr->UpdateROMBankHappy810();
					}
				}
			};
		} else if (mDeviceType == kDeviceType_810Turbo) {
			// Set RAM at $0800-17FF.
			// Set ROM at $1800-1FFF.
			mmapView.SetMemory(0x08, 0x10, mRAM + 0x100);
			mmapView.SetReadMemTraceable(0x18, 0x08, mROM);
		} else {
			// Set ROM entries at $0800-0FFF and $1800-1FFF.
			mmapView.SetReadMemTraceable(0x08, 0x08, mROM);
			mmapView.SetReadMemTraceable(0x18, 0x08, mROM);
		}

		// replicate $0000-1FFF upward since 6507 lacks A13-A15
		mmapView.MirrorFwd(0x20, 0xE0, 0x00);
	}

	if (mb1050)
		mRIOT.SetOnIrqChanged([this](bool state) {
			mRIOT.SetInputA(state ? 0x00 : 0x40, 0x40);
			mFDC.OnIndexPulse((mRIOT.ReadOutputA() & 0x40) == 0);
		});
	else
		mRIOT.SetOnIrqChanged([this](bool state) { mFDC.OnIndexPulse(state); });

	mRIOT.Init(&mDriveScheduler);
	mRIOT.Reset();

	// Clear port B bit 1 (/READY) and bit 7 (/DATAOUT)
	mRIOT.SetInputB(0x00, 0x82);

	// Set port A bits 0 and 2 to select D1:., and clear DRQ/IRQ from FDC
	if (mb1050) {
		static const uint8 k1050IDs[4]={
			0x03, 0x01, 0x00, 0x02
		};

		mRIOT.SetInputA(k1050IDs[mDriveId & 3], 0xC3);	// D1:
	} else {
		static const uint8 k810IDs[4]={
			0x05, 0x04, 0x00, 0x01
		};

		mRIOT.SetInputA(k810IDs[mDriveId & 3], 0xC5);	// D1:
	}

	if (mb1050) {
		// Pull /VCC RDY
		mRIOT.SetInputB(0, 0x02);

		// Deassert /IRQ
		mRIOT.SetInputA(0x40, 0x40);
	}

	// In general, Atari-compatible disk drives use FDCs that spec timings in their datasheets
	// for 2MHz, but run them at 1MHz in 8" mode. Therefore, we push in a period factor of 2x
	// to scale all of the timings appropriately.
	mFDC.Init(&mDriveScheduler, 288.0f, mb1050 ? 2.0f : 1.0f, mb1050 || mDeviceType == kDeviceType_810Turbo ? ATFDCEmulator::kType_2793 : ATFDCEmulator::kType_1771);

	if (mb1050)
		mFDC.SetDoubleClock(true);

	mFDC.SetDiskInterface(mpDiskInterface);
	mFDC.SetOnDrqChange([this](bool drq) { mRIOT.SetInputA(drq ? 0x80 : 0x00, 0x80); });

	if (!mb1050)
		mFDC.SetOnIrqChange([this](bool irq) { mRIOT.SetInputA(irq ? 0x40 : 0x00, 0x40); });

	if (mDeviceType == kDeviceType_Happy1050)
		mFDC.SetOnWriteEnabled([this] { OnWriteEnabled(); });

	mDriveScheduler.UnsetEvent(mpEventDriveDiskChange);
	mDiskChangeState = 0;
	OnDiskChanged(false);

	OnWriteModeChanged();
	OnTimingModeChanged();
	OnAudioModeChanged();

	UpdateRotationStatus();
	UpdateROMBank();
	UpdateROMBankSuperArchiver();
	UpdateROMBank1050Turbo();
	UpdateSlowSwitch();
	UpdateAutoSpeed();
}

void ATDeviceDiskDriveFull::Shutdown() {
	mAudioPlayer.Shutdown();
	mSerialXmitQueue.Shutdown();

	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
	ShutdownTargetControl();

	mpFwMgr = nullptr;

	if (mpSIOMgr) {
		mpSIOMgr->RemoveRawDevice(this);
		mpSIOMgr = nullptr;
	}

	mFDC.Shutdown();

	if (mpDiskInterface) {
		mpDiskInterface->SetShowMotorActive(false);
		mpDiskInterface->SetShowActivity(false, 0);
		mpDiskInterface->RemoveClient(this);
		mpDiskInterface = nullptr;
	}

	mpDiskDriveManager = nullptr;
}

uint32 ATDeviceDiskDriveFull::GetComputerPowerOnDelay() const {
	switch(mDeviceType) {
		case kDeviceType_1050Duplicator:
			return 10;

		default:
			return 0;
	}
}

void ATDeviceDiskDriveFull::WarmReset() {
	// If the computer resets, its transmission is interrupted.
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
}

void ATDeviceDiskDriveFull::ComputerColdReset() {
	WarmReset();
}

void ATDeviceDiskDriveFull::PeripheralColdReset() {
	memset(mRAM, 0, sizeof mRAM);

	mRIOT.Reset();
	mFDC.Reset();

	// clear DRQ/IRQ from FDC -> RIOT port A
	if (mb1050)
		mRIOT.SetInputA(0x00, 0x80);
	else
		mRIOT.SetInputA(0x00, 0xC0);

	mSerialXmitQueue.Reset();
	
	// start the disk drive on a track other than 0/20/39, just to make things interesting
	mCurrentTrack = mb1050 ? 20 : 10;
	mFDC.SetCurrentTrack(mCurrentTrack, false);

	mFDC.SetMotorRunning(!mb1050);
	mFDC.SetDensity(mb1050);
	mFDC.SetWriteProtectOverride(false);

	ClearWriteProtect();
	mbFastSlowToggle = false;

	mbROMBankAlt = true;
	mROMBank = 3;
	UpdateROMBankHappy810();
	UpdateROMBankHappy1050();
	UpdateROMBank1050Turbo();
	
	mCoProc.ColdReset();

	ResetTargetControl();

	// need to update motor and sound status, since the 810 starts with the motor on
	UpdateRotationStatus();

	// if we're in 1050 Turbo mode, turn on the startup hack to force density
	// detection to work around a bug with the firmware not doing so
	if (mDeviceType == kDeviceType_1050Turbo && mpDiskInterface->IsDiskLoaded())
		mFDC.SetDiskChangeStartupHackEnabled(g_ATCVFullDisk1050TurboForceDensityDetect);

	WarmReset();
}

void ATDeviceDiskDriveFull::InitFirmware(ATFirmwareManager *fwman) {
	mpFwMgr = fwman;

	ReloadFirmware();
}

bool ATDeviceDiskDriveFull::ReloadFirmware() {
	static const ATFirmwareType kFirmwareTypes[]={
		kATFirmwareType_810,
		kATFirmwareType_Happy810,
		kATFirmwareType_810Archiver,
		kATFirmwareType_1050,
		kATFirmwareType_USDoubler,
		kATFirmwareType_Speedy1050,
		kATFirmwareType_Happy1050,
		kATFirmwareType_SuperArchiver,
		kATFirmwareType_TOMS1050,
		kATFirmwareType_Tygrys1050,
		kATFirmwareType_1050Duplicator,
		kATFirmwareType_1050Turbo,
		kATFirmwareType_1050TurboII,
		kATFirmwareType_ISPlate,
		kATFirmwareType_810Turbo,
	};

	static const uint32 kFirmwareSizes[]={
		0x800,
		0x2000,
		0x1000,
		0x1000,
		0x1000,
		0x4000,
		0x2000,
		0x1000,
		0x2000,
		0x1000,
		0x2000,
		0x4000,
		0x2000,
		0x1000,
		0x800
	};

	static_assert(vdcountof(kFirmwareTypes) == kDeviceTypeCount, "firmware type array missized");
	static_assert(vdcountof(kFirmwareSizes) == kDeviceTypeCount, "firmware size array missized");

	const uint64 id = mpFwMgr->GetFirmwareOfType(kFirmwareTypes[mDeviceType], true);
	
	const vduint128 oldHash = VDHash128(mROM, sizeof mROM);

	uint8 firmware[sizeof(mROM)] = {};

	uint32 len = 0;
	mpFwMgr->LoadFirmware(id, firmware, 0, kFirmwareSizes[mDeviceType], nullptr, &len, nullptr, nullptr, &mbFirmwareUsable);

	// If we're in Happy 810 mode, check whether we only have the visible 3K/6K of the
	// ROM or a full 4K/8K image. Both are supported.
	if (mDeviceType == kDeviceType_Happy810) {
		if (len == 0xC00) {
			// 3K firmware
			memmove(firmware + 0x400, firmware, 0xC00);
			memset(firmware, 0, 0x400);
		} else if (len == 0x1000) {
			// 4K firmware
			memcpy(firmware + 0x1000, firmware, 0x1000);
		} else if (len == 0x1800) {
			// 6K firmware
			memmove(firmware + 0x1400, firmware + 0xC00, 0xC00);
			memmove(firmware + 0x400, firmware, 0xC00);
			memset(firmware, 0, 0x400);
			memset(firmware + 0x1000, 0, 0x400);
		}
	}

	// double-up 8K Speedy 1050 firmware to 16K
	if (mDeviceType == kDeviceType_Speedy1050 && len == 0x2000)
		memcpy(firmware + 0x2000, firmware, 0x2000);

	// double-up 4K I.S. Plate firmware to 8K
	if (mDeviceType == kDeviceType_ISPlate && len == 0x1000)
		memcpy(firmware + 0x1000, firmware, 0x1000);

	memcpy(mROM, firmware, sizeof mROM);

	mCoProc.InvalidateTraceCache();

	const vduint128 newHash = VDHash128(mROM, sizeof mROM);

	return oldHash != newHash;
}

const wchar_t *ATDeviceDiskDriveFull::GetWritableFirmwareDesc(uint32 idx) const {
	return nullptr;
}

bool ATDeviceDiskDriveFull::IsWritableFirmwareDirty(uint32 idx) const {
	return false;
}

void ATDeviceDiskDriveFull::SaveWritableFirmware(uint32 idx, IVDStream& stream) {
}

ATDeviceFirmwareStatus ATDeviceDiskDriveFull::GetFirmwareStatus() const {
	return mbFirmwareUsable ? ATDeviceFirmwareStatus::OK : ATDeviceFirmwareStatus::Missing;
}

void ATDeviceDiskDriveFull::InitDiskDrive(IATDiskDriveManager *ddm) {
	mpDiskDriveManager = ddm;
	mpDiskInterface = ddm->GetDiskInterface(mDriveId);
	mpDiskInterface->AddClient(this);
}

ATDeviceDiskDriveInterfaceClient ATDeviceDiskDriveFull::GetDiskInterfaceClient(uint32 index) {
	return index ? ATDeviceDiskDriveInterfaceClient{} : ATDeviceDiskDriveInterfaceClient{ this, mDriveId };
}

void ATDeviceDiskDriveFull::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

uint32 ATDeviceDiskDriveFull::GetSupportedButtons() const {
	if (mDeviceType == kDeviceType_Happy1050) {
		return (1U << kATDeviceButton_HappySlow)
			| (1U << kATDeviceButton_HappyWPDisable)
			| (1U << kATDeviceButton_HappyWPEnable)
			;
	} else if (mDeviceType == kDeviceType_Happy810) {
		return (1U << kATDeviceButton_HappySlow);
	} else {
		return 0;
	}
}

bool ATDeviceDiskDriveFull::IsButtonDepressed(ATDeviceButton idx) const {
	switch(idx) {
		case kATDeviceButton_HappySlow:
			return mbSlowSwitch;

		case kATDeviceButton_HappyWPDisable:
			return mbWPDisable;

		case kATDeviceButton_HappyWPEnable:
			return mbWPEnable;

		default:
			return false;
	}
}

void ATDeviceDiskDriveFull::ActivateButton(ATDeviceButton idx, bool state) {
	switch(idx) {
		case kATDeviceButton_HappySlow:
			if (mDeviceType == kDeviceType_Happy1050 || mDeviceType == kDeviceType_Happy810) {
				mbSlowSwitch = state;
				UpdateSlowSwitch();
			}
			break;

		case kATDeviceButton_HappyWPDisable:
			if (mDeviceType == kDeviceType_Happy1050 && mbWPDisable != state) {
				mbWPDisable = state;
				if (state)
					mbWPEnable = false;

				UpdateWriteProtectOverride();
			}
			break;

		case kATDeviceButton_HappyWPEnable:
			if (mDeviceType == kDeviceType_Happy1050 && mbWPEnable != state) {
				mbWPEnable = state;
				if (state)
					mbWPDisable = false;

				UpdateWriteProtectOverride();
			}
			break;
	}
}

void ATDeviceDiskDriveFull::OnScheduledEvent(uint32 id) {
	if (id == kEventId_DriveReceiveBit) {
		const bool newState = (mReceiveShiftRegister & 1) != 0;

		mReceiveShiftRegister >>= 1;
		mpEventDriveReceiveBit = nullptr;

		if (mReceiveShiftRegister) {
			mReceiveTimingAccum += mReceiveTimingStep;
			mpEventDriveReceiveBit = mDriveScheduler.AddEvent(mReceiveTimingAccum >> 10, this, kEventId_DriveReceiveBit);
			mReceiveTimingAccum &= 0x3FF;
		}

		if (mb1050)
			mRIOT.SetInputB(newState ? 0x00 : 0x40, 0x40);
		else
			mRIOT.SetInputB(newState ? 0x00 : 0x80, 0x80);
	} else if (id == kEventId_DriveDiskChange) {
		mpEventDriveDiskChange = nullptr;

		switch(++mDiskChangeState) {
			case 1:		// disk being removed (write protect covered)
			case 2:		// disk removed (write protect clear)
			case 3:		// disk being inserted (write protect covered)
				mDriveScheduler.SetEvent(mb1050 ? kDiskChangeStepMS : kDiskChangeStepMS >> 1, this, kEventId_DriveDiskChange, mpEventDriveDiskChange);
				break;

			case 4:		// disk inserted (write protect normal)
				mDiskChangeState = 0;
				break;
		}

		UpdateDiskStatus();
	} else
		return ATDiskDriveDebugTargetControl::OnScheduledEvent(id);
}

void ATDeviceDiskDriveFull::OnCommandStateChanged(bool asserted) {
	Sync();

	if (mb1050)
		mRIOT.SetInputB(asserted ? 0xFF : 0x00, 0x80);
	else
		mRIOT.SetInputB(asserted ? 0xFF : 0x00, 0x40);
}

void ATDeviceDiskDriveFull::OnMotorStateChanged(bool asserted) {
}

void ATDeviceDiskDriveFull::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	Sync();

	mReceiveShiftRegister = c + c + 0x200;

	// The conversion fraction we need here is 64/229, but that denominator is awkward.
	// Approximate it with 286/1024.
	mReceiveTimingAccum = 0x200;
	mReceiveTimingStep = mb1050 || mDeviceType == kDeviceType_810Turbo ? cyclesPerBit * 572 : cyclesPerBit * 286;

	mDriveScheduler.SetEvent(1, this, kEventId_DriveReceiveBit, mpEventDriveReceiveBit);
}

void ATDeviceDiskDriveFull::OnSendReady() {
}

void ATDeviceDiskDriveFull::OnDiskChanged(bool mediaRemoved) {
	if (mediaRemoved) {
		mDiskChangeState = 0;
		mDriveScheduler.SetEvent(1, this, kEventId_DriveDiskChange, mpEventDriveDiskChange);
	}

	UpdateDiskStatus();
}

void ATDeviceDiskDriveFull::OnWriteModeChanged() {
	UpdateWriteProtectStatus();
}

void ATDeviceDiskDriveFull::OnTimingModeChanged() {
	// The I.S. Plate requires accurate sector timing because of a bug in its
	// firmware where it overwrites the stack if more than about 100 sectors are
	// found during one disk rotation. Since it doesn't check the index mark,
	// having accurate sector timing disabled allows the firmware to see the same
	// sectors over and over, causing it to overwrite the stack through page 0-1
	// mirroring.
	mFDC.SetAccurateTimingEnabled(mDeviceType == kDeviceType_ISPlate || mpDiskInterface->IsAccurateSectorTimingEnabled());
}

void ATDeviceDiskDriveFull::OnAudioModeChanged() {
	mbSoundsEnabled = mpDiskInterface->AreDriveSoundsEnabled();

	UpdateRotationStatus();
}

bool ATDeviceDiskDriveFull::IsImageSupported(const IATDiskImage& image) const {
	// 810s only support single density, unless it's the 810 Turbo.
	// 1050s and the 810 Turbo can support double density.

	const auto& geo = image.GetGeometry();

	if (geo.mbHighDensity)
		return false;

	if (!mb1050 && mDeviceType != kDeviceType_810Turbo) {
		if (geo.mbMFM)
			return false;
	}

	if (geo.mSectorSize > 256)
		return false;

	if (geo.mSectorsPerTrack > 26)
		return false;

	if (geo.mTrackCount > 80)
		return false;

	return true;
}

void ATDeviceDiskDriveFull::Sync() {
	uint32 newDriveCycleLimit = AccumSubCycles();

	bool ranToCompletion = true;

	VDASSERT(mDriveScheduler.mNextEventCounter >= 0xFF000000);
	if (ATSCHEDULER_GETTIME(&mDriveScheduler) - newDriveCycleLimit >= 0x80000000) {
		mDriveScheduler.SetStopTime(newDriveCycleLimit);
		ranToCompletion = mCoProc.Run(mDriveScheduler);

		VDASSERT(ATWrapTime{ATSCHEDULER_GETTIME(&mDriveScheduler)} <= newDriveCycleLimit);
	}

	if (!ranToCompletion)
		ScheduleImmediateResume();

	FlushStepNotifications();
}

void ATDeviceDiskDriveFull::AddTransmitEdge(bool polarity) {
	mSerialXmitQueue.AddTransmitBit(DriveTimeToMasterTime() + mSerialXmitQueue.kTransmitLatency, polarity);
}

void ATDeviceDiskDriveFull::OnRIOTRegisterWrite(uint32 addr, uint8 val) {
	// check for a write to DRA or DDRA
	if ((addr & 6) == 0) {
		// compare outputs before and after write
		const uint8 outprev = mRIOT.ReadOutputA();
		mRIOT.WriteByte((uint8)addr, val);
		const uint8 outnext = mRIOT.ReadOutputA();

		// check for density change
		const uint8 delta = outprev ^ outnext;

		if (mb1050) {
			if (delta & 0x20)
				mFDC.SetDensity(!(outnext & 0x20));
		} else if (mDeviceType == kDeviceType_810Turbo) {
			if (delta & 0x08)
				mFDC.SetDensity(!(outnext & 0x08));
		}

		// check for a spindle motor state change
		if (delta & (mb1050 ? 8 : 2)) {
			const bool running = mb1050 ? (outnext & 8) == 0 : (outnext & 2) != 0;
			mFDC.SetMotorRunning(running);

			UpdateRotationStatus();
		}

		// check for a ROM bank change (Archiver)
		if (mDeviceType == kDeviceType_810Archiver && (delta & 8))
			UpdateROMBank();

		// check for a ROM bank change (Super Archiver)
		if (mDeviceType == kDeviceType_SuperArchiver && (delta & 4))
			UpdateROMBankSuperArchiver();

		// check for SLOW sense and autospeed (Happy 810)
		if (mDeviceType == kDeviceType_Happy810) {
			if (delta & 8)
				UpdateSlowSwitch();

			if ((delta & 0x20) && mbHappy810AutoSpeed)
				UpdateAutoSpeed();
		}

		// check for index pulse change (1050)
		if (mb1050 && (delta & 0x40)) {
			mFDC.OnIndexPulse((mRIOT.ReadOutputA() & 0x40) == 0);
		}
		return;
	}

	// check for a write to DRB or DDRB
	if ((addr & 6) == 2) {
		// compare outputs before and after write
		const uint8 outprev = mRIOT.ReadOutputB();
		mRIOT.WriteByte((uint8)addr, val);
		const uint8 outnext = mRIOT.ReadOutputB();
		const uint8 outdelta = outprev ^ outnext;

		// check for transition on PB0 (SIO input)
		if (outdelta & 1)
			AddTransmitEdge((outnext & 1) != 0);

		// check for stepping transition
		if (outdelta & 0x3C) {
			// OK, now compare the phases. The 810 has track 0 at a phase pattern of
			// 0x24 (%1001); seeks toward track 0 rotate to lower phases (right shift).
			// If we don't have exactly two phases energized, ignore the change. If
			// the change inverts all phases, ignore it.

			static const sint8 kOffsetTables[2][16]={
				// 810 (two adjacent phases required, noninverted)
				{	
					-1, -1, -1,  1,
					-1, -1,  2, -1,
					-1,  0, -1, -1,
					 3, -1, -1, -1
				},

				// 1050 (one phase required, inverted)
				{
					-1, -1, -1, -1,
					-1, -1, -1,  0,
					-1, -1, -1,  1,
					-1,  2,  3, -1
				}
			};

			const sint8 newOffset = kOffsetTables[mb1050][(outnext >> 2) & 15];

			g_ATLCDiskEmu("Stepper phases now: %X\n", outnext & 0x3C);

			if (newOffset >= 0) {
				switch(((uint32)newOffset - mCurrentTrack) & 3) {
					case 1:		// step in (increasing track number)
						if (mCurrentTrack < (mb1050 ? 90U : 45U)) {
							++mCurrentTrack;

							if (mb1050)
								mFDC.SetCurrentTrack(mCurrentTrack, mCurrentTrack >= 2);
							else
								mFDC.SetCurrentTrack(mCurrentTrack << 1, false);
						}

						PlayStepSound();
						break;

					case 3:		// step out (decreasing track number)
						if (mCurrentTrack > 0) {
							--mCurrentTrack;

							if (mb1050)
								mFDC.SetCurrentTrack(mCurrentTrack, mCurrentTrack >= 2);
							else
								mFDC.SetCurrentTrack(mCurrentTrack << 1, false);

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
	} else {
		mRIOT.WriteByte((uint8)addr, val);
	}
}

void ATDeviceDiskDriveFull::PlayStepSound() {
	if (!mbSoundsEnabled)
		return;

	const uint32 t = ATSCHEDULER_GETTIME(&mDriveScheduler);
	
	if (t - mLastStepSoundTime > 50000)
		mLastStepPhase = 0;

	if (mb1050)
		mAudioPlayer.PlayStepSound(kATAudioSampleId_DiskStep2H, 0.3f + 0.7f * cosf((float)mLastStepPhase++ * nsVDMath::kfPi));
	else
		mAudioPlayer.PlayStepSound(kATAudioSampleId_DiskStep1, 0.3f + 0.7f * cosf((float)mLastStepPhase++ * nsVDMath::kfPi * 0.5f));

	mLastStepSoundTime = t;
}

void ATDeviceDiskDriveFull::UpdateRotationStatus() {
	bool motorEnabled;

	if (mb1050)
		motorEnabled = (mRIOT.ReadOutputA() & 8) == 0;
	else
		motorEnabled = (mRIOT.ReadOutputA() & 2) != 0;

	mpDiskInterface->SetShowMotorActive(motorEnabled);

	mAudioPlayer.SetRotationSoundEnabled(motorEnabled && mbSoundsEnabled);
}

void ATDeviceDiskDriveFull::UpdateROMBank() {
	if (mDeviceType == kDeviceType_810Archiver) {
		// Thanks to ijor for providing the ROM banking info for the Archiver.
		uintptr romref = ((mRIOT.ReadOutputA() & 8) ? (uintptr)(mROM + 0x800) : (uintptr)mROM) - 0x800;

		// Fixup ROM entries in read map (they must be different for each mirror).
		uintptr *rmdst = mCoProc.GetReadMap() + 8;

		for(int i=0; i<16; ++i) {
			std::fill(rmdst, rmdst + 8, romref);

			romref -= 0x1000;
			rmdst += 16;
		}
	}
}

void ATDeviceDiskDriveFull::UpdateROMBankSuperArchiver() {
	if (mDeviceType == kDeviceType_SuperArchiver) {
		uintptr romref = ((mRIOT.ReadOutputA() & 4) ? (uintptr)(mROM + 0x800) : (uintptr)mROM) - 0x1800;

		// Fixup ROM entries in read map (they must be different for each mirror).
		uintptr *rmdst = mCoProc.GetReadMap() + 0x18;

		for(int i=0; i<8; ++i) {
			std::fill(rmdst, rmdst + 8, romref);

			romref -= 0x2000;
			rmdst += 32;
		}
	}
}

void ATDeviceDiskDriveFull::UpdateROMBankHappy810()  {
	if (mDeviceType != kDeviceType_Happy810)
		return;

	uintptr *readmap = mCoProc.GetReadMap();
	const uint8 *romBank = mbROMBankAlt ? mROM + 0x1000 : mROM;

	// Ignore the last page in each mirror as we need to keep the bank switching
	// node in place there. It automatically handles the bank switching itself.
	for(int i=0; i<256; i += 32)
		std::fill(readmap + 0x14 + i, readmap + 0x1F + i, (uintptr)romBank - 0x1000 - (i << 8));
}

void ATDeviceDiskDriveFull::UpdateROMBankHappy1050()  {
	if (mDeviceType != kDeviceType_Happy1050)
		return;

	uintptr *readmap = mCoProc.GetReadMap();
	const uint8 *romBank = mbROMBankAlt ? mROM + 0x1000 : mROM;

	// Ignore the last page in each mirror as we need to keep the bank switching
	// node in place there. It automatically handles the bank switching itself.
	std::fill(readmap + 0x30, readmap + 0x3F, (uintptr)romBank - 0x3000);
	std::fill(readmap + 0x70, readmap + 0x7F, (uintptr)romBank - 0x7000);
	std::fill(readmap + 0xB0, readmap + 0xBF, (uintptr)romBank - 0xB000);
	std::fill(readmap + 0xF0, readmap + 0xFF, (uintptr)romBank - 0xF000);
}

void ATDeviceDiskDriveFull::UpdateROMBank1050Turbo() {
	if (mDeviceType != kDeviceType_1050Turbo && mDeviceType != kDeviceType_1050TurboII)
		return;

	uintptr *readmap = mCoProc.GetReadMap();
	uintptr romval = (uintptr)mROM + 0x0800*mROMBank - 0x1800;

	for(int i=0; i<8; ++i) {
		std::fill(readmap + 0x18, readmap + 0x20, romval);
		readmap += 0x20;
		romval -= 0x2000;
	}
}

void ATDeviceDiskDriveFull::UpdateDiskStatus() {
	IATDiskImage *image = mpDiskInterface->GetDiskImage();

	mFDC.SetDiskImage(image, !mb1050 || (image != nullptr && mDiskChangeState == 0));

	if (!image)
		mFDC.SetDiskChangeStartupHackEnabled(false);

	UpdateWriteProtectStatus();
}

void ATDeviceDiskDriveFull::UpdateWriteProtectStatus() {
	const bool wpoverride = (mDiskChangeState & 1) != 0;
	const bool wpsense = mpDiskInterface->GetDiskImage() && !mpDiskInterface->IsDiskWritable();

	if (!mb1050)
		mRIOT.SetInputA(wpoverride || wpsense ? 0x10 : 0x00, 0x10);

	mFDC.SetWriteProtectOverride(wpoverride);
}

void ATDeviceDiskDriveFull::OnToggleFastSlow() {
	// Access to $9800-9FFF or $B800-BFFF. Toggle the fast/slow flip flop, and also
	// clear the write protect flip flop.
	if (mbSlowSwitch) {
		mbFastSlowToggle = !mbFastSlowToggle;

		if (!mbFastSlowToggle) {
			// negative transition -- pull SO line on CPU
			mCoProc.SetV();
		}
	}

	ClearWriteProtect();
}

void ATDeviceDiskDriveFull::ClearWriteProtect() {
	if (mbWPToggle && mDeviceType == kDeviceType_Happy1050) {
		mbWPToggle = false;

		UpdateWriteProtectOverride();
	}
}

void ATDeviceDiskDriveFull::OnToggleWriteProtect() {
	mbWPToggle = !mbWPToggle;

	UpdateWriteProtectOverride();
}

void ATDeviceDiskDriveFull::OnWriteEnabled() {
	// This is called when the FDC is set to invert or force enable writes, and
	// has had to enable writes on the disk interface. If we're in invert mode,
	// we need to flip our state and the FDC state so that the effective state on
	// the disk stays correct.
	if (!mbWPEnable && !mbWPDisable) {
		mbWPToggle = !mbWPToggle;

		UpdateWriteProtectOverride();
	}
}

void ATDeviceDiskDriveFull::UpdateWriteProtectOverride() {
	if (mDeviceType == kDeviceType_Happy1050) {
		mFDC.SetWriteProtectOverride2(
			mbWPEnable ? kATFDCWPOverride_WriteProtect
			: mbWPDisable ? kATFDCWPOverride_WriteEnable
			: mbWPToggle ? kATFDCWPOverride_Invert
			: kATFDCWPOverride_None);
	}
}

void ATDeviceDiskDriveFull::UpdateSlowSwitch() {
	if (mDeviceType == kDeviceType_Happy810) {
		// Connect PA3 to PA5 if in slow mode (PA3 will be the output).
		if (mbSlowSwitch)
			mRIOT.SetInputA((mRIOT.ReadOutputA() & 0x08) << 2, 0x20);
		else
			mRIOT.SetInputA(0x20, 0x20);
	}
}

void ATDeviceDiskDriveFull::UpdateAutoSpeed() {
	if (mDeviceType != kDeviceType_Happy810 || !mbHappy810AutoSpeed || (mRIOT.ReadOutputA() & 0x20))
		mFDC.SetSpeeds(288.0f, mb1050 ? 2.0f : 1.0f, mb1050);
	else
		mFDC.SetSpeeds(mHappy810AutoSpeedRate, mb1050 ? 2.0f : 1.0f, mb1050);
}
