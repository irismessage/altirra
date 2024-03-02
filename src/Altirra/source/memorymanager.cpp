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

void ATMemoryManager::MemoryLayer::UpdateEffectiveRange() {
	mEffectiveStart = std::max(mPageOffset, mMaskRangeStart);
	mEffectiveEnd = std::min(mPageOffset + mPageCount, mMaskRangeEnd);

	if (mEffectiveEnd < mEffectiveStart)
		mEffectiveEnd = mEffectiveStart;
}

///////////////////////////////////////////////////////////////////////////

ATMemoryManager::ATMemoryManager()
	: mAllocationCount(0)
	, mbFloatingDataBus(false)
	, mbFastBusEnabled(false)
{
	for(int i=0; i<256; ++i) {
		mDummyReadPageTable[i] = (uintptr)&mDummyReadNode + 1;
		mDummyWritePageTable[i] = (uintptr)&mDummyWriteNode + 1;
	}

	mDummyLayer.mPriority = -1;
	mDummyLayer.mFlags = kATMemoryAccessMode_ARW;
	mDummyLayer.mbReadOnly = false;
	mDummyLayer.mpBase = NULL;
	mDummyLayer.mAddrMask = 0xFFFFFFFF;
	mDummyLayer.mPageOffset = 0;
	mDummyLayer.mPageCount = 0x100;
	mDummyLayer.mMaskRangeStart = 0;
	mDummyLayer.mMaskRangeEnd = 0x100;
	mDummyLayer.mEffectiveStart = 0;
	mDummyLayer.mEffectiveEnd = 0x100;
	mDummyLayer.mHandlers.mbPassReads = false;
	mDummyLayer.mHandlers.mbPassAnticReads = false;
	mDummyLayer.mHandlers.mbPassWrites = false;
	mDummyLayer.mHandlers.mpThis = this;
	mDummyLayer.mHandlers.mpDebugReadHandler = DummyReadHandler;
	mDummyLayer.mHandlers.mpReadHandler = DummyReadHandler;
	mDummyLayer.mHandlers.mpWriteHandler = DummyWriteHandler;
	mDummyLayer.mpName = "Unconnected";

	mDummyReadNode.mLayerOrForward = (uintptr)&mDummyLayer;
	mDummyReadNode.mpReadHandler = DummyReadHandler;
	mDummyReadNode.mNext = 1;
	mDummyReadNode.mpThis = this;
	mDummyWriteNode.mLayerOrForward = (uintptr)&mDummyLayer;
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

void ATMemoryManager::SetFloatingIoBus(bool floating) {
	if (mbFloatingIoBus == floating)
		return;

	mbFloatingIoBus = floating;

	// Must reinitialize debug read handler for all memory nodes.
	for(MemoryLayer *layer : mLayers) {
		if (layer->mpBase) {
			if (layer->mbIoBus && floating) {
				layer->mHandlers.mpDebugReadHandler = IoMemoryDebugReadWrapperHandler;
			} else {
				layer->mHandlers.mpDebugReadHandler = ChipReadHandler;
			}
		}
	}

	// Must rebuild all nodes now as memory nodes are handled differently in I/O mode.
	RebuildNodes(&mAnticReadPageMap[0], 0, 256, kATMemoryAccessMode_AnticRead);
	RebuildNodes(&mCPUReadPageMap[0], 0, 256, kATMemoryAccessMode_CPURead);
	RebuildNodes(&mCPUWritePageMap[0], 0, 256, kATMemoryAccessMode_CPUWrite);
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

		uint32 pageStart = layer.mEffectiveStart;
		uint32 pageEnd = layer.mEffectiveEnd;

		if (pageStart < pageEnd) {
			s.sprintf("%06X-%06X", pageStart << 8, (pageEnd << 8) - 1);
		} else {
			s = "<masked>     ";
		}

		s.append_sprintf(" %2u%s %c%c%c  "
			, layer.mPriority
			, mbFastBusEnabled ? layer.mbFastBus ? " fast" : " chip" : ""
			, layer.mFlags & kATMemoryAccessMode_A ? 'A' : '-'
			, layer.mFlags & kATMemoryAccessMode_R ? 'R' : '-'
			, layer.mFlags & kATMemoryAccessMode_W ? layer.mbReadOnly ? 'O' : 'W' : '-'
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

	layer->mpParent = this;
	layer->mPriority = priority;
	layer->mFlags = 0;
	layer->mbReadOnly = readOnly;
	layer->mbFastBus = false;
	layer->mbIoBus = false;
	layer->mpBase = base;
	layer->mPageOffset = pageOffset;
	layer->mPageCount = pageCount;
	layer->mHandlers.mbPassAnticReads = false;
	layer->mHandlers.mbPassReads = false;
	layer->mHandlers.mbPassWrites = false;
	layer->mHandlers.mpThis = this;
	layer->mHandlers.mpDebugReadHandler = ChipReadHandler;
	layer->mHandlers.mpReadHandler = ChipReadHandler;
	layer->mHandlers.mpWriteHandler = ChipWriteHandler;
	layer->mHandlers.mpThis = NULL;
	layer->mAddrMask = 0xFFFFFFFFU;
	layer->mpName = NULL;
	layer->mMaskRangeStart = 0x00;
	layer->mMaskRangeEnd = 0x100;
	layer->mEffectiveStart = pageOffset;
	layer->mEffectiveEnd = pageOffset + pageCount;

	mLayers.insert(std::lower_bound(mLayers.begin(), mLayers.end(), layer, MemoryLayerPred()), layer);
	return layer;
}

ATMemoryLayer *ATMemoryManager::CreateLayer(int priority, const ATMemoryHandlerTable& handlers, uint32 pageOffset, uint32 pageCount) {
	MemoryLayer *layer = new MemoryLayer;

	layer->mpParent = this;
	layer->mPriority = priority;
	layer->mFlags = 0;
	layer->mbReadOnly = false;
	layer->mbFastBus = false;
	layer->mbIoBus = false;
	layer->mpBase = NULL;
	layer->mPageOffset = pageOffset;
	layer->mPageCount = pageCount;
	layer->mHandlers = handlers;
	layer->mAddrMask = 0xFFFFFFFFU;
	layer->mpName = NULL;
	layer->mMaskRangeStart = 0x00;
	layer->mMaskRangeEnd = 0x100;
	layer->mEffectiveStart = pageOffset;
	layer->mEffectiveEnd = pageOffset + pageCount;

	mLayers.insert(std::lower_bound(mLayers.begin(), mLayers.end(), layer, MemoryLayerPred()), layer);
	return layer;
}

void ATMemoryManager::DeleteLayer(ATMemoryLayer *layer) {
	EnableLayer(layer, false);

	mLayers.erase(std::find(mLayers.begin(), mLayers.end(), layer));

	delete (MemoryLayer *)layer;
}

void ATMemoryManager::EnableLayer(ATMemoryLayer *layer, bool enable) {
	EnableLayer(layer, kATMemoryAccessMode_ARW, enable);
}

void ATMemoryManager::EnableLayer(ATMemoryLayer *layer0, ATMemoryAccessMode accessMode, bool enable) {
	MemoryLayer *const layer = static_cast<MemoryLayer *>(layer0);
	uint8 flags = layer->mFlags;

	if (enable)
		flags |= accessMode;
	else
		flags &= ~accessMode;

	SetLayerModes(layer, (ATMemoryAccessMode)flags);
}

void ATMemoryManager::SetLayerModes(ATMemoryLayer *layer0, ATMemoryAccessMode flags) {
	MemoryLayer *const layer = static_cast<MemoryLayer *>(layer0);
	const uint8 changeFlags = layer->mFlags ^ flags;

	if (!changeFlags)
		return;

	layer->mFlags = flags;

	if (changeFlags & kATMemoryAccessMode_AnticRead)
		RebuildNodes(&mAnticReadPageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_AnticRead);

	if (changeFlags & kATMemoryAccessMode_CPURead)
		RebuildNodes(&mCPUReadPageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_CPURead);

	if (changeFlags & kATMemoryAccessMode_CPUWrite)
		RebuildNodes(&mCPUWritePageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_CPUWrite);
}

void ATMemoryManager::SetLayerMemory(ATMemoryLayer *layer0, const uint8 *base) {
	MemoryLayer *const layer = static_cast<MemoryLayer *>(layer0);

	if (base != layer->mpBase) {
		layer->mpBase = base;

		uint32 rewriteOffset = layer->mPageOffset;
		uint32 rewriteCount = layer->mPageCount;

		if (layer->mFlags & kATMemoryAccessMode_AnticRead)
			RebuildNodes(&mAnticReadPageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_AnticRead);

		if (layer->mFlags & kATMemoryAccessMode_CPURead)
			RebuildNodes(&mCPUReadPageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_CPURead);

		if (layer->mFlags & kATMemoryAccessMode_CPUWrite)
			RebuildNodes(&mCPUWritePageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_CPUWrite);
	}
}

void ATMemoryManager::SetLayerMemory(ATMemoryLayer *layer0, const uint8 *base, uint32 pageOffset, uint32 pageCount, uint32 addrMask, int readOnly) {
	VDASSERT(base);
	MemoryLayer *const layer = static_cast<MemoryLayer *>(layer0);

	bool ro = (readOnly >= 0) ? readOnly != 0 : layer->mbReadOnly;

	if (base != layer->mpBase || layer->mPageOffset != pageOffset || layer->mPageCount != pageCount || addrMask != layer->mAddrMask || ro != layer->mbReadOnly) {
		uint32 oldBegin = layer->mEffectiveStart;
		uint32 oldEnd = layer->mEffectiveEnd;

		layer->mpBase = base;
		layer->mAddrMask = addrMask;
		layer->mPageOffset = pageOffset;
		layer->mPageCount = pageCount;
		layer->mbReadOnly = ro;

		layer->UpdateEffectiveRange();

		uint32 newBegin = layer->mEffectiveStart;
		uint32 newEnd = layer->mEffectiveEnd;

		uint32 rewriteOffset = std::min<uint32>(oldBegin, newBegin);
		uint32 rewriteCount = std::max<uint32>(oldEnd, newEnd) - rewriteOffset;

		if (layer->mFlags & kATMemoryAccessMode_AnticRead)
			RebuildNodes(&mAnticReadPageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_AnticRead);

		if (layer->mFlags & kATMemoryAccessMode_CPURead)
			RebuildNodes(&mCPUReadPageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_CPURead);

		if (layer->mFlags & kATMemoryAccessMode_CPUWrite)
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

		if (layer->mFlags & kATMemoryAccessMode_AnticRead)
			RebuildNodes(&mAnticReadPageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_AnticRead);

		if (layer->mFlags & kATMemoryAccessMode_CPURead)
			RebuildNodes(&mCPUReadPageMap[rewriteOffset], rewriteOffset, rewriteCount, kATMemoryAccessMode_CPURead);

		if (layer->mFlags & kATMemoryAccessMode_CPUWrite)
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
			RebuildNodes(&mCPUReadPageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_CPURead);
			RebuildNodes(&mCPUWritePageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_CPUWrite);
		}
	}
}

void ATMemoryManager::SetLayerIoBus(ATMemoryLayer *layer0, bool ioBus) {
	MemoryLayer *const layer = static_cast<MemoryLayer *>(layer0);

	if (layer->mbIoBus == ioBus)
		return;

	layer->mbIoBus = ioBus;

	// I/O bus setting is ignored if I/O bus is not enabled.
	if (!mbFloatingIoBus)
		return;

	if (layer->mpBase) {
		if (ioBus) {
			layer->mHandlers.mpDebugReadHandler = IoMemoryDebugReadWrapperHandler;
		} else {
			layer->mHandlers.mpDebugReadHandler = ChipReadHandler;
		}
	}

	if (layer->mFlags & kATMemoryAccessMode_AnticRead)
		RebuildNodes(&mAnticReadPageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_AnticRead);

	if (layer->mFlags & kATMemoryAccessMode_CPURead)
		RebuildNodes(&mCPUReadPageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_CPURead);

	if (layer->mFlags & kATMemoryAccessMode_CPUWrite)
		RebuildNodes(&mCPUWritePageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_CPUWrite);
}

void ATMemoryManager::ClearLayerMaskRange(ATMemoryLayer *layer) {
	SetLayerMaskRange(layer, 0, 0x100);
}

void ATMemoryManager::SetLayerMaskRange(ATMemoryLayer *layer0, uint32 pageStart, uint32 pageCount) {
	MemoryLayer *const layer = static_cast<MemoryLayer *>(layer0);

	if (pageStart > 0x100)
		pageStart = 0x100;

	const uint32 pageEnd = std::min<uint32>(pageStart + pageCount, 0x100);

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

	layer->UpdateEffectiveRange();

	if (changed) {
		if (layer->mFlags & kATMemoryAccessMode_AnticRead)
			RebuildNodes(&mAnticReadPageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_AnticRead);

		if (layer->mFlags & kATMemoryAccessMode_CPURead)
			RebuildNodes(&mCPUReadPageMap[layer->mPageOffset], layer->mPageOffset, layer->mPageCount, kATMemoryAccessMode_CPURead);

		if (layer->mFlags & kATMemoryAccessMode_CPUWrite)
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

		ATMemoryReadHandler handler = ((MemoryLayer *)node.mLayerOrForward)->mHandlers.mpDebugReadHandler;
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

		if (!chipOK && !((MemoryLayer *)node.mLayerOrForward)->mbFastBus)
			return kChipReadNeedsDelay;

		// We need to precache this as the read handler may rewrite the memory config
		// and invalidate the node.
		const uintptr layerOrForward = node.mLayerOrForward;

		sint32 v = node.mpReadHandler(node.mpThis, addr32);
		if (v >= 0) {
			if (!((MemoryLayer *)layerOrForward)->mbFastBus)
				v |= 0x80000000;
			
			return v;
		}

		p = node.mNext;
	}

	return ((uint8 *)p)[address];
}

uint8 ATMemoryManager::CPUDebugReadByte(uint16 address) const {
	uintptr p = mCPUReadPageMap[(uint8)(address >> 8)];
	while(ATCPUMEMISSPECIAL(p)) {
		const MemoryNode& node = *(const MemoryNode *)(p - 1);

		ATMemoryReadHandler handler = ((MemoryLayer *)node.mLayerOrForward)->mHandlers.mpDebugReadHandler;
		if (handler) {
			sint32 v = handler(node.mpThis, address);
			if (v >= 0)
				return (uint8)v;
		}

		p = node.mNext;
	}

	return ((uint8 *)p)[address];
}

uint8 ATMemoryManager::CPUDebugExtReadByte(uint16 address, uint8 bank) const {
	uintptr p = mReadBankTable[bank][(uint8)(address >> 8)];
	const uint32 addr32 = (uint32)address + ((uint32)bank << 16);

	while(ATCPUMEMISSPECIAL(p)) {
		const MemoryNode& node = *(const MemoryNode *)(p - 1);

		ATMemoryReadHandler handler = ((MemoryLayer *)node.mLayerOrForward)->mHandlers.mpDebugReadHandler;
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
			if (!chipOK && !((MemoryLayer *)node.mLayerOrForward)->mbFastBus)
				return kChipReadNeedsDelay;

			// We need to precache this as the read handler may rewrite the memory config
			// and invalidate the node.
			const uintptr layerOrForward = node.mLayerOrForward;
			if (node.mpWriteHandler(node.mpThis, addr32, value)) {
				return ((MemoryLayer *)layerOrForward)->mbFastBus ? 0 : -1;
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

		if (!(layer->mFlags & accessMode))
			continue;

		if (base < layer->mEffectiveEnd &&
			layer->mEffectiveStart < base + n)
		{
			pertinentLayers.push_back(layer);

			if (layer->mpBase &&
				layer->mEffectiveStart <= base &&
				layer->mEffectiveEnd >= (base + n))
			{
				completeBaseLayer = true;
				break;
			}
		}
	}

	if (completeBaseLayer && pertinentLayers.size() == 1) {
		MemoryLayer *layer = pertinentLayers.front();

		if (layer->mpBase && layer->mAddrMask == 0xFFFFFFFFU && !mbFastBusEnabled && !mbFloatingIoBus) {
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

	bool boundaryBits[257] = { false };

	boundaryBits[base] = true;

	for(const MemoryLayer *layer : pertinentLayers) {
		boundaryBits[layer->mEffectiveStart] = true;
		boundaryBits[layer->mEffectiveEnd] = true;

		if (layer->mAddrMask != ~(uint32)0) {
			uint32 step = layer->mAddrMask & ~(layer->mAddrMask - 1);

			for(uint32 page = layer->mEffectiveStart; page < layer->mEffectiveEnd; page += step)
				boundaryBits[page] = true;
		}
	}

	for(uint32 i=0; i<n; ++i) {
		uintptr *root = &array[i];
		uint32 pageOffset = (base + i);

		if (!boundaryBits[pageOffset]) {
			*root = array[i-1];
			continue;
		}

		Layers::const_iterator it(pertinentLayers.begin()), itEnd(pertinentLayers.end());

		switch(accessMode) {
			case kATMemoryAccessMode_AnticRead:
				for(; it != itEnd; ++it) {
					MemoryLayer *layer = *it;

					if (pageOffset >= layer->mEffectiveStart &&
						pageOffset < layer->mEffectiveEnd)
					{
						if (mbFloatingIoBus && layer->mbIoBus) {
							MemoryNode *node = AllocNode();
							node->mpThis = layer;
							node->mLayerOrForward = (uintptr)layer;

							if (layer->mpBase)
								node->mpReadHandler = (layer->mAddrMask == (uint32)0 - 1) ? IoMemoryFastReadWrapperHandler : IoMemoryReadWrapperHandler;
							else
								node->mpReadHandler = IoHandlerReadWrapperHandler;

							*root = (uintptr)node + 1;
							root = &node->mNext;

							if (layer->mpBase || !layer->mHandlers.mbPassAnticReads) {
								*root = 1;
								break;
							}
						} else if (layer->mpBase) {
							*root = (uintptr)layer->mpBase + (((uintptr)((pageOffset - layer->mPageOffset) & layer->mAddrMask) - pageOffset) << 8);
							break;
						} else {
							MemoryNode *node = AllocNode();

							node->mLayerOrForward = (uintptr)layer;
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

					if (pageOffset >= layer->mEffectiveStart &&
						pageOffset < layer->mEffectiveEnd)
					{
						if (mbFloatingIoBus && layer->mbIoBus) {
							MemoryNode *node = AllocNode();
							node->mpThis = layer;
							node->mLayerOrForward = (uintptr)layer;

							if (layer->mpBase)
								node->mpReadHandler = (layer->mAddrMask == (uint32)0 - 1) ? IoMemoryFastReadWrapperHandler : IoMemoryReadWrapperHandler;
							else
								node->mpReadHandler = IoHandlerReadWrapperHandler;

							*root = (uintptr)node + 1;
							root = &node->mNext;

							if (layer->mpBase || !layer->mHandlers.mbPassReads) {
								*root = 1;
								break;
							}
						} else if (layer->mpBase) {
							if (mbFastBusEnabled && !layer->mbFastBus) {
								MemoryNode *node = AllocNode();

								node->mLayerOrForward = (uintptr)layer;
								node->mpReadHandler = ChipReadHandler;
								node->mpThis = (void *)(layer->mpBase + ((uintptr)((pageOffset - layer->mPageOffset) & layer->mAddrMask) << 8) - (pageOffset << 8));
								*root = (uintptr)node + 1;
								node->mNext = 1;
							} else {
								*root = (uintptr)layer->mpBase + (((uintptr)((pageOffset - layer->mPageOffset) & layer->mAddrMask) - pageOffset) << 8);
							}
							break;
						} else {
							MemoryNode *node = AllocNode();

							node->mLayerOrForward = (uintptr)layer;
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

					if (pageOffset >= layer->mEffectiveStart &&
						pageOffset < layer->mEffectiveEnd)
					{
						if (mbFloatingIoBus && layer->mbIoBus) {
							MemoryNode *node = AllocNode();
							node->mpThis = layer;
							node->mLayerOrForward = (uintptr)layer;

							if (layer->mbReadOnly)
								node->mpWriteHandler = IoNullWriteWrapperHandler;
							else if (layer->mpBase)
								node->mpWriteHandler = IoMemoryWriteWrapperHandler;
							else
								node->mpWriteHandler = IoHandlerWriteWrapperHandler;

							*root = (uintptr)node + 1;
							root = &node->mNext;

							if (layer->mpBase || !layer->mHandlers.mbPassWrites) {
								*root = 1;
								break;
							}
						} else if (layer->mbReadOnly) {
							*root = (uintptr)&mDummyWriteNode + 1;
							break;
						} else if (layer->mpBase) {
							if (mbFastBusEnabled && !layer->mbFastBus) {
								MemoryNode *node = AllocNode();

								node->mLayerOrForward = (uintptr)layer;
								node->mpWriteHandler = ChipWriteHandler;
								node->mpThis = (void *)(layer->mpBase + ((uintptr)((pageOffset - layer->mPageOffset) & layer->mAddrMask) << 8) - (pageOffset << 8));
								*root = (uintptr)node + 1;
								node->mNext = 1;
							} else {
								*root = (uintptr)layer->mpBase + (((uintptr)((pageOffset - layer->mPageOffset) & layer->mAddrMask) - pageOffset) << 8);
							}
							break;
						} else {
							MemoryNode *node = AllocNode();

							node->mLayerOrForward = (uintptr)layer;
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

	if (mAllocationCount >= 4096)
		GarbageCollect();
}

ATMemoryManager::MemoryNode *ATMemoryManager::AllocNode() {
	++mAllocationCount;
	return mAllocator.Allocate<MemoryNode>();
}

void ATMemoryManager::GarbageCollect() {
	uintptr *pLink = mCPUReadPageMap;

	// temporarily whack the dummy nodes so they forward to themselves
	mDummyReadNode.mLayerOrForward = (uintptr)&mDummyReadNode + 1;
	mDummyWriteNode.mLayerOrForward = (uintptr)&mDummyWriteNode + 1;

	// mark and copy all used nodes
	for(int i=0; i<768; ++i) {
		uintptr *pRef = pLink;

		for(;;) {
			uintptr p = *pRef;

			if (!ATCPUMEMISSPECIAL(p))
				break;

			MemoryNode *pNode = (MemoryNode *)(p - 1);
			if (!pNode)
				break;

			// check if link was already copied
			if (pNode->mLayerOrForward & 1) {
				// yes -- update the link and exit
				*pRef = pNode->mLayerOrForward;
				break;
			}

			// copy the link
			MemoryNode *pNewNode = mAllocatorNext.Allocate<MemoryNode>();
			*pNewNode = *pNode;

			// set up forwarding
			pNode->mLayerOrForward = (uintptr)pNewNode + 1;

			// update the reference
			*pRef = (uintptr)pNewNode + 1;

			// check the next node
			pRef = &pNewNode->mNext;
		}

		++pLink;
		
		// these are to stay compliant -- they should compile out
		if (pLink == &mCPUReadPageMap[256])
			pLink = &mCPUWritePageMap[0];

		if (pLink == &mCPUWritePageMap[256])
			pLink = &mAnticReadPageMap[0];
	}

	// trim and swap the allocators
	// NOTE: We must keep the previous allocator alive! This is because we might be
	// in the middle of a memory access and can't drop the allocation chain until
	// it completes.
	mAllocatorPrev.Reset();
	mAllocatorPrev.Swap(mAllocatorNext);
	mAllocator.Swap(mAllocatorPrev);
	mAllocationCount = 0;
	
	// restore the dummy nodes
	mDummyReadNode.mLayerOrForward = (uintptr)&mDummyLayer;
	mDummyWriteNode.mLayerOrForward = (uintptr)&mDummyLayer;
}

sint32 ATMemoryManager::DummyReadHandler(void *thisptr0, uint32 addr) {
	ATMemoryManager *thisptr = (ATMemoryManager *)thisptr0;

	return thisptr->ReadFloatingDataBus();
}

bool ATMemoryManager::DummyWriteHandler(void *thisptr, uint32 addr, uint8 value) {
	return true;
}

sint32 ATMemoryManager::ChipReadHandler(void *thisptr, uint32 addr) {
	return ((const uint8 *)thisptr)[addr & 0xffff];
}

bool ATMemoryManager::ChipWriteHandler(void *thisptr, uint32 addr, uint8 value) {
	((uint8 *)thisptr)[addr & 0xffff] = value;
	return true;
}

sint32 ATMemoryManager::IoMemoryFastReadWrapperHandler(void *thisptr, uint32 addr) {
	MemoryLayer *layer = (MemoryLayer *)thisptr;
	uint8 c = layer->mpBase[addr - (layer->mPageOffset << 8)];

	layer->mpParent->mIoBusValue = c;
	return c;
}

sint32 ATMemoryManager::IoMemoryDebugReadWrapperHandler(void *thisptr, uint32 addr) {
	MemoryLayer *layer = (MemoryLayer *)thisptr;
	return layer->mpBase[(addr - (layer->mPageOffset << 8)) & ((layer->mAddrMask << 8) + 0xFF)];
}

sint32 ATMemoryManager::IoMemoryReadWrapperHandler(void *thisptr, uint32 addr) {
	MemoryLayer *layer = (MemoryLayer *)thisptr;
	uint8 c = layer->mpBase[(addr - (layer->mPageOffset << 8)) & ((layer->mAddrMask << 8) + 0xFF)];

	layer->mpParent->mIoBusValue = c;
	return c;
}

bool ATMemoryManager::IoMemoryRoWriteWrapperHandler(void *thisptr, uint32 addr, uint8 value) {
	MemoryLayer *layer = (MemoryLayer *)thisptr;
	layer->mpParent->mIoBusValue = value;
	return true;
}

bool ATMemoryManager::IoMemoryWriteWrapperHandler(void *thisptr, uint32 addr, uint8 value) {
	MemoryLayer *layer = (MemoryLayer *)thisptr;
	((uint8 *)layer->mpBase)[(addr - (layer->mPageOffset << 8)) & ((layer->mAddrMask << 8) + 0xFF)] = value;

	layer->mpParent->mIoBusValue = value;
	return true;
}

sint32 ATMemoryManager::IoHandlerReadWrapperHandler(void *thisptr, uint32 addr) {
	MemoryLayer *layer = (MemoryLayer *)thisptr;
	sint32 v = layer->mHandlers.mpReadHandler(layer->mHandlers.mpThis, addr);

	if (v >= 0)
		layer->mpParent->mIoBusValue = (uint8)v;

	return v;
}

bool ATMemoryManager::IoHandlerWriteWrapperHandler(void *thisptr, uint32 addr, uint8 value) {
	MemoryLayer *layer = (MemoryLayer *)thisptr;
	layer->mHandlers.mpWriteHandler(layer->mHandlers.mpThis, addr, value);
	layer->mpParent->mIoBusValue = value;
	return layer->mHandlers.mbPassWrites;
}

bool ATMemoryManager::IoNullWriteWrapperHandler(void *thisptr, uint32 addr, uint8 value) {
	MemoryLayer *layer = (MemoryLayer *)thisptr;
	layer->mpParent->mIoBusValue = value;
	return layer->mHandlers.mbPassWrites;
}
