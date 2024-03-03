//	Altirra - Atari 800/800XL/5200 emulator
//	Device emulation library - block device write filter
//	Copyright (C) 2009-2021 Avery Lee
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

#ifndef f_AT_ATDEVICES_BLOCKDEVWRITEFILTER_H
#define f_AT_ATDEVICES_BLOCKDEVWRITEFILTER_H

#include <vd2/system/vdtypes.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/blockdevice.h>
#include <at/atcore/deviceparentimpl.h>
#include <at/atcore/devicesnapshot.h>

// ATBlockDeviceTemporaryWriteFilter
//
// Filters writes to an underlying block device to provide a writable, in-memory
// overlay on top of a read-only device.
//
class ATBlockDeviceTemporaryWriteFilter final : public ATDevice, public IATBlockDevice, public IATDeviceSnapshot {
	ATBlockDeviceTemporaryWriteFilter(const ATBlockDeviceTemporaryWriteFilter&) = delete;
	ATBlockDeviceTemporaryWriteFilter& operator=(const ATBlockDeviceTemporaryWriteFilter&) = delete;

public:
	ATBlockDeviceTemporaryWriteFilter();
	~ATBlockDeviceTemporaryWriteFilter();

public:
	int AddRef() override;
	int Release() override;
	void *AsInterface(uint32 iid) override;

public:
	void Init() override;
	void Shutdown() override;
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettingsBlurb(VDStringW& buf) override;

public:	// IATBlockDevice
	bool IsReadOnly() const override;
	uint32 GetSectorCount() const override;
	ATBlockDeviceGeometry GetGeometry() const override;
	uint32 GetSerialNumber() const override;

	void Flush() override;

	void ReadSectors(void *data, uint32 lba, uint32 n) override;
	void WriteSectors(const void *data, uint32 lba, uint32 n) override;

public:	// IATDeviceSnapshot
	virtual void LoadState(const IATObjectState *state, ATSnapshotContext& ctx) override;
	virtual vdrefptr<IATObjectState> SaveState(ATSnapshotContext& ctx) const override;

protected:
	static constexpr int kBlockGroupShift = 6;
	static constexpr uint32 kBlocksPerGroup = 1U << kBlockGroupShift;
	static constexpr uint32 kBlockSlotMask = kBlocksPerGroup - 1;

	struct BlockGroup {
		uint8 mSlots[1 << kBlockGroupShift][512];
	};

	void Clear();
	uint8 (&GetSlot(uint32 slotIndex) const)[512];

	uint32 mNextSlot = 0;
	vdfastvector<BlockGroup *> mBlockGroups;
	vdhashmap<uint32, uint32> mBlockLookup;

	vdrefptr<IATBlockDevice> mpBlockDevice;
	ATDeviceParentSingleChild mDevParent;
};

#endif
