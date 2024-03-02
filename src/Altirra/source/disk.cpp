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
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/math.h>
#include <vd2/system/binary.h>
#include <vd2/system/strutil.h>
#include "disk.h"
#include "pokey.h"
#include "console.h"
#include "cpu.h"
#include "simulator.h"
#include "debuggerlog.h"
#include "audiosyncmixer.h"
#include "cio.h"

#include "oshelper.h"
#include "resource.h"

extern ATLogChannel g_ATLCDisk;
extern ATLogChannel g_ATLCDiskCmd;
extern ATLogChannel g_ATLCDiskData;

extern ATSimulator g_sim;

namespace {
	// Cycles/second. This is only correct for NTSC, but it's close enough
	// for PAL for disk emulation purposes.
	const int kCyclesPerSecond = 7159090/4;

	// The 810 and 1050 rotate at 288 RPM.
	// The XF551 rotates at 300 RPM.
	const int kCyclesPerDiskRotation_288RPM = (kCyclesPerSecond * 60 + 144) / 288;
	const int kCyclesPerDiskRotation_300RPM = (kCyclesPerSecond + 2) / 5;

	// 810: 5.3ms step rate.
	// 1050: 20ms step rate.
	// XF551: 6ms step rate.
	const int kCyclesPerTrackStep_810		= (kCyclesPerSecond *  53 + 5000) / 10000;
	const int kCyclesPerTrackStep_810_3ms	= (kCyclesPerSecond *  30 + 5000) / 10000;
	const int kCyclesPerTrackStep_1050		= (kCyclesPerSecond * 200 + 5000) / 10000;
	const int kCyclesPerTrackStep_Speedy1050= (kCyclesPerSecond *  80 + 5000) / 10000;
	const int kCyclesPerTrackStep_XF551		= (kCyclesPerSecond *  60 + 5000) / 10000;

	// Cycles for a fake rotation as seen by the FDC. Neither the 810 nor the 1050
	// use the real index pulse; they fake it with the RIOT.
	//
	// 810: ~522ms
	// 1050: ~236ms
	//
	const int kCyclesPerFakeRot_810			= (kCyclesPerSecond * 522 + 5000) / 10000;
	const int kCyclesPerFakeRot_1050		= (kCyclesPerSecond * 236 + 5000) / 10000;

	// 810: 10ms head settling time.
	// 1050: 20ms head settling time.
	const int kCyclesForHeadSettle_810 = 17898;
	const int kCyclesForHeadSettle_1050 = 35795;

	// The bit cell rate is 1MHz.
	const int kBytesPerTrack = 26042;

	// Approx. number of cycles it takes for the CPU to send out the request.
	const int kCyclesToProcessRequest = 7000;

	const int kCyclesToExitSIO = 350;

	///////////////////////////////////////////////////////////////////////////////////
	// SIO timing parameters
	//
	// WARNING: KARATEKA IS VERY SENSITIVE TO THESE PARAMETERS AS IT HAS STUPIDLY
	//			CLOSE PHANTOM SECTORS.
	//

	// The number of cycles per byte sent over the SIO bus -- approximately 19200 baud.
	//
	// 810: 26 cycles/bit, 265 cycles/byte @ 500KHz
	// 1050: 51 cycles/bit, 549 cycles/byte @ 1MHz
	// US Doubler low speed: 53 cycles/bit, 534 cycles/byte @ 1MHz
	// US Doubler high speed: 19 cycles/bit, 220 cycles/byte @ 1MHz
	// Speedy 1050 low speed: 52 cycles/bit, 525 cycles/byte @ 1MHz
	// Speedy 1050 high speed: 18 cycles/bit, 214 cycles/byte @ 1MHz
	// XF551 low speed: 29 cycles/bit, 290 cycles/byte @ 555.5KHz
	// XF551 high speed: 14 cycles/bit, 140 cycles/byte @ 555.5KHz
	// IndusGT is a guess right now.
	//
	static const int kCyclesPerSIOByte_810 = 949;
	static const int kCyclesPerSIOByte_1050 = 982;
	static const int kCyclesPerSIOByte_PokeyDiv0 = 140;
	static const int kCyclesPerSIOByte_57600baud = 311;
	static const int kCyclesPerSIOByte_USDoubler = 956;
	static const int kCyclesPerSIOByte_USDoubler_Fast = 394;
	static const int kCyclesPerSIOByte_Speedy1050 = 940;
	static const int kCyclesPerSIOByte_Speedy1050_Fast = 383;
	static const int kCyclesPerSIOByte_XF551 = 934;
	static const int kCyclesPerSIOByte_XF551_Fast = 450;
	static const int kCyclesPerSIOByte_IndusGT = 930;
	static const int kCyclesPerSIOByte_IndusGT_Fast = 260;
	static const int kCyclesPerSIOByte_Happy = 956;
	static const int kCyclesPerSIOByte_Happy_Fast = 394;
	static const int kCyclesPerSIOByte_1050Turbo = 982;
	static const int kCyclesPerSIOByte_1050Turbo_Fast = 260;
	static const int kCyclesPerSIOBit_810 = 94;
	static const int kCyclesPerSIOBit_1050 = 91;
	static const int kCyclesPerSIOBit_PokeyDiv0 = 14;
	static const int kCyclesPerSIOBit_57600baud = 31;
	static const int kCyclesPerSIOBit_USDoubler = 95;
	static const int kCyclesPerSIOBit_USDoubler_Fast = 34;
	static const int kCyclesPerSIOBit_Speedy1050 = 93;
	static const int kCyclesPerSIOBit_Speedy1050_Fast = 32;
	static const int kCyclesPerSIOBit_XF551 = 93;
	static const int kCyclesPerSIOBit_XF551_Fast = 45;
	static const int kCyclesPerSIOBit_IndusGT = 93;
	static const int kCyclesPerSIOBit_IndusGT_Fast = 26;
	static const int kCyclesPerSIOBit_Happy = 95;
	static const int kCyclesPerSIOBit_Happy_Fast = 34;
	static const int kCyclesPerSIOBit_1050Turbo = 91;
	static const int kCyclesPerSIOBit_1050Turbo_Fast = 26;

	// Delay from command line deasserting to end of ACK byte.
	//
	// 810: ~294 cycles @ 500KHz = ~1053 cycles @ 1.79MHz.
	static const int kCyclesACKDelay = 1053;

	// Delay from end of ACK byte until FDC command is sent.
	// 810: ~1608 cycles @ 500KHz = ~5756 cycles @ 1.79MHz.
	static const int kCyclesFDCCommandDelay = 5756;

	// Delay from end of ACK byte to end of first data byte, not counting rotational delay.
	static const int kCyclesCompleteDelay_Fast = 2000;
	static const int kCyclesCompleteDelay_Accurate = 28000;
	static const int kCyclesCompleteDelay_Accurate_ED = 19100;

	// Delay from end of Complete byte to end of first data byte, at high speed.
	static const int kCyclesToFirstDataHighSpeed = 945;

	static const int kTrackInterleave18[18]={
//		17, 8, 16, 7, 15, 6, 14, 5, 13, 4, 12, 3, 11, 2, 10, 1, 9, 0
		0, 9, 1, 10, 2, 11, 3, 12, 4, 13, 5, 14, 6, 15, 7, 16, 8, 17
	};

	static const int kTrackInterleaveDD[18]={
		15, 12, 9, 6, 3, 0, 16, 13, 10, 7, 4, 1, 17, 14, 11, 8, 5, 2
	};

	static const int kTrackInterleave26[26]={
		0, 13, 1, 14, 2, 15, 3, 16, 4, 17, 5, 18, 6, 19, 7, 20, 8, 21, 9, 22, 10, 23, 11, 24, 12, 25
	};

	enum {
		kATDiskEventTransferByte = 1,
		kATDiskEventWriteCompleted,
		kATDiskEventFormatCompleted,
		kATDiskEventAutoSave,
		kATDiskEventAutoSaveError,
		kATDiskEventMotorOff
	};

	static const int kAutoSaveDelay = 3579545;		// 2 seconds

	static const uint8 kDefaultPERCOM[]={
		0x28, 0x01, 0x00, 0x12, 0x00, 0x00, 0x00, 0x80, 0xFF, 0x00, 0x00, 0x00
	};
}

// This is horrible and needs to go
sint16 g_disksample[3868 / 2];
sint16 g_disksample2[1778 / 2];
sint16 g_disksample3[11434 / 2];
sint16 g_diskspin[64024 / 2];

ATDiskEmulator::ATDiskEmulator()
	: mpPokey(NULL)
	, mpActivity(NULL)
	, mpScheduler(NULL)
	, mpSlowScheduler(NULL)
	, mpAudioSyncMixer(NULL)
	, mUnit(0)
	, mpTransferEvent(NULL)
	, mpOperationEvent(NULL)
	, mpAutoSaveEvent(NULL)
	, mpAutoSaveErrorEvent(NULL)
	, mpMotorOffEvent(NULL)
	, mLastRotationUpdateCycle(0)
	, mTransferOffset(0)
	, mTransferLength(0)
	, mTransferRate(0)
	, mTransferSecondByteDelay(0)
	, mTransferCyclesPerBit(0)
	, mTransferCyclesPerBitFirstByte(0)
	, mTransferCompleteRotPos(0)
	, mbTransferAdjustRotation(false)
	, mFDCStatus(0)
	, mActiveCommand(0)
	, mbActiveCommandHighSpeed(false)
	, mActiveCommandState(0)
	, mActiveCommandPhysSector(0)
	, mPhantomSectorCounter(0)
	, mRotationalCounter(0)
	, mRotations(0)
	, mRotationalPosition(0)
	, mCurrentTrack(0)
	, mSectorsPerTrack(0)
	, mTrackCount(0)
	, mSideCount(0)
	, mbMFM(false)
	, mbWriteEnabled(false)
	, mbWriteHighSpeed(false)
	, mbWriteHighSpeedFirstByte(false)
	, mbAutoFlush(false)
	, mbHasDiskSource(false)
	, mbErrorIndicatorPhase(false)
	, mbAccessed(false)
	, mbWriteMode(false)
	, mbCommandMode(false)
	, mbCommandValid(false)
	, mbCommandFrameHighSpeed(false)
	, mbEnabled(false)
	, mbBurstTransfersEnabled(false)
	, mbDriveSoundsEnabled(false)
	, mbAccurateSectorTiming(false)
	, mbAccurateSectorPrediction(false)
	, mbLastOpError(false)
	, mBootSectorCount(0)
	, mTotalSectorCount(0)
	, mSectorSize(0)
	, mSectorBreakpoint(0)
	, mLastSector(0)
	, mRotationSoundId(0)
	, mFormatSectorSize(0)
	, mFormatSectorCount(0)
	, mFormatBootSectorCount(0)
	, mEmuMode(kATDiskEmulationMode_Generic)
	, mCyclesPerSIOByte(1)
	, mCyclesPerSIOBit(1)
	, mCyclesPerSIOByteHighSpeed(1)
	, mCyclesPerSIOBitHighSpeed(1)
	, mCyclesToACKSent(1)
	, mCyclesToFDCCommand(1)
	, mCyclesToCompleteAccurate(1)
	, mCyclesToCompleteFast(1)
	, mCyclesPerDiskRotation(1)
	, mCyclesPerTrackStep(1)
	, mCyclesForHeadSettle(1)
	, mbSeekHalfTracks(false)
	, mbRetryMode1050(false)
	, mbReverseOnForwardSeeks(false)
{
	memset(mPERCOM, 0, sizeof mPERCOM);

	Reset();

	static bool loaded = false;
	if (!loaded) {
		loaded = true;

		vdfastvector<uint8> data;
		ATLoadMiscResource(IDR_TRACK_STEP, data);

		memcpy(&g_disksample[0], data.data(), sizeof g_disksample);

		ATLoadMiscResource(IDR_TRACK_STEP_2, data);
		memcpy(&g_disksample2[0], data.data(), sizeof g_disksample2);

		ATLoadMiscResource(IDR_TRACK_STEP_3, data);
		memcpy(&g_disksample3[0], data.data(), sizeof g_disksample3);

		ATLoadMiscResource(IDR_DISK_SPIN, data);
		memcpy(&g_diskspin[0], data.data(), sizeof g_diskspin);
	}
}

