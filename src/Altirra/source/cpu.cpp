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
#include <vd2/system/binary.h>
#include "cpu.h"
#include "cpustates.h"
#include "console.h"
#include "disasm.h"
#include "profiler.h"
#include "savestate.h"
#include "simulator.h"
#include "verifier.h"
#include "bkptmanager.h"
#include "cpuhookmanager.h"
#include "cpuheatmap.h"

using namespace AT6502;

ATCPUEmulator::ATCPUEmulator()
	: mpMemory(NULL)
	, mpHookMgr(NULL)
	, mpHLE(NULL)
	, mpProfiler(NULL)
	, mpVerifier(NULL)
	, mpHeatMap(NULL)
	, mpBkptManager(NULL)
{
	mSBrk = 0x100;
	mbTrace = false;
	mbStep = false;
	mDebugFlags = 0;
	mpNextState = NULL;
	mCPUMode = kATCPUMode_6502;
	mSubCycles = 1;
	mSubCyclesLeft = 1;
	mbForceNextCycleSlow = false;
	mCPUSubMode = kATCPUSubMode_6502;
	mAdvanceMode = kATCPUAdvanceMode_6502;

	memset(mInsnFlags, 0, sizeof mInsnFlags);

	mbHistoryOrProfilingEnabled = false;
	mbHistoryEnabled = false;
	mbPathfindingEnabled = false;
	mbPathBreakEnabled = false;
	mbIllegalInsnsEnabled = true;
	mbStopOnBRK = false;
	mbAllowBlockedNMIs = false;
	mBreakpointCount = 0;

	RebuildDecodeTables();

	VDASSERTCT(kStateStandard_Count <= kStateUpdateHeatMap);
	VDASSERTCT(kStateCount < 256);
}

ATCPUEmulator::~ATCPUEmulator() {
}

bool ATCPUEmulator::Init(ATCPUEmulatorMemory *mem, ATCPUHookManager *hookmgr, ATCPUEmulatorCallbacks *callbacks) {
	mpMemory = mem;
	mpHookMgr = hookmgr;
	mpCallbacks = callbacks;

	ColdReset();

	memset(mHistory, 0, sizeof mHistory);
	mHistoryIndex = 0;

	return true;
}

void ATCPUEmulator::SetHLE(IATCPUHighLevelEmulator *hle) {
	mpHLE = hle;
}

void ATCPUEmulator::SetBreakpointManager(ATBreakpointManager *bkptmanager) {
	mpBkptManager = bkptmanager;
}

void ATCPUEmulator::ColdReset() {
	mbStep = false;
	mbTrace = false;
	mDebugFlags &= ~(kDebugFlag_Step | kDebugFlag_Trace);
	WarmReset();
	mNMIIgnoreUnhaltedCycle = 0xFFFFFFFFU;
}

void ATCPUEmulator::WarmReset() {
	mpNextState = mStates;
	mIFlagSetCycle = mpCallbacks->CPUGetUnhaltedCycle();
	mIntFlags = 0;
	mbNMIForced = false;
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
	mbEmulationFlag = true;

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

		// force reinit
		mDecodeTableMode816 = 0xFF;
		Update65816DecodeTable();
	}

	if (mpVerifier)
		mpVerifier->OnReset();
}

bool ATCPUEmulator::IsInstructionInProgress() const {
	uint8 state = *mpNextState;

	if (state == kStateUpdateHeatMap)
		state = mpNextState[1];

	return state != kStateReadOpcode && state != kStateReadOpcodeNoBreak;
}

