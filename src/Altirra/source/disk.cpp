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
#include "disk.h"
#include "pokey.h"
#include "console.h"
#include "cpu.h"
#include "simulator.h"

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
	static const int kCyclesToProcessRequest = 1000;

	///////////////////////////////////////////////////////////////////////////////////
	// SIO timing parameters
	//
	// WARNING: KARATEKA IS VERY SENSITIVE TO THESE PARAMETERS AS IT HAS STUPIDLY
	//			CLOSE PHANTOM SECTORS.
	//

	// The number of cycles per byte sent over the SIO bus -- approximately
	// 19200 baud.
	static const int kCyclesPerSIOByte = 960;		// was 939, but 960 is closer to actual 810 speed

	// Delay from end of request to end of ACK byte.
	static const int kCyclesToACKSent = kCyclesPerSIOByte + 1000;

	// Delay from end of ACK byte until FDC command is sent.
	static const int kCyclesToFDCCommand = kCyclesToACKSent + 4000;

	// Delay from end of ACK byte to end of first data byte, not counting rotational delay.
	static const int kCyclesToFirstData = kCyclesToFDCCommand + 10000;

	static const int kTrackInterleave18[18]={
		17, 8, 16, 7, 15, 6, 14, 5, 13, 4, 12, 3, 11, 2, 10, 1, 9, 0
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
	mUnit = unit;
	mpActivity = act;
	mpScheduler = sched;
	mbEnabled = false;
	mbAccurateSectorTiming = false;
	mbAccurateSectorPrediction = false;
	mbBurstTransfer = false;
	mSectorBreakpoint = -1;
	mpRotationalEvent = mpScheduler->AddEvent(kCyclesPerDiskRotation, this, kATDiskEventRotation);
	mbWriteEnabled = false;
	mbDirty = false;
	mbErrorIndicatorPhase = false;
}

void ATDiskEmulator::SetBurstIOEnabled(bool burst) {
	mbBurstTransfer = burst;
}

bool ATDiskEmulator::IsDirty() const {
	return mbDirty;
}

void ATDiskEmulator::SetWriteFlushMode(bool writeEnabled, bool autoFlush) {
	mbWriteEnabled = writeEnabled;
	mbAutoFlush = autoFlush;

	if (writeEnabled && autoFlush) {
		if (mbDirty)
			QueueAutoSave();
	} else {
		if (mpAutoSaveEvent) {
			mpScheduler->RemoveEvent(mpAutoSaveEvent);
			mpAutoSaveEvent = NULL;
		}
	}
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

	for(VirtSectors::iterator it(mVirtSectorInfo.begin()), itEnd(mVirtSectorInfo.end()); it!=itEnd; ++it) {
		VirtSectorInfo& vsi = *it;

		vsi.mPhantomSectorCounter = 0;
	}
}

