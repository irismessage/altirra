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
#include "cpu.h"
#include "cpustates.h"
#include "console.h"
#include "disasm.h"
#include "savestate.h"
#include "simulator.h"

using namespace AT6502;

ATCPUEmulator::ATCPUEmulator()
	: mpHLE(NULL)
{
	mSBrk = 0x100;
	mpNextState = NULL;
	mCPUMode = kATCPUMode_6502;
	mCPUSubMode = kATCPUSubMode_6502;

	memset(mInsnFlags, 0, sizeof mInsnFlags);

	mbHistoryEnabled = false;
	mbPathfindingEnabled = false;
	mbIllegalInsnsEnabled = true;
	mbStopOnBRK = false;

	RebuildDecodeTables();

	VDASSERTCT(kStateCount < 256);
}

ATCPUEmulator::~ATCPUEmulator() {
}

bool ATCPUEmulator::Init(ATCPUEmulatorMemory *mem) {
	mpMemory = mem;

	ColdReset();

	memset(mHistory, 0, sizeof mHistory);
	mHistoryIndex = 0;

	return true;
}

void ATCPUEmulator::SetHLE(IATCPUHighLevelEmulator *hle) {
	mpHLE = hle;
}

void ATCPUEmulator::ColdReset() {
	mbStep = false;
	mbTrace = false;
	WarmReset();
}

void ATCPUEmulator::WarmReset() {
	mpNextState = mStates;
	mbIRQReleasePending = false;
	mbIRQPending = false;
	mbNMIPending = false;
	mbUnusedCycle = false;
	mbMarkHistoryIRQ = false;
	mbMarkHistoryNMI = false;
	mInsnPC = 0xFFFC;
	mPC = 0xFFFC;
	mP = 0x30 | kFlagI;
	mStates[0] = kStateReadAddrL;
	mStates[1] = kStateReadAddrH;
	mStates[2] = kStateAddrToPC;
	mStates[3] = kStateReadOpcode;

	// 65C816 initialization
	if (mCPUMode == kATCPUMode_65C816) {
		mSH = 0x01;
		mDP = 0x0000;
		mXH = 0;
		mYH = 0;
		mB = 0;
		mK = 0;

		// Set MXIE. Clear D. NVZC are preserved.
		mP = (mP & (kFlagN | kFlagV | kFlagZ | kFlagC)) | (kFlagM | kFlagX | kFlagI);

		mbEmulationFlag = true;

		Update65816DecodeTable();
	}
}

bool ATCPUEmulator::IsInstructionInProgress() const {
	uint8 state = *mpNextState;

	return state != kStateReadOpcode && state != kStateReadOpcodeNoBreak;
}

void ATCPUEmulator::SetPC(uint16 pc) {
	mPC = pc;
	mInsnPC = pc;
	mpDstState = mStates;
	mpNextState = mStates;
	*mpDstState++ = kStateReadOpcodeNoBreak;
}

void ATCPUEmulator::SetHook(uint16 pc, bool enable) {
	if (enable)
		mInsnFlags[pc] |= kInsnFlagHook;
	else
		mInsnFlags[pc] &= ~kInsnFlagHook;
}

sint32 ATCPUEmulator::GetNextBreakpoint(sint32 last) const {
	if ((uint32)last >= 0x10000)
		last = -1;

	for(++last; last < 0x10000; ++last) {
		if (mInsnFlags[last] & kInsnFlagBreakPt)
			return last;
	}

	return -1;
}

void ATCPUEmulator::SetCPUMode(ATCPUMode mode) {
	if (mCPUMode == mode)
		return;

	mCPUMode = mode;

	// ensure sane 65C816 state
	mbEmulationFlag = true;
	mP |= 0x30;
	mSH = 1;
	mXH = 0;
	mYH = 0;
	mAH = 0;
	mDP = 0;
	mK = 0;
	mB = 0;

	switch(mode) {
		case kATCPUMode_65C816:
			mCPUSubMode = kATCPUSubMode_65C816_Emulation;
			break;
		case kATCPUMode_65C02:
			mCPUSubMode = kATCPUSubMode_65C02;
			break;
		case kATCPUMode_6502:
			mCPUSubMode = kATCPUSubMode_6502;
			break;
	}

	RebuildDecodeTables();
}

void ATCPUEmulator::SetHistoryEnabled(bool enable) {
	if (mbHistoryEnabled != enable) {
		mbHistoryEnabled = enable;
		RebuildDecodeTables();
		mbMarkHistoryIRQ = false;
		mbMarkHistoryNMI = false;
	}
}

void ATCPUEmulator::SetPathfindingEnabled(bool enable) {
	if (mbPathfindingEnabled != enable) {
		mbPathfindingEnabled = enable;
		RebuildDecodeTables();
	}
}

void ATCPUEmulator::SetIllegalInsnsEnabled(bool enable) {
	if (mbIllegalInsnsEnabled != enable) {
		mbIllegalInsnsEnabled = enable;
		RebuildDecodeTables();
	}
}

void ATCPUEmulator::SetStopOnBRK(bool en) {
	if (mbStopOnBRK != en) {
		mbStopOnBRK = en;
		RebuildDecodeTables();
	}
}

bool ATCPUEmulator::IsBreakpointSet(uint16 addr) const {
	return 0 != (mInsnFlags[addr] & kInsnFlagBreakPt);
}

void ATCPUEmulator::SetBreakpoint(uint16 addr) {
	mInsnFlags[addr] |= kInsnFlagBreakPt;
}

void ATCPUEmulator::SetOneShotBreakpoint(uint16 addr) {
	mInsnFlags[addr] |= kInsnFlagBreakPt | kInsnFlagOneShotBreakPt;
}

void ATCPUEmulator::ClearBreakpoint(uint16 addr) {
	mInsnFlags[addr] &= ~(kInsnFlagBreakPt | kInsnFlagOneShotBreakPt);
}

void ATCPUEmulator::ClearAllBreakpoints() {
	for(uint32 i=0; i<65536; ++i)
		mInsnFlags[i] &= ~(kInsnFlagBreakPt | kInsnFlagOneShotBreakPt);
}

void ATCPUEmulator::ResetAllPaths() {
	for(uint32 i=0; i<65536; ++i)
		mInsnFlags[i] &= ~(kInsnFlagPathStart | kInsnFlagPathExecuted);
}

sint32 ATCPUEmulator::GetNextPathInstruction(sint32 last) const {
	if ((uint32)last >= 0x10000)
		last = -1;

	for(++last; last < 0x10000; ++last) {
		if (mInsnFlags[last] & (kInsnFlagPathStart | kInsnFlagPathExecuted))
			return last;
	}

	return -1;
}

bool ATCPUEmulator::IsPathStart(uint16 addr) const {
	return 0 != (mInsnFlags[addr] & kInsnFlagPathStart);
}

bool ATCPUEmulator::IsInPath(uint16 addr) const {
	return 0 != (mInsnFlags[addr] & (kInsnFlagPathStart | kInsnFlagPathExecuted));
}

void ATCPUEmulator::DumpStatus() {
	if (mCPUMode == kATCPUMode_65C816) {
		if (mbEmulationFlag) {
			ATConsoleTaggedPrintf("PC=%02X:%04X A=%02X:%02X X=%02X Y=%02X S=%02X P=%02X (%c%c%c%c%c%c)  "
				, mK
				, mInsnPC
				, mAH
				, mA
				, mX
				, mY
				, mS
				, mP
				, mP & 0x80 ? 'N' : ' '
				, mP & 0x40 ? 'V' : ' '
				, mP & 0x08 ? 'D' : ' '
				, mP & 0x04 ? 'I' : ' '
				, mP & 0x02 ? 'Z' : ' '
				, mP & 0x01 ? 'C' : ' '
				);
		} else {
			ATConsoleTaggedPrintf("PC=%02X:%04X A=%02X%02X X=%02X%02X Y=%02X%02X S=%02X%02X P=%02X (%c%c%c%c%c%c%c%c)  "
				, mK
				, mInsnPC
				, mAH
				, mA
				, mXH
				, mX
				, mYH
				, mY
				, mSH
				, mS
				, mP
				, mP & 0x80 ? 'N' : ' '
				, mP & 0x40 ? 'V' : ' '
				, mP & 0x20 ? 'M' : ' '
				, mP & 0x10 ? 'X' : ' '
				, mP & 0x08 ? 'D' : ' '
				, mP & 0x04 ? 'I' : ' '
				, mP & 0x02 ? 'Z' : ' '
				, mP & 0x01 ? 'C' : ' '
				);
		}
	} else {
		ATConsoleTaggedPrintf("PC=%04X A=%02X X=%02X Y=%02X S=%02X P=%02X (%c%c%c%c%c%c)  "
			, mInsnPC
			, mA
			, mX
			, mY
			, mS
			, mP
			, mP & 0x80 ? 'N' : ' '
			, mP & 0x40 ? 'V' : ' '
			, mP & 0x08 ? 'D' : ' '
			, mP & 0x04 ? 'I' : ' '
			, mP & 0x02 ? 'Z' : ' '
			, mP & 0x01 ? 'C' : ' '
			);
	}

	ATDisassembleInsn(mInsnPC);
}

