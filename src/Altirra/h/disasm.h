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

#ifndef AT_DISASM_H
#define AT_DISASM_H

class VDStringA;

uint16 ATDisassembleInsn(uint16 addr);
uint16 ATDisassembleInsn(char *buf, uint16 addr, bool decodeReferences);
uint16 ATDisassembleInsn(VDStringA& buf, uint16 addr, bool decodeReferences);
void ATDisassembleRange(FILE *f, uint16 addr1, uint16 addr2);
uint16 ATDisassembleGetFirstAnchor(uint16 addr, uint16 target);

int ATGetOpcodeLength(uint8 opcode);

#endif
