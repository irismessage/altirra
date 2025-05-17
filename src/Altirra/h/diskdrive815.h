//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2018 Avery Lee
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

#ifndef f_AT_DISKDRIVE815_H
#define f_AT_DISKDRIVE815_H

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

class ATDeviceDiskDrive815 final : public ATDevice
	, public IATDeviceFirmware
	, public IATDeviceDiskDrive
	, public ATDeviceSIO
	, public ATDiskDriveDebugTargetControl
	, public IATDeviceRawSIO
	, public IATDiskInterfaceClient
{
public:
	ATDeviceDiskDrive815();
	~ATDeviceDiskDrive815();

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

	uint8 DebugReadSSDA(uint32 addr);
	uint8 ReadSSDA(uint32 addr);
	void WriteSSDA(uint32 addr, uint8 value);
	void OnWriteDiskByte(uint8 v);
	void StartDiskRead();
	void StartDiskWrite();
	void ResetDiskWrite();
	void FlushDiskWrite();
	void EndDiskWrite();
	void ResetSSDA();
	void UpdateSSDAReceiveFIFO();

	void OnRIOT1RegisterWrite(uint32 addr, uint8 val);
	void OnRIOT2RegisterWrite(uint32 addr, uint8 val);
	void OnRIOT3RegisterWrite(uint32 addr, uint8 val);

	void StepDrive(int drive, uint8 newPhases);
	void PlayStepSound();
	uint32 GetCurrentBitPos(uint64 t64, int drive) const;
	void UpdateRotationStatus();
	void UpdateDiskStatus();
	void UpdateWriteProtectStatus();

	void MaterializeTrack(int drive, int track);

	enum {
		kEventId_DriveReceiveBit = kEventId_FirstCustom
	};

	ATEvent *mpEventDriveReceiveBit = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	IATDiskDriveManager *mpDiskDriveManager = nullptr;
	ATDiskInterface *mpDiskInterfaces[2] {};

	ATFirmwareManager *mpFwMgr = nullptr;

	uint32 mReceiveShiftRegister = 0;
	uint32 mReceiveTimingAccum = 0;
	uint32 mReceiveTimingStep = 0;

	uint32 mCurrentTrack[2] { 0, 0 };
	int mLastMaterializedTrack = -1;
	int mLastMaterializedDrive = -1;

	uint64 mRotationStartTime[2] {};
	uint32 mRotationStartBitPos[2] {};
	bool mbMotorEnabled[2] {};

	uint8 mDriveId = 0;
	uint8 mInhibitDiskChangeInvalidation = 0;

	// true if we are emulating the 815 reading/writing non-inverted data, which
	// is inverted polarity from other disk drives and disk image storage
	bool mbAccurateInvert = false;

	bool mbFirmwareUsable = false;
	bool mbSoundsEnabled = false;
	bool mbCommandState = false;

	ATDiskDriveAudioPlayer mAudioPlayer;

	uint32 mLastStepSoundTime = 0;
	uint32 mLastStepPhase = 0;

	uint32 mHeadPositionLastDriveCycle = 0;
	uint32 mHeadPositionLastBitPos = 0;
	uint8 mReceiveFIFO[4] {};
	uint8 mReceiveFIFOStart = 0;
	uint8 mReceiveFIFOLength = 0;
	uint8 mSSDASyncValue = 0;
	bool mbSSDASyncEnabled = false;
	bool mbSSDASyncOutputEnabled = false;
	bool mbSSDATwoByteMode = false;
	bool mbSSDARxReset = false;

	// SSDA sync state -- number of sync bytes matched (0-2). This gets
	// incremented once for each matched sync byte and locks the SSDA into
	// byte synchronization once 1- or 2-byte synchronization is achieved.
	uint8 mSSDASyncState = 0;
	uint8 mSSDABitOffset = 0;

	// Receive clocking state -- also number of sync bytes matched (0-2). This
	// is driven by the pulses from the SSDA sync logic. Once it hits 2, the
	// SSDA receives only data bits.
	uint8 mReceiveClockState = 0;
	bool mbReceiveClockReset = false;

	uint8 mTransmitState = 0;
	uint8 mTransmitLastByte = 0;
	uint16 mTransmitCurrentWord = 0;
	uint16 mTransmitCurrentCount = 0;

	// Time at which the transmit queue has finished or will finish. Since the
	// transmit clock runs at 500KHz and the transmit FIFO is three deep, this
	// needs to be no more than 32 cycles ahead for one byte free and 64 cycles
	// head for two bytes free.
	uint64 mTransmitUnderflowTime = 0;

	enum class SSDARegisterAddr : uint8 {
		// These must be ordered by (AC1, AC2) bits.
		Control2,		// AC1=0, AC2=0
		Sync,			// AC1=0, AC2=1
		Control3,		// AC1=1, AC2=0
		Transmit		// AC1=1, AC2=1
	} mSSDARegisterAddr;

	ATCoProcReadMemNode mReadNodeSSDARAM {};
	ATCoProcReadMemNode mReadNodeRIOT1Registers {};
	ATCoProcReadMemNode mReadNodeRIOT23Registers {};
	ATCoProcWriteMemNode mWriteNodeSSDARAM {};
	ATCoProcWriteMemNode mWriteNodeRIOT1Registers {};
	ATCoProcWriteMemNode mWriteNodeRIOT23Registers {};

	ATCoProc6502 mCoProc;

	struct TargetProxy final : public ATDiskDriveDebugTargetProxyBaseT<ATCoProc6502> {
		uint32 GetTime() const override {
			return ATSCHEDULER_GETTIME(mpDriveScheduler);
		}

		ATScheduler *mpDriveScheduler;
	} mTargetProxy;

	ATRIOT6532Emulator mRIOT1;
	ATRIOT6532Emulator mRIOT2;
	ATRIOT6532Emulator mRIOT3;

	vdfastvector<ATCPUHistoryEntry> mHistory;

	ATDiskDriveSerialBitTransmitQueue mSerialXmitQueue;
	ATDiskDriveSerialCommandQueue mSerialCmdQueue;

	VDALIGN(4) uint8 mRAM[0x180] = {};
	VDALIGN(4) uint8 mROM[0x1000] = {};
	VDALIGN(4) uint8 mDummyWrite[0x100] = {};
	VDALIGN(4) uint8 mDummyRead[0x100] = {};

	// 2us bit cell @ 288 RPM = 104166.7 raw bit cells per rotation
	// /8 bits in a byte = 13020.8 raw bytes per rotation
	// We wrap around an extra few bytes so at any bit position a
	// full 16-bit word can be extracted.
	static constexpr uint32 kTrackBitLen = 104167;
	static constexpr uint32 kTrackByteLen = kTrackBitLen >> 3;
	uint8 mTrackData[kTrackByteLen + 6] {};
	uint8 mWriteData[kTrackByteLen + 4] {};		// need +2 bytes on each side for insert bits
	uint32 mWriteDataStartBitPos = 0;
	uint32 mWriteDataIndex = 0;
	uint32 mWriteDataLen = 0;
	int mWriteDrive = 0;

	using SectorRenderInfo = ATDiskRenderedSectorInfo;
	vdfastvector<SectorRenderInfo> mRenderedSectors;
	
	ATDebugTargetBreakpointsImpl mBreakpointsImpl;
};

#endif
