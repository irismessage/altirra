//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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
#include <vd2/system/math.h>
#include "ide.h"
#include "console.h"
#include "scheduler.h"
#include "uirender.h"
#include "simulator.h"

extern ATSimulator g_sim;

namespace {
	enum {
		kATIDEStatus_BSY	= 0x80,		// busy
		kATIDEStatus_DRDY	= 0x40,		// drive ready
		kATIDEStatus_DWF	= 0x20,		// drive write fault
		kATIDEStatus_DSC	= 0x10,		// drive seek complete
		kATIDEStatus_DRQ	= 0x08,		// data request
		kATIDEStatus_CORR	= 0x04,		// corrected data
		kATIDEStatus_IDX	= 0x02,		// index
		kATIDEStatus_ERR	= 0x01		// error
	};

	enum {
		kATIDEError_BBK		= 0x80,		// bad block detected
		kATIDEError_UNC		= 0x40,		// uncorrectable data error
		kATIDEError_MC		= 0x20,		// media changed
		kATIDEError_IDNF	= 0x10,		// ID not found
		kATIDEError_MCR		= 0x08,		// media change request
		kATIDEError_ABRT	= 0x04,		// aborted command
		kATIDEError_TK0NF	= 0x02,		// track 0 not found
		kATIDEError_AMNF	= 0x01		// address mark not found
	};

	// These control the amount of time that BSY is asserted during a sector
	// read or write.
	const uint32 kIODelayFast = 100;	// 
	const uint32 kIODelaySlow = 10000;	// ~5.5ms
}

struct ATIDEEmulator::DecodedCHS {
	const char *c_str() const { return buf; }

	char buf[64];
};

ATIDEEmulator::ATIDEEmulator() {
	mTransferLength = 0;
	mTransferIndex = 0;
	mSectorCount = 0;
	mMaxSectorTransferCount = 32;
	mSectorsPerTrack = 0;
	mHeadCount = 0;
	mCylinderCount = 0;
	mIODelaySetting = 0;

	mTransferBuffer.resize(512 * mMaxSectorTransferCount);

	ColdReset();
}

ATIDEEmulator::~ATIDEEmulator() {
}

void ATIDEEmulator::Init(ATScheduler *scheduler, IATUIRenderer *uirenderer) {
	mpScheduler = scheduler;
	mpUIRenderer = uirenderer;
}

void ATIDEEmulator::OpenImage(bool write, bool fast, uint32 cylinders, uint32 heads, uint32 sectors, const wchar_t *filename) {
	CloseImage();

	// validate geometry
	if (!cylinders || !heads || !sectors || cylinders > 65536 || heads > 16 || sectors > 63)
		throw MyError("Invalid IDE geometry: %u cylinders / %u heads / %u sectors", cylinders, heads, sectors);

	try {
		mbWriteEnabled = write;
		mFile.open(filename, write ? nsVDFile::kReadWrite | nsVDFile::kDenyAll | nsVDFile::kOpenAlways : nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting);
		mSectorCount = cylinders * heads * sectors;
		mCylinderCount = cylinders;
		mHeadCount = heads;
		mSectorsPerTrack = sectors;
		mPath = filename;
		mIODelaySetting = fast ? kIODelayFast : kIODelaySlow;
		mbFastDevice = fast;

		ColdReset();
	} catch(const MyError&) {
		CloseImage();
		throw;
	}
}

void ATIDEEmulator::CloseImage() {
	mFile.close();
	mSectorCount = 0;
	mCylinderCount = 0;
	mHeadCount = 0;
	mSectorsPerTrack = 0;
	mPath.clear();

	ColdReset();
}

const wchar_t *ATIDEEmulator::GetImagePath() const {
	return mPath.c_str();
}

