//	Altirra - Atari 800/800XL/5200 aemulator
//	Coprocessor library - 8051 emulator
//	Copyright (C) 2024 Avery Lee
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
//	with this program. If not, see <https://www.gnu.org/licenses/>.
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_ATCOPROC_CO8051_H
#define f_ATCOPROC_CO8051_H

#include <vd2/system/function.h>
#include <at/atcore/enumutils.h>

struct ATCPUExecState;
struct ATCPUHistoryEntry;
class IATCPUBreakpointHandler;

class ATCoProc8051 {
public:
	ATCoProc8051();

	void SetProgramROM(const void *p0);

	uint8 GetSP() const { return mPSW & 7; }
	uint32 GetStepStackLevel() const { return (uint32)mPSW << 29; }

	void SetHistoryBuffer(ATCPUHistoryEntry buffer[131072]);
	uint32 GetHistoryCounter() const { return mHistoryIndex; }
	uint32 GetTime() const { return mCyclesBase - mCyclesLeft; }
	uint32 GetTimeBase() const { return mCyclesBase; }

	void GetExecState(ATCPUExecState& state) const;
	void SetExecState(const ATCPUExecState& state);

	void SetT0ReadHandler(const vdfunction<bool()>& fn);
	void SetT1ReadHandler(const vdfunction<bool()>& fn);
	void SetXRAMReadHandler(const vdfunction<uint8(uint16)>& fn);
	void SetXRAMWriteHandler(const vdfunction<void(uint16, uint8)>& fn);
	void SetPortReadHandler(const vdfunction<uint8(uint8, uint8)>& fn);
	void SetPortWriteHandler(const vdfunction<void(uint8, uint8)>& fn);
	void SetBusReadHandler(const vdfunction<uint8()>& fn);
	void SetBreakpointMap(const bool bpMap[65536], IATCPUBreakpointHandler *bphandler);

	void SetSerialXmitHandler(vdfunction<void(uint8)> fn);
	void SendSerialByteDone();
	void ReceiveSerialByte(uint8 v);

	const uint8 *GetInternalRAM() const { return mRAM; }
	uint8 *GetInternalRAM() { return mRAM; }
	uint8 ReadByte(uint8 addr) const;
	void WriteByte(uint8 addr, uint8 val);

	void ColdReset();
	void WarmReset();

	void AssertIrq();
	void NegateIrq();

	uint32 GetTStatesPending() const { return mTStatesLeft; }
	uint32 GetCyclesLeft() const { return mCyclesLeft; }
	void AddCycles(sint32 cycles) { mCyclesBase += cycles;  mCyclesLeft += cycles; }
	void Run();

private:
	enum class IntMask : uint8 {
		Ext0 = 0x01,
		Timer0 = 0x02,
		Ext1 = 0x04,
		Timer1 = 0x08,
		Serial = 0x10,
		Global = 0x80,
		All = 0x9F
	};

	AT_IMPLEMENT_ENUM_FLAGS_FRIEND_STATIC(IntMask);

	void DoAdd(uint8 v);
	void DoAddCarry(uint8 v);
	void DoSubBorrow(uint8 v);

	bool CheckBreakpoint();
	void NegateInternalIrq(IntMask);
	void AssertInternalIrq(IntMask);
	void DispatchInterrupt();

	uint32 GetTick() const;

	uint8 ReadPSW() const;
	void WritePSW(uint8 v);

	void WriteIE(uint8 v);
	void WriteIP(uint8 v);

	uint8 ReadTCON() const;
	void WriteTCON(uint8 v);

	uint8 ReadTL0() const;
	uint8 ReadTH0() const;
	void WriteTL0(uint8 v);
	void WriteTH0(uint8 v);

	uint32 ReadTimerCounter0() const;
	void UpdateTimer();
	void UpdateTimerDeadline();

	uint8 ReadP0() const;
	uint8 ReadP1() const;
	uint8 ReadP2() const;
	uint8 ReadP3() const;
	void WriteP0(uint8 v);
	void WriteP1(uint8 v);
	void WriteP2(uint8 v);
	void WriteP3(uint8 v);

