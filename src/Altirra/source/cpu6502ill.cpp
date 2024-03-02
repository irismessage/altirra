//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2009 Avery Lee
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

bool ATCPUEmulator::Decode6502Ill(uint8 opcode) {
	switch(opcode) {
		case 0x03:	// SLO (zp,X)
			DecodeReadIndX();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateAsl;
			*mpDstState++ = kStateOr;
			*mpDstState++ = kStateWrite;
			break;

		case 0x04:	// NOP zp
			DecodeReadZp();
			*mpDstState++ = kStateWait;
			break;

		case 0x07:	// SLO zp
			DecodeReadZp();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateAsl;
			*mpDstState++ = kStateOr;
			*mpDstState++ = kStateWrite;
			break;

		case 0x0B:	// AAC imm
			*mpDstState++ = kStateReadImm;
			*mpDstState++ = kStateAnc;
			*mpDstState++ = kStateDtoA;
			break;

		case 0x0C:	// NOP abs
			DecodeReadAbs();
			break;

		case 0x0F:	// SLO abs
			DecodeReadAbs();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateAsl;
			*mpDstState++ = kStateOr;
			*mpDstState++ = kStateWrite;
			break;

		case 0x14:	// NOP zp,X
			DecodeReadZpX();
			break;

		case 0x17:	// SLO zp,X
			DecodeReadZpX();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateAsl;
			*mpDstState++ = kStateOr;
			*mpDstState++ = kStateWrite;
			break;

		case 0x1A:	// NOP*
			*mpDstState++ = kStateWait;
			break;

		case 0x1C:	// NOP abs,X
			DecodeReadAbsX();
			break;

		case 0x1F:	// SLO abs,X
			*mpDstState++ = kStateReadAddrL;		// 2
			*mpDstState++ = kStateReadAddrHX;		// 3
			*mpDstState++ = kStateReadCarryForced;	// 4
			*mpDstState++ = kStateRead;				// 5
			*mpDstState++ = kStateWrite;			// 6
			*mpDstState++ = kStateAsl;				//
			*mpDstState++ = kStateOr;
			*mpDstState++ = kStateWrite;			// 7
			break;

		case 0x2B:	// AAC imm
			*mpDstState++ = kStateReadImm;
			*mpDstState++ = kStateAnc;
			*mpDstState++ = kStateDtoA;
			break;

		case 0x37:	// RLA Zp,X
			DecodeReadZpX();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateRol;
			*mpDstState++ = kStateAnd;
			*mpDstState++ = kStateWrite;
			break;

		case 0x3A:	// NOP*
			*mpDstState++ = kStateWait;
			break;

		case 0x3C:	// NOP abs,X
			DecodeReadAbsX();
			break;

		case 0x43:	// SRE (zp,X)
			DecodeReadIndX();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateLsr;
			*mpDstState++ = kStateXor;
			*mpDstState++ = kStateWrite;
			break;

		case 0x44:	// NOP zp
			DecodeReadZp();
			break;

		case 0x47:	// SRE zp
			DecodeReadZp();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateLsr;
			*mpDstState++ = kStateXor;
			*mpDstState++ = kStateWrite;
			break;

		case 0x4B:	// ASR imm
			*mpDstState++ = kStateAtoD;
			*mpDstState++ = kStateAnd;
			*mpDstState++ = kStateLsr;
			*mpDstState++ = kStateDtoA;
			break;

		case 0x4F:	// SRE abs
			DecodeReadAbs();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateLsr;
			*mpDstState++ = kStateXor;
			*mpDstState++ = kStateWrite;
			break;

		case 0x53:	// SRE (zp),Y
			DecodeReadIndY();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateLsr;
			*mpDstState++ = kStateXor;
			*mpDstState++ = kStateWrite;
			break;

		case 0x54:	// NOP zp,X
			DecodeReadZpX();
			break;

		case 0x57:	// SRE zp,X
			DecodeReadZpX();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateLsr;
			*mpDstState++ = kStateXor;
			*mpDstState++ = kStateWrite;
			break;

		case 0x5A:	// NOP*
			*mpDstState++ = kStateWait;
			break;

		case 0x5B:	// SRE abs,Y
			DecodeReadAbsY();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateLsr;
			*mpDstState++ = kStateXor;
			*mpDstState++ = kStateWrite;
			break;

		case 0x5C:	// NOP abs,X
			DecodeReadAbsX();
			break;

		case 0x5F:	// SRE abs,X
			*mpDstState++ = kStateReadAddrL;		// 2
			*mpDstState++ = kStateReadAddrHX;		// 3
			*mpDstState++ = kStateReadCarryForced;	// 4
			*mpDstState++ = kStateRead;				// 5
			*mpDstState++ = kStateWrite;			// 6
			*mpDstState++ = kStateLsr;
			*mpDstState++ = kStateXor;
			*mpDstState++ = kStateWrite;			// 7
			break;

		case 0x7A:	// NOP*
			*mpDstState++ = kStateWait;
			break;

		case 0x7C:	// NOP abs,X
			DecodeReadAbsX();
			break;

		case 0x80:	// NOP
			*mpDstState++ = kStateWait;
			break;

		case 0x8F:	// SAX abs
			*mpDstState++ = kStateReadAddrL;		
			*mpDstState++ = kStateReadAddrH;
			*mpDstState++ = kStateXtoD;
			*mpDstState++ = kStateAnd;
			*mpDstState++ = kStateWrite;
			break;

		case 0xAF:	// LAX abs
			DecodeReadAbs();
			*mpDstState++ = kStateDSetSZ;
			*mpDstState++ = kStateDtoX;
			*mpDstState++ = kStateDtoA;
			break;

		case 0xB3:	// LAX (zp),Y
			DecodeReadIndY();
			*mpDstState++ = kStateDSetSZ;
			*mpDstState++ = kStateDtoX;
			*mpDstState++ = kStateDtoA;
			break;

		case 0xBF:	// LAX abs,X
			DecodeReadAbsX();
			*mpDstState++ = kStateDSetSZ;
			*mpDstState++ = kStateDtoX;
			*mpDstState++ = kStateDtoA;
			break;

		case 0xC3:	// DCP (zp,X)
			DecodeReadIndX();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateDecC;
			*mpDstState++ = kStateWrite;
			break;

		case 0xCF:	// DCP abs
			DecodeReadAbs();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateDecC;
			*mpDstState++ = kStateWrite;
			break;

		case 0xD3:	// DCP (zp),Y
			DecodeReadIndY();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateDecC;
			*mpDstState++ = kStateWrite;
			break;

		case 0xDA:	// NOP*
			*mpDstState++ = kStateWait;
			break;

		case 0xDB:	// DCP abs,Y
			DecodeReadAbsY();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateDecC;
			*mpDstState++ = kStateWrite;
			break;

		case 0xDC:	// NOP abs,X
			DecodeReadAbsX();
			break;

		case 0xDF:	// DCP abs,X
			*mpDstState++ = kStateReadAddrL;		// 2
			*mpDstState++ = kStateReadAddrHX;		// 3
			*mpDstState++ = kStateReadCarryForced;	// 4
			*mpDstState++ = kStateRead;				// 5
			*mpDstState++ = kStateWrite;			// 6
			*mpDstState++ = kStateDecC;				//
			*mpDstState++ = kStateWrite;			// 7
			break;

		case 0xE3:	// ISB zp
			DecodeReadZp();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateInc;
			*mpDstState++ = kStateSbc;
			*mpDstState++ = kStateWrite;
			break;

		case 0xE7:	// ISB zp
			DecodeReadAbsX();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateInc;
			*mpDstState++ = kStateSbc;
			*mpDstState++ = kStateWrite;
			break;

		case 0xEF:	// ISB abs
			DecodeReadAbs();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateInc;
			*mpDstState++ = kStateSbc;
			*mpDstState++ = kStateWrite;
			break;

		case 0xF3:	// ISB zp,X
			DecodeReadZpX();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateInc;
			*mpDstState++ = kStateSbc;
			*mpDstState++ = kStateWrite;
			break;

		case 0xF4:	// NOP zp,X
			DecodeReadZpX();
			break;

		case 0xF7:	// ISB zp,X
			DecodeReadZpX();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateInc;
			*mpDstState++ = kStateSbc;
			*mpDstState++ = kStateWrite;
			break;

		case 0xFA:	// NOP*
			*mpDstState++ = kStateWait;
			break;

		case 0xFB:	// ISB abs,Y
			DecodeReadAbsY();
			*mpDstState++ = kStateWrite;
			*mpDstState++ = kStateInc;
			*mpDstState++ = kStateSbc;
			*mpDstState++ = kStateWrite;
			break;

		case 0xFC:	// NOP abs,X
			DecodeReadAbsX();
			break;

		case 0xFF:	// ISB abs,X
			*mpDstState++ = kStateReadAddrL;		// 2
			*mpDstState++ = kStateReadAddrHX;		// 3
			*mpDstState++ = kStateReadCarryForced;	// 4
			*mpDstState++ = kStateRead;				// 5
			*mpDstState++ = kStateWrite;			// 6
			*mpDstState++ = kStateInc;
			*mpDstState++ = kStateSbc;
			*mpDstState++ = kStateWrite;			// 7
			break;

		default:
			return false;
	}

	return true;
}
