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

#ifndef f_AT_DISKDRIVEPERCOM_H
#define f_AT_DISKDRIVEPERCOM_H

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/devicediskdrive.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcore/scheduler.h>
#include <at/atcpu/co6809.h>
#include <at/atcpu/breakpoints.h>
#include <at/atcpu/history.h>
#include <at/atcpu/memorymap.h>
#include <at/atdebugger/breakpointsimpl.h>
#include <at/atdebugger/target.h>
#include <at/atemulation/acia6850.h>
#include "fdc.h"
#include "irqcontroller.h"
#include "pia.h"
#include "diskdrivefullbase.h"
#include "diskinterface.h"

class ATIRQController;

class ATDeviceDiskDrivePercom final : public ATDevice
	, public IATDeviceDiskDrive
	, public ATDeviceSIO
	, public ATDiskDriveDebugTargetControl
	, public IATDeviceRawSIO
{
public:
	ATDeviceDiskDrivePercom(bool at88);
	~ATDeviceDiskDrivePercom();

	void *AsInterface(uint32 iid) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void GetSettings(ATPropertySet & settings) override;
	bool SetSettings(const ATPropertySet & settings) override;
	void Init() override;
	void Shutdown() override;
	uint32 GetComputerPowerOnDelay() const override;
	void WarmReset() override;
	void ComputerColdReset() override;
	void PeripheralColdReset() override;

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

public:
	void OnDiskChanged(uint32 index, bool mediaRemoved);
	void OnWriteModeChanged(uint32 index);
	void OnTimingModeChanged(uint32 index);
	void OnAudioModeChanged(uint32 index);

protected:
	void Sync() override final;

	uint8 OnHardwareDebugReadAT(uint32 addr);
	uint8 OnHardwareReadAT(uint32 addr);
	void OnHardwareWriteAT(uint32 addr, uint8 value);

	uint8 OnHardwareDebugReadRFD(uint32 addr);
	uint8 OnHardwareReadRFD(uint32 addr);
	void OnHardwareWriteRFD(uint32 addr, uint8 value);

	void OnFDCDataRequest(bool asserted);
	void OnFDCInterruptRequest(bool asserted);
	void OnFDCStep(bool inward);

	void OnACIATransmit(uint8 v, uint32 cyclesPerBit);
	void OnPIAPortBChanged(uint32 outputState);

	void SetMotorEnabled(bool enabled);
	void PlayStepSound();
	void UpdateRotationStatus();
	void UpdateDiskStatus();
	
	void SelectDrive(int index);

	void UpdateNmi();
	void SetNmiTimeout();

	static constexpr uint32 kNumDrives = 4;

	enum : uint32 {
		kEventId_DriveTimeout = kEventId_FirstCustom,
	};

	ATEvent *mpEventDriveTimeout = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	IATDiskDriveManager *mpDiskDriveManager = nullptr;

	bool mbCommandState = false;
	bool mbNmiState = false;
	bool mbNmiTimeoutEnabled = false;
	bool mbNmiTimeout = false;

	int mSelectedDrive = -1;
	uint8 mAvailableDrives = 0;

	bool mbSoundsEnabled = false;
	bool mbMotorRunning = false;
	uint32 mLastStepSoundTime = 0;
	uint32 mLastStepPhase = 0;
	uint8 mDriveId = 0;
	bool mbIsAT88 = false;
	bool mbIsAT88DoubleDensity = true;
	bool mbSelectFDC2 = false;

	ATDiskDriveAudioPlayer mAudioPlayer;

	enum DriveType : uint32 {
		kDriveType_None,
		kDriveType_5_25_40Track,
		kDriveType_5_25_80Track,
	};

	struct Drive : public IATDiskInterfaceClient {
		ATDeviceDiskDrivePercom *mpParent = nullptr;
		uint32 mIndex = 0;

		ATDiskInterface *mpDiskInterface = nullptr;
		uint32 mCurrentTrack = 0;
		uint32 mMaxTrack = 0;

		DriveType mType = kDriveType_None;

		ATDiskDriveChangeHandler mDiskChangeHandler;

		void OnDiskChanged(bool mediaRemoved) override;
		void OnWriteModeChanged() override;
		void OnTimingModeChanged() override;
		void OnAudioModeChanged() override;
		bool IsImageSupported(const IATDiskImage& image) const override;
	} mDrives[kNumDrives];

	ATCoProcReadMemNode mReadNodeHardware {};
	ATCoProcWriteMemNode mWriteNodeHardware {};

	ATFDCEmulator mFDC;
	ATFDCEmulator mFDC2;
	ATACIA6850Emulator mACIA;
	ATPIAEmulator mPIA;
	ATIRQController mIRQController;

	ATDiskDriveSerialByteTransmitQueue mSerialXmitQueue;
	ATDiskDriveSerialCommandQueue mSerialCmdQueue;

	ATDiskDriveDebugTargetProxyT<ATCoProc6809> mTargetHistoryProxy;
	
	ATCoProc6809 mCoProc;

	uint8 mROM[0x800] = {};
	uint8 mRAM[0x400] = {};
	uint8 mDummyRead[256] = {};
	uint8 mDummyWrite[256] = {};

	ATDiskDriveFirmwareControl mFirmwareControl;
	ATDebugTargetBreakpointsImpl mBreakpointsImpl;

	static const uint8 kPIALookup[4];
};

#endif
