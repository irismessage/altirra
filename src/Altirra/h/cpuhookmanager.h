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

#ifndef f_AT_CPUHOOKMANAGER_H
#define f_AT_CPUHOOKMANAGER_H

#include <vd2/system/vdstl.h>
#include <vd2/system/linearalloc.h>
#include <vd2/system/function.h>

struct ATCPUHookNode {};
struct ATCPUHookInitNode {};

typedef vdfunction<uint8(uint16 pc)> ATCPUHookFn;
typedef vdfunction<void(const uint8 *lowerKernelROM, const uint8 *upperKernelROM)> ATCPUHookInitFn;

class ATCPUEmulator;
class ATMMUEmulator;
class ATPBIManager;

enum ATCPUHookMode {
	kATCPUHookMode_Always,
	kATCPUHookMode_KernelROMOnly,
	kATCPUHookMode_MathPackROMOnly
};

class ATCPUHookManager {
	ATCPUHookManager(const ATCPUHookManager&) = delete;
	ATCPUHookManager& operator=(const ATCPUHookManager&) = delete;

	struct HashNode : public ATCPUHookNode {
		HashNode *mpNext;
		uint16 mPC;
		uint8 mMode;
		sint8 mPriority;

		ATCPUHookFn mpHookFn;
	};

	struct InitNode : public ATCPUHookInitNode {
		InitNode *mpNext;
		ATCPUHookInitFn mpHookFn;
	};

public:
	ATCPUHookManager();
	~ATCPUHookManager();

	void Init(ATCPUEmulator *cpu, ATMMUEmulator *mmu, ATPBIManager *pbi);
	void Shutdown();

	void EnableOSHooks(bool enabled) { mbOSHooksEnabled = enabled; }

	uint8 OnHookHit(uint16 pc) const;

	// The init hooks are called when it is first safe to register OS hooks,
	// i.e. after the OS has been set up. The routine receives a pointer to
	// the upper kernel ROM ($D800-FFFF).
	void CallInitHooks(const uint8 *lowerKernelROM, const uint8 *upperKernelROM);
	ATCPUHookInitNode *AddInitHook(const ATCPUHookInitFn& fn);
	void RemoveInitHook(ATCPUHookInitNode *hook);

	template<class T, typename Method>
	ATCPUHookNode *AddHookMethod(ATCPUHookMode mode, uint16 pc, sint8 priority, T *thisPtr, Method method);

	ATCPUHookNode *AddHook(ATCPUHookMode mode, uint16 pc, sint8 priority, const ATCPUHookFn& fn);
	void RemoveHook(ATCPUHookNode *hook);

	template<class T, typename Method>
	void SetHookMethod(ATCPUHookNode *& hook, ATCPUHookMode mode, uint16 pc, sint8 priority, T *thisPtr, Method method) {
		UnsetHook(hook);
		hook = AddHookMethod(mode, pc, priority, thisPtr, method);
	}

	void SetHook(ATCPUHookNode *& hook, ATCPUHookMode mode, uint16 pc, sint8 priority, const ATCPUHookFn& fn) {
		UnsetHook(hook);
		hook = AddHook(mode, pc, priority, fn);
	}

	void UnsetHook(ATCPUHookNode *& hook) {
		if (hook) {
			RemoveHook(hook);
			hook = NULL;
		}
	}

private:
	ATCPUEmulator *mpCPU;
	ATMMUEmulator *mpMMU;
	ATPBIManager *mpPBI;
	bool mbOSHooksEnabled;

	HashNode *mpFreeList;
	InitNode *mpInitChain;
	InitNode *mpInitFreeList;

	HashNode *mpHashTable[256];

	VDLinearAllocator mAllocator;
};

///////////////////////////////////////////////////////////////////////////
template<class T, typename Method>
ATCPUHookNode *ATCPUHookManager::AddHookMethod(ATCPUHookMode mode, uint16 pc, sint8 priority, T *thisPtr, Method method) {
	return AddHook(mode, pc, priority, [=](uint16 pc) { return (thisPtr->*method)(pc); });
}

#endif	// f_AT_CPUHOOKMANAGER_H
