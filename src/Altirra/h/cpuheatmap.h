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

#ifndef f_AT_CPUHEATMAP_H
#define f_AT_CPUHEATMAP_H

#include <vd2/system/vdtypes.h>

class ATCPUEmulator;

class ATCPUHeatMap {
public:
	enum {
		kTypeUnknown	= 0x00000,
		kTypePreset		= 0x10000,
		kTypeImm		= 0x20000,
		kTypeComputed	= 0x30000,
		kTypeMask		= 0xFFFF0000
	};

	enum {
		kAccessRead		= 0x01,
		kAccessWrite	= 0x02
	};

	ATCPUHeatMap();
	~ATCPUHeatMap();

	uint32 GetAStatus() const { return mA; }
	uint32 GetXStatus() const { return mX; }
	uint32 GetYStatus() const { return mY; }

	uint32 GetMemoryStatus(uint16 addr) const { return mMemory[addr]; }
	uint8 GetMemoryAccesses(uint16 addr) const { return mMemAccess[addr]; }

	void Reset();

	VDNOINLINE void ProcessInsn(const ATCPUEmulator& cpu, uint8 opcode, uint16 addr, uint16 pc);

protected:
	uint32	mA;
	uint32	mX;
	uint32	mY;
	uint32	mMemory[0x10000];
	uint8	mMemAccess[0x10000];
};

#endif	// f_AT_CPUHEATMAP_H