ATDiskEmulator::~ATDiskEmulator() {
}

void ATDiskEmulator::Init(int unit, IATDiskActivity *act, ATScheduler *sched, ATScheduler *slowsched, ATAudioSyncMixer *mixer) {
	mpAudioSyncMixer = mixer;
	mLastRotationUpdateCycle = ATSCHEDULER_GETTIME(sched);
	mLastSector = 0;
	mUnit = unit;
	mpActivity = act;
	mpScheduler = sched;
	mpSlowScheduler = slowsched;
	mbEnabled = false;
	mbDriveSoundsEnabled = false;
	mbAccurateSectorTiming = false;
	mbAccurateSectorPrediction = false;
	mSectorBreakpoint = -1;
	mbWriteEnabled = false;
	mbErrorIndicatorPhase = false;
	mbAccessed = false;
	mbHasDiskSource = false;
	mRotationSoundId = 0;

	memcpy(mPERCOM, kDefaultPERCOM, 12);
}

void ATDiskEmulator::Rename(int unit) {
	if (mUnit == unit)
		return;

	if (mpActivity) {
		mpActivity->OnDiskActivity(mUnit, false, 0);
		mpActivity->OnDiskMotorChange(mUnit, false);
	}

	mUnit = unit;
}

void ATDiskEmulator::SetDriveSoundsEnabled(bool enabled) {
	if (mbDriveSoundsEnabled == enabled)
		return;

	mbDriveSoundsEnabled = enabled;

	if (!enabled) {
		if (mpAudioSyncMixer) {
			if (mRotationSoundId) {
				mpAudioSyncMixer->StopSound(mRotationSoundId);
				mRotationSoundId = 0;
			}
		}
	}
}

bool ATDiskEmulator::IsDirty() const {
	return mpDiskImage && mpDiskImage->IsDirty();
}

void ATDiskEmulator::SetWriteFlushMode(bool writeEnabled, bool autoFlush) {
	mbWriteEnabled = writeEnabled;
	mbAutoFlush = autoFlush;

	if (writeEnabled && autoFlush) {
		if (IsDirty())
			QueueAutoSave();
	} else {
		mpScheduler->UnsetEvent(mpAutoSaveEvent);
		mpScheduler->UnsetEvent(mpAutoSaveErrorEvent);

		mbErrorIndicatorPhase = false;
		mpActivity->OnDiskActivity(mUnit + 1, false, 0);
	}
}

void ATDiskEmulator::ClearAccessedFlag() {
	mbAccessed = false;
}

void ATDiskEmulator::SetEmulationMode(ATDiskEmulationMode mode) {
	if (mEmuMode == mode)
		return;

	mEmuMode = mode;
	ComputeSupportedProfile();
}

void ATDiskEmulator::Flush() {
	if (mpAutoSaveEvent)
		AutoSave();
}

void ATDiskEmulator::Reset() {
	if (mpScheduler)
		mpScheduler->UnsetEvent(mpTransferEvent);

	if (mpScheduler)
		mpScheduler->UnsetEvent(mpOperationEvent);

	if (mpSlowScheduler)
		mpSlowScheduler->UnsetEvent(mpMotorOffEvent);

	if (mpAudioSyncMixer) {
		if (mRotationSoundId) {
			mpAudioSyncMixer->StopSound(mRotationSoundId);
			mRotationSoundId = 0;
		}
	}

	mTransferOffset = 0;
	mTransferLength = 0;
	mPhantomSectorCounter = 0;
	mRotationalCounter = 0;
	mRotations = 0;
	mbWriteMode = false;
	mbCommandMode = false;
	mbLastOpError = false;
	mFDCStatus = 0xFF;
	mActiveCommand = 0;

	if (mEmuMode == kATDiskEmulationMode_810)
		mCurrentTrack = mTrackCount ? mTrackCount - 1 : 0;
	else
		mCurrentTrack = 0;

	for(ExtVirtSectors::iterator it(mExtVirtSectors.begin()), itEnd(mExtVirtSectors.end()); it!=itEnd; ++it) {
		ExtVirtSector& vsi = *it;

		vsi.mPhantomSectorCounter = 0;
	}

	mWeakBitLFSR = 1;

	ComputeSupportedProfile();

	if (IsDiskLoaded()) {
		ComputeGeometry();
		ComputePERCOMBlock();
	} else
		memcpy(mPERCOM, kDefaultPERCOM, 12);

	// clear activity counter
	if (mpActivity) {
		mpActivity->OnDiskMotorChange(mUnit + 1, false);
		mpActivity->OnDiskActivity(mUnit + 1, false, 0);
	}
}

void ATDiskEmulator::LoadDisk(const wchar_t *s) {
	size_t len = wcslen(s);

	if (len >= 3) {
		if (!wcscmp(s + len - 3, L"\\**")) {
			VDStringW t(s, s + len - 3);
			return MountFolder(t.c_str(), true);
		} else if (!wcscmp(s + len - 2, L"\\*")) {
			VDStringW t(s, s + len - 2);
			return MountFolder(t.c_str(), false);
		}
	}

	VDFileStream f(s);

	LoadDisk(s, s, f);
}

void ATDiskEmulator::MountFolder(const wchar_t *path, bool sdfs) {
	UnloadDisk();

	try {
		if (sdfs)
			mpDiskImage = ATMountDiskImageVirtualFolderSDFS(path, 65535, (uint64)mUnit << 56);
		else
			mpDiskImage = ATMountDiskImageVirtualFolder(path, 720);

		InitSectorInfoArrays();

		mPath = VDMakePath(path, L"**" + !sdfs);
	} catch(const MyError&) {
		UnloadDisk();
		throw;
	}

	ComputeGeometry();
	ComputePERCOMBlock();
	mCurrentTrack = mTrackCount - 1;
	mbEnabled = true;
	mbWriteEnabled = false;
	mbAutoFlush = false;
	mbHasDiskSource = true;
}

void ATDiskEmulator::LoadDisk(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream) {
	UnloadDisk();

	try {
		mpDiskImage = ATLoadDiskImage(origPath, imagePath, stream);

		InitSectorInfoArrays();

		if (origPath)
			mPath = origPath;
		else if (imagePath)
			mPath = imagePath;
		else
			mPath.clear();
	} catch(const MyError&) {
		UnloadDisk();
		throw;
	}

	ComputeGeometry();
	ComputePERCOMBlock();
	mCurrentTrack = mTrackCount - 1;
	mbEnabled = true;
	mbWriteEnabled = false;
	mbAutoFlush = false;
	mbHasDiskSource = (origPath != NULL);
}

void ATDiskEmulator::UpdateDisk() {
	if (!mpDiskImage->Flush())
		throw MyError("The current disk image cannot be updated.");
}

void ATDiskEmulator::SaveDisk(const wchar_t *s, ATDiskImageFormat format) {
	if (mpDiskImage->IsDynamic())
		throw MyError("The current disk image is dynamic and cannot be saved.");

	mpDiskImage->Save(s, format);

	mPath = s;
	mbHasDiskSource = true;
}

void ATDiskEmulator::CreateDisk(uint32 sectorCount, uint32 bootSectorCount, uint32 sectorSize) {
	UnloadDisk();
	FormatDisk(sectorCount, bootSectorCount, sectorSize);
	mPath = L"(New disk)";
	mbWriteEnabled = false;
	mbHasDiskSource = false;
	mbEnabled = true;
}

void ATDiskEmulator::FormatDisk(uint32 sectorCount, uint32 bootSectorCount, uint32 sectorSize) {
	mpDiskImage.reset();

	mpDiskImage = ATCreateDiskImage(sectorCount, bootSectorCount, sectorSize);

	if (mbHasDiskSource)
		mpDiskImage->SetPath(mPath.c_str());

	InitSectorInfoArrays();
	ComputeGeometry();
	ComputePERCOMBlock();

	if (mbAutoFlush)
		QueueAutoSave();
}

void ATDiskEmulator::UnloadDisk() {
	Flush();

	mpDiskImage.reset();

	mBootSectorCount = 0;
	mSectorSize = 128;
	mTotalSectorCount = 0;
	mSectorsPerTrack = 1;
	mTrackCount = 1;
	mSideCount = 1;
	mbMFM = false;
	mExtPhysSectors.clear();
	mExtVirtSectors.clear();
	mPath.clear();

	SetAutoSaveError(false);
}

namespace {
	uint8 Checksum(const uint8 *p, int len) {
		uint32 checksum = 0;
		for(int i=0; i<len; ++i) {
			checksum += p[i];
			checksum += (checksum >> 8);
			checksum &= 0xff;
		}

		return (uint8)checksum;
	}
}

VDStringW ATDiskEmulator::GetMountedImageLabel() const {
	if (!mpDiskImage)
		return VDStringW(L"(No disk)");

	VDStringW label = VDFileSplitPathRight(mPath);

	if (mPath.empty())
		label = L"New disk";

	if (mpDiskImage->IsDirty())
		label += L" (modified)";

	return label;
}

uint32 ATDiskEmulator::GetSectorCount() const {
	return mTotalSectorCount;
}

uint32 ATDiskEmulator::GetSectorSize(uint16 sector) const {
	return sector && sector <= mBootSectorCount ? 128 : mSectorSize;
}

uint32 ATDiskEmulator::GetSectorPhantomCount(uint16 sector) const {
	if (!mpDiskImage)
		return 0;

	if (!sector || sector > mpDiskImage->GetVirtualSectorCount())
		return 0;

	ATDiskVirtualSectorInfo vsi;
	mpDiskImage->GetVirtualSectorInfo(sector - 1, vsi);

	return vsi.mNumPhysSectors;
}

bool ATDiskEmulator::GetSectorInfo(uint16 sector, int phantomIdx, SectorInfo& info) const {
	if (!mpDiskImage)
		return false;

	if (!sector || sector > mpDiskImage->GetVirtualSectorCount())
		return false;

	ATDiskVirtualSectorInfo vsi;
	mpDiskImage->GetVirtualSectorInfo(sector - 1, vsi);

	if (phantomIdx < 0 || (uint32)phantomIdx >= vsi.mNumPhysSectors)
		return false;

	ATDiskPhysicalSectorInfo psi;
	mpDiskImage->GetPhysicalSectorInfo(vsi.mStartPhysSector + phantomIdx, psi);

	info.mRotPos = psi.mRotPos;
	info.mFDCStatus = psi.mFDCStatus;
	return true;
}

