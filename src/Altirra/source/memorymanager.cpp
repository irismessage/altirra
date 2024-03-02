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
#include "memorymanager.h"
#include "console.h"

ATMemoryManager::ATMemoryManager()
	: mpFreeNodes(NULL)
	, mbFloatingDataBus(false)
	, mbFastBusEnabled(false)
{
	for(int i=0; i<256; ++i) {
		mDummyReadPageTable[i] = (uintptr)&mDummyReadNode + 1;
		mDummyWritePageTable[i] = (uintptr)&mDummyWriteNode + 1;
	}

	mDummyLayer.mPriority = -1;
	mDummyLayer.mbEnabled[kATMemoryAccessMode_AnticRead] = true;
	mDummyLayer.mbEnabled[kATMemoryAccessMode_CPURead] = true;
	mDummyLayer.mbEnabled[kATMemoryAccessMode_CPUWrite] = true;
	mDummyLayer.mbReadOnly = false;
	mDummyLayer.mpBase = NULL;
	mDummyLayer.mAddrMask = 0xFFFFFFFF;
	mDummyLayer.mPageOffset = 0;
	mDummyLayer.mPageCount = 0x100;
	mDummyLayer.mHandlers.mbPassReads = false;
	mDummyLayer.mHandlers.mbPassAnticReads = false;
	mDummyLayer.mHandlers.mbPassWrites = false;
	mDummyLayer.mHandlers.mpThis = this;
	mDummyLayer.mHandlers.mpDebugReadHandler = DummyReadHandler;
	mDummyLayer.mHandlers.mpReadHandler = DummyReadHandler;
	mDummyLayer.mHandlers.mpWriteHandler = DummyWriteHandler;
	mDummyLayer.mpName = "Unconnected";

	mDummyReadNode.mpLayer = &mDummyLayer;
	mDummyReadNode.mpReadHandler = DummyReadHandler;
	mDummyReadNode.mNext = 1;
	mDummyReadNode.mpThis = this;
	mDummyWriteNode.mpLayer = &mDummyLayer;
	mDummyWriteNode.mpWriteHandler = DummyWriteHandler;
	mDummyWriteNode.mNext = 1;

	mpCPUReadBankMap = mReadBankTable;
	mpCPUWriteBankMap = mWriteBankTable;
	mpCPUReadPageMap = mCPUReadPageMap;
	mpCPUWritePageMap = mCPUWritePageMap;
}

ATMemoryManager::~ATMemoryManager() {
}

void ATMemoryManager::Init() {
	mbSimple_4000_7FFF[kATMemoryAccessMode_AnticRead] = false;
	mbSimple_4000_7FFF[kATMemoryAccessMode_CPURead] = false;
	mbSimple_4000_7FFF[kATMemoryAccessMode_CPUWrite] = false;

	for(uint32 i=0; i<256; ++i) {
		mAnticReadPageMap[i] = (uintptr)&mDummyReadNode + 1;
		mCPUReadPageMap[i] = (uintptr)&mDummyReadNode + 1;
		mCPUWritePageMap[i] = (uintptr)&mDummyWriteNode + 1;
	}

	mReadBankTable[0] = mCPUReadPageMap;
	mWriteBankTable[0] = mCPUWritePageMap;

	SetHighMemoryBanks(0);
}

void ATMemoryManager::SetHighMemoryBanks(sint32 banks) {
	if (banks < 0) {
		for(uint32 i=1; i<256; ++i) {
			mReadBankTable[i] = mCPUReadPageMap;
			mWriteBankTable[i] = mCPUWritePageMap;
		}
	} else {
		mHighMemory.resize(banks << 16);

		for(uint32 i=1; i<=(uint32)banks; ++i) {
			VDMemsetPointer(mHighMemoryPageTables[i], &mHighMemory[(i-1) * 0x10000], 256);

			mReadBankTable[i] = mHighMemoryPageTables[i];
			mWriteBankTable[i] = mHighMemoryPageTables[i];
		}

		for(uint32 i=banks+1; i<256; ++i) {
			mReadBankTable[i] = mDummyReadPageTable;
			mWriteBankTable[i] = mDummyWritePageTable;
		}
	}
}

