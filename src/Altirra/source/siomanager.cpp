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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <at/atcore/enumparseimpl.h>
#include <at/atcore/snapshotimpl.h>
#include "siomanager.h"
#include "simulator.h"
#include "cpu.h"
#include "cpuhookmanager.h"
#include "disk.h"
#include "kerneldb.h"
#include "debuggerlog.h"
#include "uirender.h"
#include "hleutils.h"
#include "cassette.h"
#include "savestate.h"
#include "trace.h"
#include <at/atcore/sioutils.h>

ATDebuggerLogChannel g_ATLCHookSIOReqs(false, false, "HOOKSIOREQS", "OS SIO hook requests");
ATDebuggerLogChannel g_ATLCHookSIO(false, false, "HOOKSIO", "OS SIO hook messages");
ATDebuggerLogChannel g_ATLCSIOCmd(false, false, "SIOCMD", "SIO bus commands");
ATDebuggerLogChannel g_ATLCSIOAccel(false, false, "SIOACCEL", "SIO command acceleration");
ATDebuggerLogChannel g_ATLCSIOSteps(false, false, "SIOSTEPS", "SIO command steps");

AT_DECLARE_ENUM_TABLE(ATSIOManager::StepType);

AT_DEFINE_ENUM_TABLE_BEGIN(ATSIOManager::StepType)
	{ ATSIOManager::kStepType_None,							"none" },
	{ ATSIOManager::kStepType_Delay,						"delay" },
	{ ATSIOManager::kStepType_Send,							"send" },
	{ ATSIOManager::kStepType_SendAutoProtocol,				"sendauto" },
	{ ATSIOManager::kStepType_Receive,						"receive" },
	{ ATSIOManager::kStepType_ReceiveAutoProtocol,			"receiveauto" },
	{ ATSIOManager::kStepType_SetTransferRate,				"setxferrate" },
	{ ATSIOManager::kStepType_SetSynchronousTransmit,		"setsyncxfer" },
	{ ATSIOManager::kStepType_Fence,						"fence" },
	{ ATSIOManager::kStepType_EndCommand,					"end" },
AT_DEFINE_ENUM_TABLE_END(ATSIOManager::StepType, ATSIOManager::kStepType_None);

class ATSIOManager::RawDeviceListLock {
public:
	RawDeviceListLock(ATSIOManager *parent);
	~RawDeviceListLock();

protected:
	ATSIOManager *const mpParent;
};

ATSIOManager::RawDeviceListLock::RawDeviceListLock(ATSIOManager *parent)
	: mpParent(parent)
{
	parent->mSIORawDevicesBusy += 2;
}

ATSIOManager::RawDeviceListLock::~RawDeviceListLock() {
	mpParent->mSIORawDevicesBusy -= 2;

	if (mpParent->mSIORawDevicesBusy < 0) {
		mpParent->mSIORawDevicesBusy = 0;

		// remove dead devices
		mpParent->mSIORawDevices.erase(
			std::remove_if(mpParent->mSIORawDevices.begin(), mpParent->mSIORawDevices.end(),
				[](IATDeviceRawSIO *p) { return p == nullptr; }
			)
		);

		// add new devices
		mpParent->mSIORawDevices.insert(mpParent->mSIORawDevices.end(), mpParent->mSIORawDevicesNew.begin(), mpParent->mSIORawDevicesNew.end());
		mpParent->mSIORawDevicesNew.clear();
	}
}

///////////////////////////////////////////////////////////////////////////

ATSIOManager::ATSIOManager() {
	mCurrentStep.mType = kStepType_None;
}

ATSIOManager::~ATSIOManager() {
}

void ATSIOManager::SetSIOPatchEnabled(bool enable) {
	if (mbSIOPatchEnabled == enable)
		return;

	mbSIOPatchEnabled = enable;

	if (mpCPU)
		ReinitHooks();
}

void ATSIOManager::SetDiskSIOAccelEnabled(bool enabled) {
	if (mbDiskSIOAccelEnabled == enabled)
		return;

	mbDiskSIOAccelEnabled = enabled;

	if (mpCPU)
		ReinitHooks();
}

void ATSIOManager::SetOtherSIOAccelEnabled(bool enabled) {
	if (mbOtherSIOAccelEnabled == enabled)
		return;

	mbOtherSIOAccelEnabled = enabled;

	if (mpCPU)
		ReinitHooks();
}

void ATSIOManager::Init(ATCPUEmulator *cpu, ATSimulator *sim) {
	mpCPU = cpu;
	mpMemory = cpu->GetMemory();
	mpSim = sim;
	mpUIRenderer = sim->GetUIRenderer();
	mpScheduler = sim->GetScheduler();
	mpPokey = &mpSim->GetPokey();
	mpPIA = &mpSim->GetPIA();
	mPIAOutput = mpPIA->AllocOutput(
		[](void *p, uint32 state) {
			((ATSIOManager *)p)->OnMotorStateChanged((state & kATPIAOutput_CA2) == 0);
		},
		this,
		kATPIAOutput_CA2);

	mpPokey->AddSIODevice(this);

	ReinitHooks();
}

void ATSIOManager::Shutdown() {
	UninitHooks();

	if (mpPokey) {
		mpPokey->RemoveSIODevice(this);
		mpPokey = nullptr;
	}

	if (mpPIA) {
		mpPIA->FreeOutput(mPIAOutput);
		mpPIA = nullptr;
	}

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpDelayEvent);
		mpScheduler->UnsetEvent(mpTransferEvent);
		mpScheduler = nullptr;
	}

	mpUIRenderer = NULL;
	mpSim = NULL;
	mpMemory = NULL;
	mpCPU = NULL;
}

void ATSIOManager::ColdReset() {
	mbLoadingState = false;

	AbortActiveCommand();

	mTransferLevel = 0;
	mTransferStart = 0;
	mTransferIndex = 0;
	mTransferEnd = 0;
	mbTransferSend = false;
	mbCommandState = false;
	mbMotorState = false;
	mpActiveDevice = nullptr;
	mActiveDeviceId = 0;

	WarmReset();
}

void ATSIOManager::WarmReset() {
	mAccessedDisks = 0;
}

void ATSIOManager::ReinitHooks() {
	UninitHooks();

	ATCPUHookManager& hookmgr = *mpCPU->GetHookManager();

	if (mbSIOPatchEnabled) {
		if (mbOtherSIOAccelEnabled || mbDiskSIOAccelEnabled || mpSim->IsCassetteSIOPatchEnabled() || mpSim->IsFastBootEnabled())
			hookmgr.SetHookMethod(mpSIOVHook, kATCPUHookMode_KernelROMOnly, ATKernelSymbols::SIOV, 0, this, &ATSIOManager::OnHookSIOV);
	}
}

void ATSIOManager::UninitHooks() {
	if (mpCPU) {
		ATCPUHookManager& hookmgr = *mpCPU->GetHookManager();

		hookmgr.UnsetHook(mpSIOVHook);
	}
}

