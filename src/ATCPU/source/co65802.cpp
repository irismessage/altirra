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

#include <stdafx.h>
#include <at/atcpu/co65802.h>
#include <at/atcpu/execstate.h>
#include <at/atcpu/history.h>
#include <at/atcpu/states.h>

#define ATCP_MEMORY_CONTEXT	\
	uint16 tmpaddr;
	uintptr tmpbase;
	uint8 tmpval;

#define ATCP_DUMMY_READ_BYTE(addr) ((void)(0))
#define ATCP_DEBUG_READ_BYTE(addr) (tmpaddr = (addr), tmpbase = mReadMap[(uint8)(tmpaddr >> 8)], (tmpbase & 1 ? DebugReadByteSlow(tmpbase, tmpaddr) : *(uint8 *)(tmpbase + tmpaddr)))
#define ATCP_READ_BYTE(addr) (tmpaddr = (addr), tmpbase = mReadMap[(uint8)(tmpaddr >> 8)], (tmpbase & 1 ? ReadByteSlow(tmpbase, tmpaddr) : *(uint8 *)(tmpbase + tmpaddr)))
#define ATCP_WRITE_BYTE(addr, value) ((void)(tmpaddr = (addr), tmpval = (value), tmpbase = mWriteMap[(uint8)(tmpaddr >> 8)], (tmpbase & 1 ? WriteByteSlow(tmpbase, tmpaddr, tmpval) : (*(uint8 *)(tmpbase + tmpaddr) = tmpval))))

const uint8 ATCoProc65802::kInitialState = ATCPUStates::kStateReadOpcode;

ATCoProc65802::ATCoProc65802()
	: mA(0)
	, mAH(0)
	, mP(0)
	, mbEmulationFlag(false)
	, mX(0)
	, mXH(0)
	, mY(0)
	, mYH(0)
	, mS(0)
	, mSH(0)
	, mDP(0)
	, mB(0)
	, mK(0)
	, mPC(0)
	, mInsnPC(0)
	, mCyclesLeft(0)
	, mpHistory(nullptr)
	, mHistoryIndex(0)
{
	memset(mReadMap, 0, sizeof mReadMap);
	memset(mWriteMap, 0, sizeof mWriteMap);

	ATCPUDecoderGenerator65816 gen;
	gen.RebuildTables(mDecoderTables, false, false);
}

void ATCoProc65802::SetHistoryBuffer(ATCPUHistoryEntry buffer[131072]) {
	bool historyWasOn = (mpHistory != nullptr);
	bool historyNowOn = (buffer != nullptr);

	mpHistory = buffer;

	if (historyWasOn != historyNowOn) {
		for(uint8& op : mDecoderTables.mDecodeHeap) {
			if (op == ATCPUStates::kStateReadOpcode)
				op = ATCPUStates::kStateRegenerateDecodeTables;
			else if (op == ATCPUStates::kStateAddToHistory)
				op = ATCPUStates::kStateNop;
		}
	}
}

void ATCoProc65802::GetExecState(ATCPUExecState& state) const {
	state.mPC = mInsnPC;
	state.mA = mA;
	state.mX = mX;
	state.mY = mY;
	state.mS = mS;
	state.mP = mP;
	state.mAH = mAH;
	state.mXH = mXH;
	state.mYH = mYH;
	state.mSH = mSH;
	state.mB = mB;
	state.mK = mK;
	state.mDP = mDP;
	state.mbEmulationFlag = mbEmulationFlag;
}

