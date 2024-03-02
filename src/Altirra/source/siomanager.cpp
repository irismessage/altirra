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

#include "stdafx.h"
#include <vd2/system/binary.h>
#include "siomanager.h"
#include "simulator.h"
#include "cpu.h"
#include "cpuhookmanager.h"
#include "kerneldb.h"
#include "debuggerlog.h"
#include "uirender.h"
#include "hleutils.h"
#include "cassette.h"
#include <at/atcore/sioutils.h>

ATDebuggerLogChannel g_ATLCHookSIOReqs(false, false, "HOOKSIOREQS", "OS SIO hook requests");
ATDebuggerLogChannel g_ATLCHookSIO(false, false, "HOOKSIO", "OS SIO hook messages");
ATDebuggerLogChannel g_ATLCSIOCmd(false, false, "SIOCMD", "SIO bus commands");
ATDebuggerLogChannel g_ATLCSIOAccel(false, false, "SIOACCEL", "SIO command acceleration");
ATDebuggerLogChannel g_ATLCSIOSteps(false, false, "SIOSTEPS", "SIO command steps");

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

ATSIOManager::ATSIOManager()
	: mpCPU(NULL)
	, mpMemory(NULL)
	, mpSim(NULL)
	, mpUIRenderer(NULL)
	, mpPokey(nullptr)
	, mpPIA(nullptr)
	, mpSIOVHook(NULL)
	, mpDSKINVHook(NULL)
	, mTransferLevel(0)
	, mTransferStart(0)
	, mTransferIndex(0)
	, mTransferEnd(0)
	, mTransferCyclesPerBit(0)
	, mTransferCyclesPerByte(0)
	, mbTransferSend(false)
	, mbCommandState(false)
	, mbMotorState(false)
	, mbSIOPatchEnabled(false)
	, mbBurstTransfersEnabled(false)
	, mPollCount(0)
	, mpAccelRequest(nullptr)
	, mpAccelStatus(nullptr)
	, mpTransferEvent(nullptr)
	, mpActiveDevice(nullptr)
	, mSIORawDevicesBusy(0)
{
	mCurrentStep.mType = kStepType_None;
}

ATSIOManager::~ATSIOManager() {
}

void ATSIOManager::SetSIOPatchEnabled(bool enabled) {
	mbSIOPatchEnabled = enabled;

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
		mpScheduler->UnsetEvent(mpTransferEvent);
		mpScheduler = nullptr;
	}

	mpUIRenderer = NULL;
	mpSim = NULL;
	mpMemory = NULL;
	mpCPU = NULL;
}

void ATSIOManager::ColdReset() {
	AbortActiveCommand();

	mTransferLevel = 0;
	mTransferStart = 0;
	mTransferIndex = 0;
	mTransferEnd = 0;
	mbTransferSend = false;
	mbCommandState = false;
	mbMotorState = false;
	mpActiveDevice = nullptr;
}

void ATSIOManager::ReinitHooks() {
	UninitHooks();

	ATCPUHookManager& hookmgr = *mpCPU->GetHookManager();

	if (mbSIOPatchEnabled || mpSim->IsDiskSIOPatchEnabled() || mpSim->IsCassetteSIOPatchEnabled() || mpSim->IsFastBootEnabled())
		hookmgr.SetHookMethod(mpSIOVHook, kATCPUHookMode_KernelROMOnly, ATKernelSymbols::SIOV, 0, this, &ATSIOManager::OnHookSIOV);

	if (mpSim->IsDiskSIOPatchEnabled())
		hookmgr.SetHookMethod(mpDSKINVHook, kATCPUHookMode_KernelROMOnly, ATKernelSymbols::DSKINV, 0, this, &ATSIOManager::OnHookDSKINV);
}

void ATSIOManager::UninitHooks() {
	if (mpCPU) {
		ATCPUHookManager& hookmgr = *mpCPU->GetHookManager();

		hookmgr.UnsetHook(mpSIOVHook);
		hookmgr.UnsetHook(mpDSKINVHook);
	}
}

