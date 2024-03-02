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

#ifndef AT_CPU_H
#define AT_CPU_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vdtypes.h>

class ATCPUEmulator;
class ATSaveStateReader;
class ATSaveStateWriter;

enum ATCPUMode {
	kATCPUMode_6502,
	kATCPUMode_65C02,
	kATCPUMode_65C816,
	kATCPUModeCount
};

enum ATCPUSubMode {
	kATCPUSubMode_6502,
	kATCPUSubMode_65C02,
	kATCPUSubMode_65C816_Emulation,
	kATCPUSubMode_65C816_NativeM16X16,
	kATCPUSubMode_65C816_NativeM16X8,
	kATCPUSubMode_65C816_NativeM8X16,
	kATCPUSubMode_65C816_NativeM8X8,
	kATCPUSubModeCount
};

namespace AT6502 {
	enum {
		kFlagN = 0x80,
		kFlagV = 0x40,
		kFlagM = 0x20,		// 65C816 native mode only
		kFlagX = 0x10,		// 65C816 native mode only
		kFlagB = 0x10,
		kFlagD = 0x08,
		kFlagI = 0x04,
		kFlagZ = 0x02,
		kFlagC = 0x01
	};
}

class VDINTERFACE ATCPUEmulatorMemory {
public:
	virtual uint8 CPUReadByte(uint16 address) = 0;
	virtual uint8 CPUDebugReadByte(uint16 address) = 0;
	virtual void CPUWriteByte(uint16 address, uint8 value) = 0;
	virtual uint32 GetTimestamp() = 0;
	virtual uint8 CPUHookHit(uint16 address) = 0;

	const uint8 *const *mpCPUReadPageMap;
	uint8 *const *mpCPUWritePageMap;

	uint8 ReadByte(uint16 address) {
		const uint8 *readPage = mpCPUReadPageMap[address >> 8];
		return readPage ? readPage[address & 0xff] : CPUReadByte(address);
	}

	uint8 ReadByteHL(uint8 addrH, uint8 addrL) {
		const uint8 *readPage = mpCPUReadPageMap[addrH];
		return readPage ? readPage[addrL] : CPUReadByte(((uint32)addrH << 8) + addrL);
	}

	uint8 ExtReadByte(uint16 address, uint8 bank) {
		if (bank)
			return 0xFF;

		const uint8 *readPage = mpCPUReadPageMap[address >> 8];
		return readPage ? readPage[address & 0xff] : CPUReadByte(address);
	}
	
	void WriteByte(uint16 address, uint8 value) {
		uint8 *writePage = mpCPUWritePageMap[address >> 8];
		if (writePage)
			writePage[address & 0xff] = value;
		else
			CPUWriteByte(address, value);
	}

	void WriteByteHL(uint8 addrH, uint8 addrL, uint8 value) {
		uint8 *writePage = mpCPUWritePageMap[addrH];
		if (writePage)
			writePage[addrL] = value;
		else
			CPUWriteByte(((uint32)addrH << 8) + addrL, value);
	}

	void ExtWriteByte(uint16 address, uint8 bank, uint8 value) {
		if (!bank) {
			uint8 *writePage = mpCPUWritePageMap[address >> 8];
			if (writePage)
				writePage[address & 0xff] = value;
			else
				CPUWriteByte(address, value);
		}
	}
};

class VDINTERFACE IATCPUHighLevelEmulator {
public:
	virtual int InvokeHLE(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem, uint16 pc, uint32 code) = 0;
};

struct ATCPUHistoryEntry {
	uint32	mTimestamp;
	uint16	mPC;
	uint8	mS;
	uint8	mP;
	uint8	mA;
	uint8	mX;
	uint8	mY;
	bool	mbIRQ;
	bool	mbNMI;
	bool	mbEmulation;
	uint8	mOpcode[4];
	uint8	mSH;
	uint8	mAH;
	uint8	mXH;
	uint8	mYH;
	uint8	mB;
	uint8	mK;
	uint16	mD;
};