uint8 ATDiskEmulator::ReadSector(uint16 bufadr, uint16 len, uint16 sector, ATCPUEmulatorMemory *mpMem) {
	uint32 desiredPacketLength = 3 + len;

	UpdateRotationalCounter();

	// SIO retries once on a device error and fourteen times on a command error.
	// Why do we emulate this? Because it makes a difference with phantom sectors.
	uint8 status;
	for(int i=0; i<2; ++i) {
		for(int j=0; j<14; ++j) {
			// construct read sector packet
			mReceivePacket[0] = 0x31 + mUnit;		// device ID
			mReceivePacket[1] = 0x52;				// read command
			mReceivePacket[2] = sector & 0xff;		// sector to read
			mReceivePacket[3] = sector >> 8;
			mReceivePacket[4] = 0;					// checksum (ignored)

			UpdateRotationalCounter();

			uint32 preRotPos = mRotationalCounter + kCyclesToProcessRequest;
			mRotationalCounter = preRotPos % mCyclesPerDiskRotation;
			mRotations += preRotPos / mCyclesPerDiskRotation;

			ProcessCommandPacket();

			// fake rotation
			if (mbTransferAdjustRotation) {
				mRotationalCounter = mTransferCompleteRotPos + kCyclesToExitSIO;
				UpdateRotationalCounter();
			}

			status = mSendPacket[0];

			if (status == 0x41)
				break;
		}

		// check if command retries exhausted
		if (status != 0x41) {
			if (status == 0x45) {	// ERROR ($45)
				// report device error ($90)
				status = 0x90;
			} else {
				// report DNACK ($8B)
				status = 0x8B;
			}
			break;
		}

		// process successful command
		status = mSendPacket[1];

		if (status == 0x43 || status == 0x45) {		// COMPLT ($43) or ERROR ($45)
			uint8 checksum;

			if (status == 0x45) {	// ERROR ($45)
				// report device error ($90)
				status = 0x90;
			}

			// check sector length against expected length
			if (mTransferLength < desiredPacketLength) {
				// transfer data into user memory -- this is done via ISR, so it will happen
				// before timeout occurs
				for(uint32 i=0; i<mTransferLength - 2; ++i)
					mpMem->WriteByte(bufadr+i, mSendPacket[i+2]);

				// packet too short -- TIMOUT ($8A)
				status = 0x8A;

				checksum = Checksum(mSendPacket+2, mTransferLength - 2);
			} else if (mTransferLength >= desiredPacketLength) {
				// transfer data into user memory -- this is done via ISR, so it will happen
				// before any checksum error occurs
				for(uint32 i=0; i<len; ++i)
					mpMem->WriteByte(bufadr+i, mSendPacket[i+2]);

				checksum = Checksum(mSendPacket+2, len);

				// if the data packet is too long, a data byte will be mistaken for the
				// checksum
				if (mTransferLength > desiredPacketLength) {
					if (Checksum(mSendPacket + 2, len) != mSendPacket[2 + len]) {
						// bad checksum -- CHKSUM ($8F)
						status = 0x8F;
					}
				}
			}

			// Karateka is really sneaky: it requests a read into ROM at $F000 and then
			// checks CHKSUM in page zero.
			mpMem->WriteByte(0x0031, checksum);
		} else {
			// report DNACK ($8B)
			status = 0x8B;
		}

		// if status is still OK, stop retries and return success
		if (status == 0x43) {
			status = 0x01;
			break;
		}
	}

	// clear transfer
	mbWriteMode = false;
	mTransferOffset = 0;
	mTransferLength = 0;
	if (mpActivity)
		mpActivity->OnDiskActivity(mUnit + 1, false, mLastSector);

	if (mpTransferEvent) {
		mpScheduler->RemoveEvent(mpTransferEvent);
		mpTransferEvent = NULL;
	}

	return status;
}

uint8 ATDiskEmulator::WriteSector(uint16 bufadr, uint16 len, uint16 sector, ATCPUEmulatorMemory *mpMem) {
	// Check write enable.
	if (!mbWriteEnabled)
		return 0x90;	// device error

	// Check sector number.
	if (sector < 1 || sector > mTotalSectorCount) {
		// report DNACK
		return 0x8B;
	}

	// Look up physical sector and check length
	ATDiskVirtualSectorInfo vsi;
	mpDiskImage->GetVirtualSectorInfo(sector - 1, vsi);
	if (!vsi.mNumPhysSectors) {
		// report device error
		return 0x90;
	}

	ATDiskPhysicalSectorInfo psi;
	mpDiskImage->GetPhysicalSectorInfo(vsi.mStartPhysSector, psi);

	if (len > psi.mSize) {
		// expected size is too long -- report timeout
		return 0x8A;
	}

	if (len < psi.mSize) {
		// expected size is too short -- report checksum error
		return 0x8F;
	}

	// commit memory to disk
	uint8 buf[8192];

	for(uint32 i=0; i<len; ++i)
		buf[i] = mpMem->ReadByte(bufadr+i);

	uint8 status = ATCIOSymbols::CIOStatSuccess;

	try {
		mpDiskImage->WritePhysicalSector(vsi.mStartPhysSector, buf, len);
	} catch(const MyError&) {
		status = ATCIOSymbols::CIOStatDeviceDone;
	}

	mLastSector = sector;

	if (mpActivity) {
		mpActivity->OnDiskActivity(mUnit + 1, true, mLastSector);
		mpActivity->OnDiskActivity(mUnit + 1, false, mLastSector);
	}

	if (mbAutoFlush)
		QueueAutoSave();

	// return success
	return status;
}

void ATDiskEmulator::ReadStatus(uint8 dst[5]) {
	uint8 status = 0;

	// We need to check the sector size in the PERCOM block and not the physical
	// disk for this value. This is required as SmartDOS 8.2D does a Write PERCOM
	// Block command and then depends on FM/MFM selection being reflected in the
	// result.
	if (mPERCOM[6])		// sector size high byte
		status += 0x20;

	if (mbLastOpError)
		status += 0x04;

	if (!mbWriteEnabled)
		status += 0x08;

	if (mSideCount > 1)
		status += 0x40;

	if (mSectorsPerTrack == 26)
		status += 0x80;

	if (mpMotorOffEvent)
		status += 0x10;

	dst[0] = status;
	dst[1] = mTotalSectorCount > 0 ? mFDCStatus : 0x7F;
	dst[2] = mEmuMode == kATDiskEmulationMode_XF551 ? 0xfe : 0xe0;
	dst[3] = 0x00;
	dst[4] = Checksum(dst, 4);
}

void ATDiskEmulator::ReadPERCOMBlock(uint8 dst[13]) {
	memcpy(dst, mPERCOM, 12);

	if (mEmuMode == kATDiskEmulationMode_XF551) {
		dst[1] = 0;		// step rate 0
		dst[8] = 1;		// drive active
	}

	dst[12] = Checksum(dst, 12);
}

void ATDiskEmulator::SetForcedPhantomSector(uint16 sector, uint8 index, int order) {
	if (!sector || sector >= mExtVirtSectors.size())
		return;

	ATDiskVirtualSectorInfo vsi;
	mpDiskImage->GetVirtualSectorInfo(sector - 1, vsi);

	mExtPhysSectors[vsi.mStartPhysSector + index].mForcedOrder = (sint8)order;
}

int ATDiskEmulator::GetForcedPhantomSector(uint16 sector, uint8 index) {
	if (!sector || sector >= mExtVirtSectors.size())
		return -1;

	ATDiskVirtualSectorInfo vsi;
	mpDiskImage->GetVirtualSectorInfo(sector - 1, vsi);
	if (index >= vsi.mNumPhysSectors)
		return -1;

	return mExtPhysSectors[vsi.mStartPhysSector + index].mForcedOrder;
}

void ATDiskEmulator::OnScheduledEvent(uint32 id) {
	if (id == kATDiskEventTransferByte) {
		mpTransferEvent = NULL;

		if (!mbWriteMode)
			return;

		if (mTransferOffset >= mTransferLength)
			return;

		const uint8 data = mSendPacket[mTransferOffset];
		const uint32 cyclesPerBit = mTransferOffset == 0 ? mTransferCyclesPerBitFirstByte : mTransferCyclesPerBit;
		g_ATLCDiskData("Sending byte %u/%u = $%02x (%u cycles/bit)\n", mTransferOffset, mTransferLength, data, cyclesPerBit);

		mpPokey->ReceiveSIOByte(data, cyclesPerBit, true, mbBurstTransfersEnabled);
		++mTransferOffset;

		// SIO barfs if the third byte is sent too quickly
		uint32 transferDelay = mTransferRate;
		if (mTransferOffset == 1) {
			transferDelay = mTransferSecondByteDelay;
		}

		// Doc Wire's Solitaire Solution needs a bit more delay between the Complete byte and the first
		// data byte at high speed.
		if (mTransferOffset == 2 && mbWriteHighSpeed)
			transferDelay = kCyclesToFirstDataHighSpeed;

		if (mTransferOffset >= mTransferLength) {
			if (mbTransferAdjustRotation) {
				mRotationalCounter = mTransferCompleteRotPos;
				mLastRotationUpdateCycle = ATSCHEDULER_GETTIME(mpScheduler);
			}

			UpdateRotationalCounter();

			g_ATLCDisk("Disk transmit finished. (len=%u, rot=%.2f)\n", mTransferLength, (float)mRotations + (float)mRotationalCounter / (float)mCyclesPerDiskRotation);

			mTransferOffset = 0;
			mTransferLength = 0;
			mbWriteMode = false;
			mpActivity->OnDiskActivity(mUnit + 1, false, mLastSector);

			if (mActiveCommand)
				ProcessCommandTransmitCompleted();
		} else {
			mpTransferEvent = mpScheduler->AddEvent(transferDelay, this, kATDiskEventTransferByte);
		}
	} else if (id == kATDiskEventWriteCompleted) {
		mpOperationEvent = NULL;
		BeginTransferComplete();
	} else if (id == kATDiskEventFormatCompleted) {
		mpOperationEvent = NULL;
		mSendPacket[0] = 'C';
		memset(mSendPacket+1, 0xFF, mSectorSize);
		mSendPacket[mSectorSize + 1] = Checksum(mSendPacket+1, mSectorSize);
		BeginTransfer(mSectorSize + 2, mCyclesToACKSent, 0, mbActiveCommandHighSpeed, mbActiveCommandHighSpeed, false);
	} else if (id == kATDiskEventAutoSave) {
		mpAutoSaveEvent = NULL;

		AutoSave();
	} else if (id == kATDiskEventAutoSaveError) {
		mpActivity->OnDiskActivity(mUnit + 1, mbErrorIndicatorPhase, mUnit + 1);
		mbErrorIndicatorPhase = !mbErrorIndicatorPhase;

		mpAutoSaveErrorEvent = mpScheduler->AddEvent(894886, this, kATDiskEventAutoSaveError);
	} else if (id == kATDiskEventMotorOff) {
		mpMotorOffEvent = NULL;

		if (mpActivity)
			mpActivity->OnDiskMotorChange(mUnit + 1, false);

		if (mpAudioSyncMixer) {
			if (mRotationSoundId) {
				mpAudioSyncMixer->StopSound(mRotationSoundId);
				mRotationSoundId = 0;
			}
		}

		if (mEmuMode == kATDiskEmulationMode_810) {
			uint32 endTrack = mTrackCount ? mTrackCount - 1 : 0;
			PlaySeekSound(0, abs((int)endTrack - (int)mCurrentTrack));
			mCurrentTrack = endTrack;
		}
	}
}

void ATDiskEmulator::PokeyAttachDevice(ATPokeyEmulator *pokey) {
	mpPokey = pokey;
}

bool ATDiskEmulator::PokeyWriteSIO(uint8 c, bool command, uint32 cyclesPerBit) {
	if (!mbEnabled)
		return false;

	if (mbWriteMode) {
		mbWriteMode = false;
		mTransferOffset = 0;
	}

	if (mActiveCommand) {
		// Check the cycles per bit and make sure the transmission is at the correct rate.
		// It must be either 19,200 baud (divisor=$28) or the high speed command rate for
		// the XF551 and IndusGT. We allow up to a 5% deviation in transfer rate.
		if (mbActiveCommandHighSpeed) {
			if (cyclesPerBit < mHighSpeedDataFrameRateLo || cyclesPerBit > mHighSpeedDataFrameRateHi) {
				g_ATLCDiskCmd("Rejecting data byte $%02X sent at wrong rate (cycles per bit = %d, expected %d-%d)\n", c, cyclesPerBit, mHighSpeedDataFrameRateLo, mHighSpeedDataFrameRateHi);

				// trash the byte
				++c;
			}
		} else {
			if (cyclesPerBit < 90 || cyclesPerBit > 98) {
				g_ATLCDiskCmd("Rejecting data byte $%02X sent at wrong rate (cycles per bit = %d, expected standard rate)\n", c, cyclesPerBit);

				// trash the byte
				++c;
			}
		}		

		if (mTransferOffset < mTransferLength)
			mReceivePacket[mTransferOffset++] = c;

		if (mTransferOffset >= mTransferLength)
			ProcessCommandData();

		return mbBurstTransfersEnabled;
	}

	if (!mbCommandMode)
		return false;

	// Check the cycles per bit and make sure the transmission is at the correct rate.
	// It must be either 19,200 baud (divisor=$28) or the high speed command rate for
	// the XF551 and IndusGT. We allow up to a 5% deviation in transfer rate.
	if (cyclesPerBit < 90 || cyclesPerBit > 98) {
		if (!mbSupportedCmdFrameHighSpeed || cyclesPerBit < mHighSpeedCmdFrameRateLo || cyclesPerBit > mHighSpeedCmdFrameRateHi) {
			mbCommandValid = false;
			g_ATLCDiskCmd("Rejecting command byte $%02X sent at wrong rate (cycles per bit = %d)\n", c, cyclesPerBit);
			return false;
		} else {
			mbCommandFrameHighSpeed = true;
		}
	}

	if (mTransferOffset < 16)
		mReceivePacket[mTransferOffset++] = c;

	return mbBurstTransfersEnabled;
}

