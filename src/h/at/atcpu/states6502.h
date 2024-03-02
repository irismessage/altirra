//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2015 Avery Lee
//	CPU emulation library - state definitions
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

#ifndef f_AT_ATCPU_STATES6502_H
#define f_AT_ATCPU_STATES6502_H

#include <vd2/system/vdtypes.h>

namespace ATCPUStates6502 {
	enum ATCPUState6502 : uint8 {
		kStateNop,
		kStateReadOpcode,
		kStateReadOpcodeNoBreak,
		kStateAddToHistory,
		kStateAddEAToHistory,
		kStateReadDummyOpcode,
		kStateBreakOnUnsupportedOpcode,
		kStateReadImm,
		kStateReadAddrL,
		kStateReadAddrH,
		kStateReadAddrHX,
		kStateReadAddrHY,
		kStateReadAddrHX_SHY,
		kStateReadAddrHY_SHA,
		kStateReadAddrHY_SHX,
		kStateRead,
		kStateReadSetSZToA,
		kStateReadAddX,
		kStateReadAddY,
		kStateReadCarry,
		kStateReadCarryForced,
		kStateReadAbsIndAddr,
		kStateReadAbsIndAddrBroken,
		kStateReadIndAddr,					// Read high byte of indirect address into address register, wrapped in zero page.
		kStateReadIndYAddr,
		kStateReadIndYAddr_SHA,
		kStateWrite,
		kStateWriteA,
		kStateWait,
		kStateAtoD,
		kStateXtoD,
		kStateYtoD,
		kStateStoD,
		kStatePtoD,
		kStatePtoD_B0,
		kStatePtoD_B1,
		kState0toD,
		kStateDtoA,
		kStateDtoX,
		kStateDtoY,
		kStateDtoS,
		kStateDtoP,
		kStateDtoP_noICheck,
		kStateDSetSZ,
		kStateDSetSZToA,
		kStateDSetSV,
		kStateAddrToPC,
		kStateNMIVecToPC,
		kStateIRQVecToPC,
		kStateNMIOrIRQVecToPC,
		kStatePush,
		kStatePushPCL,
		kStatePushPCH,
		kStatePushPCLM1,
		kStatePushPCHM1,
		kStatePop,
		kStatePopPCL,
		kStatePopPCH,
		kStatePopPCHP1,
		kStateAdc,
		kStateSbc,
		kStateCmp,
		kStateCmpX,
		kStateCmpY,
		kStateInc,
		kStateIncXWait,
		kStateDec,
		kStateDecXWait,
		kStateDecC,
		kStateAnd,
		kStateAnd_SAX,
		kStateAnc,
		kStateXaa,
		kStateLas,
		kStateSbx,
		kStateArr,
		kStateXas,
		kStateOr,
		kStateXor,
		kStateAsl,
		kStateLsr,
		kStateRol,
		kStateRor,
		kStateBit,
		kStateSEI,
		kStateCLI,
		kStateSEC,
		kStateCLC,
		kStateSED,
		kStateCLD,
		kStateCLV,
		kStateJs,
		kStateJns,
		kStateJc,
		kStateJnc,
		kStateJz,
		kStateJnz,
		kStateJo,
		kStateJno,
		kStateJccFalseRead,
		kStateStepOver,
		kStateRegenerateDecodeTables,

		// === 6502 trace cache states ===

		// Align microcode IP to next cache line boundary (used to prevent
		// DCU splits). This is only required when a uop would overlap a
		// cache line. Note that this cannot have args so that it always
		// fits.
		kStateTraceBridge,

		// Addr -> PC, check for transition to cached trace
		kStateTraceAddrToPC,

		// check for transition to cached trace at PC
		kStateTracePC,

		// Traced instruction start. Must be followed by N-1 stubs for an
		// insn of N bytes to cover the prefetch in case a VM
		// exit+resume occurs mid instruction. Prefetch cycles include
		// instruction and data fetch cycles that we can assume to have no
		// no side effects and that we do not need the data for.
		//
		// Args:
		//	- Len.B: Additional prefetch byte/cycle count, not including opcode
		//	- Addr.W: Address latch preload
		//	- Data.B: Data latch preload
		//	- Opcode.B: Instruction opcode byte
		//
		// Prefetch breakdowns for various patterns:
		//	2 cycles	implied #imm rel8
		//	2 cycles	zp zp,X zp,Y
		//	3 cycles	abs abs,X abs,Y (abs)
		//	2 cycles	(zp,X) (zp),Y
		//	3 cycles	zp,X zp,Y with fast ZP
		//
		kStateTraceStartInsn,

		// Variant of TraceStartInsn that also adds a history entry.
		// Args:
		//	- Len.B: Additional prefetch byte/cycle count, not including opcode
		//	- Addr.W: Address latch preload
		//	- Data.B: Data latch preload
		//	- Opcode.B[3]: Instruction opcode bytes
		kStateTraceStartInsnWithHistory,

		// Continuation of interrupted instruction, three prefetch cycles left.
		kStateTraceContInsn3,

		// Continuation of interrupted instruction, two prefetch cycles left.
		kStateTraceContInsn2,

		// Continuation of interrupted instruction, one prefetch cycle left.
		kStateTraceContInsn1,

		kStateTraceAddrAddX,
		kStateTraceAddrAddY,
		kStateTraceAddrHX_SHY,
		kStateTraceAddrHY_SHA,
		kStateTraceAddrHY_SHX,

		// Version of Jcc opcode for use in traces (not to _enter_ traces).
		// Args:
		//	- Xor.B, And.B: Xor/And masks for P check (branch if P^X&A != 0).
		//	- Skip.B: 2 if no page crossing, 3 if. Used to track presence of
		//	          additional uop for page crossing cycle if taken.
		kStateTraceFastJcc,

		// Version of Jcc opcode for entering traces.
		// Args:
		//	- Xor.B, And.B: Xor/And masks for P check (branch if P^X&A != 0).
		kStateTraceJcc,

		// Jump to another microcode location in the trace cache. Note that
		// the offset is from the start of the offset and not the end.
		// Args:
		//	- Offset.L: Signed 32-bit offset.
		kStateTraceUJump,

		// 65C02 states
		kStateResetBit,
		kStateSetBit,
		kStateReadRel,
		kStateJ0,
		kStateJ1,
		kStateJ,
		kStateWaitForInterrupt,
		kStateStop,
		kStateTrb,
		kStateTsb,		// also used by BIT #imm
		kStateC02_Adc,
		kStateC02_Sbc,

		kStateCount
	};
}

#endif