bool ATSIOManager::TryAccelRequest(const ATSIORequest& req, bool isDSKINV) {
	g_ATLCHookSIOReqs("Checking %s request: device $%02X, command $%02X\n", isDSKINV ? "DSKINV" : "SIOV", req.mDevice, req.mCommand);

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

	if (mbSIOPatchEnabled) {
		// Convert the request to a device request.
		ATDeviceSIORequest devreq = {};
		devreq.mDevice = req.mDevice;
		devreq.mCommand = req.mCommand;
		devreq.mAUX[0] = req.mAUX[0];
		devreq.mAUX[1] = req.mAUX[1];
		devreq.mCyclesPerBit = 93;
		devreq.mbStandardRate = true;
		devreq.mPollCount = 0;
		devreq.mMode = req.mMode;
		devreq.mTimeout = req.mTimeout;
		devreq.mLength = req.mLength;
		devreq.mSector = req.mSector;

		ResetTransferRate();

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
			mpAccelRequest = nullptr;
			mpAccelStatus = nullptr;

			// check if we were specifically asked not to accelerate this request
			if (response == IATDeviceSIO::kCmdResponse_BypassAccel)
				return false;

			// check if the device handled it
			if (response != IATDeviceSIO::kCmdResponse_NotHandled)
				goto handled;
		}
	}

	// Check hard-coded devices.
	if (req.mDevice >= 0x31 && req.mDevice <= 0x3F) {
		ATDiskEmulator& disk = mpSim->GetDiskDrive(req.mDevice - 0x31);

		if (mpSim->IsDiskSIOOverrideDetectEnabled()) {
			if (!disk.IsAccessed())
				return false;
		} else {
			if (!disk.IsEnabled()) {
				if (mpSim->IsFastBootEnabled())
					goto fastbootignore;

				return false;
			}
		}

		if (!mpSim->IsDiskSIOPatchEnabled())
			return false;

		if (req.mCommand == 0x52) {		// read
			if ((req.mMode & 0xc0) != 0x40)
				return false;

			status = disk.ReadSector(req.mAddress, req.mLength, req.mSector, mpMemory);

			// copy DBUFLO/DBUFHI + size -> BUFRLO/BUFRHI
			uint32 endAddr = req.mAddress + req.mLength;
			kdb.BUFRLO_BUFRHI = endAddr;

			g_ATLCHookSIO("Intercepting disk SIO read: buf=%04X, len=%04X, sector=%04X, status=%02X\n", req.mAddress, req.mLength, req.mSector, status);

			mpUIRenderer->PulseStatusFlags(1 << (req.mDevice - 0x31));

			// leave SKCTL set to asynchronous receive
			kdb.SKCTL = (kdb.SSKCTL & 0x07) | 0x10;
		} else if (req.mCommand == 0x50 || req.mCommand == 0x57) {
			if ((req.mMode & 0xc0) != 0x80)
				return 0;

			status = disk.WriteSector(req.mAddress, req.mLength, req.mSector, mpMemory);

			// copy DBUFLO/DBUFHI + size -> BUFRLO/BUFRHI
			uint32 endAddr = req.mAddress + req.mLength;
			kdb.BUFRLO_BUFRHI = endAddr;

			g_ATLCHookSIO("Intercepting disk SIO write: buf=%04X, len=%04X, sector=%04X, status=%02X\n", req.mAddress, req.mLength, req.mSector, status);

			mpUIRenderer->PulseStatusFlags(1 << (req.mDevice - 0x31));

			// leave SKCTL set to asynchronous receive
			kdb.SKCTL = (kdb.SSKCTL & 0x07) | 0x10;
		} else if (req.mCommand == 0x53) {
			if ((req.mMode & 0xc0) != 0x40)
				return false;

			if (req.mLength != 4)
				return false;

			uint8 data[5];
			disk.ReadStatus(data);

			for(int i=0; i<4; ++i)
				mpMemory->WriteByte(req.mAddress+i, data[i]);

			kdb.CHKSUM = data[4];

			// copy DBUFLO/DBUFHI + size -> BUFRLO/BUFRHI
			uint32 endAddr = req.mAddress + req.mLength;
			kdb.BUFRLO_BUFRHI = endAddr;

			g_ATLCHookSIO("Intercepting disk SIO status req.: buf=%04X\n", req.mAddress);

			mpUIRenderer->PulseStatusFlags(1 << (req.mDevice - 0x31));

			ATClearPokeyTimersOnDiskIo(kdb);
		} else if (req.mCommand == 0x4E) {
			if ((req.mMode & 0xc0) != 0x40)
				return false;

			if (req.mLength != 12)
				return false;

			uint8 data[13];
			disk.ReadPERCOMBlock(data);

			for(int i=0; i<12; ++i)
				mpMemory->WriteByte(req.mAddress+i, data[i]);

			mpMemory->WriteByte(ATKernelSymbols::CHKSUM, data[12]);

			// copy DBUFLO/DBUFHI + size -> BUFRLO/BUFRHI
			uint32 endAddr = req.mAddress + req.mLength;
			kdb.BUFRLO_BUFRHI = endAddr;

			g_ATLCHookSIO("Intercepting disk SIO read PERCOM req.: buf=%04X\n", req.mAddress);

			mpUIRenderer->PulseStatusFlags(1 << (req.mDevice - 0x31));
		} else
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
		if (req.mMode & 0x80)
			return false;

		status = cassette.ReadBlock(req.mAddress, req.mLength, mpMemory);

		mpUIRenderer->PulseStatusFlags(1 << 16);

		g_ATLCHookSIO("Intercepting cassette SIO read: buf=%04X, len=%04X, status=%02X\n", req.mAddress, req.mLength, status);
	} else {
		return false;
	}