void ATIDEEmulator::ColdReset() {
	mActiveCommand = 0;
	mActiveCommandNextTime = 0;
	mActiveCommandState = 0;

	// ATA-1 8.1 Reset Response / ATA-4 9.1 Signature and persistence
	mRFile.mData			= 0x00;
	mRFile.mErrors			= 0x01;
	mRFile.mSectorCount		= 0x01;
	mRFile.mSectorNumber	= 0x01;
	mRFile.mCylinderLow		= 0x00;
	mRFile.mCylinderHigh	= 0x00;
	mRFile.mHead			= 0x00;
	mRFile.mStatus			= kATIDEStatus_DRDY | kATIDEStatus_DSC;

	mFeatures = 0;
	mbTransfer16Bit = true;

	memset(mTransferBuffer.data(), 0, mTransferBuffer.size());
}

uint8 ATIDEEmulator::DebugReadByte(uint8 address) {
	if (address >= 8)
		return 0xFF;

	uint32 idx = address & 7;

	UpdateStatus();

	// ATA-1 7.2.13 - if BSY=1, all reads of the command block return the status register
	if (mRFile.mStatus & kATIDEStatus_BSY)
		return mRFile.mStatus;

	if (idx == 0)
		return mTransferBuffer[mTransferIndex];

	return mRegisters[idx];
}

uint8 ATIDEEmulator::ReadByte(uint8 address) {
	if (address >= 8)
		return 0xFF;

	uint32 idx = address & 7;

	UpdateStatus();

	// ATA-1 7.2.13 - if BSY=1, all reads of the command block return the status register
	if (mRFile.mStatus & kATIDEStatus_BSY)
		return mRFile.mStatus;

	if (idx == 0) {
		uint8 v = mTransferBuffer[mTransferIndex];

		if (!mbTransferAsWrites && mTransferIndex < mTransferLength) {
			++mTransferIndex;

			if (mbTransfer16Bit)
				++mTransferIndex;

			if (mTransferIndex >= mTransferLength)
				CompleteCommand();
		}

		return v;
	}

	return mRegisters[idx];
}

void ATIDEEmulator::WriteByte(uint8 address, uint8 value) {
	if (address >= 8)
		return;

	uint32 idx = address & 7;

	switch(idx) {
		case 0:		// data
			if (mbTransferAsWrites && mTransferIndex < mTransferLength) {
				mTransferBuffer[mTransferIndex] = value;

				++mTransferIndex;

				if (mbTransfer16Bit) {
					mTransferBuffer[mTransferIndex] = 0xFF;
					++mTransferIndex;
				}

				if (mTransferIndex >= mTransferLength) {
					mRFile.mStatus &= ~kATIDEStatus_DRQ;
					mRFile.mStatus |= kATIDEStatus_BSY;

					UpdateStatus();
				}
			}
			break;

		case 1:		// features
			mFeatures = value;
			break;

		case 2:		// sector count
		case 3:		// sector number / LBA 0-7
		case 4:		// cylinder low / LBA 8-15
		case 5:		// cylinder high / LBA 16-23
		case 6:		// drive/head / LBA 24-27
			UpdateStatus();

			if (mRFile.mStatus & kATIDEStatus_BSY) {
				ATConsolePrintf("IDE: Attempted write of $%02x to register file index $%02x while drive is busy.\n", value, idx);
//				g_sim.PostInterruptingEvent(kATSimEvent_VerifierFailure);
			} else {
				// bits 7 and 5 in the drive/head register are always 1
				if (idx == 6)
					value |= 0xa0;

				mRegisters[idx] = value;
			}
			break;

		case 7:		// command
			// ignore drive 1 commands
			if (mRFile.mHead & 0x10)
				return;

			// check if drive is busy
			UpdateStatus();

			if (mRFile.mStatus & kATIDEStatus_BSY) {
				ATConsolePrintf("IDE: Attempt to start command $%02x while drive is busy.\n", value, idx);
			} else {
				StartCommand(value);
			}
			break;
	}
}

