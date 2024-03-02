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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/math.h>
#include <at/atemulation/crtc.h>

void ATCRTCEmulator::Init(ChipModel chipModel, float clockRate) {
	mChipModel = chipModel;
	mClockRate = clockRate;

	static constexpr uint8 kRegMask[32] {
		0xFF,		// R0 horizontal total
		0xFF,		// R1 horizontal displayed
		0xFF,		// R2 horizontal sync position
		0x0F,		// R3 sync width
		0x7F,		// R4 vertical total
		0x1F,		// R5 vertical total adjust
		0x7F,		// R6 vertical displayed
		0x7F,		// R7 vertical sync position
		0x03,		// R8 interlace mode and skew
		0x1F,		// R9 max scan line address
		0x7F,		// R10 cursor start
		0x1F,		// R11 cursor end
		0x3F,		// R12 start address (H)
		0xFF,		// R13 start address (L)
		0x3F,		// R14 cursor (H)
		0xFF,		// R15 cursor (L)
		0x3F,		// R16 light pen (H)
		0xFF,		// R17 light pen (L)
	};

	memcpy(mRegMask, kRegMask, sizeof mRegMask);

	if (mChipModel == ChipModel::SY6545) {
		mRegMask[0x08] = 0xFF;		// R8 mode control
		mRegMask[0x12] = 0x3F;		// R18 update address (H)
		mRegMask[0x13] = 0xFF;		// R19 update address (L)
	}
}

bool ATCRTCEmulator::CheckTimingChanged() {
	bool changed = mbTimingChanged;

	if (changed)
		mbTimingChanged = false;

	return changed;
}

bool ATCRTCEmulator::CheckImageInvalidated() {
	bool inv = mbImageInvalidated;

	if (inv)
		mbImageInvalidated = false;

	return inv;
}

uint32 ATCRTCEmulator::GetTranspAccessAddr() const {
	return VDReadUnalignedBEU16(&mReg[0x12]);
}

void ATCRTCEmulator::IncrementTranspAccessAddr() {
	VDWriteUnalignedBEU16(&mReg[0x12], (VDReadUnalignedBEU16(&mReg[0x12]) + 1) & 0x3FFF);
}

void ATCRTCEmulator::Reset() {
	memset(mReg, 0, sizeof mReg);
	mbImageInvalidated = true;
	mbTimingChanged = true;
	mbInternalTimingDirty = true;
}

void ATCRTCEmulator::Advance(float dt) {
	if (mbInternalTimingDirty)
		UpdateInternalTiming();

	mBlinkAccum += dt * mScanRates.mVert / 32.0f;
	mBlinkAccum -= floorf(mBlinkAccum);

	auto prevMask = mCursorInfo.mLineMask;
	UpdateCursorInfo();

	if (mCursorInfo.mLineMask != prevMask)
		MarkImageDirty();
}

ATCRTCEmulator::DisplayInfo ATCRTCEmulator::GetDisplayInfo() const {
	const int horizTotal = (int)mReg[0] + 1;
	const int vertTotal = (int)mReg[4] + 1;
	const int vertTotalAdjust = (int)mReg[5];
	const int charHeight = (int)mReg[9] + 1;

	DisplayInfo di {};
	di.mBaseAddr = GetBaseAddr();
	di.mRows = mReg[6];
	di.mCols = mReg[1];
	di.mLinesPerRow = mReg[9] + 1;

	return di;
}

bool ATCRTCEmulator::IsInVBLANK(uint64 currentCycle) {
	const uint32 offset = GetFrameCycleOffset(currentCycle);

	return offset >= mCyclesPerVDisp;
}

const ATCRTCEmulator::ScanRates& ATCRTCEmulator::GetScanRates() {
	if (mbInternalTimingDirty)
		UpdateInternalTiming();

	return mScanRates;
}

uint8 ATCRTCEmulator::DebugReadByte(uint8 reg) const {
	reg &= 0x1F;

	return mReg[reg];
}

uint8 ATCRTCEmulator::ReadByte(uint8 reg) {
	return DebugReadByte(reg);
}

void ATCRTCEmulator::WriteByte(uint8 reg, uint8 value) {
	reg &= 0x1F;
	value &= mRegMask[reg];

	const uint8 prevValue = mReg[reg];
	if (prevValue == value)
		return;

	mReg[reg] = value;

	if (reg < 10) {
		mbTimingChanged = true;
		mbInternalTimingDirty = true;
	}

	switch(reg) {
		case 12:	// start address (H)
		case 13:	// start address (L)
			mbImageInvalidated = true;
			break;

		case 10:	// cursor start
			// invalidate if cursor enabled or was enabled
			if ((value & 0x60) != 0x20 || (prevValue & 0x60) != 0x20)
				mbImageInvalidated = true;
			break;

		case 11:	// cursor end
		case 14:	// cursor (H)
		case 15:	// cursor (L)
			// invalidate if cursor enabled
			if ((mReg[10] & 0x60) != 0x20)
				mbImageInvalidated = true;
			break;
	}
}