void ATCPUEmulator::LoadState(ATSaveStateReader& reader) {
	mA		= reader.ReadUint8();
	mX		= reader.ReadUint8();
	mY		= reader.ReadUint8();
	mS		= reader.ReadUint8();
	mP		= reader.ReadUint8();
	SetPC(reader.ReadUint16());
	mbIRQPending	= reader.ReadBool();
	mbNMIPending	= reader.ReadBool();
	mHLEDelay		= 0;
}

void ATCPUEmulator::SaveState(ATSaveStateWriter& writer) {
	writer.WriteUint8(mA);
	writer.WriteUint8(mX);
	writer.WriteUint8(mY);
	writer.WriteUint8(mS);
	writer.WriteUint8(mP);
	writer.WriteUint16(mPC);
	writer.WriteBool(mbIRQPending);
	writer.WriteBool(mbNMIPending);
}

void ATCPUEmulator::InjectOpcode(uint8 op) {
	mOpcode = op;
	mpNextState = mpDecodePtrs[mOpcode];
}

void ATCPUEmulator::Push(uint8 v) {
	mpMemory->WriteByte(0x100 + mS, v);
	--mS;
}

uint8 ATCPUEmulator::Pop() {
	return mpMemory->ReadByte(0x100 + (uint8)++mS);
}

void ATCPUEmulator::Jump(uint16 pc) {
	mPC = pc;
	mpDstState = mStates;
	mpNextState = mStates;
	*mpDstState++ = kStateReadOpcode;
}

void ATCPUEmulator::AssertIRQ() {
	mbIRQPending = true;
//	ATConsoleTaggedPrintf("CPU: Raising IRQ\n");
}

void ATCPUEmulator::NegateIRQ() {
	mbIRQPending = false;
//	ATConsoleTaggedPrintf("CPU: Lowering IRQ\n");
}

void ATCPUEmulator::AssertNMI() {
	mbNMIPending = true;
}

//#include <vd2/system/file.h>
//VDFile f("e:\\boot.txt", nsVDFile::kWrite | nsVDFile::kDenyRead | nsVDFile::kCreateAlways);

