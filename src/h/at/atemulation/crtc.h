//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef f_AT_ATEMULATION_CRTC_H
#define f_AT_ATEMULATION_CRTC_H

class ATCRTCEmulator {
public:
	enum class ChipModel {
		MC6845,		// Motorola MC6845 - baseline
		SY6545		// Synertek SY6545 - transparent update facility
	};

	void Init(ChipModel chipModel, float clockRate);

	bool CheckTimingChanged();
	bool CheckImageInvalidated();

	struct CursorInfo {
		uint32 mLineMask;
		uint32 mRow;
		uint32 mCol;
	};

	const CursorInfo& GetCursorInfo() const { return mCursorInfo; }

	struct DisplayInfo {
		uint32 mBaseAddr;
		uint32 mRows;
		uint32 mCols;
		uint8 mLinesPerRow;
	};

	DisplayInfo GetDisplayInfo() const;
	bool IsInVBLANK(uint64 currentCycle);

	struct ScanRates { float mHoriz, mVert; };
	const ScanRates& GetScanRates();

	// Retrieve address for transparent access for SY6545 (R18/19)
	uint32 GetTranspAccessAddr() const;
	void IncrementTranspAccessAddr();

	void Reset();
	void Advance(float dt);
	void MarkImageDirty() { mbImageInvalidated = true; }

	uint8 DebugReadByte(uint8 reg) const;
	uint8 ReadByte(uint8 reg);
	void WriteByte(uint8 reg, uint8 value);

	// Given the start time of an automatic read/write (+2-3 cycles from MPU
	// write), return the time at which update ready is flagged.
	uint64 AccessTranspLatch(uint64 t);

private:
	uint32 GetBaseAddr() const;
	uint32 GetCursorAddr() const;
	void UpdateCursorInfo();
	void UpdateInternalTiming();

	uint32 GetFrameCycleOffset(uint64 t);

	ChipModel mChipModel {};
	bool mbImageInvalidated = false;
	bool mbTimingChanged = false;
	bool mbInternalTimingDirty = false;
	float mClockRate = 1.0f;
	float mBlinkAccum = 0.0f;

	uint64 mCachedFrameStart = 0;
	uint32 mCyclesPerFrame = 1;
	uint32 mCyclesPerVDisp = 0;
	uint32 mHDisp = 0;
	uint32 mHTotal = 1;
	uint32 mVDisp = 0;
	uint32 mVTotal = 1;

	CursorInfo mCursorInfo {};
	ScanRates mScanRates {};

	uint8 mReg[32] {};
	uint8 mRegMask[32] {};
};

#endif
