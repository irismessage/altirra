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

ATMMUEmulator::ATMMUEmulator()
	: mCPUBase(0)
	, mAnticBase(0)
{
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
						 ATMemoryLayer *gameLayer,
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
	mpLayerGame = gameLayer;
	mpHLE = hle;

	mCPUBase = 0;
	mAnticBase = 0;
	mbBASICForced = false;

	uint8 extbankmask = 0;

	switch(mMemoryMode) {
		case kATMemoryMode_128K:		// 4 banks * 16K = 64K
			extbankmask = 0x03;
			break;

		case kATMemoryMode_320K:		// 16 banks * 16K = 256K
		case kATMemoryMode_320K_Compy:
			extbankmask = 0x0F;
			break;

		case kATMemoryMode_576K:		// 32 banks * 16K = 512K
		case kATMemoryMode_576K_Compy:
			extbankmask = 0x1F;
			break;

		case kATMemoryMode_1088K:		// 64 banks * 16K = 1024K
			extbankmask = 0x3F;
			break;
	}

	const int kernelBankMask = (mHardwareMode == kATHardwareMode_800XL ||
		mHardwareMode == kATHardwareMode_XEGS) ? 0x00 : 0x01;

	const int cpuBankBit = (extbankmask != 0) ? 0x10 : 0x00;
	int anticBankBit = 0;

	switch(mMemoryMode) {
		case kATMemoryMode_128K:
		case kATMemoryMode_320K_Compy:
		case kATMemoryMode_576K_Compy:
			// Separate ANTIC access (bit 5)
			anticBankBit = 0x20;
			break;

		case kATMemoryMode_320K:
		case kATMemoryMode_576K:
		case kATMemoryMode_1088K:
			// CPU/ANTIC access combined (bit 4)
			anticBankBit = 0x10;
			break;
	}

	uint32 extbankoffset = 0x04;
	switch(mMemoryMode) {
		case kATMemoryMode_128K:
		case kATMemoryMode_320K:
		case kATMemoryMode_320K_Compy:
			// We push up RAMBO memory by 256K in low memory modes to make VBXE
			// emulation easier (it emulates RAMBO in its high 256K).
			extbankoffset = 0x14;
			break;
	}

	bool blockExtBasic = false;
	switch(mMemoryMode) {
		case kATMemoryMode_576K:
		case kATMemoryMode_576K_Compy:
		case kATMemoryMode_1088K:
			blockExtBasic = true;
			break;
	}

	for(int portb=0; portb<256; ++portb) {
		const bool cpuEnabled = (~portb & cpuBankBit) != 0;
		const bool anticEnabled = (~portb & anticBankBit) != 0;
		uint32 extbank = 0;
		uint16 encodedBankInfo = 0;

		if (cpuEnabled || anticEnabled) {
			// Bank bits:
			// 128K:		bits 2, 3
			// 192K:		bits 2, 3, 6
			// 320K:		bits 2, 3, 5, 6
			// 576K:		bits 1, 2, 3, 5, 6
			// 1088K:		bits 1, 2, 3, 5, 6, 7
			// 320K COMPY:	bits 2, 3, 6, 7
			// 576K COMPY:	bits 1, 2, 3, 6, 7
			encodedBankInfo = ((~portb & 0x0c) >> 2);

			switch(mMemoryMode) {
				case kATMemoryMode_320K_Compy:
				case kATMemoryMode_576K_Compy:
					encodedBankInfo += (uint32)(~portb & 0xc0) >> 4;

					if (!(portb & 0x02))
						encodedBankInfo += 0x10;
					break;

				default:
					encodedBankInfo += (uint32)(~portb & 0x60) >> 3;

					if (!(portb & 0x02))
						encodedBankInfo += 0x10;

					if (!(portb & 0x80))
						encodedBankInfo += 0x20;

					break;
			}

			encodedBankInfo &= extbankmask;
			encodedBankInfo += extbankoffset;

			if (cpuEnabled)
				encodedBankInfo |= kMapInfo_ExtendedCPU;

			if (anticEnabled)
				encodedBankInfo |= kMapInfo_ExtendedANTIC;
		}

		if (mpLayerSelfTest) {
			// NOTE: The kernel ROM must be enabled for the self-test ROM to appear.
			// Storm breaks if this is not checked.
			if ((portb & 0x81) == 0x01) {
				// If bit 7 is reused for banking, it must not enable the self-test
				// ROM when extbanking is enabled.
				switch(mMemoryMode) {
					case kATMemoryMode_320K_Compy:
					case kATMemoryMode_576K_Compy:
					case kATMemoryMode_1088K:
						if (cpuEnabled || anticEnabled)
							break;

						// fall through
					default:
						encodedBankInfo += kMapInfo_SelfTest;
						break;
				}
			}
		}

		if (!(portb & 0x02)) {
			if (!blockExtBasic || (!cpuEnabled && !anticEnabled))
				encodedBankInfo += kMapInfo_BASIC;
		}

		if ((portb | kernelBankMask) & 0x01)
			encodedBankInfo += kMapInfo_Kernel;

		mBankMap[portb] = encodedBankInfo;
	}
}

