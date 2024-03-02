//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <unordered_set>
#include <vd2/system/binary.h>
#include <vd2/system/filesys.h>
#include <vd2/system/hash.h>
#include <at/atcore/logging.h>
#include <at/atcore/propertyset.h>
#include "blockdevvirtfat32.h"

extern ATLogChannel g_ATLCVDisk;

void ATCreateDeviceBlockDevVFAT16(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATBlockDeviceVFAT32> vdev(new ATBlockDeviceVFAT32(true));

	vdev->SetSettings(pset);

	*dev = vdev.release();
}

void ATCreateDeviceBlockDevVFAT32(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATBlockDeviceVFAT32> vdev(new ATBlockDeviceVFAT32(false));

	vdev->SetSettings(pset);

	*dev = vdev.release();
}

extern const ATDeviceDefinition g_ATDeviceDefBlockDevVFAT16 = {
	"hdvirtfat16",
	"hdvirtfat16",
	L"Virtual FAT16 hard disk",
	ATCreateDeviceBlockDevVFAT16
};

extern const ATDeviceDefinition g_ATDeviceDefBlockDevVFAT32 = {
	"hdvirtfat32",
	"hdvirtfat32",
	L"Virtual FAT32 hard disk",
	ATCreateDeviceBlockDevVFAT32
};

ATBlockDeviceVFAT32::ATBlockDeviceVFAT32(bool useFAT16)
	: mbUseFAT16(useFAT16)
{
}

ATBlockDeviceVFAT32::~ATBlockDeviceVFAT32() {
	Shutdown();
}

int ATBlockDeviceVFAT32::AddRef() {
	return ATDevice::AddRef();
}

int ATBlockDeviceVFAT32::Release() {
	return ATDevice::Release();
}

void *ATBlockDeviceVFAT32::AsInterface(uint32 iid) {
	switch(iid) {
		case IATBlockDevice::kTypeID: return static_cast<IATBlockDevice *>(this);
		default:
			return ATDevice::AsInterface(iid);
	}
}

void ATBlockDeviceVFAT32::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = mbUseFAT16 ? &g_ATDeviceDefBlockDevVFAT16 : &g_ATDeviceDefBlockDevVFAT32;
}

void ATBlockDeviceVFAT32::GetSettingsBlurb(VDStringW& buf) {
	buf = mPath;
}

void ATBlockDeviceVFAT32::GetSettings(ATPropertySet& settings) {
	settings.SetString("path", mPath.c_str());
}

bool ATBlockDeviceVFAT32::SetSettings(const ATPropertySet& settings) {
	mPath = settings.GetString("path", L"");
	return false;
}

void ATBlockDeviceVFAT32::ColdReset() {
	WarmReset();
}

void ATBlockDeviceVFAT32::WarmReset() {
	mFile.closeNT();
	mActiveFileIndex = -1;
}

bool ATBlockDeviceVFAT32::IsReadOnly() const {
	return true;
}

uint32 ATBlockDeviceVFAT32::GetSectorCount() const {
	return mSectorCount;
}

ATBlockDeviceGeometry ATBlockDeviceVFAT32::GetGeometry() const {
	return mGeometry;
}

uint32 ATBlockDeviceVFAT32::GetSerialNumber() const {
	return VDHashString32I(mPath.c_str());
}

void ATBlockDeviceVFAT32::Init() {
	mGeometry.mbSolidState = true;

	BuildDirectory();
	AutoSizeVolume();
	FormatVolume();
}

void ATBlockDeviceVFAT32::Shutdown() {
	mFile.closeNT();
	mActiveFileIndex = -1;
}

void ATBlockDeviceVFAT32::Flush() {
}