void ATSIOManager::SetTraceContext(ATTraceContext *context) {
	mpTraceContext = context;

	if (context) {
		ATTraceCollection *coll = context->mpCollection;
		ATTraceGroup *group = coll->AddGroup(L"SIO Bus");
		const double invCycleRate = mpScheduler->GetRate().AsInverseDouble();

		mpTraceChannelBusSend = group->AddFormattedChannel(context->mBaseTime, invCycleRate, L"Send");
		mpTraceChannelBusReceive = group->AddFormattedChannel(context->mBaseTime, invCycleRate, L"Receive");
		mTraceCommandStartTime = mpScheduler->GetTick64();
	} else {
		mpTraceChannelBusSend = nullptr;
		mpTraceChannelBusReceive = nullptr;
	}
}

void ATSIOManager::TryAccelPBIRequest() {
	if (OnHookSIOV(0))
		mpCPU->SetP(mpCPU->GetP() | AT6502::kFlagC);
	else
		mpCPU->SetP(mpCPU->GetP() & ~AT6502::kFlagC);
}

bool ATSIOManager::TryAccelRequest(const ATSIORequest& req) {
	g_ATLCHookSIOReqs("Checking SIOV request: Device $%02X | Command $%02X | Mode $%02X | Address $%04X | Length $%04X | AUX $%02X%02X\n"
		, req.mDevice
		, req.mCommand
		, req.mMode
		, req.mAddress
		, req.mLength
		, req.mAUX[1]
		, req.mAUX[0]
		);

	// Check if we already have a command in progress. If so, bail.
	if (mpActiveDevice || mbCommandState)
		return false;

	// Abort read acceleration if the buffer overlaps the parameter region.
	// Yes, there is game stupid enough to do this (Formula 1 Racing). Specifically,
	// we need to check if TIMFLG is in the buffer as that will cause the read to
	// prematurely abort.
	if (req.mAddress <= ATKernelSymbols::TIMFLG && (ATKernelSymbols::TIMFLG - req.mAddress) < req.mLength)
		return false;

	// Check if the I flag is set -- if so, bail. This will hang in SIO
	// if not intercepted by a PBI device.
	if (mpCPU->GetP() & AT6502::kFlagI)
		return false;

	// Check if both read and write mode bits are set. This is not well defined.
	if ((req.mMode & 0xC0) == 0xC0)
		return false;

	ATKernelDatabase kdb(mpMemory);
	uint8 status = 0x01;

	mActiveDeviceId = req.mDevice;
	UpdateActiveDeviceDerivedValues();

	bool allowAccel = mbOtherSIOAccelEnabled;

	if (mbActiveDeviceDisk) {
		allowAccel = mbDiskSIOAccelEnabled;

		if (allowAccel && mpSim->IsDiskSIOOverrideDetectEnabled() && !(mAccessedDisks & (1 << (req.mDevice - 0x31))))
			return false;
	}
	
	while(allowAccel) {
		// Convert the request to a device request.
		ATDeviceSIORequest devreq = {};
		devreq.mDevice = req.mDevice;
		devreq.mCommand = req.mCommand;
		devreq.mAUX[0] = req.mAUX[0];
		devreq.mAUX[1] = req.mAUX[1];
		devreq.mCyclesPerBit = 93;
		devreq.mbStandardRate = true;
		devreq.mPollCount = mPollCount;
		devreq.mMode = req.mMode;
		devreq.mTimeout = req.mTimeout;
		devreq.mLength = req.mLength;
		devreq.mSector = req.mSector;

		ResetTransfer();

		// Run down the device chain and see if anyone is interested in this request.
		for(IATDeviceSIO *dev : mSIODevices) {
			mpAccelRequest = &devreq;
			mpAccelStatus = &status;
			mAccelBufferAddress = req.mAddress;

			mpActiveDevice = dev;
			IATDeviceSIO::CmdResponse response = dev->OnSerialAccelCommand(devreq);
			switch(response) {
				case IATDeviceSIO::kCmdResponse_Start:
					break;

				case IATDeviceSIO::kCmdResponse_Send_ACK_Complete:
					BeginCommand();
					SendACK();
					SendComplete();
					EndCommand();
					break;

				case IATDeviceSIO::kCmdResponse_Fail_NAK:
					BeginCommand();
					SendNAK();
					EndCommand();
					break;
			}

			if (response != IATDeviceSIO::kCmdResponse_NotHandled) {
				ExecuteNextStep();
			}

			mpActiveDevice = nullptr;
			mActiveDeviceId = 0;
			mpAccelRequest = nullptr;
			mpAccelStatus = nullptr;

			// check if we were specifically asked not to accelerate this request
			if (response == IATDeviceSIO::kCmdResponse_BypassAccel)
				return false;

			// check if the device handled it
			if (response != IATDeviceSIO::kCmdResponse_NotHandled)
				goto handled;
		}

		// Check if the command is a type 3 poll command and we have fast boot enabled.
		// If so, keep looping until we hit 26 retries. This gives accelerated devices
		// a chance to intercept fast boot.
		if (mpSim->IsFastBootEnabled()) {
			// type 3 poll (??/40/00/00)
			if (devreq.mCommand == 0x40 && devreq.mAUX[0] == 0x00 && devreq.mAUX[1] == 0x00) {
				++mPollCount;

				if (mPollCount <= 26)
					continue;
			}
		}

		break;
	}

	// Check hard-coded devices.
	if (req.mDevice >= 0x31 && req.mDevice <= 0x3F) {
		const uint32 diskIndex = req.mDevice - 0x31;
		ATDiskEmulator& disk = mpSim->GetDiskDrive(diskIndex);

		if (!disk.IsEnabled() && mpSim->GetDiskInterface(diskIndex).GetClientCount() < 2 && mpSim->IsFastBootEnabled())
			goto fastbootignore;

		return false;
	} else if (req.mDevice == 0x4F) {
		if (!mpSim->IsFastBootEnabled())
			return false;

fastbootignore:
		// return timeout
		status = 0x8A;
	} else if (req.mDevice == 0x5F) {
		if (!mpSim->IsCassetteSIOPatchEnabled())
			return false;

		ATCassetteEmulator& cassette = mpSim->GetCassette();

		// Check if a read or write is requested
		if (req.mMode == 0x40) {
			status = cassette.ReadBlock(req.mAddress, req.mLength, mpMemory);

			mpUIRenderer->PulseStatusFlags(1 << 16);

			g_ATLCHookSIO("Intercepted cassette SIO read: buf=%04X, len=%04X, status=%02X\n", req.mAddress, req.mLength, status);
		} else {
			status = cassette.WriteBlock(req.mAddress, req.mLength, mpMemory);

			mpUIRenderer->PulseStatusFlags(1 << 16);

			g_ATLCHookSIO("Intercepted cassette SIO write: buf=%04X, len=%04X, status=%02X\n", req.mAddress, req.mLength, status);
		}
	} else {
		return false;
	}

handled:
	UpdatePollState(req.mCommand, req.mAUX[0], req.mAUX[1]);

	// If this is anything other than a cassette request, reset timers 3+4 to 19200 baud and set
	// asynchronous receive mode. Wayout needs this to not play garbage on channels 3+4 on the
	// title screen.
	if (req.mDevice != 0x5F) {
		kdb.AUDF3 = 0x28;
		kdb.AUDF4 = 0;
		kdb.SKCTL = 0x13;
		kdb.SSKCTL = 0x13;
	}

	ATClearPokeyTimersOnDiskIo(kdb);

	// Set CDTMA1 to dummy address (KnownRTS) if it is not already set -- SIO is documented as setting
	// this on every call. This is required by Ankh, which uses OS timer 1 for a delay but doesn't
	// bother setting up CDTMV1.
	if (kdb.CDTMA1 == 0)
		kdb.CDTMA1 = 0xE4C0;

	// Set checksum sent flag. This is relied on by Apple Panic, which blindly turns on the serial
	// output complete interrupt after reading sector 404. If this isn't done, the SEROC handler
	// doesn't clear the interrupt and the CPU gets stuck in an IRQ loop.
	kdb.CHKSNT = 0xFF;

	// Clear CRITIC.
	kdb.CRITIC = 0;
	kdb.STATUS = status;
	kdb.DSTATS = status;
	
	// Set carry depending on last status. Micropainter depends on the state of the carry flag
	// after issuing a call to DSKINV, which in turn leaves the carry flag set from the call to
	// SIOV. The carry is set by two compares within SIO on the status, first to $01 (success)
	// and then to $8A (timeout).
	uint8 carry = AT6502::kFlagC;

	if (status != 0x01 && status < 0x8A)
		carry = 0;

	mpCPU->SetP((mpCPU->GetP() & ~AT6502::kFlagC) + carry);

	// Set A=0. SIOV sets this on the way out as part of clearing CRITIC.
	mpCPU->SetA(0);

	// Set X to typical return value for determinism.
	mpCPU->SetX(0xFE);

	mpCPU->Ldy(status);

	return true;
}