class ATCPUEmulator {
public:
	ATCPUEmulator();
	~ATCPUEmulator();

	bool	Init(ATCPUEmulatorMemory *mem);

	void	SetHLE(IATCPUHighLevelEmulator *hle);

	void	ColdReset();
	void	WarmReset();

	bool	GetUnusedCycle() {
		bool b = mbUnusedCycle;
		mbUnusedCycle = false;
		return b;
	}

	bool	IsInstructionInProgress() const;

	bool	GetEmulationFlag() const { return mbEmulationFlag; }
	uint16	GetInsnPC() const { return mInsnPC; }
	uint16	GetPC() const { return mPC; }
	uint8	GetP() const { return mP; }
	uint8	GetS() const { return mS; }
	uint16	GetS16() const { return mS + ((uint32)mSH << 8); }
	uint8	GetA() const { return mA; }
	uint8	GetX() const { return mX; }
	uint8	GetY() const { return mY; }

	void	SetPC(uint16 pc);
	void	SetP(uint8 p) { mP = p; }
	void	SetA(uint8 a) { mA = a; }
	void	SetX(uint8 x) { mX = x; }
	void	SetY(uint8 y) { mY = y; }
	void	SetS(uint8 s) { mS = s; }

	uint8	GetAH() const { return mAH; }
	uint8	GetXH() const { return mXH; }
	uint8	GetYH() const { return mYH; }
	uint8	GetSH() const { return mSH; }
	uint8	GetB() const { return mB; }
	uint8	GetK() const { return mK; }
	uint16	GetD() const { return mDP; }

	void	SetFlagC() { mP |= AT6502::kFlagC; }
	void	ClearFlagC() { mP &= ~AT6502::kFlagC; }

	void	SetHook(uint16 pc, bool enable);
	void	SetStep(bool step) { mbStep = step; }
	void	SetTrace(bool trace) { mbTrace = trace; }
	void	SetRTSBreak() { mSBrk = 0x100; }
	void	SetRTSBreak(uint8 sp) { mSBrk = sp; }

	void	SetCPUMode(ATCPUMode mode);
	ATCPUMode GetCPUMode() const { return mCPUMode; }
	ATCPUSubMode GetCPUSubMode() const { return mCPUSubMode; }

	bool	IsHistoryEnabled() const { return mbHistoryEnabled; }
	void	SetHistoryEnabled(bool enable);

	bool	IsPathfindingEnabled() const { return mbPathfindingEnabled; }
	void	SetPathfindingEnabled(bool enable);

	bool	AreIllegalInsnsEnabled() const { return mbIllegalInsnsEnabled; }
	void	SetIllegalInsnsEnabled(bool enable);

	bool	GetStopOnBRK() const { return mbStopOnBRK; }
	void	SetStopOnBRK(bool en);

	bool	IsBreakpointSet(uint16 addr) const;
	sint32	GetNextBreakpoint(sint32 last) const;
	void	SetBreakpoint(uint16 addr);
	void	SetOneShotBreakpoint(uint16 addr);
	void	ClearBreakpoint(uint16 addr);
	void	ClearAllBreakpoints();

	void	ResetAllPaths();
	sint32	GetNextPathInstruction(sint32 addr) const;
	bool	IsPathStart(uint16 addr) const;
	bool	IsInPath(uint16 addr) const;

	const ATCPUHistoryEntry& GetHistory(int i) const {
		return mHistory[(mHistoryIndex - i - 1) & 131071];
	}

	int GetHistoryLength() const { return 131072; }
	uint32	GetHistoryCounter() const { return mHistoryIndex; }

	void	DumpStatus();

	void	LoadState(ATSaveStateReader& reader);
	void	SaveState(ATSaveStateWriter& writer);

	void	SetHLEDelay(int delay) { mHLEDelay = delay; }
	void	InjectOpcode(uint8 op);
	void	Push(uint8 v);
	uint8	Pop();
	void	Jump(uint16 addr);