void ATDiskEmulator::LoadDisk(const wchar_t *s) {
	UnloadDisk();

	try {
		VDFile f(s);

		sint64 fileSize = f.size();

		if (!(fileSize & 127) && fileSize <= 65535 * 128 && !_wcsicmp(VDFileSplitExt(s), L".xfd")) {
			sint32 len = (sint32)fileSize;

			mDiskImage.resize(len);
			f.read(mDiskImage.data(), len);

			mBootSectorCount = 3;
			mSectorSize = 128;
			mTotalSectorCount = len >> 7;

			mPhysSectorInfo.resize(mTotalSectorCount);
			mVirtSectorInfo.resize(mTotalSectorCount);

			for(int i=0; i<mTotalSectorCount; ++i) {
				PhysSectorInfo& psi = mPhysSectorInfo[i];
				VirtSectorInfo& vsi = mVirtSectorInfo[i];

				vsi.mStartPhysSector = i;
				vsi.mNumPhysSectors = 1;
				vsi.mPhantomSectorCounter = 0;

				psi.mOffset		= 128*i;
				psi.mSize		= 128;
				psi.mFDCStatus	= 0xFF;
				psi.mRotPos		= (float)kTrackInterleave18[i % 18] / 18.0f;
			}
		} else {
			sint32 len = VDClampToSint32(f.size()) - 16;
			
			uint8 header[16];

			mDiskImage.resize(len);
			f.read(header, 16);
			f.read(mDiskImage.data(), len);

			mbAccurateSectorPrediction = false;

			if (header[0] == 'A' && header[1] == 'T' && header[2] == '8' && header[3] == 'X') {
				struct ATXHeader {
					uint8	mSignature[4];
					uint16	mVersionMajor;
					uint16	mVersionMinor;
					uint16	mFlags;
					uint16	mMysteryDataCount;
					uint8	mFill1[16];
					uint32	mTrackDataOffset;
					uint32	mMysteryDataOffset;
					uint8	mFill2[12];
				};

				struct ATXTrackHeader {
					uint16	mBlockLength;
					uint8	mFill1[6];
					uint16	mTrackNum;
					uint16	mNumSectors;
					uint8	mFill2[5];
					uint8	mMysteryIndex;
					uint8	mFill3[2];
					uint8	mUnknown1;
					uint8	mFill4[11];
					uint32	mUnknown2;
					uint32	mUnknown3;
				};

				struct ATXSectorHeader {
					uint8	mIndex;
					uint8	mFDCStatus;		// not inverted
					uint16	mTimingOffset;
					uint16	mDataOffset;
					uint8	mFill1[2];
				};

				struct ATXSectorExtraData {
					uint32	mUnknown1;
					uint8	mUnknown2;
					uint8	mUnknown3;
					uint8	mUnknown4;
					uint8	mFill;
				};

				struct ATXTrackFooter {
					uint8	mUnknown1;
					uint8	mUnknown2;
					uint8	mFill1[6];
				};

				ATXHeader atxhdr;
				f.seek(0);
				f.read(&atxhdr, sizeof atxhdr);

				f.seek(atxhdr.mTrackDataOffset);

				mDiskImage.clear();
				mBootSectorCount = 3;
				mSectorSize = 128;
				mTotalSectorCount = 720;

				vdfastvector<ATXSectorHeader> sectorHeaders;
				for(uint32 i=0; i<40; ++i) {
					ATXTrackHeader trkhdr;
					sint64 baseAddr = f.tell();

					ATConsolePrintf("track %d at %04x\n", i, (uint32)baseAddr);
					f.read(&trkhdr, sizeof trkhdr);

					// read sectors
					sectorHeaders.resize(trkhdr.mNumSectors);
					f.read(sectorHeaders.data(), sizeof(sectorHeaders[0])*trkhdr.mNumSectors);

					uint32 secDataOffset = mDiskImage.size();
					uint32 sectorsWithExtraData = 0;
					for(uint32 j=0; j<18; ++j) {
						mVirtSectorInfo.push_back();
						VirtSectorInfo& vsi = mVirtSectorInfo.back();

						vsi.mStartPhysSector = mPhysSectorInfo.size();
						vsi.mNumPhysSectors = 0;
						vsi.mPhantomSectorCounter = 0;

						for(uint32 k=0; k<trkhdr.mNumSectors; ++k) {
							const ATXSectorHeader& sechdr = sectorHeaders[k];

							if (sechdr.mIndex != j + 1)
								continue;

							if (sechdr.mFDCStatus & 0x40)
								++sectorsWithExtraData;

							mPhysSectorInfo.push_back();
							PhysSectorInfo& psi = mPhysSectorInfo.back();

							psi.mFDCStatus = ~sechdr.mFDCStatus;
							psi.mOffset = secDataOffset + 128*k;
							psi.mSize = 128;
							psi.mRotPos = (float)sechdr.mTimingOffset / (float)kBytesPerTrack;
	//						psi.mRotPos = (float)k / (float)trkhdr.mNumSectors;
							++vsi.mNumPhysSectors;

							ATConsolePrintf("trk %d sector %d (vsec=%d) (%d): %02x %02x %04x %04x %02x %02x\n", i, k, i*18 + sechdr.mIndex, sechdr.mIndex
								, sechdr.mIndex
								, sechdr.mFDCStatus
								, sechdr.mTimingOffset
								, sechdr.mDataOffset
								, sechdr.mFill1[0]
								, sechdr.mFill1[1]
							);
						}
					}

					// read footer
					ATXTrackFooter trkftr;
					f.read(&trkftr, sizeof trkftr);

					// read sector data
					size_t offset = mDiskImage.size();
					mDiskImage.resize(offset + 128*trkhdr.mNumSectors);
					f.read(mDiskImage.data() + offset, 128*trkhdr.mNumSectors);

					// read extra sector data
					ATXSectorExtraData extraData;
					for(uint32 j=0; j<sectorsWithExtraData; ++j) {
						f.read(&extraData, sizeof extraData);
						ATConsolePrintf("    extra sector data %08X %02X %02X %02X %02X\n", extraData.mUnknown1, extraData.mUnknown2, extraData.mUnknown3, extraData.mUnknown4, extraData.mFill);
					}

					f.seek(baseAddr + trkhdr.mBlockLength);
				}

				mbAccurateSectorPrediction = true;
			} else if (header[2] == 'P' && header[3] == '2') {
				mBootSectorCount = 3;
				mSectorSize = 128;

				mTotalSectorCount = VDReadUnalignedBEU16(&header[0]);
				ATConsolePrintf("total sector count: %d\n", mTotalSectorCount);

				// read sector headers
				for(int i=0; i<mTotalSectorCount; ++i) {
					const uint8 *sectorhdr = &mDiskImage[(128+12) * i];

					ATConsolePrintf("sector(%d) header: %02x %02x %02x %02x %02x %02x %02x %02x\n", i+1, sectorhdr[0], sectorhdr[1], sectorhdr[2], sectorhdr[3], sectorhdr[4], sectorhdr[5], sectorhdr[6], sectorhdr[7]);

					mPhysSectorInfo.push_back();
					PhysSectorInfo& psi = mPhysSectorInfo.back();

					psi.mOffset		= (128+12)*i+12;
					psi.mSize		= 128;
					psi.mFDCStatus	= sectorhdr[1];
					psi.mRotPos		= (float)kTrackInterleave18[i % 18] / 18.0f;

					if (!(psi.mFDCStatus & 0x10)) {
						psi.mSize = 0;
					}

					mVirtSectorInfo.push_back();
					VirtSectorInfo& vsi = mVirtSectorInfo.back();

					vsi.mStartPhysSector = mPhysSectorInfo.size() - 1;
					vsi.mNumPhysSectors = 1;
					vsi.mPhantomSectorCounter = 0;

					uint16 phantomSectorCount = sectorhdr[5];
					if (phantomSectorCount) {
						vsi.mNumPhysSectors = phantomSectorCount + 1;

						mTotalSectorCount -= phantomSectorCount;

						for(uint32 j=0; j<phantomSectorCount; ++j) {
							uint32 phantomSectorOffset = mTotalSectorCount + sectorhdr[7 + j] - 1;
							uint32 phantomSectorByteOffset = (128+12) * phantomSectorOffset;
							if (phantomSectorByteOffset + 128 > mDiskImage.size())
								throw MyError("Invalid protected disk.");
							const uint8 *sectorhdr2 = &mDiskImage[phantomSectorByteOffset];

							mPhysSectorInfo.push_back();
							PhysSectorInfo& psi = mPhysSectorInfo.back();

							psi.mOffset		= phantomSectorByteOffset+12;
							psi.mSize		= 128;
							psi.mFDCStatus	= sectorhdr2[1];
							psi.mRotPos		= (float)kTrackInterleave18[i % 18] / 18.0f + (1.0f / ((float)phantomSectorCount + 1)) * (j+1);

							if (!(psi.mFDCStatus & 0x10)) {
								psi.mSize = 0;
							}
						}
					}
				}
			} else if (header[2] == 'P' && header[3] == '3') {
				mBootSectorCount = 3;
				mSectorSize = 128;

				mTotalSectorCount = VDReadUnalignedBEU16(&header[6]);

				// read sector headers
				for(int i=0; i<mTotalSectorCount; ++i) {
					const uint8 *sectorhdr = &mDiskImage[(128+12) * i];
					uint32 phantomSectorCount = sectorhdr[5];

					ATConsolePrintf("sector(%d) header: %02x %02x %02x %02x %02x | %02x: %02x %02x %02x %02x %02x %02x\n"
						, i+1
						, sectorhdr[0]
						, sectorhdr[1]
						, sectorhdr[2]
						, sectorhdr[3]
						, sectorhdr[4]
						, sectorhdr[5]
						, sectorhdr[6]
						, sectorhdr[7]
						, sectorhdr[8]
						, sectorhdr[9]
						, sectorhdr[10]
						, sectorhdr[12]
						);

					float rotationalPosition = (float)kTrackInterleave18[i % 18] / 18.0f; 
					float rotationalIncrement = phantomSectorCount ? (1.0f / (int)phantomSectorCount) : 0.0f;

					mVirtSectorInfo.push_back();
					VirtSectorInfo& vsi = mVirtSectorInfo.back();

					vsi.mStartPhysSector = mPhysSectorInfo.size();
					vsi.mNumPhysSectors = phantomSectorCount + 1;
					vsi.mPhantomSectorCounter = 0;

					for(uint32 j=0; j<=phantomSectorCount; ++j) {
						uint8 idx = sectorhdr[6 + j];

						uint32 phantomSectorOffset = idx ? mTotalSectorCount + idx - 1 : i;
						uint32 phantomSectorByteOffset = (128+12) * phantomSectorOffset;
						if (phantomSectorByteOffset + 128 > mDiskImage.size())
							throw MyError("Invalid protected disk.");
						const uint8 *sectorhdr2 = &mDiskImage[phantomSectorByteOffset];

						mPhysSectorInfo.push_back();
						PhysSectorInfo& psi = mPhysSectorInfo.back();

						psi.mOffset		= phantomSectorByteOffset+12;
						psi.mSize		= 128;
						psi.mFDCStatus	= sectorhdr2[1];
						psi.mRotPos		= rotationalPosition;

						if (!(psi.mFDCStatus & 0x10)) {
							psi.mSize = 0;
						}

						rotationalPosition += rotationalIncrement;
					}
				}
			} else {
				mBootSectorCount = mDiskImage[1];
				mSectorSize = header[4] + 256*header[5];
				mTotalSectorCount = (len - 128*mBootSectorCount) / mSectorSize + mBootSectorCount;

				mPhysSectorInfo.resize(mTotalSectorCount);
				mVirtSectorInfo.resize(mTotalSectorCount);

				for(int i=0; i<mTotalSectorCount; ++i) {
					PhysSectorInfo& psi = mPhysSectorInfo[i];
					VirtSectorInfo& vsi = mVirtSectorInfo[i];

					vsi.mStartPhysSector = i;
					vsi.mNumPhysSectors = 1;
					vsi.mPhantomSectorCounter = 0;

					psi.mOffset		= i < mBootSectorCount ? 128*i : 128*mBootSectorCount + mSectorSize*(i-mBootSectorCount);
					psi.mSize		= i < mBootSectorCount ? 128 : + mSectorSize;
					psi.mFDCStatus	= 0xFF;
					psi.mRotPos		= mSectorSize >= 256 ? (float)kTrackInterleaveDD[i % 18] / 18.0f : (float)kTrackInterleave18[i % 18] / 18.0f;
				}
			}
		}

		mPath = s;
	} catch(const MyError&) {
		UnloadDisk();
		throw;
	}

	mCurrentTrack = 0;
	mSectorsPerTrack = mSectorSize >= 256 ? 26 : 18;
	mbEnabled = true;
	mbWriteEnabled = false;
	mbAutoFlush = false;
	mbDirty = false;
}

