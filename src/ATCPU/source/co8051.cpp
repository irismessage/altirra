//	Altirra - Atari 800/800XL/5200 emulator
//	Coprocessor library - 8051 emulator
//	Copyright (C) 2024 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <https://www.gnu.org/licenses/>.
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <at/atcore/wraptime.h>
#include <at/atcpu/breakpoints.h>
#include <at/atcpu/co8051.h>
#include <at/atcpu/execstate.h>
#include <at/atcpu/history.h>
#include <at/atcpu/memorymap.h>

ATCoProc8051::ATCoProc8051() {
	mpFnReadPort = [this](uint8, uint8 output) -> uint8 { return output; };
	mpFnWritePort = [](uint8, uint8) {};
	mpFnReadBus = [] { return 0xFF; };
	mpFnReadXRAM = [](uint16 addr) -> uint8 { return 0xFF; };
	mpFnWriteXRAM = [](uint16 addr, uint8 data) {};
	mpFnSerialXmit = [](uint8 v) {};
	mpFnReadT0 = [] { return true; }; 
	mpFnReadT1 = [] { return true; }; 
}

void ATCoProc8051::SetProgramROM(const void *rom) {
	mpProgramROM = (const uint8 *)rom;
}

void ATCoProc8051::SetHistoryBuffer(ATCPUHistoryEntry buffer[131072]) {
	mpHistory = buffer;
}

void ATCoProc8051::GetExecState(ATCPUExecState& state) const {
	memset(&state, 0, sizeof state);

	ATCPUExecState8051& state8051 = state.m8051;
	state8051.mPC = mPC;
	state8051.mA = mA;
	state8051.mP0 = mP0;
	state8051.mP1 = mP1;
	memcpy(state8051.mReg[0], mRAM, 8);
	memcpy(state8051.mReg[1], mRAM + 16, 8);
}

void ATCoProc8051::SetExecState(const ATCPUExecState& state) {
	const ATCPUExecState8051& state8051 = state.m8051;

	mPC = state8051.mPC & 0x7FF;

	mA = state8051.mA;
	mPSW = state8051.mPSW | 0x08;

	memcpy(mRAM, state8051.mReg[0], 8);
	memcpy(mRAM + 16, state8051.mReg[1], 8);
}

void ATCoProc8051::SetT0ReadHandler(const vdfunction<bool()>& fn) {
	mpFnReadT0 = fn;
}

void ATCoProc8051::SetT1ReadHandler(const vdfunction<bool()>& fn) {
	mpFnReadT1 = fn;
}

void ATCoProc8051::SetXRAMReadHandler(const vdfunction<uint8(uint16)>& fn) {
	mpFnReadXRAM= fn;
}

void ATCoProc8051::SetXRAMWriteHandler(const vdfunction<void(uint16, uint8)>& fn) {
	mpFnWriteXRAM = fn;
}

void ATCoProc8051::SetPortReadHandler(const vdfunction<uint8(uint8, uint8)>& fn) {
	mpFnReadPort = fn;
}

void ATCoProc8051::SetPortWriteHandler(const vdfunction<void(uint8, uint8)>& fn) {
	mpFnWritePort = fn;
}

void ATCoProc8051::SetBusReadHandler(const vdfunction<uint8()>& fn) {
	mpFnReadBus = fn;
}

void ATCoProc8051::SetBreakpointMap(const bool bpMap[65536], IATCPUBreakpointHandler *bpHandler) {
	mpBreakpointMap = bpMap;
	mpBreakpointHandler = bpHandler;
}

void ATCoProc8051::SetSerialXmitHandler(vdfunction<void(uint8)> fn) {
	mpFnSerialXmit = std::move(fn);
}

void ATCoProc8051::SendSerialByteDone() {
	// set transmit interrupt flag (SCON.1)
	if (!(mSCON & 0x02)) {
		mSCON |= 0x02;

		AssertInternalIrq(IntMask::Serial);
	}
}

void ATCoProc8051::ReceiveSerialByte(uint8 v) {
	mSerialReadBuffer = v;

	// set receive interrupt flag (SCON.0)
	if (!(mSCON & 0x01)) {
		mSCON |= 0x01;

		AssertInternalIrq(IntMask::Serial);
	}
}

uint8 ATCoProc8051::ReadByte(uint8 addr) const {
	return mRAM[addr];
}

void ATCoProc8051::WriteByte(uint8 addr, uint8 val) {
	mRAM[addr] = val;
}

void ATCoProc8051::ColdReset() {
	memset(mRAM, 0, sizeof mRAM);
	mA = 0;

	WarmReset();
}

void ATCoProc8051::WarmReset() {
	mPSW = 0x08;
	mSP = 0;
	mPC = 0;
	mpRegBank = &mRAM[0];

	mbTimerActive = false;
	mTimerLastUpdated = mCyclesBase - mCyclesLeft;

	mTCON = 0;
	mTH0 = 0;
	mTL0 = 0;

	mP0 = 0xFF;
	mP1 = 0xFF;
	mpFnWritePort(0, 0xFF);
	mpFnWritePort(1, 0xFF);

	mTStatesLeft = 4;
}

