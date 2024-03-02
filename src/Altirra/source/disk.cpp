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

ATDebuggerLogChannel g_ATLCDiskImage(false, false, "DISKIMAGE", "Disk image load details");
ATDebuggerLogChannel g_ATLCDisk(false, false, "DISK", "Disk activity");
ATDebuggerLogChannel g_ATLCDiskCmd(false, false, "DISKCMD", "Disk commands");

extern ATSimulator g_sim;

namespace {
	// The 810 rotates at 288 RPM.
	static const int kCyclesPerDiskRotation = 372869;

	// Use a 40ms step rate. The 810 ROM requests r1r0=11 when talking to the FDC.
	static const int kCyclesPerTrackStep = 71591;

	// Use a head settling time of 10ms. This is hardcoded into the WD1771 FDC.
	static const int kCyclesForHeadSettle = 17898;

	// The bit cell rate is 1MHz.
	static const int kBytesPerTrack = 26042;

	// Approx. number of cycles it takes for the CPU to send out the request.
	static const int kCyclesToProcessRequest = 7000;

	static const int kCyclesToExitSIO = 350;

	///////////////////////////////////////////////////////////////////////////////////
	// SIO timing parameters
	//
	// WARNING: KARATEKA IS VERY SENSITIVE TO THESE PARAMETERS AS IT HAS STUPIDLY
	//			CLOSE PHANTOM SECTORS.
	//

	// The number of cycles per byte sent over the SIO bus -- approximately
	// 19200 baud.
	static const int kCyclesPerSIOByte = 945;		// was 939, but 945 is closer to actual 810 speed
	static const int kCyclesPerSIOBit = 94;

	// XF551 high speed data transfer rate -- approx. 39000 baud (POKEY divisor = 0x10).
	static const int kCyclesPerSIOByteHighSpeed = 460;
	static const int kCyclesPerSIOBitHighSpeed = 46;

	// Delay from end of request to end of ACK byte.
	static const int kCyclesToACKSent = kCyclesPerSIOByte + 1000;

	// Delay from end of ACK byte until FDC command is sent.
	static const int kCyclesToFDCCommand = kCyclesToACKSent + 4000;

	// Delay from end of ACK byte to end of first data byte, not counting rotational delay.
//	static const int kCyclesToFirstData = kCyclesToFDCCommand + 0;
//	static const int kCyclesToFirstData = kCyclesToFDCCommand + 10000;
	static const int kCyclesToComplete = kCyclesToFDCCommand + 28000;

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
		kATDiskEventRotation = 1,
		kATDiskEventTransferByte,
		kATDiskEventWriteCompleted,
		kATDiskEventFormatCompleted,
		kATDiskEventAutoSave,
		kATDiskEventAutoSaveError
	};

	static const int kAutoSaveDelay = 3579545;		// 2 seconds

	static const uint8 kDefaultPERCOM[]={
		0x28, 0x01, 0x00, 0x12, 0x00, 0x00, 0x00, 0x80, 0xFF, 0x00, 0x00, 0x00
	};
}

ATDiskEmulator::ATDiskEmulator()
	: mpTransferEvent(NULL)
	, mpRotationalEvent(NULL)
	, mpOperationEvent(NULL)
	, mpAutoSaveEvent(NULL)
	, mpAutoSaveErrorEvent(NULL)
{
	Reset();
}

ATDiskEmulator::~ATDiskEmulator() {
}