void ATDiskEmulator::InitSectorInfoArrays() {
	const uint32 physCount = mpDiskImage->GetPhysicalSectorCount();
	mExtPhysSectors.resize(physCount);

	for(uint32 i = 0; i < physCount; ++i) {
		ExtPhysSector& psi = mExtPhysSectors[i];

		psi.mForcedOrder = -1;
	}

	const uint32 virtCount = mpDiskImage->GetVirtualSectorCount();
	mExtVirtSectors.resize(virtCount);

	mTotalSectorCount = virtCount;

	for(uint32 i=0; i<virtCount; ++i) {
		ExtVirtSector& vsi = mExtVirtSectors[i];

		vsi.mPhantomSectorCounter = 0;
	}

	mBootSectorCount = mpDiskImage->GetBootSectorCount();
	mSectorSize = mpDiskImage->GetSectorSize();
	mbAccurateSectorPrediction = (mpDiskImage->GetTimingMode() == kATDiskTimingMode_UsePrecise);
}

void ATDiskEmulator::BeginTransferACKCmd() {
	mSendPacket[0] = 'A';
	BeginTransfer(1, mCyclesToACKSent, 0, mbCommandFrameHighSpeed, mbCommandFrameHighSpeed, false);
}

void ATDiskEmulator::BeginTransferACK() {
	mSendPacket[0] = 'A';
	BeginTransfer(1, mCyclesToACKSent, 0, mbActiveCommandHighSpeed, mbActiveCommandHighSpeed, false);
}

void ATDiskEmulator::BeginTransferComplete() {
	mSendPacket[0] = 'C';
	BeginTransfer(1, mCyclesToACKSent, 0, mbActiveCommandHighSpeed, mbActiveCommandHighSpeed, false);
}

void ATDiskEmulator::BeginTransferError() {
	mSendPacket[0] = 'E';
	BeginTransfer(1, mCyclesToACKSent, 0, mbActiveCommandHighSpeed, mbActiveCommandHighSpeed, false);
}

void ATDiskEmulator::BeginTransferNAK() {
	// NAKs are only sent in response to the command itself and therefore must be sent at
	// command frame speed.
	mSendPacket[0] = 'N';
	BeginTransfer(1, mCyclesToACKSent, 0, mbCommandFrameHighSpeed, mbCommandFrameHighSpeed, false);
}

void ATDiskEmulator::BeginTransfer(uint32 length, uint32 cyclesToFirstByte, uint32 cyclesToSecondByte, bool useHighSpeedFirstByte, bool useHighSpeed, bool enhancedDensity) {
	mbWriteMode = true;
	mbWriteHighSpeedFirstByte = useHighSpeedFirstByte;
	mbWriteHighSpeed = useHighSpeed;
	mTransferRate = useHighSpeed ? mCyclesPerSIOByteHighSpeed : mCyclesPerSIOByte;
	mTransferCyclesPerBit = useHighSpeed ? mCyclesPerSIOBitHighSpeed : mCyclesPerSIOBit;
	mTransferCyclesPerBitFirstByte = useHighSpeedFirstByte ? mCyclesPerSIOBitHighSpeed : mCyclesPerSIOBit;
	mTransferOffset = 0;
	mTransferLength = length;

	if (cyclesToSecondByte)
		mTransferSecondByteDelay = cyclesToSecondByte;
	else if (!mbAccurateSectorTiming)
		mTransferSecondByteDelay = mCyclesToCompleteFast;
	else {
		mTransferSecondByteDelay = enhancedDensity ? mCyclesToCompleteAccurateED : mCyclesToCompleteAccurate;
	}

	if (mpTransferEvent)
		mpScheduler->RemoveEvent(mpTransferEvent);

	mpTransferEvent = mpScheduler->AddEvent(cyclesToFirstByte, this, kATDiskEventTransferByte);

	// We compute the post transfer position but don't actually enable it here; that's
	// done from the caller site if desired.
	mbTransferAdjustRotation = false;

	mTransferCompleteRotPos = mRotationalCounter;
	if (length) {
		mTransferCompleteRotPos += cyclesToFirstByte;
		
		if (length >= 2) {
			mTransferCompleteRotPos += mTransferSecondByteDelay;

			if (length >= 3)
				mTransferCompleteRotPos += mTransferRate * (length - 2);
		}
	}
}

void ATDiskEmulator::UpdateRotationalCounter() {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	uint32 dt = t - mLastRotationUpdateCycle;
	mLastRotationUpdateCycle = t;

	mRotationalCounter += dt;

	if (mRotationalCounter >= mCyclesPerDiskRotation) {
		uint32 rotations = mRotationalCounter / mCyclesPerDiskRotation;
		mRotationalCounter %= mCyclesPerDiskRotation;
		mRotations += rotations;
	}
}

void ATDiskEmulator::QueueAutoSave() {
	if (mpAutoSaveEvent)
		mpScheduler->RemoveEvent(mpAutoSaveEvent);

	mpAutoSaveEvent = mpScheduler->AddEvent(kAutoSaveDelay, this, kATDiskEventAutoSave); 
}

void ATDiskEmulator::AutoSave() {
	if (mpAutoSaveEvent) {
		mpScheduler->RemoveEvent(mpAutoSaveEvent);
		mpAutoSaveEvent = NULL;
	}

	if (!mpDiskImage->IsUpdatable())
		SetAutoSaveError(true);
	else {
		try {
			UpdateDisk();
			SetAutoSaveError(false);
		} catch(const MyError&) {
			mbAutoFlush = false;
			SetAutoSaveError(true);
		}
	}
}

void ATDiskEmulator::SetAutoSaveError(bool error) {
	if (error) {
		if (!mpAutoSaveErrorEvent)
			mpAutoSaveErrorEvent = mpScheduler->AddEvent(1000, this, kATDiskEventAutoSaveError);
	} else {
		mpActivity->OnDiskActivity(mUnit + 1, false, mLastSector);

		if (mpAutoSaveErrorEvent) {
			mpScheduler->RemoveEvent(mpAutoSaveErrorEvent);
			mpAutoSaveErrorEvent = NULL;
		}
	}
}

