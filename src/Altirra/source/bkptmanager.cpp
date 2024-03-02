//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
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
#include "bkptmanager.h"
#include "cpu.h"
#include "memorymanager.h"
#include "simulator.h"

ATBreakpointManager::ATBreakpointManager()
	: mpMemMgr(NULL)
{
	memset(mAttrib, 0, sizeof mAttrib);
}

ATBreakpointManager::~ATBreakpointManager() {
}

void ATBreakpointManager::Init(ATCPUEmulator *cpu, ATMemoryManager *memmgr, ATSimulator *sim) {
	mpCPU = cpu;
	mpMemMgr = memmgr;
	mpSim = sim;
	cpu->SetBreakpointManager(this);
}

void ATBreakpointManager::Shutdown() {
	if (mpMemMgr) {
		for(AccessBPLayers::const_iterator it(mAccessBPLayers.begin()), itEnd(mAccessBPLayers.end()); it != itEnd; ++it) {
			mpMemMgr->DeleteLayer(it->second.mpMemLayer);
		}

		mpMemMgr = NULL;
	}

	mAccessBPLayers.clear();

	if (mpCPU) {
		mpCPU->SetBreakpointManager(NULL);
		mpCPU = NULL;
	}
}

void ATBreakpointManager::GetAll(ATBreakpointIndices& indices) const {
	uint32 idx = 0;

	for(Breakpoints::const_iterator it(mBreakpoints.begin()), itEnd(mBreakpoints.end()); it != itEnd; ++it) {
		const BreakpointEntry& be = *it;

		if (be.mType)
			indices.push_back(idx);

		++idx;
	}
}

void ATBreakpointManager::GetAtPC(uint16 pc, ATBreakpointIndices& indices) const {
	BreakpointsByAddress::const_iterator it(mCPUBreakpoints.find(pc));

	if (it == mCPUBreakpoints.end()) {
		indices.clear();
		return;
	}
	
	indices = it->second;
}

void ATBreakpointManager::GetAtAccessAddress(uint32 addr, ATBreakpointIndices& indices) const {
	BreakpointsByAddress::const_iterator it(mAccessBreakpoints.find(addr));

	if (it == mAccessBreakpoints.end()) {
		indices.clear();
		return;
	}
	
	indices = it->second;
}

bool ATBreakpointManager::GetInfo(uint32 idx, ATBreakpointInfo& info) const {
	if (!idx || idx > mBreakpoints.size())
		return false;

	const BreakpointEntry& be = mBreakpoints[idx - 1];

	if (!be.mType)
		return false;

	info.mAddress = be.mAddress;
	info.mLength = 1;
	info.mbBreakOnPC = (be.mType & kBPT_PC) != 0;
	info.mbBreakOnRead = (be.mType & kBPT_Read) != 0;
	info.mbBreakOnWrite = (be.mType & kBPT_Write) != 0;

	if (be.mType & kBPT_Range) {
		std::pair<AccessRangeBreakpoints::const_iterator, AccessRangeBreakpoints::const_iterator> r(std::equal_range(mAccessRangeBreakpoints.begin(), mAccessRangeBreakpoints.end(), info.mAddress, BreakpointRangeAddressPred()));

		for(; r.first != r.second; ++r.first) {
			const BreakpointRangeEntry& bre = *r.first;

			if (bre.mIndex == idx) {
				info.mLength = bre.mLength;
				break;
			}
		}
	}

	return true;
}

bool ATBreakpointManager::IsSetAtPC(uint16 pc) const {
	return mCPUBreakpoints.find(pc) != mCPUBreakpoints.end();
}

void ATBreakpointManager::ClearAtPC(uint16 pc) {
	BreakpointsByAddress::iterator it(mCPUBreakpoints.find(pc));

	if (it == mCPUBreakpoints.end())
		return;

	ATBreakpointIndices indices(it->second);

	while(!indices.empty()) {
		Clear(indices.back());
		indices.pop_back();
	}
}

uint32 ATBreakpointManager::SetAtPC(uint16 pc) {
	uint32 idx = std::find_if(mBreakpoints.begin(), mBreakpoints.end(), BreakpointFreePred()) - mBreakpoints.begin();

	if (idx >= mBreakpoints.size())
		mBreakpoints.push_back();

	BreakpointEntry& be = mBreakpoints[idx++];
	be.mAddress = pc;
	be.mType = kBPT_PC;

	BreakpointsByAddress::insert_return_type r(mCPUBreakpoints.insert(pc));

	if (r.second)
		mpCPU->SetBreakpoint(pc);

	r.first->second.push_back(idx);
	return idx;
}