void ATSIOManager::PokeyAttachDevice(ATPokeyEmulator *pokey) {
}

bool ATSIOManager::PokeyWriteSIO(uint8 c, bool command, uint32 cyclesPerBit, uint64 startTime, bool framingError) {
	if (mpTraceContext) {
		const uint64 t = mpScheduler->GetTick64();

		mpTraceChannelBusSend->TruncateLastEvent(startTime);
		mpTraceChannelBusSend->AddTickEvent(startTime, t,
			[ch = (uint8)c](VDStringW& s) {
				s.sprintf(L"%02X", ch);
			},
			kATTraceColor_IO_Write
		);
	}

	if (mTransferIndex < mTransferEnd && !mbTransferSend) {
		// We place the byte into the buffer even if a framing error occurs because
		// typically drives do not check the start bit (810, 1050, XF551, Indus GT).
		mTransferBuffer[mTransferIndex++] = c;

		if (mbCommandState)
			mTransferCyclesPerBit = cyclesPerBit;
		else if (cyclesPerBit < mTransferCyclesPerBitRecvMin || cyclesPerBit > mTransferCyclesPerBitRecvMax)
			mbTransferError = true;

		if (mTransferIndex >= mTransferEnd && mpActiveDevice) {
			const uint32 transferLen = mTransferEnd - mTransferStart;
			const uint8 *data = mTransferBuffer + mTransferStart;
			const bool checksumOK = !mbTransferError && (!transferLen || ATComputeSIOChecksum(data, transferLen - 1) == data[transferLen - 1]);

			mTransferStart = mTransferEnd;

			if (mCurrentStep.mType == kStepType_ReceiveAutoProtocol) {
				if (!checksumOK) {
					mStepQueue.clear();
					// SIO protocol requires 850us minimum delay.
					Delay(1530);
					SendNAK();
					ExecuteNextStep();
					return false;
				}

				mpActiveDevice->OnSerialReceiveComplete(mCurrentStep.mTransferId, data, transferLen - 1, true);
			} else {
				mpActiveDevice->OnSerialReceiveComplete(mCurrentStep.mTransferId, data, transferLen, checksumOK);
			}

			mCurrentStep.mType = kStepType_None;
			ExecuteNextStep();
		}
	}

	{
		RawDeviceListLock lock(this);

		for(auto *rawdev : mSIORawDevices) {
			if (rawdev)
				rawdev->OnReceiveByte(c, command, cyclesPerBit);
		}
	}

	return mpActiveDevice && (mbActiveDeviceDisk ? mbDiskBurstTransfersEnabled : mbBurstTransfersEnabled);
}

void ATSIOManager::PokeyBeginCommand() {
	if (!mbLoadingState) {
		AbortActiveCommand();

		mbTransferSend = false;
		mTransferIndex = 0;
		mTransferLevel = 0;
		mTransferStart = 0;
		mTransferEnd = 5;
	}

	if (!mbCommandState) {
		mbCommandState = true;

		RawDeviceListLock lock(this);

		for(auto *rawdev : mSIORawDevices) {
			if (rawdev)
				rawdev->OnCommandStateChanged(mbCommandState);
		}
	}
}

void ATSIOManager::PokeyEndCommand() {
	if (!mbCommandState)
		return;
	
	mbCommandState = false;

	if (mbLoadingState)
		return;

	if (mTransferIndex >= mTransferEnd && ATComputeSIOChecksum(mTransferBuffer, 4) == mTransferBuffer[4]) {
		ATDeviceSIOCommand cmd = {};
		cmd.mDevice = mTransferBuffer[0];
		cmd.mCommand = mTransferBuffer[1];
		cmd.mAUX[0] = mTransferBuffer[2];
		cmd.mAUX[1] = mTransferBuffer[3];
		cmd.mCyclesPerBit = mTransferCyclesPerBit;
		cmd.mbStandardRate = (mTransferCyclesPerBit >= 91 && mTransferCyclesPerBit <= 98);
		cmd.mPollCount = mPollCount;

		// Check if this is a type 3 poll command -- we provide assistance for these.
		// Note that we've already recorded the poll count above.
		UpdatePollState(cmd.mCommand, cmd.mAUX[0], cmd.mAUX[1]);

		mTransferStart = 0;
		mTransferIndex = 0;
		mTransferEnd = 0;
		mTransferLevel = 0;

		if (g_ATLCSIOCmd.IsEnabled())
			g_ATLCSIOCmd("Device %02X | Command %02X | %02X %02X (%s)%s\n", cmd.mDevice, cmd.mCommand, cmd.mAUX[0], cmd.mAUX[1], ATDecodeSIOCommand(cmd.mDevice, cmd.mCommand, cmd.mAUX), cmd.mbStandardRate ? "" : " (high-speed command frame)");

		ResetTransfer();

		mbActiveDeviceDisk = (cmd.mDevice >= 0x31 && cmd.mDevice <= 0x3F);
		mActiveDeviceId = cmd.mDevice;

		for(IATDeviceSIO *dev : mSIODevices) {
			mpActiveDevice = dev;

			const IATDeviceSIO::CmdResponse response = dev->OnSerialBeginCommand(cmd);
			if (response) {
				if (mbActiveDeviceDisk)
					mAccessedDisks |= (1 << (cmd.mDevice - 0x31));

				switch(response) {
					case IATDeviceSIO::kCmdResponse_Start:
						break;

					case IATDeviceSIO::kCmdResponse_Send_ACK_Complete:
						BeginCommand();
						SendACK();
						SendComplete();
						EndCommand();
						break;

					case IATDeviceSIO::kCmdResponse_Fail_NAK:
						BeginCommand();
						SendNAK();
						EndCommand();
						break;
				}
				break;
			}

			mpActiveDevice = nullptr;
			mActiveDeviceId = 0;
		}
	}

	{
		RawDeviceListLock lock(this);

		for(auto *rawdev : mSIORawDevices) {
			if (rawdev)
				rawdev->OnCommandStateChanged(mbCommandState);
		}
	}
}