void ATCoProc65802::SetExecState(const ATCPUExecState& state) {
	if (mInsnPC != state.mPC) {
		mPC = state.mPC;
		mInsnPC = state.mPC;

		static const uint8 kInitialState = ATCPUStates::kStateReadOpcode;
		mpNextState = &kInitialState;
	}

	mA = state.mA;
	mX = state.mX;
	mY = state.mY;
	mS = state.mS;

	uint8 p = state.mP;
	bool redecode = false;

	if (state.mbEmulationFlag)
		p |= 0x30;

	if (mP != p) {
		if ((mP ^ p) & 0x30)
			redecode = true;

		mP = p;
	}

	mAH = state.mAH;

	if (!(mP & 0x10)) {
		mXH = state.mXH;
		mYH = state.mYH;
	}

	if (!mbEmulationFlag)
		mSH = state.mSH;

	mB = state.mB;
	mK = state.mK;

	if (mDP != state.mDP) {
		mDP = state.mDP;

		redecode = true;
	}

	if (mbEmulationFlag != state.mbEmulationFlag) {
		mbEmulationFlag = state.mbEmulationFlag;

		if (mbEmulationFlag) {
			mXH = 0;
			mYH = 0;
			mSH = 1;
		}
	}

	if (redecode)
		UpdateDecodeTable();
}

void ATCoProc65802::ColdReset() {
	mA = 0;
	mAH = 0;
	mP = 0x30;
	mX = 0;
	mXH = 0;
	mY = 0;
	mYH = 0;
	mS = 0xFF;
	mSH = 0x01;
	mDP = 0;
	mB = 0;
	mK = 0;
	mPC = 0;

	WarmReset();
}

void ATCoProc65802::WarmReset() {
	ATCP_MEMORY_CONTEXT;

	mPC = ATCP_READ_BYTE(0xFFFC) + ((uint32)ATCP_READ_BYTE(0xFFFD) << 8);

	// clear D flag
	mP &= 0xF7;

	// set MX and E flags
	mP |= 0x30;
	mbEmulationFlag = true;

	mK = 0;
	mB = 0;
	mDP = 0;
	mSH = 1;
	mSubMode = kSubMode_Emulation;

	mpNextState = &kInitialState;
	mInsnPC = mPC;

	UpdateDecodeTable();
}

void ATCoProc65802::Run() {
	using namespace ATCPUStates;

	ATCP_MEMORY_CONTEXT;

	if (mCyclesLeft <= 0)
		return;

	uint32		cyclesLeft = mCyclesLeft;
	mCyclesBase += cyclesLeft;
	const uint32 cyclesBase = mCyclesBase;

	const uint8 *nextState = mpNextState;

	#include <co65802.inl>

	mpNextState = nextState;
	mCyclesLeft = (sint32)cyclesLeft;
}

inline uint8 ATCoProc65802::DebugReadByteSlow(uintptr base, uint32 addr) {
	auto node = (ATCoProcReadMemNode *)(base - 1);

	return node->mpDebugRead(addr, node->mpThis);
}

inline uint8 ATCoProc65802::ReadByteSlow(uintptr base, uint32 addr) {
	auto node = (ATCoProcReadMemNode *)(base - 1);

	return node->mpRead(addr, node->mpThis);
}

inline void ATCoProc65802::WriteByteSlow(uintptr base, uint32 addr, uint8 value) {
	auto node = (ATCoProcWriteMemNode *)(base - 1);

	node->mpWrite(addr, value, node->mpThis);
}

void ATCoProc65802::UpdateDecodeTable() {
	SubMode subMode = kSubMode_Emulation;

	if (!mbEmulationFlag)
		subMode = (SubMode)(kSubMode_NativeM16X16 + ((mP >> 4) & 3));

	if (mSubMode != subMode) {
		mSubMode = subMode;

		if (mbEmulationFlag) {
			mSH = 0x01;
			mXH = 0;
			mYH = 0;
		} else {
			if (mP & 0x10) {
				mXH = 0;
				mYH = 0;
			}
		}
	}

	uint8 decMode = (uint8)(subMode + ((mDP & 0xff) ? 5 : 0) - kSubMode_Emulation);

	mpDecodePtrs = mDecoderTables.mInsnPtrs[decMode];
}

const uint8 *ATCoProc65802::RegenerateDecodeTables() {
	ATCPUDecoderGenerator65816 gen;
	gen.RebuildTables(mDecoderTables, false, mpHistory != nullptr);

	return &kInitialState;
}

