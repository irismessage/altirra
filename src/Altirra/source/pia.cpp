//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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
#include <vd2/system/binary.h>
#include <vd2/system/bitmath.h>
#include "pia.h"
#include "irqcontroller.h"
#include "console.h"
#include "savestate.h"

ATPIAEmulator::ATPIAEmulator()
	: mpIRQController(NULL)
	, mInput(0xFFFFFFFF)
	, mOutput(0xFFFFFFFF)
	, mPortOutput(0)
	, mPortDirection(0)
	, mPORTACTL(0)
	, mPORTBCTL(0)
	, mbPIAEdgeA(false)
	, mbPIAEdgeB(false)
	, mbCA1(true)
	, mbCB1(true)
	, mPIACB2(kPIACS_Floating)
	, mOutputReportMask(0)
	, mOutputAllocBitmap(0)
	, mInputAllocBitmap(0)
{
	for(int i=0; i<4; ++i)
		mInputs[i] = 0xFFFF;

	memset(mOutputs, 0, sizeof mOutputs);
}

int ATPIAEmulator::AllocInput() {
	if (mInputAllocBitmap == 15) {
		VDASSERT(!"PIA inputs exhausted.");
		return -1;
	}

	int idx = VDFindLowestSetBitFast(~mInputAllocBitmap);

	mInputAllocBitmap |= (1 << idx);
	return idx;
}

void ATPIAEmulator::FreeInput(int index) {
	if (index >= 0)
		mInputAllocBitmap &= ~(1 << index);
}

void ATPIAEmulator::SetInput(int index, uint32 rval) {
	VDASSERT(index < 4);

	if (index >= 0 && rval != mInputs[index]) {
		mInputs[index] = rval;

		uint32 v = mInputs[0] & mInputs[1] & mInputs[2] & mInputs[3];

		if (mInput != v) {
			mInput = v;

			UpdateOutput();
		}
	}
}

int ATPIAEmulator::AllocOutput(ATPIAOutputFn fn, void *ptr, uint32 changeMask) {
	if (mOutputAllocBitmap == 0xFF) {
		VDASSERT(!"PIA outputs exhausted.");
		return -1;
	}

	int idx = VDFindLowestSetBitFast(~mOutputAllocBitmap);

	mOutputAllocBitmap |= (1 << idx);

	OutputEntry& output = mOutputs[idx];
	output.mChangeMask = changeMask;
	output.mpFn = fn;
	output.mpData = ptr;

	mOutputReportMask |= changeMask;

	return idx;
}

void ATPIAEmulator::FreeOutput(int index) {
	if (index >= 0) {
		mOutputAllocBitmap &= ~(1 << index);

		mOutputs[index].mChangeMask = 0;

		mOutputReportMask = 0;

		for(int i=0; i<8; ++i)
			mOutputReportMask |= mOutputs[i].mChangeMask;
	}
}

void ATPIAEmulator::Init(ATIRQController *irqcon) {
	mpIRQController = irqcon;
}

void ATPIAEmulator::Reset() {
	mPortOutput = kATPIAOutput_CA2 | kATPIAOutput_CB2;
	mPortDirection = kATPIAOutput_CA2 | kATPIAOutput_CB2;
	mPORTACTL	= 0x00;
	mPORTBCTL	= 0x00;
	mbPIAEdgeA = false;
	mbPIAEdgeB = false;
	mPIACB2 = kPIACS_Floating;

	if (mpIRQController)
		mpIRQController->Negate(kATIRQSource_PIAA1 | kATIRQSource_PIAA2 | kATIRQSource_PIAB1 | kATIRQSource_PIAB2, true);

	UpdateOutput();
}

void ATPIAEmulator::SetCA1(bool level) {
	if (mbCA1 == level)
		return;

	mbCA1 = level;

	// check if interrupts are enabled and that the interrupt isn't already active
	if ((mPORTACTL & 0x81) != 0x01)
		return;

	// check if we have the correct transition
	if (mPORTACTL & 0x02) {
		if (!level)
			return;
	} else {
		if (level)
			return;
	}

	// assert IRQ
	mPORTACTL |= 0x80;

	if (mpIRQController)
		mpIRQController->Assert(kATIRQSource_PIAA1, true);
}

void ATPIAEmulator::SetCB1(bool level) {
	if (mbCB1 == level)
		return;

	mbCB1 = level;

	// check if interrupts are enabled and that the interrupt isn't already active
	if ((mPORTBCTL & 0x81) != 0x01)
		return;

	// check if we have the correct transition
	if (mPORTBCTL & 0x02) {
		if (!level)
			return;
	} else {
		if (level)
			return;
	}

	// assert IRQ
	mPORTBCTL |= 0x80;

	if (mpIRQController)
		mpIRQController->Assert(kATIRQSource_PIAB1, true);
}