void ATSIOManager::PokeySerInReady() {
	if (!(mbActiveDeviceDisk ? mbDiskBurstTransfersEnabled : mbBurstTransfersEnabled))
		return;

	{
		RawDeviceListLock lock(this);

		for(auto *rawdev : mSIORawDevices) {
			if (rawdev)
				rawdev->OnSendReady();
		}
	}

	if (!mpTransferEvent)
		return;

	if (!mbTransferSend)
		return;

	if (mTransferIndex - mTransferStart < 3)
		return;

	uint32 existingDelay = mpScheduler->GetTicksToEvent(mpTransferEvent);
	if (existingDelay > 50) {
		mTransferBurstOffset = (existingDelay - 50);
		mAccelTimeSkew += mTransferBurstOffset;

		mpScheduler->SetEvent(50, this, kEventId_Send, mpTransferEvent);
	}
}

void ATSIOManager::AddDevice(IATDeviceSIO *dev) {
	mSIODevices.push_back(dev);
}

void ATSIOManager::RemoveDevice(IATDeviceSIO *dev) {
	auto it = std::find(mSIODevices.begin(), mSIODevices.end(), dev);

	if (it != mSIODevices.end())
		mSIODevices.erase(it);

	if (mpActiveDevice == dev) {
		AbortActiveCommand();
	}
}

void ATSIOManager::BeginCommand() {
	if (mpAccelRequest) {
		g_ATLCSIOAccel("Accelerating device %02X cmd %02X aux %02X%02X (%04X) buffer %04X length %04X\n"
			, mpAccelRequest->mDevice
			, mpAccelRequest->mCommand
			, mpAccelRequest->mAUX[0]
			, mpAccelRequest->mAUX[1]
			, mpAccelRequest->mSector
			, mAccelBufferAddress
			, mpAccelRequest->mLength);
	}

	mTransferStart = 0;
	mTransferIndex = 0;
	mTransferEnd = 0;
	mTransferLevel = 0;
}

void ATSIOManager::SendData(const void *data, uint32 len, bool addChecksum) {
	VDASSERT(mTransferIndex <= mTransferLevel);

	if (!len)
		return;

	uint32 spaceRequired = len;

	if (addChecksum)
		++spaceRequired;

	if (vdcountof(mTransferBuffer) - mTransferLevel < spaceRequired) {
		// The transmit buffer is full -- let's see if we can shift it down.
		ShiftTransmitBuffer();

		// check again
		if (vdcountof(mTransferBuffer) - mTransferLevel < spaceRequired) {
			VDASSERT(!"No room left in transfer buffer.");
			return;
		}
	}

	Step& step = mStepQueue.push_back();
	step.mType = addChecksum ? kStepType_SendAutoProtocol : kStepType_Send;
	step.mTransferLength = spaceRequired;
	
	memcpy(mTransferBuffer + mTransferLevel, data, len);

	if (addChecksum)
		mTransferBuffer[mTransferLevel + len] = ATComputeSIOChecksum(mTransferBuffer + mTransferLevel, len);

	mTransferLevel += spaceRequired;

	ExecuteNextStep();
}

void ATSIOManager::SendACK() {
	// This command, and all other commands, need to silently ignore when no device
	// is active. We need to support this to allow an EndCommand() to be preemptively
	// inserted into a command stream off of a receive callback. In that case, surrounding
	// code that is being unwound may still attempt to push a couple of additional commands.
	if (!mpActiveDevice)
		return;

	if (mpAccelRequest) {
		Step& step = mStepQueue.push_back();
		step.mType = kStepType_AccelSendACK;
	} else {
		SendData("A", 1, false);
	}
}

void ATSIOManager::SendNAK() {
	if (!mpActiveDevice)
		return;

	if (mpAccelRequest) {
		Step& step = mStepQueue.push_back();
		step.mType = kStepType_AccelSendNAK;
	} else {
		SendData("N", 1, false);
	}
}

void ATSIOManager::SendComplete(bool autoDelay) {
	if (!mpActiveDevice)
		return;

	// SIO protocol requires minimum 250us delay here.
	if (autoDelay)
		Delay(450);

	if (mpAccelRequest) {
		Step& step = mStepQueue.push_back();
		step.mType = kStepType_AccelSendComplete;
	} else {
		SendData("C", 1, false);
	}
}

void ATSIOManager::SendError(bool autoDelay) {
	if (!mpActiveDevice)
		return;

	// SIO protocol requires minimum 250us delay here.
	if (autoDelay)
		Delay(450);

	if (mpAccelRequest) {
		Step& step = mStepQueue.push_back();
		step.mType = kStepType_AccelSendError;
	} else {
		SendData("E", 1, false);
	}
}

void ATSIOManager::ReceiveData(uint32 id, uint32 len, bool autoProtocol) {
	if (!mpActiveDevice)
		return;

	if (autoProtocol)
		++len;

	if (vdcountof(mTransferBuffer) - mTransferLevel < len) {
		// The transmit buffer is full -- let's see if we can shift it down.
		ShiftTransmitBuffer();

		// check again
		if (vdcountof(mTransferBuffer) - mTransferLevel < len) {
			VDASSERT(!"No room left in transfer buffer.");
			return;
		}
	}

	memset(mTransferBuffer + mTransferLevel, 0, len);
	mTransferLevel += len;

	Step& step = mStepQueue.push_back();
	step.mType = autoProtocol ? kStepType_ReceiveAutoProtocol : kStepType_Receive;
	step.mTransferLength = len;
	step.mTransferId = id;

	if (autoProtocol) {
		// SIO protocol requires 850us minimum delay.
		Delay(1530);
		SendACK();
	}

	ExecuteNextStep();
}

void ATSIOManager::SetTransferRate(uint32 cyclesPerBit, uint32 cyclesPerByte) {
	if (!mpActiveDevice)
		return;

	Step& step = mStepQueue.push_back();
	step.mType = kStepType_SetTransferRate;
	step.mTransferCyclesPerBit = cyclesPerBit;
	step.mTransferCyclesPerByte = cyclesPerByte;

	ExecuteNextStep();
}

void ATSIOManager::SetSynchronousTransmit(bool enable) {
	if (!mpActiveDevice)
		return;

	Step& step = mStepQueue.push_back();
	step.mType = kStepType_SetSynchronousTransmit;
	step.mbEnable = enable;

	ExecuteNextStep();
}

void ATSIOManager::Delay(uint32 ticks) {
	if (!mpActiveDevice)
		return;

	if (!ticks)
		return;

	Step& step = mStepQueue.push_back();
	step.mType = kStepType_Delay;
	step.mDelayTicks = ticks;

	ExecuteNextStep();
}

