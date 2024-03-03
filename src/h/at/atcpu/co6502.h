//	Altirra - Atari 800/800XL/5200 emulator
//	Coprocessor library - 6507/6502/65C02 emulator
//	Copyright (C) 2009-2017 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_ATCOPROC_CO65802_H
#define f_ATCOPROC_CO65802_H

#include <at/atcpu/decode6502.h>
#include <at/atcore/scheduler.h>

struct ATCPUExecState;
struct ATCPUHistoryEntry;
class IATCPUBreakpointHandler;

class ATCoProcTraceCache;

class ATCoProc6502 {
public:
	ATCoProc6502(bool isC02, bool enableTraceCache);
	~ATCoProc6502();

	uintptr *GetReadMap() { return mReadMap; }
	const uintptr *GetReadMap() const { return mReadMap; }
	uintptr *GetWriteMap() { return mWriteMap; }
	const uintptr *GetWriteMap() const { return mWriteMap; }
	uint32 *GetTraceMap() { return mTraceMap; }

	void SetHistoryBuffer(ATCPUHistoryEntry buffer[131072]);
	uint32 GetHistoryCounter() const { return mHistoryIndex; }

	void GetExecState(ATCPUExecState& state) const;
	void SetExecState(const ATCPUExecState& state);

	void Jump(uint16 addr);

	uint16 GetPC() const { return mInsnPC; }
	uint8 GetA() const { return mA; }
	uint8 GetX() const { return mX; }
	uint8 GetY() const { return mY; }
	uint8 GetS() const { return mS; }
	uint8 GetP() const { return mP; }
	void SetS(uint8 s) { mS = s; }
	void SetP(uint8 c) { mP = (c | 0x30); }
	void SetV() { mExtraFlags = (ExtraFlags)(mExtraFlags | kExtraFlag_SetV); }

	uint32 GetStepStackLevel() const { return (uint32)mS << 24; }

	void SetBreakpointMap(const bool bpMap[65536], IATCPUBreakpointHandler *bphandler);
	void OnBreakpointsChanged(const uint16 *pc);
	void SetBreakOnUnsupportedOpcode(bool enabled) { mbBreakOnUnsupportedOpcode = enabled; }

	void ColdReset();
	void WarmReset();

	// Returns true if all cycles were eaten, false if stop occurred at beginning of cycle.
	bool Run(ATScheduler& scheduler);

	void InvalidateTraceCache();

private:
	uint8 DebugReadByteSlow(uintptr base, uint32 addr);
	uint8 ReadByteSlow(uintptr base, uint32 addr);
	void WriteByteSlow(uintptr base, uint32 addr, uint8 value);
	void DoExtra();
	bool CheckBreakpoint();
	const uint8 *RegenerateDecodeTables();

	void TryEnterTrace(uint32 tracePageId);
	void ExitTrace();
	void ClearTraceCache();
	uint32 CreateTrace(uint16 pc);

	// Cache line size used for traces. We try not to encode uop arguments across
	// such a boundary. Note that the whole uop can be split if it is determined
	// that no multibyte argument is split.
	static constexpr uint32 kTraceLineSize = 64;

	enum ExtraFlags : uint8 {
		kExtraFlag_SetV = 0x01
	};

	ExtraFlags	mExtraFlags = {};

	uint8		mA = 0;
	uint8		mX = 0;
	uint8		mY = 0;
	uint8		mP = 0;
	uint8		mS = 0;
	uint8		mData = 0;
	uint8		mOpcode = 0;
	uint8		mRelOffset = 0;
	uint16		mAddr = 0;
	uint16		mAddr2 = 0;
	uint16		mPC = 0;
	uint16		mInsnPC = 0;
	const uint8	*mpNextState = nullptr;
	const bool	*mpBreakpointMap = nullptr;
	IATCPUBreakpointHandler *mpBreakpointHandler = nullptr;
	ATCPUHistoryEntry *mpHistory = nullptr;
	uint32		mHistoryIndex = 0;
	bool		mbBreakOnUnsupportedOpcode = false;
	bool		mbIs65C02 = false;
	bool		mbHistoryChangePending = false;

	ATCoProcTraceCache *mpTraceCache = nullptr;

	uintptr		mReadMap[256] = {};
	uintptr		mWriteMap[256] = {};
	uint32		mTraceMap[256] = {};

	ATCPUDecoderTables6502 mDecoderTables {};

	static const uint8 kInitialState;
	static const uint8 kInitialStateNoBreak;
};

#endif	// f_ATCOPROC_CO65802_H
