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
	mCPUSubMode = kATCPUSubMode_6502;

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

	VDASSERTCT(kStateStandard_Count <= kStateVerifyJump);
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

	if (mpVerifier)
		mpVerifier->OnReset();
}

bool ATCPUEmulator::IsInstructionInProgress() const {
	uint8 state = *mpNextState;

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
			case kStateRead816AddrAbsHY:
			case kStateRead816AddrAbsLongL:
			case kStateRead816AddrAbsLongH:
			case kStateRead816AddrAbsLongB:
			case kState816ReadAddrHX:
			case kState816ReadAddrHY:
			case kStateReadAddrB:
			case kStateReadAddrBX:
			case kStateReadAddrSO:
			case kState816ReadByte:
			case kStateReadL16:
			case kStateReadH16:
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

	mP = p;
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

uint8 ATCPUEmulator::GetHeldCycleValue() {
	if (mCPUMode == kATCPUMode_65C816)
		return mpMemory->ExtReadByte(mPC, mK);
	else
		return mpMemory->ReadByte(mPC);
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
			if (!(mP & kFlagX)) {
				ATConsoleTaggedPrintf("PC=%02X:%04X A=%02X%s%02X X=%02X%02X Y=%02X%02X S=%02X%02X P=%02X (%c%c%c %c%c%c%c)  "
					, mK
					, mInsnPC
					, mAH
					, (mP & kFlagM) ? ":" : ""
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
				ATConsoleTaggedPrintf("PC=%02X:%04X A=%02X%s%02X X=%02X Y=%02X S=%02X%02X P=%02X (%c%c%cX%c%c%c%c)  "
					, mK
					, mInsnPC
					, mAH
					, (mP & kFlagM) ? ":" : ""
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

int ATCPUEmulator::Advance() {
	if (mCPUMode == kATCPUMode_65C816)
		return Advance65816();
	else
		return Advance6502();
}

int ATCPUEmulator::AdvanceWithBusTracking() {
	if (mCPUMode == kATCPUMode_65C816)
		return Advance65816WithBusTracking();
	else
		return Advance6502WithBusTracking();
}

int ATCPUEmulator::Advance6502() {
	#include "cpumachine.inl"
}

int ATCPUEmulator::Advance65816() {
#define AT_CPU_MACHINE_65C816
	#include "cpumachine.inl"
#undef AT_CPU_MACHINE_65C816
}

#define AT_CPU_RECORD_BUS_ACTIVITY
int ATCPUEmulator::Advance6502WithBusTracking() {
	#include "cpumachine.inl"
}

int ATCPUEmulator::Advance65816WithBusTracking() {
#define AT_CPU_MACHINE_65C816
	#include "cpumachine.inl"
#undef AT_CPU_MACHINE_65C816
}
#undef AT_CPU_RECORD_BUS_ACTIVITY

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

			if (mpStepCallback && mpStepCallback(this, mPC, mpStepCallbackData)) {
				mStepStackLevel = mS;
			} else {
				mbStep = false;
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

bool ATCPUEmulator::ProcessInterrupts() {
	if (mIntFlags & kIntFlag_NMIPending) {
		uint32 t = mpCallbacks->CPUGetUnhaltedCycle();

		if (t - mNMIAssertTime >= (t == mNMIIgnoreUnhaltedCycle ? 3U : 2U)) {
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
	} else if ((mIntFlags & (kIntFlag_IRQActive | kIntFlag_IRQPending)) && (!(mP & kFlagI) || mIFlagSetCycle == mpCallbacks->CPUGetUnhaltedCycle() - 1)) {
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
			if ((mIntFlags & kIntFlag_IRQPending) && (mpCallbacks->CPUGetUnhaltedCycle() - mIRQAcknowledgeTime > 0)) {
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

void ATCPUEmulator::AddHistoryEntry(bool is816) {
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
		mDecodePtrs[i] = mpDstState - mDecodeHeap;

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

		if (mpHeatMap)
			*mpDstState++ = kStateUpdateHeatMap;

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
	} else {
		// predecode NMI sequence
		mpDecodePtrNMI = mpDstState;
		*mpDstState++ = kStateReadDummyOpcode;
		*mpDstState++ = kStateReadDummyOpcode;
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
		*mpDstState++ = kStatePushPCH;
		*mpDstState++ = kStatePushPCL;
		*mpDstState++ = kStatePtoD_B0;
		*mpDstState++ = kStatePush;

		if (mpVerifier)
			*mpDstState++ = kStateVerifyIRQEntry;

		*mpDstState++ = kStateSEI;

		if (mCPUMode == kATCPUMode_6502 && mbAllowBlockedNMIs)
			*mpDstState++ = kStateIRQVecToPCBlockNMIs;
		else
			*mpDstState++ = kStateIRQVecToPC;

		*mpDstState++ = kStateReadAddrL;
		*mpDstState++ = kStateDelayInterrupts;
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
