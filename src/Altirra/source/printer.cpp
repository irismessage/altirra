//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009 Avery Lee
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
#include <vd2/system/error.h>
#include <vd2/system/filesys.h>
#include "printer.h"
#include "kerneldb.h"
#include "oshelper.h"
#include "cio.h"

using namespace ATCIOSymbols;

class ATPrinterEmulator : public IATPrinterEmulator {
	ATPrinterEmulator(const ATPrinterEmulator&);
	ATPrinterEmulator& operator=(const ATPrinterEmulator&);
public:
	ATPrinterEmulator();
	~ATPrinterEmulator();

	bool IsEnabled() const;
	void SetEnabled(bool enabled);

	void SetHookPageByte(uint8 page) {
		mHookPageByte = page;
	}

	void SetOutput(IATPrinterOutput *output) {
		mpOutput = output;
	}

	void WarmReset();
	void ColdReset();

	void OnCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset);
	uint8 OnCIOCommand(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, uint8 iocbIdx, uint8 command);

protected:
	void DoOpen(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, uint8 iocb);
	void DoClose(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, uint8 iocb);
	void DoPutRecord(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, uint8 iocb);
	void DoPutChars(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, uint8 iocb);
	void DoPutByte(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem);

	void Write(const uint8 *c, uint32 count);

	IATPrinterOutput *mpOutput;

	bool		mbEnabled;
	uint8		mHookPageByte;
	uint32		mLineBufIdx;
	uint8		mLineBuf[132];
};

IATPrinterEmulator *ATCreatePrinterEmulator() {
	return new ATPrinterEmulator;
}

ATPrinterEmulator::ATPrinterEmulator()
	: mpOutput(NULL)
	, mbEnabled(false)
{
	ColdReset();
}

ATPrinterEmulator::~ATPrinterEmulator() {
	ColdReset();
}

bool ATPrinterEmulator::IsEnabled() const {
	return mbEnabled;
}

void ATPrinterEmulator::SetEnabled(bool enable) {
	if (mbEnabled == enable)
		return;

	mbEnabled = enable;

	ColdReset();
}

void ATPrinterEmulator::WarmReset() {
	ColdReset();
}

void ATPrinterEmulator::ColdReset() {
	mLineBufIdx = 0;
}

void ATPrinterEmulator::OnCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset) {
	if (!mbEnabled)
		return;

	switch(offset) {
		case 6:
			DoPutByte(cpu, mem);
			break;
	}
}

uint8 ATPrinterEmulator::OnCIOCommand(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, uint8 iocb, uint8 cmd) {
	if (!mbEnabled)
		return 0;

	switch(cmd) {
		case CIOCmdOpen:
			mem->WriteByte(ATKernelSymbols::ICPTL + iocb, 0x55);
			mem->WriteByte(ATKernelSymbols::ICPTH + iocb, mHookPageByte);
			cpu->SetY(CIOStatSuccess);
			cpu->SetP(cpu->GetP() & ~(AT6502::kFlagN | AT6502::kFlagZ));
			break;

		case CIOCmdClose:
			cpu->SetY(CIOStatSuccess);
			cpu->SetP(cpu->GetP() & ~(AT6502::kFlagN | AT6502::kFlagZ));
			break;

		case CIOCmdPutRecord:
			DoPutRecord(cpu, mem, iocb);
			cpu->SetY(CIOStatSuccess);
			cpu->SetP(cpu->GetP() & ~(AT6502::kFlagN | AT6502::kFlagZ));
			break;

		case CIOCmdPutChars:
			DoPutChars(cpu, mem, iocb);
			cpu->SetY(CIOStatSuccess);
			cpu->SetP(cpu->GetP() & ~(AT6502::kFlagN | AT6502::kFlagZ));
			break;

		default:
			cpu->SetY(CIOStatNotSupported);
			cpu->SetP((cpu->GetP() | AT6502::kFlagN) & ~AT6502::kFlagZ);
			break;
	}

	mem->WriteByte(ATKernelSymbols::ICSTA + iocb, cpu->GetY());
	return 0x60;
}

void ATPrinterEmulator::DoClose(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, uint8 iocb) {
}

void ATPrinterEmulator::DoPutRecord(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, uint8 iocb) {
	ATKernelDatabase kdb(mem);
	uint16 addr = kdb.ICBAL[iocb].r16();
	uint16 len = kdb.ICBLL[iocb].r16();
	uint8 buf[256];

	while(len) {
		uint32 tc = len > 256 ? 256 : len;
		len -= tc;

		for(uint32 i=0; i<tc; ++i) {
			uint8 c = mem->ReadByte((uint16)(addr + i));

			buf[i] = c;

			if (c == 0x9B) {
				len = 0;
				tc = i;
				break;
			}
		}

		Write(buf, tc);
		addr += tc;
	}
}

void ATPrinterEmulator::DoPutChars(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, uint8 iocb) {
	ATKernelDatabase kdb(mem);
	uint16 addr = kdb.ICBAL[iocb].r16();
	uint16 len = kdb.ICBLL[iocb].r16();
	uint8 buf[256];

	while(len) {
		uint32 tc = len > 256 ? 256 : len;

		for(uint32 i=0; i<tc; ++i)
			buf[i] = mem->ReadByte((uint16)(addr + i));

		Write(buf, tc);
		addr += tc;
		len -= tc;
	}
}

void ATPrinterEmulator::DoPutByte(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem) {
	uint8 c = cpu->GetA();
	Write(&c, 1);

	cpu->SetY(CIOStatSuccess);
	cpu->SetP(cpu->GetP() & ~(AT6502::kFlagN | AT6502::kFlagZ));
}

void ATPrinterEmulator::Write(const uint8 *s, uint32 count) {
	while(count--) {
		uint8 c = *s++;

		if (c == 0x9B)
			c = '\n';
		else if (c < 0x20 || c > 0x7F)
			c = '?';

		mLineBuf[mLineBufIdx++] = c;

		if (mLineBufIdx >= 130) {
			c = '\n';
			mLineBuf[mLineBufIdx++] = c;
		}

		if (c == '\n') {
			mLineBuf[mLineBufIdx] = 0;

			if (mpOutput)
				mpOutput->WriteLine((const char *)mLineBuf);

			mLineBufIdx = 0;
		}
	}
}