bool ATCPUEmulator::IsNextCycleWrite() const {
	const uint8 *p = mpNextState;
	for(;;) {
		const uint8 state = *p++;

		switch(state) {
			case kStateReadOpcode:
			case kStateReadOpcodeNoBreak:
			case kStateReadDummyOpcode:
			case kStateReadImm:
			case kStateReadAddrL:
			case kStateReadAddrH:
			case kStateReadAddrHX:
			case kStateReadAddrHY:
			case kStateReadAddrHX_SHY:
			case kStateRead:
			case kStateReadAddX:
			case kStateReadAddY:
			case kStateReadCarry:
			case kStateReadCarryForced:
			case kStateReadAbsIndAddr:
			case kStateReadAbsIndAddrBroken:
			case kStateReadIndAddr:
			case kStateReadIndYAddr:
			case kStateReadIndYAddr_SHA:
			case kStatePop:
			case kStatePopPCL:
			case kStatePopPCH:
			case kStatePopPCHP1:
			case kStateReadRel:
			case kStateReadImmL16:
			case kStateReadImmH16:
			case kStateReadAddrDp:
			case kStateReadAddrDpX:
			case kStateReadAddrDpY:
			case kStateReadIndAddrDp:
			case kStateReadIndAddrDpY:
			case kStateReadIndAddrDpLongH:
			case kStateReadIndAddrDpLongB:
			case kStateReadAddrAddY:
			case kState816ReadAddrL:
			case kState816ReadAddrH:
			case kState816ReadAddrHX:
			case kState816ReadAddrAbsXSpec:
			case kState816ReadAddrAbsXAlways:
			case kState816ReadAddrAbsYSpec:
			case kState816ReadAddrAbsYAlways:
			case kStateRead816AddrAbsLongL:
			case kStateRead816AddrAbsLongH:
			case kStateRead816AddrAbsLongB:
			case kStateReadAddrB:
			case kStateReadAddrBX:
			case kStateReadAddrSO:
			case kState816ReadAddrSO_AddY:
			case kState816ReadByte:
			case kStateReadL16:
			case kStateReadH16:
			case kStateReadH16_DpBank:
			case kStatePopNative:
			case kStatePopL16:
			case kStatePopH16:
			case kStatePopPBKNative:
			case kStatePopPCLNative:
			case kStatePopPCHNative:
			case kStatePopPCHP1Native:
			case kState816_MoveRead:
			case kStateWait:
				return false;

			case kStateWrite:
			case kStatePush:
			case kStatePushPCL:
			case kStatePushPCH:
			case kStatePushPCLM1:
			case kStatePushPCHM1:
			case kState816WriteByte:
			case kStateWriteL16:
			case kStateWriteH16:
			case kStateWriteH16_DpBank:
			case kStatePushNative:
			case kStatePushL16:
			case kStatePushH16:
			case kStatePushPBKNative:
			case kStatePushPCLNative:
			case kStatePushPCHNative:
			case kStatePushPCLM1Native:
			case kStatePushPCHM1Native:
			case kState816_MoveWriteP:
			case kState816_MoveWriteN:
				return true;
		}
	}
}

void ATCPUEmulator::SetEmulationFlag(bool emu) {
	if (mCPUMode != kATCPUMode_65C816)
		return;

	if (mbEmulationFlag == emu)
		return;

	mbEmulationFlag = emu;

	Update65816DecodeTable();
}

void ATCPUEmulator::SetPC(uint16 pc) {
	mPC = pc;
	mInsnPC = pc;
	mpDstState = mStates;
	mpNextState = mStates;
	*mpDstState++ = kStateReadOpcodeNoBreak;
}

void ATCPUEmulator::SetP(uint8 p) {
	if (mCPUMode != kATCPUMode_65C816 || mCPUSubMode == kATCPUSubMode_65C816_Emulation)
		p |= 0x30;

	if (mP != p) {
		mP = p;

		if (!(p & AT6502::kFlagX)) {
			mXH = 0;
			mYH = 0;
		}
	}
}

void ATCPUEmulator::SetAH(uint8 a) {
	if (!(mP & AT6502::kFlagM))
		mAH = a;
}

void ATCPUEmulator::SetXH(uint8 x) {
	if (!(mP & AT6502::kFlagX))
		mXH = x;
}

void ATCPUEmulator::SetYH(uint8 y) {
	if (!(mP & AT6502::kFlagX))
		mYH = y;
}

void ATCPUEmulator::SetSH(uint8 s) {
	if (!mbEmulationFlag)
		mSH = s;
}

void ATCPUEmulator::SetD(uint16 dp) {
	if (mCPUMode == kATCPUMode_65C816)
		mDP = dp;
}

void ATCPUEmulator::SetK(uint8 k) {
	if (mCPUMode == kATCPUMode_65C816)
		mK = k;
}

void ATCPUEmulator::SetB(uint8 b) {
	if (mCPUMode == kATCPUMode_65C816)
		mB = b;
}

void ATCPUEmulator::SetHook(uint16 pc, bool enable) {
	if (enable)
		mInsnFlags[pc] |= kInsnFlagHook;
	else
		mInsnFlags[pc] &= ~kInsnFlagHook;
}

void ATCPUEmulator::SetStepRange(uint32 regionStart, uint32 regionSize, ATCPUStepCallback stepcb, void *stepcbdata, bool stepOver) {
	mbStep = true;

	mStepRegionStart = regionStart;
	mStepRegionSize = regionSize;
	mpStepCallback = stepcb;
	mpStepCallbackData = stepcbdata;
	mStepStackLevel = -1;

	mDebugFlags |= kDebugFlag_Step;

	if (mbStepOver != stepOver) {
		mbStepOver = stepOver;

		RebuildDecodeTables();
	}
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

uint8 ATCPUEmulator::GetHeldCycleValue() {
	if (mCPUMode == kATCPUMode_65C816)
		return mpMemory->ExtReadByte(mPC, mK);
	else
		return mpMemory->ReadByte(mPC);
}

void ATCPUEmulator::SetCPUMode(ATCPUMode mode, uint32 subCycles) {
	if (subCycles < 1 || mode != kATCPUMode_65C816)
		subCycles = 1;

	if (subCycles > 16)
		subCycles = 16;

	if (mCPUMode == mode && mSubCycles == subCycles)
		return;

	mCPUMode = mode;
	mSubCycles = subCycles;
	mSubCyclesLeft = subCycles;
	mbForceNextCycleSlow = false;

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
			mAdvanceMode = subCycles > 1 ? kATCPUAdvanceMode_65816HiSpeed : kATCPUAdvanceMode_65816;
			break;
		case kATCPUMode_65C02:
			mCPUSubMode = kATCPUSubMode_65C02;
			mAdvanceMode = kATCPUAdvanceMode_6502;
			break;
		case kATCPUMode_6502:
			mCPUSubMode = kATCPUSubMode_6502;
			mAdvanceMode = kATCPUAdvanceMode_6502;
			break;
	}

	RebuildDecodeTables();
}

