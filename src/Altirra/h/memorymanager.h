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

#ifndef f_AT_MEMORYMANAGER_H
#define f_AT_MEMORYMANAGER_H

#include <vd2/system/linearalloc.h>
#include <vd2/system/vdstl.h>
#include "cpumemory.h"

typedef sint32 (*ATMemoryReadHandler)(void *thisptr, uint32 addr);
typedef bool (*ATMemoryWriteHandler)(void *thisptr, uint32 addr, uint8 value);

struct ATMemoryHandlerTable {
	bool mbPassReads;
	bool mbPassAnticReads;
	bool mbPassWrites;
	void *mpThis;
	ATMemoryReadHandler mpDebugReadHandler;
	ATMemoryReadHandler mpReadHandler;
	ATMemoryWriteHandler mpWriteHandler;
};

class ATMemoryLayer {};

enum ATMemoryAccessMode {
	kATMemoryAccessMode_AnticRead,
	kATMemoryAccessMode_CPURead,
	kATMemoryAccessMode_CPUWrite
};

enum ATMemoryPriority {
	kATMemoryPri_BaseRAM	= 0,
	kATMemoryPri_ExtRAM		= 1,
	kATMemoryPri_Extsel		= 2,
	kATMemoryPri_ROM		= 8,
	kATMemoryPri_Cartridge2	= 16,
	kATMemoryPri_Cartridge1	= 24,
	kATMemoryPri_CartridgeOverlay	= 32,
	kATMemoryPri_Hardware	= 56,
	kATMemoryPri_HardwareOverlay,
	kATMemoryPri_AccessBP	= 64
};

class ATMemoryManager : public ATCPUEmulatorMemory {
	ATMemoryManager(const ATMemoryManager&);
	ATMemoryManager& operator=(const ATMemoryManager&);
public:
	ATMemoryManager();
	~ATMemoryManager();

	const uintptr *GetAnticMemoryMap() const { return mAnticReadPageMap; }

	void Init(const void *highMemory, uint32 highMemoryBanks);

	void DumpStatus();

	ATMemoryLayer *CreateLayer(int priority, const uint8 *base, uint32 pageAddr, uint32 pages, bool readOnly);
	ATMemoryLayer *CreateLayer(int priority, const ATMemoryHandlerTable& handlers, uint32 pageOffset, uint32 pages);
	void DeleteLayer(ATMemoryLayer *layer);
	void EnableLayer(ATMemoryLayer *layer, bool enable);
	void EnableLayer(ATMemoryLayer *layer, ATMemoryAccessMode mode, bool enable);
	void SetLayerMemory(ATMemoryLayer *layer, const uint8 *base);
	void SetLayerMemory(ATMemoryLayer *layer, const uint8 *base, uint32 pageOffset, uint32 pages, uint32 addrMask = 0xFFFFFFFFU, int readOnly = -1);

	uint8 AnticReadByte(uint32 address);
	uint8 DebugAnticReadByte(uint16 address);
	uint8 CPUReadByte(uint32 address);
	uint8 CPUExtReadByte(uint16 address, uint8 bank);
	uint8 CPUDebugReadByte(uint16 address);
	uint8 CPUDebugExtReadByte(uint16 address, uint8 bank);
	void CPUWriteByte(uint16 address, uint8 value);
	void CPUExtWriteByte(uint16 address, uint8 bank, uint8 value);

protected:
	struct MemoryLayer : public ATMemoryLayer {
		int mPriority;
		bool mbEnabled[3];
		bool mbReadOnly;
		const uint8 *mpBase;
		uint32 mAddrMask;
		uint32 mPageOffset;
		uint32 mPageCount;
		ATMemoryHandlerTable mHandlers;
	};

	struct MemoryLayerPred {
		bool operator()(const MemoryLayer *x, const MemoryLayer *y) const {
			return x->mPriority > y->mPriority;
		}
	};

	struct MemoryNode {
		ATMemoryReadHandler mpDebugReadHandler;

		union {
			ATMemoryReadHandler mpReadHandler;
			ATMemoryWriteHandler mpWriteHandler;
		};

		void *mpThis;
		uintptr mNext;
	};

	void RebuildNodes(uintptr *array, uint32 base, uint32 n, ATMemoryAccessMode mode);
	MemoryNode *AllocNode();
	void FreeNode(MemoryNode *node);

	static sint32 DummyReadHandler(void *thisptr, uint32 addr);
	static bool DummyWriteHandler(void *thisptr, uint32 addr, uint8 value);

	uintptr mCPUReadPageMap[256];
	uintptr mCPUWritePageMap[256];
	uintptr mAnticReadPageMap[256];

	typedef vdfastvector<MemoryLayer *> Layers;
	Layers mLayers;

	MemoryNode *mpFreeNodes;
	VDLinearAllocator mAllocator;

	const uintptr	*mReadBankTable[256];
	uintptr			*mWriteBankTable[256];

	uintptr			mHighMemoryPageTable[256];

	uintptr			mDummyReadPageTable[256];
	uintptr			mDummyWritePageTable[256];

	MemoryNode		mDummyReadNode;
	MemoryNode		mDummyWriteNode;
};

#endif	// f_AT_MEMORYMANAGER_H