uint32 ATBreakpointManager::SetAccessBP(uint16 address, bool read, bool write) {
	VDASSERT(read || write);

	uint32 idx = std::find_if(mBreakpoints.begin(), mBreakpoints.end(), BreakpointFreePred()) - mBreakpoints.begin();

	if (idx >= mBreakpoints.size())
		mBreakpoints.push_back();

	BreakpointEntry& be = mBreakpoints[idx++];
	be.mAddress = address;
	be.mType = (read ? kBPT_Read : 0) + (write ? kBPT_Write : 0);

	BreakpointsByAddress::insert_return_type r(mAccessBreakpoints.insert(address));
	r.first->second.push_back(idx);

	RegisterAccessPage(address & 0xff00, read, write);

	// set attribute flags on address
	uint8 attrFlags = 0;
	if (read)
		attrFlags |= kAttribReadBkpt;

	if (write)
		attrFlags |= kAttribWriteBkpt;

	mAttrib[address] |= attrFlags;

	return idx;	
}

uint32 ATBreakpointManager::SetAccessRangeBP(uint16 address, uint32 len, bool read, bool write) {
	VDASSERT(read || write);

	if (address + len > 0x10000)
		len = 0x10000 - address;

	// create breakpoint entry
	uint32 idx = std::find_if(mBreakpoints.begin(), mBreakpoints.end(), BreakpointFreePred()) - mBreakpoints.begin();

	if (idx >= mBreakpoints.size())
		mBreakpoints.push_back();

	BreakpointEntry& be = mBreakpoints[idx++];
	be.mAddress = address;
	be.mType = (read ? kBPT_Read : 0) + (write ? kBPT_Write : 0) + kBPT_Range;

	// create range breakpoint entry
	BreakpointRangeEntry bre = {};
	bre.mAddress = address;
	bre.mLength = len;
	bre.mIndex = idx;
	bre.mAttrFlags = (read ? kAttribRangeReadBkpt : 0) + (write ? kAttribRangeWriteBkpt : 0);

	mAccessRangeBreakpoints.insert(std::lower_bound(mAccessRangeBreakpoints.begin(), mAccessRangeBreakpoints.end(), address, BreakpointRangeAddressPred()), bre);

	RecomputeRangePriorLimits();

	// register all access pages
	uint32 page1 = address & 0xff00;
	uint32 page2 = (address + len - 1) & 0xff00;

	for(uint32 page = page1; page <= page2; page += 0x100)
		RegisterAccessPage(page, read, write);

	// set attribute flags on all bytes in range
	for(uint32 i = 0; i < len; ++i)
		mAttrib[address + i] |= bre.mAttrFlags;

	return idx;
}

