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

#ifndef f_AT_MMU_H
#define f_AT_MMU_H

class ATMemoryLayer;
class ATMemoryManager;
class IATHLEKernel;

class ATMMUEmulator {
	ATMMUEmulator(const ATMMUEmulator&);
	ATMMUEmulator& operator=(const ATMMUEmulator&);
public:
	ATMMUEmulator();
	~ATMMUEmulator();

	void Init(int hardwareMode, int memoryMode, void *extBase, ATMemoryManager *memman, ATMemoryLayer *extLayer, ATMemoryLayer *selfTestLayer, ATMemoryLayer *lowerKernelLayer, ATMemoryLayer *upperKernelLayer, ATMemoryLayer *basicLayer, IATHLEKernel *hle);
	void Shutdown();

	uint32 GetCPUBankBase() const { return mCPUBase; }
	uint32 GetAnticBankBase() const { return mAnticBase; }

	void SetBankRegister(uint8 bank);
	void ForceBASIC();

protected:
	int mHardwareMode;
	int mMemoryMode;
	ATMemoryManager *mpMemMan;
	uint8 *mpMemory;
	ATMemoryLayer *mpLayerExtRAM;
	ATMemoryLayer *mpLayerSelfTest;
	ATMemoryLayer *mpLayerLowerKernel;
	ATMemoryLayer *mpLayerUpperKernel;
	ATMemoryLayer *mpLayerBASIC;
	IATHLEKernel *mpHLE;
	bool		mbBASICForced;

	uint32		mCPUBase;
	uint32		mAnticBase;
};

#endif
