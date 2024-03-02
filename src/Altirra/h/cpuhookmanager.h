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

struct ATCPUHookNode {};

typedef uint8 (*ATCPUHookFn)(uint16 pc, void *context1, ATCPUHookNode *hookNode);

class ATCPUEmulator;
class ATMMUEmulator;
class ATPBIManager;

enum ATCPUHookMode {
	kATCPUHookMode_Always,
	kATCPUHookMode_KernelROMOnly,
	kATCPUHookMode_MathPackROMOnly
};

class ATCPUHookManager {
	ATCPUHookManager(const ATCPUHookManager&);
	ATCPUHookManager& operator=(const ATCPUHookManager&);

#ifdef VD_COMPILER_MSVC
	struct DummyV0 {};
	struct __virtual_inheritance DummyV : public virtual DummyV0 {};
#else
	struct DummyV {};
#endif

	struct HashNode : public ATCPUHookNode {

		HashNode *mpNext;
		uint16 mPC;
		uint8 mMode;
		sint8 mPriority;

		ATCPUHookFn mpHookFn;

		void *mpContext;
		DummyV mpMethod;
	};

public:
	ATCPUHookManager();
	~ATCPUHookManager();

	void Init(ATCPUEmulator *cpu, ATMMUEmulator *mmu, ATPBIManager *pbi);
	void Shutdown();

	uint8 OnHookHit(uint16 pc) const;

	template<class T, typename Method>
	ATCPUHookNode *AddHookMethod(ATCPUHookMode mode, uint16 pc, sint8 priority, T *thisPtr, Method method);

	ATCPUHookNode *AddHook(ATCPUHookMode mode, uint16 pc, sint8 priority, ATCPUHookFn fn, void *context);
	void RemoveHook(ATCPUHookNode *hook);

	template<class T, typename Method>
	void SetHookMethod(ATCPUHookNode *& hook, ATCPUHookMode mode, uint16 pc, sint8 priority, T *thisPtr, Method method) {
		UnsetHook(hook);
		hook = AddHookMethod(mode, pc, priority, thisPtr, method);
	}

	void SetHook(ATCPUHookNode *& hook, ATCPUHookMode mode, uint16 pc, sint8 priority, ATCPUHookFn fn, void *context) {
		UnsetHook(hook);
		hook = AddHook(mode, pc, priority, fn, context);
	}

	void UnsetHook(ATCPUHookNode *& hook) {
		if (hook) {
			RemoveHook(hook);
			hook = NULL;
		}
	}

private:
	template<class T, typename Method>
	static uint8 MethodAdapter(uint16 pc, void *thisPtr0, ATCPUHookNode *hookNode);

	ATCPUEmulator *mpCPU;
	ATMMUEmulator *mpMMU;
	ATPBIManager *mpPBI;
	HashNode *mpFreeList;

	HashNode *mpHashTable[256];

	VDLinearAllocator mAllocator;
};

///////////////////////////////////////////////////////////////////////////
template<class T, typename Method>
uint8 ATCPUHookManager::MethodAdapter(uint16 pc, void *thisPtr0, ATCPUHookNode *hookNode) {
	HashNode *hashNode = static_cast<HashNode *>(hookNode);
	Method method = *(const Method *)&hashNode->mpMethod;
	T *thisPtr = (T *)thisPtr0;

	return (thisPtr->*method)(pc);
}

///////////////////////////////////////////////////////////////////////////
template<class T, typename Method>
ATCPUHookNode *ATCPUHookManager::AddHookMethod(ATCPUHookMode mode, uint16 pc, sint8 priority, T *thisPtr, Method method) {
	ATCPUHookNode *node = AddHook(mode, pc, priority, MethodAdapter<T, Method>, thisPtr);
	*(Method *)&static_cast<HashNode *>(node)->mpMethod = method;
	return node;
}

#endif	// f_AT_CPUHOOKMANAGER_H