void ATIDEEmulator::UpdateStatus() {
	if (!mActiveCommandState)
		return;

	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	if ((sint32)(t - mActiveCommandNextTime) < 0)
		return;

	switch(mActiveCommand) {
		case 0x20:	// read sector(s) w/retry
		case 0x21:	// read sector(s) w/o retry
			switch(mActiveCommandState) {
				case 1:
					mRFile.mStatus |= kATIDEStatus_BSY;
					++mActiveCommandState;

					// BOGUS: FDISK.BAS requires a delay before BSY drops since it needs to see
					// BSY=1. ATA-4 7.15.6.1 BSY (Busy) states that this is not safe as the drive
					// may operate too quickly to spot this.
					mActiveCommandNextTime = t + mIODelaySetting;
					break;

				case 2:
					{
						uint32 lba;
						uint32 nsecs = mRFile.mSectorCount;

						if (!nsecs)
							nsecs = 256;

						if (!ReadLBA(lba)) {
							AbortCommand(0);
							return;
						}

						ATConsolePrintf("IDE: Reading %u sectors starting at LBA %u (CHS %s).\n", nsecs, lba, DecodeCHS(lba).c_str());

						mpUIRenderer->SetIDEActivity(false, lba);

						if (lba >= mSectorCount || mSectorCount - lba < nsecs || nsecs >= mMaxSectorTransferCount) {
							mRFile.mStatus |= kATIDEStatus_ERR;
							CompleteCommand();
						} else {
							try {
								mFile.seek((sint64)lba << 9);

								uint32 requested = nsecs << 9;
								uint32 actual = mFile.readData(mTransferBuffer.data(), requested);

								if (requested < actual)
									memset(mTransferBuffer.data() + actual, 0, requested - actual);
							} catch(const MyError& e) {
								ATConsolePrintf("IDE: I/O ERROR: %s\n", e.gets());
								AbortCommand(kATIDEError_UNC);
								return;
							}

							WriteLBA(lba + nsecs - 1);

							BeginReadTransfer(nsecs << 9);
						}

						mActiveCommandState = 0;
					}
					break;

			}
			break;

		case 0x30:	// write sector(s) w/retry
		case 0x31:	// write sector(s) w/o retry
			switch(mActiveCommandState) {
				case 1:
					mRFile.mStatus |= kATIDEStatus_BSY;
					++mActiveCommandState;
					mActiveCommandNextTime = t + 250;
					break;

				case 2:
					{
						uint32 lba;
						if (!ReadLBA(lba)) {
							AbortCommand(0);
							return;
						}

						uint32 nsecs = mRFile.mSectorCount;

						if (!nsecs)
							nsecs = 256;

						ATConsolePrintf("IDE: Writing %u sectors starting at LBA %u (CHS %s).\n", nsecs, lba, DecodeCHS(lba).c_str());

						if (!mbWriteEnabled) {
							ATConsolePrintf("IDE: Write blocked due to read-only status.\n");
							AbortCommand(0);
						}

						mpUIRenderer->SetIDEActivity(true, lba);

						if (lba >= mSectorCount || mSectorCount - lba < nsecs || nsecs >= mMaxSectorTransferCount) {
							ATConsoleWrite("IDE: Returning error due to invalid command parameters.\n");
							mRFile.mStatus |= kATIDEStatus_ERR;
							CompleteCommand();
						} else {
							// Note that we are actually transferring 256 words, but the Atari only reads
							// the low bytes.
							mTransferLBA = lba;
							mTransferSectorCount = nsecs;
							BeginWriteTransfer(nsecs << 9);
							++mActiveCommandState;
						}
					}
					break;

				case 3:
					if (mTransferIndex < mTransferLength)
						break;

					try {
						mFile.seek((sint64)mTransferLBA << 9);
						mFile.write(mTransferBuffer.data(), 512 * mTransferSectorCount);
					} catch(const MyError& e) {
						ATConsolePrintf("IDE: I/O ERROR: %s\n", e.gets());
						AbortCommand(kATIDEError_UNC);
						return;
					}

					WriteLBA(mTransferLBA + mTransferSectorCount - 1);

					mRFile.mStatus |= kATIDEStatus_BSY;
					++mActiveCommandState;
					mActiveCommandNextTime = t + mIODelaySetting;
					break;

				case 4:
					CompleteCommand();
					break;
			}
			break;

		case 0xec:	// identify drive
			switch(mActiveCommandState) {
				case 1:
					mRFile.mStatus |= kATIDEStatus_BSY;
					++mActiveCommandState;
					mActiveCommandNextTime = t + 10000;
					break;

				case 2:
					{
						uint8 *dst = mTransferBuffer.data();

						// See ATA-1 Table 11 for format.
						memset(dst, 0, 512);
						dst[0] = 0x4c;		// soft sectored, not MFM encoded, fixed drive
						dst[1] = 0x04;		// xfer >10Mbps
						dst[2] = (uint8)mCylinderCount;	// cylinder count
						dst[3] = (uint8)(mCylinderCount >> 8);
						dst[4] = 0;			// reserved
						dst[5] = 0;
						dst[6] = (uint8)mHeadCount;// number of heads
						dst[7] = 0;
						dst[8] = 0;			// unformatted bytes per track
						dst[9] = (uint8)(2 * mSectorsPerTrack);
						dst[10] = 0;		// unformatted bytes per sector
						dst[11] = 2;
						dst[12] = (uint8)mSectorsPerTrack;		// sectors per track
						dst[13] = 0;
						dst[98] = 0;		// capabilities (LBA supported, DMA supported)
						dst[99] = 3;
						dst[102] = 0;		// PIO data transfer timing mode (PIO 2)
						dst[103] = 2;
						dst[104] = 0;		// DMA data transfer timing mode (DMA 2)
						dst[105] = 2;

						BeginReadTransfer(512);
						mActiveCommandState = 0;
					}
					break;

			}
			break;

		case 0xef:	// set features
			switch(mActiveCommandState) {
				case 1:
					mRFile.mStatus |= kATIDEStatus_BSY;
					++mActiveCommandState;
					mActiveCommandNextTime = t + 250;
					break;

				case 2:
					switch(mFeatures) {
						case 0x01:		// enable 8-bit data transfers
							mbTransfer16Bit = false;
							CompleteCommand();
							break;

						case 0x81:		// disable 8-bit data transfers
							mbTransfer16Bit = true;
							CompleteCommand();
							break;

						default:
							AbortCommand(0);
							break;
					}
					break;
			}
			break;

		default:
			AbortCommand(0);
			ATConsolePrintf("IDE: Unrecognized command $%02x.\n", mActiveCommand);
			break;
	}
}