void ATMemoryManager::SetFastBusEnabled(bool enabled) {
	if (mbFastBusEnabled == enabled)
		return;

	mbFastBusEnabled = enabled;

	RebuildNodes(&mCPUReadPageMap[0], 0, 256, kATMemoryAccessMode_CPURead);
	RebuildNodes(&mCPUWritePageMap[0], 0, 256, kATMemoryAccessMode_CPUWrite);
}

void ATMemoryManager::DumpStatus() {
	VDStringA s;

	for(Layers::const_iterator it(mLayers.begin()), itEnd(mLayers.end()); it != itEnd; ++it) {
		const MemoryLayer& layer = **it;

		uint32 pageStart = layer.mPageOffset;
		uint32 pageEnd = layer.mPageOffset + layer.mPageCount;

		if (pageStart < layer.mMaskRangeStart)
			pageStart = layer.mMaskRangeStart;

		if (pageEnd > layer.mMaskRangeEnd)
			pageEnd = layer.mMaskRangeEnd;

		if (pageStart < pageEnd) {
			s.sprintf("%06X-%06X", pageStart << 8, (pageEnd << 8) - 1);
		} else {
			s = "<masked>     ";
		}

		s.append_sprintf(" %2u%s %c%c%c  "
			, layer.mPriority
			, mbFastBusEnabled ? layer.mbFastBus ? " fast" : " chip" : ""
			, layer.mbEnabled[kATMemoryAccessMode_AnticRead] ? 'A' : '-'
			, layer.mbEnabled[kATMemoryAccessMode_CPURead] ? 'R' : '-'
			, layer.mbEnabled[kATMemoryAccessMode_CPUWrite] ? layer.mbReadOnly ? 'O' : 'W' : '-'
			);

		if (layer.mpBase) {
			s.append("direct memory");

			if (layer.mAddrMask != 0xFFFFFFFF)
				s.append_sprintf(" (mask %x)", layer.mAddrMask);
		} else {
			s += "hardware";
		}

		if (layer.mpName)
			s.append_sprintf(" [%s]", layer.mpName);

		s += '\n';
		ATConsoleWrite(s.c_str());
	}
}

ATMemoryLayer *ATMemoryManager::CreateLayer(int priority, const uint8 *base, uint32 pageOffset, uint32 pageCount, bool readOnly) {
	VDASSERT(!((uintptr)base & 1));

	MemoryLayer *layer = new MemoryLayer;

	layer->mPriority = priority;
	layer->mbEnabled[0] = false;
	layer->mbEnabled[1] = false;
	layer->mbEnabled[2] = false;
	layer->mbReadOnly = readOnly;
	layer->mbFastBus = false;
	layer->mpBase = base;
	layer->mPageOffset = pageOffset;
	layer->mPageCount = pageCount;
	layer->mHandlers.mbPassAnticReads = false;
	layer->mHandlers.mbPassReads = false;
	layer->mHandlers.mbPassWrites = false;
	layer->mHandlers.mpDebugReadHandler = ChipReadHandler;
	layer->mHandlers.mpReadHandler = ChipReadHandler;
	layer->mHandlers.mpWriteHandler = ChipWriteHandler;
	layer->mHandlers.mpThis = NULL;
	layer->mAddrMask = 0xFFFFFFFFU;
	layer->mpName = NULL;
	layer->mMaskRangeStart = 0x00;
	layer->mMaskRangeEnd = 0x100;

	mLayers.insert(std::lower_bound(mLayers.begin(), mLayers.end(), layer, MemoryLayerPred()), layer);
	return layer;
}

ATMemoryLayer *ATMemoryManager::CreateLayer(int priority, const ATMemoryHandlerTable& handlers, uint32 pageOffset, uint32 pageCount) {
	MemoryLayer *layer = new MemoryLayer;

	layer->mPriority = priority;
	layer->mbEnabled[0] = false;
	layer->mbEnabled[1] = false;
	layer->mbEnabled[2] = false;
	layer->mbReadOnly = false;
	layer->mbFastBus = false;
	layer->mpBase = NULL;
	layer->mPageOffset = pageOffset;
	layer->mPageCount = pageCount;
	layer->mHandlers = handlers;
	layer->mAddrMask = 0xFFFFFFFFU;
	layer->mpName = NULL;
	layer->mMaskRangeStart = 0x00;
	layer->mMaskRangeEnd = 0x100;

	mLayers.insert(std::lower_bound(mLayers.begin(), mLayers.end(), layer, MemoryLayerPred()), layer);
	return layer;
}

