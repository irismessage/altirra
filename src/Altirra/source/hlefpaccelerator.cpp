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
#include <vd2/system/vdalloc.h>
#include "hlefpaccelerator.h"
#include "ksyms.h"
#include "cpu.h"
#include "cpuhookmanager.h"
#include "decmath.h"

#define AT_ACCEL_FP_FOR_EACH_ENTRY(macroName)	\
	macroName(AFP)		\
	macroName(FASC)		\
	macroName(IPF)		\
	macroName(FPI)		\
	macroName(FADD)		\
	macroName(FSUB)		\
	macroName(FMUL)		\
	macroName(FDIV)		\
	macroName(LOG)		\
	macroName(SKPSPC)	\
	macroName(ISDIGT)	\
	macroName(NORMALIZE)\
	macroName(PLYEVL)	\
	macroName(ZFR0)		\
	macroName(ZF1)		\
	macroName(ZFL)		\
	macroName(LDBUFA)	\
	macroName(FLD0R)	\
	macroName(FLD0P)	\
	macroName(FLD1R)	\
	macroName(FLD1P)	\
	macroName(FST0R)	\
	macroName(FST0P)	\
	macroName(FMOVE)	\
	macroName(REDRNG)

class ATHLEFPAccelerator {
	ATHLEFPAccelerator(const ATHLEFPAccelerator&);
	ATHLEFPAccelerator& operator=(const ATHLEFPAccelerator&);
public:
	ATHLEFPAccelerator();
	~ATHLEFPAccelerator();

	void Init(ATCPUEmulator *cpu);
	void Shutdown();

private:
	void OnHook(uint16 pc);

#define AT_ACCEL_FP_DECLARE_HOOK(name) uint8 OnHook##name(uint16);
	AT_ACCEL_FP_FOR_EACH_ENTRY(AT_ACCEL_FP_DECLARE_HOOK)

	ATCPUEmulator *mpCPU;

	ATCPUHookNode *mpHookNodes[25];
};

ATHLEFPAccelerator::ATHLEFPAccelerator()
	: mpCPU(NULL)
{
	std::fill(mpHookNodes, mpHookNodes + sizeof(mpHookNodes)/sizeof(mpHookNodes[0]), (ATCPUHookNode *)NULL);
}

ATHLEFPAccelerator::~ATHLEFPAccelerator() {
	Shutdown();
}

void ATHLEFPAccelerator::Init(ATCPUEmulator *cpu) {
	mpCPU = cpu;

#define AT_ACCEL_FP_TABLE_ENTRY(name) { ATKernelSymbols::name, &ATHLEFPAccelerator::OnHook##name },

	static const struct {
		uint16 mPC;
		uint8 (ATHLEFPAccelerator::*mpMethod)(uint16);
	} kMethods[]={
		AT_ACCEL_FP_FOR_EACH_ENTRY(AT_ACCEL_FP_TABLE_ENTRY)
	};

	VDASSERTCT(sizeof(kMethods)/sizeof(kMethods[0]) == sizeof(mpHookNodes)/sizeof(mpHookNodes[0]));

	ATCPUHookManager& hookMgr = *mpCPU->GetHookManager();
	for(size_t i=0; i<sizeof(mpHookNodes)/sizeof(mpHookNodes[0]); ++i) {
		hookMgr.SetHookMethod(mpHookNodes[i], kATCPUHookMode_MathPackROMOnly, kMethods[i].mPC, 0, this, kMethods[i].mpMethod);
	}
}

void ATHLEFPAccelerator::Shutdown() {
	if (mpCPU) {
		ATCPUHookManager& hookMgr = *mpCPU->GetHookManager();

		for(size_t i=0; i<sizeof(mpHookNodes)/sizeof(mpHookNodes[0]); ++i) {
			hookMgr.UnsetHook(mpHookNodes[i]);
		}

		mpCPU = NULL;
	}
}

#define AT_ACCEL_FP_DEFINE_HOOK(name) \
	uint8 ATHLEFPAccelerator::OnHook##name(uint16) {	\
		ATAccel##name(*mpCPU, *mpCPU->GetMemory());	\
		return 0x60;	\
	}

AT_ACCEL_FP_FOR_EACH_ENTRY(AT_ACCEL_FP_DEFINE_HOOK)

ATHLEFPAccelerator *ATCreateHLEFPAccelerator(ATCPUEmulator *cpu) {
	vdautoptr<ATHLEFPAccelerator> accel(new ATHLEFPAccelerator);

	accel->Init(cpu);

	return accel.release();
}

void ATDestroyHLEFPAccelerator(ATHLEFPAccelerator *accel) {
	delete accel;
}