void ATIDEEmulator::StartCommand(uint8 cmd) {
	mRFile.mStatus &= ~kATIDEStatus_ERR;

	// BOGUS: This is unfortunately necessary to get FDISK.BAS to work, but it shouldn't
	// be necessary: ATA-4 7.15.6.6 ERR (Error) states that the ERR register shall
	// be ignored by the host when the ERR bit is 0.
	mRFile.mErrors = 0;

	mActiveCommand = cmd;
	mActiveCommandState = 1;
	mActiveCommandNextTime = ATSCHEDULER_GETTIME(mpScheduler);

	UpdateStatus();
}

void ATIDEEmulator::BeginReadTransfer(uint32 bytes) {
	mRFile.mStatus |= kATIDEStatus_DRQ;
	mRFile.mStatus &= ~kATIDEStatus_BSY;
	mTransferIndex = 0;
	mTransferLength = bytes;
	mbTransferAsWrites = false;
}

void ATIDEEmulator::BeginWriteTransfer(uint32 bytes) {
	mRFile.mStatus |= kATIDEStatus_DRQ;
	mRFile.mStatus &= ~kATIDEStatus_BSY;
	mTransferIndex = 0;
	mTransferLength = bytes;
	mbTransferAsWrites = true;
}

void ATIDEEmulator::CompleteCommand() {
	mRFile.mStatus &= ~kATIDEStatus_BSY;
	mRFile.mStatus &= ~kATIDEStatus_DRQ;
	mActiveCommand = 0;
	mActiveCommandState = 0;
}

