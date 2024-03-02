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

#include "stdafx.h"
#include "cpuhookmanager.h"
#include "cpu.h"
#include "mmu.h"
#include "pbi.h"

ATCPUHookManager::ATCPUHookManager()
	: mpCPU(NULL)
	, mpMMU(NULL)
	, mpPBI(NULL)
	, mpFreeList(NULL)
{
	std::fill(mpHashTable, mpHashTable + 256, (HashNode *)NULL);
}

ATCPUHookManager::~ATCPUHookManager() {
}

void ATCPUHookManager::Init(ATCPUEmulator *cpu, ATMMUEmulator *mmu, ATPBIManager *pbi) {
	mpCPU = cpu;
	mpMMU = mmu;
	mpPBI = pbi;
}

void ATCPUHookManager::Shutdown() {
	for(int i=0; i<256; ++i) {
		HashNode *node = mpHashTable[i];

		while(node) {
			uint16 pc = node->mPC;

			do {
				node = node->mpNext;
			} while(node && node->mPC == pc);

			mpCPU->SetHook(pc, false);
		}
	}

	std::fill(mpHashTable, mpHashTable + 256, (HashNode *)NULL);
	mAllocator.Clear();

	mpPBI = NULL;
	mpMMU = NULL;
	mpCPU = NULL;
}

uint8 ATCPUHookManager::OnHookHit(uint16 pc) const {
	HashNode *node = mpHashTable[pc & 0xff];
	HashNode *next;

	for(; node; node = next) {
		next = node->mpNext;

		if (node->mPC == pc) {
			switch(node->mMode) {
				case kATCPUHookMode_Always:
				default:
					break;

				case kATCPUHookMode_KernelROMOnly:
					if (!mpMMU->IsKernelROMEnabled())
						continue;
					break;

				case kATCPUHookMode_MathPackROMOnly:
					if (!mpMMU->IsKernelROMEnabled())
						continue;

					if (mpPBI->IsROMOverlayActive())
						continue;
					break;
			}

			uint8 opcode = node->mpHookFn(pc, node->mpContext, node);

			if (opcode)
				return opcode;
		}
	}

// Can't enable this until all manual hooks are gone.
//	VDASSERT(!"OnHookHit() called without a hook being registered.");
	return 0;
}

ATCPUHookNode *ATCPUHookManager::AddHook(ATCPUHookMode mode, uint16 pc, sint8 priority, ATCPUHookFn fn, void *context) {
	if (!mpFreeList) {
		HashNode *node = mAllocator.Allocate<HashNode>();

		node->mpNext = NULL;
		node->mpHookFn = NULL;
		mpFreeList = node;
	}

	HashNode *node = mpFreeList;
	mpFreeList = node->mpNext;
	VDASSERT(!node->mpHookFn);

	node->mpHookFn = fn;
	node->mpContext = context;
	node->mMode = mode;
	node->mPriority = priority;
	node->mPC = pc;

	int hc = pc & 0xff;
	HashNode **insertPrev = &mpHashTable[hc];
	HashNode *insertNext = *insertPrev;

	while(insertNext && insertNext->mPC != pc) {
		insertPrev = &insertNext->mpNext;
		insertNext = *insertPrev;
	}

	while(insertNext && insertNext->mPC == pc && insertNext->mPriority >= priority) {
		insertPrev = &insertNext->mpNext;
		insertNext = *insertPrev;
	}

	*insertPrev = node;
	node->mpNext = insertNext;

	mpCPU->SetHook(pc, true);
	return node;
}

void ATCPUHookManager::RemoveHook(ATCPUHookNode *hook) {
	if (!hook)
		return;

	const uint16 pc = static_cast<HashNode *>(hook)->mPC;
	HashNode **prev = &mpHashTable[pc & 0xff];
	HashNode *node = *prev;
	sint32 prevpc = -1;

	while(node) {
		if (node == hook) {
			*prev = node->mpNext;

			if (prevpc != pc && (!node->mpNext || node->mpNext->mPC != pc))
				mpCPU->SetHook(pc, false);

			node->mpNext = mpFreeList;
			mpFreeList = node;

			node->mpHookFn = NULL;
			return;
		}

		prevpc = node->mPC;
		prev = &node->mpNext;
		node = *prev;
	}

	VDASSERT(!"Attempt to remove invalid CPU hook!");
}