bool ATBreakpointManager::Clear(uint32 id) {
	if (!id || id > mBreakpoints.size())
		return false;

	BreakpointEntry& be = mBreakpoints[id - 1];

	if (!be.mType)
		return false;

	const uint32 address = be.mAddress;

	if (be.mType & (kBPT_Read | kBPT_Write)) {
		if (be.mType & kBPT_Range) {
			const bool read = (be.mType & kBPT_Read) != 0;
			const bool write = (be.mType & kBPT_Write) != 0;

			// find range breakpoint entry
			std::pair<AccessRangeBreakpoints::iterator, AccessRangeBreakpoints::iterator> r(std::equal_range(mAccessRangeBreakpoints.begin(), mAccessRangeBreakpoints.end(), be.mAddress, BreakpointRangeAddressPred()));

			bool bprfound = false;
			for(; r.first != r.second; ++r.first) {
				BreakpointRangeEntry& bre = *r.first;

				if (bre.mIndex == id) {
					const uint32 len = bre.mLength;

					// Decrement refcount over page range.
					uint32 page1 = address & 0xff00;
					uint32 page2 = (address + len - 1) & 0xff00;

					for(uint32 page = page1; page <= page2; page += 0x100)
						UnregisterAccessPage(page, read, write);

					// Delete range entry.
					mAccessRangeBreakpoints.erase(r.first);

					RecomputeRangePriorLimits();

					// Clear attribute flags in range.
					const uint32 limit = address + len;

					VDASSERT(limit <= 0x10000);

					for(uint32 i = 0; i < len; ++i)
						mAttrib[i] &= ~(kAttribRangeReadBkpt | kAttribRangeWriteBkpt);

					// Reapply attribute flags for any other existing range breakpoints.
					AccessRangeBreakpoints::const_iterator itRemRange(std::upper_bound(mAccessRangeBreakpoints.begin(), mAccessRangeBreakpoints.end(), limit, BreakpointRangeAddressPred()));
					AccessRangeBreakpoints::const_iterator itRemRangeBegin(mAccessRangeBreakpoints.begin());

					while(itRemRange != itRemRangeBegin) {
						--itRemRange;
						const BreakpointRangeEntry& remRange = *itRemRange;

						// compute intersecting range
						uint32 remad1 = remRange.mAddress;
						uint32 remad2 = remRange.mAddress + remRange.mLength;

						if (remad1 < address)
							remad1 = address;

						if (remad2 > limit)
							remad2 = limit;

						// reapply attribute flags
						const uint8 remaf = remRange.mAttrFlags;

						for(uint32 remad = remad1; remad < remad2; ++remad)
							mAttrib[remad] |= remaf;

						// early out if we don't need to go any farther
						if (remRange.mPriorLimit <= address)
							break;
					}

					bprfound = true;
					break;
				}
			}

			if (!bprfound) {
				VDASSERT(!"Range breakpoint is missing range entry.");
			}
		} else {
			UnregisterAccessPage(address & 0xff00, (be.mType & kBPT_Read) != 0, (be.mType & kBPT_Write) != 0);

			BreakpointsByAddress::iterator itAddr(mAccessBreakpoints.find(address));
			VDASSERT(itAddr != mAccessBreakpoints.end());

			BreakpointIndices& indices = itAddr->second;
			BreakpointIndices::iterator itIndex(std::find(indices.begin(), indices.end(), id));
			VDASSERT(itIndex != indices.end());

			indices.erase(itIndex);

			// recompute attributes for address
			uint8 attr = 0;
			for(itIndex = indices.begin(); itIndex != indices.end(); ++itIndex) {
				const BreakpointEntry& be = mBreakpoints[*itIndex - 1];

				if (be.mType & kBPT_Read)
					attr |= kAttribReadBkpt;

				if (be.mType & kBPT_Write)
					attr |= kAttribWriteBkpt;
			}

			mAttrib[address] = (mAttrib[address] & ~(kBPT_Read | kBPT_Write)) + attr;
		}
	}

	if (be.mType & kBPT_PC) {
		BreakpointsByAddress::iterator it(mCPUBreakpoints.find(address));
		VDASSERT(it != mCPUBreakpoints.end());

		BreakpointIndices& indices = it->second;
		BreakpointIndices::iterator itIndex(std::find(indices.begin(), indices.end(), id));
		VDASSERT(itIndex != indices.end());

		indices.erase(itIndex);

		if (indices.empty()) {
			mCPUBreakpoints.erase(it);
			mpCPU->ClearBreakpoint(address);
		}
	}

	be.mType = 0;

	return true;
}

void ATBreakpointManager::ClearAll() {
	uint32 n = mBreakpoints.size();

	for(uint32 i=0; i<n; ++i) {
		if (mBreakpoints[i].mType)
			Clear(i+1);
	}
}

void ATBreakpointManager::RecomputeRangePriorLimits() {
	uint32 priorLimit = 0;

	for(AccessRangeBreakpoints::iterator it(mAccessRangeBreakpoints.begin()), itEnd(mAccessRangeBreakpoints.end());
		it != itEnd;
		++it)
	{
		BreakpointRangeEntry& entry = *it;

		entry.mPriorLimit = priorLimit;

		const uint32 limit = entry.mAddress + entry.mLength;
		if (limit > priorLimit)
			priorLimit = limit;
	}
}

