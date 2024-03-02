//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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

#ifndef AT_DISK_H
#define AT_DISK_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/time.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/devicesio.h>
#include <at/atcore/scheduler.h>
#include <at/atio/diskimage.h>
#include "diskinterface.h"

class ATAudioSamplePlayer;

class ATCPUEmulatorMemory;
class IVDRandomAccessStream;
class IATDeviceIndicatorManager;
class IATUIRenderer;
class IATObjectState;
class IATAudioSoundGroup;

struct ATTraceContext;
class ATTraceChannelFormatted;

enum ATMediaWriteMode : uint8;
enum class ATSoundId : uint32;

struct ATDiskProfile;

enum ATDiskEmulationMode : uint8 {
	kATDiskEmulationMode_Generic,
	kATDiskEmulationMode_FastestPossible,
	kATDiskEmulationMode_810,
	kATDiskEmulationMode_1050,
	kATDiskEmulationMode_XF551,
	kATDiskEmulationMode_USDoubler,
	kATDiskEmulationMode_Speedy1050,
	kATDiskEmulationMode_IndusGT,
	kATDiskEmulationMode_Happy1050,
	kATDiskEmulationMode_1050Turbo,
	kATDiskEmulationMode_Generic57600,
	kATDiskEmulationMode_Happy810,
	kATDiskEmulationModeCount
};

class ATDiskEmulator final
	: public IATDeviceSIO
	, public IATSchedulerCallback
	, public IATDiskInterfaceClient
{
public:
	ATDiskEmulator();
	~ATDiskEmulator();

	void Init(int unit, ATDiskInterface *dif, ATScheduler *sched, ATScheduler *slowsched, ATAudioSamplePlayer *mixer);
	void Shutdown();

	void Rename(int unit);

	bool IsEnabled() const { return mbEnabled; }

	void SetEnabled(bool enabled);

	void SetEmulationMode(ATDiskEmulationMode mode);
	ATDiskEmulationMode GetEmulationMode() { return mEmuMode; }

	void Reset();

	void SetForcedPhantomSector(uint16 sector, uint8 index, int order);
	int GetForcedPhantomSector(uint16 sector, uint8 index);

	void SetTraceContext(ATTraceContext *context);

	void SaveState(IATObjectState **pp) const;
	void LoadState(const IATObjectState& state);

public:
	void OnScheduledEvent(uint32 id) override;

public:
	void InitSIO(IATDeviceSIOManager *mgr) override;
	CmdResponse OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) override;
	void OnSerialAbortCommand() override;
	void OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) override;
	void OnSerialFence(uint32 id) override; 
	CmdResponse OnSerialAccelCommand(const ATDeviceSIORequest& request) override;

public:		// IATDiskInterfaceClient
	void OnDiskChanged(bool mediaRemoved) override;
	void OnWriteModeChanged() override;
	void OnTimingModeChanged() override;
	void OnAudioModeChanged() override;
	bool IsImageSupported(const IATDiskImage& image) const override;