void ATDiskEmulator::Init(int unit, IATDiskActivity *act, ATScheduler *sched) {
	mLastSector = 0;
	mUnit = unit;
	mpActivity = act;
	mpScheduler = sched;
	mbEnabled = false;
	mbAccurateSectorTiming = false;
	mbAccurateSectorPrediction = false;
	mSectorBreakpoint = -1;
	mpRotationalEvent = mpScheduler->AddEvent(kCyclesPerDiskRotation, this, kATDiskEventRotation);
	mbWriteEnabled = false;
	mbErrorIndicatorPhase = false;
	mbAccessed = false;
	mbHasDiskSource = false;

	memcpy(mPERCOM, kDefaultPERCOM, 12);
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

void ATDiskEmulator::Flush() {
	if (mpAutoSaveEvent)
		AutoSave();
}

void ATDiskEmulator::Reset() {
	if (mpTransferEvent) {
		mpScheduler->RemoveEvent(mpTransferEvent);
		mpTransferEvent = NULL;
	}

	if (mpRotationalEvent) {
		mpScheduler->RemoveEvent(mpRotationalEvent);
		mpRotationalEvent = NULL;
	}

	if (mpOperationEvent) {
		mpScheduler->RemoveEvent(mpOperationEvent);
		mpOperationEvent = NULL;
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

	for(ExtVirtSectors::iterator it(mExtVirtSectors.begin()), itEnd(mExtVirtSectors.end()); it!=itEnd; ++it) {
		ExtVirtSector& vsi = *it;

		vsi.mPhantomSectorCounter = 0;
	}

	mWeakBitLFSR = 1;

	if (IsDiskLoaded())
		ComputePERCOMBlock();
	else
		memcpy(mPERCOM, kDefaultPERCOM, 12);
}

void ATDiskEmulator::LoadDisk(const wchar_t *s) {
	VDFileStream f(s);

	LoadDisk(s, s, f);
}

void ATDiskEmulator::LoadDisk(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream) {
	UnloadDisk();

	try {
		mpDiskImage = ATLoadDiskImage(origPath, imagePath, stream);

		InitSectorInfoArrays();

		mPath = origPath;
	} catch(const MyError&) {
		UnloadDisk();
		throw;
	}

	mCurrentTrack = 0;
	ComputeSectorsPerTrack();
	ComputePERCOMBlock();
	mbEnabled = true;
	mbWriteEnabled = false;
	mbAutoFlush = false;
	mbHasDiskSource = true;
}

void ATDiskEmulator::UpdateDisk() {
	if (!mpDiskImage->Flush())
		throw MyError("The current disk image cannot be updated.");
}

void ATDiskEmulator::SaveDisk(const wchar_t *s) {
	mpDiskImage->SaveATR(s);

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
		mpDiskImage->SetPathATR(mPath.c_str());

	InitSectorInfoArrays();
	ComputeSectorsPerTrack();
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

float ATDiskEmulator::GetSectorTiming(uint16 sector, int phantomIdx) const {
	if (!mpDiskImage)
		return -1;

	if (!sector || sector > mpDiskImage->GetVirtualSectorCount())
		return -1;

	ATDiskVirtualSectorInfo vsi;
	mpDiskImage->GetVirtualSectorInfo(sector - 1, vsi);

	if (phantomIdx < 0 || (uint32)phantomIdx >= vsi.mNumPhysSectors)
		return -1;

	ATDiskPhysicalSectorInfo psi;
	mpDiskImage->GetPhysicalSectorInfo(vsi.mStartPhysSector + phantomIdx, psi);

	return psi.mRotPos;
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
			mRotationalCounter = preRotPos % kCyclesPerDiskRotation;
			mRotations += preRotPos / kCyclesPerDiskRotation;

			ProcessCommandPacket();

			// fake rotation
			uint32 rotPos = mRotationalPosition + kCyclesPerSIOByte * (mTransferLength - 1) + kCyclesToComplete + kCyclesToExitSIO;

			mRotationalCounter = rotPos % kCyclesPerDiskRotation;
			mRotations += rotPos / kCyclesPerDiskRotation;

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
		return 0x8B;	// DNACK

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
	uint8 buf[512];

	for(uint32 i=0; i<len; ++i)
		buf[i] = mpMem->ReadByte(bufadr+i);

	mpDiskImage->WritePhysicalSector(vsi.mStartPhysSector, buf, len);

	mLastSector = sector;

	if (mpActivity) {
		mpActivity->OnDiskActivity(mUnit + 1, true, mLastSector);
		mpActivity->OnDiskActivity(mUnit + 1, false, mLastSector);
	}

	if (mbAutoFlush)
		QueueAutoSave();

	// return success
	return 0x01;
}

void ATDiskEmulator::ReadStatus(uint8 dst[5]) {
	dst[0] = (mSectorSize > 128 ? 0x30 : 0x10) + (mbLastOpError ? 0x04 : 0x00) + (mbWriteEnabled ? 0x00 : 0x08) + (mSectorsPerTrack == 26 ? 0x80 : 0x00);
	dst[1] = mTotalSectorCount > 0 ? mFDCStatus : 0x7F;
	dst[2] = 0xe0;
	dst[3] = 0x00;
	dst[4] = Checksum(dst, 4);
}

void ATDiskEmulator::ReadPERCOMBlock(uint8 dst[13]) {
	memcpy(dst, mPERCOM, 12);
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

		mpPokey->ReceiveSIOByte(mSendPacket[mTransferOffset], mTransferOffset == 0 ? mTransferCyclesPerBitFirstByte : mTransferCyclesPerBit, true);
		++mTransferOffset;

		// SIO barfs if the third byte is sent too quickly
		uint32 transferDelay = mTransferRate;
		if (mTransferOffset == 1) {
			transferDelay = kCyclesToComplete;

			if (mbWriteRotationalDelay) {
				// compute rotational delay
				UpdateRotationalCounter();

				// add rotational delay
				uint32 rotationalDelay;
				if (mRotationalCounter > mRotationalPosition)
					rotationalDelay = kCyclesPerDiskRotation - mRotationalCounter + mRotationalPosition;
				else
					rotationalDelay = mRotationalPosition - mRotationalCounter;
	
				// If accurate sector timing is enabled, delay execution until the sector arrives. Otherwise,
				// warp the disk position.
				if (mbAccurateSectorTiming) {
					transferDelay += rotationalDelay;
				} else {
					uint32 rotPos = mRotationalCounter + rotationalDelay;

					mRotationalCounter = rotPos % kCyclesPerDiskRotation;
					mRotations += rotPos / kCyclesPerDiskRotation;
				}
			}
		}

		// Doc Wire's Solitaire Solution needs a bit more delay between the Complete byte and the first
		// data byte at high speed.
		if (mTransferOffset == 2 && mbWriteHighSpeed)
			transferDelay = kCyclesToFirstDataHighSpeed;

		if (mTransferOffset >= mTransferLength) {
			UpdateRotationalCounter();

			g_ATLCDisk("Disk transmit finished. (len=%u, rot=%.2f)\n", mTransferLength, (float)mRotations + (float)mRotationalCounter / (float)kCyclesPerDiskRotation);

			mTransferOffset = 0;
			mTransferLength = 0;
			mbWriteMode = false;
			mpActivity->OnDiskActivity(mUnit + 1, false, mLastSector);

			if (mActiveCommand)
				ProcessCommandTransmitCompleted();
		} else {
			mpTransferEvent = mpScheduler->AddEvent(transferDelay, this, kATDiskEventTransferByte);
		}
	} else if (id == kATDiskEventRotation) {
		mRotationalCounter += kCyclesPerDiskRotation;
		mpRotationalEvent = NULL;
		UpdateRotationalCounter();
	} else if (id == kATDiskEventWriteCompleted) {
		mpOperationEvent = NULL;
		mSendPacket[0] = 'C';
		BeginTransfer(1, kCyclesToACKSent, false, mbActiveCommandHighSpeed, mbActiveCommandHighSpeed);
	} else if (id == kATDiskEventFormatCompleted) {
		mpOperationEvent = NULL;
		mSendPacket[0] = 'C';
		memset(mSendPacket+1, 0xFF, mSectorSize);
		mSendPacket[mSectorSize + 1] = Checksum(mSendPacket+1, mSectorSize);
		BeginTransfer(mSectorSize + 2, kCyclesToACKSent, false, false, false);
	} else if (id == kATDiskEventAutoSave) {
		mpAutoSaveEvent = NULL;

		AutoSave();
	} else if (id == kATDiskEventAutoSaveError) {
		mpActivity->OnDiskActivity(mUnit + 1, mbErrorIndicatorPhase, mUnit + 1);
		mbErrorIndicatorPhase = !mbErrorIndicatorPhase;

		mpAutoSaveErrorEvent = mpScheduler->AddEvent(894886, this, kATDiskEventAutoSaveError);
	}
}

void ATDiskEmulator::PokeyAttachDevice(ATPokeyEmulator *pokey) {
	mpPokey = pokey;
}

void ATDiskEmulator::PokeyWriteSIO(uint8 c, bool command, uint32 cyclesPerBit) {
	if (!mbEnabled)
		return;

	if (mbWriteMode) {
		mbWriteMode = false;
		mTransferOffset = 0;
	}

	if (mActiveCommand) {
		if (mTransferOffset < mTransferLength)
			mReceivePacket[mTransferOffset++] = c;

		if (mTransferOffset >= mTransferLength)
			ProcessCommandData();

		return;
	}

	if (!mbCommandMode)
		return;

//	ATConsoleTaggedPrintf("DISK: Receiving command byte %02x (index=%d)\n", c, mTransferOffset);

	if (mTransferOffset < 16)
		mReceivePacket[mTransferOffset++] = c;
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

void ATDiskEmulator::BeginTransfer(uint32 length, uint32 cyclesToFirstByte, bool rotationalDelay, bool useHighSpeedFirstByte, bool useHighSpeed) {
	mbWriteMode = true;
	mbWriteRotationalDelay = rotationalDelay;
	mbWriteHighSpeedFirstByte = useHighSpeedFirstByte;
	mbWriteHighSpeed = useHighSpeed;
	mTransferRate = useHighSpeed ? kCyclesPerSIOByteHighSpeed : kCyclesPerSIOByte;
	mTransferCyclesPerBit = useHighSpeed ? kCyclesPerSIOBitHighSpeed : kCyclesPerSIOBit;
	mTransferCyclesPerBitFirstByte = useHighSpeedFirstByte ? kCyclesPerSIOBitHighSpeed : kCyclesPerSIOBit;
	mTransferOffset = 0;
	mTransferLength = length;

	if (mpTransferEvent)
		mpScheduler->RemoveEvent(mpTransferEvent);

	mpTransferEvent = mpScheduler->AddEvent(cyclesToFirstByte, this, kATDiskEventTransferByte);
}

void ATDiskEmulator::UpdateRotationalCounter() {
	if (mpRotationalEvent) {
		mRotationalCounter += kCyclesPerDiskRotation - mpScheduler->GetTicksToEvent(mpRotationalEvent);
		mpScheduler->RemoveEvent(mpRotationalEvent);
	}

	mpRotationalEvent = mpScheduler->AddEvent(kCyclesPerDiskRotation, this, kATDiskEventRotation);

	if (mRotationalCounter >= kCyclesPerDiskRotation) {
		uint32 rotations = mRotationalCounter / kCyclesPerDiskRotation;
		mRotationalCounter %= kCyclesPerDiskRotation;
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
	g_ATLCDiskCmd("Processing command: Unit %02X, Command %02X, Aux data %02X %02X\n"
		, mReceivePacket[0]
		, mReceivePacket[1]
		, mReceivePacket[2]
		, mReceivePacket[3]
	);
	const uint8 command = mReceivePacket[1];
	const bool highSpeed = (command & 0x80) != 0;

	mbActiveCommandHighSpeed = highSpeed;

	switch(command) {
		case 0x53:	// status
		case 0xD3:	// status (XF551 high speed)
			mSendPacket[0] = 0x41;
			mSendPacket[1] = 0x43;
			ReadStatus(mSendPacket + 2);
			BeginTransfer(7, 2500, false, false, highSpeed);
			break;

		case 0x52:	// read
		case 0xD2:	// read (XF551 high speed)
			{
				uint32 sector = mReceivePacket[2] + mReceivePacket[3] * 256;

				mLastSector = sector;

				if (sector == mSectorBreakpoint)
					g_sim.PostInterruptingEvent(kATSimEvent_DiskSectorBreakpoint);

				mSendPacket[0] = 0x41;		// ACK
				if (!sector || sector > (uint32)mTotalSectorCount) {
					// NAK the command immediately. We used to send ACK (41) + ERROR (45), but the kernel
					// is stupid and waits for data anyway. The Linux ATARISIO kernel module does this
					// too.
					mSendPacket[0] = 0x4E;		// error

					mbLastOpError = true;
					BeginTransfer(1, kCyclesToACKSent, false, false, false);
					g_ATLCDisk("Error reading sector %d.\n", sector);
					break;
				}

				// get virtual sector information
				ATDiskVirtualSectorInfo vsi;
				mpDiskImage->GetVirtualSectorInfo(sector - 1, vsi);

				ExtVirtSector& evs = mExtVirtSectors[sector - 1];

				// choose a physical sector
				bool missingSector = false;

				uint32 physSector;
				if (mbAccurateSectorPrediction || mbAccurateSectorTiming) {
					uint32 track = (sector - 1) / mSectorsPerTrack;
					uint32 tracksToStep = (uint32)abs((int)track - (int)mCurrentTrack);

					mCurrentTrack = track;

					if (vsi.mNumPhysSectors == 0) {
						missingSector = true;
						mbLastOpError = true;

						// sector not found....
						mSendPacket[0] = 0x41;
						mSendPacket[1] = 0x45;
//						memset(mSendPacket+2, 0, 128);
						mSendPacket[128+2] = Checksum(mSendPacket + 2, 128);
						BeginTransfer(131, kCyclesToACKSent, false, false, highSpeed);

						mRotationalCounter += kCyclesPerDiskRotation >> 1;
						UpdateRotationalCounter();

						g_ATLCDisk("Reporting missing sector %d.\n", sector);
						break;
					}

					UpdateRotationalCounter();
					uint32 postSeekPosition = (mRotationalCounter + kCyclesToFDCCommand + (tracksToStep ? tracksToStep * kCyclesPerTrackStep + kCyclesForHeadSettle : 0)) % kCyclesPerDiskRotation;
					uint32 bestDelay = 0xFFFFFFFFU;
					uint8 bestStatus = 0;

					physSector = vsi.mStartPhysSector;

					for(uint32 i=0; i<vsi.mNumPhysSectors; ++i) {
						ATDiskPhysicalSectorInfo psi;
						mpDiskImage->GetPhysicalSectorInfo(vsi.mStartPhysSector + i, psi);

						const ExtPhysSector& eps = mExtPhysSectors[vsi.mStartPhysSector + i];
						uint32 time = VDRoundToInt(psi.mRotPos * kCyclesPerDiskRotation);
						uint32 delay = time < mRotationalCounter ? time + kCyclesPerDiskRotation - mRotationalCounter : time - mRotationalCounter;
						uint8 status = psi.mFDCStatus;

						if (eps.mForcedOrder == evs.mPhantomSectorCounter) {
							physSector = vsi.mStartPhysSector + i;
							mPhantomSectorCounter = i;

							if (++evs.mPhantomSectorCounter >= vsi.mNumPhysSectors)
								evs.mPhantomSectorCounter = 0;
							break;
						}

						if ((delay < bestDelay && (psi.mFDCStatus == 0xff || bestStatus != 0xff)) || (bestStatus != 0xFF && psi.mFDCStatus == 0xFF)) {
							bestDelay = delay;
							bestStatus = psi.mFDCStatus;

							physSector = vsi.mStartPhysSector + i;
							mPhantomSectorCounter = i;
						}
					}
				} else {
					physSector = vsi.mStartPhysSector + evs.mPhantomSectorCounter;

					if (++evs.mPhantomSectorCounter >= vsi.mNumPhysSectors)
						evs.mPhantomSectorCounter = 0;
				}

				ATDiskPhysicalSectorInfo psi;
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
				mRotationalPosition = VDRoundToInt(psi.mRotPos * kCyclesPerDiskRotation);

				// check for missing sector
				// note: must send ACK (41) + ERROR (45) -- BeachHead expects to get DERROR from SIO
				if (!psi.mSize) {
					mSendPacket[0] = 0x41;
					mSendPacket[1] = 0x45;
//					memset(mSendPacket+2, 0, 128);
					mSendPacket[128+2] = Checksum(mSendPacket + 2, 128);
					BeginTransfer(131, kCyclesToACKSent, true, false, highSpeed);
					mbLastOpError = true;

					g_ATLCDisk("Reporting missing sector %d.\n", sector);
					break;
				}

				mSendPacket[1] = 0x43;		// complete

				mbLastOpError = (mFDCStatus != 0xFF);

				mpDiskImage->ReadPhysicalSector(physSector, mSendPacket + 2, psi.mSize);

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

				BeginTransfer(transferLength, kCyclesToACKSent, true, false, highSpeed);
				g_ATLCDisk("Reading vsec=%3d (%d/%d) (trk=%d), psec=%3d, chk=%02x, rot=%.2f >> %.2f%s.\n"
						, sector
						, physSector - vsi.mStartPhysSector + 1
						, vsi.mNumPhysSectors
						, (sector - 1) / 18
						, physSector
						, mSendPacket[mTransferLength - 1]
						, (float)mRotations + (float)mRotationalCounter / (float)kCyclesPerDiskRotation
						, psi.mRotPos
						, mFDCStatus & 0x08 ? "" : " (w/CRC error)");
			}
			break;

#if 0
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
			if (!mbWriteEnabled) {
				g_ATLCDisk("FORMAT COMMAND RECEIVED. Blocking due to read-only disk!\n");
				mSendPacket[0] = 'N';
				mbLastOpError = true;
				BeginTransfer(1, kCyclesToACKSent, false, false, false);
			} else {
				mSendPacket[0] = 'A';
				mbLastOpError = false;
				BeginTransfer(1, kCyclesToACKSent, false, false, false);

				int formatSectorSize = VDReadUnalignedBEU16(&mPERCOM[6]);
				int formatSectorCount = mPERCOM[0] * (sint32)VDReadUnalignedBEU16(&mPERCOM[2]) * (mPERCOM[4] + 1);
				int formatBootSectorCount = formatSectorSize == 512 ? 0 : 3;

				g_ATLCDisk("FORMAT COMMAND RECEIVED. Reformatting disk as %u sectors of %u bytes each.\n", formatSectorCount, formatSectorSize);
				FormatDisk(formatSectorCount, formatBootSectorCount, formatSectorSize);

				if (mpOperationEvent)
					mpScheduler->RemoveEvent(mpOperationEvent);
				mpOperationEvent = mpScheduler->AddEvent(1000000, this, kATDiskEventFormatCompleted);
			}
			break;

		case 0x22:	// format disk medium density
			if (!mbWriteEnabled) {
				g_ATLCDisk("FORMAT COMMAND RECEIVED. Blocking due to read-only disk!\n");
				mSendPacket[0] = 'N';
				mbLastOpError = true;
				BeginTransfer(1, kCyclesToACKSent, false, false, false);
			} else {
				mSendPacket[0] = 'A';
				mbLastOpError = false;
				BeginTransfer(1, kCyclesToACKSent, false, false, false);

				g_ATLCDisk("FORMAT COMMAND RECEIVED. Reformatting disk as enhanced density.\n");
				FormatDisk(1040, 3, 128);

				ComputePERCOMBlock();

				if (mpOperationEvent)
					mpScheduler->RemoveEvent(mpOperationEvent);
				mpOperationEvent = mpScheduler->AddEvent(1000000, this, kATDiskEventFormatCompleted);
			}
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

				mSendPacket[0] = 0x41;		// ACK
				if (!sector || sector > (uint32)mTotalSectorCount || !mbWriteEnabled) {
					// NAK the command immediately. We used to send ACK (41) + ERROR (45), but the kernel
					// is stupid and waits for data anyway. The Linux ATARISIO kernel module does this
					// too.
					mSendPacket[0] = 0x4E;		// error

					mbLastOpError = true;
					BeginTransfer(1, kCyclesToACKSent, false, false, highSpeed);
					g_ATLCDisk("Error writing sector %d.\n", sector);
					break;
				}

				mbLastOpError = false;
				BeginTransfer(1, kCyclesToACKSent, false, false, highSpeed);

				// get virtual sector information
				ATDiskVirtualSectorInfo vsi;
				mpDiskImage->GetVirtualSectorInfo(sector - 1, vsi);

				mActiveCommand = 'W';
				mActiveCommandState = 0;
				mActiveCommandPhysSector = vsi.mStartPhysSector;
				break;
			}
			break;

		case 0x4E:	// read PERCOM block
		case 0xCE:	// read PERCOM block (XF551 high speed)
			{
				mSendPacket[0] = 'A';
				mSendPacket[1] = 'C';

				ReadPERCOMBlock(mSendPacket + 2);

				BeginTransfer(15, 2500, false, false, highSpeed);
			}
			break;

		case 0x4F:	// write PERCOM block
		case 0xCF:	// write PERCOM block (XF551 high speed)
			{
				mSendPacket[0] = 'A';
				BeginTransfer(1, kCyclesToACKSent, false, false, highSpeed);
				mActiveCommand = 0x4F;
				mActiveCommandState = 0;
			}
			break;

#if 0
		case 0x3F:	// get high speed index
			{
				mSendPacket[0] = 'A';
				mSendPacket[1] = 'C';

				mSendPacket[2] = 0x06;
				mSendPacket[3] = Checksum(mSendPacket + 2, 1);

				BeginTransfer(4, 2500, false, false, false);
			}
			break;
#endif

		default:
			mSendPacket[0] = 0x4E;
			BeginTransfer(1, kCyclesToACKSent, false, false, false);

			{
				const char *desc = "?";

				switch(command) {
					case 0x3F:
						desc = "Get high speed index";
						break;

					case 0x4F:
						desc = "Write PERCOM block";
						break;

					case 0x58:
						desc = "CA-2001 write/execute";
						break;
				}

				g_ATLCDisk("Unsupported command %02X (%s)\n", command, desc);
			}
			break;

	}

	mpActivity->OnDiskActivity(mUnit + 1, mbWriteMode, mLastSector);
}

void ATDiskEmulator::ProcessCommandTransmitCompleted() {
	if (mActiveCommand == 'W') {
		ATDiskPhysicalSectorInfo psi;
		mpDiskImage->GetPhysicalSectorInfo(mActiveCommandPhysSector, psi);

		if (mActiveCommandState == 0) {
			// wait for remaining data
			mTransferOffset = 0;
			mTransferLength = psi.mSize + 1;

			g_ATLCDisk("Sent ACK, now waiting for write data.\n", mActiveCommandPhysSector);
			mActiveCommandState = 1;
		} else {
			// commit data to physical sector
			mpDiskImage->WritePhysicalSector(mActiveCommandPhysSector, mReceivePacket, psi.mSize);

			g_ATLCDisk("Writing psec=%3d.\n", mActiveCommandPhysSector);

			// set FDC status
			mFDCStatus = 0xFF;
			mbLastOpError = false;

			// compute rotational delay
			UpdateRotationalCounter();
			uint32 rotPos = VDRoundToInt(psi.mRotPos * kCyclesPerDiskRotation);

			uint32 rotDelay = rotPos < mRotationalCounter ? (rotPos - mRotationalCounter) + kCyclesPerDiskRotation : (rotPos - mRotationalCounter);

			rotDelay += 10000;	// fudge factor

			mActiveCommand = 0;

			if (mbAutoFlush)
				QueueAutoSave();

			if (mpOperationEvent)
				mpScheduler->RemoveEvent(mpOperationEvent);
			mpOperationEvent = mpScheduler->AddEvent(rotDelay, this, kATDiskEventWriteCompleted);
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
	}
}

void ATDiskEmulator::ProcessCommandData() {
	if (mActiveCommand == 'W') {
		ATDiskPhysicalSectorInfo psi;
		mpDiskImage->GetPhysicalSectorInfo(mActiveCommandPhysSector, psi);

		// test checksum
		uint8 chk = Checksum(mReceivePacket, psi.mSize);

		if (chk != mReceivePacket[psi.mSize]) {
			g_ATLCDisk("Checksum error detected while receiving write data.\n");

			mSendPacket[0] = 'E';
			BeginTransfer(1, kCyclesToACKSent, false, mbActiveCommandHighSpeed, mbActiveCommandHighSpeed);
			mActiveCommand = 0;
			return;
		}

		mSendPacket[0] = 'A';
		BeginTransfer(1, kCyclesToACKSent, false, mbActiveCommandHighSpeed, mbActiveCommandHighSpeed);
	} else if (mActiveCommand == 0x4F) {
		// test checksum
		uint8 chk = Checksum(mReceivePacket, 12);

		if (chk != mReceivePacket[12]) {
			g_ATLCDisk("Checksum error detected while receiving PERCOM data.\n");

			mSendPacket[0] = 'E';
			BeginTransfer(1, kCyclesToACKSent, false, mbActiveCommandHighSpeed, mbActiveCommandHighSpeed);
			mActiveCommand = 0;
			return;
		}

		// validate PERCOM data
		bool valid = true;
		uint16 sectorSize = VDReadUnalignedBEU16(&mReceivePacket[6]);
		uint32 sectorCount = mReceivePacket[0] * (sint32)VDReadUnalignedBEU16(&mReceivePacket[2]) * (mReceivePacket[4] + 1);

		if (mReceivePacket[0] == 0) {
			g_ATLCDisk("Invalid PERCOM data: tracks per sector = 0\n");
			valid = false;
		} else if (mReceivePacket[1] == 0 && mReceivePacket[2] == 0) {
			g_ATLCDisk("Invalid PERCOM data: sectors per track = 0\n");
			valid = false;
		} else if (mReceivePacket[4] >= 2) {
			g_ATLCDisk("Invalid PERCOM data: invalid sides encoded value %02x\n", mReceivePacket[4]);
			valid = false;
		} else if (sectorCount > 65535) {
			g_ATLCDisk("Invalid PERCOM data: total sectors > 65535\n");
			valid = false;
		} else if (sectorSize != 128 && sectorSize != 256 && sectorSize != 512) {
			g_ATLCDisk("Invalid PERCOM data: unsupported sector size (%u)\n", sectorSize);
			valid = false;
		}

		if (!valid) {
			mSendPacket[0] = 'E';
			BeginTransfer(1, kCyclesToACKSent, false, mbActiveCommandHighSpeed, mbActiveCommandHighSpeed);
			mActiveCommand = 0;
			return;
		}

		memcpy(mPERCOM, mReceivePacket, 12);

		g_ATLCDisk("Setting PERCOM data: %u sectors of %u bytes each, %u boot sectors\n", sectorCount, sectorSize, sectorSize > 256 ? 0 : 3);

		mSendPacket[0] = 'A';
		BeginTransfer(1, kCyclesToACKSent, false, mbActiveCommandHighSpeed, mbActiveCommandHighSpeed);
	}
}

void ATDiskEmulator::PokeyBeginCommand() {
//	ATConsoleTaggedPrintf("DISK: Beginning command.\n");
	mbCommandMode = true;
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
	if (!mbEnabled)
		return;

	if (mbWriteMode && mTransferOffset > 2 && mpTransferEvent && mpScheduler->GetTicksToEvent(mpTransferEvent) > 50) {
		mpScheduler->RemoveEvent(mpTransferEvent);
		mpTransferEvent = mpScheduler->AddEvent(50, this, kATDiskEventTransferByte);
	}
}

void ATDiskEmulator::ComputeSectorsPerTrack() {
	mSectorsPerTrack = mSectorSize >= 256 ? 18 : mTotalSectorCount > 720 && !(mTotalSectorCount % 26) ? 26 : 18;
}

void ATDiskEmulator::ComputePERCOMBlock() {
	// track count
	mPERCOM[0] = 1;

	// sectors per track
	mPERCOM[2] = (uint8)(mTotalSectorCount >> 8);
	mPERCOM[3] = (uint8)(mTotalSectorCount);

	// handle special cases of actual disks
	bool mfm = mSectorSize > 128;
	uint8 sides = 0;

	if (mBootSectorCount > 0) {
		if (mSectorSize == 128) {
			switch(mTotalSectorCount) {
				case 720:
				case 1440:
				case 2880:
					// track count
					mPERCOM[0] = (uint8)((mTotalSectorCount + 17) / 18);

					// sectors per track
					mPERCOM[2] = 0;
					mPERCOM[3] = 18;

					sides = 1;
					if (mTotalSectorCount > 720) {
						sides = 2;
						mPERCOM[0] >>= 1;
					}
					break;

				case 1040:
					// track count
					mPERCOM[0] = (uint8)((mTotalSectorCount + 25) / 26);

					// sectors per track
					mPERCOM[2] = 0;
					mPERCOM[3] = 26;

					mfm = true;
					sides = 1;
					break;
			}
		} else if (mSectorSize == 256) {
			switch(mTotalSectorCount) {
				case 720:
				case 1440:
				case 2880:
					// track count
					mPERCOM[0] = (uint8)((mTotalSectorCount + 17) / 18);

					// sectors per track
					mPERCOM[2] = 0;
					mPERCOM[3] = 18;

					sides = 1;
					if (mTotalSectorCount > 720) {
						sides = 2;
						mPERCOM[0] >>= 1;
					}
					break;
			}
		}
	}

	// step rate
	mPERCOM[1] = 0x01;

	// sides minus one
	mPERCOM[4] = sides ? sides - 1 : 0;

	// record method
	mPERCOM[5] = mfm ? 4 : 0;

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
