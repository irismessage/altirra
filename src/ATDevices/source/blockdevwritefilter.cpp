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

#include <stdafx.h>
#include <vd2/system/vdalloc.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/snapshotimpl.h>
#include <at/atcore/savestate.h>
#include <at/atdevices/blockdevwritefilter.h>

////////////////////////////////////////////////////////////////////////////

class ATSaveStateTempWriteFilter final : public ATSnapExchangeObject<ATSaveStateTempWriteFilter, "ATSaveStateTempWriteFilter"> {
public:
	template<ATExchanger T>
	void Exchange(T& ex);

	void PostDeserialize() override;

	vdrefptr<ATSaveStateMemoryBuffer> mpModifiedBlockStore;
	vdfastvector<uint32> mModifiedBlockLBAs;
};

template<ATExchanger T>
void ATSaveStateTempWriteFilter::Exchange(T& ex) {
	ex.Transfer("modified_block_store", &mpModifiedBlockStore);
	ex.Transfer("modified_block_lbas", &mModifiedBlockLBAs);
}

void ATSaveStateTempWriteFilter::PostDeserialize() {
	if (mpModifiedBlockStore) {
		if (mModifiedBlockLBAs.size() * 512 != mpModifiedBlockStore->GetReadBuffer().size())
			throw ATInvalidSaveStateException();
	} else {
		if (!mModifiedBlockLBAs.empty())
			throw ATInvalidSaveStateException();
	}
}

////////////////////////////////////////////////////////////////////////////

void ATCreateDeviceBlockDevTemporaryWriteFilter(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATBlockDeviceTemporaryWriteFilter> vdev(new ATBlockDeviceTemporaryWriteFilter);

	vdev->SetSettings(pset);

	*dev = vdev.release();
}

extern const ATDeviceDefinition g_ATDeviceDefBlockDevTemporaryWriteFilter = {
	"hdtempwritefilter",
	nullptr,
	L"Temporary write filter",
	ATCreateDeviceBlockDevTemporaryWriteFilter
};

ATBlockDeviceTemporaryWriteFilter::ATBlockDeviceTemporaryWriteFilter() {
	mDevParent.Init(IATBlockDevice::kTypeID, "harddisk", L"Read-only base disk", "source", static_cast<ATDevice *>(this));

	mDevParent.SetOnAttach([this] {
		mpBlockDevice = mDevParent.GetChild<IATBlockDevice>();
	});

	mDevParent.SetOnDetach([this] {
		mpBlockDevice = nullptr;
	});
}

ATBlockDeviceTemporaryWriteFilter::~ATBlockDeviceTemporaryWriteFilter() {
	Shutdown();
}

int ATBlockDeviceTemporaryWriteFilter::AddRef() {
	return ATDevice::AddRef();
}

int ATBlockDeviceTemporaryWriteFilter::Release() {
	return ATDevice::Release();
}

void *ATBlockDeviceTemporaryWriteFilter::AsInterface(uint32 iid) {
	switch(iid) {
		case IATBlockDevice::kTypeID: return static_cast<IATBlockDevice *>(this);
		case IATDeviceParent::kTypeID: return static_cast<IATDeviceParent *>(&mDevParent);
		case IATDeviceSnapshot::kTypeID: return static_cast<IATDeviceSnapshot *>(this);
		default:
			return ATDevice::AsInterface(iid);
	}
}

void ATBlockDeviceTemporaryWriteFilter::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefBlockDevTemporaryWriteFilter;
}

void ATBlockDeviceTemporaryWriteFilter::GetSettingsBlurb(VDStringW& buf) {
	unsigned n = (unsigned)mBlockLookup.size();
	buf.sprintf(L"%u dirty sector%s", n, n != 1 ? L"s" : L"");
}

bool ATBlockDeviceTemporaryWriteFilter::IsReadOnly() const {
	return false;
}

uint32 ATBlockDeviceTemporaryWriteFilter::GetSectorCount() const {
	return mpBlockDevice ? mpBlockDevice->GetSectorCount() : 0;
}