void ATIDEEmulator::AbortCommand(uint8 error) {
	mRFile.mStatus &= ~kATIDEStatus_BSY;
	mRFile.mStatus &= ~kATIDEStatus_DRQ;
	mRFile.mStatus |= kATIDEStatus_ERR;
	mRFile.mErrors = error | kATIDEError_ABRT;
	mActiveCommand = 0;
	mActiveCommandState = 0;
}

bool ATIDEEmulator::ReadLBA(uint32& lba) {
	if (mRFile.mHead & 0x40) {
		// LBA mode
		lba = ((uint32)(mRFile.mHead & 0x0f) << 24)
			+ ((uint32)mRFile.mCylinderHigh << 16)
			+ ((uint32)mRFile.mCylinderLow << 8)
			+ (uint32)mRFile.mSectorNumber;

		if (lba >= mSectorCount) {
			ATConsolePrintf("IDE: Invalid LBA %u >= %u\n", lba, mSectorCount);
			return false;
		}

		return true;
	} else {
		// CHS mode
		uint32 head = mRFile.mHead & 15;
		uint32 sector = mRFile.mSectorNumber;
		uint32 cylinder = ((uint32)mRFile.mCylinderHigh << 8) + mRFile.mCylinderLow;

		if (!sector || sector > mSectorsPerTrack) {
			ATConsolePrintf("IDE: Invalid CHS %u/%u/%u (bad sector number)\n", cylinder, head, sector);
			return false;
		}

		lba = (sector - 1) + (cylinder*mHeadCount + head)*mSectorsPerTrack;
		if (lba >= mSectorCount) {
			ATConsolePrintf("IDE: Invalid CHS %u/%u/%u (beyond total sector count of %u)\n", cylinder, head, sector, mSectorCount);
			return false;
		}

		return true;
	}
}

void ATIDEEmulator::WriteLBA(uint32 lba) {
	if (mRFile.mHead & 0x40) {
		// LBA mode
		mRFile.mHead = (mRFile.mHead & 0xf0) + ((lba >> 24) & 0x0f);
		mRFile.mCylinderHigh = (uint8)(lba >> 16);
		mRFile.mCylinderLow = (uint8)(lba >> 8);
		mRFile.mSectorNumber = (uint8)lba;
	} else if (mSectorsPerTrack && mHeadCount) {
		// CHS mode
		uint32 track = lba / mSectorsPerTrack;
		uint32 sector = lba % mSectorsPerTrack;
		uint32 cylinder = track / mHeadCount;
		uint32 head = track % mHeadCount;

		mRFile.mHead = (mRFile.mHead & 0xf0) + head;
		mRFile.mCylinderHigh = (uint8)(cylinder >> 8);
		mRFile.mCylinderLow = (uint8)cylinder;
		mRFile.mSectorNumber = sector + 1;
	} else {
		// uh...

		mRFile.mHead = (mRFile.mHead & 0xf0);
		mRFile.mCylinderHigh = 0;
		mRFile.mCylinderLow = 0;
		mRFile.mSectorNumber = 1;
	}
}

ATIDEEmulator::DecodedCHS ATIDEEmulator::DecodeCHS(uint32 lba) {
	DecodedCHS s;

	if (!mSectorsPerTrack || !mHeadCount) {
		strcpy(s.buf, "???");
	} else {
		uint32 track = lba / mSectorsPerTrack;
		uint32 sector = lba % mSectorsPerTrack;
		uint32 cylinder = track / mHeadCount;
		uint32 head = track % mHeadCount;
		sprintf(s.buf, "%u/%u/%u", cylinder, head, sector);
	}

	return s;
}
