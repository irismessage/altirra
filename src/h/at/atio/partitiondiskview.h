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

#ifndef f_AT_ATIO_PARTITIONDISKVIEW_H
#define f_AT_ATIO_PARTITIONDISKVIEW_H

#include <vd2/system/refcount.h>
#include <at/atio/diskimage.h>
#include <at/atio/partitiontable.h>

class IATBlockDevice;

class ATPartitionDiskView : public vdrefcounted<IATDiskImage> {
	ATPartitionDiskView(const ATPartitionDiskView&) = delete;
	ATPartitionDiskView& operator=(const ATPartitionDiskView&) = delete;
public:
	ATPartitionDiskView(IATBlockDevice& dev, const ATPartitionInfo& partitionInfo);
	~ATPartitionDiskView();

	void *AsInterface(uint32 iid);

	ATImageType GetImageType() const override;
	std::optional<uint32> GetImageFileCRC() const override;
	std::optional<ATChecksumSHA256> GetImageFileSHA256() const override;

	ATDiskTimingMode GetTimingMode() const override;
	bool IsDirty() const override;
	bool IsUpdatable() const override;
	bool IsDynamic() const override;
	ATDiskImageFormat GetImageFormat() const override;
	void Flush() override;
	uint64 GetImageChecksum() const override;
	void SetPath(const wchar_t *path, ATDiskImageFormat format) override;
	void Save(const wchar_t *path, ATDiskImageFormat format) override;
	ATDiskGeometryInfo GetGeometry() const override;
	uint32 GetSectorSize() const override;
	uint32 GetSectorSize(uint32 virtIndex) const override;
	uint32 GetBootSectorCount() const override;
	uint32 GetPhysicalSectorCount() const override;
	void GetPhysicalSectorInfo(uint32 index, ATDiskPhysicalSectorInfo& info) const override;
	void ReadPhysicalSector(uint32 index, void *data, uint32 len) override;
	void WritePhysicalSector(uint32 index, const void *data, uint32 len, uint8 fdcStatus) override;
	uint32 GetVirtualSectorCount() const override;
	void GetVirtualSectorInfo(uint32 index, ATDiskVirtualSectorInfo& info) const override;
	uint32 ReadVirtualSector(uint32 index, void *data, uint32 len) override;
	bool WriteVirtualSector(uint32 index, const void *data, uint32 len) override;
	void Resize(uint32 sectors) override;
	void FormatTrack(uint32 vsIndexStart, uint32 vsCount, const ATDiskVirtualSectorInfo *vsecs, uint32 psCount, const ATDiskPhysicalSectorInfo *psecs, const uint8 *psecData) override;
	bool IsSafeToReinterleave() const override;
	void Reinterleave(ATDiskInterleave interleave) override;

private:
	struct SectorByteMapping {
		uint32 mOffset;
		uint32 mStride;
	};

	bool IsRMWRequired() const;
	uint32 GetLBAForSector(uint32 sectorIndex) const;
	SectorByteMapping GetSectorByteMapping(uint32 sectorIndex) const;

	ATPartitionInfo mPartitionInfo;
	ATDiskGeometryInfo mGeometryInfo;
	vdrefptr<IATBlockDevice> mpBlockDevice;
};

#endif