uint64 ATCRTCEmulator::AccessTranspLatch(uint64 t) {
	// From the SY6545 Application Note 3:
	//
	// The update begins no sooner than two cycles past MPU access. We don't
	// model that here, but instead in the calling code, which is expected to
	// offset t. This also allows this function to be used for writes.
	//
	// Read update takes 4 cycles, the first two of which need to happen before
	// blanking ends. If the read update is truncated, the not ready status only
	// persists through the third cycle and the fourth cycle is dropped. If only
	// one cycle is available before blanking, the update is aborted and status
	// stays not ready until the access is retried at the next blanking region.
	// It is not specified what happens if the horizontal blank is only one
	// cycle long, but presumably the update would continuously abort until
	// vertical blank, and if vertical blank did not exist, it would never
	// complete until timing was reconfigured. We fudge that last case as
	// neither common nor interesting.
	//
	// In the case of a write, two updates happen back-to-back, with no delays
	// in between. When there is enough blanking time, this occurs as a
	// continuous sequence of 8 cycles.

	uint32 frameOffset = GetFrameCycleOffset(t);
	uint32 x = frameOffset % mHTotal;
	uint32 y = frameOffset / mHTotal;

	// Check if we must delay until next HBLANK. This happens if:
	// - We are in horizontal display, or
	// - We are in the last cycle of horizontal blank of any line EXCEPT the
	//   last displayed line, or
	// - We are in the last cycle of vertical blank.

	if (x < mHDisp && y < mVDisp) {
		// in active display -- delay until HBLANK, then run 4 cycles
		return t + (mHDisp - x) + 4;
	} else if (x >= mHTotal - 2 && (y + 1 < mVDisp || y == mVTotal - 1)) {
		// at end of HBLANK with not enough time to run 4 cycles
		if (x >= mHTotal - 1) {
			// not enough time for two cycles -- delay until next HBLANK, then
			// run 4 cycles
			return t + mHDisp + 5;
		} else {
			// just enough time for two cycles -- run three cycles, drop the
			// fourth (the third cycle overlaps display)
			return t + 3;
		}
	}

	// enough time to run 4 cycles, so do it
	return t + 4;
}

uint32 ATCRTCEmulator::GetBaseAddr() const {
	return VDReadUnalignedBEU16(&mReg[0x0C]);
}

uint32 ATCRTCEmulator::GetCursorAddr() const {
	return VDReadUnalignedBEU16(&mReg[0x0E]);
}

void ATCRTCEmulator::UpdateCursorInfo() {
	mCursorInfo = {};

	// check cursor visibility by mode
	switch(mReg[10] & 0x60) {
		case 0x00:	// always on
			break;

		case 0x20:	// always off
			return;

		case 0x40:	// blinking at 1/16th field rate
			if (!((int)(mBlinkAccum * 4) & 1))
				return;
			break;

		case 0x60:	// blinking at 1/32nd field rate
			if (mBlinkAccum >= 0.5f)
				return;
			break;
	}

	// check if cursor is actually within display area
	uint32 cursorOffset = (GetCursorAddr() - GetBaseAddr()) & 0x3FFF;
	uint32 horizDisp = mReg[1];
	uint32 vertDisp = mReg[6];

	if (cursorOffset >= horizDisp * vertDisp)
		return;

	// Compute cursor line mask.
	//
	// cursor_first is the row at which the cursor turns on (at the start
	// of the row), and cursor_last is the row at which the cursor turns
	// off (at the end of the row). If the cursor start is outside of the
	// character cell, it never turns on, and if the cursor stop is outside
	// of the character cell, it never turns off. If both are outside then
	// the last cursor state sticks.
	const uint8 cursorFirst = mReg[0x0A] & 0x1F;
	const uint8 cursorLast = mReg[0x0B] & 0x1F;
	const uint8 linesPerCell = mReg[0x09] + 1;

	if (cursorFirst < linesPerCell) {
		mCursorInfo.mLineMask = ~UINT32_C(0);

		if (cursorFirst <= cursorLast) {
			// well-ordered cursor -- invert rows [first, min(last, 7)]
			mCursorInfo.mLineMask <<= cursorFirst;
		} else if (cursorFirst > cursorLast + 1) {
			// irregular inverted cursor
			mCursorInfo.mLineMask = (uint8)~((1U << cursorFirst) - (1U << (cursorLast + 1)));
		}
	}

	// return info with cursor position
	mCursorInfo.mRow = cursorOffset / horizDisp;
	mCursorInfo.mCol = cursorOffset % horizDisp;
}

void ATCRTCEmulator::UpdateInternalTiming() {
	const int horizTotal = (int)mReg[0] + 1;
	const int horizDisp = (int)mReg[1];
	const int vertTotal = (int)mReg[4] + 1;
	const int vertTotalAdjust = (int)mReg[5];
	const int vertDisp = (int)mReg[6];
	const int charHeight = (int)mReg[9] + 1;

	const int totalLines = vertTotal * charHeight + vertTotalAdjust;

	mScanRates.mHoriz = mClockRate / (float)horizTotal;
	mScanRates.mVert = mScanRates.mHoriz / (float)totalLines;

	mCyclesPerFrame = horizTotal * totalLines;
	mCyclesPerVDisp = vertDisp * charHeight * horizTotal;

	mHTotal = horizTotal;
	mHDisp = horizDisp;
	mVTotal = totalLines;
	mVDisp = vertDisp * charHeight;

	mbInternalTimingDirty = false;
}

uint32 ATCRTCEmulator::GetFrameCycleOffset(uint64 t) {
	if (mbInternalTimingDirty)
		UpdateInternalTiming();

	uint64 offset = t - mCachedFrameStart;
	if (offset >= mCyclesPerFrame) {
		offset = t % mCyclesPerFrame;
		mCachedFrameStart = t - offset;
	}

	return (uint32)offset;
}
