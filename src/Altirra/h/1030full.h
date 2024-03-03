//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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

#ifndef f_AT_1030FULL_H
#define f_AT_1030FULL_H

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/devicediskdrive.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcpu/co8048.h>
#include <at/atcpu/breakpoints.h>
#include <at/atcpu/history.h>
#include <at/atcpu/memorymap.h>
#include <at/atdebugger/breakpointsimpl.h>
#include <at/atdebugger/target.h>
#include <at/atcore/scheduler.h>
#include "diskdrivefullbase.h"
#include "diskinterface.h"

class ATIRQController;
class ATModemEmulator;

class ATDevice1030Full final : public ATDevice
	, public IATDeviceFirmware
	, public ATDeviceSIO
	, public ATDiskDriveDebugTargetControl
	, public IATDeviceRawSIO
{
public:
	ATDevice1030Full(bool mode835);
	~ATDevice1030Full();

	void *AsInterface(uint32 iid) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettings(ATPropertySet & settings) override;
	bool SetSettings(const ATPropertySet & settings) override;
	void Init() override;
	void Shutdown() override;
	void WarmReset() override;
	void ComputerColdReset() override;
	void PeripheralColdReset() override;

public:		// IATDeviceFirmware
	void InitFirmware(ATFirmwareManager *fwman) override;
	bool ReloadFirmware() override;
	const wchar_t *GetWritableFirmwareDesc(uint32 idx) const override;
	bool IsWritableFirmwareDirty(uint32 idx) const override;
	void SaveWritableFirmware(uint32 idx, IVDStream& stream) override;
	ATDeviceFirmwareStatus GetFirmwareStatus() const override;

public:		// ATDeviceSIO
	void InitSIO(IATDeviceSIOManager *mgr) override;

public:	// IATSchedulerCallback
	void OnScheduledEvent(uint32 id) override;

public:	// IATDeviceRawSIO
	void OnCommandStateChanged(bool asserted) override;
	void OnMotorStateChanged(bool asserted) override;
	void OnBeginReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnTruncateByte() override;
	void OnSendReady() override;

protected:
	void Sync();

	void AddTransmitEdge(bool polarity);
	void AddCommandEdge(uint32 polarity);

	bool OnReadT0_835() const;
	bool OnReadT0_1030() const;
	bool OnReadT1() const;
	uint8 OnReadPort_835(uint8 addr, uint8 output);
	void OnWritePort_835(uint8 addr, uint8 output);
	uint8 OnReadPort_1030(uint8 addr, uint8 output);
	void OnWritePort_1030(uint8 addr, uint8 output);
	uint8 OnReadXRAM(uint8 addr) const;

	void OnModemReadReady();
	void TryReadModemByte();

	enum : uint32 {
		kEventId_DriveReceiveBit = kEventId_FirstCustom,
		kEventId_ModemReceiveBit,
		kEventId_ModemTransmitBit
	};

	// The master clock runs at 4.032MHz, so the 8048 machine cycle rate
	// is 4.032MHz / 15 = 268.8KHz. At 300 baud, this is 896 cycles per
	// bit.
	static constexpr uint32 kDeviceCyclesPerModemBit = 896;

	const bool mb835Mode;

	ATEvent *mpEventDriveReceiveBit = nullptr;
	ATEvent *mpEventModemReceiveBit = nullptr;
	ATEvent *mpEventModemTransmitBit = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;

	vdrefptr<ATModemEmulator> mpModem;

	ATFirmwareManager *mpFwMgr = nullptr;
	bool mbFirmwareUsable = false;

	uint32 mReceiveShiftRegister = 0;
	uint32 mReceiveTimingAccum = 0;
	uint32 mReceiveTimingStep = 0;
	uint32 mModemReceiveShifter = 0;
	uint32 mModemTransmitShifter = 0;

	bool mbCommandState = false;
	bool mbDirectReceiveOutput = true;
	bool mbDirectTransmitOutput = true;

	uint8 mP1 = 0xFF;
	uint8 mP2 = 0xFF;

	ATDiskDriveSerialBitTransmitQueue mSerialXmitQueue;
	ATDiskDriveSerialCommandQueue mSerialCmdQueue;
	
	alignas(2) uint8 mROM[2][0x802] = {};
	alignas(2) uint8 mXROM[0x2000] = {};
	
	class TargetProxy final : public IATDiskDriveDebugTargetProxy {
	public:
		TargetProxy(ATDevice1030Full& parent);

		std::pair<const uintptr *, const uintptr *> GetReadWriteMaps() const;
		void SetHistoryBuffer(ATCPUHistoryEntry *harray) override;
		uint32 GetHistoryCounter() const override;
		uint32 GetTime() const override;
		uint32 GetStepStackLevel() const override;
		void GetExecState(ATCPUExecState& state) const override;
		void SetExecState(const ATCPUExecState& state) override;

	private:
		ATDevice1030Full& mParent;
	};

	TargetProxy mTargetProxy;
	ATCoProc8048 mCoProc;

	ATDebugTargetBreakpointsImplT<0x1000> mBreakpointsImpl;

	uintptr mDebugReadMap[256];
	uintptr mDebugWriteMap[256];
	uint8 mDummyRead[256] {};
	uint8 mDummyWrite[256] {};
};

#endif