void ATCoProc8051::AssertIrq() {
	mbIrqPending = true;
}

void ATCoProc8051::NegateIrq() {
	mbIrqPending = false;
}

void ATCoProc8051::Run() {
	static const uint8 kInsnBytes[256]={
//		0 1 2 3 4 5 6 7 8 9 A B C D E F
		1,2,3,1,1,2,1,1,1,1,1,1,1,1,1,1,	// 0x
		3,2,3,1,1,2,1,1,1,1,1,1,1,1,1,1,	// 1x
		3,2,1,1,2,2,1,1,1,1,1,1,1,1,1,1,	// 2x
		3,2,1,2,2,1,1,1,1,1,1,1,1,1,1,1,	// 3x
		2,2,2,3,2,2,1,1,1,1,1,1,1,1,1,1,	// 4x
		2,2,2,3,2,2,1,1,1,1,1,1,1,1,1,1,	// 5x
		2,2,2,3,2,2,1,1,1,1,1,1,1,1,1,1,	// 6x
		2,2,2,1,2,3,2,2,2,2,2,2,2,2,2,2,	// 7x
		2,2,2,1,1,3,2,2,2,2,2,2,2,2,2,2,	// 8x
		3,2,2,1,2,2,1,1,1,1,1,1,1,1,1,1,	// 9x
		2,2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,	// Ax
		2,2,2,1,3,3,3,3,3,3,3,3,3,3,3,3,	// Bx
		2,2,2,1,1,2,1,1,1,1,1,1,1,1,1,1,	// Cx
		2,2,2,1,1,3,1,1,2,2,2,2,2,2,2,2,	// Dx
		1,2,1,1,1,2,1,1,1,1,1,1,1,1,1,1,	// Ex
		1,2,1,1,1,2,1,1,1,1,1,1,1,1,1,1,	// Fx
	};

	static const uint8 kInsnCycles[256]={
//		0 1 2 3 4 5 6 7 8 9 A B C D E F
		1,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,	// 0x
		2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,	// 1x
		2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,	// 2x
		2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,	// 3x
		2,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,	// 4x
		2,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,	// 5x
		2,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,	// 6x
		2,2,2,2,1,2,1,1,1,1,1,1,1,1,1,1,	// 7x
		2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,	// 8x
		2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,	// 9x
		2,2,1,2,4,1,2,2,2,2,2,2,2,2,2,2,	// Ax
		2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2,	// Bx
		2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,	// Cx
		2,2,1,1,1,2,1,1,2,2,2,2,2,2,2,2,	// Dx
		2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,	// Ex
		2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,	// Fx
	};

	mCyclesLeft += mCyclesSaved;
	mCyclesSaved = 0;

	while(mCyclesLeft > 0) {
		if (mbIrqPending)
			DispatchInterrupt();

		const uint8 opcode = mpProgramROM[mPC];
		const uint8 insnCycles = kInsnCycles[opcode];

		mTStatesLeft = insnCycles;

		if (mCyclesLeft < mTStatesLeft) {
			mCyclesSaved = mCyclesLeft;
			mCyclesLeft = 0;
			break;
		}

		if (mpBreakpointMap && mpBreakpointMap[mPC]) {
			bool shouldExit = CheckBreakpoint();

			if (shouldExit) {
				goto force_exit;
			}
		}

		const uint8 operand = mpProgramROM[mPC + 1];

		if (mpHistory) {
			ATCPUHistoryEntry * VDRESTRICT he = &mpHistory[mHistoryIndex++ & 131071];

			he->mCycle = mCyclesBase - mCyclesLeft;
			he->mUnhaltedCycle = mCyclesBase - mCyclesLeft;
			he->mEA = 0xFFFFFFFFUL;
			he->mPC = mPC;
			he->mA = mA;
			he->mP = mPSW;
			he->m8051_P1 = mP0;
			he->m8051_P2 = mP1;
			he->mExt.m8051_R0 = mpRegBank[0];
			he->mExt.m8051_R1 = mpRegBank[1];
			he->mExt.m8051_R2 = mpRegBank[2];
			he->mExt.m8051_R3 = mpRegBank[3];
			he->m8051_R4 = mpRegBank[4];
			he->m8051_R5 = mpRegBank[5];
			he->m8051_DPTR = mDPTR;
			he->mB = 0;
			he->mK = 0;
			he->mS = mSP;
			he->mbIRQ = mbHistoryIrqPending;
			mbHistoryIrqPending = false;

			he->mOpcode[0] = opcode;
			he->mOpcode[1] = operand;
			he->mOpcode[2] = mpProgramROM[mPC + 2];
		}

		mCyclesLeft -= insnCycles;

		mPC += kInsnBytes[opcode];

		switch(opcode) {
			case 0x24:	// ADD A,#data
				DoAdd(operand);
				break;

			case 0x25:	// ADD A,direct
				DoAdd(ReadDirect(operand));
				break;

			case 0x28:	// ADD A,R0
			case 0x29:	// ADD A,R1
			case 0x2A:	// ADD A,R2
			case 0x2B:	// ADD A,R3
			case 0x2C:	// ADD A,R4
			case 0x2D:	// ADD A,R5
			case 0x2E:	// ADD A,R6
			case 0x2F:	// ADD A,R7
				DoAdd(mpRegBank[opcode - 0x28]);
				break;

			case 0x34:	// ADDC A,#data
				DoAddCarry(operand);
				break;

			case 0x38:	// ADDC A,R0
			case 0x39:	// ADDC A,R1
			case 0x3A:	// ADDC A,R2
			case 0x3B:	// ADDC A,R3
			case 0x3C:	// ADDC A,R4
			case 0x3D:	// ADDC A,R5
			case 0x3E:	// ADDC A,R6
			case 0x3F:	// ADDC A,R7
				DoAddCarry(mpRegBank[opcode - 0x38]);
				break;

			case 0x01:	// AJMP addr11
			case 0x21:	// AJMP addr11
			case 0x41:	// AJMP addr11
			case 0x61:	// AJMP addr11
			case 0x81:	// AJMP addr11
			case 0xA1:	// AJMP addr11
			case 0xC1:	// AJMP addr11
			case 0xE1:	// AJMP addr11
				mPC = (mPC & 0xF800) + ((uint32)(opcode - 0x01) << 3) + operand;
				break;

			case 0x52:	// ANL direct,A
				WriteDirect(operand, ReadDirectRMW(operand) & mA);
				break;

			case 0x54:	// ANL A,#data
				mA &= operand;
				break;

			case 0x55:	// ANL A,direct
				mA &= ReadDirect(operand);
				break;

			case 0x56:	// ANL A,@R0
			case 0x57:	// ANL A,@R1
				mA &= ReadIndirect(mpRegBank[opcode - 0x56]);
				break;

			case 0x53:	// ANL PSW,#data
				WritePSW(ReadPSW() & operand);
				break;

			case 0xB4:	// CJNE A,#data,rel
				if (mA != operand)
					mPC += (sint16)(sint8)mpProgramROM[mPC - 1];

				WriteCarry(mA < operand);
				break;

			case 0xB5:	// CJNE A,direct,rel
				{
					const uint8 v = ReadDirect(operand);

					if (mA != v)
						mPC += (sint16)(sint8)mpProgramROM[mPC - 1];

					WriteCarry(mA < v);
				}
				break;

			case 0xB8:	// CJNE R0,#data,rel
			case 0xB9:	// CJNE R1,#data,rel
			case 0xBA:	// CJNE R2,#data,rel
			case 0xBB:	// CJNE R3,#data,rel
			case 0xBC:	// CJNE R4,#data,rel
			case 0xBD:	// CJNE R5,#data,rel
			case 0xBE:	// CJNE R6,#data,rel
			case 0xBF:	// CJNE R7,#data,rel
				{
					uint8 rn = mpRegBank[opcode - 0xB8];

					if (rn != operand)
						mPC += (sint16)(sint8)mpProgramROM[mPC - 1];

					WriteCarry(rn < operand);
				}
				break;

			case 0xC2:	// CLR bit
				WriteBit(operand, false);
				break;

			case 0xE4:	// CLR A
				mA = 0;
				break;

			case 0xC3:	// CLR C
				WriteCarry(false);
				break;

			case 0xF4:	// CPL A
				mA = ~mA;
				break;

			case 0x14:	// DEC A
				--mA;
				break;

			case 0x15:	// DEC direct
				WriteDirect(operand, ReadDirectRMW(operand) - 1);
				break;

			case 0x18:	// DEC R0
			case 0x19:	// DEC R1
			case 0x1A:	// DEC R2
			case 0x1B:	// DEC R3
			case 0x1C:	// DEC R4
			case 0x1D:	// DEC R5
			case 0x1E:	// DEC R6
			case 0x1F:	// DEC R7
				--mpRegBank[opcode - 0x18];
				break;

			case 0x84:	// DIV AB
				{
					// clear C and OV
					mPSW &= 0x7B;

					// if B=0, set OV and trash A/B
					if (!mB) {
						mA = 0xFF;
						mB = 0xFF;
						mPSW |= 0x04;
					} else {
						uint8 q = mA / mB;
						uint8 r = mA % mB;
						mA = q;
						mB = r;
					}
				}
				break;

			case 0xD5:	// DJNZ direct,rel
				{
					uint8 v = ReadDirect(operand) - 1;
					WriteDirect(operand, v);

					if (v)
						mPC += (sint16)(sint8)mpProgramROM[mPC - 1];
				}
				break;

			case 0xD8:	// DJNZ R0,rel
			case 0xD9:	// DJNZ R1,rel
			case 0xDA:	// DJNZ R2,rel
			case 0xDB:	// DJNZ R3,rel
			case 0xDC:	// DJNZ R4,rel
			case 0xDD:	// DJNZ R5,rel
			case 0xDE:	// DJNZ R6,rel
			case 0xDF:	// DJNZ R7,rel
				if (--mpRegBank[opcode - 0xD8])
					mPC += (sint16)(sint8)mpProgramROM[mPC - 1];
				break;

			case 0x06:	// INC @R0
			case 0x07:	// INC @R1
				{
					uint8 addr = mpRegBank[opcode - 0x06];

					WriteIndirect(addr, ReadIndirect(addr) + 1);
				}
				break;

			case 0x05:	// INC direct
				WriteDirect(operand, ReadDirectRMW(operand) + 1);
				break;

			case 0x04:	// INC A
				++mA;
				break;

			case 0xA3:	// INC DPTR
				++mDPTR;
				break;

			case 0x08:	// INC R0
			case 0x09:	// INC R1
			case 0x0A:	// INC R2
			case 0x0B:	// INC R3
			case 0x0C:	// INC R4
			case 0x0D:	// INC R5
			case 0x0E:	// INC R6
			case 0x0F:	// INC R7
				++mpRegBank[opcode - 0x08];
				break;

			case 0x20:	// JB bit,rel
				if (ReadBit(operand))
					mPC += (sint16)(sint8)mpProgramROM[mPC - 1];
				break;

			case 0x10:	// JBC bit,rel
				if (ReadBitRMW(operand)) {
					// docs explicitly say bit is not cleared if already zero
					WriteBit(operand, false);

					mPC += (sint16)(sint8)mpProgramROM[mPC - 1];
				}
				break;

			case 0x40:	// JC rel
				if (mPSW & 0x80)
					mPC += (sint16)(sint8)mpProgramROM[mPC - 1];
				break;

			case 0x30:	// JNB bit,rel
				if (!ReadBit(operand))
					mPC += (sint16)(sint8)mpProgramROM[mPC - 1];
				break;

			case 0x50:	// JNC rel
				if (!ReadCarry())
					mPC += (sint16)(sint8)operand;
				break;

			case 0x70:	// JNZ rel
				if (mA)
					mPC += (sint16)(sint8)operand;
				break;

			case 0x60:	// JZ rel
				if (!mA)
					mPC += (sint16)(sint8)operand;
				break;

			case 0x12:	// LCALL addr16
				Push((uint8)mPC);
				Push((uint8)(mPC >> 8));
				mPC = VDReadUnalignedBEU16(&mpProgramROM[mPC - 2]);
				break;

			case 0x02:	// LJUMP addr16
				mPC = VDReadUnalignedBEU16(&mpProgramROM[mPC - 2]);
				break;

			case 0xA6:	// MOV @R0,direct
			case 0xA7:	// MOV @R1,direct
				WriteIndirect(mpRegBank[opcode - 0xA6], ReadDirect(operand));
				break;

			case 0xF6:	// MOV @R0,A
				WriteIndirect(mpRegBank[0], mA);
				break;

			case 0x92:	// MOV bit,C
				WriteBit(operand, ReadCarry());
				break;

			case 0x75:	// MOV direct,#imm
				WriteDirect(operand, mpProgramROM[mPC - 1]);
				break;

			case 0x85:	// MOV direct,direct
				WriteDirect(mpProgramROM[mPC - 1], ReadDirectRMW(operand));
				break;

			case 0xF5:	// MOV direct,A
				WriteDirect(operand, mA);
				break;

			case 0x88:	// MOV direct,R0
			case 0x89:	// MOV direct,R1
			case 0x8A:	// MOV direct,R2
			case 0x8B:	// MOV direct,R3
			case 0x8C:	// MOV direct,R4
			case 0x8D:	// MOV direct,R5
			case 0x8E:	// MOV direct,R6
			case 0x8F:	// MOV direct,R7
				WriteDirect(operand, mpRegBank[opcode - 0x88]);
				break;

			case 0x74:	// MOV A,#data
				mA = operand;
				break;

			case 0xE5:	// MOV A,direct
				mA = ReadDirect(operand);
				break;

			case 0xE8:	// MOV A,R0
			case 0xE9:	// MOV A,R1
			case 0xEA:	// MOV A,R2
			case 0xEB:	// MOV A,R3
			case 0xEC:	// MOV A,R4
			case 0xED:	// MOV A,R5
			case 0xEE:	// MOV A,R6
			case 0xEF:	// MOV A,R7
				mA = mpRegBank[opcode - 0xE8];
				break;

			case 0xA2:	// MOV C,bit
				WriteCarry(ReadBit(operand));
				break;

			case 0x90:	// MOV DPTR,#data16
				mDPTR = VDReadUnalignedBEU16(&mpProgramROM[mPC - 2]);
				break;

			case 0x78:	// MOV R0,#imm
			case 0x79:	// MOV R1,#imm
			case 0x7A:	// MOV R2,#imm
			case 0x7B:	// MOV R3,#imm
			case 0x7C:	// MOV R4,#imm
			case 0x7D:	// MOV R5,#imm
			case 0x7E:	// MOV R6,#imm
			case 0x7F:	// MOV R7,#imm
				mpRegBank[opcode - 0x78] = operand;
				break;

			case 0xA8:	// MOV R0,direct
			case 0xA9:	// MOV R1,direct
			case 0xAA:	// MOV R2,direct
			case 0xAB:	// MOV R3,direct
			case 0xAC:	// MOV R4,direct
			case 0xAD:	// MOV R5,direct
			case 0xAE:	// MOV R6,direct
			case 0xAF:	// MOV R7,direct
				mpRegBank[opcode - 0xA8] = ReadDirect(operand);
				break;

			case 0xF8:	// MOV R0,A
			case 0xF9:	// MOV R1,A
			case 0xFA:	// MOV R2,A
			case 0xFB:	// MOV R3,A
			case 0xFC:	// MOV R4,A
			case 0xFD:	// MOV R5,A
			case 0xFE:	// MOV R6,A
			case 0xFF:	// MOV R7,A
				mpRegBank[opcode - 0xF8] = mA;
				break;

			case 0x93:	// MOVC A,@A+DPTR
				mA = mpProgramROM[(mDPTR + mA) & 0xFFFF];
				break;

			case 0x83:	// MOVC A,@A+PC
				mA = mpProgramROM[(mPC + mA) & 0xFFFF];
				break;

			case 0xF0:	// MOVX @DPTR,A
				WriteXRAM(mDPTR, mA);
				break;

			case 0xE0:	// MOVX A,@DPTR
				mA = ReadXRAM(mDPTR);
				break;

			case 0xA4:	// MUL AB
				{
					uint32 v = (uint32)mA * mB;

					mA = (uint8)v;
					mB = (uint8)(v >> 8);

					// clear C, set OV if B>0
					mPSW &= 0x7B;

					if (mB)
						mPSW |= 0x04;
				}
				break;

			case 0x00:	// NOP
				break;

			case 0x43:	// ORL direct,#data
				WriteDirect(operand, ReadDirectRMW(operand) | mpProgramROM[mPC - 1]);
				break;

			case 0x44:	// ORL A,#imm
				mA |= operand;
				break;

			case 0x46:	// ORL A,@R0
			case 0x47:	// ORL A,@R1
				mA |= ReadIndirect(mpRegBank[opcode - 0x46]);
				break;

			case 0x48:	// ORL A,R0
			case 0x49:	// ORL A,R1
			case 0x4A:	// ORL A,R2
			case 0x4B:	// ORL A,R3
			case 0x4C:	// ORL A,R4
			case 0x4D:	// ORL A,R5
			case 0x4E:	// ORL A,R6
			case 0x4F:	// ORL A,R7
				mA |= mpRegBank[opcode - 0x48];
				break;

			case 0xD0:	// POP direct
				// special case this to handle POP SP
				WriteDirect(operand, ReadIndirect(mSP));
				--mSP;
				break;

			case 0xC0:	// PUSH direct
				Push(ReadDirect(operand));
				break;

			case 0x22:	// RET
				mPC = Pop();
				mPC = (mPC << 8) + Pop();
				break;

			case 0x32:	// RETI
				// pop PC
				mPC = Pop();
				mPC = (mPC << 8) + Pop();

				// clear the highest priority interrupt FF active, if any, and then hold off
				// interrupts for one instruction; documented as same as RET if no interrupts
				// being serviced
				if (mbIntServicingHighPri) {
					mbIntServicingHighPri = false;
					mbIrqHoldoff = true;
					mbIrqPending = true;
				} else if (mbIntServicingLowPri) {
					mbIntServicingHighPri = false;
					mbIrqHoldoff = true;
					mbIrqPending = true;
				}
				break;

			case 0x23:	// RL A
				mA = (mA << 1) + (mA >> 7);
				break;

			case 0x33:	// RLC A
				{
					uint8 v = (mA << 1) + (mPSW & 0x80 ? 1 : 0);
					
					mPSW &= 0x7F;
					mPSW |= mA & 0x80;

					mA = v;
				}
				break;

			case 0x03:	// RR A
				mA = (mA >> 1) + (mA << 7);
				break;

			case 0x13:	// RRC A
				{
					uint8 v = (mA >> 1) + (mPSW & 0x80);
					
					mPSW &= 0x7F;
					if (mA & 1)
						mPSW |= 0x80;

					mA = v;
				}
				break;

			case 0xD2:	// SETB bit
				WriteBit(operand, true);
				break;

			case 0xD3:	// SETB C
				WriteCarry(true);
				break;

			case 0x80:	// SJMP rel
				mPC += (sint16)(sint8)operand;
				break;

			case 0x95:	// SUBB A,direct
				DoSubBorrow(ReadDirect(operand));
				break;

			default:
				[[unlikely]]
				mCyclesLeft = 0;
				mPC -= kInsnBytes[opcode];
				break;
		}
	}

force_exit:
	;
}

