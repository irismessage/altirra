//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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

#ifndef f_AT_SIOMANAGER_H
#define f_AT_SIOMANAGER_H

#include <at/atcore/devicesio.h>
#include "pokey.h"
#include "scheduler.h"

class ATCPUEmulator;
class ATCPUEmulatorMemory;
class ATSimulator;
class IATUIRenderer;
struct ATCPUHookNode;
class ATPokeyEmulator;
class ATPIAEmulator;

struct ATSIORequest {
	uint8	mDevice;
	uint8	mCommand;
	uint8	mMode;
	uint8	mTimeout;
	uint16	mAddress;
	uint16	mLength;
	uint16	mSector;
	uint8	mAUX[2];
};

class ATSIOManager final : public IATPokeySIODevice, public IATDeviceSIOManager, public IATSchedulerCallback {
	ATSIOManager(const ATSIOManager&) = delete;
	ATSIOManager& operator=(const ATSIOManager&) = delete;
public:
	ATSIOManager();
	~ATSIOManager();

	bool GetSIOPatchEnabled() const { return mbSIOPatchEnabled; }
	void SetSIOPatchEnabled(bool enabled);

	bool GetBurstTransfersEnabled() const { return mbBurstTransfersEnabled; }
	void SetBurstTransfersEnabled(bool enabled) { mbBurstTransfersEnabled = enabled; }

	void Init(ATCPUEmulator *cpu, ATSimulator *sim);
	void Shutdown();

	void ColdReset();

	void ReinitHooks();
	void UninitHooks();

	bool TryAccelRequest(const ATSIORequest& req, bool isDSKINV);

public:
	virtual void PokeyAttachDevice(ATPokeyEmulator *pokey) override;
	virtual void PokeyWriteSIO(uint8 c, bool command, uint32 cyclesPerBit) override;
	virtual void PokeyBeginCommand() override;
	virtual void PokeyEndCommand() override;
	virtual void PokeySerInReady() override;

public:
	virtual void AddDevice(IATDeviceSIO *dev) override;
	virtual void RemoveDevice(IATDeviceSIO *dev) override;
	virtual void BeginCommand() override;
	virtual void SendData(const void *data, uint32 len, bool addChecksum) override;
	virtual void SendACK() override;
	virtual void SendNAK() override;
	virtual void SendComplete() override;
	virtual void SendError() override;
	virtual void ReceiveData(uint32 id, uint32 len, bool autoProtocol) override;
	virtual void SetTransferRate(uint32 cyclesPerBit, uint32 cyclesPerByte) override;
	virtual void Delay(uint32 ticks) override;
	virtual void InsertFence(uint32 id) override;
	virtual void EndCommand() override;

	virtual void AddRawDevice(IATDeviceRawSIO *dev) override;
	virtual void RemoveRawDevice(IATDeviceRawSIO *dev) override;
	virtual void SendRawByte(uint8 byte, uint32 cyclesPerBit) override;
	virtual void SetSIOInterrupt(IATDeviceRawSIO *dev, bool state) override;
	virtual void SetSIOProceed(IATDeviceRawSIO *dev, bool state) override;

	virtual void SetExternalClock(IATDeviceRawSIO *dev, uint32 initialOffset, uint32 period) override;

public:
	virtual void OnScheduledEvent(uint32 id) override;

private:
	enum {
		kEventId_Delay = 1,
		kEventId_Send
	};

	class RawDeviceListLock;
	friend class RawDeviceListLock;

	uint8 OnHookDSKINV(uint16);
	uint8 OnHookSIOV(uint16);

	void ExecuteNextStep();
	void ShiftTransmitBuffer();
	void ResetTransferRate();
	void OnMotorStateChanged(bool asserted);

	ATCPUEmulator *mpCPU;
	ATCPUEmulatorMemory *mpMemory;
	ATSimulator *mpSim;
	IATUIRenderer *mpUIRenderer;
	ATScheduler *mpScheduler;
	ATPokeyEmulator *mpPokey;
	ATPIAEmulator *mpPIA;
	int mPIAOutput;

	ATCPUHookNode *mpSIOVHook;
	ATCPUHookNode *mpDSKINVHook;

	uint32	mTransferLevel;		// Write pointer for accumulating send data.
	uint32	mTransferStart;		// Starting offset for current transfer.
	uint32	mTransferIndex;		// Next byte to send/receive for current transfer.
	uint32	mTransferEnd;		// Stopping offset for current transfer.
	uint32	mTransferCyclesPerBit;
	uint32	mTransferCyclesPerBitRecvMin;
	uint32	mTransferCyclesPerBitRecvMax;
	uint32	mTransferCyclesPerByte;
	bool	mbTransferSend;
	bool	mbTransferError;
	bool	mbCommandState;
	bool	mbMotorState;
	bool	mbSIOPatchEnabled;
	bool	mbBurstTransfersEnabled;
	uint8	mPollCount;
	uint32	mAccelBufferAddress;
	const ATDeviceSIORequest *mpAccelRequest;
	uint8	*mpAccelStatus;
	ATEvent *mpTransferEvent;
	IATDeviceSIO *mpActiveDevice;

	vdfastvector<IATDeviceSIO *> mSIODevices;
	vdfastvector<IATDeviceRawSIO *> mSIORawDevices;
	vdfastvector<IATDeviceRawSIO *> mSIORawDevicesNew;
	sint32 mSIORawDevicesBusy;

	vdfastvector<IATDeviceRawSIO *> mSIOInterruptActive;
	vdfastvector<IATDeviceRawSIO *> mSIOProceedActive;

	struct ExternalClock {
		IATDeviceRawSIO *mpDevice;
		uint32 mTimeBase;
		uint32 mPeriod;
	};

	vdfastvector<ExternalClock> mExternalClocks;

	enum StepType {
		kStepType_None,
		kStepType_Delay,
		kStepType_Send,
		kStepType_SendAutoProtocol,
		kStepType_Receive,
		kStepType_ReceiveAutoProtocol,
		kStepType_SetTransferRate,
		kStepType_Fence,
		kStepType_EndCommand,
		kStepType_AccelSendNAK,
		kStepType_AccelSendError,
	};

	struct Step {
		StepType mType;
		union {
			uint32 mTransferLength;
			uint32 mFenceId;
			uint32 mDelayTicks;
			uint32 mTransferCyclesPerBit;
		};

		union {
			uint32 mTransferCyclesPerByte;
			uint32 mTransferId;
		};
	};

	Step mCurrentStep;

	vdfastdeque<Step> mStepQueue;

	uint8 mTransferBuffer[65536];
};

#endif	// f_AT_SIOMANAGER_H
