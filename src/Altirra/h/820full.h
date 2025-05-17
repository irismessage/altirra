//	Altirra - Atari 800/800XL/5200 emulator
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
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef f_AT_820FULL_H
#define f_AT_820FULL_H

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/audiomixer.h>
#include <at/atcore/audiosource.h>
#include <at/atcore/devicediskdrive.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceprinter.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcore/scheduler.h>
#include <at/atcpu/co6502.h>
#include <at/atcpu/breakpoints.h>
#include <at/atcpu/history.h>
#include <at/atcpu/memorymap.h>
#include <at/atdebugger/breakpointsimpl.h>
#include <at/atdebugger/target.h>
#include <at/atemulation/riot.h>
#include "diskdrivefullbase.h"
#include "diskinterface.h"
#include "printerbase.h"

class ATIRQController;
class ATModemEmulator;

class ATDevice820Full final : public ATDevice
	, public IATDeviceFirmware
	, public ATDeviceSIO
	, public ATDiskDriveDebugTargetControl
	, public IATDeviceRawSIO
{
public:
	ATDevice820Full();
	~ATDevice820Full();

	void *AsInterface(uint32 iid) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
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
	void OnReadyStateChanged(bool asserted) override;
	void OnBeginReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnTruncateByte() override;
	void OnSendReady() override;

protected:
	uint8 OnRIOTMemoryRead(uint32 addr) const;
	void OnRIOTMemoryWrite(uint32 addr, uint8 val);
	uint8 OnRIOTRegistersDebugRead(uint32 addr) const;
	uint8 OnRIOTRegistersRead(uint32 addr);
	void OnRIOTRegistersWrite(uint32 addr, uint8 val);

	// Timing per service manual:
	// - 160-190ms from motor on / home to print ready
	// - 310ms print area
	// - 800ms from motor on to home / print not ready
	static constexpr uint32 kDriveCyclesLinePeriod = 800000;
	static constexpr uint32 kDriveCyclesLinePeriodHalf = kDriveCyclesLinePeriod / 2;
	static constexpr uint32 kDriveCyclesLinePrintReady = 175000;
	static constexpr uint32 kDriveCyclesLineStartOffset = 145000;

	void UpdateDriveMotorState();

	void Sync();

	void AddTransmitEdge(bool polarity);
	void AddCommandEdge(uint32 polarity);

	enum : uint32 {
		kEventId_DriveReceiveBit = kEventId_FirstCustom,
		kEventId_DriveMotor,
		kEventId_ModemReceiveBit,
		kEventId_ModemTransmitBit
	};

	ATEvent *mpEventDriveReceiveBit = nullptr;
	ATEvent *mpEventDriveMotor = nullptr;
	ATEvent *mpEventModemReceiveBit = nullptr;
	ATEvent *mpEventModemTransmitBit = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	IATAudioMixer *mpAudioMixer = nullptr;

	ATFirmwareManager *mpFwMgr = nullptr;
	bool mbFirmwareUsable = false;

	vdrefptr<IATPrinterGraphicalOutput> mpPrinterGraphicalOutput;

	uint32 mReceiveShiftRegister = 0;
	uint32 mReceiveTimingAccum = 0;
	uint32 mReceiveTimingStep = 0;
	uint32 mModemReceiveShifter = 0;
	uint32 mModemTransmitShifter = 0;

	bool mbCommandState = false;

	uint32 mLineDriveTimeOffset = 0;
	uint32 mLineDriveTimeLast = 0;
	bool mbMotorRunning = false;

	ATCoProcReadMemNode mRIOTMemoryReadHandler;
	ATCoProcWriteMemNode mRIOTMemoryWriteHandler;
	ATCoProcReadMemNode mRIOTRegistersReadHandler;
	ATCoProcWriteMemNode mRIOTRegistersWriteHandler;

	ATDiskDriveSerialBitTransmitQueue mSerialXmitQueue;
	ATDiskDriveSerialCommandQueue mSerialCmdQueue;
	
	alignas(2) uint8 mROM[0x800] = {};
	alignas(2) uint8 mRIOTRAM[128] {};
	
	class TargetProxy final : public ATDiskDriveDebugTargetProxyBaseT<ATCoProc6502> {
	public:
		TargetProxy(ATDevice820Full& parent);

		uint32 GetTime() const override;

	private:
		ATDevice820Full& mParent;
	};

	TargetProxy mTargetProxy;
	ATRIOT6532Emulator mRIOT;
	ATCoProc6502 mCoProc;

	ATPrinterSoundSource mPrinterSoundSource;

	ATDebugTargetBreakpointsImplT<0x10000> mBreakpointsImpl;

	uint8 mDummyRead[256] {};
	uint8 mDummyWrite[256] {};
};

#endif