void ATBlockDeviceVFAT32::ReadSectors(void *data, uint32 lba, uint32 n) {
	while(n--) {
		unsigned char *sector = (unsigned char *)data;
		data = sector + 512;
		uint32 offset = lba++;

		// check for MBR
		if (mbEnableMBR) {
			if (offset < 8) {
				memset(sector, 0, 512);

				if (offset == 0) {
					// Reuse the VSN as the disk serial number; we need this or
					// Windows 10 insists on rewriting a new one. It will actually
					// refuse to recognize the MBR without one when the VHD is
					// mounted read-only.
					memcpy(sector + 0x1B8, mBootSectors + 67, 4);

					sector[0x1BE] = 0x80;
					VDWriteUnalignedLEU32(&sector[0x1C0], 9 << 10);
					sector[0x1C2] = mbUseFAT16 ? 0x0E : 0x0C;
					sector[0x1C3] = 0xFE;
					sector[0x1C4] = 0xFF;
					sector[0x1C5] = 0xFF;
					VDWriteUnalignedLEU32(&sector[0x1C6], 8);
					VDWriteUnalignedLEU32(&sector[0x1CA], mSectorCount - 8);

					sector[0x1FE] = 0x55;
					sector[0x1FF] = 0xAA;
				}
				continue;
			}

			offset -= 8;
		}

		// check for reserved sectors (boot, backup boot, FSInfo)
		const uint32 numReserved = mbUseFAT16 ? 1 : 16;

		if (offset < numReserved) {
			memcpy(sector, mBootSectors + offset * 512, 512);
			continue;
		}

		offset -= numReserved;

		// check for FAT
		if (offset < mSectorsPerFAT * 2) {
			// remap second FAT back to first
			if (offset >= mSectorsPerFAT)
				offset -= mSectorsPerFAT;

			// determine starting cluster managed by this FAT
			uint32 mappedClusterStart = mbUseFAT16 ? offset << 8 : offset << 7;

			// find the nearest extent
			auto itEnd = mDataClusterBoundaries.end();
			auto it = std::lower_bound(mDataClusterBoundaries.begin(), itEnd, mappedClusterStart + 1);
			uint32 nextTerminator = it == mDataClusterBoundaries.end() ? ~(uint32)0 : *it - 1;
			uint32 limit = mDataClusterBoundaries.back();

			if (mbUseFAT16) {
				for(uint32 i = 0; i < 256; ++i) {
					uint16 link;

					if (mappedClusterStart + i >= limit) {
						link = 0;
					} else if (mappedClusterStart + i == nextTerminator) {
						link = 0xFFFF;
						nextTerminator = *++it - 1;
					} else {
						link = mappedClusterStart + i + 1;
					}

					VDWriteUnalignedLEU16((char *)sector + 2 * i, link);
				}
			} else {
				for(uint32 i = 0; i < 128; ++i) {
					uint32 link;

					if (mappedClusterStart + i >= limit) {
						link = 0;
					} else if (mappedClusterStart + i == nextTerminator) {
						link = 0x0FFFFFFF;
						nextTerminator = *++it - 1;
					} else {
						link = mappedClusterStart + i + 1;
					}

					VDWriteUnalignedLEU32((char *)sector + 4 * i, link);
				}
			}

			// if this is the first FAT sector, write the media type and clean
			// FS bits
			if (offset == 0) {
				static constexpr uint8 kFATHeader[8] {
					0xF8, 0xFF, 0xFF, 0xFF,
					0xFF, 0xFF, 0xFF, 0xFF
				};

				memcpy(sector, kFATHeader, mbUseFAT16 ? 4 : 8);
			}

			continue;
		}

		offset -= mSectorsPerFAT * 2;

		// check for root directory (FAT16 only)
		if (mbUseFAT16) {
			if (offset < 32) {
				uint32 copyOffset = offset << 9;
				uint32 copyMax = (uint32)mRootDirectory.size();
				uint32 copyLen = copyOffset < copyMax ? std::min<uint32>(512, copyMax - copyOffset) : 0;

				if (copyLen)
					memcpy(sector, mRootDirectory.data() + copyOffset, copyLen);

				memset((char *)sector + copyLen, 0, 512 - copyLen);
				continue;
			}

			offset -= 32;
		}

		// check if it is a data cluster
		const auto it = std::upper_bound(mDataClusterBoundaries.begin(), mDataClusterBoundaries.end(), (offset >> 3) + 2);
		const uint32 fileIndex = (uint32)(it - mDataClusterBoundaries.begin());
		const uint32 fileCount = (uint32)mFiles.size();

		if (fileIndex < (mbUseFAT16 ? fileCount : fileCount + 1)) {
			if (fileIndex)
				offset -= (mDataClusterBoundaries[fileIndex - 1] - 2)*8;

			if (fileIndex < fileCount) {
				memset(sector, 0, 512);

				try {
					if (mActiveFileIndex != (sint32)fileIndex) {
						mActiveFileIndex = (sint32)fileIndex;

						mFile.closeNT();
						mFile.open(VDMakePath(mPath.c_str(), mFiles[fileIndex].mFileName.c_str()).c_str());
						mActiveFilePos = 0;
					}

					if (mFile.isOpen()) {
						sint64 byteOffset = (sint64)offset << 9;

						if (mActiveFilePos != byteOffset) {
							mActiveFilePos = -1;
							mFile.seek(byteOffset);
						}

						auto actual = mFile.readData(sector, 512);
						if (actual >= 0)
							mActiveFilePos = byteOffset + actual;
						else
							mActiveFilePos = -1;
					}
				} catch(const MyError&) {
				}
			} else {
				// root directory, copy the sector
				uint32 copyOffset = offset << 9;
				uint32 copyMax = (uint32)mRootDirectory.size();
				uint32 copyLen = copyOffset < copyMax ? std::min<uint32>(512, copyMax - copyOffset) : 0;

				if (copyLen)
					memcpy(sector, mRootDirectory.data() + copyOffset, copyLen);

				memset((char *)sector + copyLen, 0, 512 - copyLen);
			}
			continue;
		}

		// no hit, just clear the sector
		memset(sector, 0, 512);
	}
}