ATBlockDeviceGeometry ATBlockDeviceTemporaryWriteFilter::GetGeometry() const {
	return mpBlockDevice ? mpBlockDevice->GetGeometry() : ATBlockDeviceGeometry{};
}

uint32 ATBlockDeviceTemporaryWriteFilter::GetSerialNumber() const {
	return mpBlockDevice ? mpBlockDevice->GetSerialNumber() : 0;
}

void ATBlockDeviceTemporaryWriteFilter::Init() {
}

void ATBlockDeviceTemporaryWriteFilter::Shutdown() {
	mDevParent.Shutdown();

	Clear();
}

void ATBlockDeviceTemporaryWriteFilter::Flush() {
}

void ATBlockDeviceTemporaryWriteFilter::ReadSectors(void *data, uint32 lba, uint32 n) {
	while(n--) {
		auto it = mBlockLookup.find(lba);

		if (it != mBlockLookup.end()) {
			uint32 slotIndex = it->second;

			memcpy(data, GetSlot(slotIndex), 512);
		} else if (mpBlockDevice) {
			mpBlockDevice->ReadSectors(data, lba, 1);
		} else {
			memset(data, 0, 512);
		}

		++lba;
		data = (char *)data + 512;
	}
}

void ATBlockDeviceTemporaryWriteFilter::WriteSectors(const void *data, uint32 lba, uint32 n) {
	while(n--) {
		auto r = mBlockLookup.insert(lba);

		if (r.second) {
			if (!(mNextSlot & kBlocksPerGroup)) {
				vdautoptr newGroup { new BlockGroup };

				mBlockGroups.push_back(newGroup);
				newGroup.release();
			}

			r.first->second = mNextSlot++;
		}

		const uint32 slotIndex = r.first->second;
		memcpy(GetSlot(slotIndex), data, 512);

		++lba;
		data = (char *)data + 512;
	}
}

void ATBlockDeviceTemporaryWriteFilter::LoadState(const IATObjectState *state, ATSnapshotContext& ctx) {
	if (ctx.mbSkipStorage)
		return;

	if (!state) {
		Clear();
		return;
	}

	const ATSaveStateTempWriteFilter& wfstate = atser_cast<const ATSaveStateTempWriteFilter&>(*state);

	if (!wfstate.mpModifiedBlockStore) {
		// we are requested to load storage, but the snapshot was saved without it -- so leave
		// the existing storage in place
		return;
	}

	// write the modified sectors into the local store; the serialization routine
	// has already validated that the buffer/LBA lengths match, so we're good there
	const uint8 *src = wfstate.mpModifiedBlockStore->GetReadBuffer().data();

	for(const auto lba : wfstate.mModifiedBlockLBAs) {
		WriteSectors(src, lba, 1);
		src += 512;
	}
}

vdrefptr<IATObjectState> ATBlockDeviceTemporaryWriteFilter::SaveState(ATSnapshotContext& ctx) const {
	vdrefptr state { new ATSaveStateTempWriteFilter };

	if (!ctx.mbSkipStorage) {
		state->mpModifiedBlockStore = new ATSaveStateMemoryBuffer;
		state->mpModifiedBlockStore->mpDirectName = L"modifiedblocks.bin";

		auto& lbas = state->mModifiedBlockLBAs;
		auto& buffer = state->mpModifiedBlockStore->GetWriteBuffer();

		lbas.reserve(mBlockLookup.size());
		buffer.resize(mBlockLookup.size() * 512);

		uint8 *dst = buffer.data();

		for(auto&& [lba, slotIndex] : mBlockLookup) {
			state->mModifiedBlockLBAs.emplace_back(lba);
			
			memcpy(dst, GetSlot(slotIndex), 512);
			dst += 512;
		}
	}

	return state;
}

void ATBlockDeviceTemporaryWriteFilter::Clear() {
	mBlockLookup.clear();

	while(!mBlockGroups.empty()) {
		delete mBlockGroups.back();

		mBlockGroups.pop_back();
	}
}

uint8 (&ATBlockDeviceTemporaryWriteFilter::GetSlot(uint32 slotIndex) const)[512] {
	return (*mBlockGroups[slotIndex >> kBlockGroupShift]).mSlots[slotIndex & kBlockSlotMask];
}
