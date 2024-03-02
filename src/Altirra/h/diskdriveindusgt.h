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

#ifndef f_AT_DISKDRIVEINDUSGT_H
#define f_AT_DISKDRIVEINDUSGT_H

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/devicediskdrive.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcpu/coz80.h>
#include <at/atcpu/breakpoints.h>
#include <at/atcpu/history.h>
#include <at/atcpu/memorymap.h>
#include <at/atdebugger/breakpointsimpl.h>
#include <at/atdebugger/target.h>
#include <at/atcore/scheduler.h>
#include "fdc.h"
#include "diskdrivefullbase.h"
#include "diskinterface.h"
#include "audiorawsource.h"

class ATIRQController;

class ATDeviceDiskDriveIndusGT final : public ATDevice
	, public IATDeviceDiskDrive
	, public ATDeviceSIO
	, public IATDeviceAudioOutput
	, public IATDeviceButtons
	, public ATDiskDriveDebugTargetControl
	, public IATDeviceRawSIO
	, public IATDiskInterfaceClient
{
public:
	ATDeviceDiskDriveIndusGT();
	~ATDeviceDiskDriveIndusGT();

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

public:		// IATDeviceDiskDrive
	void InitDiskDrive(IATDiskDriveManager *ddm) override;
	ATDeviceDiskDriveInterfaceClient GetDiskInterfaceClient(uint32 index) override;

public:		// ATDeviceSIO
	void InitSIO(IATDeviceSIOManager *mgr) override;

public:		// IATDeviceAudioOutput
	void InitAudioOutput(IATAudioMixer *mixer) override;

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
	bool IsImageSupported(const IATDiskImage& image) const override;

protected:
	void Sync();

	void AddTransmitEdge(uint32 polarity);

	uint8 OnReadStatus1(uint8 port);
	uint8 OnDebugReadStatus1() const;
	uint8 OnReadStatus2() const;
	uint8 OnReadControl();
	void OnWriteControl(uint8 val);
	uint8 TranslateLED(uint8 val);
	uint8 OnReadControlLED1();
	void OnWriteControlLED1(uint8 val);
	uint8 OnReadControlLED2();
	void OnWriteControlLED2(uint8 val);
	uint8 OnDebugReadFDC(uint32 addr) const;
	uint8 OnReadFDC(uint32 addr);
	void OnWriteFDC(uint32 addr, uint8 val);
	void OnAccessPort(uint8 addr);

	void PlayStepSound();
	void UpdateMemoryMap();
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

	static constexpr uint32 kClockDivisorNTSC = 229;
	static constexpr uint32 kClockDivisorPAL = 227;
	static constexpr uint32 kDiskChangeStepMS = 50;

	uint32 mLEDState = 0;

	uint8 mDriveId = 0;
	uint8 mStatus1 = 0;
	uint8 mStatus2 = 0;
	uint8 mStatus1ButtonsHeld = 0;

	uint64 mStatus1CPMHoldStart = 0;
	uint64 mStatus1ChangeDensityHoldStart = 0;

	uint32 mReceiveShiftRegister = 0;
	uint32 mReceiveTimingAccum = 0;
	uint32 mReceiveTimingStep = 0;

	bool mbCommandState = false;
	bool mbDirectReceiveOutput = true;
	bool mbDirectTransmitOutput = true;

	uint32 mCurrentTrack = 0;
	uint8 mActiveStepperPhases = 0;

	bool mbFirmwareUsable = false;
	bool mbSoundsEnabled = false;
	bool mbForcedIndexPulse = false;
	bool mbMotorRunning = false;
	bool mbExtendedRAMEnabled = false;
	uint32 mLastStepSoundTime = 0;
	uint32 mLastStepPhase = 0;
	uint8 mDiskChangeState = 0;

	ATCoProcReadMemNode mReadNodeStat1 {};
	ATCoProcReadMemNode mReadNodeStat2 {};
	ATCoProcReadMemNode mReadNodeControl {};
	ATCoProcReadMemNode mReadNodeControlLED1 {};
	ATCoProcReadMemNode mReadNodeControlLED2 {};
	ATCoProcReadMemNode mReadNodeFDC {};
	ATCoProcWriteMemNode mWriteNodeControl {};
	ATCoProcWriteMemNode mWriteNodeControlLED1 {};
	ATCoProcWriteMemNode mWriteNodeControlLED2 {};
	ATCoProcWriteMemNode mWriteNodeFDC {};

	ATDiskDriveAudioPlayer mAudioPlayer;
	ATAudioRawSource mAudioRawSource;

	ATFDCEmulator mFDC;

	vdfastvector<ATCPUHistoryEntry> mHistory;

	ATDiskDriveSerialBitTransmitQueue mSerialXmitQueue;
	ATDiskDriveSerialCommandQueue mSerialCmdQueue;

	ATDiskDriveDebugTargetProxyT<ATCoProcZ80> mTargetProxy;

	uintptr mReadMapBackup[256];
	uintptr mWriteMapBackup[256];

	VDALIGN(4) uint8 mRAM[0x10800] = {};
	VDALIGN(4) uint8 mROM[0x1000] = {};
	VDALIGN(4) uint8 mDummyWrite[0x100] = {};
	VDALIGN(4) uint8 mDummyRead[0x100] = {};

	ATCoProcZ80 mCoProc;

	ATDiskDriveFirmwareControl mFirmwareControl;
	ATDebugTargetBreakpointsImpl mBreakpointsImpl;
};

#endif
