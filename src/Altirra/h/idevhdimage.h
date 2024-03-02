//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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

#ifndef f_AT_IDEVHDIMAGE_H
#define f_AT_IDEVHDIMAGE_H

#include <at/atcore/blockdevice.h>
#include <at/atcore/deviceimpl.h>
#include <vd2/system/binary.h>
#include <vd2/system/file.h>

struct ATVHDFooter {
	enum : uint32 {
		kDiskTypeFixed = 2,
		kDiskTypeDynamic = 3,
		kDiskTypeDifferencing = 4
	};

	uint8	mCookie[8];
	uint32	mFeatures;
	uint32	mVersion;
	uint64	mDataOffset;
	uint32	mTimestamp;
	uint32	mCreatorApplication;
	uint32	mCreatorVersion;
	uint32	mCreatorHostOS;
	uint64	mOriginalSize;
	uint64	mCurrentSize;
	uint32	mDiskGeometry;
	uint32	mDiskType;
	uint32	mChecksum;
	uint8	mUniqueId[16];
	uint8	mSavedState;
	uint8	mReserved[427];
};

struct ATVHDParentLocator {
	static constexpr uint32 kCodeWindowsAbsPath = VDMAKEFOURCC('W', '2', 'k', 'u');
	static constexpr uint32 kCodeWindowsRelPath = VDMAKEFOURCC('W', '2', 'r', 'u');

	uint32	mCode;
	uint32	mSpace;		// Documented in the VHD spec as sectors, but is actually bytes
	uint32	mLength;	// Length in bytes; null terminator not present
	uint32	mReserved;
	uint64	mOffset;	// Absolute file position of data
};

struct ATVHDDynamicDiskHeader {
	uint8	mCookie[8];
	uint64	mDataOffset;
	uint64	mTableOffset;
	uint32	mHeaderVersion;
	uint32	mMaxTableEntries;
	uint32	mBlockSize;
	uint32	mChecksum;
	uint8	mParentUniqueId[16];
	uint32	mParentTimestamp;
	uint32	mReserved;
	uint16	mParentUnicodeName[256];
	ATVHDParentLocator mParentLocators[8];
	uint8	mReserved2[256];
};

class ATIDEVHDImage final : public IATBlockDevice, public IATBlockDeviceDirectAccess, public ATDevice {
	ATIDEVHDImage(const ATIDEVHDImage&) = delete;
	ATIDEVHDImage& operator=(const ATIDEVHDImage&) = delete;
public:
	ATIDEVHDImage();
	~ATIDEVHDImage();

public:
	int AddRef() override;
	int Release() override;
	void *AsInterface(uint32 iid) override;

public:
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;

public:
	virtual bool IsReadOnly() const override { return mbReadOnly; }
	uint32 GetSectorCount() const override;
	ATBlockDeviceGeometry GetGeometry() const override;
	uint32 GetSerialNumber() const override;

	const wchar_t *GetAbsPath() const { return mAbsPath.c_str(); }
	const void *GetUID() const { return mFooter.mUniqueId; }
	uint32 GetVHDTimestamp() const;
	uint8 GetVHDHeads() const { return (mFooter.mDiskGeometry >> 8) & 0xFF; }
	uint8 GetVHDSectorsPerTrack() const { return mFooter.mDiskGeometry & 0xFF; }

	void Init(const wchar_t *path, bool write, bool solidState);
	void InitNew(const wchar_t *path, uint8 heads, uint8 spt, uint32 totalSectorCount, bool dynamic, ATIDEVHDImage *parent);
	void Init() override {}
	void Shutdown() override;

	void Flush() override;

	void ReadSectors(void *data, uint32 lba, uint32 n) override;
	void WriteSectors(const void *data, uint32 lba, uint32 n) override;

public:
	VDStringW GetVHDDirectAccessPath() const override;

private:
	void InitCommon();
	void ReadDynamicDiskSectors(void *data, uint32 lba, uint32 n);
	void WriteDynamicDiskSectors(const void *data, uint32 lba, uint32 n);
	void SetCurrentBlock(uint32 blockIndex);
	void FlushCurrentBlockBitmap();
	void AllocateBlock();

	static uint32 ConvertToVHDTimestamp(const VDDate& date);

	VDFile mFile;
	VDStringW mPath;
	VDStringW mAbsPath;
	bool mbReadOnly;
	bool mbSolidState;
	sint64 mFooterLocation;
	uint32 mSectorCount;
	int mBlockSizeShift;
	uint32 mBlockLBAMask;
	uint32 mBlockSize;
	uint32 mBlockBitmapSize;

	vdblock<uint32> mBlockAllocTable;

	uint32 mCurrentBlock;
	sint64 mCurrentBlockDataOffset;
	bool mbCurrentBlockBitmapDirty;
	bool mbCurrentBlockAllocated;
	vdblock<uint8> mCurrentBlockBitmap;

	ATVHDFooter mFooter;
	ATVHDDynamicDiskHeader mDynamicHeader;

	vdrefptr<ATIDEVHDImage> mpParentImage;
};

#endif