void ATMemoryManager::DeleteLayer(ATMemoryLayer *layer) {
	EnableLayer(layer, false);

	mLayers.erase(std::find(mLayers.begin(), mLayers.end(), layer));

	delete (MemoryLayer *)layer;
}

void ATMemoryManager::EnableLayer(ATMemoryLayer *layer, bool enable) {
	EnableLayer(layer, kATMemoryAccessMode_AnticRead, enable);
	EnableLayer(layer, kATMemoryAccessMode_CPURead, enable);
	EnableLayer(layer, kATMemoryAccessMode_CPUWrite, enable);
}

void ATMemoryManager::EnableLayer(ATMemoryLayer *layer0, ATMemoryAccessMode accessMode, bool enable) {
	MemoryLayer *const layer = static_cast<MemoryLayer *>(layer0);

	if (layer->mbEnabled[accessMode] == enable)
		return;

	layer->mbEnabled[accessMode] = enable;

	switch(accessMode) {
		case kATMemoryAccessMode_AnticRead:
			RebuildNodes(&mAnticReadPageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_AnticRead);
			break;

		case kATMemoryAccessMode_CPURead:
			RebuildNodes(&mCPUReadPageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_CPURead);
			break;

		case kATMemoryAccessMode_CPUWrite:
			RebuildNodes(&mCPUWritePageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_CPUWrite);
			break;
	}
}

void ATMemoryManager::SetLayerMemory(ATMemoryLayer *layer0, const uint8 *base) {
	MemoryLayer *const layer = static_cast<MemoryLayer *>(layer0);

	if (base != layer->mpBase) {
		layer->mpBase = base;

		uint32 rewriteOffset = layer->mPageOffset;
		uint32 rewriteCount = layer->mPageCount;

		if (layer->mbEnabled[kATMemoryAccessMode_AnticRead])
			RebuildNodes(&mAnticReadPageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_AnticRead);

		if (layer->mbEnabled[kATMemoryAccessMode_CPURead])
			RebuildNodes(&mCPUReadPageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_CPURead);

		if (layer->mbEnabled[kATMemoryAccessMode_CPUWrite])
			RebuildNodes(&mCPUWritePageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_CPUWrite);
	}
}

void ATMemoryManager::SetLayerMemory(ATMemoryLayer *layer0, const uint8 *base, uint32 pageOffset, uint32 pageCount, uint32 addrMask, int readOnly) {
	MemoryLayer *const layer = static_cast<MemoryLayer *>(layer0);

	bool ro = (readOnly >= 0) ? readOnly != 0 : layer->mbReadOnly;

	if (base != layer->mpBase || layer->mPageOffset != pageOffset || layer->mPageCount != pageCount || addrMask != layer->mAddrMask || ro != layer->mbReadOnly) {
		uint32 oldBegin = layer->mPageOffset;
		uint32 oldEnd = layer->mPageOffset + layer->mPageCount;

		layer->mpBase = base;
		layer->mAddrMask = addrMask;
		layer->mPageOffset = pageOffset;
		layer->mPageCount = pageCount;
		layer->mbReadOnly = ro;

		uint32 newBegin = layer->mPageOffset;
		uint32 newEnd = layer->mPageOffset + layer->mPageCount;

		uint32 rewriteOffset = std::min<uint32>(oldBegin, newBegin);
		uint32 rewriteCount = std::max<uint32>(oldEnd, newEnd) - rewriteOffset;

		if (layer->mbEnabled[kATMemoryAccessMode_AnticRead])
			RebuildNodes(&mAnticReadPageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_AnticRead);

		if (layer->mbEnabled[kATMemoryAccessMode_CPURead])
			RebuildNodes(&mCPUReadPageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_CPURead);

		if (layer->mbEnabled[kATMemoryAccessMode_CPUWrite])
			RebuildNodes(&mCPUWritePageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_CPUWrite);
	}
}

