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

#include <vd2/system/function.h>
#include <at/atcore/deviceport.h>

class ATScheduler;
class ATSaveStateReader;
class ATScheduler;
struct ATTraceContext;
class ATTraceChannelFormatted;
class IATObjectState;

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

struct ATPIAFloatingInputs {
	ATScheduler *mpScheduler;
	uint8 mFloatingInputMask;
	uint32	mRandomSeed;
	uint32	mDecayTimeMin;
	uint32	mDecayTimeRange;
	uint64	mFloatTimers[8];
};

class ATPIAEmulator final : public IATDevicePortManager {
public:
	enum : uint32 { kTypeID = 'PIA ' };

	ATPIAEmulator();

	uint8 GetPortBOutput() const { return (uint8)(mOutput >> 8); }

	int AllocInput() override;
	void FreeInput(int index) override;
	void SetInput(int index, uint32 rval) override;

	uint32 GetOutputState() const override { return mOutput; }
	int AllocOutput(ATPortOutputFn fn, void *ptr, uint32 changeMask) override;
	void ModifyOutputMask(int index, uint32 changeMask);
	void FreeOutput(int index) override;

	void SetTraceContext(ATTraceContext *context);
	void SetIRQHandler(vdfunction<void(uint32, bool)> fn);

	void Init(ATScheduler *scheduler);
	void ColdReset();
	void WarmReset();

	void SetCA1(bool level);		// Proceed
	void SetCA2(bool level);
	void SetCB1(bool level);		// Interrupt

	uint8 DebugReadByte(uint8 addr) const;
	uint8 ReadByte(uint8 addr);
	void WriteByte(uint8 addr, uint8 value);

	// Sets which port B bits are left floating and can therefore drift when switched
	// to input mode. Port A cannot float since it has internal pull-ups.
	//
	// The scheduler pointer, input mask, and decay time range, and random fields must
	// be filled out. The float timers will be auto-inited on set.
	//
	// Floating bits are only visible to the CPU, not to allocated outputs. Any bits
	// that are actually used are typically pulled up. A special case is U1MB, which
	// can have active bank bits that are floating on the PIA. We don't use floating
	// bits for banking there either, as the U1MB is itself shadowing PIA state
	// independently in the CPLD and doesn't see the floating bits.
	//
	void SetPortBFloatingInputs(ATPIAFloatingInputs *inputs);

	void GetState(ATPIAState& state) const;
	void DumpState();

	void BeginLoadState(ATSaveStateReader& reader);
	void LoadStateArch(ATSaveStateReader& reader);
	void EndLoadState(ATSaveStateReader& reader);

	void SaveState(IATObjectState **pp) const;
	void LoadState(IATObjectState& state);
	void PostLoadState();

protected:
	void UpdateCA2();
	void UpdateCB2();
	void UpdateOutput();
	bool SetPortBDirection(uint8 value);

	void NegateIRQs(uint32 mask);
	void AssertIRQs(uint32 mask);

	void SetCRA(uint8 v);
	void SetCRB(uint8 v);

	void UpdateTraceCRA();
	void UpdateTraceCRB();
	void UpdateTraceInputA();

	ATScheduler *mpScheduler;
	ATPIAFloatingInputs *mpFloatingInputs;
	vdfunction<void(uint32, bool)> mpIRQHandler;

	uint32	mInput;
	uint32	mOutput;

	uint32	mPortOutput;
	uint32	mPortDirection;

	uint8	mPORTACTL;
	uint8	mPORTBCTL;
	bool	mbPIAEdgeA;
	bool	mbPIAEdgeB;
	bool	mbCA1;
	bool	mbCA2;
	bool	mbCB1;
	uint8	mPIACB2;

	enum {
		kPIACS_Floating,
		kPIACS_Low,
		kPIACS_High
	};

	uint32	mOutputReportMask;
	uint16	mOutputAllocBitmap;
	uint8	mInputAllocBitmap;
	uint16	mInputs[4];

	struct OutputEntry {
		uint32 mChangeMask;
		ATPortOutputFn mpFn;
		void *mpData;
	};

	OutputEntry mOutputs[12];

	uint32 mTraceIRQState = 0;
	ATTraceContext *mpTraceContext = nullptr;
	ATTraceChannelFormatted *mpTraceCRA = nullptr;
	ATTraceChannelFormatted *mpTraceCRB = nullptr;
	ATTraceChannelFormatted *mpTraceInputA = nullptr;
};

#endif