void ATCoProc8051::DoAdd(uint8 v) {
	uint8 a = mA;
	uint32 sum = (uint32)a + v;
	uint32 carries = sum ^ (a ^ v);

	mA = sum;

	mPSW &= 0x3B;

	// set carry
	if (sum & 0x100)
		mPSW += 0x80;

	// set auxiliary carry
	if (carries & 0x10)
		mPSW += 0x40;

	// set overflow
	switch(carries & 0x180) {
		case 0x80:
		case 0x100:
			mPSW += 0x04;
			break;

		default:
			break;
	}
}

void ATCoProc8051::DoAddCarry(uint8 v) {
	uint8 a = mA;
	uint32 sum = (uint32)a + v + (mPSW & 0x80 ? 1 : 0);
	uint32 carries = sum ^ (a ^ v);

	mA = sum;

	mPSW &= 0x3B;

	// set carry
	if (sum & 0x100)
		mPSW += 0x80;

	// set auxiliary carry
	if (carries & 0x10)
		mPSW += 0x40;

	// set overflow
	switch(carries & 0x180) {
		case 0x80:
		case 0x100:
			mPSW += 0x04;
			break;

		default:
			break;
	}
}

void ATCoProc8051::DoSubBorrow(uint8 v) {
	uint8 a = mA;
	uint8 c = (mPSW & 0x80 ? 1 : 0);
	uint32 diff = (uint32)a - v - c;
	uint32 carryIn = diff ^ a ^ v;

	mA = diff;

	mPSW &= 0x3B;

	// set carry
	if (diff & 0x100)
		mPSW += 0x80;

	// set auxiliary carry
	if (carryIn & 0x10)
		mPSW += 0x40;

	// set overflow
	switch(carryIn & 0x180) {
		case 0x80:
		case 0x100:
			mPSW += 0x04;
			break;

		default:
			break;
	}
}

