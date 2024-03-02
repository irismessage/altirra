//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2010 Avery Lee
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
#include "cpu.h"
#include "console.h"
#include "verifier.h"
#include "simulator.h"
#include "ksyms.h"

ATCPUVerifier::ATCPUVerifier()
	: mpCPU(NULL)
	, mpMemory(NULL)
	, mpSimulator(NULL)
{
}

ATCPUVerifier::~ATCPUVerifier() {
}

void ATCPUVerifier::Init(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, ATSimulator *sim) {
	mpCPU = cpu;
	mpMemory = mem;
	mpSimulator = sim;
}

void ATCPUVerifier::VerifyJump(uint16 addr) {
	using namespace ATKernelSymbols;
	uint16 pc = mpCPU->GetInsnPC();

	// we only care if the target is in kernel ROM space
	if (!mpSimulator->IsKernelROMLocation(addr))
		return;

	// ignore jumps from kernel ROM
	if (mpSimulator->IsKernelROMLocation(pc))
		return;

	// check for a known vector
	switch(addr) {
		// floating-point ROM
		case AFP:
		case FASC:
		case IPF:
		case FPI:
		case ZFR0:
		case ZF1:
		case ZFL:
		case LDBUFA:
		case FADD:
		case FSUB:
		case FMUL:
		case FDIV:
		case SKPSPC:
		case ISDIGT:
		case NORMALIZE:
		case PLYEVL:
		case FLD0R:
		case FLD0P:
		case FLD1R:
		case FLD1P:
		case FST0R:
		case FST0P:
		case FMOVE:
		case EXP:
		case EXP10:
		case REDRNG:
		case LOG:
		case LOG10:
			return;

		// kernel ROM
		case DISKIV:
		case DSKINV:
		case CIOV:
		case SIOV:
		case SETVBV:
		case SYSVBV:
		case XITVBV:
		case SIOINV:
		case SENDEV:
		case INTINV:
		case CIOINV:
		case BLKBDV:
		case WARMSV:
		case COLDSV:
		case RBLOKV:
		case CSOPIV:
			return;
	}

	// trip a verifier failure
	ATConsolePrintf("\n");
	ATConsolePrintf("VERIFIER: Invalid jump into kernel ROM space detected.\n");
	ATConsolePrintf("          PC: %04X   Fault address: %04X\n", pc, addr);
	ATConsolePrintf("\n");
	mpSimulator->NotifyEvent(kATSimEvent_VerifierFailure);
}
