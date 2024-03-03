//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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

#ifndef f_AT_1450XLD_H
#define f_AT_1450XLD_H

#include <vd2/system/vdalloc.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicepbi.h>
#include "diskdrivefullbase.h"
#include "diskinterface.h"

class ATMemoryLayer;
class ATDiskInterface;

class AT1450XLDiskDevice final : public ATDeviceT<IATDeviceFirmware, IATDeviceMemMap>, public IATPBIDevice, public IATSchedulerCallback, public IATDiskInterfaceClient {
public:
	static constexpr uint32 kTypeID = "AT1450XLDiskDevice"_vdtypeid;

	AT1450XLDiskDevice(bool full, bool tong);

	void *AsInterface(uint32 iid) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;
	void WarmReset() override;

	void SetDiskAttn(bool attn);

	sint32 DebugReadByte(uint32 addr) const;
	sint32 ReadByte(uint32 addr);
	bool WriteByte(uint32 addr, uint8 value);

public:
	void GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const override;
	void SelectPBIDevice(bool enable) override;
	bool IsPBIOverlayActive() const override;
	uint8 ReadPBIStatus(uint8 busData, bool debugOnly) override;

public:	// IATDeviceFirmware
	void InitFirmware(ATFirmwareManager *fwman) override;
	bool ReloadFirmware() override;
	const wchar_t *GetWritableFirmwareDesc(uint32 idx) const override;
	bool IsWritableFirmwareDirty(uint32 idx) const override;
	void SaveWritableFirmware(uint32 idx, IVDStream& stream) override;
	ATDeviceFirmwareStatus GetFirmwareStatus() const override;

public:	// IATDeviceMemMap
	void InitMemMap(ATMemoryManager *memmap) override;
	bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const override;

public:	// IATSchedulerCallback
	void OnScheduledEvent(uint32 id) override;

public:	// IATDiskInterfaceClient
	void OnDiskChanged(bool mediaRemoved) override;
	void OnWriteModeChanged() override;
	void OnTimingModeChanged() override;
	void OnAudioModeChanged() override;
	bool IsImageSupported(const IATDiskImage& image) const override;

private:
	enum class EventIds : uint32 {
		ReceiveTimeout = 1,
		MotorOff,
		OperationDelay,
		Transfer,
		DiskChange
	};

	void SetReceiveTimeout();
	void ClearReceiveTimeout();
	uint32 MotorOn();
	void MotorOff();
	void SetMotorTimeout();
	void ClearMotorTimeout();
	void UpdateMotorStatus();

	void PrepareOperation();
	void SendCommandACK();
	void SendCommandNAK();
	void StartOperation();
	void BeginReceive(uint32 dataLen);
	void StartOperationPostReceive();
	void SetOperationDelay(uint32 cycles);
	void SendOperationResult();
	void BeginResultTransfer(bool success, uint32 dataLen);
	void EndCommand();
	uint32 Seek(uint32 t, uint32 track);

	void OnFDCStep(bool inward);
	void OnMotorChange();
	void SetSelectedDrive(uint8 driveIdx);
	void UpdateDiskStatus();
	void UpdateWriteProtectStatus();

	static constexpr uint32 kCyclesPerRotation = 372869;

	const bool mbIsTONG = false;
	bool mbAttn = false;
	bool mbControllerBusy = false;
	bool mbControllerBoot = false;
	uint8 mReadLatch = 0;
	uint32 mTransferIndex = 0;
	uint32 mTransferLength = 0;
	bool mbOperationSuccess = false;
	uint8 mDriveStatus = 0;
	uint8 mFDCStatus = 0;
	uint32 mWeakBitLFSR = 0;

	uint32 mCurrentTrack = 0;
	uint64 mMotorOnTime = 0;
	uint64 mLastTransferTime = 0;
	uint32 mLastStepSoundTime = 0;
	uint32 mLastStepPhase = 0;

	enum class ControllerState : uint8 {
		Post,
		WaitingForCommandFrame,
		WaitingForAttn,
		ReceiveCommandFrame,
		SendCommandNAK,
		SendCommandACK,
		ReceiveDataFrame,
		SendDataACK,
		SendDataNAK,
		OperationDelay,
		SendComplete,
		SendError,
		SendDataFrame
	} mControllerState = ControllerState::WaitingForCommandFrame;
	uint32 mCommandSubPhase = 0;

	ATScheduler *mpScheduler = nullptr;
	ATEvent *mpEventTransfer = nullptr;

	ATScheduler *mpSlowScheduler = nullptr;
	ATEvent *mpSlowEventReceiveTimeout = nullptr;
	ATEvent *mpSlowEventMotorOff = nullptr;
	ATEvent *mpSlowEventOpDelay = nullptr;

	ATEvent *mpDriveEventDiskChange = nullptr;

	IATDevicePBIManager *mpPBIMgr = nullptr;
	ATFirmwareManager *mpFwMgr = nullptr;
	ATDeviceFirmwareStatus mFirmwareStatus = ATDeviceFirmwareStatus::Invalid;

	ATMemoryManager *mpMemMgr = nullptr;
	ATMemoryLayer *mpMemLayerFirmware = nullptr;
	ATMemoryLayer *mpMemLayerHardware = nullptr;

	ATDiskInterface *mpDiskInterface = nullptr;

	class FullEmulation;
	vdautoptr<FullEmulation> mpFullEmulation;
	uint32 mDiskChangeState = 0;
	uint8 mSelectedDriveIndex = 0;

	uint8 mCommandBuffer[5] {};
	uint8 mTransferBuffer[256 + 3] {};

	ATDiskDriveAudioPlayer mAudioPlayer;

	alignas(4) uint8 mFirmware[2048] {};
};

#endif