bool ATCoProc8051::CheckBreakpoint() {
	if (mpBreakpointHandler->CheckBreakpoint(mPC)) {
		return true;
	}

	return false;
}

void ATCoProc8051::NegateInternalIrq(IntMask mask) {
	mActiveInterruptMask &= ~mask;
}

void ATCoProc8051::AssertInternalIrq(IntMask mask) {
	// early out if interrupt is already active
	if (+(mActiveInterruptMask & mask))
		return;

	// set interrupt as active
	mActiveInterruptMask |= mask;

	// check if an interrupt is not already pending
	if (mbIrqPending)
		return;

	// check if interrupt and global interrupts are both enabled
	if ((mIE & (uint8)(mActiveInterruptMask | IntMask::Global)) > (uint8)IntMask::Global) {
		// check if interrupt is not pre-empted by priority and current servicing
		if (mIP & +mActiveInterruptMask) {
			// high priority
			if (!mbIntServicingHighPri)
				mbIrqPending = true;
		} else {
			// low priority
			if (!mbIntServicingHighPri && !mbIntServicingLowPri)
				mbIrqPending = true;
		}
	}
}

void ATCoProc8051::DispatchInterrupt() {
	if (mbIrqPending) {
		// check if a one-instruction holdoff is in effect
		if (mbIrqHoldoff) {
			// yes, clear the holdoff flag but leave pending enabled so interrupts
			// can be re-evaluated
			mbIrqHoldoff = false;
			return;
		}

		// after this point, we will dispatch an interrupt if we can, otherwise no interrupt
		// is eligible and we can stop checking
		mbIrqPending = false;

		// check if interrupts are globally disabled or a high-priority interrupt is already
		// being serviced
		if (!(mIE & 0x80) || mbIntServicingHighPri)
			return;

		// check if a high-priority interrupt is active
		const uint8 activeEnabledInts = mIE & +mActiveInterruptMask;
		uint8 intToService = mIP & activeEnabledInts;
		if (!intToService) {
			// no high-priority interrupts active -- exit if low-priority interrupts are blocked
			if (mbIntServicingLowPri)
				return;

			// exit if no interrupts are active
			intToService = +activeEnabledInts;
			if (!intToService)
				return;
		}

		// indicate interrupt in next history entry
		mbHistoryIrqPending = true;

		// execute LCALL to interrupt vector
		Push((uint8)mPC);
		Push((uint8)(mPC >> 8));

		// select interrupt vector according to priority:
		//	INT0 -> 0003H
		//	Timer 0 -> 000BH
		//	INT1 -> 0013H
		//	Timer 1 -> 001BH
		//	Serial port -> 0023H

		static constexpr uint8 kInterruptVectors[] {
			0x00, 0x03, 0x0B, 0x03, 0x13, 0x03, 0x0B, 0x03, 0x1B, 0x03, 0x0B, 0x03, 0x13, 0x03, 0x0B, 0x03,
			0x23, 0x03, 0x0B, 0x03, 0x13, 0x03, 0x0B, 0x03, 0x1B, 0x03, 0x0B, 0x03, 0x13, 0x03, 0x0B, 0x03
		};
		static_assert(vdcountof(kInterruptVectors) == 32);

		mPC = kInterruptVectors[intToService & 31];
	}
}

