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

#ifndef f_AT_CPUSTATES_H
#define f_AT_CPUSTATES_H

namespace AT6502States {
	enum ATCPUState {
		kStateNop,
		kStateReadOpcode,
		kStateReadOpcodeNoBreak,
		kStateAddToHistory,
		kStateReadDummyOpcode,
		kStateAddAsPathStart,
		kStateAddToPath,
		kStateBreakOnUnsupportedOpcode,
		kStateReadImm,
		kStateReadAddrL,
		kStateReadAddrH,
		kStateReadAddrHX,
		kStateReadAddrHY,
		kStateReadAddrHX_SHY,
		kStateRead,
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
		kStateWriteZpX,
		kStateWriteZpY,
		kStateWriteAbsX,
		kStateWriteAbsY,
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
		kStateDSetSZ,
		kStateDSetSV,
		kStateAddrToPC,
		kStateNMIVecToPC,
		kStateIRQVecToPC,
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
		kStateDec,
		kStateDecC,
		kStateAnd,
		kStateAnc,
		kStateXaa,
		kStateLas,
		kStateSbx,
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
		kStateJsAddToPath,
		kStateJnsAddToPath,
		kStateJcAddToPath,
		kStateJncAddToPath,
		kStateJzAddToPath,
		kStateJnzAddToPath,
		kStateJoAddToPath,
		kStateJnoAddToPath,
		kStateJccFalseRead,
		kStateInvokeHLE,
		kStateHLEDelay,

		// 65C02 states
		kStateResetBit,
		kStateSetBit,
		kStateReadRel,
		kStateJ0,
		kStateJ1,
		kStateJ0AddToPath,
		kStateJ1AddToPath,
		kStateJ,
		kStateJAddToPath,
		kStateWaitForInterrupt,
		kStateStop,
		kStateTrb,
		kStateTsb,
		kStateC02_Adc,
		kStateC02_Sbc,

		// 65C816 states
		kStateReadImmL16,				// Read 16-bit immediate, low byte
		kStateReadImmH16,				// Read 16-bit immediate, high byte
		kStateReadAddrDp,				// Read direct page offset to address register
		kStateReadAddrDpX,				// Read direct page offset to address register and add X16
		kStateReadAddrDpY,				// Read direct page offset to address register and add Y16
		kStateReadIndAddrDp,			// Read high byte of indirect address from direct page
		kStateReadIndAddrDpY,			// Read high byte of indirect address from direct page and add Y16
		kStateReadIndAddrDpLongH,		// Read high byte of indirect long address from direct page
		kStateReadIndAddrDpLongB,		// Read bank byte of indirect long address from direct page
		kStateReadAddrAddY,				// Add Y16 to address register
		kState816ReadAddrL,				// Read low byte of absolute address and push data bank
		kStateRead816AddrAbsHY,			// Read high byte of absolute address from data bank, add Y16
		kStateRead816AddrAbsLongL,		// Read low byte of long address from data bank
		kStateRead816AddrAbsLongH,		// Read high byte of long address from data bank
		kStateRead816AddrAbsLongB,		// Read bank byte of long address from data bank
		kState816ReadAddrHX,
		kState816ReadAddrHY,
		kStateReadAddrB,				// Read bank byte of long absolute address
		kStateReadAddrBX,				// Read bank byte of long absolute address and add X16
		kStateReadAddrSO,				// Read stack offset and compute EA
		kStateBtoD,
		kStateKtoD,
		kState0toD16,
		kStateAtoD16,
		kStateXtoD16,
		kStateYtoD16,
		kStateStoD16,
		kStateDPtoD16,
		kStateDtoB,
		kStateDtoK,
		kStateDtoPNative,
		kStateDtoA16,
		kStateDtoX16,
		kStateDtoY16,
		kStateDtoS16,
		kStateDtoDP16,
		kStateDSetSZ16,
		kStateDSetSV16,
		kState816WriteByte,
		kStateWriteL16,
		kStateWriteH16,
		kState816ReadByte,
		kState816ReadByte_PBK,
		kStateReadL16,
		kStateReadH16,
		kStateOr16,
		kStateAnd16,
		kStateXor16,
		kStateAdc16,
		kStateCmp16,
		kStateSbc16,
		kStateInc16,
		kStateDec16,
		kStateRol16,
		kStateRor16,
		kStateAsl16,
		kStateLsr16,
		kStateBit16,
		kStateTrb16,
		kStateTsb16,
		kStateCmpX16,
		kStateCmpY16,
		kStateXba,
		kStateXce,
		kStatePushNative,
		kStatePushL16,
		kStatePushH16,
		kStatePushPBKNative,
		kStatePushPCLNative,
		kStatePushPCHNative,
		kStatePushPCLM1Native,
		kStatePushPCHM1Native,
		kStatePopNative,
		kStatePopL16,
		kStatePopH16,
		kStatePopPBKNative,
		kStatePopPCLNative,
		kStatePopPCHNative,
		kStatePopPCHP1Native,
		kStateSep,
		kStateRep,
		kStateJ16,
		kStateJ16AddToPath,
		kState816_NatCOPVecToPC,
		kState816_EmuCOPVecToPC,
		kState816_NatNMIVecToPC,
		kState816_NatIRQVecToPC,
		kState816_NatBRKVecToPC,
		kState816_SetI_ClearD,
		kState816_LongAddrToPC,
		kState816_MoveRead,
		kState816_MoveWriteP,
		kState816_MoveWriteN,
		kState816_Per,

		kStateVerifyJump	= 0xF8,

		kStateCount
	};
}

using namespace AT6502States;

#endif
