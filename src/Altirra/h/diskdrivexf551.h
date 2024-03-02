//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2016 Avery Lee
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

#ifndef f_AT_DISKDRIVEXF551_H
#define f_AT_DISKDRIVEXF551_H

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
#include "fdc.h"
#include "diskdrivefullbase.h"
#include "diskinterface.h"

class ATIRQController;

class ATDeviceDiskDriveXF551 final : public ATDevice
	, public IATDeviceFirmware
	, public IATDeviceDiskDrive
	, public ATDeviceSIO
	, public ATDiskDriveDebugTargetControl
	, public IATDeviceRawSIO
	, public IATDiskInterfaceClient
{
public:
	ATDeviceDiskDriveXF551();
	~ATDeviceDiskDriveXF551();

	void *AsInterface(uint32 iid) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettingsBlurb(VDStringW& buf) override;
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

public:		// IATDeviceDiskDrive
	void InitDiskDrive(IATDiskDriveManager *ddm) override;
	ATDeviceDiskDriveInterfaceClient GetDiskInterfaceClient(uint32 index) override;

public:		// ATDeviceSIO
	void InitSIO(IATDeviceSIOManager *mgr) override;

public:	// IATSchedulerCallback
	void OnScheduledEvent(uint32 id) override;

public:	// IATDeviceRawSIO
	void OnCommandStateChanged(bool asserted) override;
	void OnMotorStateChanged(bool asserted) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnSendReady() override;

public:	// IATDiskInterfaceClient
	void OnDiskChanged(bool mediaRemoved) override;
	void OnWriteModeChanged() override;
	void OnTimingModeChanged() override;
	void OnAudioModeChanged() override;
	bool IsImageSupported(const IATDiskImage& image) const override;

protected:
	void Sync();

	void AddTransmitEdge(bool polarity);

	void AddCommandEdge(uint32 polarity);

	void OnFDCStep(bool inward);
	void OnFDCMotorChange(bool enabled);
	bool OnReadT0() const;
	bool OnReadT1() const;
	uint8 OnReadPort(uint8 addr, uint8 output);
	void OnWritePort(uint8 addr, uint8 output);
	uint8 OnReadXRAM();
	void OnWriteXRAM(uint8 val);

	void PlayStepSound();
	void UpdateRotationStatus();
	void UpdateDiskStatus();
	void UpdateWriteProtectStatus();

	enum : uint32 {
		kEventId_DriveReceiveBit = kEventId_FirstCustom,
		kEventId_DriveDiskChange,
	};

	ATEvent *mpEventDriveReceiveBit = nullptr;
	ATEvent *mpEventDriveDiskChange = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	IATDiskDriveManager *mpDiskDriveManager = nullptr;
	ATDiskInterface *mpDiskInterface = nullptr;

	ATFirmwareManager *mpFwMgr = nullptr;
	bool mbFirmwareUsable = false;

	static constexpr uint32 kDiskChangeStepMS = 50;

	uint32 mReceiveShiftRegister = 0;
	uint32 mReceiveTimingAccum = 0;
	uint32 mReceiveTimingStep = 0;

	bool mbCommandState = false;
	bool mbDriveCommandState = false;
	bool mbDirectReceiveOutput = true;
	bool mbDirectTransmitOutput = true;

	uint32 mCurrentTrack = 0;
	uint8 mActiveStepperPhases = 0;

	bool mbSoundsEnabled = false;
	bool mbForcedIndexPulse = false;
	bool mbMotorRunning = false;
	bool mbExtendedRAMEnabled = false;

	ATDiskDriveAudioPlayer mAudioPlayer;
	uint32 mLastStepSoundTime = 0;
	uint32 mLastStepPhase = 0;
	uint8 mDiskChangeState = 0;
	uint8 mDriveId = 0;

	ATFDCEmulator mFDC;

	ATDiskDriveSerialBitTransmitQueue mSerialXmitQueue;
	ATDiskDriveSerialCommandQueue mSerialCmdQueue;
	
	alignas(2) uint8 mROM[2][0x802] = {};
	
	class ATDiskDriveDebugTargetProxyXF551 final : public IATDiskDriveDebugTargetProxy {
	public:
		ATDiskDriveDebugTargetProxyXF551(ATDeviceDiskDriveXF551& parent);

		std::pair<const uintptr *, const uintptr *> GetReadWriteMaps() const;
		void SetHistoryBuffer(ATCPUHistoryEntry *harray) override;
		uint32 GetHistoryCounter() const override;
		uint32 GetTime() const override;
		uint32 GetStepStackLevel() const override;
		void GetExecState(ATCPUExecState& state) const override;
		void SetExecState(const ATCPUExecState& state) override;

	private:
		ATDeviceDiskDriveXF551& mParent;
	};

	ATDiskDriveDebugTargetProxyXF551 mTargetProxy;
	ATCoProc8048 mCoProc;

	ATDebugTargetBreakpointsImplT<0x1000> mBreakpointsImpl;

	uintptr mDebugReadMap[256];
	uintptr mDebugWriteMap[256];
	uint8 mDummyRead[256] {};
	uint8 mDummyWrite[256] {};
};

#endif