uint32 ATCoProc8051::GetTick() const {
	return mCyclesBase - mCyclesLeft;
}

uint8 ATCoProc8051::ReadPSW() const {
	return mPSW;
}

void ATCoProc8051::WritePSW(uint8 v) {
	if (mPSW != v) {
		uint8 delta = mPSW ^ v;
		mPSW = v;

		if (delta & 0x18)
			mpRegBank = &mRAM[v & 0x18];
	}
}

void ATCoProc8051::WriteIE(uint8 v) {
	// Any write to IE causes a one-insn interrupt holdoff. Note that this needs to
	// happen even if IE wasn't changed.
	mbIrqHoldoff = true;
	mbIrqPending = true;

	mIE = v;
}

void ATCoProc8051::WriteIP(uint8 v) {
	// Any write to IP causes a one-insn interrupt holdoff. Note that this needs to
	// happen even if IP wasn't changed.
	mbIrqHoldoff = true;
	mbIrqPending = true;

	mIP = v;
}

uint8 ATCoProc8051::ReadTCON() const {
	uint8 v = mTCON;

	// if timer 0 is running (TCON.4 = 1) and not overflowed (TCON.5 = 0), adjust if it should be
	if ((v & 0x30) == 0x10 && ATWrapTime{GetTick()} >= mTimerDeadline)
		v |= 0x20;

	return v;
}

