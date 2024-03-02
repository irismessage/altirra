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
#include "mmu.h"
#include "memorymanager.h"
#include "simulator.h"
#include "hlekernel.h"

ATMMUEmulator::ATMMUEmulator() {
	Shutdown();
}

ATMMUEmulator::~ATMMUEmulator() {
}

void ATMMUEmulator::Init(int hwmode, int memoryMode, void *mem,
						 ATMemoryManager *memman,
						 ATMemoryLayer *extLayer,
						 ATMemoryLayer *selfTestLayer,
						 ATMemoryLayer *lowerKernelLayer,
						 ATMemoryLayer *upperKernelLayer,
						 ATMemoryLayer *basicLayer,
						 IATHLEKernel *hle) {
	mHardwareMode = hwmode;
	mMemoryMode = memoryMode;
	mpMemory = (uint8 *)mem;
	mpMemMan = memman;
	mpLayerExtRAM = extLayer;
	mpLayerSelfTest = selfTestLayer;
	mpLayerLowerKernel = lowerKernelLayer;
	mpLayerUpperKernel = upperKernelLayer;
	mpLayerBASIC = basicLayer;
	mpHLE = hle;

	mCPUBase = 0;
	mAnticBase = 0;
	mbBASICForced = false;
}

void ATMMUEmulator::Shutdown() {
	mpMemory = NULL;
	mpLayerExtRAM = NULL;
	mpLayerSelfTest = NULL;
	mpLayerLowerKernel = NULL;
	mpLayerUpperKernel = NULL;
	mpLayerBASIC = NULL;
	mpHLE = NULL;
}

void ATMMUEmulator::SetBankRegister(uint8 bank) {
	bool cpuEnabled = false;
	switch(mMemoryMode) {
		case kATMemoryMode_128K:
		case kATMemoryMode_320K:
		case kATMemoryMode_576K:
		case kATMemoryMode_1088K:
			if (!(bank & 0x10))
				cpuEnabled = true;
			break;
	}

	bool anticEnabled = false;
	switch(mMemoryMode) {
		case kATMemoryMode_1088K:
		case kATMemoryMode_576K:
		case kATMemoryMode_320K:
			anticEnabled = !(bank & 0x10);
			break;

		case kATMemoryMode_128K:
			anticEnabled = !(bank & 0x20);
			break;
	}

	mCPUBase = 0;
	mAnticBase = 0;

	if (cpuEnabled || anticEnabled) {
		uint8 *bankbase = mpMemory + ((~bank & 0x0c) << 12) + 0x10000;

		if (mMemoryMode >= kATMemoryMode_320K) {
			bankbase += (uint32)(~bank & 0x60) << 11;

			if (mMemoryMode >= kATMemoryMode_576K) {
				if (!(bank & 0x02))
					bankbase += 0x40000;
				if (mMemoryMode >= kATMemoryMode_1088K) {
					if (!(bank & 0x80))
						bankbase += 0x80000;
				}
			}
		}

		if (mMemoryMode < kATMemoryMode_576K) {
			// We push up RAMBO memory by 256K in low memory modes to make VBXE
			// emulation easier (it emulates RAMBO in its high 256K).
			bankbase += 0x40000;
		}

		mpMemMan->SetLayerMemory(mpLayerExtRAM, bankbase, 0x40, 0x40);

		if (cpuEnabled)
			mCPUBase = bankbase - mpMemory;

		if (anticEnabled)
			mAnticBase = bankbase - mpMemory;
	}

	mpMemMan->EnableLayer(mpLayerExtRAM, kATMemoryAccessMode_CPURead, cpuEnabled);
	mpMemMan->EnableLayer(mpLayerExtRAM, kATMemoryAccessMode_CPUWrite, cpuEnabled);
	mpMemMan->EnableLayer(mpLayerExtRAM, kATMemoryAccessMode_AnticRead, anticEnabled);

	bool selfTestEnabled = false;

	if (mpLayerSelfTest) {
		// NOTE: The kernel ROM must be enabled for the self-test ROM to appear.
		// Storm breaks if this is not checked.
		if ((bank & 0x81) == 0x01 && (mMemoryMode != kATMemoryMode_1088K || !cpuEnabled))
			selfTestEnabled = true;

		mpMemMan->EnableLayer(mpLayerSelfTest, selfTestEnabled);
	}

	if (mpHLE)
		mpHLE->EnableSelfTestROM(selfTestEnabled);

	bool kernelEnabled = true;

	if (mHardwareMode == kATHardwareMode_800XL) {
		if (!(bank & 0x01))
			kernelEnabled = false;
	}

	if (mpLayerLowerKernel) {
		mpMemMan->EnableLayer(mpLayerLowerKernel, kernelEnabled);

		if (mpHLE)
			mpHLE->EnableLowerROM(kernelEnabled);
	} else {
		if (mpHLE)
			mpHLE->EnableLowerROM(false);
	}

	if (mpLayerUpperKernel) {
		mpMemMan->EnableLayer(mpLayerUpperKernel, kernelEnabled);

		if (mpHLE)
			mpHLE->EnableUpperROM(kernelEnabled);
	} else {
		if (mpHLE)
			mpHLE->EnableUpperROM(false);
	}

	if (mpLayerBASIC && !mbBASICForced) {
		bool basicEnabled = false;

		if (!(bank & 0x02) && ((mMemoryMode != kATMemoryMode_1088K && mMemoryMode != kATMemoryMode_576K) || !cpuEnabled))
			basicEnabled = true;

		mpMemMan->EnableLayer(mpLayerBASIC, basicEnabled);
	}
}

void ATMMUEmulator::ForceBASIC() {
	mbBASICForced = true;

	mpMemMan->EnableLayer(mpLayerBASIC, true);
}