void ATBlockDeviceVFAT32::WriteSectors(const void *data, uint32 lba, uint32 n) {
	throw MyError("Device is read-only.");
}

void ATBlockDeviceVFAT32::FormatVolume() {
	// Compute size of each FAT.
	//
	// We need to solve a formula for this since the FAT is sized to address
	// the data area, which is in turn sized by the space remaining after the
	// FATs. This is resolved as follows:
	//
	// - The boot area and FATs are aligned to 4K clusters to make everything
	//   nice. For FAT16 we must forgo this as the reserved sector count must
	//   be 1.
	// - Each cluster of FAT can then track 1024 clusters, giving a natural
	//   cluster group size of 1026 clusters.
	// - The first two entries of the FAT are reserved, however, so the first
	//   FAT cluster only tracks 1022 data clusters. Including the boot sector,
	//   this means that we need a second cluster group starting at 1024 clusters.
	// - For a FAT16 volume, we need to add an additional 4 clusters (32 sectors)
	//   for the root directory. These are thankfully not data clusters and
	//   don't affect the FAT calculations.

	const uint32 hiddenSectors = mbEnableMBR ? 8 : 0;
	const uint32 reservedSectors = mbUseFAT16 ? 1 : 16;
	const uint32 rootDirectorySectors = mbUseFAT16 ? 32 : 0;
	const uint32 totalClusters = (mSectorCount - reservedSectors - rootDirectorySectors - hiddenSectors) >> 3;
	const uint32 clustersPerFAT = mbUseFAT16 ? (totalClusters + 2) / 2050 + 1 : (totalClusters + 2) / 1026 + 1;
	const uint32 dataClusters = totalClusters - 2*clustersPerFAT;

	mSectorsPerFAT = 8 * clustersPerFAT;

	memset(mBootSectors, 0, sizeof mBootSectors);

	// set up BPB
	static constexpr uint8 kBootStartFAT16[] {
		0xEB, 0x3C, 0x90,
		(uint8)'M',
		(uint8)'S',
		(uint8)'D',
		(uint8)'O',
		(uint8)'S',
		(uint8)'5',
		(uint8)'.',
		(uint8)'0',
		0x00, 0x02,		// bytes per sector
		0x08,			// sectors per cluster
		0x08, 0x00,		// reserved sector count
		0x02,
		0x00, 0x02,		// +17 number of root directory entries (0 for FAT32)
		0x00, 0x00,		// +19 total sectors; filled in dynamically
		0xF8,			// +21 media type
		0x00, 0x00,		// +22 FAT12/FAT16 size; filled in dynamically
		0x3F, 0x00,
		0xFF, 0x00,		// +26 head count
		0x00, 0x00, 0x00, 0x00,		// +28 hidden sector count
		0x00, 0x00, 0x00, 0x00,		// +32 total size in sectors
		
		// Extended Boot Record (+36)
		0x80,			// Int13h drive number
		0,				// reserved
		0x29,			// extended boot signature
		0x00, 0x00, 0x00, 0x00,		// +67 volume serial number
		(uint8)'N',					// +71 volume label
		(uint8)'O',
		(uint8)' ',
		(uint8)'N',
		(uint8)'A',
		(uint8)'M',
		(uint8)'E',
		(uint8)' ',
		(uint8)' ',
		(uint8)' ',
		(uint8)' ',
		(uint8)'F',					// +82 system identifier string
		(uint8)'A',
		(uint8)'T',
		(uint8)'1',
		(uint8)'6',
		(uint8)' ',
		(uint8)' ',
		(uint8)' ',
	};

	static constexpr uint8 kBootStartFAT32[] {
		0xEB, 0x58, 0x90,
		(uint8)'M',
		(uint8)'S',
		(uint8)'D',
		(uint8)'O',
		(uint8)'S',
		(uint8)'5',
		(uint8)'.',
		(uint8)'0',
		0x00, 0x02,		// bytes per sector
		0x08,			// sectors per cluster
		0x08, 0x00,		// +14 reserved sector count
		0x02,			// +16
		0x00, 0x00,		// +17 number of root directory entries (0 for FAT32)
		0x00, 0x00,		// +19 total sectors; filled in dynamically
		0xF8,			// +21 media type
		0x00, 0x00,
		0x3F, 0x00,
		0xFF, 0x00,		// +26 head count
		0x00, 0x00, 0x00, 0x00,		// +28 hidden sector count
		0x00, 0x00, 0x00, 0x00,		// +32 total size in sectors
		
		// Extended Boot Record (+36)
		0,0,0,0,					// +36 FAT size in sectors
		0,0,
		0,0,		// fat version number
		2,0,0,0,	// +44 cluster number of root directory
		1,0,		// fsinfo (sector 1)
		6,0,		// backup boot sector (sector 6)
		0,0,0,0,0,0,0,0,0,0,0,0,
		0x80,
		0x00,
		0x29,
		0x00, 0x00, 0x00, 0x00,		// +67 volume serial number
		(uint8)'N',					// +71 volume label
		(uint8)'O',
		(uint8)' ',
		(uint8)'N',
		(uint8)'A',
		(uint8)'M',
		(uint8)'E',
		(uint8)' ',
		(uint8)' ',
		(uint8)' ',
		(uint8)' ',
		(uint8)'F',					// +82 system identifier string
		(uint8)'A',
		(uint8)'T',
		(uint8)'3',
		(uint8)'2',
		(uint8)' ',
		(uint8)' ',
		(uint8)' ',
	};

	static_assert(sizeof(kBootStartFAT32) == 90);
	static_assert(sizeof(kBootStartFAT16) == 62);

	if (mbUseFAT16)
		memcpy(mBootSectors, kBootStartFAT16, sizeof kBootStartFAT16);
	else
		memcpy(mBootSectors, kBootStartFAT32, sizeof kBootStartFAT32);

	// update reserved sector count
	VDWriteUnalignedLEU16(&mBootSectors[14], reservedSectors);

	// update hidden sector count
	VDWriteUnalignedLEU32(&mBootSectors[28], hiddenSectors);

	// update total sector count -- this needs to be correct or Windows 10
	// CHKDSK will reject FAT16 volumes even if the FAT driver doesn't; oddly
	// it doesn't do this check for FAT32
	VDWriteUnalignedLEU32(&mBootSectors[32], mSectorCount - hiddenSectors);

	if (mbUseFAT16) {
		// update FAT size
		VDWriteUnalignedLEU16(&mBootSectors[22], clustersPerFAT*8);
	} else {
		// update FAT size
		VDWriteUnalignedLEU32(&mBootSectors[36], clustersPerFAT*8);

		// set root directory cluster
		VDWriteUnalignedLEU32(&mBootSectors[44], mDataClusterBoundaries.size() > 1 ? mDataClusterBoundaries.end()[-2] : 2);
	}

	// set volume serial number
	VDWriteUnalignedLEU32(&mBootSectors[67], GetSerialNumber());

	mBootSectors[510] = 0x55;
	mBootSectors[511] = 0xAA;

	if (mbUseFAT16) {
		// init backup boot sector (sector 1)
		memcpy(mBootSectors + 512, mBootSectors, 512);

	} else {
		static constexpr uint8 kFSInfoHeader[] {
			0x52, 0x52, 0x61, 0x41,
		};

		static constexpr uint8 kFSInfoFooter[] {
			0x72, 0x72, 0x41, 0x61,
			0x00, 0x00, 0x00, 0x00,
			0xFF, 0xFF, 0xFF, 0xFF,
			0,0,0,0,0,0,0,0,0,0,0,0,
			0x00, 0x00, 0x55, 0xAA
		};

		static_assert(sizeof(kFSInfoHeader) == 4);
		static_assert(sizeof(kFSInfoFooter) == 28);

		// write FSInfo to sector 1
		memcpy(&mBootSectors[512], kFSInfoHeader, 4);
		memcpy(&mBootSectors[1024 - 28], kFSInfoFooter, 28);

		// create empty boot sector 2
		mBootSectors[1536 - 2] = 0x55;
		mBootSectors[1536 - 1] = 0xAA;

		// init backup boot sectors (sectors 6-8)
		memcpy(mBootSectors + 512 * 6, mBootSectors, 512 * 3);


		// set free cluster count
		VDWriteUnalignedLEU32(&mBootSectors[512 + 488], dataClusters - mDataClusterBoundaries.back());
	}

	const uint32 storageOffset = hiddenSectors + reservedSectors;
	if (mbUseFAT16) {
		g_ATLCVDisk("FAT16 layout: FAT 1 at %08X, FAT 2 at %08X, root directory at %08X-%08X\n"
			, storageOffset
			, storageOffset + mSectorsPerFAT
			, storageOffset + mSectorsPerFAT*2
			, storageOffset + mSectorsPerFAT*2 + rootDirectorySectors);
	} else {
		g_ATLCVDisk("FAT32 layout: FAT 1 at %08X, FAT 2 at %08X, root directory at %08X-%08X\n"
			, storageOffset
			, storageOffset + mSectorsPerFAT
			, storageOffset + mSectorsPerFAT*2 + 8 * (mDataClusterBoundaries.size() < 2 ? 0 : mDataClusterBoundaries.end()[-2] - 2)
			, storageOffset + mSectorsPerFAT*2 + 8 * (mDataClusterBoundaries.back() - 2) - 1);
	}
}