void ATSIOManager::InsertFence(uint32 id) {
	if (!mpActiveDevice)
		return;

	Step& step = mStepQueue.push_back();
	step.mType = kStepType_Fence;
	step.mFenceId = id;
}

void ATSIOManager::FlushQueue() {
	mStepQueue.clear();
}

void ATSIOManager::EndCommand() {
	if (!mpActiveDevice)
		return;

	Step& step = mStepQueue.push_back();
	step.mType = kStepType_EndCommand;
}

uint32 ATSIOManager::GetCyclesPerBitRecv() const {
	return mpPokey->GetSerialCyclesPerBitRecv();
}

uint32 ATSIOManager::GetRecvResetCounter() const {
	return mpPokey->GetSerialInputResetCounter();
}

class ATSaveStateSioCommandStep final : public ATSnapExchangeObject<ATSaveStateSioCommandStep> {
public:
	ATSaveStateSioCommandStep() = default;
	ATSaveStateSioCommandStep(const ATSIOManager::Step& step)
		: mStep(step)
	{
	}

	template<typename T>
	void Exchange(T& rw) {
		ATSIOManager::StepType stepType;
		uint32 arg1 = 0;
		uint32 arg2 = 0;

		if constexpr (rw.IsWriter) {
			stepType = mStep.mType;
			arg1 = 0;
			arg2 = 0;

			switch(mStep.mType) {
				case ATSIOManager::kStepType_Delay:
					arg1 = mStep.mDelayTicks;
					break;

				case ATSIOManager::kStepType_Send:
				case ATSIOManager::kStepType_SendAutoProtocol:
					arg1 = mStep.mTransferLength;
					break;

				case ATSIOManager::kStepType_Receive:
				case ATSIOManager::kStepType_ReceiveAutoProtocol:
					arg1 = mStep.mTransferLength;
					arg2 = mStep.mTransferId;
					break;

				case ATSIOManager::kStepType_SetTransferRate:
					arg1 = mStep.mTransferCyclesPerByte;
					arg2 = mStep.mTransferCyclesPerBit;
					break;

				case ATSIOManager::kStepType_SetSynchronousTransmit:
					arg1 = mStep.mbEnable;
					break;

				case ATSIOManager::kStepType_Fence:
					arg1 = mStep.mFenceId;
					break;

				case ATSIOManager::kStepType_EndCommand:
					break;
			}
		}

		rw.TransferEnum("step_type", &stepType);
		rw.Transfer("arg1", &arg1);
		rw.Transfer("arg2", &arg2);
		rw.Transfer("transfer_data", &mTransferData);

		if constexpr (rw.IsReader) {
			mStep.mType = stepType;

			switch(stepType) {
				case ATSIOManager::kStepType_Delay:
					mStep.mDelayTicks = arg1;
					break;

				case ATSIOManager::kStepType_Send:
				case ATSIOManager::kStepType_SendAutoProtocol:
					mStep.mTransferLength = arg1;

					if (mTransferData.size() != arg1)
						throw ATInvalidSaveStateException();
					break;

				case ATSIOManager::kStepType_Receive:
				case ATSIOManager::kStepType_ReceiveAutoProtocol:
					mStep.mTransferLength = arg1;
					mStep.mTransferId = arg2;
					break;

				case ATSIOManager::kStepType_SetTransferRate:
					mStep.mTransferCyclesPerByte = arg1;
					mStep.mTransferCyclesPerBit = arg2;

					if (!mStep.mTransferCyclesPerByte || !mStep.mTransferCyclesPerBit)
						throw ATInvalidSaveStateException();
					break;

				case ATSIOManager::kStepType_SetSynchronousTransmit:
					mStep.mbEnable = arg1 != 0;
					break;

				case ATSIOManager::kStepType_Fence:
					mStep.mFenceId = arg1;
					break;

				case ATSIOManager::kStepType_EndCommand:
					break;
			}
		}
	}

	ATSIOManager::Step mStep {};
	vdfastvector<uint8> mTransferData;
};

ATSERIALIZATION_DEFINE(ATSaveStateSioCommandStep);

class ATSaveStateSioActiveCommand final : public ATSnapExchangeObject<ATSaveStateSioActiveCommand> {
public:
	template<typename T>
	void Exchange(T& rw) {
		rw.Transfer("device_id", &mDeviceId);
		rw.Transfer("transfer_start", &mTransferStart);
		rw.Transfer("transfer_index", &mTransferIndex);
		rw.Transfer("transfer_error", &mbTransferError);
		rw.Transfer("transfer_cycles_per_bit", &mTransferCyclesPerBit);
		rw.Transfer("transfer_cycles_per_byte", &mTransferCyclesPerByte);
		rw.Transfer("transmit_synchronous", &mbTransmitSynchronous);
		rw.Transfer("current_step", &mpCurrentStep);
		rw.Transfer("step_delay", &mStepDelay);
		rw.Transfer("steps", &mSteps);

		if constexpr (rw.IsReader) {
			if (mTransferIndex > sizeof(ATSIOManager::mTransferBuffer))
				throw ATInvalidSaveStateException();
		}
	}

	uint8 mDeviceId = 0;
	uint32 mTransferIndex = 0;
	uint32 mTransferStart = 0;
	bool mbTransferError = false;
	uint32 mStepDelay = 0;
	uint32 mTransferCyclesPerBit = 0;
	uint32 mTransferCyclesPerByte = 0;
	bool mbTransmitSynchronous = false;
	vdvector<vdrefptr<ATSaveStateSioCommandStep>> mSteps;
	vdrefptr<ATSaveStateSioCommandStep> mpCurrentStep;
};

ATSERIALIZATION_DEFINE(ATSaveStateSioActiveCommand);

void ATSIOManager::SaveActiveCommandState(const IATDeviceSIO *device, IATObjectState **ppState) const {
	if (!device || device != mpActiveDevice) {
		*ppState = nullptr;
		return;
	}

	vdrefptr state(new ATSaveStateSioActiveCommand);

	uint32 transferPos = mTransferEnd;
	if (mCurrentStep.mType) {
		ATSaveStateSioCommandStep *savedCurStep = new ATSaveStateSioCommandStep(mCurrentStep);

		state->mpCurrentStep = vdrefptr(savedCurStep);

		if (mCurrentStep.mType == kStepType_Send || mCurrentStep.mType == kStepType_SendAutoProtocol) {
			savedCurStep->mTransferData.resize(mCurrentStep.mTransferLength);
			memcpy(savedCurStep->mTransferData.data(), mTransferBuffer + mTransferStart, mCurrentStep.mTransferLength);

			// transferPos already points to end
		}
	}

	state->mSteps.reserve(mStepQueue.size());

	for(const Step& step : mStepQueue) {
		ATSaveStateSioCommandStep *savedStep = new ATSaveStateSioCommandStep(step);

		state->mSteps.emplace_back(vdrefptr(savedStep));

		if (step.mType == kStepType_Send || step.mType == kStepType_SendAutoProtocol) {
			savedStep->mTransferData.resize(step.mTransferLength);
			memcpy(savedStep->mTransferData.data(), mTransferBuffer + transferPos, step.mTransferLength);

			transferPos += step.mTransferLength;
		}
	}

	state->mDeviceId = mActiveDeviceId;
	state->mTransferStart = mTransferStart;
	state->mTransferIndex = mTransferIndex - mTransferStart;
	state->mbTransferError = mbTransferError;
	state->mTransferCyclesPerByte = mTransferCyclesPerByte;
	state->mTransferCyclesPerBit = mTransferCyclesPerBit;
	state->mbTransmitSynchronous = mbTransmitSynchronous;

	if (mpTransferEvent)
		state->mStepDelay = mpScheduler->GetTicksToEvent(mpTransferEvent);
	else if (mpDelayEvent)
		state->mStepDelay = mpScheduler->GetTicksToEvent(mpDelayEvent);

	*ppState = state.release();
}

