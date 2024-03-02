//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2012 Avery Lee
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
#include "cpuheatmap.h"
#include "simeventmanager.h"
#include "console.h"

#define TRAP_LOAD(memstat) if (mbTrapOnUninitAccess && (memstat) < kTypePreset) TrapOnUninitAccess(opcode, addr, pc); else ((void)0)

ATCPUHeatMap::ATCPUHeatMap()
	: mpSimEvtMgr(nullptr)
	, mbTrapOnUninitAccess(false)
{
	Reset();
}

ATCPUHeatMap::~ATCPUHeatMap() {
}

void ATCPUHeatMap::Init(ATSimulatorEventManager *pSimEvtMgr) {
	mpSimEvtMgr = pSimEvtMgr;
}

void ATCPUHeatMap::Reset() {
	mA = kTypeUnknown;
	mX = kTypeUnknown;
	mY = kTypeUnknown;

	for(uint32 i=0; i<0x10000; ++i)
		mMemory[i] = kTypePreset + i;

	for(uint32 i=0; i<0x10000; ++i)
		mMemAccess[i] = 0;
}

void ATCPUHeatMap::ResetMemoryRange(uint32 addr, uint32 len) {
	if (addr >= 0x10000)
		return;

	if (0x10000 - addr < len)
		len = 0x10000 - addr;

	while(len--) {
		mMemory[addr] = kTypeUnknown;
		mMemAccess[addr] = 0;
		++addr;
	}
}

void ATCPUHeatMap::PresetMemoryRange(uint32 addr, uint32 len) {
	if (addr >= 0x10000)
		return;

	if (0x10000 - addr < len)
		len = 0x10000 - addr;

	while(len--) {
		mMemory[addr] = kTypePreset + addr;
		mMemAccess[addr] = 0;
		++addr;
	}
}