protected:
	void UpdateAccelTimeSkew();
	void InitSectorInfoArrays();
	void SetupTransferSpeed(bool highSpeed);
	void BeginTransferACKCmd();
	void BeginTransferACK();
	void BeginTransferComplete();
	void BeginTransferError();
	void BeginTransferNAKCommand();
	void BeginTransferNAKData();
	void SendResult(bool successful, uint32 length);
	void Send(uint32 length);
	void BeginReceive(uint32 len);
	void WarpOrDelay(uint32 cycles, uint32 minCycles = 0);
	void WarpOrDelayFromStopBit(uint32 cycles);
	void Wait(uint32 nextState);
	void EndCommand();
	void AbortCommand();
	uint32 GetUpdatedRotationalCounter() const;
	void UpdateRotationalCounter();

	void ProcessCommand();
	void ProcessUnsupportedCommand();

	void ProcessCommandStatus();
	bool ProcessCommandReadWriteCommon(bool isWrite);
	void ProcessCommandRead();
	void ProcessCommandWrite();
	void ProcessCommandReadPERCOMBlock();
	void ProcessCommandWritePERCOMBlock();
	void ProcessCommandHappy();
	void ProcessCommandHappyQuiet();
	void ProcessCommandHappyRAMTest();
	void ProcessCommandHappyHeadPosTest();
	void ProcessCommandHappyRPMTest();

	void ProcessCommandExecuteIndusGT();
	void ProcessCommandGetHighSpeedIndex();
	void ProcessCommandFormat();

	void ProcessUnhandledCommandState();

	void ComputeGeometry();
	void ComputePERCOMBlock();
	void ComputeSupportedProfile();
	bool SetPERCOMData(const uint8 *data);
	void TurnOffMotor();
	bool TurnOnMotor(uint32 delay = 0);
	void ExtendMotorTimeoutBy(uint32 additionalDelay);
	void ExtendMotorTimeoutTo(uint32 delay);
	void SetMotorEvent();
	void PlaySeekSound(uint32 initialDelay, uint32 trackCount);

	IATDeviceSIOManager *mpSIOMgr = nullptr;
	ATScheduler *mpScheduler = nullptr;
	ATScheduler *mpSlowScheduler = nullptr;
	int		mUnit = 0;

	ATEvent		*mpMotorOffEvent = nullptr;
	uint32	mMotorOffTime = 0;

	uint32	mLastRotationUpdateCycle = 0;
	uint32	mLastAccelTimeSkew = 0;
	bool	mbReceiveChecksumOK = false;
	uint32	mTransferLength = 0;
	uint32	mTransferCompleteRotPos = 0;
	uint8	mFDCStatus = 0;
	uint8	mOriginalDevice = 0;
	uint8	mOriginalCommand = 0;
	uint8	mActiveCommand = 0;
	bool	mbActiveCommandHighSpeed = false;
	bool	mbActiveCommandWait = false;
	uint32	mActiveCommandState = 0;
	uint32	mActiveCommandSector = 0;
	sint32	mActiveCommandPhysSector = 0;
	float	mActiveCommandStartRotPos = 0;
	uint32	mActiveCommandStartTime = 0;
	uint8	mCustomCodeState = 0;
	uint32	mPhantomSectorCounter = 0;
	uint32	mRotationalCounter = 0;
	uint32	mRotations = 0;
	uint32	mCurrentTrack = 0;
	uint32	mSectorsPerTrack = 0;
	uint32	mTrackCount = 0;
	uint32	mSideCount = 0;
	bool	mbMFM = false;
	bool	mbHighDensity = false;

	bool	mbFormatEnabled = false;
	bool	mbWriteEnabled = false;

	bool	mbCommandValid = false;
	bool	mbCommandFrameHighSpeed = false;
	bool	mbEnabled = false;
	bool	mbDriveSoundsEnabled = false;
	bool	mbAccurateSectorTiming = false;
	bool	mbAccurateSectorPrediction = false;
	bool	mbLastOpError = false;

	int		mBootSectorCount = 0;
	int		mTotalSectorCount = 0;
	int		mSectorSize = 0;
	uint32	mLastSector = 0;

	uint8	mPERCOM[12] = {};
	int		mFormatSectorSize = 0;
	int		mFormatSectorCount = 0;
	int		mFormatBootSectorCount = 0;

	ATDiskEmulationMode mEmuMode = kATDiskEmulationMode_Generic;

	const ATDiskProfile *mpProfile = nullptr;
	uint32	mCyclesPerSIOBitCurrent = 1;
	uint32	mCyclesPerSIOByteCurrent = 1;

	ATAudioSamplePlayer *mpAudioSyncMixer = nullptr;
	vdrefptr<IATAudioSoundGroup> mpRotationSoundGroup;
	vdrefptr<IATAudioSoundGroup> mpStepSoundGroup;

	ATDiskInterface *mpDiskInterface = nullptr;
	ATTraceContext *mpTraceContext = nullptr;
	ATTraceChannelFormatted *mpTraceChannel = nullptr;

	struct ExtPhysSector {
		sint8	mForcedOrder;
	};

	typedef vdfastvector<ExtPhysSector> ExtPhysSectors;
	ExtPhysSectors mExtPhysSectors;

	struct ExtVirtSector {
		uint32	mPhantomSectorCounter;
	};
	typedef vdfastvector<ExtVirtSector> ExtVirtSectors;
	ExtVirtSectors mExtVirtSectors;

	uint32	mWeakBitLFSR = 0;

	uint8	mSendPacket[8192 + 16] = {};
	uint8	mReceivePacket[8192 + 16] = {};
	uint8	mDriveRAM[8192] = {};
};

#endif