void ATSIOManager::SetReadyState(bool ready) {
	mbReadyState = ready;
}

void ATSIOManager::PreLoadState() {
	AbortActiveCommand();

	mbLoadingState = true;
}

void ATSIOManager::PostLoadState() {
	mbLoadingState = false;
}

void ATSIOManager::LoadActiveCommandState(IATDeviceSIO *device, IATObjectState *state0) {
	if (!device || !state0)
		return;

	try {
		const ATSaveStateSioActiveCommand& state = atser_cast<const ATSaveStateSioActiveCommand&>(*state0);

		AbortActiveCommand();
		ResetTransfer();

		if (state.mTransferCyclesPerByte)
			mTransferCyclesPerByte = state.mTransferCyclesPerByte;

		if (state.mTransferCyclesPerBit)
			mTransferCyclesPerBit = state.mTransferCyclesPerBit;

		mActiveDeviceId = state.mDeviceId;
		mbTransmitSynchronous = state.mbTransmitSynchronous;
		mTransferStart = state.mTransferStart;
		mTransferEnd = mTransferStart;
		mbTransferError = state.mbTransferError;
		mbTransferSend = false;

		if (state.mpCurrentStep) {
			mCurrentStep = state.mpCurrentStep->mStep;

			switch(mCurrentStep.mType) {
				case kStepType_Receive:
				case kStepType_ReceiveAutoProtocol:
					if (vdcountof(mTransferBuffer) - mTransferStart < mCurrentStep.mTransferLength)
						throw ATInvalidSaveStateException();

					mTransferEnd = mTransferStart + mCurrentStep.mTransferLength;
					mbTransferSend = false;

					memcpy(mTransferBuffer + mTransferStart, state.mpCurrentStep->mTransferData.data(), mCurrentStep.mTransferLength);
					break;

				case kStepType_Send:
				case kStepType_SendAutoProtocol:
					mTransferEnd = mTransferStart + mCurrentStep.mTransferLength;
					mbTransferSend = true;

					if (state.mStepDelay)
						mpScheduler->SetEvent(state.mStepDelay, this, kEventId_Send, mpTransferEvent);
					break;

				case kStepType_Delay:
					if (state.mStepDelay)
						mpScheduler->SetEvent(state.mStepDelay, this, kEventId_Delay, mpDelayEvent);
					break;

				default:
					break;
			}
		}

		if (state.mTransferIndex > mTransferEnd - mTransferStart)
			throw ATInvalidSaveStateException();

		mTransferIndex = state.mTransferStart + state.mTransferIndex;
		mTransferLevel = mTransferEnd;

		for(const auto& step : state.mSteps) {
			if (step)
				mStepQueue.push_back(step->mStep);

			switch(step->mStep.mType) {
				case kStepType_Send:
				case kStepType_SendAutoProtocol:
				case kStepType_Receive:
				case kStepType_ReceiveAutoProtocol:
					if (vdcountof(mTransferBuffer) - mTransferLevel < step->mStep.mTransferLength)
						throw ATInvalidSaveStateException();

					if (step->mStep.mType == kStepType_Send || step->mStep.mType == kStepType_SendAutoProtocol)
						memcpy(&mTransferBuffer[mTransferLevel], step->mTransferData.data(), step->mStep.mTransferLength);

					mTransferLevel += step->mStep.mTransferLength;
					break;
			}
		}


		// sanity check transfer parameters
		if (mTransferCyclesPerBit > 100000 || mTransferCyclesPerByte > 1000000)
			throw ATInvalidSaveStateException();

		if (mTransferCyclesPerByte < mTransferCyclesPerBit * 8)
			throw ATInvalidSaveStateException();

		// derived parameter cleanup
		mpActiveDevice = device;

		UpdateActiveDeviceDerivedValues();
		UpdateTransferRateDerivedValues();

	} catch(...) {
		AbortActiveCommand();
		ResetTransfer();
		UpdateTransferRateDerivedValues();
		throw;
	}
}

void ATSIOManager::AddRawDevice(IATDeviceRawSIO *dev) {
	if (std::find(mSIORawDevices.begin(), mSIORawDevices.end(), dev) != mSIORawDevices.end())
		return;

	if (std::find(mSIORawDevicesNew.begin(), mSIORawDevicesNew.end(), dev) != mSIORawDevicesNew.end())
		return;

	if (mSIORawDevicesBusy)
		mSIORawDevicesNew.push_back(dev);
	else
		mSIORawDevices.push_back(dev);
}

void ATSIOManager::RemoveRawDevice(IATDeviceRawSIO *dev) {
	SetSIOInterrupt(dev, false);
	SetSIOProceed(dev, false);

	SetExternalClock(dev, 0, 0);

	auto it = std::find(mSIORawDevicesNew.begin(), mSIORawDevicesNew.end(), dev);
	if (it != mSIORawDevicesNew.end()) {
		mSIORawDevicesNew.erase(it);
		return;
	}

	auto it2 = std::find(mSIORawDevices.begin(), mSIORawDevices.end(), dev);
	if (it2 != mSIORawDevices.end()) {
		if (mSIORawDevicesBusy) {
			if (!(mSIORawDevicesBusy & 1))
				--mSIORawDevicesBusy;

			*it2 = nullptr;
		} else
			mSIORawDevices.erase(it2);
	}
}

void ATSIOManager::SendRawByte(uint8 byte, uint32 cyclesPerBit, bool synchronous, bool forceFramingError, bool simulateInput) {
	mpPokey->ReceiveSIOByte(byte, cyclesPerBit, simulateInput, false, synchronous, forceFramingError);

	TraceReceive(byte, cyclesPerBit);
}

void ATSIOManager::SetRawInput(bool input) {
	mpPokey->SetDataLine(input);
}

bool ATSIOManager::IsSIOCommandAsserted() const {
	return mbCommandState;
}

bool ATSIOManager::IsSIOMotorAsserted() const {
	return mbMotorState;
}

bool ATSIOManager::IsSIOReadyAsserted() const {
	return mbReadyState;
}

void ATSIOManager::SetSIOInterrupt(IATDeviceRawSIO *dev, bool state) {
	auto it = std::lower_bound(mSIOInterruptActive.begin(), mSIOInterruptActive.end(), dev);

	if (state) {
		if (it == mSIOInterruptActive.end() || *it != dev) {
			if (mSIOInterruptActive.empty())
				mpPIA->SetCB1(false);

			mSIOInterruptActive.insert(it, dev);
		}
	} else {
		if (it != mSIOInterruptActive.end()) {
			mSIOInterruptActive.erase(it);

			if (mSIOInterruptActive.empty())
				mpPIA->SetCB1(true);
		}
	}
}