void ATMemoryManager::SetLayerAddressRange(ATMemoryLayer *layer0, uint32 pageOffset, uint32 pageCount) {
	MemoryLayer *const layer = static_cast<MemoryLayer *>(layer0);

	if (layer->mPageOffset != pageOffset || layer->mPageCount != pageCount) {
		uint32 oldBegin = layer->mPageOffset;
		uint32 oldEnd = layer->mPageOffset + layer->mPageCount;

		layer->mPageOffset = pageOffset;
		layer->mPageCount = pageCount;

		uint32 newBegin = layer->mPageOffset;
		uint32 newEnd = layer->mPageOffset + layer->mPageCount;

		uint32 rewriteOffset = std::min<uint32>(oldBegin, newBegin);
		uint32 rewriteCount = std::max<uint32>(oldEnd, newEnd) - rewriteOffset;

		if (layer->mbEnabled[kATMemoryAccessMode_AnticRead])
			RebuildNodes(&mAnticReadPageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_AnticRead);

		if (layer->mbEnabled[kATMemoryAccessMode_CPURead])
			RebuildNodes(&mCPUReadPageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_CPURead);

		if (layer->mbEnabled[kATMemoryAccessMode_CPUWrite])
			RebuildNodes(&mCPUWritePageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_CPUWrite);
	}
}

void ATMemoryManager::SetLayerName(ATMemoryLayer *layer0, const char *name) {
	MemoryLayer *const layer = static_cast<MemoryLayer *>(layer0);

	layer->mpName = name;
}

void ATMemoryManager::SetLayerFastBus(ATMemoryLayer *layer0, bool fast) {
	MemoryLayer *const layer = static_cast<MemoryLayer *>(layer0);

	if (layer->mbFastBus != fast) {
		layer->mbFastBus = fast;

		if (layer->mpBase) {
			const uint32 rewriteOffset = layer->mPageOffset;
			const uint32 rewriteCount = layer->mPageOffset + layer->mPageCount;

			RebuildNodes(&mCPUReadPageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_CPURead);
			RebuildNodes(&mCPUWritePageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_CPUWrite);
		}
	}
}

void ATMemoryManager::ClearLayerMaskRange(ATMemoryLayer *layer) {
	SetLayerMaskRange(layer, 0, 0x100);
}

void ATMemoryManager::SetLayerMaskRange(ATMemoryLayer *layer0, uint32 pageStart, uint32 pageCount) {
	MemoryLayer *const layer = static_cast<MemoryLayer *>(layer0);

	const uint32 pageEnd = pageStart + pageCount;

	if (layer->mMaskRangeStart == pageStart &&
		layer->mMaskRangeEnd == pageEnd)
	{
		return;
	}

	const auto rangeIntersect = [](uint32 a0, uint32 a1, uint32 b0, uint32 b1) {
		uint32 as = (uint32)abs((sint32)a0 - (sint32)a1);
		uint32 bs = (uint32)abs((sint32)b0 - (sint32)b1);

		return as && bs && (a0+a1) - (b0+b1) > (as+bs)*2;
	};

	// check if an update is needed -- this is true if the ranges covered by the
	// changes to either the start or end of the mask range intersect the layer
	const bool changed = rangeIntersect(pageStart, layer->mMaskRangeStart, layer->mPageOffset, layer->mPageOffset + layer->mPageCount)
		|| rangeIntersect(pageEnd, layer->mMaskRangeEnd, layer->mPageOffset, layer->mPageOffset + layer->mPageCount);

	layer->mMaskRangeStart = pageStart;
	layer->mMaskRangeEnd = pageEnd;

	if (changed) {
		if (layer->mbEnabled[kATMemoryAccessMode_AnticRead])
			RebuildNodes(&mAnticReadPageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_AnticRead);

		if (layer->mbEnabled[kATMemoryAccessMode_CPURead])
			RebuildNodes(&mCPUReadPageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_CPURead);

		if (layer->mbEnabled[kATMemoryAccessMode_CPUWrite])
			RebuildNodes(&mCPUWritePageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_CPUWrite);
	}
}