int ATCPUEmulator::Advance(bool busLocked) {
	if (busLocked)
		return kATSimEvent_None;

	for(;;) {
		switch(*mpNextState++) {
			case kStateNop:
				break;

			case kStateReadOpcode:
				mInsnPC = mPC;

				{
					uint8 iflags = mInsnFlags[mPC];

					if (iflags & (kInsnFlagBreakPt | kInsnFlagSpecialTracePt)) {
						if (mInsnFlags[mPC] & kInsnFlagOneShotBreakPt)
							mInsnFlags[mPC] &= ~(kInsnFlagBreakPt | kInsnFlagOneShotBreakPt);

						RedecodeInsnWithoutBreak();
						return kATSimEvent_CPUPCBreakpoint;
					}
				}

				if (mbStep) {
					mbStep = false;
					mbUnusedCycle = true;
					RedecodeInsnWithoutBreak();
					return kATSimEvent_CPUSingleStep;
				}

				if (mS >= mSBrk) {
					mSBrk = 0x100;
					mbUnusedCycle = true;
					RedecodeInsnWithoutBreak();
					return kATSimEvent_CPUStackBreakpoint;
				}

				if (mbTrace)
					DumpStatus();

#if 0
				{
					static bool foo = false;

					if (mPC == 0xf1e1)
						foo = true;
					
					if (foo) {
						static char buf[256];
						sprintf(buf, "PC=%04X A=%02X X=%02X Y=%02X S=%02X P=%02X (%c%c%c%c%c%c%c%c)  "
							, mPC
							, mA
							, mX
							, mY
							, mS
							, mP
							, mP & 0x80 ? 'N' : ' '
							, mP & 0x40 ? 'V' : ' '
							, mP & 0x20 ? '1' : ' '
							, mP & 0x10 ? 'B' : ' '
							, mP & 0x08 ? 'D' : ' '
							, mP & 0x04 ? 'I' : ' '
							, mP & 0x02 ? 'Z' : ' '
							, mP & 0x01 ? 'C' : ' '
							);
						f.write(buf, strlen(buf));
						ATDisassembleInsn(buf, mPC);
						f.write(buf, strlen(buf));
					}
				}
#endif
			case kStateReadOpcodeNoBreak:
				{
					uint8 iflags = mInsnFlags[mPC];

					if (iflags & kInsnFlagHook) {
						uint8 op = mpMemory->CPUHookHit(mPC);

						if (op) {
							++mPC;
							mOpcode = op;
							mpNextState = mpDecodePtrs[mOpcode];
							return kATSimEvent_None;
						}
					}
				}

				if (mbNMIPending) {
					if (mbTrace)
						ATConsoleWrite("CPU: Jumping to NMI vector\n");

					mbNMIPending = false;
					mbMarkHistoryNMI = true;

					mpNextState = mpDecodePtrNMI;
					continue;
				}

				if (mbIRQReleasePending)
					mbIRQReleasePending = false;
				else if (mbIRQPending && !(mP & kFlagI)) {
					if (mbTrace)
						ATConsoleWrite("CPU: Jumping to IRQ vector\n");

					mbMarkHistoryIRQ = true;

					mpNextState = mpDecodePtrIRQ;
					continue;
				}

				mOpcode = mpMemory->ReadByte(mPC++);

				if (mbHistoryEnabled) {
					HistoryEntry& he = mHistory[mHistoryIndex++ & 131071];

					he.mTimestamp = mpMemory->GetTimestamp();
					he.mPC = mPC - 1;
					he.mS = mS;
					he.mP = mP;
					he.mA = mA;
					he.mX = mX;
					he.mY = mY;
					he.mOpcode[0] = mOpcode;
					he.mOpcode[1] = mpMemory->CPUDebugReadByte(mPC);
					he.mOpcode[2] = mpMemory->CPUDebugReadByte(mPC+1);
					he.mOpcode[3] = mpMemory->CPUDebugReadByte(mPC+2);
					he.mbIRQ = mbMarkHistoryIRQ;
					he.mbNMI = mbMarkHistoryNMI;
					he.mbEmulation = mbEmulationFlag;
					he.mSH = mSH;
					he.mAH = mAH;
					he.mXH = mXH;
					he.mYH = mYH;
					he.mB = mB;
					he.mK = mK;
					he.mD = mDP;

					mbMarkHistoryIRQ = false;
					mbMarkHistoryNMI = false;
				}

				mpNextState = mpDecodePtrs[mOpcode];
				return kATSimEvent_None;

			case kStateReadDummyOpcode:
				mpMemory->ReadByte(mPC);
				return kATSimEvent_None;

			case kStateAddAsPathStart:
				if (!(mInsnFlags[mPC] & kInsnFlagPathStart)) {
					mInsnFlags[mPC] |= kInsnFlagPathStart;
				}
				break;

			case kStateAddToPath:
				{
					uint16 adjpc = mPC - 1;
					if (!(mInsnFlags[adjpc] & kInsnFlagPathExecuted))
						mInsnFlags[adjpc] |= kInsnFlagPathExecuted;
				}
				break;

			case kStateBreakOnUnsupportedOpcode:
				mbUnusedCycle = true;
				return kATSimEvent_CPUIllegalInsn;

			case kStateReadImm:
				mData = mpMemory->ReadByte(mPC++);
				return kATSimEvent_None;

			case kStateReadAddrL:
				mAddr = mpMemory->ReadByte(mPC++);
				return kATSimEvent_None;

			case kStateReadAddrH:
				mAddr += mpMemory->ReadByte(mPC++) << 8;
				return kATSimEvent_None;

			case kStateReadAddrHX:
				mAddr += mpMemory->ReadByte(mPC++) << 8;
				mAddr2 = (mAddr & 0xff00) + ((mAddr + mX) & 0x00ff);
				mAddr = mAddr + mX;
				return kATSimEvent_None;

			case kStateReadAddrHX_SHY:
				{
					uint8 hiByte = mpMemory->ReadByte(mPC++);
					mData = mY & (hiByte + 1);
					mP &= ~(kFlagN | kFlagZ);
					if (mData & 0x80)
						mP |= kFlagN;
					if (!mData)
						mP |= kFlagZ;
					mAddr = (uint16)(mAddr + ((uint32)hiByte << 8) + mX);
				}
				return kATSimEvent_None;

			case kStateReadAddrHY:
				mAddr += mpMemory->ReadByte(mPC++) << 8;
				mAddr2 = (mAddr & 0xff00) + ((mAddr + mY) & 0x00ff);
				mAddr = mAddr + mY;
				return kATSimEvent_None;

			case kStateRead:
				mData = mpMemory->ReadByte(mAddr);
				return kATSimEvent_None;

			case kStateReadAddX:
				mData = mpMemory->ReadByte(mAddr);
				mAddr = (uint8)(mAddr + mX);
				return kATSimEvent_None;

			case kStateReadAddY:
				mData = mpMemory->ReadByte(mAddr);
				mAddr = (uint8)(mAddr + mY);
				return kATSimEvent_None;

			case kStateReadCarry:
				mData = mpMemory->ReadByte(mAddr2);
				if (mAddr == mAddr2)
					++mpNextState;
				return kATSimEvent_None;

			case kStateReadCarryForced:
				mData = mpMemory->ReadByte(mAddr2);
				return kATSimEvent_None;

			case kStateWrite:
				mpMemory->WriteByte(mAddr, mData);
				return kATSimEvent_None;

			case kStateReadAbsIndAddr:
				mAddr = mData + ((uint16)mpMemory->ReadByte(mAddr + 1) << 8);
				return kATSimEvent_None;

			case kStateReadAbsIndAddrBroken:
				mAddr = mData + ((uint16)mpMemory->ReadByte((mAddr & 0xff00) + ((mAddr + 1) & 0xff)) << 8);
				return kATSimEvent_None;

			case kStateReadIndAddr:
				mAddr = mData + ((uint16)mpMemory->ReadByte(0xff & (mAddr + 1)) << 8);
				return kATSimEvent_None;

			case kStateReadIndYAddr:
				mAddr = mData + ((uint16)mpMemory->ReadByte(0xff & (mAddr + 1)) << 8);
				mAddr2 = (mAddr & 0xff00) + ((mAddr + mY) & 0x00ff);
				mAddr = mAddr + mY;
				return kATSimEvent_None;

			case kStateWait:
				return kATSimEvent_None;

			case kStateAtoD:
				mData = mA;
				break;

			case kStateXtoD:
				mData = mX;
				break;

			case kStateYtoD:
				mData = mY;
				break;

			case kStateStoD:
				mData = mS;
				break;

			case kStatePtoD:
				mData = mP;
				break;

			case kStatePtoD_B0:
				mData = mP & ~kFlagB;
				break;

			case kStatePtoD_B1:
				mData = mP | kFlagB;
				break;

			case kState0toD:
				mData = 0;
				break;

			case kStateDtoA:
				mA = mData;
				break;

			case kStateDtoX:
				mX = mData;
				break;

			case kStateDtoY:
				mY = mData;
				break;

			case kStateDtoS:
				mS = mData;
				break;

			case kStateDtoP:
				if ((mP & ~mData) & kFlagI)
					mbIRQReleasePending = true;

				mP = mData | 0x30;
				break;

			case kStateDSetSZ:
				mP &= ~(kFlagN | kFlagZ);
				if (mData & 0x80)
					mP |= kFlagN;
				if (!mData)
					mP |= kFlagZ;
				break;

			case kStateDSetSV:
				mP &= ~(kFlagN | kFlagV);
				mP |= (mData & 0xC0);
				break;

			case kStateAddrToPC:
				mPC = mAddr;
				break;

			case kStateNMIVecToPC:
				mPC = 0xFFFA;
				break;

			case kStateIRQVecToPC:
				mPC = 0xFFFE;
				break;

			case kStatePush:
				mpMemory->WriteByte(0x100 + (uint8)mS--, mData);
				return kATSimEvent_None;

			case kStatePushPCL:
				mpMemory->WriteByte(0x100 + (uint8)mS--, mPC & 0xff);
				return kATSimEvent_None;

			case kStatePushPCH:
				mpMemory->WriteByte(0x100 + (uint8)mS--, mPC >> 8);
				return kATSimEvent_None;

			case kStatePushPCLM1:
				mpMemory->WriteByte(0x100 + (uint8)mS--, (mPC - 1) & 0xff);
				return kATSimEvent_None;

			case kStatePushPCHM1:
				mpMemory->WriteByte(0x100 + (uint8)mS--, (mPC - 1) >> 8);
				return kATSimEvent_None;

			case kStatePop:
				mData = mpMemory->ReadByte(0x100 + (uint8)++mS);
				return kATSimEvent_None;

			case kStatePopPCL:
				mPC = mpMemory->ReadByte(0x100 + (uint8)++mS);
				return kATSimEvent_None;

			case kStatePopPCH:
				mPC += mpMemory->ReadByte(0x100 + (uint8)++mS) << 8;
				return kATSimEvent_None;

			case kStatePopPCHP1:
				mPC += mpMemory->ReadByte(0x100 + (uint8)++mS) << 8;
				++mPC;
				return kATSimEvent_None;

			case kStateAdc:
				if (mP & kFlagD) {
					// BCD
					uint32 lowResult = (mA & 15) + (mData & 15) + (mP & kFlagC);
					if (lowResult >= 10)
						lowResult += 6;

					if (lowResult >= 0x20)
						lowResult -= 0x10;

					uint32 highResult = (mA & 0xf0) + (mData & 0xf0) + lowResult;

					mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

					mP |= (((highResult ^ mA) & ~(mData ^ mA)) >> 1) & kFlagV;

					if (highResult & 0x80)
						mP |= kFlagN;

					if (highResult >= 0xA0)
						highResult += 0x60;

					if (highResult >= 0x100)
						mP |= kFlagC;

					if (!(uint8)(mA + mData))
						mP |= kFlagZ;

					mA = (uint8)highResult;
				} else {
					uint32 carry7 = (mA & 0x7f) + (mData & 0x7f) + (mP & kFlagC);
					uint32 result = carry7 + (mA & 0x80) + (mData & 0x80);

					mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

					if (result & 0x80)
						mP |= kFlagN;

					if (result >= 0x100)
						mP |= kFlagC;

					if (!(result & 0xff))
						mP |= kFlagZ;

					mP |= ((result >> 2) ^ (carry7 >> 1)) & kFlagV;

					mA = (uint8)result;
				}
				break;

			case kStateSbc:
				if (mP & kFlagD) {
					// Pole Position needs N set properly here for its passing counter
					// to stop correctly!

					mData ^= 0xff;

					// Flags set according to binary op
					uint32 carry7 = (mA & 0x7f) + (mData & 0x7f) + (mP & kFlagC);
					uint32 result = carry7 + (mA & 0x80) + (mData & 0x80);

					// BCD
					uint32 lowResult = (mA & 15) + (mData & 15) + (mP & kFlagC);
					if (lowResult < 0x10)
						lowResult -= 6;

					uint32 highResult = (mA & 0xf0) + (mData & 0xf0) + (lowResult & 0x1f);

					if (highResult < 0x100)
						highResult -= 0x60;

					mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

					if (result & 0x80)
						mP |= kFlagN;

					if (result >= 0x100)
						mP |= kFlagC;

					if (!(result & 0xff))
						mP |= kFlagZ;

					mP |= ((result >> 2) ^ (carry7 >> 1)) & kFlagV;

					mA = (uint8)highResult;
				} else {
					mData ^= 0xff;
					uint32 carry7 = (mA & 0x7f) + (mData & 0x7f) + (mP & kFlagC);
					uint32 result = carry7 + (mA & 0x80) + (mData & 0x80);

					mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

					if (result & 0x80)
						mP |= kFlagN;

					if (result >= 0x100)
						mP |= kFlagC;

					if (!(result & 0xff))
						mP |= kFlagZ;

					mP |= ((result >> 2) ^ (carry7 >> 1)) & kFlagV;

					mA = (uint8)result;
				}
				break;

			case kStateCmp:
				{
					mData ^= 0xff;
					uint32 result = mA + mData + 1;

					mP &= ~(kFlagC | kFlagN | kFlagZ);

					if (result & 0x80)
						mP |= kFlagN;

					if (result >= 0x100)
						mP |= kFlagC;

					if (!(result & 0xff))
						mP |= kFlagZ;
				}
				break;

			case kStateCmpX:
				{
					mData ^= 0xff;
					uint32 result = mX + mData + 1;

					mP &= ~(kFlagC | kFlagN | kFlagZ);

					if (result & 0x80)
						mP |= kFlagN;

					if (result >= 0x100)
						mP |= kFlagC;

					if (!(result & 0xff))
						mP |= kFlagZ;
				}
				break;

			case kStateCmpY:
				{
					mData ^= 0xff;
					uint32 result = mY + mData + 1;

					mP &= ~(kFlagC | kFlagN | kFlagZ);

					if (result & 0x80)
						mP |= kFlagN;

					if (result >= 0x100)
						mP |= kFlagC;

					if (!(result & 0xff))
						mP |= kFlagZ;
				}
				break;

			case kStateInc:
				++mData;
				mP &= ~(kFlagN | kFlagZ);
				if (mData & 0x80)
					mP |= kFlagN;
				if (!mData)
					mP |= kFlagZ;
				break;

			case kStateDec:
				--mData;
				mP &= ~(kFlagN | kFlagZ);
				if (mData & 0x80)
					mP |= kFlagN;
				if (!mData)
					mP |= kFlagZ;
				break;

			case kStateDecC:
				--mData;
				mP |= kFlagC;
				if (!mData)
					mP &= ~kFlagC;
				break;

			case kStateAnd:
				mData &= mA;
				mP &= ~(kFlagN | kFlagZ);
				if (mData & 0x80)
					mP |= kFlagN;
				if (!mData)
					mP |= kFlagZ;
				break;

			case kStateAnc:
				mData &= mA;
				mP &= ~(kFlagN | kFlagZ | kFlagC);
				if (mData & 0x80)
					mP |= kFlagN | kFlagC;
				if (!mData)
					mP |= kFlagZ;
				break;

			case kStateXaa:
				mA &= (mData & mX);
				mP &= ~(kFlagN | kFlagZ);
				if (mA & 0x80)
					mP |= kFlagN;
				if (!mA)
					mP |= kFlagZ;
				break;

			case kStateOr:
				mA |= mData;
				mP &= ~(kFlagN | kFlagZ);
				if (mA & 0x80)
					mP |= kFlagN;
				if (!mA)
					mP |= kFlagZ;
				break;

			case kStateXor:
				mA ^= mData;
				mP &= ~(kFlagN | kFlagZ);
				if (mA & 0x80)
					mP |= kFlagN;
				if (!mA)
					mP |= kFlagZ;
				break;

			case kStateAsl:
				mP &= ~(kFlagN | kFlagZ | kFlagC);
				if (mData & 0x80)
					mP |= kFlagC;
				mData += mData;
				if (mData & 0x80)
					mP |= kFlagN;
				if (!mData)
					mP |= kFlagZ;
				break;

			case kStateLsr:
				mP &= ~(kFlagN | kFlagZ | kFlagC);
				if (mData & 0x01)
					mP |= kFlagC;
				mData >>= 1;
				if (!mData)
					mP |= kFlagZ;
				break;

			case kStateRol:
				{
					uint32 result = (uint32)mData + (uint32)mData + (mP & kFlagC);
					mP &= ~(kFlagN | kFlagZ | kFlagC);
					if (result & 0x100)
						mP |= kFlagC;
					mData = (uint8)result;
					if (mData & 0x80)
						mP |= kFlagN;
					if (!mData)
						mP |= kFlagZ;
				}
				break;

			case kStateRor:
				{
					uint32 result = (mData >> 1) + ((mP & kFlagC) << 7);
					mP &= ~(kFlagN | kFlagZ | kFlagC);
					if (mData & 0x1)
						mP |= kFlagC;
					mData = (uint8)result;
					if (mData & 0x80)
						mP |= kFlagN;
					if (!mData)
						mP |= kFlagZ;
				}
				break;

			case kStateBit:
				{
					uint8 result = mData & mA;
					mP &= ~kFlagZ;
					if (!result)
						mP |= kFlagZ;
				}
				break;

			case kStateSEI:
				mP |= kFlagI;
				break;

			case kStateCLI:
				mP &= ~kFlagI;
				mbIRQReleasePending = true;
				break;

			case kStateSEC:
				mP |= kFlagC;
				break;

			case kStateCLC:
				mP &= ~kFlagC;
				break;

			case kStateSED:
				mP |= kFlagD;
				break;

			case kStateCLD:
				mP &= ~kFlagD;
				break;

			case kStateCLV:
				mP &= ~kFlagV;
				break;

			case kStateJs:
				if (!(mP & kFlagN)) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJns:
				if (mP & kFlagN) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJc:
				if (!(mP & kFlagC)) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJnc:
				if (mP & kFlagC) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJz:
				if (!(mP & kFlagZ)) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJnz:
				if (mP & kFlagZ) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJo:
				if (!(mP & kFlagV)) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJno:
				if (mP & kFlagV) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			/////////
			case kStateJsAddToPath:
				if (!(mP & kFlagN)) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				mInsnFlags[mPC] |= kInsnFlagPathStart;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJnsAddToPath:
				if (mP & kFlagN) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				mInsnFlags[mPC] |= kInsnFlagPathStart;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJcAddToPath:
				if (!(mP & kFlagC)) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				mInsnFlags[mPC] |= kInsnFlagPathStart;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJncAddToPath:
				if (mP & kFlagC) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				mInsnFlags[mPC] |= kInsnFlagPathStart;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJzAddToPath:
				if (!(mP & kFlagZ)) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				mInsnFlags[mPC] |= kInsnFlagPathStart;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJnzAddToPath:
				if (mP & kFlagZ) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				mInsnFlags[mPC] |= kInsnFlagPathStart;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJoAddToPath:
				if (!(mP & kFlagV)) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				mInsnFlags[mPC] |= kInsnFlagPathStart;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJnoAddToPath:
				if (mP & kFlagV) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				mInsnFlags[mPC] |= kInsnFlagPathStart;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJccFalseRead:
				mpMemory->ReadByte(mAddr);
				return kATSimEvent_None;

			case kStateInvokeHLE:
				if (mpHLE) {
					mHLEDelay = 0;
					int r = mpHLE->InvokeHLE(*this, *mpMemory, mPC - 1, mAddr);

					if (r == -2) {
						mpNextState += 2;
						break;
					}

					if (r >= 0)
						return r;
				}
				break;

			case kStateHLEDelay:
				if (mHLEDelay) {
					--mHLEDelay;
					return kATSimEvent_None;
				}
				break;

			case kStateResetBit:
				mData &= ~(1 << ((mOpcode >> 4) & 7));
				break;

			case kStateSetBit:
				mData |= 1 << ((mOpcode >> 4) & 7);
				break;

			case kStateReadRel:
				mRelOffset = mpMemory->ReadByte(mPC++);
				return kATSimEvent_None;

			case kStateJ0:
				if (mData & (1 << ((mOpcode >> 4) & 7))) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mRelOffset;
				mAddr += mPC & 0xff;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJ1:
				if (!(mData & (1 << ((mOpcode >> 4) & 7)))) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mRelOffset;
				mAddr += mPC & 0xff;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJ0AddToPath:
				if (mData & (1 << ((mOpcode >> 4) & 7))) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mRelOffset;
				mAddr += mPC & 0xff;
				mInsnFlags[mPC] |= kInsnFlagPathStart;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJ1AddToPath:
				if (!(mData & (1 << ((mOpcode >> 4) & 7)))) {
					++mpNextState;
					break;
				}

				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mRelOffset;
				mAddr += mPC & 0xff;
				mInsnFlags[mPC] |= kInsnFlagPathStart;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateWaitForInterrupt:
				if (!mbIRQPending && !mbNMIPending)
					--mpNextState;
				return kATSimEvent_None;

			case kStateStop:
				--mpNextState;
				return kATSimEvent_None;

			case kStateJ:
				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateJAddToPath:
				mpMemory->ReadByte(mPC);
				mAddr = mPC & 0xff00;
				mPC += (sint16)(sint8)mData;
				mAddr += mPC & 0xff;
				mInsnFlags[mPC] |= kInsnFlagPathStart;
				if (mAddr == mPC)
					++mpNextState;
				return kATSimEvent_None;

			case kStateTrb:
				mP &= ~kFlagZ;
				if (!(mData & mA))
					mP |= kFlagZ;

				mData &= ~mA;
				break;

			case kStateTsb:
				mP &= ~kFlagZ;
				if (!(mData & mA))
					mP |= kFlagZ;

				mData |= mA;
				break;

			case kStateC02_Adc:
				if (mP & kFlagD) {
					uint32 lowResult = (mA & 15) + (mData & 15) + (mP & kFlagC);
					if (lowResult >= 10)
						lowResult += 6;

					if (lowResult >= 0x20)
						lowResult -= 0x10;

					uint32 highResult = (mA & 0xf0) + (mData & 0xf0) + lowResult;

					mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

					mP |= (((highResult ^ mA) & ~(mData ^ mA)) >> 1) & kFlagV;

					if (highResult >= 0xA0)
						highResult += 0x60;

					if (highResult >= 0x100)
						mP |= kFlagC;

					uint8 result = (uint8)highResult;

					if (!result)
						mP |= kFlagZ;

					if (result & 0x80)
						mP |= kFlagN;

					mA = result;
				} else {
					uint32 carry7 = (mA & 0x7f) + (mData & 0x7f) + (mP & kFlagC);
					uint32 result = carry7 + (mA & 0x80) + (mData & 0x80);

					mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

					if (result & 0x80)
						mP |= kFlagN;

					if (result >= 0x100)
						mP |= kFlagC;

					if (!(result & 0xff))
						mP |= kFlagZ;

					mP |= ((result >> 2) ^ (carry7 >> 1)) & kFlagV;

					mA = (uint8)result;

					// No extra cycle unless decimal mode is on.
					++mpNextState;
				}
				break;

			case kStateC02_Sbc:
				if (mP & kFlagD) {
					// Pole Position needs N set properly here for its passing counter
					// to stop correctly!

					mData ^= 0xff;

					// Flags set according to binary op
					uint32 carry7 = (mA & 0x7f) + (mData & 0x7f) + (mP & kFlagC);
					uint32 result = carry7 + (mA & 0x80) + (mData & 0x80);

					// BCD
					uint32 lowResult = (mA & 15) + (mData & 15) + (mP & kFlagC);
					if (lowResult < 0x10)
						lowResult -= 6;

					uint32 highResult = (mA & 0xf0) + (mData & 0xf0) + (lowResult & 0x1f);

					if (highResult < 0x100)
						highResult -= 0x60;

					mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

					uint8 bcdresult = (uint8)highResult;

					if (bcdresult & 0x80)
						mP |= kFlagN;

					if (result >= 0x100)
						mP |= kFlagC;

					if (!(bcdresult & 0xff))
						mP |= kFlagZ;

					mP |= ((result >> 2) ^ (carry7 >> 1)) & kFlagV;

					mA = bcdresult;
				} else {
					mData ^= 0xff;
					uint32 carry7 = (mA & 0x7f) + (mData & 0x7f) + (mP & kFlagC);
					uint32 result = carry7 + (mA & 0x80) + (mData & 0x80);

					mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

					if (result & 0x80)
						mP |= kFlagN;

					if (result >= 0x100)
						mP |= kFlagC;

					if (!(result & 0xff))
						mP |= kFlagZ;

					mP |= ((result >> 2) ^ (carry7 >> 1)) & kFlagV;

					mA = (uint8)result;

					// No extra cycle unless decimal mode is on.
					++mpNextState;
				}
				break;

			////////// 65C816 states

			case kStateReadImmL16:
				mData16 = mpMemory->ReadByte(mPC++);
				return kATSimEvent_None;

			case kStateReadImmH16:
				mData16 += (uint32)mpMemory->ReadByte(mPC++) << 8;
				return kATSimEvent_None;

			case kStateReadAddrDp:
				mAddr = (uint32)mpMemory->ReadByte(mPC++) + mDP;
				mAddrBank = 0;
				return kATSimEvent_None;

			case kStateReadAddrDpX:
				mAddr = (uint32)mpMemory->ReadByte(mPC++) + mDP + mX + ((uint32)mXH << 8);
				mAddrBank = 0;
				return kATSimEvent_None;

			case kStateReadAddrDpY:
				mAddr = (uint32)mpMemory->ReadByte(mPC++) + mDP + mY + ((uint32)mYH << 8);
				mAddrBank = 0;
				return kATSimEvent_None;

			case kStateReadIndAddrDp:
				mAddr = mData + ((uint16)mpMemory->ReadByte(mDP + mAddr + 1) << 8);
				mAddrBank = mB;
				return kATSimEvent_None;

			case kStateReadIndAddrDpY:
				mAddr = mData + ((uint16)mpMemory->ReadByte(mDP + mAddr + 1) << 8) + mY + ((uint32)mYH << 8);
				mAddrBank = mB;
				return kATSimEvent_None;

			case kStateReadIndAddrDpLongH:
				mData16 = mData + ((uint16)mpMemory->ReadByte(mDP + mAddr + 1) << 8);
				return kATSimEvent_None;

			case kStateReadIndAddrDpLongB:
				mAddrBank = mpMemory->ReadByte(mDP + mAddr + 2);
				mAddr = mData16;
				return kATSimEvent_None;

			case kStateReadAddrAddY:
				mAddr += mY + ((uint32)mYH << 8);
				break;

			case kState816ReadAddrL:
				mAddrBank = mB;
				mAddr = mpMemory->ReadByte(mPC++);
				return kATSimEvent_None;

			case kStateRead816AddrAbsHY:
				mAddr = mData + ((uint32)mpMemory->ExtReadByte(mAddr + 1, mAddrBank) << 8) + mY + ((uint32)mYH << 8);
				mAddrBank = mB;
				return kATSimEvent_None;

			case kStateRead816AddrAbsLongL:
				mData = mpMemory->ExtReadByte(mAddr, mAddrBank);
				return kATSimEvent_None;

			case kStateRead816AddrAbsLongH:
				mData16 = mData + ((uint32)mpMemory->ExtReadByte(mAddr + 1, mAddrBank) << 8);
				return kATSimEvent_None;

			case kStateRead816AddrAbsLongB:
				mAddrBank = mpMemory->ExtReadByte(mAddr + 2, mAddrBank);
				mAddr = mData16;
				return kATSimEvent_None;

			case kState816ReadAddrHX:
				mAddr += mpMemory->ReadByte(mPC++) << 8;
				mAddr2 = (mAddr & 0xff00) + ((mAddr + mX) & 0x00ff);
				mAddr = mAddr + mX + ((uint32)mXH << 8);
				return kATSimEvent_None;

			case kState816ReadAddrHY:
				mAddr += mpMemory->ReadByte(mPC++) << 8;
				mAddr2 = (mAddr & 0xff00) + ((mAddr + mY) & 0x00ff);
				mAddr = mAddr + mY + ((uint32)mYH << 8);
				return kATSimEvent_None;

			case kStateReadAddrB:
				mAddrBank = mpMemory->ReadByte(mPC++);
				return kATSimEvent_None;

			case kStateReadAddrBX:
				mAddrBank = mpMemory->ReadByte(mPC++);
				{
					uint32 ea = (uint32)mAddr + mX + ((uint32)mXH << 8);

					if (ea >= 0x10000)
						++mAddrBank;

					mAddr = (uint16)ea;
				}
				return kATSimEvent_None;

			case kStateReadAddrSO:
				mAddrBank = mB;
				mAddr = mS + ((uint32)mSH << 8) + mpMemory->ReadByte(mPC++);
				if (mbEmulationFlag)
					mAddr = (uint8)mAddr + 0x100;

				return kATSimEvent_None;

			case kStateBtoD:
				mData = mB;
				break;

			case kStateKtoD:
				mData = mK;
				break;

			case kState0toD16:
				mData16 = 0;
				break;

			case kStateAtoD16:
				mData16 = ((uint32)mAH << 8) + mA;
				break;

			case kStateXtoD16:
				mData16 = ((uint32)mXH << 8) + mX;
				break;

			case kStateYtoD16:
				mData16 = ((uint32)mYH << 8) + mY;
				break;

			case kStateStoD16:
				mData16 = ((uint32)mSH << 8) + mS;
				break;

			case kStateDPtoD16:
				mData16 = mDP;
				break;

			case kStateDtoB:
				mB = mData;
				break;

			case kStateDtoA16:
				mA = (uint8)mData16;
				mAH = (uint8)(mData16 >> 8);
				break;

			case kStateDtoX16:
				mX = (uint8)mData16;
				mXH = (uint8)(mData16 >> 8);
				break;

			case kStateDtoY16:
				mY = (uint8)mData16;
				mYH = (uint8)(mData16 >> 8);
				break;

			case kStateDtoPNative:
				if ((mP & ~mData) & kFlagI)
					mbIRQReleasePending = true;
				mP = mData;
				Update65816DecodeTable();
				break;

			case kStateDtoS16:
				VDASSERT(!mbEmulationFlag);
				mS = (uint8)mData16;
				mSH = (uint8)(mData16 >> 8);
				break;

			case kStateDtoDP16:
				mDP = mData16;
				break;

			case kStateDSetSZ16:
				mP &= ~(kFlagN | kFlagZ);
				if (mData16 & 0x8000)
					mP |= kFlagN;
				if (!mData16)
					mP |= kFlagZ;
				break;

			case kStateDSetSV16:
				mP &= ~(kFlagN | kFlagV);
				mP |= ((uint8)(mData16 >> 8) & 0xC0);
				break;

			case kState816WriteByte:
				mpMemory->ExtWriteByte(mAddr, mAddrBank, mData);
				return kATSimEvent_None;

			case kStateWriteL16:
				mpMemory->ExtWriteByte(mAddr, mAddrBank, (uint8)mData16);
				return kATSimEvent_None;

			case kStateWriteH16:
				mpMemory->ExtWriteByte(mAddr + 1, mAddrBank, (uint8)(mData16 >> 8));
				return kATSimEvent_None;

			case kState816ReadByte:
				mData = mpMemory->ExtReadByte(mAddr, mAddrBank);
				return kATSimEvent_None;

			case kStateReadL16:
				mData16 = mpMemory->ExtReadByte(mAddr, mAddrBank);
				return kATSimEvent_None;

			case kStateReadH16:
				mData16 += ((uint32)mpMemory->ExtReadByte(mAddr + 1, mAddrBank) << 8);
				return kATSimEvent_None;

			case kStateAnd16:
				mData16 &= (mA + ((uint32)mAH << 8));
				mP &= ~(kFlagN | kFlagZ);
				if (mData16 & 0x8000)
					mP |= kFlagN;
				if (!mData16)
					mP |= kFlagZ;
				break;

			case kStateOr16:
				mA |= (uint8)mData16;
				mAH |= (uint8)(mData16 >> 8);
				mP &= ~(kFlagN | kFlagZ);
				if (mAH & 0x80)
					mP |= kFlagN;
				if (!(mA | mAH))
					mP |= kFlagZ;
				break;

			case kStateXor16:
				mA ^= (uint8)mData16;
				mAH ^= (uint8)(mData16 >> 8);
				mP &= ~(kFlagN | kFlagZ);
				if (mAH & 0x80)
					mP |= kFlagN;
				if (!(mA | mAH))
					mP |= kFlagZ;
				break;

			case kStateAdc16:
				if (mP & kFlagD) {
					uint32 lowResult = (mA & 15) + (mData16 & 15) + (mP & kFlagC);
					if (lowResult >= 10)
						lowResult += 6;

					uint32 midResult = (mA & 0xf0) + (mData16 & 0xf0) + lowResult;
					if (midResult >= 0xA0)
						midResult += 0x60;

					uint32 acchi = (uint32)mAH << 8;
					uint32 midHiResult = (acchi & 0xf00) + (mData16 & 0xf00) + midResult;
					if (midHiResult >= 0xA00)
						midHiResult += 0x600;

					uint32 highResult = (acchi & 0xf000) + (mData16 & 0xf000) + midHiResult;
					if (highResult >= 0xA000)
						highResult += 0x6000;

					mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

					if (highResult >= 0x10000)
						mP |= kFlagC;

					if (!(highResult & 0xffff))
						mP |= kFlagZ;

					if (highResult & 0x8000)
						mP |= kFlagN;

					mA = (uint8)highResult;
					mAH = (uint8)(highResult >> 8);
				} else {
					uint32 data = mData16;
					uint32 acc = mA + ((uint32)mAH << 8);
					uint32 carry15 = (acc & 0x7fff) + (data & 0x7fff) + (mP & kFlagC);
					uint32 result = carry15 + (acc & 0x8000) + (data & 0x8000);

					mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

					if (result & 0x8000)
						mP |= kFlagN;

					if (result >= 0x10000)
						mP |= kFlagC;

					if (!(result & 0xffff))
						mP |= kFlagZ;

					mP |= ((result >> 10) ^ (carry15 >> 9)) & kFlagV;

					mA = (uint8)result;
					mAH = (uint8)(result >> 8);
				}
				break;

			case kStateSbc16:
				if (mP & kFlagD) {
					uint32 data = (uint32)mData ^ 0xffff;
					uint32 acc = mA + ((uint32)mAH << 8);

					// BCD
					uint32 lowResult = (acc & 15) + (data & 15) + (mP & kFlagC);
					if (lowResult < 0x10)
						lowResult -= 6;

					uint32 midResult = (acc & 0xf0) + (data & 0xf0) + lowResult;
					if (midResult < 0x100)
						midResult -= 0x60;

					uint32 midHiResult = (acc & 0xf00) + (data & 0xf00) + midResult;
					if (midHiResult < 0x1000)
						midHiResult -= 0x600;

					uint32 highResult = (acc & 0xf000) + (data & 0xf000) + midHiResult;
					if (highResult < 0x10000)
						highResult -= 0x6000;

					mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

					if (highResult & 0x8000)
						mP |= kFlagN;

					if (highResult >= 0x10000)
						mP |= kFlagC;

					if (!(highResult & 0xffff))
						mP |= kFlagZ;

					mA = (uint8)highResult;
					mAH = (uint8)(highResult >> 8);
				} else {
					uint32 acc = ((uint32)mAH << 8) + mA;
					
					uint32 d16 = (uint32)mData16 ^ 0xffff;
					uint32 carry15 = (acc & 0x7fff) + (d16 & 0x7fff) + (mP & kFlagC);
					uint32 result = carry15 + (acc & 0x8000) + (d16 & 0x8000);

					mP &= ~(kFlagC | kFlagN | kFlagZ | kFlagV);

					if (result & 0x8000)
						mP |= kFlagN;

					if (result >= 0x10000)
						mP |= kFlagC;

					if (!(result & 0xffff))
						mP |= kFlagZ;

					mP |= ((result >> 10) ^ (carry15 >> 9)) & kFlagV;

					mA = (uint8)result;
					mAH = (uint8)(result >> 8);
				}
				break;

			case kStateCmp16:
				{
					uint32 acc = ((uint32)mAH << 8) + mA;
					uint32 d16 = (uint32)mData16 ^ 0xffff;
					uint32 result = acc + d16 + 1;

					mP &= ~(kFlagC | kFlagN | kFlagZ);

					if (result & 0x8000)
						mP |= kFlagN;

					if (result >= 0x10000)
						mP |= kFlagC;

					if (!(result & 0xffff))
						mP |= kFlagZ;
				}
				break;

			case kStateInc16:
				++mData16;
				mP &= ~(kFlagN | kFlagZ);
				if (mData16 & 0x8000)
					mP |= kFlagN;
				if (!mData16)
					mP |= kFlagZ;
				break;

			case kStateDec16:
				--mData16;
				mP &= ~(kFlagN | kFlagZ);
				if (mData16 & 0x8000)
					mP |= kFlagN;
				if (!mData16)
					mP |= kFlagZ;
				break;

			case kStateRol16:
				{
					uint32 result = (uint32)mData16 + (uint32)mData16 + (mP & kFlagC);
					mP &= ~(kFlagN | kFlagZ | kFlagC);
					if (result & 0x10000)
						mP |= kFlagC;
					mData16 = (uint16)result;
					if (mData16 & 0x8000)
						mP |= kFlagN;
					if (!mData16)
						mP |= kFlagZ;
				}
				break;

			case kStateRor16:
				{
					uint32 result = ((uint32)mData16 >> 1) + ((uint32)(mP & kFlagC) << 15);
					mP &= ~(kFlagN | kFlagZ | kFlagC);
					if (mData16 & 1)
						mP |= kFlagC;
					mData16 = (uint16)result;
					if (result & 0x8000)
						mP |= kFlagN;
					if (!result)
						mP |= kFlagZ;
				}
				break;

			case kStateAsl16:
				mP &= ~(kFlagN | kFlagZ | kFlagC);
				if (mData16 & 0x8000)
					mP |= kFlagC;
				mData16 += mData16;
				if (mData16 & 0x8000)
					mP |= kFlagN;
				if (!mData16)
					mP |= kFlagZ;
				break;

			case kStateLsr16:
				mP &= ~(kFlagN | kFlagZ | kFlagC);
				if (mData16 & 0x01)
					mP |= kFlagC;
				mData16 >>= 1;
				if (!mData16)
					mP |= kFlagZ;
				break;

			case kStateBit16:
				{
					uint32 acc = mA + ((uint32)mAH << 8);
					uint16 result = mData16 & acc;

					mP &= ~kFlagZ;
					if (!result)
						mP |= kFlagZ;
				}
				break;

			case kStateTrb16:
				{
					uint32 acc = mA + ((uint32)mAH << 8);

					mP &= ~kFlagZ;
					if (!(mData16 & acc))
						mP |= kFlagZ;

					mData16 &= ~acc;
				}
				break;

			case kStateTsb16:
				{
					uint32 acc = mA + ((uint32)mAH << 8);

					mP &= ~kFlagZ;
					if (!(mData16 & acc))
						mP |= kFlagZ;

					mData16 |= acc;
				}
				break;

			case kStateCmpX16:
				{
					uint32 data = (uint32)mData16 ^ 0xffff;
					uint32 result = mX + ((uint32)mXH << 8) + data + 1;

					mP &= ~(kFlagC | kFlagN | kFlagZ);

					if (result & 0x8000)
						mP |= kFlagN;

					if (result >= 0x10000)
						mP |= kFlagC;

					if (!(result & 0xffff))
						mP |= kFlagZ;
				}
				break;

			case kStateCmpY16:
				{
					uint32 data = (uint32)mData16 ^ 0xffff;
					uint32 result = mY + ((uint32)mYH << 8) + data + 1;

					mP &= ~(kFlagC | kFlagN | kFlagZ);

					if (result & 0x8000)
						mP |= kFlagN;

					if (result >= 0x10000)
						mP |= kFlagC;

					if (!(result & 0xffff))
						mP |= kFlagZ;
				}
				break;

			case kStateXba:
				{
					uint8 t = mAH;
					mAH = mA;
					mA = t;

					mP &= ~(kFlagN | kFlagZ);

					mP |= (t & kFlagN);

					if (!t)
						mP |= kFlagZ;
				}
				break;

			case kStateXce:
				{
					bool newEmuFlag = ((mP & kFlagC) != 0);
					mP &= ~kFlagC;
					if (mbEmulationFlag)
						mP |= kFlagC;

					mP |= (kFlagM | kFlagX);
					mbEmulationFlag = newEmuFlag;
					Update65816DecodeTable();
				}
				break;

			case kStatePushNative:
				mpMemory->WriteByteHL(mSH, mS, mData);
				if (!mS-- && !mbEmulationFlag)
					--mSH;
				return kATSimEvent_None;

			case kStatePushL16:
				mpMemory->WriteByteHL(mSH, mS, (uint8)mData16);
				if (!mS-- && !mbEmulationFlag)
					--mSH;
				return kATSimEvent_None;

			case kStatePushH16:
				mpMemory->WriteByteHL(mSH, mS, (uint8)(mData16 >> 8));
				if (!mS-- && !mbEmulationFlag)
					--mSH;
				return kATSimEvent_None;

			case kStatePushPBKNative:
				mpMemory->WriteByteHL(mSH, mS, mK);
				if (!mS-- && !mbEmulationFlag)
					--mSH;
				return kATSimEvent_None;

			case kStatePushPCLNative:
				mpMemory->WriteByteHL(mSH, mS, mPC & 0xff);
				if (!mS-- && !mbEmulationFlag)
					--mSH;
				return kATSimEvent_None;

			case kStatePushPCHNative:
				mpMemory->WriteByteHL(mSH, mS, mPC >> 8);
				if (!mS-- && !mbEmulationFlag)
					--mSH;
				return kATSimEvent_None;

			case kStatePushPCLM1Native:
				mpMemory->WriteByteHL(mSH, mS, (mPC - 1) & 0xff);
				if (!mS-- && !mbEmulationFlag)
					--mSH;
				return kATSimEvent_None;

			case kStatePushPCHM1Native:
				mpMemory->WriteByteHL(mSH, mS, (mPC - 1) >> 8);
				if (!mS-- && !mbEmulationFlag)
					--mSH;
				return kATSimEvent_None;

			case kStatePopNative:
				if (!++mS && !mbEmulationFlag)
					++mSH;
				mData = mpMemory->ReadByteHL(mSH, mS);
				return kATSimEvent_None;

			case kStatePopL16:
				if (!++mS && !mbEmulationFlag)
					++mSH;

				mData16 = mpMemory->ReadByteHL(mSH, mS);
				return kATSimEvent_None;

			case kStatePopH16:
				if (!++mS && !mbEmulationFlag)
					++mSH;

				mData16 += (uint32)mpMemory->ReadByteHL(mSH, mS) << 8;
				return kATSimEvent_None;

			case kStatePopPCLNative:
				if (!++mS && !mbEmulationFlag)
					++mSH;
				mPC = mpMemory->ReadByteHL(mSH, mS);
				return kATSimEvent_None;

			case kStatePopPCHNative:
				if (!++mS && !mbEmulationFlag)
					++mSH;
				mPC += (uint32)mpMemory->ReadByteHL(mSH, mS) << 8;
				return kATSimEvent_None;

			case kStatePopPCHP1Native:
				if (!++mS && !mbEmulationFlag)
					++mSH;
				mPC += ((uint32)mpMemory->ReadByteHL(mSH, mS) << 8) + 1;
				return kATSimEvent_None;

			case kStatePopPBKNative:
				if (!++mS && !mbEmulationFlag)
					++mSH;
				mK = mpMemory->ReadByteHL(mSH, mS);
				return kATSimEvent_None;

			case kStateRep:
				if (mData & kFlagI)
					mbIRQReleasePending = true;

				if (mbEmulationFlag)
					mP &= ~(mData & 0xcf);		// m and x are off-limits
				else
					mP &= ~mData;

				Update65816DecodeTable();
				return kATSimEvent_None;

			case kStateSep:
				if (mbEmulationFlag)
					mP |= mData & 0xcf;		// m and x are off-limits
				else
					mP |= mData;

				Update65816DecodeTable();
				return kATSimEvent_None;

			case kStateJ16:
				mPC += mData16;
				return kATSimEvent_None;

			case kStateJ16AddToPath:
				mPC += mData16;
				mInsnFlags[mPC] |= kInsnFlagPathStart;
				return kATSimEvent_None;

			case kState816_NatCOPVecToPC:
				mPC = 0xFFE4;
				mK = 0;
				break;

			case kState816_EmuCOPVecToPC:
				mPC = 0xFFF4;
				break;

			case kState816_NatNMIVecToPC:
				mPC = 0xFFEA;
				mK = 0;
				break;

			case kState816_NatIRQVecToPC:
				mPC = 0xFFEE;
				mK = 0;
				break;

			case kState816_NatBRKVecToPC:
				mPC = 0xFFE6;
				mK = 0;
				break;

			case kState816_SetI_ClearD:
				mP |= kFlagI;
				mP &= ~kFlagD;
				break;

			case kState816_LongAddrToPC:
				mPC = mAddr;
				mK = mAddrBank;
				break;

			case kState816_MoveRead:
				mData = mpMemory->ExtReadByte(mX + ((uint32)mXH << 8), mAddrBank);
				return kATSimEvent_None;

			case kState816_MoveWriteP:
				mAddr = mY + ((uint32)mYH << 8);
				mpMemory->ExtWriteByte(mAddr, mAddrBank, mData);

				if (!mbEmulationFlag && !(mP & kFlagX)) {
					if (!mX)
						--mXH;

					if (!mY)
						--mYH;
				}

				--mX;
				--mY;

				if (mA-- || mAH--)
						mPC -= 3;

				return kATSimEvent_None;

			case kState816_MoveWriteN:
				mAddr = mY + ((uint32)mYH << 8);
				mpMemory->ExtWriteByte(mAddr, mAddrBank, mData);

				++mX;
				++mY;
				if (!mbEmulationFlag && !(mP & kFlagX)) {
					if (!mX)
						++mXH;

					if (!mY)
						++mYH;
				}

				if (mA-- || mAH--)
						mPC -= 3;

				return kATSimEvent_None;

			case kState816_Per:
				mData16 += mPC;
				break;

#ifdef _DEBUG
			default:
				VDASSERT(!"Invalid CPU state detected.");
				break;
#endif
		}
	}
}

