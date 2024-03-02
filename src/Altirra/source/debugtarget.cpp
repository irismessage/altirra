//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2015 Avery Lee
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

#include <stdafx.h>
#include <at/atcpu/execstate.h>
#include "cpu.h"
#include "debugtarget.h"
#include "simulator.h"

extern ATSimulator g_sim;

void *ATDebuggerDefaultTarget::AsInterface(uint32 iid) {
	if (iid == IATDebugTargetHistory::kTypeID)
		return static_cast<IATDebugTargetHistory *>(this);

	return nullptr;
}

const char *ATDebuggerDefaultTarget::GetName() {
	return "Main CPU";
}

ATDebugDisasmMode ATDebuggerDefaultTarget::GetDisasmMode() {
	switch(g_sim.GetCPU().GetCPUMode()) {
		case kATCPUMode_6502:
		default:
			return kATDebugDisasmMode_6502;

		case kATCPUMode_65C02:
			return kATDebugDisasmMode_65C02;

		case kATCPUMode_65C816:
			return kATDebugDisasmMode_65C816;
	}
}

void ATDebuggerDefaultTarget::GetExecState(ATCPUExecState& state) {
	ATCPUEmulator& cpu = g_sim.GetCPU();
	state.mPC = cpu.GetInsnPC();
	state.mA = cpu.GetA();
	state.mX = cpu.GetX();
	state.mY = cpu.GetY();
	state.mS = cpu.GetS();
	state.mP = cpu.GetP();

	state.mAH = cpu.GetAH();
	state.mXH = cpu.GetXH();
	state.mYH = cpu.GetYH();
	state.mSH = cpu.GetSH();
	state.mB = cpu.GetB();
	state.mK = cpu.GetK();
	state.mDP = cpu.GetD();
	state.mbEmulationFlag = cpu.GetEmulationFlag();
}

void ATDebuggerDefaultTarget::SetExecState(const ATCPUExecState& state) {
	ATCPUEmulator& cpu = g_sim.GetCPU();

	// we must guard this to avoid disturbing an instruction in progress
	if (state.mPC != cpu.GetInsnPC())
		cpu.SetPC(state.mPC);

	cpu.SetA(state.mA);
	cpu.SetX(state.mX);
	cpu.SetY(state.mY);
	cpu.SetS(state.mS);
	cpu.SetP(state.mP);
	cpu.SetAH(state.mAH);
	cpu.SetXH(state.mXH);
	cpu.SetYH(state.mYH);
	cpu.SetSH(state.mSH);
	cpu.SetB(state.mB);
	cpu.SetK(state.mK);
	cpu.SetD(state.mDP);
	cpu.SetEmulationFlag(state.mbEmulationFlag);
}

uint8 ATDebuggerDefaultTarget::ReadByte(uint32 address) {
	if (address < 0x1000000)
		return g_sim.DebugExtReadByte(address);

	return 0;
}

void ATDebuggerDefaultTarget::ReadMemory(uint32 address, void *dst, uint32 n) {
	const uint32 addrSpace = address & kATAddressSpaceMask;
	uint32 addrOffset = address;

	while(n--) {
		*(uint8 *)dst = ReadByte(addrSpace + (addrOffset++ & kATAddressOffsetMask));
		dst = (uint8 *)dst + 1;
	}
}

uint8 ATDebuggerDefaultTarget::DebugReadByte(uint32 address) {
	return g_sim.DebugGlobalReadByte(address);
}

void ATDebuggerDefaultTarget::DebugReadMemory(uint32 address, void *dst, uint32 n) {
	const uint32 addrSpace = address & kATAddressSpaceMask;
	uint32 addrOffset = address;

	while(n--) {
		*(uint8 *)dst = DebugReadByte(addrSpace + (addrOffset++ & kATAddressOffsetMask));
		dst = (uint8 *)dst + 1;
	}

}

void ATDebuggerDefaultTarget::WriteByte(uint32 address, uint8 value) {
	g_sim.DebugGlobalWriteByte(address, value);
}

void ATDebuggerDefaultTarget::WriteMemory(uint32 address, const void *src, uint32 n) {
	const uint32 addrSpace = address & kATAddressSpaceMask;
	uint32 addrOffset = address;

	while(n--) {
		WriteByte(addrSpace + (addrOffset++ & kATAddressOffsetMask), *(const uint8 *)src);
		src = (const uint8 *)src + 1;
	}
}

bool ATDebuggerDefaultTarget::GetHistoryEnabled() const {
	return g_sim.GetCPU().IsHistoryEnabled();
}

void ATDebuggerDefaultTarget::SetHistoryEnabled(bool enable) {
	g_sim.GetCPU().SetHistoryEnabled(enable);
}

std::pair<uint32, uint32> ATDebuggerDefaultTarget::GetHistoryRange() const {
	const auto& cpu = g_sim.GetCPU();
	const uint32 hcnt = cpu.GetHistoryCounter();
	const uint32 hlen = cpu.GetHistoryLength();

	return std::pair<uint32, uint32>(hcnt - hlen, hcnt);
}

uint32 ATDebuggerDefaultTarget::ExtractHistory(const ATCPUHistoryEntry **hparray, uint32 start, uint32 n) const {
	const auto& cpu = g_sim.GetCPU();
	const uint32 hcnt = cpu.GetHistoryCounter();
	uint32 hidx = (hcnt - 1) - start;

	for(uint32 i=n; i; --i)
		*hparray++ = &cpu.GetHistory(hidx--);

	return n;
}

uint32 ATDebuggerDefaultTarget::ConvertRawTimestamp(uint32 rawTimestamp) const {
	return rawTimestamp;
}