void ATSIOManager::SetSIOProceed(IATDeviceRawSIO *dev, bool state) {
	auto it = std::lower_bound(mSIOProceedActive.begin(), mSIOProceedActive.end(), dev);

	if (state) {
		if (it == mSIOProceedActive.end() || *it != dev) {
			if (mSIOProceedActive.empty())
				mpPIA->SetCA1(false);

			mSIOProceedActive.insert(it, dev);
		}
	} else {
		if (it != mSIOProceedActive.end()) {
			mSIOProceedActive.erase(it);

			if (mSIOProceedActive.empty())
				mpPIA->SetCA1(true);
		}
	}
}

void ATSIOManager::SetExternalClock(IATDeviceRawSIO *dev, uint32 initialOffset, uint32 period) {
	bool updatePOKEY = false;

	VDASSERT(!period || std::find(mSIORawDevices.begin(), mSIORawDevices.end(), dev) != mSIORawDevices.end()
		|| std::find(mSIORawDevicesNew.begin(), mSIORawDevicesNew.end(), dev) != mSIORawDevicesNew.end());

	auto it = std::find_if(mExternalClocks.begin(), mExternalClocks.end(),
		[=](const ExternalClock& x) { return x.mpDevice == dev; });

	if (period) {
		uint32 timeBase = initialOffset + ATSCHEDULER_GETTIME(mpScheduler);

		if (it != mExternalClocks.end()) {
			if (it->mPeriod == period && it->mTimeBase == timeBase)
				return;

			mExternalClocks.erase(it);

			if (it == mExternalClocks.begin())
				updatePOKEY = true;
		}

		const ExternalClock newEntry { dev, timeBase, period };
		auto it2 = std::lower_bound(mExternalClocks.begin(), mExternalClocks.end(), newEntry,
			[](const ExternalClock& x, const ExternalClock& y) {
				return x.mPeriod < y.mPeriod;
			}
		);

		if (it2 == mExternalClocks.begin())
			updatePOKEY = true;

		mExternalClocks.insert(it2, newEntry);
	} else {
		if (it != mExternalClocks.end()) {
			if (it == mExternalClocks.begin())
				updatePOKEY = true;

			mExternalClocks.erase(it);
		}
	}

	if (updatePOKEY && mpPokey) {
		if (mExternalClocks.empty())
			mpPokey->SetExternalSerialClock(0, 0);
		else {
			auto clk = mExternalClocks.front();

			mpPokey->SetExternalSerialClock(clk.mTimeBase, clk.mPeriod);
		}
	}
}

void ATSIOManager::OnScheduledEvent(uint32 id) {
	switch(id) {
		case kEventId_Delay:
			mpDelayEvent = nullptr;
			mCurrentStep.mType = kStepType_None;
			ExecuteNextStep();
			break;

		case kEventId_Send:
			mpTransferEvent = nullptr;
			if (mTransferIndex < mTransferEnd) {
				uint8 c = mTransferBuffer[mTransferIndex++];

				mpPokey->ReceiveSIOByte(c, mTransferCyclesPerBit, true, mbActiveDeviceDisk ? mbDiskBurstTransfersEnabled : mbBurstTransfersEnabled, false, false);
				TraceReceive(c, mTransferCyclesPerBit);

				mpScheduler->SetEvent(mTransferCyclesPerByte + mTransferBurstOffset, this, kEventId_Send, mpTransferEvent);
				mTransferBurstOffset = 0;
			} else {
				mTransferStart = mTransferEnd;
				mCurrentStep.mType = kStepType_None;
				ExecuteNextStep();
			}
			break;
	}
}

uint8 ATSIOManager::OnHookSIOV(uint16 pc) {
	ATKernelDatabase kdb(mpMemory);

	// read out SIO block
	uint8 siodata[12];

	for(int i=0; i<12; ++i)
		siodata[i] = mpMemory->ReadByte(ATKernelSymbols::DDEVIC + i);

	// assemble parameter block
	ATSIORequest req;

	req.mDevice		= siodata[0] + siodata[1] - 1;
	req.mCommand	= siodata[2];
	req.mMode		= siodata[3];
	req.mTimeout	= siodata[6];
	req.mAddress	= VDReadUnalignedLEU16(&siodata[4]);
	req.mLength		= VDReadUnalignedLEU16(&siodata[8]);
	req.mSector		= VDReadUnalignedLEU16(&siodata[10]);

	for(int i=0; i<2; ++i)
		req.mAUX[i] = siodata[i + 10];

	return TryAccelRequest(req) ? 0x60 : 0;
}

void ATSIOManager::AbortActiveCommand() {
	if (mpActiveDevice) {
		mpActiveDevice->OnSerialAbortCommand();
		mpActiveDevice = nullptr;
	}

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpTransferEvent);
		mpScheduler->UnsetEvent(mpDelayEvent);
	}

	mStepQueue.clear();
	mCurrentStep.mType = kStepType_None;
}