void ATDiskEmulator::ProcessCommandPacket() {
	mpActivity->OnDiskActivity(mUnit + 1, true, mLastSector);

	UpdateRotationalCounter();

	// interpret the command
	g_ATLCDiskCmd("Processing command: Unit %02X, Command %02X, Aux data %02X %02X%s\n"
		, mReceivePacket[0]
		, mReceivePacket[1]
		, mReceivePacket[2]
		, mReceivePacket[3]
		, mbCommandFrameHighSpeed ? " (high-speed command frame)" : ""
	);
	const uint8 command = mReceivePacket[1];
	bool highSpeed = mbCommandFrameHighSpeed || (command & 0x80) != 0;

	// reject all high speed commands if not XF551 or generic
	if (!mbSupportedCmdHighSpeed && (command & 0x80))
		goto unsupported_command;

	// check if this is a 1050 Turbo command
	if (mEmuMode == kATDiskEmulationMode_1050Turbo && (mReceivePacket[3] & 0x80)) {
		switch(command) {
			case 0x4E:	// read PERCOM block
			case 0x4F:	// write PERCOM block
			case 0x52:	// read
			case 0x50:	// put (without verify)
			case 0x57:	// write (with verify)
				mReceivePacket[3] &= 0x7f;
				mbCommandFrameHighSpeed = true;
				highSpeed = true;
				break;
		}
	}

	mbActiveCommandHighSpeed = highSpeed;

	switch(command) {
		case 0x53:	// status
		case 0xD3:	// status (XF551 high speed)
			mSendPacket[0] = 0x41;
			mSendPacket[1] = 0x43;
			ReadStatus(mSendPacket + 2);
			BeginTransfer(7, 2500, 0, mbCommandFrameHighSpeed, highSpeed, false);
			break;

		case 0x52:	// read
		case 0xD2:	// read (XF551 high speed)
			{
				uint32 sector = mReceivePacket[2] + mReceivePacket[3] * 256;

				mLastSector = sector;

				if (sector == mSectorBreakpoint)
					g_sim.PostInterruptingEvent(kATSimEvent_DiskSectorBreakpoint);

				// check if we actually have a disk; if not, we still allow sectors 1-720, but
				// report them as missing
				if (!mpDiskImage && sector >= 1 && sector <= 720) {
					mbLastOpError = true;
					mFDCStatus = 0xF7;

					// sector not found....
					mSendPacket[0] = 0x41;
					mSendPacket[1] = 0x45;
					// don't clear the sector buffer!
					mSendPacket[128+2] = Checksum(mSendPacket + 2, 128);
					BeginTransfer(131, mCyclesToACKSent, 0, mbCommandFrameHighSpeed, highSpeed, false);

					mRotationalCounter += mCyclesPerDiskRotation >> 1;
					UpdateRotationalCounter();

					g_ATLCDisk("Reporting missing sector %d (no disk in drive).\n", sector);
					break;
				}

				if (!sector || sector > (uint32)mTotalSectorCount) {
					// NAK the command immediately -- the 810 and 1050 both NAK commands
					// with invalid sector numbers.

					mbLastOpError = true;
					BeginTransferNAK();
					g_ATLCDisk("Error reading sector %d.\n", sector);
					break;
				}

				// check if we need to turn on the motor, and start computing operation delay
				uint32 opDelay = mCyclesToFDCCommand;
				
				if (TurnOnMotor(mCyclesToACKSent))
					opDelay += 7159090/8;

				// do initial seek
				uint32 track = (sector - 1) / mSectorsPerTrack;
				int trackDelta = (int)track - (int)mCurrentTrack;
				uint32 tracksToStep = (uint32)abs(trackDelta);

				mCurrentTrack = track;

				if (tracksToStep) {
					// The 1050 drive does an extra pair of half steps after a forward seek, one forward
					// and one backward. This ensures that tracks are always read or written after the
					// head has seeked backwards. The 810 does not do this.
					if (trackDelta > 0 && mbReverseOnForwardSeeks) {
						PlaySeekSound(opDelay, tracksToStep + 1);
						opDelay += (tracksToStep + 1) * mCyclesPerTrackStep;
					} else {
						PlaySeekSound(opDelay, tracksToStep);
						opDelay += tracksToStep * mCyclesPerTrackStep;
					}

					opDelay += mCyclesForHeadSettle;
				}

				UpdateRotationalCounter();

				// get virtual sector information
				ATDiskVirtualSectorInfo vsi;
				mpDiskImage->GetVirtualSectorInfo(sector - 1, vsi);

				ExtVirtSector& evs = mExtVirtSectors[sector - 1];

				// check if we have any sectors
				ATDiskPhysicalSectorInfo psi;
				bool havePhysSector = false;
				uint32 postSeekPosition = 0;
				uint32 physSector;

				if (vsi.mNumPhysSectors) {
					// choose a physical sector
					if (mbAccurateSectorPrediction || mbAccurateSectorTiming) {
						// compute post-seek rotational position
						postSeekPosition = (mRotationalCounter + opDelay) % mCyclesPerDiskRotation;

						uint32 bestDelay = 0xFFFFFFFFU;
						uint8 bestStatus = 0;

						physSector = vsi.mStartPhysSector;

						for(uint32 i=0; i<vsi.mNumPhysSectors; ++i) {
							ATDiskPhysicalSectorInfo psi;
							mpDiskImage->GetPhysicalSectorInfo(vsi.mStartPhysSector + i, psi);

							const ExtPhysSector& eps = mExtPhysSectors[vsi.mStartPhysSector + i];

							// compute sector's rotational position in cycles
							uint32 sectorPos = VDRoundToInt(psi.mRotPos * mCyclesPerDiskRotation);

							// compute rotational delay to sector
							uint32 delay = sectorPos < postSeekPosition ? sectorPos + mCyclesPerDiskRotation - postSeekPosition : sectorPos - postSeekPosition;

							if (eps.mForcedOrder == evs.mPhantomSectorCounter) {
								physSector = vsi.mStartPhysSector + i;
								mPhantomSectorCounter = i;

								if (++evs.mPhantomSectorCounter >= vsi.mNumPhysSectors)
									evs.mPhantomSectorCounter = 0;
								break;
							}

							if (delay < bestDelay) {
								bestDelay = delay;
								bestStatus = psi.mFDCStatus;

								physSector = vsi.mStartPhysSector + i;
								mPhantomSectorCounter = i;
							}
						}
					} else {
						uint32 phantomIdx = evs.mPhantomSectorCounter;
						for(uint32 i=0; i<vsi.mNumPhysSectors; ++i) {
							const ExtPhysSector& eps = mExtPhysSectors[vsi.mStartPhysSector + i];

							if (eps.mForcedOrder == evs.mPhantomSectorCounter) {
								phantomIdx = i;
								break;
							}
						}

						physSector = vsi.mStartPhysSector + phantomIdx;

						if (++evs.mPhantomSectorCounter >= vsi.mNumPhysSectors)
							evs.mPhantomSectorCounter = 0;
					}

					mpDiskImage->GetPhysicalSectorInfo(physSector, psi);

					// Set FDC status.
					//
					// Note that in order to get a lost data condition (bit 2), there must already have
					// been data pending (bit 1) when more data arrived. The 810 ROM does not clear the
					// DRQ flag before storing status. The Music Studio requires this.
					mFDCStatus = psi.mFDCStatus;
					if (!(mFDCStatus & 0x04))
						mFDCStatus &= ~0x02;

					// set rotational delay
					mRotationalPosition = VDRoundToInt(psi.mRotPos * mCyclesPerDiskRotation);
				} else {
					// indicate missing track/sector (record not found)
					mFDCStatus = 0xEF;
				}

				const bool missingSector = !(mFDCStatus & 0x10);

				// compute seek time
				uint32 secondByteDelay = opDelay - mCyclesToACKSent;

				// If we have the sector, add rotational delay from the post seek position to the
				// sector's position; otherwise, add two revs for the FDC's attempt to find it.
				if (missingSector) {
					if (mbRetryMode1050)
						secondByteDelay += kCyclesPerFakeRot_1050;
					else
						secondByteDelay += kCyclesPerFakeRot_810;
				} else {
					if (postSeekPosition > mRotationalPosition)
						secondByteDelay += mCyclesPerDiskRotation;

					secondByteDelay += mRotationalPosition - postSeekPosition;
				}

				// Check if we got an error.
				if (mFDCStatus != 0xFF) {
					// Check if we're modeling the 810 or 1050. The 810 does four tries overall
					// with a possible recalibrate between the first two and second two attempts;
					// the 1050 does two tries with a recalibrate or restep in between.
					if (!mbRetryMode1050) {
						// 810 -- add another (fake) rotation
						if (missingSector)
							secondByteDelay += kCyclesPerFakeRot_810;
						else
							secondByteDelay += mCyclesPerDiskRotation;
					}

					// Compute the restep/recalibration delay.
					if (missingSector) {
						// Missing sector -- we'll be recalibrating.
						//
						// The 1050 has a track 0 sensor, so it only does the necessary number of steps
						// when recalibrating. The 810, on the other hand, doesn't have one and just
						// steps back 43 tracks.
						const uint32 restoreSteps = mbRetryMode1050 ? track : 43;
						const uint32 seekTime1 = restoreSteps ? restoreSteps * mCyclesPerTrackStep + mCyclesForHeadSettle : 0;

						if (restoreSteps) {
							PlaySeekSound(secondByteDelay, restoreSteps);
							secondByteDelay += seekTime1;
						}

						// compute time to seek back -- no rotational delay to get back to sector, it
						// doesn't exist (sectors don't magically reappear in our model).
						if (track) {
							const uint32 tracksToStep = mbReverseOnForwardSeeks ? track+1 : track;

							PlaySeekSound(secondByteDelay, tracksToStep);
							secondByteDelay += tracksToStep * mCyclesPerTrackStep + mCyclesForHeadSettle;
						}

						// ...and do another fake rotation.
						if (mbRetryMode1050)
							secondByteDelay += kCyclesPerFakeRot_1050;
						else
							secondByteDelay += kCyclesPerFakeRot_810;
					} else {
						// Found sector but read with error. For the 810, we'll just fail three
						// more times. For the 1050, we need to do a half step in and back, then
						// retry once. To do this fully accurately we should be checking for phantom
						// sectors each time; for now we just assume it's the same sector.
						if (mbRetryMode1050) {
							// seek in and out
							PlaySeekSound(secondByteDelay, 1);

							// apply seek delay + rotational delay to get back to same sector
							uint32 reseekDelay = mCyclesPerTrackStep + mCyclesForHeadSettle;
							reseekDelay += (mCyclesPerDiskRotation - (reseekDelay % mCyclesPerDiskRotation)) % mCyclesPerDiskRotation;

							secondByteDelay += reseekDelay;
						} else {
							secondByteDelay += mCyclesPerDiskRotation * 3;
						}
					}

				}

				// add time to read sector and compute checksum
				//
				// sector read: ~130 bytes at 125Kbits/sec = ~8.3ms = ~14891 cycles
				// FDC reset and checksum: ~2568 cycles @ 500KHz = 9192 cycles
				if (mbMFM && psi.mSize == 128)
					secondByteDelay += 12431;
				else
					secondByteDelay += 24083;

				// check if accurate sector timing is off
				if (!mbAccurateSectorTiming) {
					// yes it is -- shorten the second byte delay to the fast complete time and
					// warp the disk rotation by the amount of time we removed
					if (secondByteDelay > mCyclesToCompleteFast) {
						const uint32 warpTime = secondByteDelay - mCyclesToCompleteFast;
						secondByteDelay = mCyclesToCompleteFast;

						mRotationalCounter += warpTime;

						UpdateRotationalCounter();
					}
				}

				// check for missing sector
				// note: must send ACK (41) + ERROR (45) -- BeachHead expects to get DERROR from SIO
				if (missingSector) {
					mbLastOpError = true;

					// sector not found....
					mSendPacket[0] = 0x41;
					mSendPacket[1] = 0x45;

					// don't clear the sector buffer!
					mSendPacket[128+2] = Checksum(mSendPacket + 2, 128);
					BeginTransfer(131, mCyclesToACKSent, secondByteDelay, mbCommandFrameHighSpeed, highSpeed, false);

					g_ATLCDisk("Reporting missing sector %d.\n", sector);
					break;
				}

				mSendPacket[0] = 0x41;		// ACK
				mSendPacket[1] = 0x43;		// complete

				mbLastOpError = (mFDCStatus != 0xFF);

				try {
					mpDiskImage->ReadPhysicalSector(physSector, mSendPacket + 2, psi.mSize);
				} catch(const MyError&) {
					// wipe sector and report CRC error
					memset(mSendPacket + 2, 0, psi.mSize);
					mFDCStatus = 0xF7;
					mbLastOpError = true;
				}

				// check for CRC error
				// must return data on CRC error -- Koronis Rift requires this
				if (~mFDCStatus & 0x28) {
					mSendPacket[1] = 0x45;	// error

					// Check if we should emulate weak bits.
					if (psi.mWeakDataOffset >= 0) {
						for(int i = psi.mWeakDataOffset; i < (int)psi.mSize; ++i) {
							mSendPacket[2 + i] ^= (uint8)mWeakBitLFSR;

							mWeakBitLFSR = (mWeakBitLFSR << 8) + (0xff & ((mWeakBitLFSR >> (28 - 8)) ^ (mWeakBitLFSR >> (31 - 8))));
						}
					}
				}

				mSendPacket[psi.mSize+2] = Checksum(mSendPacket + 2, psi.mSize);
				const uint32 transferLength = psi.mSize + 3;

				BeginTransfer(transferLength, mCyclesToACKSent, secondByteDelay, mbCommandFrameHighSpeed, highSpeed, mbMFM && psi.mSize == 128);
				g_ATLCDisk("Reading vsec=%3d (%d/%d) (trk=%d), psec=%3d, chk=%02x, rot=%.2f >> %.2f >> %.2f%s.\n"
						, sector
						, physSector - vsi.mStartPhysSector + 1
						, vsi.mNumPhysSectors
						, (sector - 1) / mSectorsPerTrack
						, physSector
						, mSendPacket[mTransferLength - 1]
						, (float)mRotations + (float)mRotationalCounter / (float)mCyclesPerDiskRotation
						, psi.mRotPos
						, (mTransferCompleteRotPos % mCyclesPerDiskRotation) / (float)mCyclesPerDiskRotation
						,  psi.mWeakDataOffset >= 0 ? " (w/weak bits)"
							: !(mFDCStatus & 0x04) ? " (w/long sector)"
							: !(mFDCStatus & 0x08) ? " (w/CRC error)"
							: !(mFDCStatus & 0x10) ? " (w/missing sector)"
							: !(mFDCStatus & 0x20) ? " (w/deleted sector)"
							: ""
						);

				if (mbAccurateSectorTiming || mbAccurateSectorPrediction)
					mbTransferAdjustRotation = true;
			}
			break;

#if 0	// These commands are documented by Atari in the OS manual but were never implemented.
		case 0x20:	// download
			break;
		case 0x51:	// read spin
			break;
		case 0x54:	// readaddr
			break;
		case 0x55:	// motor on
			break;
		case 0x56:	// verify sector
			break;
#endif

		case 0x21:	// format
		case 0xA1:	// format (high speed)
			TurnOnMotor();

			// Disable high speed operation if we're getting an XF551 command -- the high bit
			// is used for sector skew and not high speed.
			if (command == 0xA1)
				mbActiveCommandHighSpeed = false;

			if (!mbWriteEnabled) {
				g_ATLCDisk("FORMAT COMMAND RECEIVED. Blocking due to read-only disk!\n");

				// The FORMAT command always sends an ACK first and then sends ERROR instead of
				// COMPLETE if the disk is write protected. In that case, we need to send a data
				// frame.
				mSendPacket[0] = 'A';		// ACK
				mSendPacket[1] = 'E';		// Error
				mSendPacket[2] = 0xFF;		// Sector terminator (sector buffer data)
				mSendPacket[3] = 0xFF;
				memset(mSendPacket + 4, 0, mSectorSize - 2);
				mSendPacket[mSectorSize + 2] = 0xFF;
				mbLastOpError = true;

				// Assert FDC status bit 6 (write protect).
				mFDCStatus = 0xBF;

				BeginTransfer(mSectorSize + 3, mCyclesToACKSent, 0, mbCommandFrameHighSpeed, false, false);
			} else {
				mbLastOpError = false;
				BeginTransferACKCmd();

				// If we are doing this on an 810 or 1050, reset the PERCOM block to default.
				switch(mEmuMode) {
					case kATDiskEmulationMode_810:
					case kATDiskEmulationMode_1050:
						memcpy(mPERCOM, kDefaultPERCOM, sizeof mPERCOM);
						break;
				}

				int formatSectorSize = VDReadUnalignedBEU16(&mPERCOM[6]);
				int formatSectorCount = mPERCOM[0] * (sint32)VDReadUnalignedBEU16(&mPERCOM[2]) * (mPERCOM[4] + 1);
				int formatBootSectorCount = formatSectorSize >= 512 ? 0 : 3;

				g_ATLCDisk("FORMAT COMMAND RECEIVED. Reformatting disk as %u sectors of %u bytes each.\n", formatSectorCount, formatSectorSize);
				FormatDisk(formatSectorCount, formatBootSectorCount, formatSectorSize);

				if (mpOperationEvent)
					mpScheduler->RemoveEvent(mpOperationEvent);

				mpOperationEvent = mpScheduler->AddEvent(1000000, this, kATDiskEventFormatCompleted);
			}
			break;

		case 0x22:	// format disk medium density
		case 0xA2:	// format disk medium density (high speed)
			if (mEmuMode == kATDiskEmulationMode_810)
				goto unsupported_command;

			TurnOnMotor();

			if (!mbWriteEnabled) {
				g_ATLCDisk("FORMAT COMMAND RECEIVED. Blocking due to read-only disk!\n");
				mSendPacket[0] = 'A';		// ACK
				mSendPacket[1] = 'E';		// Error
				mSendPacket[2] = 0xFF;		// Sector terminator (sector buffer data)
				mSendPacket[3] = 0xFF;
				memset(mSendPacket + 4, 0, 126);
				mSendPacket[130] = 0xFF;
				mbLastOpError = true;

				// Assert FDC status bit 6 (write protect).
				mFDCStatus = 0xBF;

				BeginTransfer(131, mCyclesToACKSent, 0, mbCommandFrameHighSpeed, highSpeed, false);
			} else {
				mbLastOpError = false;
				BeginTransferACKCmd();

				g_ATLCDisk("FORMAT COMMAND RECEIVED. Reformatting disk as enhanced density.\n");
				FormatDisk(1040, 3, 128);

				ComputeGeometry();
				ComputePERCOMBlock();

				if (mpOperationEvent)
					mpScheduler->RemoveEvent(mpOperationEvent);

				// Disable high speed operation if we're getting an XF551 command -- the high bit
				// is used for sector skew and not high speed.
				if (command == 0xA2)
					mbActiveCommandHighSpeed = false;

				mpOperationEvent = mpScheduler->AddEvent(1000000, this, kATDiskEventFormatCompleted);
			}
			break;

		case 0x66:	// format disk skewed
		case 0xE6:	// format disk skewed
			if (!mbSupportedCmdFormatSkewed)
				goto unsupported_command;

			TurnOnMotor();

			mbLastOpError = false;
			BeginTransferACKCmd();

			mActiveCommand = 0x66;
			mActiveCommandState = 0;
			break;

		case 0x50:	// put (without verify)
		case 0xD0:	// put (without verify) (XF551 high speed)
		case 0x57:	// write (with verify)
		case 0xD7:	// write (with verify) (XF551 high speed)
			{
				uint32 sector = mReceivePacket[2] + mReceivePacket[3] * 256;

				mLastSector = sector;

				if (sector == mSectorBreakpoint)
					g_sim.PostInterruptingEvent(kATSimEvent_DiskSectorBreakpoint);

				if (!sector || sector > (uint32)mTotalSectorCount) {
					// NAK the command immediately -- the 810 and 1050 both NAK commands
					// with invalid sector numbers.
					mbLastOpError = true;
					BeginTransferNAK();
					g_ATLCDisk("Error writing sector %d.\n", sector);
					break;
				}

				TurnOnMotor();

				mbLastOpError = false;
				BeginTransferACKCmd();

				// get virtual sector information
				ATDiskVirtualSectorInfo vsi;
				mpDiskImage->GetVirtualSectorInfo(sector - 1, vsi);

				mActiveCommand = ((command & 0x7f) == 0x50) ? 'P' : 'W';
				mActiveCommandState = 0;
				mActiveCommandPhysSector = vsi.mStartPhysSector;
				break;
			}
			break;

		case 0x4E:	// read PERCOM block
		case 0xCE:	// read PERCOM block (XF551 high speed)
			if (!mbSupportedCmdPERCOM)
				goto unsupported_command;

			{
				mSendPacket[0] = 'A';
				mSendPacket[1] = 'C';

				ReadPERCOMBlock(mSendPacket + 2);

				const int sectorSize = VDReadUnalignedBEU16(&mPERCOM[6]);
				const int sectorCount = mPERCOM[0] * (sint32)VDReadUnalignedBEU16(&mPERCOM[2]) * (mPERCOM[4] + 1);
				g_ATLCDisk("Reading PERCOM data: %u sectors of %u bytes each, %u boot sectors\n", sectorCount, sectorSize, sectorSize > 256 ? 0 : 3);

				BeginTransfer(15, 2500, 0, mbCommandFrameHighSpeed, highSpeed, false);
			}
			break;

		case 0x4F:	// write PERCOM block
		case 0xCF:	// write PERCOM block (XF551 high speed)
			if (!mbSupportedCmdPERCOM)
				goto unsupported_command;

			{
				BeginTransferACKCmd();
				mActiveCommand = 0x4F;
				mActiveCommandState = 0;
			}
			break;

		case 0x3F:	// get high speed index
			if (!mbSupportedCmdGetHighSpeedIndex)
				goto unsupported_command;

			{
				mSendPacket[0] = 'A';
				mSendPacket[1] = 'C';

				mSendPacket[2] = mHighSpeedIndex;
				mSendPacket[3] = Checksum(mSendPacket + 2, 1);

				BeginTransfer(4, 2500, 0, mbCommandFrameHighSpeed, mbCommandFrameHighSpeed, false);
			}
			break;

		case 0x48:
			if (mEmuMode != kATDiskEmulationMode_Happy)
				goto unsupported_command;

			{
				mSendPacket[0] = 'A';
				mSendPacket[1] = 'C';

				BeginTransfer(2, 2500, 0, mbCommandFrameHighSpeed, mbCommandFrameHighSpeed, false);
			}
			break;

		default:
unsupported_command:
			BeginTransferNAK();

			{
				const char *desc = "?";

				switch(command) {
					case 0x21:
						desc = "Format medium density; not supported by current profile";
						break;

					case 0x3F:
						desc = "Get high speed index; not supported by current profile";
						break;

					case 0x48:
						desc = "Happy command; not supported by current profile";
						break;

					case 0x4E:
						desc = "Read PERCOM block; not supported by current profile";
						break;

					case 0x4F:
						desc = "Write PERCOM block; not supported by current profile";
						break;

					case 0x58:
						desc = "CA-2001 write/execute";
						break;

					case 0x66:
						desc = "Format with sector skew; not supported by current profile";
						break;
				}

				g_ATLCDisk("Unsupported command %02X (%s)\n", command, desc);
			}
			break;

	}

	mpActivity->OnDiskActivity(mUnit + 1, mbWriteMode, mLastSector);
}