void ATBreakpointManager::RegisterAccessPage(uint32 address, bool read, bool write) {
	AccessBPLayers::insert_return_type r2(mAccessBPLayers.insert(address & 0xff00));
	AccessBPLayer& layer = r2.first->second;
	if (r2.second) {
		ATMemoryHandlerTable handlers = {};
		handlers.mbPassAnticReads = true;
		handlers.mbPassReads = true;
		handlers.mbPassWrites = true;
		handlers.mpDebugReadHandler = NULL;
		handlers.mpReadHandler = OnAccessTrapRead;
		handlers.mpWriteHandler = OnAccessTrapWrite;
		handlers.mpThis = this;
		layer.mRefCountRead = 0;
		layer.mRefCountWrite = 0;
		layer.mpMemLayer = mpMemMgr->CreateLayer(kATMemoryPri_AccessBP, handlers, address >> 8, 1);
	}

	if (read) {
		if (!layer.mRefCountRead++)
			mpMemMgr->EnableLayer(layer.mpMemLayer, kATMemoryAccessMode_CPURead, true);
	}

	if (write) {
		if (!layer.mRefCountWrite++)
			mpMemMgr->EnableLayer(layer.mpMemLayer, kATMemoryAccessMode_CPUWrite, true);
	}
}

void ATBreakpointManager::UnregisterAccessPage(uint32 address, bool read, bool write) {
	AccessBPLayers::iterator it(mAccessBPLayers.find(address & 0xff00));
	VDASSERT(it != mAccessBPLayers.end());

	AccessBPLayer& layer = it->second;

	if (read) {
		VDASSERT(layer.mRefCountRead);
		if (!--layer.mRefCountRead)
			mpMemMgr->EnableLayer(layer.mpMemLayer, kATMemoryAccessMode_CPURead, false);
	}

	if (write) {
		VDASSERT(layer.mRefCountWrite);
		if (!--layer.mRefCountWrite)
			mpMemMgr->EnableLayer(layer.mpMemLayer, kATMemoryAccessMode_CPUWrite, false);
	}

	if (!(layer.mRefCountRead | layer.mRefCountWrite)) {
		mpMemMgr->DeleteLayer(layer.mpMemLayer);
		mAccessBPLayers.erase(it);
	}
}

int ATBreakpointManager::CheckPCBreakpoints(uint32 pc, const BreakpointIndices& bpidxs) {
	bool shouldBreak = false;
	bool noisyBreak = false;

	for(BreakpointIndices::const_iterator it(bpidxs.begin()), itEnd(bpidxs.end()); it != itEnd; ++it) {
		const uint32 idx = *it;
		const BreakpointEntry& bpe = mBreakpoints[idx - 1];

		ATBreakpointEvent ev;
		ev.mIndex = idx;
		ev.mAddress = pc;
		ev.mValue = 0;
		ev.mbBreak = false;
		ev.mbSilentBreak = false;

		mEventBreakpointHit.Raise(this, &ev);

		if (ev.mbBreak) {
			shouldBreak = true;
			if (!ev.mbSilentBreak)
				noisyBreak = true;
		}
	}

	return shouldBreak ? noisyBreak ? kATSimEvent_CPUPCBreakpoint : kATSimEvent_AnonymousInterrupt : kATSimEvent_None;
}

