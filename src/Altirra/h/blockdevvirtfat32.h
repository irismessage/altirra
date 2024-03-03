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

#ifndef f_AT_BLOCKDEVVIRTFAT32_H
#define f_AT_BLOCKDEVVIRTFAT32_H

#include <vd2/system/date.h>
#include <vd2/system/file.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/blockdevice.h>

// ATBlockDeviceVFAT32
//
// Converts a host directory of files to a virtual FAT32 partition, reading the
// file data from the live files. Currently read-only.
//
class ATBlockDeviceVFAT32 final : public ATDeviceT<IATBlockDevice, IATBlockDeviceDynamic> {
public:
	ATBlockDeviceVFAT32(bool useFat16);
	~ATBlockDeviceVFAT32();

public:
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void ColdReset() override;
	void WarmReset() override;

public:
	bool IsReadOnly() const override;
	uint32 GetSectorCount() const override;
	ATBlockDeviceGeometry GetGeometry() const override;
	uint32 GetSerialNumber() const override;

	void Init() override;
	void Shutdown() override;

	void Flush() override;

	void ReadSectors(void *data, uint32 lba, uint32 n) override;
	void WriteSectors(const void *data, uint32 lba, uint32 n) override;

public:
	void RescanDynamicDisk() override;

protected:
	void FormatVolume();
	void BuildDirectory();
	void AutoSizeVolume();

	VDFile mFile;
	sint32 mActiveFileIndex = -1;
	sint64 mActiveFilePos = -1;

	VDStringW mPath;
	bool mbUseFAT16 = false;
	bool mbEnableMBR = true;

	uint32 mSectorCount = 0;
	uint32 mSectorsPerFAT = 0;

	struct FileEntry {
		VDStringW mFileName;
		uint8 mShortName[11];
		vdfastvector<uint16> mLongName;
		bool mbLFNRequired;
		uint32 mFileSize;
		uint32 mStartingCluster;
		VDDate mCreationDate;
		VDDate mLastWriteDate;
	};

	ATBlockDeviceGeometry mGeometry {};

	vdvector<FileEntry> mFiles;
	vdfastvector<uint8> mRootDirectory;
	vdfastvector<uint32> mDataClusterBoundaries;

	uint8 mBootSectors[512 * 16] {};
};

#endif