	void	AssertIRQ();
	void	NegateIRQ();
	void	AssertNMI();
	void	NegateNMI();
	int		Advance(bool busLocked);

protected:
	void	RedecodeInsnWithoutBreak();
	void	Update65816DecodeTable();
	void	RebuildDecodeTables();
	bool	Decode6502(uint8 opcode);
	bool	Decode6502Ill(uint8 opcode);
	bool	Decode65C02(uint8 opcode);
	bool	Decode65C816(uint8 opcode, bool unalignedDP, bool emu, bool mode16, bool index16);
	void	DecodeReadImm();
	void	DecodeReadZp();
	void	DecodeReadZpX();
	void	DecodeReadZpY();
	void	DecodeReadAbs();
	void	DecodeReadAbsX();
	void	DecodeReadAbsY();
	void	DecodeReadIndX();
	void	DecodeReadIndY();
	void	DecodeReadInd();

	void	Decode65816AddrDp(bool unalignedDP);
	void	Decode65816AddrDpX(bool unalignedDP);
	void	Decode65816AddrDpY(bool unalignedDP);
	void	Decode65816AddrDpInd(bool unalignedDP);
	void	Decode65816AddrDpIndX(bool unalignedDP);
	void	Decode65816AddrDpIndY(bool unalignedDP);
	void	Decode65816AddrDpLongInd(bool unalignedDP);
	void	Decode65816AddrDpLongIndY(bool unalignedDP);
	void	Decode65816AddrAbs();
	void	Decode65816AddrAbsX();
	void	Decode65816AddrAbsY();
	void	Decode65816AddrAbsLong();
	void	Decode65816AddrAbsLongX();
	void	Decode65816AddrStackRel();
	void	Decode65816AddrStackRelInd();

	uint8	mA;
	uint8	mX;
	uint8	mY;
	uint8	mS;
	uint8	mP;
	uint16	mInsnPC;
	uint16	mPC;
	uint16	mAddr;
	uint16	mAddr2;
	sint16	mRelOffset;
	uint8	mData;
	uint8	mOpcode;
	uint16	mData16;
	uint8	mAddrBank;

	uint8	mB;		// data bank register
	uint8	mK;		// program bank register
	uint8	mAH;
	uint8	mXH;
	uint8	mYH;
	uint8	mSH;
	uint16	mDP;

	bool	mbIRQReleasePending;
	bool	mbIRQPending;
	bool	mbNMIPending;
	bool	mbTrace;
	bool	mbStep;
	bool	mbUnusedCycle;
	bool	mbEmulationFlag;
	uint32	mSBrk;
	ATCPUMode	mCPUMode;
	ATCPUSubMode	mCPUSubMode;

	ATCPUEmulatorMemory	*mpMemory;
	IATCPUHighLevelEmulator	*mpHLE;
	uint32	mHLEDelay;

	const uint8 *mpNextState;
	uint8 *mpDstState;
	uint8	mStates[16];

	bool	mbHistoryEnabled;
	bool	mbPathfindingEnabled;
	bool	mbIllegalInsnsEnabled;
	bool	mbStopOnBRK;
	bool	mbMarkHistoryIRQ;
	bool	mbMarkHistoryNMI;

	enum {
		kInsnFlagBreakPt		= 0x01,
		kInsnFlagOneShotBreakPt	= 0x02,
		kInsnFlagSpecialTracePt	= 0x04,
		kInsnFlagHook			= 0x08,
		kInsnFlagPathStart		= 0x10,
		kInsnFlagPathExecuted	= 0x20
	};

	const uint8 *mpDecodePtrIRQ;
	const uint8 *mpDecodePtrNMI;
	const uint8 *mpDecodePtrs[256];
	uint8	mDecodeHeap[4096];
	uint8	mInsnFlags[65536];

	typedef ATCPUHistoryEntry HistoryEntry;
	HistoryEntry mHistory[131072];
	int mHistoryIndex;
};

#endif