void ATCoProc8051::WriteTCON(uint8 v) {
	uint8 delta = v ^ mTCON;
	if (!delta)
		return;

	UpdateTimer();

	mTCON = v;

	if (delta & 0x30)
		UpdateTimerDeadline();
}

uint8 ATCoProc8051::ReadTL0() const {
	if (mTCON & 0x10)
		return (uint8)((GetTick() - mTimerDeadline) & 0x1F);
	else
		return mTL0;
}

uint8 ATCoProc8051::ReadTH0() const {
	if (mTCON & 0x10)
		return (uint8)((GetTick() - mTimerDeadline) >> 5);
	else
		return mTH0;
}

void ATCoProc8051::WriteTL0(uint8 v) {
	if (mTL0 != v) {
		if (mTCON & 0x10)
			UpdateTimer();

		mTL0 = v;

		if (mTCON & 0x10)
			UpdateTimerDeadline();
	}
}

void ATCoProc8051::WriteTH0(uint8 v) {
	if (mTH0 != v) {
		if (mTCON & 0x10)
			UpdateTimer();

		mTH0 = v;

		if (mTCON & 0x10)
			UpdateTimerDeadline();
	}
}

void ATCoProc8051::UpdateTimer() {
	const uint32 t = GetTick();
	const uint32 dt = t - mTimerLastUpdated;
	mTimerLastUpdated = t;

	// check if T0 is running
	if (mTCON & 0x10) {
		// update timer counter 0
		uint32 timerCounter0 = ((mTL0 & 0x1F) + ((uint32)mTH0 << 5)) + dt;

		if (timerCounter0 >= 0x10000) {
			// set TF0 (TCON.5)
			mTCON |= 0x20;
		}

		mTH0 = (uint8)(timerCounter0 >> 5);
		mTL0 = (uint8)(timerCounter0 & 0x1F);
	}
}