uint8 ATMemoryManager::ReadFloatingDataBus() const {
	if (mbFloatingDataBus)
		return mBusValue;

	// Star Raiders 5200 has a bug where some 800 code to read the joysticks via the PIA
	// wasn't removed, so it needs a non-zero value to be returned here.
	return 0xFF;
}

uint8 ATMemoryManager::AnticReadByte(uint32 address) {
	uintptr p = mAnticReadPageMap[(uint8)(address >> 8)];
	address &= 0xffff;

	while(ATCPUMEMISSPECIAL(p)) {
		const MemoryNode& node = *(const MemoryNode *)(p - 1);

		sint32 v = node.mpReadHandler(node.mpThis, address);
		if (v >= 0)
			return (uint8)v;

		p = node.mNext;
	}

	return ((uint8 *)p)[address];
}

uint8 ATMemoryManager::DebugAnticReadByte(uint16 address) {
	uintptr p = mAnticReadPageMap[(uint8)(address >> 8)];
	address &= 0xffff;

	while(ATCPUMEMISSPECIAL(p)) {
		const MemoryNode& node = *(const MemoryNode *)(p - 1);

		ATMemoryReadHandler handler = node.mpLayer->mHandlers.mpDebugReadHandler;
		if (handler) {
			sint32 v = handler(node.mpThis, address);
			if (v >= 0)
				return (uint8)v;
		}

		p = node.mNext;
	}

	return ((uint8 *)p)[address];
}

void ATMemoryManager::DebugAnticReadMemory(void *dst, uint16 address, uint32 len) {
	uint8 *dst8 = (uint8 *)dst;

	while(len--) {
		*dst8++ = DebugAnticReadByte(address++);
	}
}

uint8 ATMemoryManager::CPUReadByte(uint32 address) {
	uintptr p = mCPUReadPageMap[(uint8)(address >> 8)];
	address &= 0xffff;

	while(ATCPUMEMISSPECIAL(p)) {
		const MemoryNode& node = *(const MemoryNode *)(p - 1);

		sint32 v = node.mpReadHandler(node.mpThis, address);
		if (v >= 0)
			return (uint8)v;

		p = node.mNext;
	}

	return ((uint8 *)p)[address];
}

uint8 ATMemoryManager::CPUExtReadByte(uint16 address, uint8 bank) {
	uintptr p = mReadBankTable[bank][(uint8)(address >> 8)];
	const uint32 addr32 = (uint32)address + ((uint32)bank << 16);

	while(ATCPUMEMISSPECIAL(p)) {
		const MemoryNode& node = *(const MemoryNode *)(p - 1);

		sint32 v = node.mpReadHandler(node.mpThis, addr32);
		if (v >= 0)
			return (uint8)v;

		p = node.mNext;
	}

	return ((uint8 *)p)[address];
}

sint32 ATMemoryManager::CPUExtReadByteAccel(uint16 address, uint8 bank, bool chipOK) {
	uintptr p = mReadBankTable[bank][(uint8)(address >> 8)];
	const uint32 addr32 = (uint32)address + ((uint32)bank << 16);

	while(ATCPUMEMISSPECIAL(p)) {
		const MemoryNode& node = *(const MemoryNode *)(p - 1);

		if (!chipOK && !node.mpLayer->mbFastBus)
			return kChipReadNeedsDelay;

		sint32 v = node.mpReadHandler(node.mpThis, addr32);
		if (v >= 0) {
			if (!node.mpLayer->mbFastBus)
				v |= 0x80000000;
			
			return v;
		}

		p = node.mNext;
	}

	return ((uint8 *)p)[address];
}

uint8 ATMemoryManager::CPUDebugReadByte(uint16 address) {
	uintptr p = mCPUReadPageMap[(uint8)(address >> 8)];
	while(ATCPUMEMISSPECIAL(p)) {
		const MemoryNode& node = *(const MemoryNode *)(p - 1);

		ATMemoryReadHandler handler = node.mpLayer->mHandlers.mpDebugReadHandler;
		if (handler) {
			sint32 v = handler(node.mpThis, address);
			if (v >= 0)
				return (uint8)v;
		}

		p = node.mNext;
	}

	return ((uint8 *)p)[address];
}

