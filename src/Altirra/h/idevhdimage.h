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

#include <vd2/system/file.h>
#include "idedisk.h"

struct ATVHDFooter {
	enum {
		kDiskTypeFixed = 2,
		kDiskTypeDynamic = 3
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
	uint8	mParentLocatorEntry[8][24];
	uint8	mReserved2[256];
};

class ATIDEVHDImage : public vdrefcounted<IATIDEDisk> {
	ATIDEVHDImage(const ATIDEVHDImage&);
	ATIDEVHDImage& operator=(const ATIDEVHDImage&);
public:
	ATIDEVHDImage();
	~ATIDEVHDImage();

	uint32 GetSectorCount() const;

	void Init(const wchar_t *path, bool write);
	void InitNew(const wchar_t *path, uint8 heads, uint8 spt, uint32 totalSectorCount, bool dynamic);
	void Shutdown();

	void Flush();

	void RequestUpdate();

	void ReadSectors(void *data, uint32 lba, uint32 n);
	void WriteSectors(const void *data, uint32 lba, uint32 n);

protected:
	void InitCommon();
	void ReadDynamicDiskSectors(void *data, uint32 lba, uint32 n);
	void WriteDynamicDiskSectors(const void *data, uint32 lba, uint32 n);
	void SetCurrentBlock(uint32 blockIndex);
	void FlushCurrentBlockBitmap();
	void AllocateBlock();

	VDFile mFile;
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
};

#endif
