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
	, mFlags(0)
{
}

ATCPUVerifier::~ATCPUVerifier() {
}

void ATCPUVerifier::Init(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, ATSimulator *sim) {
	mpCPU = cpu;
	mpMemory = mem;
	mpSimulator = sim;

	OnReset();
	ResetAllowedTargets();
}

void ATCPUVerifier::SetFlags(uint32 flags) {
	if (flags == mFlags)
		return;

	uint32 disabledFlags = mFlags & ~flags;
	mFlags = flags;

	if (disabledFlags & kATVerifierFlag_RecursiveNMI)
		mbInNMIRoutine = false;

	if (disabledFlags & kATVerifierFlag_UndocumentedKernelEntry)
		ResetAllowedTargets();

	if (disabledFlags & kATVerifierFlag_InterruptRegs)
		memset(mStackRegState, 0, sizeof mStackRegState);
}

void ATCPUVerifier::AddAllowedTarget(uint32 addr) {
	Addresses::iterator it(std::lower_bound(mAllowedTargets.begin(), mAllowedTargets.end(), addr));

	if (it == mAllowedTargets.end() || *it != addr)
		mAllowedTargets.insert(it, addr);
}

void ATCPUVerifier::RemoveAllowedTarget(uint32 addr) {
	Addresses::iterator it(std::lower_bound(mAllowedTargets.begin(), mAllowedTargets.end(), addr));

	if (it != mAllowedTargets.end() && *it == addr)
		mAllowedTargets.erase(it);
}

void ATCPUVerifier::RemoveAllowedTargets() {
	mAllowedTargets.clear();
}

void ATCPUVerifier::ResetAllowedTargets() {
	using namespace ATKernelSymbols;
	static const uint16 kDefaultTargets[]={
		// math pack (+Atari BASIC vectors)
		AFP,
		FASC,
		IPF,
		FPI,
		ZFR0,
		ZF1,
		ZFL,
		LDBUFA,
		FADD,
		FSUB,
		FMUL,
		FDIV,
		SKPSPC,
		ISDIGT,
		NORMALIZE,
		PLYEVL,
		FLD0R,
		FLD0P,
		FLD1R,
		FLD1P,
		FST0R,
		FST0P,
		FMOVE,
		EXP,
		EXP10,
		REDRNG,
		LOG,
		LOG10,

		// initialization vectors for E:/S:/K:/P:/C:
		0xE40C,
		0xE41C,
		0xE42C,
		0xE43C,
		0xE44C,

		// standard vectors
		DISKIV,
		DSKINV,
		CIOV,
		SIOV,
		SETVBV,
		SYSVBV,
		XITVBV,
		SIOINV,
		SENDEV,
		INTINV,
		CIOINV,
		BLKBDV,
		WARMSV,
		COLDSV,
		RBLOKV,
		CSOPIV,
		PUPDIV,
		SLFTSV,
		PENTV,
		PHUNLV,
		PHINIV
	};

	mAllowedTargets.assign(kDefaultTargets, kDefaultTargets + sizeof(kDefaultTargets)/sizeof(kDefaultTargets[0]));
	std::sort(mAllowedTargets.begin(), mAllowedTargets.end());
}

void ATCPUVerifier::GetAllowedTargets(vdfastvector<uint16>& exceptions) {
	exceptions = mAllowedTargets;
}

void ATCPUVerifier::OnReset() {
	mbInNMIRoutine = false;
		memset(mStackRegState, 0, sizeof mStackRegState);
}

void ATCPUVerifier::OnIRQEntry() {
	if (mFlags & kATVerifierFlag_InterruptRegs) {
		StackRegState& rs = mStackRegState[mpCPU->GetS()];
		rs.mA = mpCPU->GetA();
		rs.mX = mpCPU->GetX();
		rs.mY = mpCPU->GetY();
		rs.mbActive = true;
		rs.mPC = mpCPU->GetInsnPC();
		rs.mPad2 = 0;
	}
}

void ATCPUVerifier::OnNMIEntry() {
	if (mFlags & kATVerifierFlag_InterruptRegs) {
		StackRegState& rs = mStackRegState[mpCPU->GetS()];
		rs.mA = mpCPU->GetA();
		rs.mX = mpCPU->GetX();
		rs.mY = mpCPU->GetY();
		rs.mbActive = true;
		rs.mPC = mpCPU->GetInsnPC();
		rs.mPad2 = 0;
	}

	if (!(mFlags & kATVerifierFlag_RecursiveNMI))
		return;

	if (mbInNMIRoutine) {
		ATConsolePrintf("\n");
		ATConsolePrintf("VERIFIER: Recursive NMI handler execution detected.\n");
		ATConsolePrintf("          PC: %04X\n", mpCPU->GetPC());
		ATConsolePrintf("\n");
		mpSimulator->NotifyEvent(kATSimEvent_VerifierFailure);
	} else {
		mNMIStackLevel = mpCPU->GetS();
		mbInNMIRoutine = true;
	}
}

void ATCPUVerifier::OnReturn() {
	if (mFlags & kATVerifierFlag_InterruptRegs) {
		StackRegState& rs = mStackRegState[mpCPU->GetS()];
		uint8 a = mpCPU->GetA();
		uint8 x = mpCPU->GetX();
		uint8 y = mpCPU->GetY();

		if (rs.mbActive) {
			rs.mbActive = false;

			if (rs.mA != a || rs.mX != x || rs.mY != y) {
				ATConsolePrintf("\n");
				ATConsolePrintf("VERIFIER: Register mismatch between interrupt handler entry and exit.\n");
				ATConsolePrintf("          Entry: PC=%04x  A=%02x X=%02x Y=%02x\n", rs.mPC, rs.mA, rs.mX, rs.mY);
				ATConsolePrintf("          Exit:  PC=%04x  A=%02x X=%02x Y=%02x\n", mpCPU->GetInsnPC(), a, x, y);
				mpSimulator->NotifyEvent(kATSimEvent_VerifierFailure);
				return;
			}
		}
	}

	if (mFlags & kATVerifierFlag_RecursiveNMI) {
		if (mbInNMIRoutine) {
			uint8 s = mpCPU->GetS();

			if ((uint8)(s - mNMIStackLevel) < 8)
				mbInNMIRoutine = false;
		}
	}
}

void ATCPUVerifier::VerifyJump(uint16 addr) {
	if (!(mFlags & kATVerifierFlag_UndocumentedKernelEntry))
		return;

	uint16 pc = mpCPU->GetInsnPC();

	// we only care if the target is in kernel ROM space
	if (!mpSimulator->IsKernelROMLocation(addr))
		return;

	// ignore jumps from kernel ROM
	if (mpSimulator->IsKernelROMLocation(pc))
		return;

	// check for an allowed target
	if (std::binary_search(mAllowedTargets.begin(), mAllowedTargets.end(), addr))
		return;

	// trip a verifier failure
	ATConsolePrintf("\n");
	ATConsolePrintf("VERIFIER: Invalid jump into kernel ROM space detected.\n");
	ATConsolePrintf("          PC: %04X   Fault address: %04X\n", pc, addr);
	ATConsolePrintf("\n");
	mpSimulator->NotifyEvent(kATSimEvent_VerifierFailure);
}