void ATCPUEmulator::RedecodeInsnWithoutBreak() {
	mpNextState = mStates;
	mStates[0] = kStateReadOpcodeNoBreak;
}

void ATCPUEmulator::Update65816DecodeTable() {
	ATCPUSubMode subMode = kATCPUSubMode_65C816_Emulation;

	if (!mbEmulationFlag)
		subMode = (ATCPUSubMode)(kATCPUSubMode_65C816_NativeM16X16 + ((mP >> 4) & 3));

	if (subMode == mCPUSubMode)
		return;

	mCPUSubMode = subMode;

	if (mbEmulationFlag) {
		mSH = 0x01;
		mXH = 0;
		mYH = 0;
	} else {
		if (mP & kFlagX) {
			mXH = 0;
			mYH = 0;
		}
	}

	RebuildDecodeTables();
}

void ATCPUEmulator::RebuildDecodeTables() {
	if (mpNextState && !(mpNextState >= mStates && mpNextState < mStates + sizeof(mStates)/sizeof(mStates[0]))) {
		uint8 *dst = mStates;
		for(int i=0; i<15; ++i) {
			if (mpNextState[i] == kStateReadOpcode)
				break;

			*dst++ = mpNextState[i];
		}

		*dst = kStateReadOpcode;
		mpNextState = mStates;
	}

	const bool unalignedDP = (mDP & 0xff) != 0;
	const bool emulationMode = mbEmulationFlag;
	const bool mode16 = !mbEmulationFlag && !(mP & kFlagM);
	const bool index16 = !mbEmulationFlag && !(mP & kFlagX);

	mpDstState = mDecodeHeap;
	for(int i=0; i<256; ++i) {
		mpDecodePtrs[i] = mpDstState;

		if (mbPathfindingEnabled)
			*mpDstState++ = kStateAddToPath;

		uint8 c = (uint8)i;

		switch(mCPUMode) {
			case kATCPUMode_6502:
				if (!Decode6502(c)) {
					if (!mbIllegalInsnsEnabled || !Decode6502Ill(c))
						*mpDstState++ = kStateBreakOnUnsupportedOpcode;
				}
				break;

			case kATCPUMode_65C02:
				if (!Decode65C02(c) && !Decode6502(c))
					*mpDstState++ = kStateBreakOnUnsupportedOpcode;

				break;

			case kATCPUMode_65C816:
				if (!Decode65C816(c, unalignedDP, emulationMode, mode16, index16))
					*mpDstState++ = kStateBreakOnUnsupportedOpcode;
				break;
		}

		*mpDstState++ = kStateReadOpcode;
	}

	if (mCPUMode == kATCPUMode_65C816) {
		// predecode NMI sequence
		mpDecodePtrNMI = mpDstState;
		*mpDstState++ = kStateReadDummyOpcode;
		*mpDstState++ = kStateReadDummyOpcode;

		if (emulationMode) {
			*mpDstState++ = kStatePushPCH;
			*mpDstState++ = kStatePushPCL;
			*mpDstState++ = kStatePtoD_B0;
			*mpDstState++ = kStatePush;
		} else {
			*mpDstState++ = kStatePushPBKNative;
			*mpDstState++ = kStatePushPCHNative;
			*mpDstState++ = kStatePushPCLNative;
			*mpDstState++ = kStatePtoD;
			*mpDstState++ = kStatePushNative;
		}

		*mpDstState++ = kStateSEI;

		if (emulationMode)
			*mpDstState++ = kStateNMIVecToPC;
		else
			*mpDstState++ = kState816_NatNMIVecToPC;

		*mpDstState++ = kStateReadAddrL;
		*mpDstState++ = kStateReadAddrH;
		*mpDstState++ = kStateAddrToPC;

		if (mbPathfindingEnabled)
			*mpDstState++ = kStateAddAsPathStart;

		*mpDstState++ = kStateReadOpcode;

		// predecode IRQ sequence
		mpDecodePtrIRQ = mpDstState;
		*mpDstState++ = kStateReadDummyOpcode;
		*mpDstState++ = kStateReadDummyOpcode;

		if (emulationMode) {
			*mpDstState++ = kStatePushPCH;
			*mpDstState++ = kStatePushPCL;
			*mpDstState++ = kStatePtoD_B0;
			*mpDstState++ = kStatePush;
		} else {
			*mpDstState++ = kStatePushPBKNative;
			*mpDstState++ = kStatePushPCHNative;
			*mpDstState++ = kStatePushPCLNative;
			*mpDstState++ = kStatePtoD;
			*mpDstState++ = kStatePushNative;
		}

		*mpDstState++ = kStateSEI;

		if (emulationMode)
			*mpDstState++ = kStateIRQVecToPC;
		else
			*mpDstState++ = kState816_NatIRQVecToPC;

		*mpDstState++ = kStateReadAddrL;
		*mpDstState++ = kStateReadAddrH;
		*mpDstState++ = kStateAddrToPC;

		if (mbPathfindingEnabled)
			*mpDstState++ = kStateAddAsPathStart;

		*mpDstState++ = kStateReadOpcode;
	} else {
		// predecode NMI sequence
		mpDecodePtrNMI = mpDstState;
		*mpDstState++ = kStateReadDummyOpcode;
		*mpDstState++ = kStateReadDummyOpcode;
		*mpDstState++ = kStatePushPCH;
		*mpDstState++ = kStatePushPCL;
		*mpDstState++ = kStatePtoD_B0;
		*mpDstState++ = kStatePush;
		*mpDstState++ = kStateSEI;
		*mpDstState++ = kStateNMIVecToPC;
		*mpDstState++ = kStateReadAddrL;
		*mpDstState++ = kStateReadAddrH;
		*mpDstState++ = kStateAddrToPC;

		if (mbPathfindingEnabled)
			*mpDstState++ = kStateAddAsPathStart;

		*mpDstState++ = kStateReadOpcode;

		// predecode IRQ sequence
		mpDecodePtrIRQ = mpDstState;
		*mpDstState++ = kStateReadDummyOpcode;
		*mpDstState++ = kStateReadDummyOpcode;
		*mpDstState++ = kStatePushPCH;
		*mpDstState++ = kStatePushPCL;
		*mpDstState++ = kStatePtoD_B0;
		*mpDstState++ = kStatePush;
		*mpDstState++ = kStateSEI;
		*mpDstState++ = kStateIRQVecToPC;
		*mpDstState++ = kStateReadAddrL;
		*mpDstState++ = kStateReadAddrH;
		*mpDstState++ = kStateAddrToPC;

		if (mbPathfindingEnabled)
			*mpDstState++ = kStateAddAsPathStart;

		*mpDstState++ = kStateReadOpcode;
	}

	VDASSERT(mpDstState <= mDecodeHeap + sizeof mDecodeHeap / sizeof mDecodeHeap[0]);
}