void ATDiskEmulator::SaveDisk(const wchar_t *s) {
	// scan for virtual sectors with errors or phantoms -- we can't handle that.
	for(int i=0; i<mTotalSectorCount; ++i) {
		const VirtSectorInfo& vsi = mVirtSectorInfo[i];

		if (vsi.mNumPhysSectors != 1)
			throw MyError("Cannot save a disk which contains phantom or missing sectors.");
	}

	uint32 physCount = mPhysSectorInfo.size();
	for(uint32 i=0; i<physCount; ++i) {
		const PhysSectorInfo& psi = mPhysSectorInfo[i];

		if (psi.mFDCStatus != 0xFF)
			throw MyError("Cannot save a disk which contains deliberate sector errors.");
	}

	// create ATR header
	uint8 header[16] = {0};
	uint32 paras = mDiskImage.size() >> 4;
	VDWriteUnalignedLEU16(header+0, 0x0296);
	VDWriteUnalignedLEU16(header+2, (uint16)paras);
	VDWriteUnalignedLEU16(header+4, mSectorSize);
	header[6] = (uint8)(paras >> 16);

	VDFile f(s, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	f.write(header, 16);

	for(int i=0; i<mTotalSectorCount; ++i) {
		const VirtSectorInfo& vsi = mVirtSectorInfo[i];
		const PhysSectorInfo& psi = mPhysSectorInfo[vsi.mStartPhysSector];

		f.write(&mDiskImage[psi.mOffset], psi.mSize);
	}

	mPath = s;
	mbDirty = false;
}

void ATDiskEmulator::CreateDisk(uint32 sectorCount, uint32 bootSectorCount, bool doubleDensity) {
	UnloadDisk();

	mBootSectorCount = bootSectorCount;
	mSectorSize = doubleDensity ? 256 : 128;
	mTotalSectorCount = sectorCount;

	mDiskImage.clear();
	mDiskImage.resize(128 * bootSectorCount + 256 * (sectorCount - bootSectorCount), 0);

	mPhysSectorInfo.resize(mTotalSectorCount);
	mVirtSectorInfo.resize(mTotalSectorCount);

	for(int i=0; i<mTotalSectorCount; ++i) {
		PhysSectorInfo& psi = mPhysSectorInfo[i];
		VirtSectorInfo& vsi = mVirtSectorInfo[i];

		vsi.mStartPhysSector = i;
		vsi.mNumPhysSectors = 1;
		vsi.mPhantomSectorCounter = 0;

		psi.mOffset		= i < mBootSectorCount ? 128*i : 128*mBootSectorCount + mSectorSize*(i-mBootSectorCount);
		psi.mSize		= i < mBootSectorCount ? 128 : + mSectorSize;
		psi.mFDCStatus	= 0xFF;
		psi.mRotPos		= mSectorSize >= 256 ? (float)kTrackInterleaveDD[i % 18] / 18.0f : (float)kTrackInterleave18[i % 18] / 18.0f;
	}

	mPath = L"(New disk)";
	mbWriteEnabled = false;
	mbEnabled = true;
}

void ATDiskEmulator::UnloadDisk() {
	mBootSectorCount = 0;
	mSectorSize = 128;
	mTotalSectorCount = 0;
	mSectorsPerTrack = 1;
	mDiskImage.clear();
	mPhysSectorInfo.clear();
	mVirtSectorInfo.clear();
	mPath.clear();

	if (mpAutoSaveEvent) {
		mpScheduler->RemoveEvent(mpAutoSaveEvent);
		mpAutoSaveEvent = NULL;
	}

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

			uint32 preRotPos = mRotationalCounter + kCyclesPerSIOByte * 5 + kCyclesToProcessRequest;
			mRotationalCounter = preRotPos % kCyclesPerDiskRotation;
			mRotations += preRotPos / kCyclesPerDiskRotation;

			ProcessCommandPacket();

			// fake rotation
			uint32 rotPos = mRotationalPosition + kCyclesToFirstData + kCyclesPerSIOByte * (mTransferLength - 2);

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
		mpActivity->OnDiskActivity(mUnit + 1, false);

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
	const VirtSectorInfo& vsi = mVirtSectorInfo[sector - 1];
	if (!vsi.mNumPhysSectors) {
		// report device error
		return 0x90;
	}

	const PhysSectorInfo& psi = mPhysSectorInfo[vsi.mStartPhysSector];

	if (len > psi.mSize) {
		// expected size is too long -- report timeout
		return 0x8A;
	}

	if (len < psi.mSize) {
		// expected size is too short -- report checksum error
		return 0x8F;
	}

	// commit memory to disk
	uint8 *dst = &mDiskImage[psi.mOffset];
	for(uint32 i=0; i<len; ++i)
		dst[i] = mpMem->ReadByte(bufadr+i);

	mbDirty = true;

	QueueAutoSave();

	// return success
	return 0x01;
}

void ATDiskEmulator::OnScheduledEvent(uint32 id) {
	if (id == kATDiskEventTransferByte) {
		mpTransferEvent = NULL;

		if (!mbWriteMode)
			return;

		if (mTransferOffset >= mTransferLength)
			return;

		mpPokey->ReceiveSIOByte(mSendPacket[mTransferOffset++]);

		// SIO barfs if the third byte is sent too quickly
		uint32 transferDelay = kCyclesPerSIOByte;
		if (mTransferOffset == 1) {
			transferDelay = kCyclesToFirstData;

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

		if (mTransferOffset >= mTransferLength) {
			UpdateRotationalCounter();

			mTransferOffset = 0;
			mTransferLength = 0;
			mbWriteMode = false;
			ATConsolePrintf("DISK: Disk transmit finished. (rot=%.2f)\n", (float)mRotations + (float)mRotationalCounter / (float)kCyclesPerDiskRotation);
			mpActivity->OnDiskActivity(mUnit + 1, false);

			if (mActiveCommand)
				ProcessCommandTransmitCompleted();
		} else {
			mpTransferEvent = mpScheduler->AddEvent(transferDelay, this, kATDiskEventTransferByte);
		}
	} else if (id == kATDiskEventRotation) {
		mpRotationalEvent = NULL;
		UpdateRotationalCounter();
	} else if (id == kATDiskEventWriteCompleted) {
		mpOperationEvent = NULL;
		mSendPacket[0] = 'C';
		BeginTransfer(1, kCyclesToACKSent);
	} else if (id == kATDiskEventFormatCompleted) {
		mpOperationEvent = NULL;
		mSendPacket[0] = 'C';
		memset(mSendPacket+1, 0xFF, 128);
		mSendPacket[129] = Checksum(mSendPacket+1, 128);
		BeginTransfer(130, kCyclesToACKSent);
	} else if (id == kATDiskEventAutoSave) {
		mpAutoSaveEvent = NULL;
		try {
			SaveDisk(mPath.c_str());
			SetAutoSaveError(false);
		} catch(const MyError&) {
			mbAutoFlush = false;
			SetAutoSaveError(true);
		}
	} else if (id == kATDiskEventAutoSaveError) {
		mpActivity->OnDiskActivity(mUnit + 1, mbErrorIndicatorPhase);
		mbErrorIndicatorPhase = !mbErrorIndicatorPhase;

		mpAutoSaveErrorEvent = mpScheduler->AddEvent(894886, this, kATDiskEventAutoSaveError);
	}
}

void ATDiskEmulator::PokeyAttachDevice(ATPokeyEmulator *pokey) {
	mpPokey = pokey;
}

void ATDiskEmulator::PokeyWriteSIO(uint8 c) {
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

void ATDiskEmulator::BeginTransfer(uint32 length, uint32 cyclesToFirstByte) {
	mbWriteMode = true;
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

void ATDiskEmulator::SetAutoSaveError(bool error) {
	if (error) {
		if (!mpAutoSaveErrorEvent)
			mpAutoSaveErrorEvent = mpScheduler->AddEvent(1000, this, kATDiskEventAutoSaveError);
	} else {
		mpActivity->OnDiskActivity(mUnit + 1, false);

		if (mpAutoSaveErrorEvent) {
			mpScheduler->RemoveEvent(mpAutoSaveErrorEvent);
			mpAutoSaveErrorEvent = NULL;
		}
	}
}

void ATDiskEmulator::ProcessCommandPacket() {
	mpActivity->OnDiskActivity(mUnit + 1, true);

	UpdateRotationalCounter();

	// interpret the command
//		ATConsoleTaggedPrintf("DISK: Processing command %02X\n", mReceivePacket[1]);
	switch(mReceivePacket[1]) {
		case 0x53:	// status
			mSendPacket[0] = 0x41;
			mSendPacket[1] = 0x43;
			mSendPacket[2] = (mSectorSize > 128 ? 0x30 : 0x10) + (mbLastOpError ? 0x04 : 0x00) + (mbWriteEnabled ? 0x00 : 0x08);
			mSendPacket[3] = mTotalSectorCount > 0 ? mFDCStatus : 0x7F;
			mSendPacket[4] = 0xe0;
			mSendPacket[5] = 0x00;
			mSendPacket[6] = Checksum(mSendPacket+2, 4);
			BeginTransfer(7, 2500);
			break;

		case 0x52:	// read
			{
				uint32 sector = mReceivePacket[2] + mReceivePacket[3] * 256;

				if (sector == mSectorBreakpoint)
					g_sim.PostInterruptingEvent(kATSimEvent_DiskSectorBreakpoint);

				mSendPacket[0] = 0x41;		// ACK
				if (!sector || sector > (uint32)mTotalSectorCount) {
					// NAK the command immediately. We used to send ACK (41) + ERROR (45), but the kernel
					// is stupid and waits for data anyway. The Linux ATARISIO kernel module does this
					// too.
					mSendPacket[0] = 0x4E;		// error

					mbLastOpError = true;
					BeginTransfer(1, kCyclesToACKSent);
					ATConsolePrintf("DISK: Error reading sector %d.\n", sector);
					break;
				}

				// get virtual sector information
				VirtSectorInfo& vsi = mVirtSectorInfo[sector - 1];

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
						BeginTransfer(131, kCyclesToACKSent);

						ATConsolePrintf("DISK: Reporting missing sector %d.\n", sector);
						break;
					}

					UpdateRotationalCounter();
					uint32 postSeekPosition = (mRotationalCounter + kCyclesToFDCCommand + (tracksToStep ? tracksToStep * kCyclesPerTrackStep + kCyclesForHeadSettle : 0)) % kCyclesPerDiskRotation;
					uint32 bestDelay = 0xFFFFFFFFU;
					uint8 bestStatus = 0;

					physSector = vsi.mStartPhysSector;

					for(uint32 i=0; i<vsi.mNumPhysSectors; ++i) {
						const PhysSectorInfo& psi = mPhysSectorInfo[vsi.mStartPhysSector + i];
						uint32 time = VDRoundToInt(psi.mRotPos * kCyclesPerDiskRotation);
						uint32 delay = time < mRotationalCounter ? time + kCyclesPerDiskRotation - mRotationalCounter : time - mRotationalCounter;
						uint8 status = mPhysSectorInfo[vsi.mStartPhysSector + i].mFDCStatus;

						if ((delay < bestDelay && (psi.mFDCStatus == 0xff || bestStatus != 0xff)) || (bestStatus != 0xFF && psi.mFDCStatus == 0xFF)) {
							bestDelay = delay;
							bestStatus = psi.mFDCStatus;
							physSector = vsi.mStartPhysSector + i;
							mPhantomSectorCounter = i;
						}
					}
				} else {
					physSector = vsi.mStartPhysSector + vsi.mPhantomSectorCounter;

					if (++vsi.mPhantomSectorCounter >= vsi.mNumPhysSectors)
						vsi.mPhantomSectorCounter = 0;
				}

				const PhysSectorInfo& psi = mPhysSectorInfo[physSector];

				// set FDC status
				mFDCStatus = psi.mFDCStatus;

				// set rotational delay
				mRotationalPosition = VDRoundToInt(psi.mRotPos * kCyclesPerDiskRotation);

				// check for missing sector
				// note: must send ACK (41) + ERROR (45) -- BeachHead expects to get DERROR from SIO
				if (!psi.mSize) {
					mSendPacket[0] = 0x41;
					mSendPacket[1] = 0x45;
//					memset(mSendPacket+2, 0, 128);
					mSendPacket[128+2] = Checksum(mSendPacket + 2, 128);
					BeginTransfer(131, kCyclesToACKSent);
					mbLastOpError = true;

					ATConsolePrintf("DISK: Reporting missing sector %d.\n", sector);
					break;
				}

				mSendPacket[1] = 0x43;		// complete

				mbLastOpError = (mFDCStatus != 0xFF);

				// check for CRC error
				// must return data on CRC error -- Koronis Rift requires this
				if (~mFDCStatus & 0x28)
					mSendPacket[1] = 0x45;	// error

				memcpy(mSendPacket+2, mDiskImage.data() + psi.mOffset, psi.mSize);

				mSendPacket[psi.mSize+2] = Checksum(mSendPacket + 2, psi.mSize);
				const uint32 transferLength = psi.mSize + 3;

				BeginTransfer(transferLength, kCyclesToACKSent);
				ATConsolePrintf("DISK: Reading vsec=%3d (%d/%d), psec=%3d, chk=%02x, rot=%.2f >> %.2f%s.\n"
						, sector
						, physSector - vsi.mStartPhysSector + 1
						, vsi.mNumPhysSectors
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
				mSendPacket[0] = 'N';
				mbLastOpError = true;
				BeginTransfer(1, kCyclesToACKSent);
			} else {
				mSendPacket[0] = 'A';
				mbLastOpError = false;
				BeginTransfer(1, kCyclesToACKSent);

				if (mpOperationEvent)
					mpScheduler->RemoveEvent(mpOperationEvent);
				mpOperationEvent = mpScheduler->AddEvent(1000000, this, kATDiskEventFormatCompleted);
			}
			break;

		case 0x50:	// put (without verify)
		case 0x57:	// write (with verify)
			{
				uint32 sector = mReceivePacket[2] + mReceivePacket[3] * 256;

				if (sector == mSectorBreakpoint)
					g_sim.PostInterruptingEvent(kATSimEvent_DiskSectorBreakpoint);

				mSendPacket[0] = 0x41;		// ACK
				if (!sector || sector > (uint32)mTotalSectorCount || !mbWriteEnabled) {
					// NAK the command immediately. We used to send ACK (41) + ERROR (45), but the kernel
					// is stupid and waits for data anyway. The Linux ATARISIO kernel module does this
					// too.
					mSendPacket[0] = 0x4E;		// error

					mbLastOpError = true;
					BeginTransfer(1, kCyclesToACKSent);
					ATConsolePrintf("DISK: Error writing sector %d.\n", sector);
					break;
				}

				mbLastOpError = false;
				BeginTransfer(1, kCyclesToACKSent);

				// get virtual sector information
				VirtSectorInfo& vsi = mVirtSectorInfo[sector - 1];

				mActiveCommand = 'W';
				mActiveCommandState = 0;
				mActiveCommandPhysSector = vsi.mStartPhysSector;
				break;
			}
			break;

		default:
			mSendPacket[0] = 0x4E;
			BeginTransfer(1, kCyclesToACKSent);
			ATConsolePrintf("DISK: Unsupported command %02X\n", mReceivePacket[1]);
			break;

	}

	mpActivity->OnDiskActivity(mUnit + 1, mbWriteMode);
}

void ATDiskEmulator::ProcessCommandTransmitCompleted() {
	if (mActiveCommand == 'W') {
		const PhysSectorInfo& psi = mPhysSectorInfo[mActiveCommandPhysSector];
		if (mActiveCommandState == 0) {
			// wait for remaining data
			mTransferOffset = 0;
			mTransferLength = psi.mSize + 1;

			ATConsolePrintf("DISK: Sent ACK, now waiting for write data.\n", mActiveCommandPhysSector);
			mActiveCommandState = 1;
		} else {
			// commit data to physical sector
			memcpy(&mDiskImage[psi.mOffset], mReceivePacket, psi.mSize);

			ATConsolePrintf("DISK: Writing psec=%3d.\n", mActiveCommandPhysSector);

			// set FDC status
			mFDCStatus = 0xFF;
			mbLastOpError = false;

			// compute rotational delay
			UpdateRotationalCounter();
			uint32 rotPos = VDRoundToInt(psi.mRotPos * kCyclesPerDiskRotation);

			uint32 rotDelay = rotPos < mRotationalCounter ? (rotPos - mRotationalCounter) + kCyclesPerDiskRotation : (rotPos - mRotationalCounter);

			rotDelay += 10000;	// fudge factor

			mActiveCommand = 0;
			mbDirty = true;
			QueueAutoSave();

			if (mpOperationEvent)
				mpScheduler->RemoveEvent(mpOperationEvent);
			mpOperationEvent = mpScheduler->AddEvent(rotDelay, this, kATDiskEventWriteCompleted);
		}
	}
}

void ATDiskEmulator::ProcessCommandData() {
	if (mActiveCommand == 'W') {
		const PhysSectorInfo& psi = mPhysSectorInfo[mActiveCommandPhysSector];

		// test checksum
		uint8 chk = Checksum(mReceivePacket, psi.mSize);

		if (chk != mReceivePacket[psi.mSize]) {
			ATConsolePrintf("DISK: Checksum error detected while receiving write data.\n");

			mSendPacket[0] = 'E';
			BeginTransfer(1, kCyclesToACKSent);
			mActiveCommand = 0;
			return;
		}

		mSendPacket[0] = 'A';
		BeginTransfer(1, kCyclesToACKSent);
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

		ProcessCommandPacket();
	}
}

void ATDiskEmulator::PokeySerInReady() {
	if (!mbEnabled)
		return;

	if (mbBurstTransfer && mbWriteMode && mTransferOffset > 2 && mpTransferEvent && mpScheduler->GetTicksToEvent(mpTransferEvent) > 50) {
		mpScheduler->RemoveEvent(mpTransferEvent);
		mpTransferEvent = mpScheduler->AddEvent(50, this, kATDiskEventTransferByte);
	}
}
