//	Altirra - Atari 800/800XL/5200 emulator
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

#ifndef f_AT_VERONICA_H
#define f_AT_VERONICA_H

#include <vd2/system/vdstl.h>
#include <vd2/system/refcount.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceprinter.h>
#include <at/atcore/deviceserial.h>
#include <at/atcpu/co65802.h>
#include <at/atcpu/history.h>
#include <at/atdebugger/target.h>
#include <at/atcore/scheduler.h>

class ATMemoryLayer;
class ATIRQController;
class IATIDEDisk;

class ATVeronicaEmulator final : public ATDevice
	, public IATDeviceMemMap
	, public IATDeviceScheduling
	, public IATDeviceDebugTarget
	, public IATDeviceCartridge
	, public IATDebugTarget
	, public IATDebugTargetHistory
	, public IATSchedulerCallback
{
public:
	ATVeronicaEmulator();
	~ATVeronicaEmulator();

	void *AsInterface(uint32 iid);

	virtual void GetDeviceInfo(ATDeviceInfo& info);
	virtual void GetSettings(ATPropertySet& settings);
	virtual bool SetSettings(const ATPropertySet& settings);
	virtual void Init();
	virtual void Shutdown();
	virtual void WarmReset();
	virtual void ColdReset();

public:
	virtual void InitMemMap(ATMemoryManager *memmap);
	virtual bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const;

public:
	virtual void InitScheduling(ATScheduler *sch, ATScheduler *slowsch);

public:
	virtual void InitCartridge(IATDeviceCartridgePort *port) override;
	virtual bool IsRD5Active() const override;

public:	// IATDeviceDebugTarget
	IATDebugTarget *GetDebugTarget(uint32 index) override;

public:	// IATDebugTarget
	const char *GetName() override;
	ATDebugDisasmMode GetDisasmMode() override;

	void GetExecState(ATCPUExecState& state) override;
	void SetExecState(const ATCPUExecState& state) override;

	uint8 ReadByte(uint32 address) override;
	void ReadMemory(uint32 address, void *dst, uint32 n) override;

	uint8 DebugReadByte(uint32 address) override;
	void DebugReadMemory(uint32 address, void *dst, uint32 n) override;

	void WriteByte(uint32 address, uint8 value) override;
	void WriteMemory(uint32 address, const void *src, uint32 n) override;

public:	// IATDebugTargetHistory
	bool GetHistoryEnabled() const override;
	void SetHistoryEnabled(bool enable) override;

	std::pair<uint32, uint32> GetHistoryRange() const override;
	uint32 ExtractHistory(const ATCPUHistoryEntry **hparray, uint32 start, uint32 n) const override;
	uint32 ConvertRawTimestamp(uint32 rawTimestamp) const override;

public:
	virtual void OnScheduledEvent(uint32 id);

protected:
	static sint32 OnDebugRead(void *thisptr, uint32 addr);
	static sint32 OnRead(void *thisptr, uint32 addr);
	static bool OnWrite(void *thisptr, uint32 addr, uint8 value);

	static uint8 OnCorruptableDebugRead(uint32 addr, void *thisptr);
	static uint8 OnCorruptableRead(uint32 addr, void *thisptr);
	static void OnCorruptableWrite(uint32 addr, uint8 value, void *thisptr);

	void WriteVControl(uint8 val);
	void UpdateCoProcWindowDormant();
	void UpdateCoProcWindowActive();
	void UpdateWindowBase();
	void Sync();
	uint32 PeekRand16() const;
	uint32 Rand16();

	ATScheduler *mpScheduler;
	ATScheduler *mpSlowScheduler;
	ATEvent *mpRunEvent;
	ATMemoryManager *mpMemMan;
	ATMemoryLayer *mpMemLayerLeftWindow;
	ATMemoryLayer *mpMemLayerRightWindow;
	ATMemoryLayer *mpMemLayerControl;
	IATDeviceCartridgePort *mpCartridgePort;

	uint32 mLastSync;
	uint8 mAControl;
	uint8 mVControl;
	bool mbVersion1;
	bool mbCorruptNextCycle;
	uint8 *mpCoProcWinBase;

	uint32	mPRNG;

	ATCoProcWriteMemNode mWriteNode;
	ATCoProcReadMemNode mCorruptedReadNode;
	ATCoProcWriteMemNode mCorruptedWriteNode;
	ATCoProc65802 mCoProc;

	vdfastvector<ATCPUHistoryEntry> mHistory;

	VDALIGN(4) uint8 mRAM[0x20000];
};

#endif