void ATCPUHeatMap::ProcessInsn(const ATCPUEmulator& cpu, uint8 opcode, uint16 addr, uint16 pc) {
	switch(opcode) {
		// stack operations
		case 0x00:	// BRK
		case 0x08:	// PHP
		case 0x28:	// PLP
			break;

		case 0x48:	// PHA
			mMemory[addr] = mA;
			mMemAccess[addr] |= kAccessWrite;
			break;

		case 0x68:	// PLA
			mA = mMemory[addr];
			mMemAccess[addr] |= kAccessRead;
			break;

		// load immediates
		case 0xA9:	// LDA imm
			mA = kTypeImm + pc;
			break;

		case 0xA2:	// LDX imm
			mX = kTypeImm + pc;
			break;

		case 0xA0:	// LDY imm
			mY = kTypeImm + pc;
			break;

		// transfer
		case 0x8A:	// TXA
			mA = mX;
			break;

		case 0x98:	// TYA
			mA = mY;
			break;

		case 0xA8:	// TAY
			mY = mA;
			break;

		case 0xAA:	// TAX
			mX = mA;
			break;

		case 0x9A:	// TXS
			mX = kTypeUnknown;
			break;

		case 0xBA:	// TSX
			break;

		// load A from memory
		case 0xA1:	// LDA (zp,X)
		case 0xA5:	// LDA zp
		case 0xAD:	// LDA abs
		case 0xB1:	// LDA (zp),Y
		case 0xB5:	// LDA zp,X
		case 0xB9:	// LDA abs,Y
		case 0xBD:	// LDA abs,X
			mA = mMemory[addr];
			TRAP_LOAD(mA);
			mMemAccess[addr] |= kAccessRead;
			break;

		// load X from memory
		case 0xA6:	// LDX zp
		case 0xAE:	// LDX abs
		case 0xB6:	// LDX zp,Y
		case 0xBE:	// LDX abs,Y
			mX = mMemory[addr];
			TRAP_LOAD(mX);
			mMemAccess[addr] |= kAccessRead;
			break;

		// load Y
		case 0xA4:	// LDY zp
		case 0xAC:	// LDY abs
		case 0xB4:	// LDY zp,X
		case 0xBC:	// LDY abs,X
			mY = mMemory[addr];
			TRAP_LOAD(mY);
			mMemAccess[addr] |= kAccessRead;
			break;

		// store A
		case 0x81:	// STA (zp,X)
		case 0x85:	// STA zp
		case 0x8D:	// STA abs
		case 0x91:	// STA (zp),Y
		case 0x95:	// STA zp,X
		case 0x99:	// STA abs,Y
		case 0x9D:	// STA abs,X
			mMemory[addr] = mA;
			mMemAccess[addr] |= kAccessWrite;
			break;

		// store X
		case 0x86:	// STX zp
		case 0x8E:	// STX abs
		case 0x96:	// STX zp,Y
			mMemory[addr] = mX;
			mMemAccess[addr] |= kAccessWrite;
			break;

		// store Y
		case 0x84:	// STY zp
		case 0x8C:	// STY abs
		case 0x94:	// STY zp,X
			mMemory[addr] = mY;
			mMemAccess[addr] |= kAccessWrite;
			break;

		// update A
		case 0x09:	// ORA imm
		case 0x0A:	// ASL A
		case 0x29:	// AND imm
		case 0x2A:	// ROL A
		case 0x49:	// EOR imm
		case 0x4A:	// LSR A
		case 0x69:	// ADC imm
		case 0x6A:	// ROR A
		case 0xE9:	// SBC imm
			break;

		// update A from memory
		case 0x01:	// ORA (zp,X)
		case 0x05:	// ORA zp
		case 0x0D:	// ORA abs
		case 0x11:	// ORA (zp),Y
		case 0x15:	// ORA zp,X
		case 0x19:	// ORA abs,Y
		case 0x1D:	// ORA abs,X
		case 0x21:	// AND (zp,X)
		case 0x25:	// AND zp
		case 0x2D:	// AND abs
		case 0x31:	// AND (zp),Y
		case 0x35:	// AND zp,X
		case 0x39:	// AND abs,Y
		case 0x3D:	// AND abs,X
		case 0x41:	// EOR (zp,X)
		case 0x45:	// EOR zp
		case 0x4D:	// EOR abs
		case 0x51:	// EOR (zp),Y
		case 0x55:	// EOR zp,X
		case 0x59:	// EOR abs,Y
		case 0x5D:	// EOR abs,X
		case 0x6D:	// ADC abs
		case 0x61:	// ADC (zp,X)
		case 0x65:	// ADC zp
		case 0x71:	// ADC (zp),Y
		case 0x75:	// ADC zp,X
		case 0x79:	// ADC abs,Y
		case 0x7D:	// ADC abs,X
		case 0xE1:	// SBC (zp,X)
		case 0xE5:	// SBC zp
		case 0xED:	// SBC abs
		case 0xF1:	// SBC (zp),Y
		case 0xF5:	// SBC zp,X
		case 0xF9:	// SBC abs,Y
		case 0xFD:	// SBC abs,X
			TRAP_LOAD(mMemory[addr]);
			mA = kTypeComputed + pc;
			break;

		// update X
		case 0xCA:	// DEX
		case 0xE8:	// INX
			mX = kTypeComputed + pc;
			break;

		// update Y
		case 0x88:	// DEY
		case 0xC8:	// INY
			mY = kTypeComputed + pc;
			break;

		// update P with memory load
		case 0x24:	// BIT zp
		case 0x2C:	// BIT abs
		case 0xC1:	// CMP (zp,X)
		case 0xC4:	// CPY zp
		case 0xC5:	// CMP zp
		case 0xCC:	// CPY abs
		case 0xCD:	// CMP abs
		case 0xD1:	// CMP (zp),Y
		case 0xD5:	// CMP zp,X
		case 0xD9:	// CMP abs,Y
		case 0xDD:	// CMP abs,X
		case 0xE4:	// CPX zp
		case 0xEC:	// CPX abs
			TRAP_LOAD(mMemory[addr]);
			break;

		// update P, no memory load (ignorable)
		case 0x18:	// CLC
		case 0x38:	// SEC
		case 0x58:	// CLI
		case 0x78:	// SEI
		case 0xB8:	// CLV
		case 0xC0:	// CPY imm
		case 0xC9:	// CMP imm
		case 0xD8:	// CLD
		case 0xE0:	// CPX imm
		case 0xF8:	// SED
			break;

		// branch instructions (ignorable)
		case 0x10:	// BPL rel
		case 0x20:	// JSR abs
		case 0x30:	// BMI rel
		case 0x40:	// RTI
		case 0x4C:	// JMP abs
		case 0x50:	// BVC
		case 0x60:	// RTS
		case 0x70:	// BVS
		case 0x90:	// BCC rel8
		case 0xB0:	// BCS rel8
		case 0xD0:	// BNE rel8
		case 0xF0:	// BEQ rel8
			break;

		// indirect branch instructions
		case 0x6C:	// JMP (abs)
			TRAP_LOAD(mMemory[addr]);
			TRAP_LOAD(mMemory[(addr+1) & 0xffff]);
			break;

		// ignorable operations
		case 0x42:	// HLE (emulator escape insn)
		case 0xEA:	// NOP
			break;

		// read-modify-write instructions
		case 0x06:	// ASL zp
		case 0x0E:	// ASL abs
		case 0x16:	// ASL zp,X
		case 0x1E:	// ASL abs,X
		case 0x26:	// ROL zp
		case 0x2E:	// ROL abs
		case 0x36:	// ROL zp,X
		case 0x3E:	// ROL abs,X
		case 0x46:	// LSR zp
		case 0x4E:	// LSR abs
		case 0x56:	// LSR zp,X
		case 0x5E:	// LSR abs,X
		case 0x66:	// ROR zp
		case 0x6E:	// ROR abs
		case 0x76:	// ROR zp,X
		case 0x7E:	// ROR abs,X
		case 0xC6:	// DEC zp
		case 0xD6:	// DEC zp,X
		case 0xCE:	// DEC abs
		case 0xDE:	// DEC abs,X
		case 0xE6:	// INC zp
		case 0xEE:	// INC abs
		case 0xF6:	// INC zp,X
		case 0xFE:	// INC abs,X
			TRAP_LOAD(mMemory[addr]);
			mMemory[addr] = kTypeComputed + pc;
			mMemAccess[addr] |= kAccessRead | kAccessWrite;
			break;
	}
}

void ATCPUHeatMap::TrapOnUninitAccess(uint8 opcode, uint16 addr, uint16 pc) {
	ATConsolePrintf("\n");
	ATConsolePrintf("VERIFIER: Read from uninitialized memory.\n");
	mpSimEvtMgr->NotifyEvent(kATSimEvent_VerifierFailure);
}