uint8 ATMemoryManager::CPUDebugExtReadByte(uint16 address, uint8 bank) {
	uintptr p = mReadBankTable[bank][(uint8)(address >> 8)];
	const uint32 addr32 = (uint32)address + ((uint32)bank << 16);

	while(ATCPUMEMISSPECIAL(p)) {
		const MemoryNode& node = *(const MemoryNode *)(p - 1);

		ATMemoryReadHandler handler = node.mpLayer->mHandlers.mpDebugReadHandler;
		if (handler) {
			sint32 v = handler(node.mpThis, addr32);
			if (v >= 0)
				return (uint8)v;
		}

		p = node.mNext;
	}

	return ((uint8 *)p)[address];
}

void ATMemoryManager::CPUWriteByte(uint16 address, uint8 value) {
	uintptr p = mCPUWritePageMap[(uint8)(address >> 8)];

	while(ATCPUMEMISSPECIAL(p)) {
		const MemoryNode& node = *(const MemoryNode *)(p - 1);

		if (node.mpWriteHandler) {
			if (node.mpWriteHandler(node.mpThis, address, value))
				return;
		}

		p = node.mNext;
		if (p == 1)
			return;
	}

	((uint8 *)p)[address] = value;
}

void ATMemoryManager::CPUExtWriteByte(uint16 address, uint8 bank, uint8 value) {
	uintptr p = mWriteBankTable[bank][(uint8)(address >> 8)];
	const uint32 addr32 = (uint32)address + ((uint32)bank << 16);

	while(ATCPUMEMISSPECIAL(p)) {
		const MemoryNode& node = *(const MemoryNode *)(p - 1);

		if (node.mpWriteHandler) {
			if (node.mpWriteHandler(node.mpThis, addr32, value))
				return;
		}

		p = node.mNext;
		if (p == 1)
			return;
	}

	((uint8 *)p)[address] = value;
}

sint32 ATMemoryManager::CPUExtWriteByteAccel(uint16 address, uint8 bank, uint8 value, bool chipOK) {
	uintptr p = mWriteBankTable[bank][(uint8)(address >> 8)];
	const uint32 addr32 = (uint32)address + ((uint32)bank << 16);

	while(ATCPUMEMISSPECIAL(p)) {
		const MemoryNode& node = *(const MemoryNode *)(p - 1);

		if (node.mpWriteHandler) {
			if (!chipOK && !node.mpLayer->mbFastBus)
				return kChipReadNeedsDelay;

			if (node.mpWriteHandler(node.mpThis, addr32, value)) {
				return node.mpLayer->mbFastBus ? 0 : -1;
			}
		}

		p = node.mNext;
		if (p == 1)
			return 0;
	}

	((uint8 *)p)[address] = value;
	return 0;
}