handled:
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

	mpCPU->Ldy(status);

	return true;
}

void ATSIOManager::PokeyAttachDevice(ATPokeyEmulator *pokey) {
}

bool ATSIOManager::PokeyWriteSIO(uint8 c, bool command, uint32 cyclesPerBit) {
	if (mTransferIndex < mTransferEnd && !mbTransferSend) {
		mTransferBuffer[mTransferIndex++] = c;

		if (mbCommandState)
			mTransferCyclesPerBit = cyclesPerBit;
		else if (cyclesPerBit < mTransferCyclesPerBitRecvMin || cyclesPerBit > mTransferCyclesPerBitRecvMax)
			mbTransferError = true;

		if (mTransferIndex >= mTransferEnd && mpActiveDevice) {
			const uint32 transferLen = mTransferEnd - mTransferStart;
			const uint8 *data = mTransferBuffer + mTransferStart;
			const bool checksumOK = !mbTransferError && (!transferLen || ATComputeSIOChecksum(data, transferLen - 1) == data[transferLen - 1]);

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

	return mbBurstTransfersEnabled;
}

void ATSIOManager::PokeyBeginCommand() {
	AbortActiveCommand();

	mStepQueue.clear();
	mCurrentStep.mType = kStepType_None;

	mbTransferSend = false;
	mTransferIndex = 0;
	mTransferLevel = 0;
	mTransferStart = 0;
	mTransferEnd = 5;

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
		if (cmd.mCommand == 0x40 && cmd.mAUX[0] == cmd.mAUX[1]) {
			if (cmd.mAUX[0] == 0x00)		// Type 3 poll (??/40/00/00) - increment counter
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

		mTransferStart = 0;
		mTransferIndex = 0;
		mTransferEnd = 0;
		mTransferLevel = 0;

		if (g_ATLCSIOCmd.IsEnabled())
			g_ATLCSIOCmd("Device %02X | Command %02X | %02X %02X (%s)\n", cmd.mDevice, cmd.mCommand, cmd.mAUX[0], cmd.mAUX[1], ATDecodeSIOCommand(cmd.mDevice, cmd.mCommand, cmd.mAUX));

		ResetTransferRate();

		for(IATDeviceSIO *dev : mSIODevices) {
			mpActiveDevice = dev;

			const IATDeviceSIO::CmdResponse response = dev->OnSerialBeginCommand(cmd);
			if (response) {
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
	if (!mbBurstTransfersEnabled)
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

	if (mTransferIndex - mTransferStart < 2)
		return;

	if (mpScheduler->GetTicksToEvent(mpTransferEvent) > 50)
		mpScheduler->SetEvent(50, this, kEventId_Send, mpTransferEvent);
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
}

void ATSIOManager::SendData(const void *data, uint32 len, bool addChecksum) {
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
	if (!mpAccelRequest)
		SendData("A", 1, false);
}

void ATSIOManager::SendNAK() {
	if (mpAccelRequest) {
		Step& step = mStepQueue.push_back();
		step.mType = kStepType_AccelSendNAK;
	} else {
		SendData("N", 1, false);
	}
}

void ATSIOManager::SendComplete() {
	if (!mpAccelRequest) {
		// SIO protocol requires minimum 250us delay here.
		Delay(450);
		SendData("C", 1, false);
	}
}

void ATSIOManager::SendError() {
	if (mpAccelRequest) {
		Step& step = mStepQueue.push_back();
		step.mType = kStepType_AccelSendError;
	} else {
		// SIO protocol requires minimum 250us delay here.
		Delay(450);
		SendData("E", 1, false);
	}
}

void ATSIOManager::ReceiveData(uint32 id, uint32 len, bool autoProtocol) {
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
	Step& step = mStepQueue.push_back();
	step.mType = kStepType_SetTransferRate;
	step.mTransferCyclesPerBit = cyclesPerBit;
	step.mTransferCyclesPerByte = cyclesPerByte;

	ExecuteNextStep();
}

void ATSIOManager::Delay(uint32 ticks) {
	if (!ticks || mpAccelRequest)
		return;

	Step& step = mStepQueue.push_back();
	step.mType = kStepType_Delay;
	step.mDelayTicks = ticks;

	ExecuteNextStep();
}

void ATSIOManager::InsertFence(uint32 id) {
	Step& step = mStepQueue.push_back();
	step.mType = kStepType_Fence;
	step.mFenceId = id;
}

void ATSIOManager::EndCommand() {
	Step& step = mStepQueue.push_back();
	step.mType = kStepType_EndCommand;
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

void ATSIOManager::SendRawByte(uint8 byte, uint32 cyclesPerBit) {
	mpPokey->ReceiveSIOByte(byte, cyclesPerBit, true, mbBurstTransfersEnabled);
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
	mpTransferEvent = nullptr;

	switch(id) {
		case kEventId_Delay:
			mCurrentStep.mType = kStepType_None;
			ExecuteNextStep();
			break;

		case kEventId_Send:
			if (mTransferIndex < mTransferEnd) {
				uint8 c = mTransferBuffer[mTransferIndex++];

				mpPokey->ReceiveSIOByte(c, mTransferCyclesPerBit, true, mbBurstTransfersEnabled);

				mpScheduler->SetEvent(mTransferCyclesPerByte, this, kEventId_Send, mpTransferEvent);
			} else {
				mTransferStart = mTransferEnd;
				mCurrentStep.mType = kStepType_None;
				ExecuteNextStep();
			}
			break;
	}
}

uint8 ATSIOManager::OnHookDSKINV(uint16 pc) {
	ATKernelDatabase kdb(mpMemory);

	// check if we support the command
	const uint8 cmd = kdb.DCOMND;

	switch(cmd) {
		case 0x50:	// put
		case 0x52:	// read
		case 0x57:	// write
			break;

		default:
			return 0;
	}

	// Check if the I flag is set -- if so, bail. This will hang in SIO
	// if not intercepted by a PBI device.
	if (mpCPU->GetP() & AT6502::kFlagI)
		return 0;

	// Set transfer mode.
	kdb.DSTATS = (cmd == 0x52) ? 0x40 : 0x80;

	// Set sector size. If we have an XL bios, use DSCTLN, otherwise force 128 bytes.
	// Since hooks only trigger from ROM, we can check if the lower ROM exists.
	if (mpSim->GetKernelMode() == kATKernelMode_XL) {
		uint16 seclen = kdb.DSCTLN;

		// punt if sector length isn't 128 or 256 bytes -- this check is needed if
		// OS-B is running on XL+U1MB
		if (seclen != 128 && seclen != 256)
			return 0;

		kdb.DBYTLO_DBYTHI = seclen;
	} else
		kdb.DBYTLO_DBYTHI = 0x80;

	// set device and invoke SIOV
	kdb.DDEVIC = 0x31;

	uint8 opcode = OnHookSIOV(pc);
	if (!opcode)
		return 0;

	// We need to set the carry flag to satisfy Arcade Machine, which stupidly
	// relies on it being set after a CMP #'!' command check in the OS. Since
	// we only handle commands above that, the carry flag is always set.
	//
	// Update: This needs to be done only on success. Micropainter depends on
	//         DSKINV not changing the carry flag coming out of SIOV on failure.
	if (mpCPU->GetY() < 0x80)
		mpCPU->SetFlagC();

	return opcode;
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

	return TryAccelRequest(req, pc == ATKernelSymbols::DSKINV) ? 0x60 : 0;
}

void ATSIOManager::AbortActiveCommand() {
	if (mpActiveDevice) {
		mpActiveDevice->OnSerialAbortCommand();
		mpActiveDevice = nullptr;
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
				VDASSERT(mTransferEnd <= vdcountof(mTransferBuffer));

				if (mpAccelRequest) {
					const uint32 len = mCurrentStep.mTransferLength + (mCurrentStep.mType == kStepType_SendAutoProtocol ? -1 : 0);
					const uint32 reqLen = (mpAccelRequest->mMode & 0x40) ? mpAccelRequest->mLength : 0;
					const uint32 minLen = std::min<uint32>(len, reqLen);
					const uint8 *src = mTransferBuffer + mTransferIndex;

					for(uint32 i=0; i<minLen; ++i)
						mpMemory->WriteByte(mAccelBufferAddress + i, src[i]);

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

				mTransferCyclesPerBitRecvMin = mTransferCyclesPerBit - (mTransferCyclesPerBit + 19)/20;
				mTransferCyclesPerBitRecvMax = mTransferCyclesPerBit + (mTransferCyclesPerBit + 19)/20;

				if (mpAccelRequest) {
					const uint32 id = mCurrentStep.mTransferId;
					const uint32 len = mCurrentStep.mTransferLength + (mCurrentStep.mType == kStepType_ReceiveAutoProtocol ? -1 : 0);
					const uint32 reqLen = mpAccelRequest->mMode & 0x80 ? mpAccelRequest->mLength : 0;
					const uint32 minLen = std::min<uint32>(len, reqLen);

					for(uint32 i=0; i<minLen; ++i)
						mTransferBuffer[mTransferStart + i] = mpMemory->ReadByte(mAccelBufferAddress + i);

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

			case kStepType_Delay:
				g_ATLCSIOSteps("Delaying for %u ticks\n", mCurrentStep.mDelayTicks);

				VDASSERT(mCurrentStep.mDelayTicks);
				mpScheduler->SetEvent(mCurrentStep.mDelayTicks, this, kEventId_Delay, mpTransferEvent);
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
				break;

			case kStepType_AccelSendNAK:
				*mpAccelStatus = 0x8B;		// NAK error
				mCurrentStep.mType = kStepType_None;
				break;

			case kStepType_AccelSendError:
				*mpAccelStatus = 0x90;		// Device error
				mCurrentStep.mType = kStepType_None;
				break;

			default:
				VDASSERT("Unknown step in step queue.");
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

void ATSIOManager::ResetTransferRate() {
	mTransferCyclesPerByte = 932;
	mTransferCyclesPerBit = 93;
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
