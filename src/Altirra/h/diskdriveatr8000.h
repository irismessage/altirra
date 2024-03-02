//	Altirra - Atari 800/800XL/5200 emulator
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_DISKDRIVEATR8000_H
#define f_AT_DISKDRIVEATR8000_H

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/devicediskdrive.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceparent.h>
#include <at/atcore/deviceprinter.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcpu/coz80.h>
#include <at/atcpu/breakpoints.h>
#include <at/atcpu/history.h>
#include <at/atcpu/memorymap.h>
#include <at/atdebugger/breakpointsimpl.h>
#include <at/atdebugger/target.h>
#include <at/atcore/scheduler.h>
#include <at/atemulation/ctc.h>
#include "fdc.h"
#include "diskdrivefullbase.h"
#include "diskinterface.h"

class ATIRQController;

class ATDeviceDiskDriveATR8000 final : public ATDevice
	, public IATDeviceDiskDrive
	, public ATDeviceSIO
	, public IATDeviceButtons
	, public IATDevicePrinter
	, public IATDeviceParent
	, public ATDiskDriveDebugTargetControl
	, public IATDeviceRawSIO
{
public:
	ATDeviceDiskDriveATR8000();
	~ATDeviceDiskDriveATR8000();

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

	void DeviceReset();

public:		// IATDeviceDiskDrive
	void InitDiskDrive(IATDiskDriveManager *ddm) override;
	ATDeviceDiskDriveInterfaceClient GetDiskInterfaceClient(uint32 index) override;

public:		// ATDeviceSIO
	void InitSIO(IATDeviceSIOManager *mgr) override;

public:		// IATDeviceButtons
	uint32 GetSupportedButtons() const override;
	bool IsButtonDepressed(ATDeviceButton idx) const override;
	void ActivateButton(ATDeviceButton idx, bool state) override;

public:		// IATDevicePrinter
	void SetPrinterOutput(IATPrinterOutput *out) override;

public:
	IATDeviceBus *GetDeviceBus(uint32 index) override;

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
	void Sync();

	void AddTransmitEdge(bool polarity);

	uint8 OnDebugReadFDC(uint32 addr) const;
	uint8 OnReadFDC(uint32 addr);
	void OnWriteFDC(uint32 addr, uint8 val);
	uint8 OnReadPort(uint8 addr);
	void OnWritePort(uint8 addr, uint8 val);

	uint8 OnGetIntVector();
	void OnIntAck();
	void OnHaltChange(bool halted);
	void OnCTCInterrupt(sint32 vec);
	void OnFDCStep(bool inward);
	void OnFDCDrq(bool active);
	void OnFDCIrq(bool active);
	void OnFDCMotorChange(bool enabled);

	void PlayStepSound();
	void UpdateMemoryMap();
	void UpdateRotationStatus();
	void UpdateDiskStatus();
	void UpdateWriteProtectStatus();
	void UpdateCTCTrigger0Input();
	void UpdateIndexPulseMode();
	void UpdateFDCSpeed();
	void SelectDrives(uint8 mask);

	static constexpr uint32 kNumDrives = 4;

	enum : uint32 {
		kEventId_DriveReceiveBit = kEventId_FirstCustom,
		kEventId_DriveDiskChange0,		// 4 events, one per drive
	};

	ATEvent *mpEventDriveReceiveBit = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	IATDiskDriveManager *mpDiskDriveManager = nullptr;

	ATFirmwareManager *mpFwMgr = nullptr;
	bool mbFirmwareUsable = false;

	static constexpr uint32 kDiskChangeStepMS = 50;

	uint8 mDriveId = 0;

	uint32 mReceiveShiftRegister = 0;
	uint32 mReceiveTimingAccum = 0;
	uint32 mReceiveTimingStep = 0;

	bool mbCommandState = false;
	bool mbDirectTransmitOutput = true;
	bool mbTrigger0SelectData = false;

	// The ATR8000 can select multiple drives at once, and in fact the firmware does so
	// for the power-on restore. Therefore, we must also track the multi-select case.
	uint8 mSelectedDrives = 0;
	uint8 mAvailableDrives = 0;

	uint8 mPendingIntVector = 0;

	bool mbSoundsEnabled = false;
	bool mbIndexPulseEnabled = true;
	bool mbIndexPulseDisabled = false;
	bool mbFastClock = false;
	bool mbMotorRunning = false;
	bool mbROMEnabled = false;

	ATDiskDriveAudioPlayer mAudioPlayer;
	uint32 mLastStepSoundTime = 0;
	uint32 mLastStepPhase = 0;

	enum DriveType : uint32 {
		kDriveType_None,
		kDriveType_5_25,
		kDriveType_8,
	};

	struct Drive : public IATDiskInterfaceClient {
		ATDeviceDiskDriveATR8000 *mpParent = nullptr;
		uint32 mIndex = 0;

		ATDiskInterface *mpDiskInterface = nullptr;
		ATEvent *mpEventDriveDiskChange = nullptr;
		uint32 mCurrentTrack = 0;
		uint8 mDiskChangeState = 0;

		DriveType mType = kDriveType_None;

		void OnDiskChanged(bool mediaRemoved) override;
		void OnWriteModeChanged() override;
		void OnTimingModeChanged() override;
		void OnAudioModeChanged() override;
		bool IsImageSupported(const IATDiskImage& image) const override;
	} mDrives[kNumDrives];

	ATFDCEmulator mFDC;

	vdfastvector<ATCPUHistoryEntry> mHistory;

	ATDiskDriveSerialBitTransmitQueue mSerialXmitQueue;
	
	uint8 mPrinterData = 0;
	bool mbPrinterStrobeAsserted = false;
	IATPrinterOutput *mpPrinterOutput = nullptr;

	ATCTCEmulator mCTC;
	
	class SerialPort;
	SerialPort *mpSerialPort = nullptr;

	uintptr mReadMapBackup[256] = {};
	uintptr mWriteMapBackup[256] = {};

	VDALIGN(4) uint8 mRAM[0x10000] = {};
	VDALIGN(4) uint8 mROM[0x1000] = {};

	ATDiskDriveDebugTargetProxyT<ATCoProcZ80> mTargetProxy;

	ATCoProcZ80 mCoProc;

	ATDebugTargetBreakpointsImpl mBreakpointsImpl;
	ATDiskDriveFirmwareControl mFirmwareControl;
};

#endif