sint32 ATBreakpointManager::OnAccessTrapRead(void *thisptr0, uint32 addr) {
	ATBreakpointManager *thisptr = (ATBreakpointManager *)thisptr0;
	const uint8 attr = thisptr->mAttrib[addr];

	if (!(attr & (kAttribReadBkpt | kAttribRangeReadBkpt)))
		return -1;

	bool shouldBreak = false;
	bool noisyBreak = false;

	if (attr & kAttribReadBkpt) {
		BreakpointIndices& bpidxs = thisptr->mAccessBreakpoints.find(addr)->second;

		for(BreakpointIndices::const_iterator it(bpidxs.begin()), itEnd(bpidxs.end()); it != itEnd; ++it) {
			const uint32 idx = *it;
			const BreakpointEntry& bpe = thisptr->mBreakpoints[idx - 1];

			if (!(bpe.mType & kBPT_Read))
				continue;

			ATBreakpointEvent ev;
			ev.mIndex = idx;
			ev.mAddress = addr;
			ev.mValue = 0;
			ev.mbBreak = false;
			ev.mbSilentBreak = false;

			thisptr->mEventBreakpointHit.Raise(thisptr, &ev);

			if (ev.mbBreak) {
				shouldBreak = true;
				if (!ev.mbSilentBreak)
					noisyBreak = true;
			}
		}
	}

	if (attr & kAttribRangeReadBkpt) {
		AccessRangeBreakpoints::const_iterator it2(std::upper_bound(thisptr->mAccessRangeBreakpoints.begin(), thisptr->mAccessRangeBreakpoints.end(), addr, BreakpointRangeAddressPred()));
		AccessRangeBreakpoints::const_iterator it2Begin(thisptr->mAccessRangeBreakpoints.begin());
		while(it2 != it2Begin) {
			--it2;

			const BreakpointRangeEntry& bre = *it2;

			if ((bre.mAttrFlags & kAttribRangeReadBkpt) && (addr - bre.mAddress) < bre.mLength) {
				const uint32 idx = bre.mIndex;
				const BreakpointEntry& bpe = thisptr->mBreakpoints[bre.mIndex - 1];

				ATBreakpointEvent ev;
				ev.mIndex = idx;
				ev.mAddress = addr;
				ev.mValue = 0;
				ev.mbBreak = false;
				ev.mbSilentBreak = false;

				thisptr->mEventBreakpointHit.Raise(thisptr, &ev);

				if (ev.mbBreak) {
					shouldBreak = true;
					if (!ev.mbSilentBreak)
						noisyBreak = true;
				}
			}

			if (bre.mPriorLimit <= addr)
				break;
		}
	}

	if (shouldBreak)
		thisptr->mpSim->PostInterruptingEvent(noisyBreak ? kATSimEvent_ReadBreakpoint : kATSimEvent_AnonymousInterrupt);

	return -1;
}

bool ATBreakpointManager::OnAccessTrapWrite(void *thisptr0, uint32 addr, uint8 value) {
	ATBreakpointManager *thisptr = (ATBreakpointManager *)thisptr0;
	const uint8 attr = thisptr->mAttrib[addr];

	if (!(attr & (kAttribWriteBkpt | kAttribRangeWriteBkpt)))
		return false;

	bool shouldBreak = false;
	bool noisyBreak = false;

	if (attr & kAttribWriteBkpt) {
		BreakpointIndices& bpidxs = thisptr->mAccessBreakpoints.find(addr)->second;

		for(BreakpointIndices::const_iterator it(bpidxs.begin()), itEnd(bpidxs.end()); it != itEnd; ++it) {
			const uint32 idx = *it;
			const BreakpointEntry& bpe = thisptr->mBreakpoints[idx - 1];

			if (!(bpe.mType & kBPT_Write))
				continue;

			ATBreakpointEvent ev;
			ev.mIndex = idx;
			ev.mAddress = addr;
			ev.mValue = value;
			ev.mbBreak = false;
			ev.mbSilentBreak = false;

			thisptr->mEventBreakpointHit.Raise(thisptr, &ev);

			if (ev.mbBreak) {
				shouldBreak = true;
				if (!ev.mbSilentBreak)
					noisyBreak = true;
			}
		}
	}

	if (attr & kAttribRangeWriteBkpt) {
		AccessRangeBreakpoints::const_iterator it2(std::upper_bound(thisptr->mAccessRangeBreakpoints.begin(), thisptr->mAccessRangeBreakpoints.end(), addr, BreakpointRangeAddressPred()));
		AccessRangeBreakpoints::const_iterator it2Begin(thisptr->mAccessRangeBreakpoints.begin());
		while(it2 != it2Begin) {
			--it2;

			const BreakpointRangeEntry& bre = *it2;

			if ((bre.mAttrFlags & kAttribRangeWriteBkpt) && (addr - bre.mAddress) < bre.mLength) {
				const uint32 idx = bre.mIndex;
				const BreakpointEntry& bpe = thisptr->mBreakpoints[bre.mIndex - 1];

				ATBreakpointEvent ev;
				ev.mIndex = idx;
				ev.mAddress = addr;
				ev.mValue = value;
				ev.mbBreak = false;
				ev.mbSilentBreak = false;

				thisptr->mEventBreakpointHit.Raise(thisptr, &ev);

				if (ev.mbBreak) {
					shouldBreak = true;
					if (!ev.mbSilentBreak)
						noisyBreak = true;
				}
			}

			if (bre.mPriorLimit <= addr)
				break;
		}
	}

	if (shouldBreak)
		thisptr->mpSim->PostInterruptingEvent(noisyBreak ? kATSimEvent_WriteBreakpoint : kATSimEvent_AnonymousInterrupt);

	return false;
}