uint8 ATPIAEmulator::DebugReadByte(uint8 addr) const {
	switch(addr & 0x03) {
	case 0x00:
	default:
		// Port A reads the actual state of the output lines.
		return mPORTACTL & 0x04
			? (uint8)(mInput & (mOutput | ~mPortDirection))
			: (uint8)mPortDirection;

	case 0x01:
		// Port B reads output bits instead of input bits for those selected as output. No ANDing with input.
		return mPORTBCTL & 0x04
			? (uint8)((((mInput ^ mOutput) & mPortDirection) ^ mInput) >> 8)
			: (uint8)(mPortDirection >> 8);

	case 0x02:
		return mPORTACTL;

	case 0x03:
		return mPORTBCTL;
	}
}

uint8 ATPIAEmulator::ReadByte(uint8 addr) {
	switch(addr & 0x03) {
	case 0x00:
		if (mPORTACTL & 0x04) {
			// Reading the PIA port A data register negates port A interrupts.
			mPORTACTL &= 0x3F;

			if (mpIRQController)
				mpIRQController->Negate(kATIRQSource_PIAA1 | kATIRQSource_PIAA2, true);
		}
		break;

	case 0x01:
		if (mPORTBCTL & 0x04) {
			// Reading the PIA port A data register negates port B interrupts.
			mPORTBCTL &= 0x3F;

			if (mpIRQController)
				mpIRQController->Negate(kATIRQSource_PIAB1 | kATIRQSource_PIAB2, true);
		}

		break;

	case 0x02:
		return mPORTACTL;
	case 0x03:
		return mPORTBCTL;
	}

	return DebugReadByte(addr);
}

void ATPIAEmulator::WriteByte(uint8 addr, uint8 value) {
	switch(addr & 0x03) {
	case 0x00:
		{
			uint32 xor;
			if (mPORTACTL & 0x04) {
				xor = (mPortOutput ^ value) & 0xff;
				if (!xor)
					return;

				mPortOutput ^= xor;
			} else {
				xor = (mPortDirection ^ value) & 0xff;

				if (!xor)
					return;

				mPortDirection ^= xor;
			}

			UpdateOutput();
		}
		break;
	case 0x01:
		{
			uint32 xor;
			if (mPORTBCTL & 0x04) {
				xor = (mPortOutput ^ ((uint32)value << 8)) & 0xff00;
				if (!xor)
					return;

				mPortOutput ^= xor;
			} else {
				xor = (mPortDirection ^ ((uint32)value << 8)) & 0xff00;

				if (!xor)
					return;

				mPortDirection ^= xor;
			}

			UpdateOutput();
		}
		break;
	case 0x02:
		switch(value & 0x38) {
			case 0x00:
			case 0x08:
			case 0x28:
				mbPIAEdgeA = false;
				break;

			case 0x10:
			case 0x18:
				if (mbPIAEdgeA) {
					mbPIAEdgeA = false;
					mPORTACTL |= 0x40;
				}
				break;

			case 0x20:
			case 0x38:
				break;

			case 0x30:
				mbPIAEdgeA = true;
				break;
		}

		if (value & 0x20)
			mPORTACTL &= ~0x40;

		mPORTACTL = (mPORTACTL & 0xc0) + (value & 0x3f);

		if (mpIRQController) {
			if ((mPORTACTL & 0x68) == 0x48)
				mpIRQController->Assert(kATIRQSource_PIAA2, true);
			else
				mpIRQController->Negate(kATIRQSource_PIAA2, true);
		}

		UpdateCA2();
		break;
	case 0x03:
		switch(value & 0x38) {
			case 0x00:
			case 0x08:
			case 0x10:
			case 0x18:
				if (mbPIAEdgeB) {
					mbPIAEdgeB = false;
					mPORTBCTL |= 0x40;
				}

				mPIACB2 = kPIACS_Floating;
				break;

			case 0x20:
				mbPIAEdgeB = false;
				break;

			case 0x28:
				mbPIAEdgeB = false;
				mPIACB2 = kPIACS_High;
				break;

			case 0x30:
				mbPIAEdgeB = false;
				mPIACB2 = kPIACS_Low;
				break;

			case 0x38:
				if (mPIACB2 == kPIACS_Low)
					mbPIAEdgeB = true;

				mPIACB2 = kPIACS_High;
				break;
		}

		if (value & 0x20)
			mPORTBCTL &= ~0x40;

		mPORTBCTL = (mPORTBCTL & 0xc0) + (value & 0x3f);

		if (mpIRQController) {
			if ((mPORTBCTL & 0x68) == 0x48)
				mpIRQController->Assert(kATIRQSource_PIAB2, true);
			else
				mpIRQController->Negate(kATIRQSource_PIAB2, true);
		}

		UpdateCB2();
		break;
	}
}

void ATPIAEmulator::GetState(ATPIAState& state) const {
	state.mCRA = mPORTACTL;
	state.mCRB = mPORTBCTL;
	state.mDDRA = (uint8)mPortDirection;
	state.mDDRB = (uint8)(mPortDirection >> 8);
	state.mORA = (uint8)mPortOutput;
	state.mORB = (uint8)(mPortOutput >> 8);
}