uint8 ATCoProc8051::ReadP0() const {
	return mpFnReadPort(0, mP0);
}

uint8 ATCoProc8051::ReadP1() const {
	return mpFnReadPort(1, mP1);
}

uint8 ATCoProc8051::ReadP2() const {
	return 0xFF;
}

uint8 ATCoProc8051::ReadP3() const {
	// P3.0 - RXD
	// P3.1 - TXD
	// P3.2 - /INT0
	// P3.3 - /INT1
	// P3.4 - T0
	// P3.5 - T1
	// P3.6 - /WR
	// P3.7 - /RD

	return 0xCF
		+ (mpFnReadT0() ? 0x10 : 0x00)
		+ (mpFnReadT1() ? 0x20 : 0x00)
		;
}

void ATCoProc8051::WriteP0(uint8 v) {
	if (mP0 != v) {
		mP0 = v;

		mpFnWritePort(0, v);
	}
}

void ATCoProc8051::WriteP1(uint8 v) {
	if (mP1 != v) {
		mP1 = v;

		mpFnWritePort(1, v);
	}
}

void ATCoProc8051::WriteP2(uint8 v) {
}

void ATCoProc8051::WriteP3(uint8 v) {
}

uint8 ATCoProc8051::ReadSCON() const {
	return mSCON;
}

void ATCoProc8051::WriteSCON(uint8 v){
	if (mSCON == v)
		return;

	// if we're clearing previously active interrupt flags, deactivate the
	// serial interrupt source
	if ((mSCON & 0x03) && !(v & 0x03))
		NegateInternalIrq(IntMask::Serial);

	mSCON = v; 
}

uint8 ATCoProc8051::ReadSBUF() const {
	return mSerialReadBuffer;
}

void ATCoProc8051::WriteSBUF(uint8 v) {
	// Unlike a lot of UARTs, the 8051 does not have a separate output buffer
	// and shift registers -- SBUF itself is the shift register.
	mpFnSerialXmit(v);
}

void ATCoProc8051::UpdateTimerDeadline() {
	mTimerDeadline = (mCyclesBase - mCyclesLeft) + 0x2000 - ((mTL0 & 0x1F) + ((uint32)mTH0 << 5));
}

