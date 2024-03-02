//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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
#include "hlekernel.h"
#include "cpu.h"
#include "simulator.h"
#include "ksyms.h"
#include "kerneldb.h"
#include "decmath.h"

extern ATSimulator g_sim;

class ATHLEKernel : public IATCPUHighLevelEmulator, public IATHLEKernel {
public:
	ATHLEKernel();
	~ATHLEKernel();

	IATCPUHighLevelEmulator *AsCPUHLE() { return this; }

	void Init(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem);

	void EnableLowerROM(bool enable);
	void EnableUpperROM(bool enable);
	void EnableSelfTestROM(bool enable);
	int InvokeHLE(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem, uint16 pcm, uint32 code);

protected:
	uint16 ReadWord(uint16 addr) {
		return mpMemory->ReadByte(addr) + 256*mpMemory->ReadByte(addr+1);
	}

	void WriteWord(uint16 addr, uint16 val) {
		mpMemory->WriteByte(addr+0, (uint8)val);
		mpMemory->WriteByte(addr+1, (uint8)(val >> 8));
	}

#include "hlekernel_gen.h"

	ATCPUEmulator *mpCPU;
	ATCPUEmulatorMemory *mpMemory;
	bool mbLowerROM;
	bool mbSelfTestROM;
	bool mbUpperROM;
};

IATHLEKernel *ATCreateHLEKernel() {
	return new ATHLEKernel;
}

ATHLEKernel::ATHLEKernel()
	: mbLowerROM(false)
	, mbSelfTestROM(false)
	, mbUpperROM(false)
{
}

ATHLEKernel::~ATHLEKernel() {
}

void ATHLEKernel::Init(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	mpCPU = &cpu;
	mpMemory = &mem;
}

namespace ATKernelFillHelpers {
	class ByteWriter {
	public:
		ByteWriter(uint8 *dst) : mpDst(dst) {}

		ByteWriter& operator=(uint8 c) {
			*mpDst = c;
			return *this;
		}

		ByteWriter operator,(uint8 c) {
			mpDst[1] = c;
			return ByteWriter(mpDst + 1);
		}

	protected:
		uint8 *mpDst;
	};

	class KernelWriter {
	public:
		KernelWriter(uint8 *kernel) : mpKernel(kernel) {}

		ByteWriter operator()(uint32 addr) {
			if (addr >= 0x5000 && addr < 0x5800)
				return ByteWriter(mpKernel + (addr - 0x4000));

			if (addr >= 0xC000 && addr < 0xD000)
				return ByteWriter(mpKernel + (addr - 0xC000));

			if (addr >= 0xD800 && addr < 0x10000)
				return ByteWriter(mpKernel + (addr - 0xC000));

			return ByteWriter(NULL);
		}

	protected:
		uint8 *mpKernel;
	};
}

void ATHLEKernel::EnableLowerROM(bool enable) {
	mbLowerROM = enable;
}

void ATHLEKernel::EnableUpperROM(bool enable) {
	mbUpperROM = enable;
}

void ATHLEKernel::EnableSelfTestROM(bool enable) {
	mbSelfTestROM = enable;
}

int ATHLEKernel::InvokeHLE(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem, uint16 pc, uint32 code) {
	Dispatch(code);
	return -1;
}

#include "hlekernel_gen.inl"