void ATMemoryManager::RebuildNodes(uintptr *array, uint32 base, uint32 n, ATMemoryAccessMode accessMode) {
	Layers pertinentLayers;
	pertinentLayers.swap(mLayerTempList);
	pertinentLayers.clear();

	bool completeBaseLayer = false;
	for(Layers::const_iterator it(mLayers.begin()), itEnd(mLayers.end()); it != itEnd; ++it) {
		MemoryLayer *layer = *it;

		if (!layer->mbEnabled[accessMode])
			continue;

		if (base < layer->mPageOffset + layer->mPageCount &&
			layer->mPageOffset < base + n &&
			base < layer->mMaskRangeEnd &&
			layer->mMaskRangeStart < base + n)
		{
			pertinentLayers.push_back(layer);

			if (layer->mpBase &&
				layer->mPageOffset <= base &&
				(layer->mPageOffset + layer->mPageCount) >= (base + n) &&
				layer->mMaskRangeStart <= base &&
				layer->mMaskRangeEnd >= (base + n))
			{
				completeBaseLayer = true;
				break;
			}
		}
	}

	if (completeBaseLayer && pertinentLayers.size() == 1) {
		MemoryLayer *layer = pertinentLayers.front();

		if (layer->mpBase && layer->mAddrMask == 0xFFFFFFFFU && !mbFastBusEnabled) {
			if (base != 0x40 || n != 0x40 || !mbSimple_4000_7FFF[accessMode]) {
				for(uint32 i=0; i<n; ++i) {
					uintptr *root = &array[i];

					// free existing nodes
					for(uintptr p = *root; ATCPUMEMISSPECIAL(p);) {
						if (p == ((uintptr)&mDummyReadNode + 1) || p == ((uintptr)&mDummyWriteNode + 1))
							break;

						MemoryNode *node = (MemoryNode *)(p - 1);
						uintptr next = node->mNext;

						FreeNode(node);

						p = next;

						if (p == 1)
							break;
					}
				}
			} else {
				for(uint32 i=0; i<n; ++i) {
					uintptr p = array[i];

					VDASSERT(!ATCPUMEMISSPECIAL(p) || p == ((uintptr)&mDummyReadNode + 1) || p == ((uintptr)&mDummyWriteNode + 1));
				}
			}

			if (base == 0x40 && n == 0x40)
				mbSimple_4000_7FFF[accessMode] = true;

			const uintptr layerPtr = (uintptr)layer->mpBase - ((uintptr)layer->mPageOffset << 8);

			switch(accessMode) {
				case kATMemoryAccessMode_AnticRead:
				case kATMemoryAccessMode_CPURead:
					for(uint32 i=0; i<n; ++i)
						array[i] = layerPtr;
					break;

				case kATMemoryAccessMode_CPUWrite:
					if (layer->mbReadOnly) {
						for(uint32 i=0; i<n; ++i)
							array[i] = (uintptr)&mDummyWriteNode + 1;
					} else {
						for(uint32 i=0; i<n; ++i)
							array[i] = layerPtr;
					}
					break;
			}

			pertinentLayers.swap(mLayerTempList);
			return;
		}
	}

	if (base < 0x80 && base + n > 0x40)
		mbSimple_4000_7FFF[accessMode] = false;

	for(uint32 i=0; i<n; ++i) {
		uintptr *root = &array[i];
		uint32 pageOffset = (base + i);

		// free existing nodes
		for(uintptr p = *root; ATCPUMEMISSPECIAL(p);) {
			if (p == ((uintptr)&mDummyReadNode + 1) || p == ((uintptr)&mDummyWriteNode + 1))
				break;

			MemoryNode *node = (MemoryNode *)(p - 1);
			uintptr next = node->mNext;

			FreeNode(node);

			p = next;

			if (p == 1)
				break;
		}

		Layers::const_iterator it(pertinentLayers.begin()), itEnd(pertinentLayers.end());

		switch(accessMode) {
			case kATMemoryAccessMode_AnticRead:
				for(; it != itEnd; ++it) {
					MemoryLayer *layer = *it;

					if (pageOffset - layer->mPageOffset < layer->mPageCount &&
						pageOffset >= layer->mMaskRangeStart &&
						pageOffset < layer->mMaskRangeEnd)
					{
						if (layer->mpBase) {
							*root = (uintptr)layer->mpBase + (((uintptr)((pageOffset - layer->mPageOffset) & layer->mAddrMask) - pageOffset) << 8);
							break;
						} else {
							MemoryNode *node = AllocNode();

							node->mpLayer = layer;
							node->mpReadHandler = layer->mHandlers.mpReadHandler;
							node->mpThis = layer->mHandlers.mpThis;
							*root = (uintptr)node + 1;
							root = &node->mNext;

							if (!layer->mHandlers.mbPassAnticReads) {
								*root = 1;
								break;
							}
						}
					}
				}

				if (it == itEnd)
					*root = (uintptr)&mDummyReadNode + 1;
				break;

			case kATMemoryAccessMode_CPURead:
				for(; it != itEnd; ++it) {
					MemoryLayer *layer = *it;

					if (pageOffset - layer->mPageOffset < layer->mPageCount &&
						pageOffset >= layer->mMaskRangeStart &&
						pageOffset < layer->mMaskRangeEnd)
					{
						if (layer->mpBase) {
							if (mbFastBusEnabled && !layer->mbFastBus) {
								MemoryNode *node = AllocNode();

								node->mpLayer = layer;
								node->mpReadHandler = ChipReadHandler;
								node->mpThis = (void *)(layer->mpBase + ((uintptr)((pageOffset - layer->mPageOffset) & layer->mAddrMask) << 8));
								*root = (uintptr)node + 1;
								node->mNext = 1;
							} else {
								*root = (uintptr)layer->mpBase + (((uintptr)((pageOffset - layer->mPageOffset) & layer->mAddrMask) - pageOffset) << 8);
							}
							break;
						} else {
							MemoryNode *node = AllocNode();

							node->mpLayer = layer;
							node->mpReadHandler = layer->mHandlers.mpReadHandler;
							node->mpThis = layer->mHandlers.mpThis;
							*root = (uintptr)node + 1;
							root = &node->mNext;

							if (!layer->mHandlers.mbPassReads) {
								*root = 1;
								break;
							}
						}
					}
				}

				if (it == itEnd)
					*root = (uintptr)&mDummyReadNode + 1;
				break;

			case kATMemoryAccessMode_CPUWrite:
				for(; it != itEnd; ++it) {
					MemoryLayer *layer = *it;

					if (pageOffset - layer->mPageOffset < layer->mPageCount &&
						pageOffset >= layer->mMaskRangeStart &&
						pageOffset < layer->mMaskRangeEnd)
					{
						if (layer->mbReadOnly) {
							*root = (uintptr)&mDummyWriteNode + 1;
							break;
						} else if (layer->mpBase) {
							if (mbFastBusEnabled && !layer->mbFastBus) {
								MemoryNode *node = AllocNode();

								node->mpLayer = layer;
								node->mpWriteHandler = ChipWriteHandler;
								node->mpThis = (void *)(layer->mpBase + ((uintptr)((pageOffset - layer->mPageOffset) & layer->mAddrMask) << 8));
								*root = (uintptr)node + 1;
								node->mNext = 1;
							} else {
								*root = (uintptr)layer->mpBase + (((uintptr)((pageOffset - layer->mPageOffset) & layer->mAddrMask) - pageOffset) << 8);
							}
							break;
						} else {
							MemoryNode *node = AllocNode();

							node->mpLayer = layer;
							node->mpWriteHandler = layer->mHandlers.mpWriteHandler;
							node->mpThis = layer->mHandlers.mpThis;
							*root = (uintptr)node + 1;
							root = &node->mNext;

							if (!layer->mHandlers.mbPassWrites) {
								*root = 1;
								break;
							}
						}
					}
				}

				if (it == itEnd)
					*root = (uintptr)&mDummyWriteNode + 1;
				break;
		}
	}

	pertinentLayers.swap(mLayerTempList);
}

ATMemoryManager::MemoryNode *ATMemoryManager::AllocNode() {
	MemoryNode *node = mpFreeNodes;

	if (!node) {
		node = mAllocator.Allocate<MemoryNode>();
		node->mNext = (uintptr)mpFreeNodes;
	}

	mpFreeNodes = (MemoryNode *)node->mNext;
	return node;
}

void ATMemoryManager::FreeNode(MemoryNode *node) {
	node->mNext = (uintptr)mpFreeNodes;
	mpFreeNodes = node;
}

sint32 ATMemoryManager::DummyReadHandler(void *thisptr0, uint32 addr) {
	ATMemoryManager *thisptr = (ATMemoryManager *)thisptr0;

	return thisptr->ReadFloatingDataBus();
}

bool ATMemoryManager::DummyWriteHandler(void *thisptr, uint32 addr, uint8 value) {
	return true;
}

sint32 ATMemoryManager::ChipReadHandler(void *thisptr, uint32 addr) {
	return ((const uint8 *)thisptr)[addr & 0xff];
}

bool ATMemoryManager::ChipWriteHandler(void *thisptr, uint32 addr, uint8 value) {
	((uint8 *)thisptr)[addr & 0xff] = value;
	return true;
}
