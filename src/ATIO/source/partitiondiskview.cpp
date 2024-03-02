//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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
#include <at/atcore/blockdevice.h>
#include <at/atio/partitiondiskview.h>

ATPartitionDiskView::ATPartitionDiskView(IATBlockDevice& dev, const ATPartitionInfo& partitionInfo)
	: mPartitionInfo(partitionInfo)
	, mpBlockDevice(&dev)
{
	uint32 bootSectorCount = std::min<uint32>(partitionInfo.mSectorCount, 3);

	if (partitionInfo.mSectorSize >= 512)
		bootSectorCount = 1;

	if (dev.IsReadOnly())
		mPartitionInfo.mbWriteProtected = true;

	mGeometryInfo = ATDiskCreateDefaultGeometry(mPartitionInfo.mSectorCount, mPartitionInfo.mSectorSize, bootSectorCount);
}

ATPartitionDiskView::~ATPartitionDiskView() {
}

void *ATPartitionDiskView::AsInterface(uint32 iid) {
	if (iid == IATDiskImage::kTypeID)
		return static_cast<IATDiskImage *>(this);

	return nullptr;
}

ATImageType ATPartitionDiskView::GetImageType() const {
	return kATImageType_Disk;
}

std::optional<uint32> ATPartitionDiskView::GetImageFileCRC() const {
	return std::nullopt;
}

std::optional<ATChecksumSHA256> ATPartitionDiskView::GetImageFileSHA256() const {
	return std::nullopt;
}

ATDiskTimingMode ATPartitionDiskView::GetTimingMode() const {
	return kATDiskTimingMode_Any;
}

bool ATPartitionDiskView::IsDirty() const {
	return false;
}

bool ATPartitionDiskView::IsUpdatable() const {
	return !mPartitionInfo.mbWriteProtected;
}

bool ATPartitionDiskView::IsDynamic() const {
	return false;
}

ATDiskImageFormat ATPartitionDiskView::GetImageFormat() const {
	return kATDiskImageFormat_None;
}

void ATPartitionDiskView::Flush() {
}

uint64 ATPartitionDiskView::GetImageChecksum() const {
	return 0;
}

void ATPartitionDiskView::SetPath(const wchar_t *path, ATDiskImageFormat format) {
}

void ATPartitionDiskView::Save(const wchar_t *path, ATDiskImageFormat format) {
	ATThrowDiskReadOnlyException();
}

ATDiskGeometryInfo ATPartitionDiskView::GetGeometry() const {
	return mGeometryInfo;
}

uint32 ATPartitionDiskView::GetSectorSize() const {
	return mPartitionInfo.mSectorSize;
}

uint32 ATPartitionDiskView::GetSectorSize(uint32 virtIndex) const {
	if (virtIndex < mGeometryInfo.mBootSectorCount)
		return 128;
	else
		return mPartitionInfo.mSectorSize;
}

uint32 ATPartitionDiskView::GetBootSectorCount() const {
	return mGeometryInfo.mBootSectorCount;
}

uint32 ATPartitionDiskView::GetPhysicalSectorCount() const {
	return mPartitionInfo.mSectorCount;
}

void ATPartitionDiskView::GetPhysicalSectorInfo(uint32 index, ATDiskPhysicalSectorInfo& info) const {
	info = {};

	info.mOffset = 0;
	info.mDiskOffset = 0;
	info.mPhysicalSize = GetSectorSize(index);
	info.mImageSize = info.mPhysicalSize;
	info.mbDirty = false;
	info.mbMFM = false;
	info.mRotPos = 0;
	info.mFDCStatus = 0xFF;
	info.mWeakDataOffset = -1;
}

void ATPartitionDiskView::ReadPhysicalSector(uint32 index, void *data, uint32 len) {
	(void)ReadVirtualSector(index, data, len);
}

void ATPartitionDiskView::WritePhysicalSector(uint32 index, const void *data, uint32 len, uint8 fdcStatus) {
	(void)WriteVirtualSector(index, data, len);
}

uint32 ATPartitionDiskView::GetVirtualSectorCount() const {
	return mPartitionInfo.mSectorCount;
}

void ATPartitionDiskView::GetVirtualSectorInfo(uint32 index, ATDiskVirtualSectorInfo& info) const {
	info = {};
	info.mStartPhysSector = index;
	info.mNumPhysSectors = 1;
}