void ATSIOManager::ExecuteNextStep() {
	while(!mCurrentStep.mType && !mStepQueue.empty()) {
		mCurrentStep = mStepQueue.front();
		mStepQueue.pop_front();

		switch(mCurrentStep.mType) {
			case kStepType_Send:
			case kStepType_SendAutoProtocol:
				if (!mpAccelRequest)
					g_ATLCSIOSteps("Sending %u bytes (%02X)\n", mCurrentStep.mTransferLength, mTransferBuffer[mTransferIndex]);

				mbTransferSend = true;
				mTransferStart = mTransferIndex;
				mTransferEnd = mTransferStart + mCurrentStep.mTransferLength;
				mTransferBurstOffset = 0;
				VDASSERT(mTransferEnd <= vdcountof(mTransferBuffer));

				if (mpAccelRequest) {
					if (mpAccelRequest->mMode & 0x40) {
						const uint32 len = mCurrentStep.mTransferLength + (mCurrentStep.mType == kStepType_SendAutoProtocol ? -1 : 0);
						const uint32 reqLen = mpAccelRequest->mLength;
						const uint32 minLen = std::min<uint32>(len, reqLen);
						const uint8 *src = mTransferBuffer + mTransferIndex;

						for(uint32 i=0; i<minLen; ++i)
							mpMemory->WriteByte(mAccelBufferAddress + i, src[i]);

						mAccelTimeSkew += (minLen + 1) * mTransferCyclesPerByte;

						uint8 checksum = ATComputeSIOChecksum(src, minLen);

						if (len < reqLen) {
							// We sent less data than SIO was expecting. This will cause a timeout.
							*mpAccelStatus = 0x8A;
						} else if (len > reqLen) {
							// We sent more data than SIO was expecting. This may cause a checksum
							// error.
							if (checksum != src[reqLen])
								*mpAccelStatus = 0x8F;
						}

						mpMemory->WriteByte(ATKernelSymbols::CHKSUM, checksum);

						const uint32 endAddr = mAccelBufferAddress + minLen;
						mpMemory->WriteByte(ATKernelSymbols::BUFRLO, (uint8)endAddr);
						mpMemory->WriteByte(ATKernelSymbols::BUFRHI, (uint8)(endAddr >> 8));
					}

					mTransferIndex = mTransferEnd;
					mCurrentStep.mType = kStepType_None;
				} else {
					OnScheduledEvent(kEventId_Send);
				}
				break;

			case kStepType_Receive:
			case kStepType_ReceiveAutoProtocol:
				if (!mpAccelRequest)
					g_ATLCSIOSteps("Receiving %u bytes\n", mCurrentStep.mTransferLength);

				mbTransferSend = false;
				mbTransferError = false;
				mTransferStart = mTransferIndex;
				mTransferEnd = mTransferStart + mCurrentStep.mTransferLength;

				UpdateTransferRateDerivedValues();

				if (mpAccelRequest) {
					const uint32 id = mCurrentStep.mTransferId;
					const uint32 len = mCurrentStep.mTransferLength + (mCurrentStep.mType == kStepType_ReceiveAutoProtocol ? -1 : 0);
					const uint32 reqLen = mpAccelRequest->mMode & 0x80 ? mpAccelRequest->mLength : 0;
					const uint32 minLen = std::min<uint32>(len, reqLen);

					for(uint32 i=0; i<minLen; ++i)
						mTransferBuffer[mTransferStart + i] = mpMemory->ReadByte(mAccelBufferAddress + i);

					mAccelTimeSkew += (minLen + 1) * (mTransferCyclesPerBit * 10);

					if (reqLen < len) {
						// SIO provided less data than we were expecting. Hmm. On an 810,
						// this will hang the drive indefinitely until the expected bytes
						// are provided. For now, we report a broken checksum to the device.
						memset(mTransferBuffer + mTransferStart + minLen, 0, len - minLen);

						mpActiveDevice->OnSerialReceiveComplete(id, mTransferBuffer + mTransferStart, len, false);
					} else if (reqLen > len) {
						// SIO provided more data than we were expecting. This may cause
						// a checksum error.
						const uint8 checksum = mpMemory->ReadByte(mAccelBufferAddress + minLen);

						mpActiveDevice->OnSerialReceiveComplete(id, mTransferBuffer + mTransferStart, len, checksum == ATComputeSIOChecksum(mTransferBuffer + mTransferStart, minLen));
					} else {
						mpActiveDevice->OnSerialReceiveComplete(id, mTransferBuffer + mTransferStart, len, true);
					}

					mTransferIndex = mTransferEnd;
					mCurrentStep.mType = kStepType_None;
				}
				break;

			case kStepType_SetTransferRate:
				mTransferCyclesPerBit = mCurrentStep.mTransferCyclesPerBit;
				mTransferCyclesPerByte = mCurrentStep.mTransferCyclesPerByte;
				mCurrentStep.mType = kStepType_None;
				break;

			case kStepType_SetSynchronousTransmit:
				mbTransmitSynchronous = mCurrentStep.mbEnable;
				mCurrentStep.mType = kStepType_None;
				break;

			case kStepType_Delay:
				VDASSERT(mCurrentStep.mDelayTicks);

				if (mpAccelRequest) {
					mAccelTimeSkew += mCurrentStep.mDelayTicks;
					mCurrentStep.mType = kStepType_None;
				} else {
					g_ATLCSIOSteps("Delaying for %u ticks\n", mCurrentStep.mDelayTicks);
					mpScheduler->SetEvent(mCurrentStep.mDelayTicks, this, kEventId_Delay, mpDelayEvent);
				}
				break;

			case kStepType_Fence:
				mCurrentStep.mType = kStepType_None;
				mpActiveDevice->OnSerialFence(mCurrentStep.mFenceId);
				break;

			case kStepType_EndCommand:
				if (!mpAccelRequest)
					g_ATLCSIOSteps <<= "Ending command\n";
				mpActiveDevice = nullptr;
				mCurrentStep.mType = kStepType_None;
				mStepQueue.clear();
				break;

			case kStepType_AccelSendACK:
			case kStepType_AccelSendComplete:
				mAccelTimeSkew += mTransferCyclesPerByte;
				mCurrentStep.mType = kStepType_None;
				break;

			case kStepType_AccelSendNAK:
				*mpAccelStatus = 0x8B;		// NAK error
				mCurrentStep.mType = kStepType_None;
				mAccelTimeSkew += mTransferCyclesPerByte;
				break;

			case kStepType_AccelSendError:
				*mpAccelStatus = 0x90;		// Device error
				mCurrentStep.mType = kStepType_None;
				mAccelTimeSkew += mTransferCyclesPerByte;
				break;

			default:
				VDFAIL("Unknown step in step queue.");
				break;
		}
	}
}

void ATSIOManager::ShiftTransmitBuffer() {
	if (mTransferStart > 0) {
		memmove(mTransferBuffer, mTransferBuffer + mTransferStart, mTransferLevel - mTransferStart);

		mTransferEnd -= mTransferStart;
		mTransferIndex -= mTransferStart;
		mTransferLevel -= mTransferStart;
		mTransferStart = 0;
	}
}

void ATSIOManager::ResetTransfer() {
	mTransferCyclesPerByte = 932;
	mTransferCyclesPerBit = 93;
	mbTransmitSynchronous = false;
}

void ATSIOManager::UpdateActiveDeviceDerivedValues() {
	mbActiveDeviceDisk = (mActiveDeviceId >= 0x31 && mActiveDeviceId <= 0x3F);
}

void ATSIOManager::UpdateTransferRateDerivedValues() {
	mTransferCyclesPerBitRecvMin = mTransferCyclesPerBit - (mTransferCyclesPerBit + 19)/20;
	mTransferCyclesPerBitRecvMax = mTransferCyclesPerBit + (mTransferCyclesPerBit + 19)/20;
}

void ATSIOManager::OnMotorStateChanged(bool asserted) {
	if (mbMotorState != asserted) {
		mbMotorState = asserted;

		RawDeviceListLock lock(this);

		for(auto *rawdev : mSIORawDevices) {
			if (rawdev)
				rawdev->OnMotorStateChanged(mbMotorState);
		}
	}
}

void ATSIOManager::TraceReceive(uint8 c, uint32 cyclesPerBit) {
	if (mpTraceContext) {
		const uint64 t = mpScheduler->GetTick64();
		mpTraceChannelBusReceive->AddTickEvent(t, t + cyclesPerBit * 10,
			[c](VDStringW& s) { s.sprintf(L"%02X", c); },
			kATTraceColor_IO_Read
		);
	}
}

void ATSIOManager::UpdatePollState(uint8 cmd, uint8 aux1, uint8 aux2) {
	if (cmd == 0x40 && aux1 == aux2) {
		if (aux1 == 0x00)		// Type 3 poll (??/40/00/00) - increment counter
			++mPollCount;
		else {
			// Any command that is not a type 3 poll command resets the counter. This
			// This includes the null poll (??/40/4E/4E) and poll reset (??/40/4F/4F).
			mPollCount = 0;
		}
	} else {
		// Any command to any dveice that is not a type 3 poll command resets
		// the counter.
		mPollCount = 0;
	}
}