void ATCPUEmulator::DecodeReadZp() {
	*mpDstState++ = kStateReadAddrL;
	*mpDstState++ = kStateRead;
}

void ATCPUEmulator::DecodeReadZpX() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateReadAddX;			// 3
	*mpDstState++ = kStateRead;				// 4
}

void ATCPUEmulator::DecodeReadZpY() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateReadAddY;			// 3
	*mpDstState++ = kStateRead;				// 4
}

void ATCPUEmulator::DecodeReadAbs() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateReadAddrH;		// 3
	*mpDstState++ = kStateRead;				// 4
}

void ATCPUEmulator::DecodeReadAbsX() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateReadAddrHX;		// 3
	*mpDstState++ = kStateReadCarry;		// 4
	*mpDstState++ = kStateRead;				// (5)
}

void ATCPUEmulator::DecodeReadAbsY() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateReadAddrHY;		// 3
	*mpDstState++ = kStateReadCarry;		// 4
	*mpDstState++ = kStateRead;				// (5)
}

void ATCPUEmulator::DecodeReadIndX() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateReadAddX;			// 3
	*mpDstState++ = kStateRead;				// 4
	*mpDstState++ = kStateReadIndAddr;		// 5
	*mpDstState++ = kStateRead;				// 6
}

void ATCPUEmulator::DecodeReadIndY() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateRead;				// 3
	*mpDstState++ = kStateReadIndYAddr;		// 4
	*mpDstState++ = kStateReadCarry;		// 5
	*mpDstState++ = kStateRead;				// (6)
}

void ATCPUEmulator::DecodeReadInd() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateRead;				// 3
	*mpDstState++ = kStateReadIndAddr;		// 4
	*mpDstState++ = kStateRead;				// 5
}