void ATPIAEmulator::DumpState() {
	ATPIAState state;
	GetState(state);

	ATConsolePrintf("Port A control:   %02x (%s, motor line %s)\n"
		, state.mCRA
		, state.mCRA & 0x04 ? "IOR" : "DDR"
		, (state.mCRA & 0x38) == 0x30 ? "low / on" : "high / off"
		);
	ATConsolePrintf("Port A direction: %02x\n", state.mDDRA);
	ATConsolePrintf("Port A output:    %02x\n", state.mORA);
	ATConsolePrintf("Port A edge:      %s\n", mbPIAEdgeA ? "pending" : "none");

	ATConsolePrintf("Port B control:   %02x (%s, command line %s)\n"
		, state.mCRB
		, state.mCRB & 0x04 ? "IOR" : "DDR"
		, (state.mCRB & 0x38) == 0x30 ? "low / on" : "high / off"
		);
	ATConsolePrintf("Port B direction: %02x\n", state.mDDRB);
	ATConsolePrintf("Port B output:    %02x\n", state.mORB);
	ATConsolePrintf("Port B edge:      %s\n", mbPIAEdgeB ? "pending" : "none");
}

void ATPIAEmulator::BeginLoadState(ATSaveStateReader& reader) {
	reader.RegisterHandlerMethod(kATSaveStateSection_Arch, VDMAKEFOURCC('P', 'I', 'A', ' '), this, &ATPIAEmulator::LoadStateArch);
	reader.RegisterHandlerMethod(kATSaveStateSection_End, 0, this, &ATPIAEmulator::EndLoadState);
}

void ATPIAEmulator::LoadStateArch(ATSaveStateReader& reader) {
	const uint8 ora = reader.ReadUint8();
	const uint8 orb = reader.ReadUint8();
	const uint8 ddra = reader.ReadUint8();
	const uint8 ddrb = reader.ReadUint8();

	mPortOutput = ((uint32)orb << 8) + ora;
	mPortDirection = ((uint32)ddrb << 8) + ddra + kATPIAOutput_CA2 + kATPIAOutput_CB2;
	mPORTACTL = reader.ReadUint8();
	mPORTBCTL = reader.ReadUint8();
}

void ATPIAEmulator::EndLoadState(ATSaveStateReader& reader) {
	UpdateCA2();
	UpdateCB2();
	UpdateOutput();
}

void ATPIAEmulator::BeginSaveState(ATSaveStateWriter& writer) {
	writer.RegisterHandlerMethod(kATSaveStateSection_Arch, this, &ATPIAEmulator::SaveStateArch);
}

void ATPIAEmulator::SaveStateArch(ATSaveStateWriter& writer) {
	ATPIAState state;
	GetState(state);

	writer.BeginChunk(VDMAKEFOURCC('P', 'I', 'A', ' '));
	writer.WriteUint8(state.mORA);
	writer.WriteUint8(state.mORB);
	writer.WriteUint8(state.mDDRA);
	writer.WriteUint8(state.mDDRB);
	writer.WriteUint8(state.mCRA);
	writer.WriteUint8(state.mCRB);
	writer.EndChunk();
}

void ATPIAEmulator::UpdateCA2() {
	// bits 3-5:
	//	0xx		input (passively pulled high)
	//	100		output - set high by interrupt and low by port A data read
	//	101		output - normally set high, pulse low for one cycle by port A data read
	//	110		output - low
	//	111		output - high
	//
	// Right now we don't emulate the interrupt or pulse modes.

	if ((mPORTACTL & 0x38) == 0x30)
		mPortOutput &= ~kATPIAOutput_CA2;
	else
		mPortOutput |= kATPIAOutput_CA2;

	UpdateOutput();
}

void ATPIAEmulator::UpdateCB2() {
	// bits 3-5:
	//	0xx		input (passively pulled high)
	//	100		output - set high by interrupt and low by port B data read
	//	101		output - normally set high, pulse low for one cycle by port B data read
	//	110		output - low
	//	111		output - high
	//
	// Right now we don't emulate the interrupt or pulse modes.

	if ((mPORTBCTL & 0x38) == 0x30)
		mPortOutput &= ~kATPIAOutput_CB2;
	else
		mPortOutput |= kATPIAOutput_CB2;

	UpdateOutput();
}

void ATPIAEmulator::UpdateOutput() {
	const uint32 newOutput = mPortOutput | ~mPortDirection;
	const uint32 delta = mOutput ^ newOutput;

	if (!delta)
		return;

	mOutput = newOutput;

	if (delta & mOutputReportMask) {
		for(int i=0; i<8; ++i) {
			const OutputEntry& output = mOutputs[i];

			if (output.mChangeMask & delta)
				output.mpFn(output.mpData, mOutput);
		}
	}
}