void ATDiskEmulator::ProcessCommandTransmitCompleted() {
	if (mActiveCommand == 'W' || mActiveCommand == 'P') {
		ATDiskPhysicalSectorInfo psi;
		mpDiskImage->GetPhysicalSectorInfo(mActiveCommandPhysSector, psi);

		if (mActiveCommandState == 0) {
			// wait for remaining data
			mTransferOffset = 0;
			mTransferLength = psi.mSize + 1;

			g_ATLCDisk("Sent ACK, now waiting for write data.\n", mActiveCommandPhysSector);
			mActiveCommandState = 1;
		} else if (!mbWriteEnabled) {
			mFDCStatus = 0xBF;
			mbLastOpError = true;
			mActiveCommand = 0;

			BeginTransferError();
		} else {
			// commit data to physical sector
			g_ATLCDisk("Writing psec=%3d.\n", mActiveCommandPhysSector);

			try {
				mpDiskImage->WritePhysicalSector(mActiveCommandPhysSector, mReceivePacket, psi.mSize);

				// set FDC status
				mFDCStatus = 0xFF;
				mbLastOpError = false;
			} catch(const MyError&) {
				mFDCStatus = 0xF7;	// crc error
				mbLastOpError = true;
			}

			if (mbAutoFlush)
				QueueAutoSave();

			uint32 rotDelay = kCyclesCompleteDelay_Fast;

			if (mbAccurateSectorTiming) {
				// compute rotational delay
				UpdateRotationalCounter();
				uint32 rotPos = VDRoundToInt(psi.mRotPos * mCyclesPerDiskRotation);

				rotDelay = rotPos < mRotationalCounter ? (rotPos - mRotationalCounter) + mCyclesPerDiskRotation : (rotPos - mRotationalCounter);

				// add verify delay if we're doing write w/verify
				if (mActiveCommand == 'W')
					rotDelay += mCyclesPerDiskRotation;

				rotDelay += 10000;	// fudge factor
			}

			mActiveCommand = 0;

			mpScheduler->SetEvent(rotDelay, this, kATDiskEventWriteCompleted, mpOperationEvent);
		}
	} else if (mActiveCommand == 0x4F) {		// write PERCOM block
		if (mActiveCommandState == 0) {
			// wait for remaining data
			mTransferOffset = 0;
			mTransferLength = 13;

			g_ATLCDisk("Sent ACK, now waiting for PERCOM block data.\n");
			mActiveCommandState = 1;
		} else {

			mFDCStatus = 0xFF;
			mbLastOpError = false;

			mActiveCommand = 0;

			mpScheduler->SetEvent(1000, this, kATDiskEventWriteCompleted, mpOperationEvent);
		}
	} else if (mActiveCommand == 0x66) {		// format skewed
		if (mActiveCommandState == 0) {
			// wait for remaining data
			mTransferOffset = 0;
			mTransferLength = 129;

			g_ATLCDisk("Sent ACK, now waiting for PERCOM and sector skew data.\n");
			mActiveCommandState = 1;
		} else {
			mActiveCommand = 0;

			if (!mbWriteEnabled) {
				g_ATLCDisk("Failing format skewed command ($66/$E6) due to read-only disk!\n");
				mSendPacket[0] = 0x45;		// error
				mSendPacket[1] = 0xFF;
				mSendPacket[2] = 0xFF;
				memset(mSendPacket + 3, 0, mSectorSize - 2);
				mSendPacket[mSectorSize + 1] = 0xFF;

				// Assert FDC status bit 6 (write protect).
				mFDCStatus = 0xBF;

				mbLastOpError = true;
				BeginTransfer(mSectorSize + 2, mCyclesToACKSent, 0, mbActiveCommandHighSpeed, mbActiveCommandHighSpeed, false);
			} else {
				mFDCStatus = 0xFF;
				mbLastOpError = false;

				if (mpOperationEvent)
					mpScheduler->RemoveEvent(mpOperationEvent);
				mpOperationEvent = mpScheduler->AddEvent(1000000, this, kATDiskEventFormatCompleted);
			}
		}
	}
}

