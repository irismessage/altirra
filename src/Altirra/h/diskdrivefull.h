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

#ifndef f_AT_DISKDRIVEFULL_H
#define f_AT_DISKDRIVEFULL_H

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/devicediskdrive.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcpu/co6502.h>
#include <at/atcpu/breakpoints.h>
#include <at/atcpu/history.h>
#include <at/atcpu/memorymap.h>
#include <at/atdebugger/breakpointsimpl.h>
#include <at/atdebugger/target.h>
#include <at/atcore/scheduler.h>
#include <at/atemulation/riot.h>
#include "fdc.h"
#include "diskdrivefullbase.h"
#include "diskinterface.h"

class ATIRQController;

class ATDeviceDiskDriveFull final : public ATDevice
	, public IATDeviceFirmware
	, public IATDeviceDiskDrive
	, public ATDeviceSIO
	, public IATDeviceButtons
	, public ATDiskDriveDebugTargetControl
	, public IATDeviceRawSIO
	, public IATDiskInterfaceClient
{
public:
	enum DeviceType : uint8 {
		kDeviceType_810,
		kDeviceType_Happy810,
		kDeviceType_810Archiver,
		kDeviceType_1050,
		kDeviceType_USDoubler,
		kDeviceType_Speedy1050,
		kDeviceType_Happy1050,
		kDeviceType_SuperArchiver,
		kDeviceType_TOMS1050,
		kDeviceType_Tygrys1050,
		kDeviceType_1050Duplicator,
		kDeviceType_1050Turbo,
		kDeviceType_1050TurboII,
		kDeviceType_ISPlate,
		kDeviceType_810Turbo,
		kDeviceTypeCount,
	};

	ATDeviceDiskDriveFull(bool is1050, DeviceType deviceType);
	~ATDeviceDiskDriveFull();

	void *AsInterface(uint32 iid) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void Init() override;
	void Shutdown() override;
	uint32 GetComputerPowerOnDelay() const override;
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

public:		// IATDeviceButtons
	uint32 GetSupportedButtons() const;
	bool IsButtonDepressed(ATDeviceButton idx) const;
	void ActivateButton(ATDeviceButton idx, bool state);

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
	bool IsImageSupported(const IATDiskImage& image) const;

protected:
	void Sync();

	void AddTransmitEdge(bool polarity);

	void OnRIOTRegisterWrite(uint32 addr, uint8 val);

	void PlayStepSound();
	void UpdateRotationStatus();
	void UpdateDiskStatus();
	void UpdateWriteProtectStatus();
	void UpdateROMBank();
	void UpdateROMBankSuperArchiver();
	void UpdateROMBankHappy810();
	void UpdateROMBankHappy1050();
	void UpdateROMBank1050Turbo();
	void OnToggleFastSlow();
	void ClearWriteProtect();
	void OnToggleWriteProtect();
	void OnWriteEnabled();
	void UpdateWriteProtectOverride();
	void UpdateSlowSwitch();
	void UpdateAutoSpeed();

	enum {
		kEventId_DriveReceiveBit = kEventId_FirstCustom,
		kEventId_DriveDiskChange,
	};

	ATEvent *mpEventDriveReceiveBit = nullptr;
	ATEvent *mpEventDriveDiskChange = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	IATDiskDriveManager *mpDiskDriveManager = nullptr;
	ATDiskInterface *mpDiskInterface = nullptr;

	ATFirmwareManager *mpFwMgr = nullptr;

	static constexpr uint32 kDiskChangeStepMS = 50;

	uint32 mReceiveShiftRegister = 0;
	uint32 mReceiveTimingAccum = 0;
	uint32 mReceiveTimingStep = 0;

	uint32 mCurrentTrack = 0;
	bool mbROMBankAlt = false;
	uint8 mROMBank = 0;

	const bool mb1050;
	const DeviceType mDeviceType;
	uint8 mDriveId = 0;

	bool mbFirmwareUsable = false;
	bool mbSoundsEnabled = false;
	bool mbSlowSwitch = false;
	bool mbFastSlowToggle = false;
	bool mbWPToggle = false;
	bool mbWPEnable = false;
	bool mbWPDisable = false;
	bool mbHappy810AutoSpeed = false;

	static constexpr float kDefaultAutoSpeedRate = 266.0f;
	float mHappy810AutoSpeedRate = kDefaultAutoSpeedRate;

	ATDiskDriveAudioPlayer mAudioPlayer;

	uint32 mLastStepSoundTime = 0;
	uint32 mLastStepPhase = 0;
	uint8 mDiskChangeState = 0;

	ATCoProcReadMemNode mReadNodeFDCRAM {};
	ATCoProcReadMemNode mReadNodeRIOTRAM {};
	ATCoProcReadMemNode mReadNodeRIOTRegisters {};
	ATCoProcReadMemNode mReadNodeROMBankSwitch {};
	ATCoProcReadMemNode mReadNodeFastSlowToggle {};
	ATCoProcReadMemNode mReadNodeWriteProtectToggle {};
	ATCoProcWriteMemNode mWriteNodeFDCRAM {};
	ATCoProcWriteMemNode mWriteNodeRIOTRAM {};
	ATCoProcWriteMemNode mWriteNodeRIOTRegisters {};
	ATCoProcWriteMemNode mWriteNodeROMBankSwitch {};
	ATCoProcWriteMemNode mWriteNodeFastSlowToggle {};
	ATCoProcWriteMemNode mWriteNodeWriteProtectToggle {};

	ATCoProc6502 mCoProc;

	struct TargetProxy final : public ATDiskDriveDebugTargetProxyBaseT<ATCoProc6502> {
		uint32 GetTime() const override {
			return ATSCHEDULER_GETTIME(mpDriveScheduler);
		}

		ATScheduler *mpDriveScheduler;
	} mTargetProxy;

	ATFDCEmulator mFDC;
	ATRIOT6532Emulator mRIOT;

	vdfastvector<ATCPUHistoryEntry> mHistory;

	ATDiskDriveSerialBitTransmitQueue mSerialXmitQueue;

	VDALIGN(4) uint8 mRAM[0x4100] = {};
	VDALIGN(4) uint8 mROM[0x4000] = {};
	VDALIGN(4) uint8 mDummyWrite[0x100] = {};
	VDALIGN(4) uint8 mDummyRead[0x100] = {};
	
	ATDebugTargetBreakpointsImpl mBreakpointsImpl;
};

#endif