	uint8 ReadSCON() const;
	void WriteSCON(uint8 v);
	uint8 ReadSBUF() const;
	void WriteSBUF(uint8 v);

	bool ReadCarry();
	void WriteCarry(bool v);

	uint8 ReadDirect(uint8 addr);
	uint8 ReadDirectRMW(uint8 addr);
	void WriteDirect(uint8 addr, uint8 v);
	bool ReadBit(uint8 bitAddr);
	bool ReadBitRMW(uint8 bitAddr);
	void WriteBit(uint8 bitAddr, bool v);
	uint8 ReadIndirect(uint8 addr);
	void WriteIndirect(uint8 addr, uint8 v);
	uint8 ReadXRAM(uint16 addr);
	void WriteXRAM(uint16 addr, uint8 v);
	void Push(uint8 v);
	uint8 Pop();

	int		mTStatesLeft = 0;

	uint8	mA = 0;
	uint8	mB = 0;
	uint8	mPSW = 0;
	uint8	mSP = 0;
	uint16	mDPTR = 0;
	uint8	*mpRegBank = nullptr;
	const uint8	*mpProgramROM = nullptr;

	uint8	mTCON = 0;
	uint32	mTH0 = 0;
	uint32	mTL0 = 0;

	uint8	mP0 = 0xFF;
	uint8	mP1 = 0xFF;
	uint8	mIE = 0;
	uint8	mIP = 0;

	// Interrupts that are currently active, though not necessarily enabled.
	IntMask	mActiveInterruptMask {};

	bool	mbIrqPending = false;

	// True if interrupts are being held off by one instruction after RETI or a write
	// to IE/IP. Implies mbIrqPending=true, as this needs to be cleared after an insn
	// even if all interrupts are disabled.
	bool	mbIrqHoldoff = false;

	// True if an interrupt is being serviced at high or low priority. A high priority
	// interrupt blocks all interrupts; a low priority interrupt blocks low-priority
	// interrupts. The MCS-51 manual implies that these are individual flip-flops
	// separate from the interrupt sources and IE/IP.
	bool	mbIntServicingHighPri = false;
	bool	mbIntServicingLowPri = false;

	bool	mbIrqAttention = false;
	bool	mbTimerIrqPending = false;

	// True if an interrupt was dispatched and should be indicated in the next history
	// entry.
	bool	mbHistoryIrqPending = false;

	uint16		mPC = 0;
	sint32		mCyclesLeft = 0;
	uint32		mCyclesBase = 0;
	uint32		mCyclesSaved = 0;

	uint32		mTimerLastUpdated = 0;
	bool		mbTimerActive = false;
	uint32		mTimerDeadline = 0;

	// serial port
	uint8		mSCON = 0;
	uint8		mSerialReadBuffer = 0;

	const uint8	*mpNextState = nullptr;
	const bool	*mpBreakpointMap = nullptr;
	IATCPUBreakpointHandler *mpBreakpointHandler = nullptr;
	ATCPUHistoryEntry *mpHistory = nullptr;
	uint32		mHistoryIndex = 0;
	uint8		mReadOpcodeState = 0;

	vdfunction<bool()> mpFnReadT0;
	vdfunction<bool()> mpFnReadT1;
	vdfunction<uint8(uint16)> mpFnReadXRAM;
	vdfunction<void(uint16, uint8)> mpFnWriteXRAM;
	vdfunction<uint8(uint8, uint8)> mpFnReadPort;
	vdfunction<void(uint8, uint8)> mpFnWritePort;
	vdfunction<uint8()> mpFnReadBus;
	vdfunction<void(uint8)> mpFnSerialXmit;
	
	alignas(2) uint8	mRAM[256];

	static const uint8 kInitialState;
	static const uint8 kInitialStateNoBreak;
	static const uint8 kIrqSequence[];
};

#endif	// f_ATCOPROC_CO8051_H