void ATDiskEmulator::ProcessCommandData() {
	if (mActiveCommand == 'W' || mActiveCommand == 'P') {
		ATDiskPhysicalSectorInfo psi;
		mpDiskImage->GetPhysicalSectorInfo(mActiveCommandPhysSector, psi);

		// test checksum
		uint8 chk = Checksum(mReceivePacket, psi.mSize);

		if (chk != mReceivePacket[psi.mSize]) {
			g_ATLCDisk("Checksum error detected while receiving write data.\n");

			BeginTransferNAK();
			mActiveCommand = 0;
			return;
		}

		BeginTransferACK();
	} else if (mActiveCommand == 0x4F) {
		// test checksum
		uint8 chk = Checksum(mReceivePacket, 12);

		if (chk != mReceivePacket[12]) {
			g_ATLCDisk("Checksum error detected while receiving PERCOM data.\n");

			BeginTransferNAK();
			return;
		}

		// validate PERCOM data
		bool valid = SetPERCOMData(mReceivePacket);

		if (!valid) {
			BeginTransferError();
			return;
		}

		BeginTransferACK();
	} else if (mActiveCommand == 0x66) {
		// test checksum
		uint8 chk = Checksum(mReceivePacket, 128);

		if (chk != mReceivePacket[128]) {
			g_ATLCDisk("Checksum error detected while receiving PERCOM and sector skew data.\n");

			BeginTransferNAK();
			mActiveCommand = 0;
			return;
		}

		// validate PERCOM data
		bool valid = SetPERCOMData(mReceivePacket);

		if (!valid) {
			BeginTransferError();
			mActiveCommand = 0;
			return;
		}

		BeginTransferACK();

		int formatSectorSize = VDReadUnalignedBEU16(&mPERCOM[6]);
		int formatSectorCount = mPERCOM[0] * (sint32)VDReadUnalignedBEU16(&mPERCOM[2]) * (mPERCOM[4] + 1);
		int formatBootSectorCount = formatSectorSize == 512 ? 0 : 3;

		g_ATLCDisk("FORMAT COMMAND RECEIVED. Reformatting disk as %u sectors of %u bytes each.\n", formatSectorCount, formatSectorSize);
		FormatDisk(formatSectorCount, formatBootSectorCount, formatSectorSize);
	}
}

void ATDiskEmulator::PokeyBeginCommand() {
//	ATConsoleTaggedPrintf("DISK: Beginning command.\n");
	mbCommandMode = true;
	mbCommandValid = true;
	mbCommandFrameHighSpeed = false;
	mTransferOffset = 0;
	mbWriteMode = false;
	mActiveCommand = 0;

	if (mpTransferEvent) {
		mpScheduler->RemoveEvent(mpTransferEvent);
		mpTransferEvent = NULL;
	}

	if (mpOperationEvent) {
		mpScheduler->RemoveEvent(mpOperationEvent);
		mpOperationEvent = NULL;
	}
}

void ATDiskEmulator::PokeyEndCommand() {
	mbCommandMode = false;
	if (!mbCommandValid)
		return;

	if (mTransferOffset == 5) {
		// check if it's us
		if (mReceivePacket[0] != (uint8)(0x31 + mUnit)) {
			return;
		}

		// verify checksum
		uint8 checksum = Checksum(mReceivePacket, 4);
		if (checksum != mReceivePacket[4]) {
			return;
		}

		mbAccessed = true;

		ProcessCommandPacket();
	}
}

void ATDiskEmulator::PokeySerInReady() {
	if (!mbEnabled || !mbBurstTransfersEnabled)
		return;

	if (mbWriteMode && mTransferOffset > 2 && mpTransferEvent && mpScheduler->GetTicksToEvent(mpTransferEvent) > 50) {
		mpScheduler->RemoveEvent(mpTransferEvent);
		mpTransferEvent = mpScheduler->AddEvent(50, this, kATDiskEventTransferByte);
	}
}

void ATDiskEmulator::ComputeGeometry() {
	const ATDiskGeometryInfo& info = mpDiskImage->GetGeometry();

	mTrackCount = info.mTrackCount;
	mSideCount = info.mSideCount;
	mbMFM = info.mbMFM;
	mSectorsPerTrack = info.mSectorsPerTrack;
	mTrackCount = info.mTrackCount;
}

void ATDiskEmulator::ComputePERCOMBlock() {
	// Note that we do not enforce drive invariants (i.e. XF551) here; we do so in the
	// read PERCOM block command instead.

	// track count
	mPERCOM[0] = (uint8)mTrackCount;

	// step rate
	mPERCOM[1] = 0x01;

	// sectors per track
	mPERCOM[2] = (uint8)(mSectorsPerTrack >> 8);
	mPERCOM[3] = (uint8)(mSectorsPerTrack);

	// sides minus one
	mPERCOM[4] = mSideCount ? mSideCount - 1 : 0;

	// record method
	mPERCOM[5] = mbMFM ? 4 : 0;

	// bytes per sector
	mPERCOM[6] = (uint8)(mSectorSize >> 8);
	mPERCOM[7] = (uint8)mSectorSize;

	// drive online
	mPERCOM[8] = 0xFF;

	// unused
	mPERCOM[9] = 0;
	mPERCOM[10] = 0;
	mPERCOM[11] = 0;
}

void ATDiskEmulator::ComputeSupportedProfile() {
	uint32 highSpeedCmdFrameDivisor = 0;

	switch(mEmuMode) {
		case kATDiskEmulationMode_Generic:
		default:
			mbSupportedCmdHighSpeed = true;
			mbSupportedCmdFrameHighSpeed = true;
			mbSupportedCmdPERCOM = true;
			mbSupportedCmdFormatSkewed = true;
			mbSupportedCmdGetHighSpeedIndex = false;
			mHighSpeedIndex = 16;
			mCyclesPerSIOByte = kCyclesPerSIOByte_810;
			mCyclesPerSIOBit = kCyclesPerSIOBit_810;
			mCyclesPerSIOByteHighSpeed = kCyclesPerSIOByte_810;
			mCyclesPerSIOBitHighSpeed = kCyclesPerSIOBit_810;
			mCyclesPerDiskRotation = kCyclesPerDiskRotation_288RPM;
			mCyclesPerTrackStep = kCyclesPerTrackStep_810;
			mCyclesForHeadSettle = kCyclesForHeadSettle_810;
			mbSeekHalfTracks = false;
			mbRetryMode1050 = false;
			mbReverseOnForwardSeeks = false;
			break;

		case kATDiskEmulationMode_Generic57600:
			mbSupportedCmdHighSpeed = true;
			mbSupportedCmdFrameHighSpeed = true;
			mbSupportedCmdPERCOM = true;
			mbSupportedCmdFormatSkewed = true;
			mbSupportedCmdGetHighSpeedIndex = true;
			mHighSpeedIndex = 8;
			highSpeedCmdFrameDivisor = 8;
			mCyclesPerSIOByte = kCyclesPerSIOByte_810;
			mCyclesPerSIOBit = kCyclesPerSIOBit_810;
			mCyclesPerSIOByteHighSpeed = kCyclesPerSIOByte_57600baud;
			mCyclesPerSIOBitHighSpeed = kCyclesPerSIOBit_57600baud;
			mCyclesPerDiskRotation = kCyclesPerDiskRotation_288RPM;
			mCyclesPerTrackStep = kCyclesPerTrackStep_810;
			mCyclesForHeadSettle = kCyclesForHeadSettle_810;
			mbSeekHalfTracks = false;
			mbRetryMode1050 = false;
			mbReverseOnForwardSeeks = false;
			break;

		case kATDiskEmulationMode_FastestPossible:
			mbSupportedCmdHighSpeed = true;
			mbSupportedCmdFrameHighSpeed = true;
			mbSupportedCmdPERCOM = true;
			mbSupportedCmdFormatSkewed = true;
			mbSupportedCmdGetHighSpeedIndex = true;
			mHighSpeedIndex = 0;
			highSpeedCmdFrameDivisor = 0;
			mCyclesPerSIOByte = kCyclesPerSIOByte_810;
			mCyclesPerSIOBit = kCyclesPerSIOBit_810;
			mCyclesPerSIOByteHighSpeed = kCyclesPerSIOByte_PokeyDiv0;
			mCyclesPerSIOBitHighSpeed = kCyclesPerSIOBit_PokeyDiv0;
			mCyclesPerDiskRotation = kCyclesPerDiskRotation_288RPM;
			mCyclesPerTrackStep = kCyclesPerTrackStep_810_3ms;
			mCyclesForHeadSettle = kCyclesForHeadSettle_810;
			mbSeekHalfTracks = false;
			mbRetryMode1050 = false;
			mbReverseOnForwardSeeks = false;
			break;

		case kATDiskEmulationMode_810:
			mbSupportedCmdHighSpeed = false;
			mbSupportedCmdFrameHighSpeed = false;
			mbSupportedCmdPERCOM = false;
			mbSupportedCmdFormatSkewed = false;
			mbSupportedCmdGetHighSpeedIndex = false;
			mHighSpeedIndex = -1;
			mCyclesPerSIOByte = kCyclesPerSIOByte_810;
			mCyclesPerSIOBit = kCyclesPerSIOBit_810;
			mCyclesPerSIOByteHighSpeed = kCyclesPerSIOByte_810;
			mCyclesPerSIOBitHighSpeed = kCyclesPerSIOBit_810;
			mCyclesPerDiskRotation = kCyclesPerDiskRotation_288RPM;
			mCyclesPerTrackStep = kCyclesPerTrackStep_810;
			mCyclesForHeadSettle = kCyclesForHeadSettle_810;
			mbSeekHalfTracks = false;
			mbRetryMode1050 = false;
			mbReverseOnForwardSeeks = false;
			break;

		case kATDiskEmulationMode_1050:
			mbSupportedCmdHighSpeed = false;
			mbSupportedCmdFrameHighSpeed = false;
			mbSupportedCmdPERCOM = false;
			mbSupportedCmdFormatSkewed = false;
			mbSupportedCmdGetHighSpeedIndex = false;
			mHighSpeedIndex = -1;
			mCyclesPerSIOByte = kCyclesPerSIOByte_1050;
			mCyclesPerSIOBit = kCyclesPerSIOBit_1050;
			mCyclesPerSIOByteHighSpeed = kCyclesPerSIOByte_1050;
			mCyclesPerSIOBitHighSpeed = kCyclesPerSIOBit_1050;
			mCyclesPerDiskRotation = kCyclesPerDiskRotation_288RPM;
			mCyclesPerTrackStep = kCyclesPerTrackStep_1050;
			mCyclesForHeadSettle = kCyclesForHeadSettle_1050;
			mbSeekHalfTracks = true;
			mbRetryMode1050 = true;
			mbReverseOnForwardSeeks = true;
			break;

		case kATDiskEmulationMode_XF551:
			mbSupportedCmdHighSpeed = true;
			mbSupportedCmdFrameHighSpeed = false;
			mbSupportedCmdPERCOM = true;
			mbSupportedCmdFormatSkewed = false;
			mbSupportedCmdGetHighSpeedIndex = false;
			mHighSpeedIndex = 16;
			mCyclesPerSIOByte = kCyclesPerSIOByte_XF551;
			mCyclesPerSIOBit = kCyclesPerSIOBit_XF551;
			mCyclesPerSIOByteHighSpeed = kCyclesPerSIOByte_XF551_Fast;
			mCyclesPerSIOBitHighSpeed = kCyclesPerSIOBit_XF551_Fast;
			mCyclesPerDiskRotation = kCyclesPerDiskRotation_300RPM;
			mCyclesPerTrackStep = kCyclesPerTrackStep_XF551;
			mCyclesForHeadSettle = kCyclesForHeadSettle_1050;
			mbSeekHalfTracks = false;
			mbRetryMode1050 = true;
			mbReverseOnForwardSeeks = true;
			break;

		case kATDiskEmulationMode_USDoubler:
			mbSupportedCmdHighSpeed = false;
			mbSupportedCmdFrameHighSpeed = true;
			mbSupportedCmdPERCOM = true;
			mbSupportedCmdFormatSkewed = true;
			mbSupportedCmdGetHighSpeedIndex = true;
			highSpeedCmdFrameDivisor = 10;
			mHighSpeedIndex = 10;
			mCyclesPerSIOByte = kCyclesPerSIOByte_USDoubler;
			mCyclesPerSIOBit = kCyclesPerSIOBit_USDoubler;
			mCyclesPerSIOByteHighSpeed = kCyclesPerSIOByte_USDoubler_Fast;
			mCyclesPerSIOBitHighSpeed = kCyclesPerSIOBit_USDoubler_Fast;
			mCyclesPerDiskRotation = kCyclesPerDiskRotation_288RPM;
			mCyclesPerTrackStep = kCyclesPerTrackStep_1050;
			mCyclesForHeadSettle = kCyclesForHeadSettle_1050;
			mbSeekHalfTracks = true;
			mbRetryMode1050 = true;
			mbReverseOnForwardSeeks = true;
			break;

		case kATDiskEmulationMode_Speedy1050:
			mbSupportedCmdHighSpeed = false;
			mbSupportedCmdFrameHighSpeed = true;
			mbSupportedCmdPERCOM = true;
			mbSupportedCmdFormatSkewed = false;
			mbSupportedCmdGetHighSpeedIndex = true;
			highSpeedCmdFrameDivisor = 9;
			mHighSpeedIndex = 9;
			mCyclesPerSIOByte = kCyclesPerSIOByte_Speedy1050;
			mCyclesPerSIOBit = kCyclesPerSIOBit_Speedy1050;
			mCyclesPerSIOByteHighSpeed = kCyclesPerSIOByte_Speedy1050_Fast;
			mCyclesPerSIOBitHighSpeed = kCyclesPerSIOBit_Speedy1050_Fast;
			mCyclesPerDiskRotation = kCyclesPerDiskRotation_288RPM;
			mCyclesPerTrackStep = kCyclesPerTrackStep_Speedy1050;
			mCyclesForHeadSettle = kCyclesForHeadSettle_1050;
			mbSeekHalfTracks = true;
			mbRetryMode1050 = true;
			mbReverseOnForwardSeeks = true;
			break;

		case kATDiskEmulationMode_IndusGT:
			mbSupportedCmdHighSpeed = true;
			mbSupportedCmdFrameHighSpeed = false;
			mbSupportedCmdPERCOM = true;
			mbSupportedCmdFormatSkewed = false;
			mbSupportedCmdGetHighSpeedIndex = false;
			highSpeedCmdFrameDivisor = 0;
			mHighSpeedIndex = 6;
			mCyclesPerSIOByte = kCyclesPerSIOByte_IndusGT;
			mCyclesPerSIOBit = kCyclesPerSIOBit_IndusGT;
			mCyclesPerSIOByteHighSpeed = kCyclesPerSIOByte_IndusGT_Fast;
			mCyclesPerSIOBitHighSpeed = kCyclesPerSIOBit_IndusGT_Fast;
			mCyclesPerDiskRotation = kCyclesPerDiskRotation_288RPM;
			mCyclesPerTrackStep = kCyclesPerTrackStep_1050;
			mCyclesForHeadSettle = kCyclesForHeadSettle_1050;
			mbSeekHalfTracks = true;
			mbRetryMode1050 = true;
			mbReverseOnForwardSeeks = true;
			break;

		case kATDiskEmulationMode_Happy:
			mbSupportedCmdHighSpeed = false;
			mbSupportedCmdFrameHighSpeed = true;
			mbSupportedCmdPERCOM = true;
			mbSupportedCmdFormatSkewed = false;
			mbSupportedCmdGetHighSpeedIndex = true;
			highSpeedCmdFrameDivisor = 10;
			mHighSpeedIndex = 10;
			mCyclesPerSIOByte = kCyclesPerSIOByte_Happy;
			mCyclesPerSIOBit = kCyclesPerSIOBit_Happy;
			mCyclesPerSIOByteHighSpeed = kCyclesPerSIOByte_Happy_Fast;
			mCyclesPerSIOBitHighSpeed = kCyclesPerSIOBit_Happy_Fast;
			mCyclesPerDiskRotation = kCyclesPerDiskRotation_288RPM;
			mCyclesPerTrackStep = kCyclesPerTrackStep_1050;
			mCyclesForHeadSettle = kCyclesForHeadSettle_1050;
			mbSeekHalfTracks = true;
			mbRetryMode1050 = true;
			mbReverseOnForwardSeeks = true;
			break;

		case kATDiskEmulationMode_1050Turbo:
			mbSupportedCmdHighSpeed = false;
			mbSupportedCmdFrameHighSpeed = false;
			mbSupportedCmdPERCOM = true;
			mbSupportedCmdFormatSkewed = false;
			mbSupportedCmdGetHighSpeedIndex = false;
			highSpeedCmdFrameDivisor = 6;
			mHighSpeedIndex = 6;
			mCyclesPerSIOByte = kCyclesPerSIOByte_1050Turbo;
			mCyclesPerSIOBit = kCyclesPerSIOBit_1050Turbo;
			mCyclesPerSIOByteHighSpeed = kCyclesPerSIOByte_1050Turbo_Fast;
			mCyclesPerSIOBitHighSpeed = kCyclesPerSIOBit_1050Turbo_Fast;
			mCyclesPerDiskRotation = kCyclesPerDiskRotation_288RPM;
			mCyclesPerTrackStep = kCyclesPerTrackStep_1050;
			mCyclesForHeadSettle = kCyclesForHeadSettle_1050;
			mbSeekHalfTracks = true;
			mbRetryMode1050 = true;
			mbReverseOnForwardSeeks = true;
			break;
	}

	mCyclesToACKSent = mCyclesPerSIOByte + kCyclesACKDelay;
	mCyclesToFDCCommand = mCyclesToACKSent + kCyclesFDCCommandDelay;
	mCyclesToCompleteFast = mCyclesToFDCCommand + kCyclesCompleteDelay_Fast;
	mCyclesToCompleteAccurate = mCyclesToFDCCommand + kCyclesCompleteDelay_Accurate;
	mCyclesToCompleteAccurateED = mCyclesToFDCCommand + kCyclesCompleteDelay_Accurate_ED;

	mHighSpeedDataFrameRateLo = mCyclesPerSIOBitHighSpeed - (mCyclesPerSIOBitHighSpeed + 19) / 20;
	mHighSpeedDataFrameRateHi = mCyclesPerSIOBitHighSpeed + (mCyclesPerSIOBitHighSpeed + 19) / 20;

	mHighSpeedCmdFrameRateLo = 0;
	mHighSpeedCmdFrameRateHi = 0;

	if (mbSupportedCmdFrameHighSpeed) {
		int rate = (highSpeedCmdFrameDivisor + 7) * 2;
		mHighSpeedCmdFrameRateLo = rate - (rate + 19) / 20;
		mHighSpeedCmdFrameRateHi = rate + (rate + 19) / 20;
	}
}