uint32 ATPartitionDiskView::ReadVirtualSector(uint32 index, void *data, uint32 len) {
	const uint32 actualLen = index < mPartitionInfo.mSectorCount ? GetSectorSize(index) : 0;

	if (actualLen) {
		const uint32 lba = GetLBAForSector(index);

		uint8 secbuf[512];
		mpBlockDevice->ReadSectors(secbuf, lba, 1);

		uint32 toCopy = std::min(actualLen, len);

		const auto mapping = GetSectorByteMapping(index);

		uint8 *VDRESTRICT dst8 = (uint8 *)data;
		const uint8 *VDRESTRICT src8 = secbuf + mapping.mOffset;

		for(uint32 i=0; i<toCopy; ++i) {
			dst8[i] = *src8;
			src8 += mapping.mStride;
		}
	}

	if (actualLen < len)
		memset((char *)data + actualLen, 0, len - actualLen);

	return actualLen;
}

bool ATPartitionDiskView::WriteVirtualSector(uint32 index, const void *data, uint32 len) {
	if (mPartitionInfo.mbWriteProtected)
		ATThrowDiskReadOnlyException();

	if (index >= mPartitionInfo.mSectorCount)
		return false;

	uint8 secbuf[512] {};

	const uint32 lba = GetLBAForSector(index);

	if (IsRMWRequired())
		mpBlockDevice->ReadSectors(secbuf, lba, 1);

	const uint32 toCopy = std::min(len, GetSectorSize(index));
	
	const auto mapping = GetSectorByteMapping(index);

	{
		uint8 *VDRESTRICT dst8 = &secbuf[mapping.mOffset];
		const uint8 *VDRESTRICT src8 = (const uint8 *)data;

		for(uint32 i=0; i<toCopy; ++i) {
			*dst8 = src8[i];
			dst8 += mapping.mStride;
		}
	}

	mpBlockDevice->WriteSectors(secbuf, lba, 1);
	return true;
}

void ATPartitionDiskView::Resize(uint32 sectors) {
	ATThrowDiskUnsupportedOperation();
}

void ATPartitionDiskView::FormatTrack(uint32 vsIndexStart, uint32 vsCount, const ATDiskVirtualSectorInfo *vsecs, uint32 psCount, const ATDiskPhysicalSectorInfo *psecs, const uint8 *psecData) {
	ATThrowDiskUnsupportedOperation();
}

bool ATPartitionDiskView::IsSafeToReinterleave() const {
	return false;
}

void ATPartitionDiskView::Reinterleave(ATDiskInterleave interleave) {
	ATThrowDiskUnsupportedOperation();
}

bool ATPartitionDiskView::IsRMWRequired() const {
	switch(mPartitionInfo.mSectorMode) {
		case ATPartitionSectorMode::Direct:
		case ATPartitionSectorMode::Direct2x:
		case ATPartitionSectorMode::Direct4x:
		default:
			return false;

		case ATPartitionSectorMode::InterleavedSectorBytes:
		case ATPartitionSectorMode::InterleavedSectorBytes2x:
		case ATPartitionSectorMode::PackedSectors:
		case ATPartitionSectorMode::PackedSectorsByteToWord:
			return true;
	}
}

uint32 ATPartitionDiskView::GetLBAForSector(uint32 sectorIndex) const {
	uint32 blockOffset = sectorIndex;

	switch(mPartitionInfo.mSectorMode) {
		case ATPartitionSectorMode::Direct:
		case ATPartitionSectorMode::Direct2x:
		case ATPartitionSectorMode::Direct4x:
			break;

		case ATPartitionSectorMode::InterleavedSectorBytes:
		case ATPartitionSectorMode::InterleavedSectorBytes2x:
		case ATPartitionSectorMode::PackedSectors:
		case ATPartitionSectorMode::PackedSectorsByteToWord:
			blockOffset >>= 1;
			break;
	}

	return blockOffset + mPartitionInfo.mBlockStart;
}

ATPartitionDiskView::SectorByteMapping ATPartitionDiskView::GetSectorByteMapping(uint32 sectorIndex) const {
	SectorByteMapping mapping {};
	mapping.mOffset = 0;
	mapping.mStride = 1;

	switch(mPartitionInfo.mSectorMode) {
		case ATPartitionSectorMode::Direct:
		default:
			break;

		case ATPartitionSectorMode::Direct2x:
			mapping.mStride = 2;
			break;

		case ATPartitionSectorMode::Direct4x:
			mapping.mStride = 4;
			break;

		case ATPartitionSectorMode::InterleavedSectorBytes:
			mapping.mOffset = sectorIndex & 1;
			mapping.mStride = 2;
			break;

		case ATPartitionSectorMode::InterleavedSectorBytes2x:
			mapping.mOffset = sectorIndex & 1;
			mapping.mStride = 4;
			break;

		case ATPartitionSectorMode::PackedSectors:
			if (sectorIndex & 1)
				mapping.mOffset = 256;

			break;

		case ATPartitionSectorMode::PackedSectorsByteToWord:
			if (sectorIndex & 1)
				mapping.mOffset = 256;

			mapping.mStride = 2;
			break;
	}

	return mapping;
}

