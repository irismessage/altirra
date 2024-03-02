//	Altirra - Atari 800/800XL/5200 emulator
//	Coprocessor library - 65802 emulator
//	Copyright (C) 2009-2015 Avery Lee
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

#ifndef f_ATCOPROC_CO65802_H
#define f_ATCOPROC_CO65802_H

#include <at/atcpu/decode65816.h>

struct ATCPUExecState;
struct ATCPUHistoryEntry;

struct ATCoProcReadMemNode {
	uint8 (*mpRead)(uint32 addr, void *thisptr);
	uint8 (*mpDebugRead)(uint32 addr, void *thisptr);
	void *mpThis;

	uintptr AsBase() const {
		return (uintptr)this + 1;
	}
};

struct ATCoProcWriteMemNode {
	void (*mpWrite)(uint32 addr, uint8 val, void *thisptr);
	void *mpThis;

	uintptr AsBase() const {
		return (uintptr)this + 1;
	}
};

class ATCoProc65802 final {
public:
	ATCoProc65802();

	uintptr *GetReadMap() { return mReadMap; }
	uintptr *GetWriteMap() { return mWriteMap; }

	void SetHistoryBuffer(ATCPUHistoryEntry buffer[131072]);
	uint32 GetHistoryCounter() const { return mHistoryIndex; }
	uint32 GetTimeBase() const { return mCyclesBase; }

	void GetExecState(ATCPUExecState& state) const;
	void SetExecState(const ATCPUExecState& state);

	void ColdReset();
	void WarmReset();

	void AddCycles(sint32 cycles) { mCyclesLeft += cycles; }
	void Run();

private:
	inline uint8 DebugReadByteSlow(uintptr base, uint32 addr);
	inline uint8 ReadByteSlow(uintptr base, uint32 addr);
	inline void WriteByteSlow(uintptr base, uint32 addr, uint8 value);
	void UpdateDecodeTable();
	const uint8 *RegenerateDecodeTables();

	enum SubMode {
		kSubMode_Emulation,
		kSubMode_NativeM16X16,
		kSubMode_NativeM16X8,
		kSubMode_NativeM8X16,
		kSubMode_NativeM8X8,
		kSubModeCount
	};

	SubMode		mSubMode;

	uint8		mA;
	uint8		mAH;
	uint8		mP;
	bool		mbEmulationFlag;
	uint8		mX;
	uint8		mXH;
	uint8		mY;
	uint8		mYH;
	uint8		mS;
	uint8		mSH;
	uint16		mDP;
	uint8		mB;
	uint8		mK;
	uint8		mData;
	uint16		mData16;
	uint16		mAddr;
	uint16		mAddr2;
	uint8		mAddrBank;
	uint16		mPC;
	uint16		mInsnPC;
	sint32		mCyclesLeft;
	uint32		mCyclesBase;
	const uint8	*mpNextState;
	const uint16 *mpDecodePtrs;
	ATCPUHistoryEntry *mpHistory;
	uint32		mHistoryIndex;

	uintptr		mReadMap[256];
	uintptr		mWriteMap[256];

	ATCPUDecoderTables65816 mDecoderTables;

	static const uint8 kInitialState;
};

#endif	// f_ATCOPROC_CO65802_H