void ATCPUEmulator::SetHistoryEnabled(bool enable) {
	if (mbHistoryEnabled != enable) {
		mbHistoryEnabled = enable;

		mbHistoryOrProfilingEnabled = mbHistoryEnabled || mpProfiler;

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

void ATCPUEmulator::SetProfiler(ATCPUProfiler *profiler) {
	if (mpProfiler != profiler) {
		mpProfiler = profiler;

		mbHistoryOrProfilingEnabled = mbHistoryEnabled || mpProfiler;
		RebuildDecodeTables();
	}
}

void ATCPUEmulator::SetVerifier(ATCPUVerifier *verifier) {
	if (mpVerifier != verifier) {
		mpVerifier = verifier;

		RebuildDecodeTables();
	}
}

void ATCPUEmulator::SetHeatMap(ATCPUHeatMap *heatmap) {
	if (mpHeatMap != heatmap) {
		mpHeatMap = heatmap;

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

void ATCPUEmulator::SetNMIBlockingEnabled(bool enable) {
	if (mbAllowBlockedNMIs != enable) {
		mbAllowBlockedNMIs = enable;
		RebuildDecodeTables();
	}
}

bool ATCPUEmulator::IsBreakpointSet(uint16 addr) const {
	return 0 != (mInsnFlags[addr] & kInsnFlagBreakPt);
}

void ATCPUEmulator::SetBreakpoint(uint16 addr) {
	if (!(mInsnFlags[addr] & kInsnFlagBreakPt)) {
		mInsnFlags[addr] |= kInsnFlagBreakPt;

		if (!mBreakpointCount++)
			mDebugFlags |= kDebugFlag_BP;
	}
}

void ATCPUEmulator::ClearBreakpoint(uint16 addr) {
	if (mInsnFlags[addr] & kInsnFlagBreakPt) {
		mInsnFlags[addr] &= ~kInsnFlagBreakPt;

		if (!--mBreakpointCount)
			mDebugFlags &= ~kDebugFlag_BP;
	}
}

void ATCPUEmulator::ClearAllBreakpoints() {
	for(uint32 i=0; i<65536; ++i)
		mInsnFlags[i] &= ~kInsnFlagBreakPt;

	mBreakpointCount = 0;
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

void ATCPUEmulator::DumpStatus(bool extended) {
	if (mCPUMode == kATCPUMode_65C816) {
		if (mbEmulationFlag) {
			ATConsoleTaggedPrintf("C=%02X%02X X=%02X Y=%02X S=%02X P=%02X (%c%c%c%c%c%c)  "
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
			if (!(mP & kFlagX)) {
				ATConsoleTaggedPrintf("%c=%02X%02X X=%02X%02X Y=%02X%02X S=%02X%02X P=%02X (%c%c%c %c%c%c%c)  "
					, (mP & kFlagM) ? 'C' : 'A'
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
					, mP & 0x08 ? 'D' : ' '
					, mP & 0x04 ? 'I' : ' '
					, mP & 0x02 ? 'Z' : ' '
					, mP & 0x01 ? 'C' : ' '
					);
			} else {
				ATConsoleTaggedPrintf("%c=%02X%02X X=--%02X Y=--%02X S=%02X%02X P=%02X (%c%c%cX%c%c%c%c)  "
					, (mP & kFlagM) ? 'C' : 'A'
					, mAH
					, mA
					, mX
					, mY
					, mSH
					, mS
					, mP
					, mP & 0x80 ? 'N' : ' '
					, mP & 0x40 ? 'V' : ' '
					, mP & 0x20 ? 'M' : ' '
					, mP & 0x08 ? 'D' : ' '
					, mP & 0x04 ? 'I' : ' '
					, mP & 0x02 ? 'Z' : ' '
					, mP & 0x01 ? 'C' : ' '
					);
			}
		}
	} else {
		ATConsoleTaggedPrintf("A=%02X X=%02X Y=%02X S=%02X P=%02X (%c%c%c%c%c%c)  "
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

	ATDisassembleInsn(mInsnPC, mK);

	if (extended && mCPUMode == kATCPUMode_65C816)
		ATConsolePrintf("              B=%02X D=%04X\n", mB, mDP);
}

void ATCPUEmulator::BeginLoadState(ATSaveStateReader& reader) {
	reader.RegisterHandlerMethod(kATSaveStateSection_Arch, VDMAKEFOURCC('6', '5', '0', '2'), this, &ATCPUEmulator::LoadState6502);
	reader.RegisterHandlerMethod(kATSaveStateSection_Arch, VDMAKEFOURCC('C', '8', '1', '6'), this, &ATCPUEmulator::LoadState65C816);
	reader.RegisterHandlerMethod(kATSaveStateSection_Private, VDMAKEFOURCC('6', '5', '0', '2'), this, &ATCPUEmulator::LoadStatePrivate);
	reader.RegisterHandlerMethod(kATSaveStateSection_ResetPrivate, 0, this, &ATCPUEmulator::LoadStateResetPrivate);
	reader.RegisterHandlerMethod(kATSaveStateSection_End, 0, this, &ATCPUEmulator::EndLoadState);
}

void ATCPUEmulator::LoadState6502(ATSaveStateReader& reader) {
	SetPC(reader.ReadUint16());
	mA		= reader.ReadUint8();
	mX		= reader.ReadUint8();
	mY		= reader.ReadUint8();
	mS		= reader.ReadUint8();
	mP		= reader.ReadUint8();
}

void ATCPUEmulator::LoadState65C816(ATSaveStateReader& reader) {
	mDP		= reader.ReadUint16();
	mAH		= reader.ReadUint8();
	mXH		= reader.ReadUint8();
	mYH		= reader.ReadUint8();
	mSH		= reader.ReadUint8();
	mbEmulationFlag	= (reader.ReadUint8() & 1) != 0;
	mB		= reader.ReadUint8();
	mK		= reader.ReadUint8();
	SetPC(reader.ReadUint16());
	mIntFlags = reader.ReadUint8();
}

void ATCPUEmulator::LoadStatePrivate(ATSaveStateReader& reader) {
	const uint32 t = mpCallbacks->CPUGetUnhaltedCycle();

	mbUnusedCycle = reader.ReadBool();
	mIntFlags = reader.ReadUint8();
	mIRQAssertTime = reader.ReadUint32() + t;
	mIRQAcknowledgeTime = reader.ReadUint32() + t;
	mNMIAssertTime = reader.ReadUint32() + t;
}

void ATCPUEmulator::LoadStateResetPrivate(ATSaveStateReader& reader) {
	mIRQAssertTime = mpCallbacks->CPUGetUnhaltedCycle() - 10;
	mIRQAcknowledgeTime = mIRQAssertTime;
	mbUnusedCycle	= false;
}

void ATCPUEmulator::EndLoadState(ATSaveStateReader& reader) {
	mHLEDelay		= 0;
}

void ATCPUEmulator::BeginSaveState(ATSaveStateWriter& writer) {
	writer.RegisterHandlerMethod(kATSaveStateSection_Arch, this, &ATCPUEmulator::SaveStateArch);
	writer.RegisterHandlerMethod(kATSaveStateSection_Private, this, &ATCPUEmulator::SaveStatePrivate);
}

void ATCPUEmulator::SaveStateArch(ATSaveStateWriter& writer) {
	writer.BeginChunk(VDMAKEFOURCC('6','5','0','2'));

	// We write PC and not InsnPC here because the opcode fetch hasn't happened yet.
	writer.WriteUint16(mPC);
	writer.WriteUint8(mA);
	writer.WriteUint8(mX);
	writer.WriteUint8(mY);
	writer.WriteUint8(mS);
	writer.WriteUint8(mP);
	writer.EndChunk();

	if (mCPUMode == kATCPUMode_65C816) {
		writer.BeginChunk(VDMAKEFOURCC('C', '8', '1', '6'));
		writer.WriteUint16(mDP);
		writer.WriteUint8(mAH);
		writer.WriteUint8(mXH);
		writer.WriteUint8(mYH);
		writer.WriteUint8(mSH);
		writer.WriteUint8(mbEmulationFlag ? 1 : 0);
		writer.WriteUint8(mB);
		writer.WriteUint8(mK);
		writer.EndChunk();
	}
}

void ATCPUEmulator::SaveStatePrivate(ATSaveStateWriter& writer) {
	const uint32 t = mpCallbacks->CPUGetUnhaltedCycle();

	writer.BeginChunk(VDMAKEFOURCC('6','5','0','2'));
	writer.WriteUint8(mbUnusedCycle);
	writer.WriteUint8(mIntFlags);
	writer.WriteUint32(mIRQAssertTime - t);
	writer.WriteUint32(mIRQAcknowledgeTime - t);
	writer.WriteUint32(mNMIAssertTime - t);
	writer.EndChunk();
}

void ATCPUEmulator::InjectOpcode(uint8 op) {
	mOpcode = op;
	mpNextState = mDecodeHeap + mDecodePtrs[mOpcode];
}

void ATCPUEmulator::Push(uint8 v) {
	mpMemory->WriteByte(0x100 + mS, v);
	--mS;
}

void ATCPUEmulator::PushWord(uint16 v) {
	Push((uint8)(v >> 8));
	Push((uint8)v);
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

void ATCPUEmulator::Ldy(uint8 v) {
	mY = v;

	mP &= ~kFlagN & ~kFlagZ;
	mP |= (v & 0x80);

	if (!v)
		mP |= kFlagZ;
}

void ATCPUEmulator::AssertIRQ(int cycleOffset) {
	if (!(mIntFlags & kIntFlag_IRQActive)) {
		if (mIntFlags & kIntFlag_IRQPending)
			UpdatePendingIRQState();

		mIntFlags |= kIntFlag_IRQActive;
		mIRQAssertTime = mpCallbacks->CPUGetUnhaltedCycle() + cycleOffset;
	}
}

void ATCPUEmulator::NegateIRQ() {
	if (mIntFlags & kIntFlag_IRQActive) {
		if (!(mIntFlags & kIntFlag_IRQPending))
			UpdatePendingIRQState();

		mIntFlags &= ~kIntFlag_IRQActive;
	}
}

void ATCPUEmulator::AssertNMI() {
	if (!(mIntFlags & kIntFlag_NMIPending)) {
		mIntFlags |= kIntFlag_NMIPending;
		mNMIAssertTime = mpCallbacks->CPUGetUnhaltedCycle();
	}
}

void ATCPUEmulator::PeriodicCleanup() {
	// check if we need to bump the interrupt assert times forward to avoid wrapping
	const uint32 t = mpCallbacks->CPUGetUnhaltedCycle();

	if (((t - mNMIAssertTime) >> 30) == 2)
		mNMIAssertTime += 0x40000000;

	if (((t - mNMIIgnoreUnhaltedCycle) >> 30) == 2)
		mNMIIgnoreUnhaltedCycle += 0x40000000;

	if (((t - mIRQAssertTime) >> 30) == 2)
		mIRQAssertTime += 0x40000000;
	
	if (((t - mIRQAcknowledgeTime) >> 30) == 2)
		mIRQAcknowledgeTime += 0x40000000;

	if (((t - mIFlagSetCycle) >> 30) == 2)
		mIFlagSetCycle += 0x40000000;
}

int ATCPUEmulator::Advance() {
	if (mCPUMode == kATCPUMode_65C816) {
		if (mSubCycles > 1)
			return Advance65816HiSpeed(false);
		else
			return Advance65816();
	} else
		return Advance6502();
}

int ATCPUEmulator::Advance6502() {
	#include "cpumachine.inl"
}

int ATCPUEmulator::Advance65816() {
#define AT_CPU_MACHINE_65C816
	#include "cpumachine.inl"
#undef AT_CPU_MACHINE_65C816
}

int ATCPUEmulator::Advance65816HiSpeed(bool dma) {
	if (dma) {
		if (!--mSubCyclesLeft) {
			mSubCyclesLeft = mSubCycles;
			return kATSimEvent_None;
		}
	}

	for(;;) {
		#define AT_CPU_MACHINE_65C816
		#define AT_CPU_MACHINE_65C816_HISPEED
		#include "cpumachine.inl"
		#undef AT_CPU_MACHINE_65C816_HISPEED
		#undef AT_CPU_MACHINE_65C816

end_sub_cycle:
		if (!--mSubCyclesLeft) {
wait_slow_cycle:
			if (mbForceNextCycleSlow) {
				mbForceNextCycleSlow = false;
				mSubCyclesLeft = 1;
			} else {
				mSubCyclesLeft = mSubCycles;
			}
			break;
		}
	}

	return kATSimEvent_None;
}

uint8 ATCPUEmulator::ProcessDebugging() {
	uint8 iflags = mInsnFlags[mPC];

	if (iflags & kInsnFlagBreakPt) {
		ATSimulatorEvent event = (ATSimulatorEvent)mpBkptManager->TestPCBreakpoint(mPC);
		if (event) {
			mbUnusedCycle = true;
			RedecodeInsnWithoutBreak();
			return event;
		}
	}

	if (mbStep && (mPC - mStepRegionStart) >= mStepRegionSize) {
		if (mStepStackLevel >= 0 && (sint8)(mS - mStepStackLevel) <= 0) {
			// do nothing -- stepping through subroutine
		} else {
			mStepStackLevel = -1;

			ATCPUStepResult stepResult = kATCPUStepResult_Stop;

			if (mpStepCallback)
				stepResult = mpStepCallback(this, mPC, false, mpStepCallbackData);

			if (stepResult == kATCPUStepResult_SkipCall) {
				mStepStackLevel = mS;
			} else if (stepResult == kATCPUStepResult_Stop) {
				mbStep = false;
				mbStepOver = false;
				mDebugFlags &= ~kDebugFlag_Step;

				mbUnusedCycle = true;
				RedecodeInsnWithoutBreak();
				return kATSimEvent_CPUSingleStep;
			}
		}
	}

	if (mS >= mSBrk) {
		mSBrk = 0x100;
		mbUnusedCycle = true;
		RedecodeInsnWithoutBreak();
		return kATSimEvent_CPUStackBreakpoint;
	}

	if (mbTrace)
		DumpStatus();

	return 0;
}

void ATCPUEmulator::ProcessStepOver() {
	if (mStepStackLevel >= 0) {
		// We apply a two-byte adjustment here to do this test approximately
		// at the level of the parent (we can't do it exactly).
		if ((sint8)(mS - mStepStackLevel) <= -2)
			return;

		mStepStackLevel = -1;
	}

	ATCPUStepResult stepResult = kATCPUStepResult_Stop;

	if (mpStepCallback) {
		// This needs to use PC instead of insnPC, because we are
		// in the middle of a JSR or interrupt dispatch.
		stepResult = mpStepCallback(this, mPC, true, mpStepCallbackData);
	}

	switch(stepResult) {
		case kATCPUStepResult_SkipCall:
			mStepStackLevel = mS;
			break;
		case kATCPUStepResult_Stop:
			mbStepOver = false;
			mpStepCallback = NULL;
			mStepRegionStart = 0;
			mStepRegionSize = 0;
			mStepStackLevel = -1;

			RebuildDecodeTables();
			break;

		case kATCPUStepResult_Continue:
		default:
			break;
	}
}

template<bool T_Accel>
bool ATCPUEmulator::ProcessInterrupts() {
	if (mIntFlags & kIntFlag_NMIPending) {
		uint32 t = mpCallbacks->CPUGetUnhaltedCycle();

		if (T_Accel || t - mNMIAssertTime >= (t == mNMIIgnoreUnhaltedCycle ? 3U : 2U)) {
			if (mbTrace)
				ATConsoleWrite("CPU: Jumping to NMI vector\n");

			mIntFlags &= ~kIntFlag_NMIPending;
			mbMarkHistoryNMI = true;

			mpNextState = mpDecodePtrNMI;
			return true;
		}
	}

	if (mIntFlags & kIntFlag_IRQReleasePending) {
		mIntFlags &= ~kIntFlag_IRQReleasePending;
	} else if ((mIntFlags & (kIntFlag_IRQActive | kIntFlag_IRQPending)) && (!(mP & kFlagI) || (!T_Accel && mIFlagSetCycle == mpCallbacks->CPUGetUnhaltedCycle() - 1))) {
		if (mNMIIgnoreUnhaltedCycle == mIRQAssertTime + 1) {
			++mIRQAssertTime;
		} else {
			switch(mIntFlags & (kIntFlag_IRQPending | kIntFlag_IRQActive)) {
				case kIntFlag_IRQPending:
				case kIntFlag_IRQActive:
					UpdatePendingIRQState();
					break;
			}

			// If an IRQ is pending, check if it was just acknowledged this cycle. If so,
			// bypass it as it's too late to interrupt this instruction.
			if ((mIntFlags & kIntFlag_IRQPending) && mpCallbacks->CPUGetUnhaltedCycle() - mIRQAcknowledgeTime > 0) {
				if (mbTrace)
					ATConsoleWrite("CPU: Jumping to IRQ vector\n");

				mbMarkHistoryIRQ = true;

				mpNextState = mpDecodePtrIRQ;
				return true;
			}
		}
	}

	return false;
}

uint8 ATCPUEmulator::ProcessHook() {
	uint8 op = mpHookMgr->OnHookHit(mPC);

	if (op && op != 0x4C) {
		++mPC;
		mOpcode = op;
		mpNextState = mDecodeHeap + mDecodePtrs[mOpcode];
	}

	return op;
}

uint8 ATCPUEmulator::ProcessHook816() {
	// We block hooks if any of the following are true:
	// - the CPU is in native mode
	// - PBK != 0
	// - DBK != 0
	// - D != 0

	if (!mbEmulationFlag || mDP || mK || mB)
		return 0;

	return ProcessHook();
}

template<bool is816, bool hispeed>
void ATCPUEmulator::AddHistoryEntry(bool slowFlag) {
	HistoryEntry& he = mHistory[mHistoryIndex++ & 131071];

	he.mCycle = (uint16)mpCallbacks->CPUGetCycle();
	he.mUnhaltedCycle = (uint16)mpCallbacks->CPUGetUnhaltedCycle();
	he.mTimestamp = mpCallbacks->CPUGetTimestamp();
	he.mEA = 0xFFFFFFFFUL;
	he.mPC = mPC - 1;
	he.mS = mS;
	he.mP = mP;
	he.mA = mA;
	he.mX = mX;
	he.mY = mY;

	he.mOpcode[0] = mOpcode;

	if (is816) {
		he.mOpcode[1] = mpMemory->DebugExtReadByte(mPC, mK);
		he.mOpcode[2] = mpMemory->DebugExtReadByte(mPC+1, mK);
		he.mOpcode[3] = mpMemory->DebugExtReadByte(mPC+2, mK);
	} else {
		he.mOpcode[1] = mpMemory->DebugReadByte(mPC);
		he.mOpcode[2] = mpMemory->DebugReadByte(mPC+1);
		he.mOpcode[3] = mpMemory->DebugReadByte(mPC+2);
	}

	he.mbIRQ = mbMarkHistoryIRQ;
	he.mbNMI = mbMarkHistoryNMI;

	if (is816) {
		if (hispeed && !slowFlag)
			he.mSubCycle = mSubCycles - mSubCyclesLeft;
		else
			he.mSubCycle = 0;

		he.mbEmulation = mbEmulationFlag;
		he.mSH = mSH;
		he.mAH = mAH;
		he.mXH = mXH;
		he.mYH = mYH;
		he.mB = mB;
		he.mK = mK;
		he.mD = mDP;
	} else {
		he.mbEmulation = true;
		he.mSubCycle = 0;
		he.mSH = 1;
		he.mAH = 0;
		he.mXH = 0;
		he.mYH = 0;
		he.mB = 0;
		he.mK = 0;
		he.mD = 0;
	}

	mbMarkHistoryIRQ = false;
	mbMarkHistoryNMI = false;
}

void ATCPUEmulator::UpdatePendingIRQState() {
	VDASSERT(((mIntFlags & kIntFlag_IRQActive) != 0) != ((mIntFlags & kIntFlag_IRQPending) != 0));

	const uint32 cycle = mpCallbacks->CPUGetUnhaltedCycle();

	if (mIntFlags & kIntFlag_IRQActive) {
		// IRQ line is pulled down, but no IRQ acknowledge has happened. Check if the IRQ
		// line has been down for a minimum of three cycles; if so, set the pending
		// IRQ flag.

		if (cycle - mIRQAssertTime >= 3) {
			mIntFlags |= kIntFlag_IRQPending;
			mIRQAcknowledgeTime = mIRQAssertTime + 3;
		}
	} else {
		// IRQ line is pulled up, but an IRQ acknowledge is pending. Check if more than
		// one cycle has passed by. If so, kill the acknowledged IRQ.

		if (cycle - mIRQAcknowledgeTime >= 2)
			mIntFlags &= ~kIntFlag_IRQPending;
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

	if (mCPUSubMode != subMode) {
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
	}

	uint8 decMode = (uint8)(subMode + ((mDP & 0xff) ? 5 : 0) - kATCPUSubMode_65C816_Emulation);

	if (mDecodeTableMode816 != decMode) {
		mDecodeTableMode816 = decMode;
		memcpy(mDecodePtrs, mDecodePtrs816[decMode], sizeof mDecodePtrs);

		mpDecodePtrNMI = mDecodeHeap + mDecodePtrs816[decMode][256];
		mpDecodePtrIRQ = mDecodeHeap + mDecodePtrs816[decMode][257];
	}
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

	switch(mCPUMode) {
		case kATCPUMode_6502:
			RebuildDecodeTables6502(false);
			break;

		case kATCPUMode_65C02:
			RebuildDecodeTables6502(true);
			break;

		case kATCPUMode_65C816:
			RebuildDecodeTables65816();
			Update65816DecodeTable();
			break;
	}

	VDASSERT(mpDstState <= mDecodeHeap + sizeof mDecodeHeap / sizeof mDecodeHeap[0]);
}

void ATCPUEmulator::RebuildDecodeTables6502(bool cmos) {
	const bool unalignedDP = (mDP & 0xff) != 0;
	const bool emulationMode = mbEmulationFlag;
	const bool mode16 = !mbEmulationFlag && !(mP & kFlagM);
	const bool index16 = !mbEmulationFlag && !(mP & kFlagX);

	mpDstState = mDecodeHeap;
	for(int i=0; i<256; ++i) {
		mDecodePtrs[i] = mpDstState - mDecodeHeap;

		if (mbPathfindingEnabled)
			*mpDstState++ = kStateAddToPath;

		uint8 c = (uint8)i;

		if (cmos) {
			if (!Decode65C02(c) && !Decode6502(c))
				*mpDstState++ = kStateBreakOnUnsupportedOpcode;
		} else {
			if (!Decode6502(c)) {
				if (!mbIllegalInsnsEnabled || !Decode6502Ill(c))
					*mpDstState++ = kStateBreakOnUnsupportedOpcode;
			}
		}

		if (mpHeatMap)
			*mpDstState++ = kStateUpdateHeatMap;

		*mpDstState++ = kStateReadOpcode;
	}

	// predecode NMI sequence
	mpDecodePtrNMI = mpDstState;
	*mpDstState++ = kStateReadDummyOpcode;
	*mpDstState++ = kStateReadDummyOpcode;

	if (mbStepOver)
		*mpDstState++ = kStateStepOver;

	*mpDstState++ = kStatePushPCH;
	*mpDstState++ = kStatePushPCL;
	*mpDstState++ = kStatePtoD_B0;
	*mpDstState++ = kStatePush;

	if (mpVerifier)
		*mpDstState++ = kStateVerifyNMIEntry;

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

	if (mbStepOver)
		*mpDstState++ = kStateStepOver;

	*mpDstState++ = kStatePushPCH;
	*mpDstState++ = kStatePushPCL;
	*mpDstState++ = kStatePtoD_B0;

	if (mCPUMode == kATCPUMode_6502 && mbAllowBlockedNMIs)
		*mpDstState++ = kStateCheckNMIBlocked;

	*mpDstState++ = kStatePush;

	if (mpVerifier)
		*mpDstState++ = kStateVerifyIRQEntry;

	*mpDstState++ = kStateSEI;

	if (mCPUMode == kATCPUMode_6502 && mbAllowBlockedNMIs)
		*mpDstState++ = kStateNMIOrIRQVecToPCBlockable;
	else
		*mpDstState++ = kStateNMIOrIRQVecToPC;

	*mpDstState++ = kStateReadAddrL;
	*mpDstState++ = kStateDelayInterrupts;
	*mpDstState++ = kStateReadAddrH;
	*mpDstState++ = kStateAddrToPC;

	if (mbPathfindingEnabled)
		*mpDstState++ = kStateAddAsPathStart;

	*mpDstState++ = kStateReadOpcode;
}

void ATCPUEmulator::RebuildDecodeTables65816() {
	mpDstState = mDecodeHeap;

	static const struct ModeInfo {
		bool mbUnalignedDP;
		bool mbEmulationMode;
		bool mbMode16;
		bool mbIndex16;
	} kModeInfo[10]={
		{ false, true,  false, false },
		{ false, false, true,  true  },
		{ false, false, true,  false },
		{ false, false, false, true  },
		{ false, false, false, false },

		{ true,  true,  false, false },
		{ true,  false, true,  true  },
		{ true,  false, true,  false },
		{ true,  false, false, true  },
		{ true,  false, false, false },
	};

	for(int i=0; i<10; ++i) {
		const bool unalignedDP = kModeInfo[i].mbUnalignedDP;
		const bool emulationMode = kModeInfo[i].mbEmulationMode;
		const bool mode16 = kModeInfo[i].mbMode16;
		const bool index16 = kModeInfo[i].mbIndex16;

		for(int j=0; j<256; ++j) {
			mDecodePtrs816[i][j] = mpDstState - mDecodeHeap;

			if (mbPathfindingEnabled)
				*mpDstState++ = kStateAddToPath;

			uint8 c = (uint8)j;

			if (!Decode65C816(c, unalignedDP, emulationMode, mode16, index16))
				*mpDstState++ = kStateBreakOnUnsupportedOpcode;

			if (mpHeatMap)
				*mpDstState++ = kStateUpdateHeatMap;

			*mpDstState++ = kStateReadOpcode;
		}

		// predecode NMI sequence
		mDecodePtrs816[i][256] = mpDstState - mDecodeHeap;
		*mpDstState++ = kStateReadDummyOpcode;
		*mpDstState++ = kStateReadDummyOpcode;

		if (mbStepOver)
			*mpDstState++ = kStateStepOver;

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

		if (mpVerifier)
			*mpDstState++ = kStateVerifyNMIEntry;

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
		mDecodePtrs816[i][257] = mpDstState - mDecodeHeap;
		*mpDstState++ = kStateReadDummyOpcode;
		*mpDstState++ = kStateReadDummyOpcode;

		if (mbStepOver)
			*mpDstState++ = kStateStepOver;

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

		if (mpVerifier)
			*mpDstState++ = kStateVerifyIRQEntry;

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
	}
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

	if (mbHistoryEnabled)
		*mpDstState++ = kStateAddEAToHistory;
}

void ATCPUEmulator::DecodeReadIndY() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateRead;				// 3
	*mpDstState++ = kStateReadIndYAddr;		// 4
	*mpDstState++ = kStateReadCarry;		// 5
	*mpDstState++ = kStateRead;				// (6)

	if (mbHistoryEnabled)
		*mpDstState++ = kStateAddEAToHistory;
}

void ATCPUEmulator::DecodeReadInd() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateRead;				// 3
	*mpDstState++ = kStateReadIndAddr;		// 4
	*mpDstState++ = kStateRead;				// 5

	if (mbHistoryEnabled)
		*mpDstState++ = kStateAddEAToHistory;
}