void ATMMUEmulator::Shutdown() {
	mpMemory = NULL;
	mpLayerExtRAM = NULL;
	mpLayerSelfTest = NULL;
	mpLayerLowerKernel = NULL;
	mpLayerUpperKernel = NULL;
	mpLayerBASIC = NULL;
	mpHLE = NULL;

	mCPUBase = 0;
	mAnticBase = 0;
}

void ATMMUEmulator::SetBankRegister(uint8 bank) {
	const uint32 bankInfo = mBankMap[bank];
	const uint32 bankOffset = ((bankInfo & kMapInfo_BankMask) << 14);
	uint8 *bankbase = mpMemory + bankOffset;

	mpMemMan->SetLayerMemory(mpLayerExtRAM, bankbase, 0x40, 0x40);

	mCPUBase = 0;
	mAnticBase = 0;

	if (bankInfo & kMapInfo_ExtendedCPU) {
		mCPUBase = bankOffset;
		mpMemMan->EnableLayer(mpLayerExtRAM, kATMemoryAccessMode_CPURead, true);
		mpMemMan->EnableLayer(mpLayerExtRAM, kATMemoryAccessMode_CPUWrite, true);
	} else {
		mpMemMan->EnableLayer(mpLayerExtRAM, kATMemoryAccessMode_CPURead, false);
		mpMemMan->EnableLayer(mpLayerExtRAM, kATMemoryAccessMode_CPUWrite, false);
	}

	if (bankInfo & kMapInfo_ExtendedANTIC) {
		mAnticBase = bankOffset;
		mpMemMan->EnableLayer(mpLayerExtRAM, kATMemoryAccessMode_AnticRead, true);
	} else {
		mpMemMan->EnableLayer(mpLayerExtRAM, kATMemoryAccessMode_AnticRead, false);
	}

	const bool selfTestEnabled = (bankInfo & kMapInfo_SelfTest) != 0;

	if (mpLayerSelfTest)
		mpMemMan->EnableLayer(mpLayerSelfTest, selfTestEnabled);

	if (mpHLE)
		mpHLE->EnableSelfTestROM(selfTestEnabled);

	const bool kernelEnabled = (bankInfo & kMapInfo_Kernel) != 0;

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
		mpMemMan->EnableLayer(mpLayerBASIC, (bankInfo & kMapInfo_BASIC) != 0);
	}

	if (mpLayerGame)
		mpMemMan->EnableLayer(mpLayerGame, !(bank & 0x40));
}

void ATMMUEmulator::ForceBASIC() {
	mbBASICForced = true;

	mpMemMan->EnableLayer(mpLayerBASIC, true);
}
