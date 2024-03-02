//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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

#ifndef f_AT_PIA_H
#define f_AT_PIA_H

class ATIRQController;
class ATSaveStateReader;
class ATSaveStateWriter;

struct ATPIAState {
	uint8 mORA;
	uint8 mDDRA;
	uint8 mCRA;
	uint8 mORB;
	uint8 mDDRB;
	uint8 mCRB;
};

enum ATPIAOutputBits {
	kATPIAOutput_CA2 = 0x10000,		// motor
	kATPIAOutput_CB2 = 0x20000		// command
};

typedef void (*ATPIAOutputFn)(void *data, uint32 outputState);

class ATPIAEmulator {
public:
	ATPIAEmulator();

	uint8 GetPortBOutput() const { return (uint8)(mOutput >> 8); }

	int AllocInput();
	void FreeInput(int index);
	void SetInput(int index, uint32 rval);

	int AllocOutput(ATPIAOutputFn fn, void *ptr, uint32 changeMask);
	void FreeOutput(int index);

	void Init(ATIRQController *irqcon);
	void Reset();

	void AssertProceed();
	void AssertInterrupt();

	uint8 DebugReadByte(uint8 addr) const;
	uint8 ReadByte(uint8 addr);
	void WriteByte(uint8 addr, uint8 value);

	void GetState(ATPIAState& state) const;
	void DumpState();

	void BeginLoadState(ATSaveStateReader& reader);
	void LoadStateArch(ATSaveStateReader& reader);
	void EndLoadState(ATSaveStateReader& reader);

	void BeginSaveState(ATSaveStateWriter& writer);
	void SaveStateArch(ATSaveStateWriter& writer);

protected:
	void UpdateCA2();
	void UpdateCB2();
	void UpdateOutput();

	ATIRQController *mpIRQController;

	uint32	mInput;
	uint32	mOutput;

	uint32	mPortOutput;
	uint32	mPortDirection;

	uint8	mPORTACTL;
	uint8	mPORTBCTL;
	bool	mbPIAEdgeA;
	bool	mbPIAEdgeB;
	uint8	mPIACB2;

	enum {
		kPIACS_Floating,
		kPIACS_Low,
		kPIACS_High
	};

	uint32	mOutputReportMask;
	uint8	mOutputAllocBitmap;
	uint8	mInputAllocBitmap;
	uint16	mInputs[4];

	struct OutputEntry {
		uint32 mChangeMask;
		ATPIAOutputFn mpFn;
		void *mpData;
	};

	OutputEntry mOutputs[8];
};

#endif