bool ATDiskEmulator::SetPERCOMData(const uint8 *data) {
	uint16 sectorSize;
	uint32 sectorCount;

	if (mEmuMode == kATDiskEmulationMode_XF551) {
		// The XF551 is very lax about PERCOM blocks: it simply checks the minimum
		// number of bytes to detect SD, ED, or DD formats.

		if (data[3] == 26) {
			// enhanced density
			mPERCOM[2] = 0;		// spt high
			mPERCOM[3] = 26;
			mPERCOM[4] = 0;		// sides minus one
			mPERCOM[5] = 4;		// FM/MFM encoding
			mPERCOM[6] = 0;		// bps high
			mPERCOM[7] = 128;		// bps low
		} else if (data[4] == 0) {
			// single density
			mPERCOM[2] = 0;		// spt high
			mPERCOM[3] = 18;		// spt low
			mPERCOM[4] = 0;		// sides minus one
			mPERCOM[5] = 0;		// FM/MFM encoding
			mPERCOM[6] = 0;		// bps high
			mPERCOM[7] = 128;		// bps low
		} else {
			if (data[4]) {
				// DSDD
				mPERCOM[ 4] = 1;		// sides minus one
			} else {
				// DSSD
				mPERCOM[ 4] = 0;		// sides minus one
			}

			mPERCOM[2] = 0;		// spt high
			mPERCOM[3] = 18;		// spt low
			mPERCOM[5] = 4;		// FM/MFM encoding
			mPERCOM[6] = 1;		// bps high
			mPERCOM[7] = 0;		// bps low
		}
		
		// force XF551 invariants
		mPERCOM[ 0] = 40;		// 40 tracks
		mPERCOM[ 1] = 0;		// step rate 0
		mPERCOM[ 8] = 1;		// drive active
		mPERCOM[ 9] = 0x41;		// reserved
		mPERCOM[10] = 0;		// reserved
		mPERCOM[11] = 0;		// reserved

		sectorSize = VDReadUnalignedBEU16(&mPERCOM[6]);
		sectorCount = mPERCOM[0] * (sint32)VDReadUnalignedBEU16(&mPERCOM[2]) * (mPERCOM[4] + 1);
	} else {
		sectorSize = VDReadUnalignedBEU16(&data[6]);
		sectorCount = data[0] * (sint32)VDReadUnalignedBEU16(&data[2]) * (data[4] + 1);

		if (data[0] == 0) {
			g_ATLCDisk("Invalid PERCOM data: tracks per sector = 0\n");
			return false;
		}
		
		if (data[2] == 0 && data[3] == 0) {
			g_ATLCDisk("Invalid PERCOM data: sectors per track = 0\n");
			return false;
		}
		
		if (data[4] >= 2) {
			g_ATLCDisk("Invalid PERCOM data: invalid sides encoded value %02x\n", data[4]);
			return false;
		}
		
		if (sectorCount > 65535) {
			g_ATLCDisk("Invalid PERCOM data: total sectors > 65535\n");
			return false;
		}
		
		if (sectorSize != 128 && sectorSize != 256 && sectorSize != 512 && sectorSize != 8192) {
			g_ATLCDisk("Invalid PERCOM data: unsupported sector size (%u)\n", sectorSize);
			return false;
		}

		memcpy(mPERCOM, data, 12);
	}

	g_ATLCDisk("Setting PERCOM data: %u sectors of %u bytes each, %u sides, %u boot sectors\n", sectorCount, sectorSize, mPERCOM[4]+1, sectorSize > 256 ? 0 : 3);
	return true;
}

bool ATDiskEmulator::TurnOnMotor(uint32 delay) {
	bool spinUpDelay = !mpMotorOffEvent;

	if (spinUpDelay) {
		if (mpActivity)
			mpActivity->OnDiskMotorChange(mUnit + 1, true);

		if (!mRotationSoundId && mbDriveSoundsEnabled)
			mRotationSoundId = mpAudioSyncMixer->AddLoopingSound(kATAudioMix_Drive, delay, g_diskspin, sizeof(g_diskspin)/sizeof(g_diskspin[0]), 0.05f);
	}

	mpSlowScheduler->SetEvent(48041, this, kATDiskEventMotorOff, mpMotorOffEvent);

	return spinUpDelay;
}

void ATDiskEmulator::PlaySeekSound(uint32 stepDelay, uint32 tracksToStep) {
	float v = 0.4f;

	if (!mbDriveSoundsEnabled)
		return;

	// limit step length in case we have a synthetic disk or aren't waiting
	if (mbAccurateSectorTiming) {
		if (tracksToStep > 80)
			tracksToStep = 80;
	} else {
		if (tracksToStep > 10)
			tracksToStep = 10;
	}

	if (mbSeekHalfTracks) {
		for(uint32 i=0; i<tracksToStep; ++i) {
			mpAudioSyncMixer->AddSound(kATAudioMix_Drive, stepDelay, g_disksample3, vdcountof(g_disksample3), v);

			stepDelay += mCyclesPerTrackStep;
		}
	} else {
		switch(mEmuMode) {
		case kATDiskEmulationMode_810:
		case kATDiskEmulationMode_Generic:
		case kATDiskEmulationMode_Generic57600:
		case kATDiskEmulationMode_FastestPossible:
			for(uint32 i=0; i<tracksToStep; ++i) {
				mpAudioSyncMixer->AddSound(kATAudioMix_Drive, stepDelay, g_disksample, 3868/2, v * (0.3f + 0.7f * sinf(i * nsVDMath::kfPi * 0.5f)));

				stepDelay += mCyclesPerTrackStep;
			}
			break;

		default:
			for(uint32 i=0; i<tracksToStep; i += 2) {
				if (i + 2 > tracksToStep)
					mpAudioSyncMixer->AddSound(kATAudioMix_Drive, stepDelay, g_disksample2, 1778/4, v * 2.0f);
				else
					mpAudioSyncMixer->AddSound(kATAudioMix_Drive, stepDelay, g_disksample2, 1778/2, v * 2.0f);

				stepDelay += mCyclesPerTrackStep * 2;
			}
			break;
		}
	}
}
