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

#ifndef f_AT_1029FULL_H
#define f_AT_1029FULL_H

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/devicediskdrive.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceprinter.h>
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
#include "printerbase.h"

class ATIRQController;
class ATModemEmulator;

class ATDevice1029Full final : public ATDevice
	, public IATDeviceFirmware
	, public ATDeviceSIO
	, public ATDiskDriveDebugTargetControl
	, public IATDeviceRawSIO
{
public:
	ATDevice1029Full();
	~ATDevice1029Full();

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
	void OnReadyStateChanged(bool asserted) override;
	void OnCommandStateChanged(bool asserted) override;
	void OnMotorStateChanged(bool asserted) override;
	void OnBeginReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnTruncateByte() override;
	void OnSendReady() override;

protected:
	void Sync();

	void AddCommandEdge(uint32 polarity);
	void AddTransmitEdge(bool polarity);

	bool OnReadT0() const;
	bool OnReadT1() const;
	void OnT1CounterEnabled(bool enabled);
	uint8 OnReadPort(uint8 addr, uint8 output);
	void OnWritePort(uint8 addr, uint8 output);

	uint8 OnReadXRAM(uint8 addr) const;
	void OnWriteXRAM(uint8 addr, uint8 v);

	void UpdateHeadPosition();
	uint32 ComputeHeadPosition() const;
	uint32 ComputeHeadPosition(uint32 t) const;

	void UpdateDotPosition();
	uint32 ComputeDotPosition() const;
	uint32 ComputeDotPosition(uint32 t) const;
	void UpdateDotEvent();

	static constexpr uint32 kHomeSensorCycles = 1000;	// total guess
	static constexpr float kVerticalDotPitchMM = 0.44704f;

	// Dot clock estimated from audio recordings of a 1029 printing.
	static constexpr uint32 kCyclesPerDotPulse = 245;
	static constexpr uint32 kCyclesPerDotPulseLow = 60;
	static constexpr uint32 kCyclesPerDotGroup = 2222;

	struct DotSignalState {
		uint32 mDriveTicksToNextFallingEdge = 0;
		bool mbCurrentDotSignal = false;
	};

	DotSignalState GetDotSignalState() const;

	void UpdateHeadSounds();

	enum : uint32 {
		kEventId_DriveReceiveBit = kEventId_FirstCustom,
		kEventId_DriveDotFallingEdge,
		kEventId_DriveHeadRetract,
	};

	ATEvent *mpEventDriveReceiveBit = nullptr;
	ATEvent *mpEventDriveDotFallingEdge = nullptr;
	ATEvent *mpEventDriveHeadRetract = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	IATAudioMixer *mpAudioMixer = nullptr;

	ATFirmwareManager *mpFwMgr = nullptr;
	bool mbFirmwareUsable = false;

	uint32 mReceiveShiftRegister = 0;
	uint32 mReceiveTimingAccum = 0;
	uint32 mReceiveTimingStep = 0;

	bool mbCommandState = false;
	bool mbDirectReceiveOutput = true;
	bool mbDirectTransmitOutput = true;

	uint8 mP1 = 0xFF;
	uint8 mP2 = 0xFF;

	uint8 mFeedPosition = 0;
	uint32 mPrintHeadPosition = 0;
	uint32 mLastHeadUpdateTime = 0;
	uint32 mPrintDotPosition = 0;
	uint32 mLastDotUpdateTime = 0;
	bool mbDotCountingEnabled = false;

	vdrefptr<IATPrinterGraphicalOutput> mpPrinterGraphicalOutput;

	ATDiskDriveSerialBitTransmitQueue mSerialXmitQueue;
	ATDiskDriveSerialCommandQueue mSerialCmdQueue;
		
	class TargetProxy final : public IATDiskDriveDebugTargetProxy {
	public:
		TargetProxy(ATDevice1029Full& parent);

		std::pair<const uintptr *, const uintptr *> GetReadWriteMaps() const;
		void SetHistoryBuffer(ATCPUHistoryEntry *harray) override;
		uint32 GetHistoryCounter() const override;
		uint32 GetTime() const override;
		uint32 GetStepStackLevel() const override;
		void GetExecState(ATCPUExecState& state) const override;
		void SetExecState(const ATCPUExecState& state) override;

	private:
		ATDevice1029Full& mParent;
	};

	TargetProxy mTargetProxy;
	ATCoProc8048 mCoProc;

	ATPrinterSoundSource mPrinterSoundSource;

	alignas(2) uint8 mXRAM[1024] {};
	alignas(2) uint8 mROM[2][0x802] = {};
	ATDebugTargetBreakpointsImplT<0x1000> mBreakpointsImpl;

	uintptr mDebugReadMap[256];
	uintptr mDebugWriteMap[256];
	uint8 mDummyRead[256] {};
	uint8 mDummyWrite[256] {};
};

#endif