bool ATCoProc8051::ReadCarry() {
	return (mPSW & 0x80) != 0;
}

void ATCoProc8051::WriteCarry(bool v) {
	if (v)
		mPSW |= 0x80;
	else
		mPSW &= 0x7F;
}

uint8 ATCoProc8051::ReadDirect(uint8 addr) {
	if (addr < 0x80) {
		return mRAM[addr];
	} else {
		switch(addr) {
			case 0x80:	return ReadP0();
			case 0x81:	return mSP;
			case 0x82:	return (uint8)mDPTR;
			case 0x83:	return (uint8)(mDPTR >> 8);
			case 0x88:	return ReadTCON();
			case 0x8A:	return ReadTL0();
			case 0x8C:	return ReadTH0();
			case 0x90:	return ReadP1();
			case 0x98:	return ReadSCON();
			case 0x99:	return ReadSBUF();
			case 0xA0:	return ReadP2();
			case 0xA8:	return mIE;
			case 0xB0:	return ReadP3();
			case 0xB8:	return mIP;
			case 0xD0:	return ReadPSW();
			case 0xE0:	return mA;
			case 0xF0:	return mB;
			default:	return 0;
		}
	}
}

uint8 ATCoProc8051::ReadDirectRMW(uint8 addr) {
	// read/modify/write instructions read the latch instead of the port
	switch(addr) {
		case 0x80:	return mP0;
		case 0x88:	return mP1;
		case 0x90:	return 0xFF;
		case 0x98:	return 0xFF;
		default:	return ReadDirect(addr);
	}
}

void ATCoProc8051::WriteDirect(uint8 addr, uint8 v) {
	if (addr < 0x80) {
		mRAM[addr] = v;
	} else {
		switch(addr) {
			case 0x80:	WriteP0(v); break;
			case 0x81:	mSP = v; break;
			case 0x82:	mDPTR = (mDPTR & 0xFF00) + v; break;
			case 0x83:	mDPTR = (uint8)mDPTR + ((uint16)v << 8); break;
			case 0x88:	WriteTCON(v); break;
			case 0x8A:	WriteTL0(v); break;
			case 0x8C:	WriteTH0(v); break;
			case 0x90:	WriteP1(v); break;
			case 0x98:	WriteSCON(v); break;
			case 0x99:	WriteSBUF(v); break;
			case 0xA0:	WriteP2(v); break;
			case 0xA8:	WriteIE(v); break;
			case 0xB0:	WriteP3(v); break;
			case 0xB8:	WriteIP(v); break;
			case 0xD0:	WritePSW(v); break;
			case 0xE0:	mA = v; break;
			case 0xF0:	mB = v; break;
			default:	break;
		}
	}
}

// Bit access
//
// Bit accesses in 00.7FH access bits in 20-3FH internal memory, and bit accesses
// in 80..FFH access bits 0-7 in SFR addresses 80H, 88H, 90H....
//
bool ATCoProc8051::ReadBit(uint8 bitAddr) {
	const uint8 byteAddr = bitAddr < 0x80 ? 0x20 + (bitAddr >> 3) : bitAddr & 0xF8;
	const uint8 bv = ReadDirect(byteAddr);

	return (bv & (1 << (bitAddr & 7))) != 0;
}

bool ATCoProc8051::ReadBitRMW(uint8 bitAddr) {
	const uint8 byteAddr = bitAddr < 0x80 ? 0x20 + (bitAddr >> 3) : bitAddr & 0xF8;
	const uint8 bv = ReadDirectRMW(byteAddr);

	return (bv & (1 << (bitAddr & 7))) != 0;
}

void ATCoProc8051::WriteBit(uint8 bitAddr, bool v) {
	const uint8 bit = 1 << (bitAddr & 7);
	const uint8 byteAddr = bitAddr < 0x80 ? 0x20 + (bitAddr >> 3) : bitAddr & 0xF8;
	const uint8 bv = ReadDirect(byteAddr);

	if (v)
		WriteDirect(byteAddr, bv | bit);
	else
		WriteDirect(byteAddr, bv & ~bit);
}

// Indirect access
//
// The 8051 only allows access to internal RAM with indirect access; SFRs are not
// accessible. The 8052 has an extra 128 bytes of internal RAM underneath the SFRs.
//
uint8 ATCoProc8051::ReadIndirect(uint8 addr) {
	return addr < 0x80 ? mRAM[addr] : 0;
}

void ATCoProc8051::WriteIndirect(uint8 addr, uint8 v) {
	if (addr < 0x80)
		mRAM[addr] = v;
}

uint8 ATCoProc8051::ReadXRAM(uint16 addr) {
	return mpFnReadXRAM(addr);
}

void ATCoProc8051::WriteXRAM(uint16 addr, uint8 v) {
	mpFnWriteXRAM(addr, v);
}

void ATCoProc8051::Push(uint8 v) {
	WriteIndirect(++mSP, v);
}

uint8 ATCoProc8051::Pop() {
	const uint8 v = ReadIndirect(mSP);
	--mSP;

	return v;
}