void ATBlockDeviceVFAT32::BuildDirectory() {
	mRootDirectory.clear();
	mDataClusterBoundaries.clear();
	mFiles.clear();

	// create volume label
	static constexpr uint8 kVolumeLabel[32] {
		(uint8)'N',
		(uint8)'O',
		(uint8)' ',
		(uint8)'N',
		(uint8)'A',
		(uint8)'M',
		(uint8)'E',
		(uint8)' ',
		(uint8)' ',
		(uint8)' ',
		(uint8)' ',
		0x08,
	};

	mRootDirectory.resize(32);
	memcpy(mRootDirectory.data(), kVolumeLabel, 32);

	uint32 nextCluster = 2;
	if (!mPath.empty()) {
		struct ShortName {
			uint8 c[11];
		};

		struct ShortNameHashPred {
			size_t operator()(const ShortName& name) const {
				size_t hash = 0;

				for(int i=0; i<11; ++i)
					hash = (hash * 31) + name.c[i];

				return hash;
			}

			bool operator()(const ShortName& x, const ShortName& y) const {
				return !memcmp(x.c, y.c, 11);
			}
		};

		std::unordered_set<ShortName, ShortNameHashPred, ShortNameHashPred> shortNames;

		for(VDDirectoryIterator it(VDMakePath(mPath.c_str(), L"*.*").c_str()); it.Next() && mFiles.size() < 1000000; ) {
			if (it.IsDirectory())
				continue;

			if (!it.ResolveLinkSize())
				continue;

			sint32 size64 = it.GetSize();

			if (size64 > 0xFFFFFFFF)
				continue;

			const uint32 size32 = (uint32)size64;

			// compute the number of clusters needed for this file
			const uint32 numClusters = (size32 >> 12) + (size32 & 0xFFF ? 1 : 0);

			// if this is a FAT16 volume and we are out of space, end population; we
			// stop a bit short of the theoretical max limit per general recommendations
			// to avoid variance in FAT16 implementations
			if (65500 - nextCluster < numClusters)
				break;

			mFiles.emplace_back();
			FileEntry& fe = mFiles.back();
			fe.mFileName = it.GetName();
			fe.mFileSize = size32;
			fe.mStartingCluster = size32 ? nextCluster : 0;
			fe.mbLFNRequired = false;

			// convert filename to UTF-16
			int nameLen = 0;
			int extLen = 0;
			bool inName = true;
			bool inExt = false;

			memset(fe.mShortName, 0x20, sizeof fe.mShortName);

			for(const wchar_t c : fe.mFileName) {
				uint32 c32 = (uint32)c;

				if constexpr(sizeof(wchar_t) > 2) {
					if (c32 >= 0x10000) {
						c32 -= 0x10000;

						fe.mLongName.push_back((uint16)(0xD800 + (c32 >> 10)));

						c32 = 0xDC00 + (c32 & 0x3FF);
					}
				}

				fe.mLongName.push_back((uint16)c32);

				uint8 cleanChar = (uint8)'_';

				switch(c) {
					case L'$':
					case L'%':
					case L'\'':
					case L'-':
					case L'_':
					case L'@':
					case L'~':
					case L'`':
					case L'!':
					case L'(':
					case L')':
					case L'{':
					case L'}':
					case L'^':
					case L'#':
					case L'&':
					case L'.':		// not used in short encoding, but we need to pass it through
						cleanChar = (uint8)c;
						break;

					default:
						if ((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z'))
							cleanChar = (uint8)c;
						else {
							if (c >= L'a' && c <= L'z')
								cleanChar = (uint8)(c - L'a' + L'A');

							fe.mbLFNRequired = true;
						}
						break;
				}

				if (inName) {
					if (c == '.') {
						if (nameLen == 0) {
							fe.mShortName[nameLen++] = (uint8)'_';
							fe.mbLFNRequired = true;
						}

						inName = false;
						inExt = true;
					} else if (nameLen < 8) {
						fe.mShortName[nameLen++] = cleanChar;
					} else {
						fe.mbLFNRequired = true;
					}
				} else if (inExt) {
					if (c == '.') {
						inExt = false;
					} else if (extLen < 3) {
						fe.mShortName[8 + extLen++] = cleanChar;
					} else {
						fe.mbLFNRequired = true;
					}
				}
			}

			// if a long name is not required, then push it into the hash set
			// first -- these short names have priority before we make the mangled
			// names
			if (!fe.mbLFNRequired) {
				ShortName shortName;
				memcpy(shortName.c, fe.mShortName, 11);
				if (!shortNames.insert(shortName).second) { 
					// hmm, we have a conflicting short name -- we're going to
					// have to drop this entry
					mFiles.pop_back();
					continue;
				}
			}

			nextCluster += numClusters;
			mDataClusterBoundaries.push_back(nextCluster);

			fe.mCreationDate = it.GetCreationDate();
			fe.mLastWriteDate = it.GetLastWriteDate();
		}

		// allocate short names for all entries that require long names
		for(FileEntry& fe : mFiles) {
			if (!fe.mbLFNRequired)
				continue;

			fe.mShortName[6] = (uint8)'~';
			fe.mShortName[7] = (uint8)'1';
			
			for(;;) {
				ShortName shortName;
				memcpy(shortName.c, fe.mShortName, 11);

				if (shortNames.insert(shortName).second)
					break;

				for(int i=7; i>0; --i) {
					uint8& c = fe.mShortName[i];

					if (c == (uint8)'~') {
						fe.mShortName[i-1] = (uint8)'~';
						c = (uint8)'1';
						break;
					} else if (c == (uint8)'9') {
						c = (uint8)'0';
					} else {
						++c;
						break;
					}
				}
			}
		}

		// create root directory from all files
		for(FileEntry& fe : mFiles) {
			uint8 baseDirEnt[32] {};
			memcpy(baseDirEnt, fe.mShortName, 11);

			const VDExpandedDate modLocalDate = VDGetLocalDate(fe.mLastWriteDate);
			const VDExpandedDate creationLocalDate = VDGetLocalDate(fe.mCreationDate);

			// set creation date
			baseDirEnt[13] = (creationLocalDate.mMilliseconds / 10) + (creationLocalDate.mSecond & 1 ? 100 : 0);
			VDWriteUnalignedLEU16(&baseDirEnt[14], (creationLocalDate.mSecond >> 1) + (creationLocalDate.mMinute << 5) + (creationLocalDate.mHour << 11));
			VDWriteUnalignedLEU16(&baseDirEnt[16], creationLocalDate.mDay + (creationLocalDate.mMonth << 5) + ((std::clamp<uint32>(creationLocalDate.mYear, 1980, 1980 + 127) - 1980) << 9));

			// set modification date
			VDWriteUnalignedLEU16(&baseDirEnt[22], (modLocalDate.mSecond >> 1) + (modLocalDate.mMinute << 5) + (modLocalDate.mHour << 11));
			VDWriteUnalignedLEU16(&baseDirEnt[24], modLocalDate.mDay + (modLocalDate.mMonth << 5) + ((std::clamp<uint32>(modLocalDate.mYear, 1980, 1980 + 127) - 1980) << 9));

			// set accessed date (to mod date)
			baseDirEnt[18] = baseDirEnt[24];
			baseDirEnt[19] = baseDirEnt[25];

			// set starting cluster
			VDWriteUnalignedLEU16(&baseDirEnt[20], (uint16)(fe.mStartingCluster >> 16));
			VDWriteUnalignedLEU16(&baseDirEnt[26], (uint16)fe.mStartingCluster);

			// set file size
			VDWriteUnalignedLEU32(&baseDirEnt[28], fe.mFileSize);

			// check if LFN is required
			if (fe.mbLFNRequired) {
				// compute short name checksum
				uint8 checksum = 0;
				for(int i=0; i<11; ++i)
					checksum = (uint8)((checksum >> 1) + (checksum << 7) + baseDirEnt[i]);

				// compute number of extents required
				const uint32 numExtents = ((uint32)fe.mLongName.size() + 12) / 13;

				// null terminate the long filename and pad, only if not already in whole extents
				if (fe.mLongName.size() % 13) {
					fe.mLongName.push_back(0);
					fe.mLongName.resize(numExtents * 13, 0xFFFF);
				}

				// add space for new directory entries
				mRootDirectory.resize(mRootDirectory.size() + 32 * numExtents);

				// populate extents
				uint8 *dst = &*(mRootDirectory.end() - 32);
				const uint16 *src = fe.mLongName.data();
				for(uint32 i = 0; i < numExtents; ++i) {
					dst[0] = i + 1 + (i == numExtents - 1 ? 0x40 : 0x00);
					dst[11] = 0x0F;
					dst[12] = 0x00;
					dst[13] = checksum;
					dst[26] = 0;
					dst[27] = 0;

					VDWriteUnalignedLEU16(&dst[1], src[0]);
					VDWriteUnalignedLEU16(&dst[3], src[1]);
					VDWriteUnalignedLEU16(&dst[5], src[2]);
					VDWriteUnalignedLEU16(&dst[7], src[3]);
					VDWriteUnalignedLEU16(&dst[9], src[4]);
					VDWriteUnalignedLEU16(&dst[14], src[5]);
					VDWriteUnalignedLEU16(&dst[16], src[6]);
					VDWriteUnalignedLEU16(&dst[18], src[7]);
					VDWriteUnalignedLEU16(&dst[20], src[8]);
					VDWriteUnalignedLEU16(&dst[22], src[9]);
					VDWriteUnalignedLEU16(&dst[24], src[10]);
					VDWriteUnalignedLEU16(&dst[28], src[11]);
					VDWriteUnalignedLEU16(&dst[30], src[12]);

					dst -= 32;
					src += 13;
				}
			}

			// write entry to root directory
			mRootDirectory.insert(mRootDirectory.end(), baseDirEnt, baseDirEnt + 32);
		}
	}

	// add root directory entry for FAT32
	if (!mbUseFAT16) {
		nextCluster += (mRootDirectory.size() + 4095) >> 12;
		mDataClusterBoundaries.push_back(nextCluster);
	}
}

void ATBlockDeviceVFAT32::AutoSizeVolume() {
	uint32 totalClustersNeeded = 0;
	uint32 dataClustersNeeded = mDataClusterBoundaries.empty() ? 0 : mDataClusterBoundaries.back() - 2;
	uint32 reservedSectorsNeeded = mbEnableMBR ? 8 : 0;

	if (mbUseFAT16) {
		// one boot sector and 32 root directory sectors
		reservedSectorsNeeded += 33;

		if (dataClustersNeeded < 4096)
			dataClustersNeeded = 4096;

		uint32 fatClustersNeeded = (dataClustersNeeded + 2 + 2047) >> 11;
		totalClustersNeeded += fatClustersNeeded*2 + dataClustersNeeded;
	} else {
		reservedSectorsNeeded += 16;

		if (dataClustersNeeded < 65536)
			dataClustersNeeded = 65536;

		uint32 fatClustersNeeded = (dataClustersNeeded + 2 + 1023) >> 10;
		totalClustersNeeded += fatClustersNeeded*2 + dataClustersNeeded;
	}

	mSectorCount = totalClustersNeeded * 8 + reservedSectorsNeeded;
}
