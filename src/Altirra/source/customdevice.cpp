//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/date.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/logging.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/vfs.h>
#include "customdevice.h"
#include "customdevicecompiler.h"
#include "customdevice_win32.h"
#include "inputcontroller.h"
#include "memorymanager.h"
#include "simulator.h"

extern ATSimulator g_sim;

ATLogChannel g_ATLCCustomDev(true, false, "CUSTOMDEV", "Custom device");

void ATCreateDeviceCustom(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceCustom> p(new ATDeviceCustom);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefCustom = { "custom", "custom", L"Custom", ATCreateDeviceCustom };

/////////////////////////////////////////////////////////////////////////////

const ATCDVMObjectClass ATDeviceCustom::Segment::kVMObjectClass {
	"Segment",
	{
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Segment::VMCallClear>("clear"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Segment::VMCallFill>("fill"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Segment::VMCallCopy>("copy"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Segment::VMCallReadByte>("read_byte"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Segment::VMCallWriteByte>("write_byte")
	}
};

const ATCDVMObjectClass ATDeviceCustom::MemoryLayer::kVMObjectClass {
	"MemoryLayer",
	{
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::MemoryLayer::VMCallSetOffset>("set_offset"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::MemoryLayer::VMCallSetSegmentAndOffset>("set_segment_and_offset"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::MemoryLayer::VMCallSetModes>("set_modes"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::MemoryLayer::VMCallSetReadOnly>("set_readonly")
	}
};

const ATCDVMObjectClass ATDeviceCustom::Network::kVMObjectClass {
	"Network",
	{
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Network::VMCallSendMessage>("send_message"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Network::VMCallPostMessage>("post_message"),
	}
};

const ATCDVMObjectClass ATDeviceCustom::SIO::kVMObjectClass {
	"SIO",
	{
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallAck>("ack"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallNak>("nak"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallError>("error"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallComplete>("complete"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallSendFrame>("send_frame", ATCDVMFunctionFlags::AsyncSIO),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallRecvFrame>("recv_frame", ATCDVMFunctionFlags::AsyncSIO),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallDelay>("delay", ATCDVMFunctionFlags::AsyncSIO),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallEnableRaw>("enable_raw"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallSetProceed>("set_proceed"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallSetInterrupt>("set_interrupt"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallCommandAsserted>("command_asserted"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallMotorAsserted>("motor_asserted"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallSendRawByte>("send_raw_byte", ATCDVMFunctionFlags::AsyncRawSIO),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallRecvRawByte>("recv_raw_byte", ATCDVMFunctionFlags::AsyncRawSIO),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallWaitCommand>("wait_command", ATCDVMFunctionFlags::AsyncRawSIO),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallWaitCommandOff>("wait_command_off", ATCDVMFunctionFlags::AsyncRawSIO),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallWaitMotorChanged>("wait_motor_changed", ATCDVMFunctionFlags::AsyncRawSIO),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallResetRecvChecksum>("reset_recv_checksum"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallResetSendChecksum>("reset_send_checksum"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallGetRecvChecksum>("get_recv_checksum"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallCheckRecvChecksum>("check_recv_checksum"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::SIO::VMCallGetSendChecksum>("get_send_checksum"),
	}
};

const ATCDVMObjectClass ATDeviceCustom::SIODevice::kVMObjectClass {
	"SIODevice",
	{}
};

const ATCDVMObjectClass ATDeviceCustom::PBIDevice::kVMObjectClass {
	"PBIDevice",
	{}
};

const ATCDVMObjectClass ATDeviceCustom::Clock::kVMObjectClass {
	"Clock",
	{
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Clock::VMCallCaptureLocalTime>("capture_local_time"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Clock::VMCallLocalYear>("local_year"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Clock::VMCallLocalMonth>("local_month"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Clock::VMCallLocalDay>("local_day"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Clock::VMCallLocalDayOfWeek>("local_day_of_week"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Clock::VMCallLocalHour>("local_hour"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Clock::VMCallLocalMinute>("local_minute"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Clock::VMCallLocalSecond>("local_second"),
	}
};

const ATCDVMObjectClass ATDeviceCustom::ControllerPort::kVMObjectClass {
	"ControllerPort",
	{
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::ControllerPort::VMCallSetPaddleA>("set_paddle_a"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::ControllerPort::VMCallSetPaddleB>("set_paddle_b"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::ControllerPort::VMCallSetTrigger>("set_trigger"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::ControllerPort::VMCallSetDirs>("set_dirs"),
	}
};

const ATCDVMObjectClass ATDeviceCustom::Debug::kVMObjectClass {
	"Debug",
	{
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Debug::VMCallLog>("log"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::Debug::VMCallLogInt>("log_int"),
	}
};

const ATCDVMObjectClass ATDeviceCustom::ScriptThread::kVMObjectClass {
	"Thread",
	{
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::ScriptThread::VMCallIsRunning>("is_running"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::ScriptThread::VMCallRun>("run"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::ScriptThread::VMCallInterrupt>("interrupt"),
		ATCDVMExternalMethod::Bind<&ATDeviceCustom::ScriptThread::VMCallSleep>("sleep"),
	}
};

/////////////////////////////////////////////////////////////////////////////

ATDeviceCustom::ATDeviceCustom() {
	mNetwork.mpParent = this;
	mSIO.mpParent = this;
	mSIOFrameSegment.mbReadOnly = true;
	mSIOFrameSegment.mbSpecial = true;
	mClock.mpParent = this;

	mVMDomain.mpParent = this;

	mpSleepAbortFn = [this](ATCDVMThread& thread) {
		AbortThreadSleep(thread);
	};

	mpRawSendAbortFn = [this](ATCDVMThread& thread) {
		AbortRawSend(thread);
	};
}

void *ATDeviceCustom::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceMemMap::kTypeID: return static_cast<IATDeviceMemMap *>(this);
		case IATDeviceCartridge::kTypeID: return static_cast<IATDeviceCartridge *>(this);
		case IATDeviceIndicators::kTypeID: return static_cast<IATDeviceIndicators *>(this);
		case IATDeviceScheduling::kTypeID: return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceSIO::kTypeID: return static_cast<IATDeviceSIO *>(this);
		case IATDeviceRawSIO::kTypeID: return static_cast<IATDeviceRawSIO *>(this);
		case IATDevicePBIConnection::kTypeID: return static_cast<IATDevicePBIConnection *>(this);
		default:
			return ATDevice::AsInterface(iid);
	}
}

void ATDeviceCustom::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefCustom;
}

void ATDeviceCustom::GetSettingsBlurb(VDStringW& buf) {
	if (!mDeviceName.empty())
		buf += mDeviceName;
	else
		buf += mConfigPath;
}

void ATDeviceCustom::GetSettings(ATPropertySet& settings) {
	settings.SetString("path", mConfigPath.c_str());
	settings.SetBool("hotreload", mbHotReload);
}

bool ATDeviceCustom::SetSettings(const ATPropertySet& settings) {
	VDStringW newPath(settings.GetString("path", L""));
	const bool hotReload = settings.GetBool("hotreload");

	if (mConfigPath != newPath || mbHotReload != hotReload) {
		mConfigPath = newPath;
		mbHotReload = hotReload;

		if (mbInited)
			ReloadConfig();
	}

	return true;
}

void ATDeviceCustom::Init() {
	const auto hardwareMode = g_sim.GetHardwareMode();
	const bool hasPort34 = (hardwareMode != kATHardwareMode_800XL && hardwareMode != kATHardwareMode_1200XL && hardwareMode != kATHardwareMode_XEGS && hardwareMode != kATHardwareMode_130XE);

	for(int i=0; i<4; ++i) {
		auto *port = mpControllerPorts[i];

		if (!port)
			continue;

		if (i < 2 || hasPort34)
			port->Enable();
		else
			port->Disable();
	}

	mpAsyncDispatcher = mpDeviceManager->GetService<IATAsyncDispatcher>();

	mbInited = true;

	ReloadConfig();

	if (mpNetworkEngine)
		mpNetworkEngine->WaitForFirstConnectionAttempt();
}

void ATDeviceCustom::Shutdown() {
	mbInited = false;

	ShutdownCustomDevice();

	mpAsyncDispatcher = nullptr;
	mpIndicators = nullptr;
	mpCartPort = nullptr;
	mpMemMan = nullptr;
	mpScheduler = nullptr;
	mpSIOMgr = nullptr;
	mpPBIMgr = nullptr;
}

void ATDeviceCustom::ColdReset() {
	if (!mbInitedCustomDevice) {
		uint64 t = mpScheduler->GetTick64();

		if (mLastReloadAttempt != t) {
			mLastReloadAttempt = t;

			ReloadConfig();
		}
	}

	ReinitSegmentData(false);

	ResetCustomDevice();

	if (mpScriptEventColdReset) {
		mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
		mVMThread.RunVoid(*mpScriptEventColdReset);
	}

	if (mpNetworkEngine) {
		if (mpNetworkEngine->IsConnected())
			SendNetCommand(0, 0, NetworkCommand::ColdReset);
		else {
			uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

			if (mLastConnectionErrorCycle != t) {
				mLastConnectionErrorCycle = t;

				mpIndicators->ReportError(L"No connection to device server. Custom device may not function properly.");
			}
		}
	}

	// reapply controller port state in case hardware mode has changed and we need to
	// set ports that were previously disabled (3/4)
	for(auto *port : mpControllerPorts) {
		if (port)
			port->Reapply();
	}

	RunReadyThreads();
}

void ATDeviceCustom::WarmReset() {	
	if (mpScriptEventWarmReset) {
		mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
		mVMThread.RunVoid(*mpScriptEventWarmReset);
	}

	if (mpNetworkEngine)
		SendNetCommand(0, 0, NetworkCommand::WarmReset);

	RunReadyThreads();
}

bool ATDeviceCustom::GetErrorStatus(uint32 idx, VDStringW& error) {
	if (!mLastError.empty()) {
		if (!idx--) {
			error = mLastError;
			return true;
		}
	}

	if (mpNetworkEngine && !mpNetworkEngine->IsConnected()) {
		if (!idx--) {
			error = L"No connection to device server. Custom device may not function properly.";
			return true;
		}
	}

	return false;
}

void ATDeviceCustom::InitMemMap(ATMemoryManager *memmap) {
	mpMemMan = memmap;
}

bool ATDeviceCustom::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	if (index >= mMemoryLayers.size())
		return false;

	const auto& ml = *mMemoryLayers[index];
	lo = ml.mAddressBase;
	hi = ml.mAddressBase + ml.mSize;
	return true;
}

void ATDeviceCustom::InitCartridge(IATDeviceCartridgePort *cartPort) {
	mpCartPort = cartPort;
}

bool ATDeviceCustom::IsLeftCartActive() const {
	for(const MemoryLayer *ml : mMemoryLayers) {
		if (ml->mbRD5Active)
			return true;
	}

	return false;
}

void ATDeviceCustom::SetCartEnables(bool leftEnable, bool rightEnable, bool cctlEnable) {
	for(const MemoryLayer *ml : mMemoryLayers) {
		if (!ml->mpPhysLayer)
			continue;

		if (ml->mbAutoRD4) {
			if (rightEnable)
				mpMemMan->ClearLayerMaskRange(ml->mpPhysLayer);
			else
				mpMemMan->SetLayerMaskRange(ml->mpPhysLayer, 0, 0);
		}

		if (ml->mbAutoRD5) {
			if (leftEnable)
				mpMemMan->ClearLayerMaskRange(ml->mpPhysLayer);
			else
				mpMemMan->SetLayerMaskRange(ml->mpPhysLayer, 0, 0);
		}

		if (ml->mbAutoCCTL) {
			if (cctlEnable)
				mpMemMan->ClearLayerMaskRange(ml->mpPhysLayer);
			else
				mpMemMan->SetLayerMaskRange(ml->mpPhysLayer, 0, 0);
		}
	}
}

void ATDeviceCustom::UpdateCartSense(bool leftActive) {
}

void ATDeviceCustom::InitIndicators(IATDeviceIndicatorManager *r) {
	mpIndicators = r;
}

void ATDeviceCustom::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
}

void ATDeviceCustom::OnScheduledEvent(uint32 id) {
	if (id == kEventId_Sleep) {
		mpEventThreadSleep = nullptr;

		const uint64 t = mpScheduler->GetTick64();
		while(!mSleepHeap.empty()) {
			const auto& si = mSleepHeap.front();
			if (si.mWakeTime > t)
				break;

			mVMThreadRunQueue.Suspend(*mVMDomain.mThreads[si.mThreadIndex]);

			std::pop_heap(mSleepHeap.begin(), mSleepHeap.end(), SleepInfoPred());
			mSleepHeap.pop_back();
		}

		UpdateThreadSleep();
		RunReadyThreads();
	} else if (id == kEventId_RawSend) {
		mpEventRawSend = nullptr;

		if (!mRawSendQueue.empty()) {
			const sint32 threadIndex = mRawSendQueue.front().mThreadIndex;
			mRawSendQueue.pop_front();
			SendNextRawByte();

			// this may be -1 if the head thread was interrupted; we needed to keep the
			// entry to avoid resuming an unrelated thread
			if (threadIndex >= 0) {
				ATCDVMThread& thread = *mVMDomain.mThreads[threadIndex];
				thread.Resume();
			}

			RunReadyThreads();
		}
	}
}

void ATDeviceCustom::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
}

IATDeviceSIO::CmdResponse ATDeviceCustom::OnSerialAccelCommand(const ATDeviceSIORequest& cmd) {
	const SIODevice *device = mpSIODeviceTable->mpDevices[cmd.mDevice];
	if (!device)
		return OnSerialBeginCommand(cmd);

	if (!device->mbAllowAccel)
		return IATDeviceSIO::kCmdResponse_BypassAccel;

	const SIOCommand *cmdEntry = device->mpCommands[cmd.mCommand];
	if (cmdEntry && !cmdEntry->mbAllowAccel)
		return IATDeviceSIO::kCmdResponse_BypassAccel;

	return OnSerialBeginCommand(cmd);
}

IATDeviceSIO::CmdResponse ATDeviceCustom::OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) {
	if (mbInitedRawSIO)
		return IATDeviceSIO::kCmdResponse_NotHandled;

	if (!cmd.mbStandardRate)
		return IATDeviceSIO::kCmdResponse_NotHandled;

	const SIODevice *device = mpSIODeviceTable->mpDevices[cmd.mDevice];
	if (!device)
		return IATDeviceSIO::kCmdResponse_NotHandled;

	const SIOCommand *cmdEntry = device->mpCommands[cmd.mCommand];
	if (!cmdEntry)
		return IATDeviceSIO::kCmdResponse_Fail_NAK;

	if (cmdEntry->mpAutoTransferSegment) {
		mpActiveCommand = cmdEntry;

		mpSIOMgr->SendACK();

		if (cmdEntry->mbAutoTransferWrite) {
			mpSIOMgr->ReceiveData(0, cmdEntry->mAutoTransferLength, true);
			mpSIOMgr->SendComplete();
		} else {
			mpSIOMgr->SendComplete();
			mpSIOMgr->SendData(cmdEntry->mpAutoTransferSegment->mpData + cmdEntry->mAutoTransferOffset, cmdEntry->mAutoTransferLength, true);
		}

		mpSIOMgr->EndCommand();

		return IATDeviceSIO::kCmdResponse_Start;
	}

	if (!cmdEntry->mpScript)
		return IATDeviceSIO::kCmdResponse_Send_ACK_Complete;
	
	mpActiveCommand = cmdEntry;

	mVMThreadSIO.mThreadVariables[(int)ThreadVarIndex::Command] = cmd.mCommand;
	mVMThreadSIO.mThreadVariables[(int)ThreadVarIndex::Device] = cmd.mDevice;
	mVMThreadSIO.mThreadVariables[(int)ThreadVarIndex::Aux1] = cmd.mAUX[0];
	mVMThreadSIO.mThreadVariables[(int)ThreadVarIndex::Aux2] = cmd.mAUX[1];
	mVMThreadSIO.mThreadVariables[(int)ThreadVarIndex::Aux] = VDReadUnalignedLEU16(cmd.mAUX);
	mVMThreadSIO.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);

	mSIO.mbDataFrameReceived = false;
	mSIO.mbValid = true;
	if (mVMThreadSIO.RunVoid(*cmdEntry->mpScript))
		mpSIOMgr->EndCommand();
	mSIO.mbValid = false;

	return IATDeviceSIO::kCmdResponse_Start;
}

void ATDeviceCustom::OnSerialAbortCommand() {
	mVMThreadSIO.Abort();
	mpActiveCommand = nullptr;
}

void ATDeviceCustom::OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) {
	if (!mpActiveCommand)
		return;

	if (id == (uint32)SerialFenceId::AutoReceive) {
		memcpy(mpActiveCommand->mpAutoTransferSegment->mpData + mpActiveCommand->mAutoTransferOffset, data, len);
	} else if (id == (uint32)SerialFenceId::ScriptReceive) {
		mSIOFrameSegment.mpData = (uint8 *)data;
		mSIOFrameSegment.mSize = len;
		mSIO.mbValid = true;
		
		if (mVMThreadSIO.Resume())
			mpSIOMgr->EndCommand();

		mSIO.mbValid = false;
		mSIOFrameSegment.mpData = nullptr;
		mSIOFrameSegment.mSize = 0;
	}
}

void ATDeviceCustom::OnSerialFence(uint32 id) {
	if (id == (uint32)SerialFenceId::ScriptDelay) {
		mSIO.mbValid = true;

		if (mVMThreadSIO.Resume())
			mpSIOMgr->EndCommand();

		mSIO.mbValid = false;
	}
}

void ATDeviceCustom::OnCommandStateChanged(bool asserted) {
	if (mpScriptEventSIOCommandChanged) {
		mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
		mVMThread.RunVoid(*mpScriptEventSIOCommandChanged);
	}

	if (asserted) {
		mVMThreadSIOCommandAssertQueue.ResumeVoid();
		RunReadyThreads();
	} else {
		mVMThreadSIOCommandOffQueue.ResumeVoid();
		RunReadyThreads();
	}
}

void ATDeviceCustom::OnMotorStateChanged(bool asserted) {
	if (mpScriptEventSIOMotorChanged) {
		mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
		mVMThread.RunVoid(*mpScriptEventSIOMotorChanged);
	}

	mVMThreadSIOMotorChangedQueue.ResumeVoid();
	RunReadyThreads();
}

void ATDeviceCustom::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	if (mpScriptEventSIOReceivedByte) {
		mVMDomain.mSpecialVariables[(int)SpecialVarIndex::Value] = c;
		mVMThread.mThreadVariables[(int)ThreadVarIndex::Command] = command;
		mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
		mVMThread.RunVoid(*mpScriptEventSIOReceivedByte);
		RunReadyThreads();
	} else {
		ATCDVMThread *thread = mVMThreadRawRecvQueue.GetNext();

		if (thread) {
			mSIO.mRecvChecksum += c;
			mSIO.mRecvLast = c;

			thread->mThreadVariables[(int)ThreadVarIndex::Aux] = cyclesPerBit;
			mVMThreadRawRecvQueue.ResumeInt(c);
			RunReadyThreads();
		}
	}
}

void ATDeviceCustom::OnSendReady() {
}

void ATDeviceCustom::InitPBI(IATDevicePBIManager *pbiman) {
	mpPBIMgr = pbiman;
}

void ATDeviceCustom::GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const {
	devInfo.mbHasIrq = false;
	devInfo.mDeviceId = mPBIDeviceId;
}

void ATDeviceCustom::SelectPBIDevice(bool enable) {
	if (mbPBIDeviceSelected != enable) {
		mbPBIDeviceSelected = enable;

		for(MemoryLayer *ml : mMemoryLayers) {
			if (ml->mbAutoPBI)
				UpdateLayerModes(*ml);
		}

		if (enable) {
			if (mpScriptEventPBISelect) {
				mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
				mVMThread.RunVoid(*mpScriptEventPBISelect);
				RunReadyThreads();
			}
		} else {
			if (mpScriptEventPBIDeselect) {
				mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
				mVMThread.RunVoid(*mpScriptEventPBIDeselect);
				RunReadyThreads();
			}
		}
	}
}

bool ATDeviceCustom::IsPBIOverlayActive() const {
	if (!mbPBIDeviceSelected)
		return false;

	for(const auto *ml : mMemoryLayers) {
		if (ml->mbAutoPBI && ml->mAddressBase >= 0xD800 && ml->mAddressBase < 0xE000 && (ml->mEnabledModes & kATMemoryAccessMode_R))
			return true;
	}

	return false;
}

uint8 ATDeviceCustom::ReadPBIStatus(uint8 busData, bool debugOnly) {
	return busData;
}

bool ATDeviceCustom::OnFileUpdated(const wchar_t *path) {
	if (CheckForTrackedChanges()) {
		ReloadConfig();

		if (mbInitedCustomDevice && mpIndicators) {
			mpIndicators->SetStatusMessage(L"Custom device configuration reloaded.");
			ColdReset();
		}
	}

	return true;
}

template<bool T_DebugOnly>
sint32 ATDeviceCustom::ReadControl(MemoryLayer& ml, uint32 addr) {
	const AddressBinding& binding = ml.mpReadBindings[addr - ml.mAddressBase];
	sint32 v;

	switch(binding.mAction) {
		case AddressAction::None:
			return -1;

		case AddressAction::ConstantData:
			return binding.mByteData;

		case AddressAction::Network:
			return 0xFF & SendNetCommand(addr, 0, T_DebugOnly ? NetworkCommand::DebugReadByte : NetworkCommand::ReadByte);

		case AddressAction::Script:
			mVMDomain.mSpecialVariables[(int)SpecialVarIndex::Address] = addr;
			mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);

			v = 0xFF & mVMThread.RunInt(*mScriptFunctions[binding.mScriptFunctions[T_DebugOnly]]);
			RunReadyThreads();
			return v;

		case AddressAction::Variable:
			return 0xFF & mVMDomain.mGlobalVariables[binding.mVariableIndex];
	}

	return -1;
}

bool ATDeviceCustom::WriteControl(MemoryLayer& ml, uint32 addr, uint8 value) {
	const AddressBinding& binding = ml.mpWriteBindings[addr - ml.mAddressBase];

	switch(binding.mAction) {
		case AddressAction::None:
			return false;

		case AddressAction::Block:
			return true;

		case AddressAction::Network:
			SendNetCommand(addr, value, NetworkCommand::WriteByte);
			return true;

		case AddressAction::Script:
			mVMDomain.mSpecialVariables[(int)SpecialVarIndex::Address] = addr;
			mVMDomain.mSpecialVariables[(int)SpecialVarIndex::Value] = value;
			mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);

			mVMThread.RunVoid(*mScriptFunctions[binding.mScriptFunctions[0]]);
			RunReadyThreads();
			return true;

		case AddressAction::Variable:
			mVMDomain.mGlobalVariables[binding.mVariableIndex] = value;
			return true;
	}

	return false;
}

bool ATDeviceCustom::PostNetCommand(uint32 address, sint32 value, NetworkCommand cmd) {
	if (!mpNetworkEngine)
		return false;

	uint8 cmdbuf[17];
	cmdbuf[0] = (uint8)cmd;
	VDWriteUnalignedLEU32(&cmdbuf[1], address);
	VDWriteUnalignedLEU32(&cmdbuf[5], value);
	VDWriteUnalignedLEU64(&cmdbuf[9], mpScheduler->GetTick64());

	for(;;) {
		if (mpNetworkEngine->Send(cmdbuf, 17))
			break;

		if (!mpNetworkEngine->Restore())
			return false;

		mpNetworkEngine->SetRecvNotifyEnabled(true);
	}

	return true;
}

sint32 ATDeviceCustom::SendNetCommand(uint32 address, sint32 value, NetworkCommand cmd) {
	if (!mpNetworkEngine)
		return value;

	if (!PostNetCommand(address, value, cmd))
		return value;

	return ExecuteNetRequests(true);
}

sint32 ATDeviceCustom::ExecuteNetRequests(bool waitingForReply) {
	uint8 cmdbuf[16];
	sint32 returnValue = 0;

	for(;;) {
		if (!waitingForReply) {
			if (mpNetworkEngine->SetRecvNotifyEnabled(false))
				break;
		}

		if (!mpNetworkEngine->Recv(cmdbuf, 1)) {
			waitingForReply = false;

			if (!mpNetworkEngine->Restore())
				return returnValue;

			continue;
		}

		switch(cmdbuf[0]) {
			case (uint8)NetworkReply::None:
				break;

			case (uint8)NetworkReply::ReturnValue:
				mpNetworkEngine->Recv(cmdbuf, 4);
				returnValue = VDReadUnalignedLEU32(&cmdbuf[0]);
				waitingForReply = false;
				break;

			case (uint8)NetworkReply::EnableMemoryLayer:
				mpNetworkEngine->Recv(cmdbuf, 2);
				if (cmdbuf[0] < mMemoryLayers.size()) {
					auto *ml = mMemoryLayers[cmdbuf[0]];

					if (ml->mpPhysLayer) {
						uint8 modes = 0;

						if (cmdbuf[1] & 0x01)
							modes |= kATMemoryAccessMode_W;

						if (cmdbuf[1] & 0x02)
							modes |= kATMemoryAccessMode_AR;

						if (ml->mEnabledModes != modes) {
							ml->mEnabledModes = modes;

							UpdateLayerModes(*ml);
						}
					}
				} else
					PostNetError("EnableMemoryLayer: Invalid memory layer index");
				break;

			case (uint8)NetworkReply::SetMemoryLayerOffset:
				mpNetworkEngine->Recv(cmdbuf, 5);
				if (cmdbuf[0] < mMemoryLayers.size()) {
					auto *ml = mMemoryLayers[cmdbuf[0]];

					if (ml->mpPhysLayer && ml->mpSegment) {
						uint32 offset = VDReadUnalignedLEU32(&cmdbuf[1]);

						if (!(offset & 0xFF) && offset <= ml->mMaxOffset)
							mpMemMan->SetLayerMemory(ml->mpPhysLayer, ml->mpSegment->mpData + offset);
					} else
						PostNetError("SetMemoryLayerOffset: Invalid memory layer offset");
				} else
					PostNetError("SetMemoryLayerOffset: Invalid memory layer index");
				break;

			case (uint8)NetworkReply::SetMemoryLayerSegmentOffset:
				mpNetworkEngine->Recv(cmdbuf, 6);
				if (cmdbuf[0] < mMemoryLayers.size() && cmdbuf[1] < mSegments.size()) {
					auto& ml = *mMemoryLayers[cmdbuf[0]];
					auto& seg = *mSegments[cmdbuf[1]];

					uint32 offset = VDReadUnalignedLEU32(&cmdbuf[2]);
					if (ml.mpPhysLayer && !(offset & 0xFF) && offset < seg.mSize && seg.mSize - offset >= ml.mSize) {
						ml.mpSegment = &seg;
						ml.mMaxOffset = seg.mSize - ml.mSize;

						mpMemMan->SetLayerMemory(ml.mpPhysLayer, seg.mpData + offset);
					} else
						PostNetError("SetMemoryLayerSegmentOffset: Invalid memory layer range");
				} else
					PostNetError("SetMemoryLayerSegmentOffset: Invalid memory layer index");
				break;

			case (uint8)NetworkReply::SetMemoryLayerReadOnly:
				mpNetworkEngine->Recv(cmdbuf, 2);
				if (cmdbuf[0] < mMemoryLayers.size()) {
					auto *ml = mMemoryLayers[cmdbuf[0]];

					if (ml->mpPhysLayer)
						mpMemMan->SetLayerReadOnly(ml->mpPhysLayer, cmdbuf[1] != 0);
				} else
					PostNetError("SetMemoryLayerReadOnly: Invalid memory layer index");
				break;

			case (uint8)NetworkReply::ReadSegmentMemory:
				mpNetworkEngine->Recv(cmdbuf, 9);
				if (cmdbuf[0] < mSegments.size()) {
					auto& seg = *mSegments[cmdbuf[0]];
					uint32 offset = VDReadUnalignedLEU32(&cmdbuf[1]);
					uint32 len = VDReadUnalignedLEU32(&cmdbuf[5]);

					if (offset <= seg.mSize && seg.mSize - offset >= len) {
						mpNetworkEngine->Send(seg.mpData + offset, len);
					} else
						PostNetError("ReadSegmentMemory: Invalid segment range");
				} else
					PostNetError("ReadSegmentMemory: Invalid segment index");
				break;

			case (uint8)NetworkReply::WriteSegmentMemory:
				mpNetworkEngine->Recv(cmdbuf, 9);
				if (cmdbuf[0] < mSegments.size()) {
					auto& seg = *mSegments[cmdbuf[0]];
					uint32 offset = VDReadUnalignedLEU32(&cmdbuf[1]);
					uint32 len = VDReadUnalignedLEU32(&cmdbuf[5]);

					if (offset <= seg.mSize && seg.mSize - offset >= len) {
						mpNetworkEngine->Recv(seg.mpData + offset, len);
					} else
						PostNetError("WriteSegmentMemory: Invalid segment range");
				} else
					PostNetError("WriteSegmentMemory: Invalid segment index");
				break;

			case (uint8)NetworkReply::CopySegmentMemory:
				mpNetworkEngine->Recv(cmdbuf, 14);
				if (cmdbuf[0] < mSegments.size() && cmdbuf[5] < mSegments.size()) {
					auto& dstSeg = *mSegments[cmdbuf[0]];
					uint32 dstOffset = VDReadUnalignedLEU32(&cmdbuf[1]);
					auto& srcSeg = *mSegments[cmdbuf[5]];
					uint32 srcOffset = VDReadUnalignedLEU32(&cmdbuf[6]);
					uint32 len = VDReadUnalignedLEU32(&cmdbuf[10]);

					if (dstOffset <= dstSeg.mSize && dstSeg.mSize - dstOffset >= len
						&& srcOffset <= srcSeg.mSize && srcSeg.mSize - srcOffset >= len)
					{
						memmove(dstSeg.mpData + dstOffset, srcSeg.mpData + srcOffset, len);
					} else {
						PostNetError("CopySegmentMemory: Invalid segment ranges");
					}
				} else
					PostNetError("CopySegmentMemory: Invalid segment index");
				break;

			case (uint8)NetworkReply::ScriptInterrupt:
				mpNetworkEngine->Recv(cmdbuf, 8);

				if (mpScriptEventNetworkInterrupt && mVMDomain.mpActiveThread != &mVMThreadScriptInterrupt) {
					mVMThreadScriptInterrupt.mThreadVariables[(int)ThreadVarIndex::Aux1] = VDReadUnalignedLEU32(&cmdbuf[0]);
					mVMThreadScriptInterrupt.mThreadVariables[(int)ThreadVarIndex::Aux2] = VDReadUnalignedLEU32(&cmdbuf[4]);
					mVMThreadScriptInterrupt.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
					mVMThreadScriptInterrupt.RunVoid(*mpScriptEventNetworkInterrupt);
				}
				break;

			default:
				PostNetError("Invalid command received");
				break;
		}
	}

	return returnValue;
}

void ATDeviceCustom::PostNetError(const char *msg) {
	if (mpNetworkEngine) {
		uint32 len = (uint32)strlen(msg);

		// send the command frame directly as to avoid a loop
		uint8 cmdbuf[17];
		cmdbuf[0] = (uint8)NetworkCommand::Error;
		VDWriteUnalignedLEU32(&cmdbuf[1], 0);
		VDWriteUnalignedLEU32(&cmdbuf[5], len);
		VDWriteUnalignedLEU64(&cmdbuf[9], mpScheduler->GetTick64());
		mpNetworkEngine->Send(cmdbuf, 17);

		mpNetworkEngine->Send(msg, len);
	}

	if (mpIndicators) {
		VDStringW s;
		s.sprintf(L"Communication error with custom device server: %hs", msg);
		mpIndicators->ReportError(s.c_str());
	}
}

void ATDeviceCustom::OnNetRecvOOB() {
	ExecuteNetRequests(false);
}

void ATDeviceCustom::ResetCustomDevice() {
	for(ATCDVMThread *thread : mVMDomain.mThreads)
		thread->Reset();

	mClock.Reset();

	mVMThreadRunQueue.Reset();
	mVMThreadRawRecvQueue.Reset();
	mVMThreadSIOCommandAssertQueue.Reset();
	mVMThreadSIOCommandOffQueue.Reset();
	mVMThreadSIOMotorChangedQueue.Reset();
	mSleepHeap.clear();
	mRawSendQueue.clear();

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpEventThreadSleep);
		mpScheduler->UnsetEvent(mpEventRawSend);
	}

	mSIO.Reset();
}

void ATDeviceCustom::ShutdownCustomDevice() {
	auto& sem = *g_sim.GetEventManager();

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpEventThreadSleep);
		mpScheduler->UnsetEvent(mpEventRawSend);
	}

	if (mEventBindingVBLANK) {
		sem.RemoveEventCallback(mEventBindingVBLANK);
		mEventBindingVBLANK = 0;
	}

	mTrackedFileWatcher.Shutdown();

	mbInitedCustomDevice = false;
	mpActiveCommand = nullptr;

	mDeviceName.clear();

	if (mpNetworkEngine) {
		mpNetworkEngine->Shutdown();
		mpNetworkEngine = nullptr;
	}

	if (mAsyncNetCallback) {
		mpAsyncDispatcher->Cancel(&mAsyncNetCallback);
		mAsyncNetCallback = 0;
	}

	if (mbInitedCart) {
		mbInitedCart = false;

		mpCartPort->RemoveCartridge(mCartId, this);
	}

	if (mbInitedSIO) {
		mbInitedSIO = false;

		if (mbInitedRawSIO) {
			mbInitedRawSIO = false;

			mpSIOMgr->RemoveRawDevice(this);
		}

		mpSIOMgr->RemoveDevice(this);
	}

	if (mbInitedPBI) {
		mbInitedPBI = false;

		mpPBIMgr->RemoveDevice(this);
	}

	for(MemoryLayer *ml : mMemoryLayers) {
		if (ml->mpPhysLayer)
			mpMemMan->DeleteLayerPtr(&ml->mpPhysLayer);
	}

	for(auto *& cport : mpControllerPorts) {
		if (cport) {
			cport->Shutdown();
			cport->~ControllerPort();
			cport = nullptr;
		}
	}

	mMemoryLayers = {};
	mSegments = {};
	mpSIODeviceTable = nullptr;

	mpScriptEventColdReset = nullptr;
	mpScriptEventWarmReset = nullptr;
	mpScriptEventInit = nullptr;
	mpScriptEventVBLANK = nullptr;
	mpScriptEventSIOCommandChanged = nullptr;
	mpScriptEventSIOMotorChanged = nullptr;
	mpScriptEventSIOReceivedByte = nullptr;
	mpScriptEventPBISelect = nullptr;
	mpScriptEventPBIDeselect = nullptr;
	mpScriptEventNetworkInterrupt = nullptr;

	mPBIDeviceId = 0;

	mSleepHeap.clear();

	mConfigAllocator.Clear();
	mVMDomain.Clear();

	while(!mScriptThreads.empty()) {
		delete mScriptThreads.back();
		mScriptThreads.pop_back();
	}
}

void ATDeviceCustom::ReinitSegmentData(bool clearNonVolatile) {
	for(const Segment *seg : mSegments) {
		if (!clearNonVolatile && seg->mbNonVolatile)
			continue;

		uint8 *dst = seg->mpData;
		const uint32 len = seg->mSize;

		if (!dst)
			continue;
		
		const uint8 *pat = seg->mpInitData;
		const uint32 patlen = seg->mInitSize;

		if (seg->mInitSize == 1)
			if (pat)
				memset(dst, pat[0], len);
			else
				memset(dst, 0, len);
		else if (seg->mInitSize == 2) {
			VDMemset16(dst, VDReadUnalignedU16(pat), len >> 1);
			memcpy(dst + (len & ~1), pat, len & 1);
		} else if (seg->mInitSize == 4) {
			VDMemset32(dst, VDReadUnalignedU32(pat), len >> 1);
			memcpy(dst + (len & ~3), pat, len & 3);
		} else if (pat) {
			// we are guaranteed that the segment is at least as big as the pattern,
			// as this is enforced by the parser
			memcpy(dst, pat, patlen);

			const uint8 *src = dst;
			dst += patlen;

			// do a deliberately self-referencing ascending copy
			for(uint32 i = patlen; i < len; ++i)
				*dst++ = *src++;
		}
	}
}

void ATDeviceCustom::SendNextRawByte() {
	VDASSERT(!mpEventRawSend);

	if (mRawSendQueue.empty())
		return;

	RawSendInfo& rsi = mRawSendQueue.front();
	mSIO.mSendChecksum += rsi.mByte;

	mpSIOMgr->SendRawByte(rsi.mByte, rsi.mCyclesPerBit);
	mpScheduler->SetEvent(rsi.mCyclesPerBit * 10, this, kEventId_RawSend, mpEventRawSend);
}

void ATDeviceCustom::AbortRawSend(const ATCDVMThread& thread) {
	if (mRawSendQueue.empty())
		return;

	// we need a special case for the head thread; its send is in progress so we can't
	// remove the entry, we have to null it instead
	if (mRawSendQueue.front().mThreadIndex == thread.mThreadIndex) {
		mRawSendQueue.front().mThreadIndex = -1;
		return;
	}

	auto it = std::remove_if(mRawSendQueue.begin(), mRawSendQueue.end(),
		[threadIndex = thread.mThreadIndex](const RawSendInfo& rsi) {
			return rsi.mThreadIndex == threadIndex;
		}
	);

	if (it != mRawSendQueue.end())
		mRawSendQueue.pop_back();
}

void ATDeviceCustom::AbortThreadSleep(const ATCDVMThread& thread) {
	auto it = std::find_if(mSleepHeap.begin(), mSleepHeap.end(),
		[threadIndex = thread.mThreadIndex](const SleepInfo& si) {
			return si.mThreadIndex == threadIndex;
		}
	);
	
	if (it == mSleepHeap.end()) {
		VDFAIL("Trying to abort sleep for thread not in sleep heap.");
		return;
	}

	*it = mSleepHeap.back();
	mSleepHeap.pop_back();
	std::sort_heap(mSleepHeap.begin(), mSleepHeap.end(), SleepInfoPred());

	UpdateThreadSleep();
}

void ATDeviceCustom::UpdateThreadSleep() {
	if (mSleepHeap.empty()) {
		mpScheduler->UnsetEvent(mpEventThreadSleep);
	} else {
		uint64 delay = mSleepHeap.front().mWakeTime - mpScheduler->GetTick64();

		if (!mpEventThreadSleep || mpScheduler->GetTicksToEvent(mpEventThreadSleep) > delay)
			mpScheduler->SetEvent((uint32)std::min<uint64>(delay, 1000000), this, kEventId_Sleep, mpEventThreadSleep);
	}
}

void ATDeviceCustom::RunReadyThreads() {
	while(auto *thread = mVMThreadRunQueue.GetNext()) {
		thread->mThreadVariables[(int)ThreadVarIndex::Timestamp] = (sint32)ATSCHEDULER_GETTIME(mpScheduler);

		mVMThreadRunQueue.ResumeVoid();
	}
}

void ATDeviceCustom::ReloadConfig() {
	ShutdownCustomDevice();
	ClearFileTracking();

	try {
		{
			vdrefptr<ATVFSFileView> view;
			OpenViewWithTracking(mConfigPath.c_str(), ~view);

			mResourceBasePath.assign(mConfigPath.c_str(), ATVFSSplitPathFile(mConfigPath.c_str()));

			IVDRandomAccessStream& stream = view->GetStream();
			sint64 len = stream.Length();

			if ((uint64)len >= kMaxRawConfigSize)
				throw MyError("Custom device description is too large: %llu bytes.", (unsigned long long)len);

			vdblock<char> buf;
			buf.resize((size_t)len);
			stream.Read(buf.data(), len);

			ProcessDesc(buf.data(), buf.size());
		}

		bool needCart = false;

		for(MemoryLayer *ml : mMemoryLayers) {
			if (ml->mbAutoRD4 || ml->mbAutoRD5 || ml->mbAutoCCTL) {
				needCart = true;
				break;
			}
		}

		if (needCart) {
			mpCartPort->AddCartridge(this, kATCartridgePriority_Default, mCartId);
			mpCartPort->OnLeftWindowChanged(mCartId, IsLeftCartActive());

			mbInitedCart = true;
		}

		if (mpSIODeviceTable) {
			mpSIOMgr->AddDevice(this);
			mbInitedSIO = true;
		}

		if (mPBIDeviceId) {
			mpPBIMgr->AddDevice(this);
			mbInitedPBI = true;
		}

		ReinitSegmentData(true);

		if (mNetworkPort) {
			mpNetworkEngine = ATCreateDeviceCustomNetworkEngine(mNetworkPort);
			if (!mpNetworkEngine)
				throw MyError("Unable to set up local networking for custom device.");

			mpNetworkEngine->SetRecvHandler(
				[this] {
					mpAsyncDispatcher->Queue(&mAsyncNetCallback,
						[this] {
							VDASSERT(mpNetworkEngine && mbInitedCustomDevice);

							OnNetRecvOOB();
						}
					);
				}
			);
		}

		if (mpScriptEventVBLANK) {
			mEventBindingVBLANK = g_sim.GetEventManager()->AddEventCallback(kATSimEvent_VBLANK,
				[this] {
					mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
					mVMThread.RunVoid(*mpScriptEventVBLANK);
					RunReadyThreads();
				}
			);
		}

		mbInitedCustomDevice = true;
		mLastError.clear();

		ResetCustomDevice();

		if (mpScriptEventInit) {
			mVMThread.mThreadVariables[(int)ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpScheduler);
			mVMThread.RunVoid(*mpScriptEventInit);
			RunReadyThreads();
		}

		// make sure that we have a notification request posted to the network code
		// in case the remote server issues a request before we do
		if (mpNetworkEngine)
			ExecuteNetRequests(false);
	} catch(const MyError& e) {
		ShutdownCustomDevice();

		mLastError = VDTextAToW(e.gets());

		if (mpIndicators)
			mpIndicators->ReportError(mLastError.c_str());
	}

	// We need to set up tracking even if the load failed, so we can try again when the file is fixed
	if (!mTrackedFiles.empty() && mbHotReload) {
		try {
			mTrackedFileWatcher.InitDir(VDFileSplitPathLeft(mTrackedFiles.begin()->first).c_str(), false, this);
		} catch(const MyError&) {
		}
	}
}

class ATDeviceCustom::MemberParser {
public:
	MemberParser(const ATCDVMDataValue& val);

	const ATCDVMDataValue& Required(const char *name);
	const ATCDVMDataValue *Optional(const char *name);

	uint32 RequiredUint32(const char *name);
	const char *RequiredString(const char *name);

	void AssertNoUnused();

	const ATCDVMDataValue& mRootObject;
	vdfastvector<const ATCDVMDataMember *> mMembers;
};

ATDeviceCustom::MemberParser::MemberParser(const ATCDVMDataValue& val)
	: mRootObject(val)
{
	if (!val.IsDataObject())
		throw ATCDCompileError(mRootObject, "Expected data object");

	size_t n = val.mLength;
	mMembers.resize(n);

	for(size_t i = 0; i < n; ++i)
		mMembers[i] = &val.mpObjectMembers[i];
}

const ATCDVMDataValue& ATDeviceCustom::MemberParser::Required(const char *name) {
	const ATCDVMDataValue *val = Optional(name);

	if (!val)
		throw ATCDCompileError::Format(mRootObject, "Required member '%s' not found", name);

	return *val;
}

const ATCDVMDataValue *ATDeviceCustom::MemberParser::Optional(const char *name) {
	auto it = std::find_if(mMembers.begin(), mMembers.end(),
		[name, hash = VDHashString32(name)](const ATCDVMDataMember *member) {
			return member->mNameHash == hash && !strcmp(member->mpName, name);
		}
	);

	if (it == mMembers.end())
		return nullptr;

	auto val = *it;

	mMembers.erase(it);

	return &val->mValue;
}

uint32 ATDeviceCustom::MemberParser::RequiredUint32(const char *name) {
	auto& val = Required(name);

	if (!val.IsInteger())
		throw ATCDCompileError(val, "Integer expected");

	return val.mIntValue;
}

const char *ATDeviceCustom::MemberParser::RequiredString(const char *name) {
	auto& val = Required(name);

	if (!val.IsString())
		throw ATCDCompileError(val, "String expected");

	return val.mpStrValue;
}

void ATDeviceCustom::MemberParser::AssertNoUnused() {
	if (!mMembers.empty()) 
		throw ATCDCompileError::Format(mRootObject, "Unexpected member '%s'", (*mMembers.begin())->mpName);
}

void ATDeviceCustom::ProcessDesc(const void *buf, size_t len) {
	ATCustomDeviceCompiler compiler(mVMDomain);
	compiler.DefineSpecialVariable("address");
	compiler.DefineSpecialVariable("value");
	compiler.DefineThreadVariable("timestamp");
	compiler.DefineThreadVariable("device");
	compiler.DefineThreadVariable("command");
	compiler.DefineThreadVariable("aux1");
	compiler.DefineThreadVariable("aux2");
	compiler.DefineThreadVariable("aux");
	compiler.DefineThreadVariable("thread");
	compiler.DefineSpecialObjectVariable("network", &mNetwork);
	compiler.DefineSpecialObjectVariable("sio_frame", &mSIOFrameSegment);
	compiler.DefineSpecialObjectVariable("sio", &mSIO);
	compiler.DefineSpecialObjectVariable("clock", &mClock);
	compiler.DefineSpecialObjectVariable("debug", &mDebug);

	ATCustomDeviceCompiler::DefineInstanceFn createThreadFn(
		[this](ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers) -> bool {
			if (initializers)
				return compiler.ReportError("Thread object does not take initializers");

			vdautoptr<ScriptThread> thread { new ScriptThread };

			thread->mpParent = this;
			thread->mVMThread.Init(mVMDomain);

			bool success = compiler.DefineObjectVariable(name, thread.get());

			mScriptThreads.push_back(thread);
			thread.release();

			return success;
		}
	);

	compiler.DefineClass(ScriptThread::kVMObjectClass, &createThreadFn);

	ATCustomDeviceCompiler::DefineInstanceFn defineSegmentFn(
		[this](ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers) -> bool {
			return OnDefineSegment(compiler, name, initializers);
		}
	);

	compiler.DefineClass(Segment::kVMObjectClass, &defineSegmentFn);

	ATCustomDeviceCompiler::DefineInstanceFn defineMemoryLayerFn(
		[this](ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers) -> bool {
			return OnDefineMemoryLayer(compiler, name, initializers);
		}
	);

	compiler.DefineClass(MemoryLayer::kVMObjectClass, &defineMemoryLayerFn);

	ATCustomDeviceCompiler::DefineInstanceFn defineSIODeviceFn(
		[this](ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers) -> bool {
			return OnDefineSIODevice(compiler, name, initializers);
		}
	);

	compiler.DefineClass(SIODevice::kVMObjectClass, &defineSIODeviceFn);

	ATCustomDeviceCompiler::DefineInstanceFn defineControllerPortFn(
		[this](ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers) -> bool {
			return OnDefineControllerPort(compiler, name, initializers);
		}
	);

	compiler.DefineClass(ControllerPort::kVMObjectClass, &defineControllerPortFn);

	ATCustomDeviceCompiler::DefineInstanceFn definePBIDeviceFn(
		[this](ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers) -> bool {
			return OnDefinePBIDevice(compiler, name, initializers);
		}
	);

	compiler.DefineClass(PBIDevice::kVMObjectClass, &definePBIDeviceFn);

	compiler.SetBindEventHandler(
		[this](ATCustomDeviceCompiler& compiler, const char *eventName, const ATCDVMScriptFragment& scriptFragment) -> bool {
			const ATCDVMFunction *func = compiler.DeferCompile(kATCDVMTypeVoid, scriptFragment);
			if (!func)
				return false;

			static constexpr struct {
				const ATCDVMFunction *ATDeviceCustom::*const mpEvent;
				const char *mpEventName;
			} kEvents[] = {
				{ &ATDeviceCustom::mpScriptEventInit				, "init"					},
				{ &ATDeviceCustom::mpScriptEventColdReset			, "cold_reset"				},
				{ &ATDeviceCustom::mpScriptEventWarmReset			, "warm_reset"				},
				{ &ATDeviceCustom::mpScriptEventVBLANK				, "vblank"					},
				{ &ATDeviceCustom::mpScriptEventSIOCommandChanged	, "sio_command_changed"		},
				{ &ATDeviceCustom::mpScriptEventSIOMotorChanged		, "sio_motor_changed"		},
				{ &ATDeviceCustom::mpScriptEventSIOReceivedByte		, "sio_received_byte"		},
				{ &ATDeviceCustom::mpScriptEventPBISelect			, "pbi_select"				},
				{ &ATDeviceCustom::mpScriptEventPBIDeselect			, "pbi_deselect"			},
				{ &ATDeviceCustom::mpScriptEventNetworkInterrupt	, "network_interrupt"		},
			};

			for(const auto& eventDef : kEvents) {
				if (!strcmp(eventName, eventDef.mpEventName)) {
					if (this->*eventDef.mpEvent)
						return compiler.ReportErrorF("Event '%s' already bound", eventName);

					this->*eventDef.mpEvent = func;
					return true;
				}
			}

			return compiler.ReportErrorF("Unknown event '%s'", eventName);
		}
	);

	compiler.SetOptionHandler([this](ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue& value) {
		return OnSetOption(compiler, name, value);
	});

	mpCompiler = &compiler;
	mNetworkPort = 0;

	if (!compiler.CompileFile((const char *)buf, len) || !compiler.CompileDeferred()) {
		VDStringA s;

		auto [line, column] = mpCompiler->GetErrorLinePos();
		s.sprintf("%ls(%u,%u): %s", mConfigPath.c_str(), line, column, mpCompiler->GetError());
		throw MyError("%s", s.c_str());
	}

	mVMThread.Init(mVMDomain);
	mVMThreadSIO.Init(mVMDomain);
	mVMThreadScriptInterrupt.Init(mVMDomain);

	if (!mpSIODeviceTable && compiler.IsSpecialVariableReferenced("sio"))
		mpSIODeviceTable = mConfigAllocator.Allocate<SIODeviceTable>();
}

bool ATDeviceCustom::OnSetOption(ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue& value) {
	if (!strcmp(name, "name")) {
		if (!value.IsString())
			throw ATCDCompileError(value, "Option 'name' must be a string");

		mDeviceName = VDTextU8ToW(VDStringSpanA(value.AsString()));
	} else if (!strcmp(name, "network")) {
		if (!value.IsDataObject())
			throw ATCDCompileError(value, "Option 'network' must be a data object");

		MemberParser networkMembers(value);
		const ATCDVMDataValue& portNode = networkMembers.Required("port");
		uint32 port = ParseRequiredUint32(portNode);

		if (port < 1024 || port >= 49151)
			throw ATCDCompileError(portNode, "Invalid network port (not in 1024-49150).");

		mNetworkPort = port;

		networkMembers.AssertNoUnused();
	} else {
		throw ATCDCompileError::Format(value, "Unknown option '%s'", name);
	}

	return true;
}

bool ATDeviceCustom::OnDefineSegment(ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers) {
	if (!initializers)
		return compiler.ReportError("Segment object must be initialized");

	if (!initializers->IsDataObject())
		return compiler.ReportError("Segment initializer is not an object.");

	mSegments.push_back(nullptr);
	Segment& seg = *mConfigAllocator.Allocate<Segment>();
	mSegments.back() = &seg;

	MemberParser segMembers(*initializers);

	uint32 size = segMembers.RequiredUint32("size");
	if (size > kMaxTotalSegmentData)
		return compiler.ReportError("Total segment size is too large.");

	seg.mpData = (uint8 *)mConfigAllocator.Allocate(size, 4);
	if (!seg.mpData)
		throw MyMemoryError();

	seg.mSize = size;
	seg.mbNonVolatile = false;
	seg.mbMappable = true;

	if (const ATCDVMDataValue *sourceNode = segMembers.Optional("source")) {
		vdrefptr<ATVFSFileView> sourceView;		
		LoadDependency(*sourceNode, ~sourceView);

		const ATCDVMDataValue *offsetNode = segMembers.Optional("source_offset");
		const uint32 offset = offsetNode ? ParseRequiredUint32(*offsetNode) : 0;
		
		IVDRandomAccessStream& stream = sourceView->GetStream();
		seg.mpInitData = (uint8 *)mConfigAllocator.Allocate(size, 4);
		seg.mInitSize = size;

		if (offset)
			stream.Seek(offset);

		stream.Read(seg.mpInitData, seg.mInitSize);
	} else if (const ATCDVMDataValue *initPatternNode = segMembers.Optional("init_pattern")) {
		auto pattern = ParseBlob(*initPatternNode);

		if (pattern.size() > seg.mSize)
			return compiler.ReportError("Init pattern is larger than the segment size.");

		seg.mpInitData = pattern.data();
		seg.mInitSize = (uint32)pattern.size();
	} else {
		seg.mInitSize = 1;
	}

	if (const ATCDVMDataValue *persistenceNode = segMembers.Optional("persistence")) {
		const VDStringSpanA mode(ParseRequiredString(*persistenceNode));

		if (mode == "nonvolatile") {
			seg.mbNonVolatile = true;
		} else if (mode != "volatile")
			return compiler.ReportError("Unknown segment persistence mode.");
	}

	segMembers.AssertNoUnused();

	if (!mpCompiler->DefineObjectVariable(name, &seg, Segment::kVMObjectClass))
		return false;

	return true;
}

bool ATDeviceCustom::OnDefineMemoryLayer(ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers) {
	if (!initializers)
		throw ATCDCompileError("Memory layer must be initialized with a data object.");
		
	if (!initializers->IsDataObject())
		throw ATCDCompileError(*initializers, "Memory layer must be initialized with a data object.");

	MemberParser mlMembers(*initializers);

	const uint32 address = mlMembers.RequiredUint32("address");
	const uint32 size = mlMembers.RequiredUint32("size");

	if (address >= 0x10000 || (address & 0xFF))
		throw ATCDCompileError(*initializers, "Memory layer address is invalid.");

	if (size == 0 || 0x10000 - address < size || (size & 0xFF))
		throw ATCDCompileError(*initializers, "Memory layer size is invalid.");

	MemoryLayer& ml = *mConfigAllocator.Allocate<MemoryLayer>();
	if (!compiler.DefineObjectVariable(name, &ml))
		return false;

	mMemoryLayers.push_back(&ml);

	ml.mpParent = this;
	ml.mAddressBase = address;
	ml.mSize = size;

	if (const ATCDVMDataValue *pbiModeRef = mlMembers.Optional("auto_pbi")) {
		if (ParseBool(*pbiModeRef)) {
			ml.mbAutoPBI = true;

			if (!mPBIDeviceId)
				throw ATCDCompileError(*pbiModeRef, "pbi_device must be declared for auto_pbi layers");
		}
	}

	int pri = kATMemoryPri_Cartridge1;

	if (ml.mbAutoPBI)
		pri = kATMemoryPri_PBI;

	// see if we have a fixed mapping or control mapping
	const ATCDVMDataValue *fixedMappingNode = mlMembers.Optional("fixed_mapping");

	if (!fixedMappingNode)
		fixedMappingNode = mlMembers.Optional("segment");

	if (fixedMappingNode) {
		MemberParser segmentMembers(*fixedMappingNode);
		const ATCDVMDataValue& sourceNode = segmentMembers.Required("source");

		if (!sourceNode.IsRuntimeObject<Segment>())
			throw ATCDCompileError(sourceNode, "'source' must be a Segment object");

		Segment *srcSegment = sourceNode.AsRuntimeObject<Segment>(mVMDomain);

		// parse byte offset within segment
		const ATCDVMDataValue *offsetNode = segmentMembers.Optional("offset");
		uint32 offset = offsetNode ? ParseRequiredUint32(*offsetNode) : 0;

		// validate that it is page aligned and within the source segment
		if (offset & 0xFF)
			throw ATCDCompileError(*offsetNode, "Source segment offset is invalid.");

		if (offset >= srcSegment->mSize || srcSegment->mSize - offset < size)
			throw ATCDCompileError(*offsetNode, "Reference extends outside of source segment.");

		ml.mpSegment = srcSegment;
		ml.mMaxOffset = srcSegment->mSize - size;

		// parse mode
		const ATCDVMDataValue& modeNode = segmentMembers.Required("mode");
		const char *mode = ParseRequiredString(modeNode);
		ATMemoryAccessMode accessMode;
		bool isReadOnly = false;

		if (!strcmp(mode, "r")) {
			accessMode = kATMemoryAccessMode_AR;
		} else if (!strcmp(mode, "ro")) {
			accessMode = kATMemoryAccessMode_ARW;
			isReadOnly = true;
		} else if (!strcmp(mode, "w")) {
			accessMode = kATMemoryAccessMode_W;
		} else if (!strcmp(mode, "rw")) {
			accessMode = kATMemoryAccessMode_ARW;
		} else
			throw ATCDCompileError(*offsetNode, "Invalid access mode.");

		ml.mEnabledModes = accessMode;
		ml.mpPhysLayer = mpMemMan->CreateLayer(pri, srcSegment->mpData + offset, address >> 8, size >> 8, isReadOnly); 

		if (!ml.mbAutoPBI)
			mpMemMan->SetLayerModes(ml.mpPhysLayer, accessMode);

		segmentMembers.AssertNoUnused();
	} else if (const ATCDVMDataValue *controlNode = mlMembers.Optional("control")) {
		// parse address actions
		if (!controlNode->IsArray())
			throw ATCDCompileError(*controlNode, "Control item was not an object.");

		ml.mpReadBindings = mConfigAllocator.AllocateArray<AddressBinding>(size);
		ml.mpWriteBindings = mConfigAllocator.AllocateArray<AddressBinding>(size);

		uint32 memoryLayerMode = 0;

		for(const ATCDVMDataValue& bindingNode : vdvector_view(controlNode->mpArrayElements, controlNode->mLength)) {
			if (!bindingNode.IsDataObject())
				throw ATCDCompileError(bindingNode, "Binding item was not an object.");

			MemberParser bindingMembers(bindingNode);

			// parse address attribute and validate it
			const ATCDVMDataValue& bindingAddressNode = bindingMembers.Required("address");
			uint32 actionAddress = ParseRequiredUint32(bindingAddressNode);

			// parse optional size attribute
			const ATCDVMDataValue *sizeNode = bindingMembers.Optional("size");
			uint32 actionSize = sizeNode ? ParseRequiredUint32(*sizeNode) : 1;

			// validate that address action is within layer
			if (actionAddress < address || actionAddress >= address + size || (address + size) - actionAddress < actionSize)
				throw ATCDCompileError(bindingAddressNode, "Binding address is outside of the memory layer.");

			// parse mode
			const ATCDVMDataValue& bindingActionModeNode = bindingMembers.Required("mode");
			const char *actionMode = ParseRequiredString(bindingActionModeNode);
			bool actionRead = false;
			bool actionWrite = false;

			if (!strcmp(actionMode, "r"))
				actionRead = true;
			else if (!strcmp(actionMode, "w"))
				actionWrite = true;
			else if (!strcmp(actionMode, "rw"))
				actionRead = actionWrite = true;
			else
				throw ATCDCompileError(bindingActionModeNode, "Invalid binding mode.");

			// validate that actions are not already assigned
			vdvector_view<AddressBinding> actionReadBindings;
			vdvector_view<AddressBinding> actionWriteBindings;

			if (actionRead) {
				actionReadBindings = { &ml.mpReadBindings[actionAddress - address], actionSize };

				for(const auto& rb : actionReadBindings) {
					if (rb.mAction != AddressAction::None)
						throw ATCDCompileError(bindingNode, "Address conflict between two read bindings in the same layer.");
				}

				memoryLayerMode |= kATMemoryAccessMode_AR;
			}

			if (actionWrite) {
				actionWriteBindings = { &ml.mpWriteBindings[actionAddress - address], actionSize };

				for(const auto& wb : actionWriteBindings) {
					if (wb.mAction != AddressAction::None)
						throw ATCDCompileError(bindingNode, "Address conflict between two write bindings in the same layer.");
				}

				memoryLayerMode |= kATMemoryAccessMode_W;
			}

			if (const ATCDVMDataValue *dataNode = bindingMembers.Optional("data")) {
				const vdvector_view<uint8> pattern = ParseBlob(*dataNode);

				if (!actionRead || actionWrite)
					throw ATCDCompileError(*dataNode, "Data bindings can only be read-only.");

				if (pattern.size() == 1) {
					for(auto& rb : actionReadBindings) {
						rb.mAction = AddressAction::ConstantData;
						rb.mByteData = pattern[0];
					}
				} else {
					if (pattern.size() != actionSize)
						throw ATCDCompileError(*dataNode, "Data must either be a single byte or the same size as the memory layer.");

					for(uint32 i = 0; i < actionSize; ++i) {
						auto& arb = actionReadBindings[i];

						arb.mAction = AddressAction::ConstantData;
						arb.mByteData = pattern[i];
					}
				}
			} else if (const ATCDVMDataValue *actionNode = bindingMembers.Optional("action")) {
				const char *actionStr = actionNode->mpStrValue;

				if (!strcmp(actionStr, "block")) {
					if (actionRead || !actionWrite)
						throw ATCDCompileError(*actionNode, "Block bindings can only be write-only.");

					for(auto& wb : actionWriteBindings)
						wb.mAction = AddressAction::Block;
				} else if (!strcmp(actionStr, "network")) {
					if (mNetworkPort == 0)
						throw ATCDCompileError(*actionNode, "Cannot use a network binding with no network connection set up.");

					for(auto& rb : actionReadBindings)
						rb.mAction = AddressAction::Network;

					for(auto& wb : actionWriteBindings)
						wb.mAction = AddressAction::Network;
				} else
					throw ATCDCompileError(*actionNode, "Unknown action type.");
			} else if (const ATCDVMDataValue *variableNode = bindingMembers.Optional("variable")) {
				const char *varNameStr = ParseRequiredString(*variableNode);

				const ATCDVMTypeInfo *varTypeInfo = mpCompiler->GetVariable(varNameStr);

				if (!varTypeInfo)
					throw ATCDCompileError(*variableNode, "Variable not defined.");

				if (varTypeInfo->mClass != ATCDVMTypeClass::IntLValueVariable)
					throw ATCDCompileError(*variableNode, "Variable must be of integer type for address binding.");

				for(auto& rb : actionReadBindings) {
					rb.mAction = AddressAction::Variable;
					rb.mVariableIndex = varTypeInfo->mIndex;
				}

				for(auto& wb : actionWriteBindings) {
					wb.mAction = AddressAction::Variable;
					wb.mVariableIndex = varTypeInfo->mIndex;
				}
			} else if (const ATCDVMDataValue *scriptNode = bindingMembers.Optional("script")) {
				const ATCDVMFunction *func = ParseScript(actionRead ? kATCDVMTypeInt : kATCDVMTypeVoid, *scriptNode, ATCDConditionalMask::NonDebugOnly);

				const ATCDVMFunction *debugFunc = nullptr;
				if (const ATCDVMDataValue *debugScriptNode = bindingMembers.Optional("debug_script"))
					debugFunc = ParseScript(kATCDVMTypeInt, *debugScriptNode, ATCDConditionalMask::None);
				else
					debugFunc = ParseScript(actionRead ? kATCDVMTypeInt : kATCDVMTypeVoid, *scriptNode, ATCDConditionalMask::DebugOnly);

				uint16 funcCode = (uint16)mScriptFunctions.size();
				mScriptFunctions.push_back(func);

				uint16 debugFuncCode = funcCode;
				if (debugFunc) {
					debugFuncCode = (uint16)mScriptFunctions.size();
					mScriptFunctions.push_back(debugFunc);
				}


				for(auto& rb : actionReadBindings) {
					rb.mAction = AddressAction::Script;
					rb.mScriptFunctions[0] = funcCode;
					rb.mScriptFunctions[1] = debugFuncCode;
				}

				for(auto& wb : actionWriteBindings) {
					wb.mAction = AddressAction::Script;
					wb.mScriptFunctions[0] = funcCode;
				}
			} else if (const ATCDVMDataValue *copyNode = bindingMembers.Optional("copy_from")) {
				const uint32 srcAddr = ParseRequiredUint32(*copyNode);

				if (srcAddr < address || (srcAddr - address) >= size || size - (srcAddr - address) < actionSize)
					throw ATCDCompileError(bindingNode, "Binding copy source is outside of memory layer.");

				const auto *rsrc = &ml.mpReadBindings[srcAddr - address];
				for(auto& rb : actionReadBindings)
					rb = *rsrc++;

				const auto *wsrc = &ml.mpWriteBindings[srcAddr - address];
				for(auto& wb : actionWriteBindings)
					wb = *wsrc++;
			} else
				throw ATCDCompileError(bindingNode, "No binding type specified.");

			bindingMembers.AssertNoUnused();
		}

		if (!memoryLayerMode)
			throw ATCDCompileError(*controlNode, "No address bindings were specified.");

		// create physical memory layer
		ATMemoryHandlerTable handlers {};
		handlers.mbPassAnticReads = true;
		handlers.mbPassReads = true;
		handlers.mbPassWrites = true;
		handlers.mpThis = &ml;

		handlers.mpDebugReadHandler = [](void *thisptr, uint32 addr) -> sint32 {
			MemoryLayer& ml = *(MemoryLayer *)thisptr;

			return ml.mpParent->ReadControl<true>(ml, addr);
		};

		handlers.mpReadHandler = [](void *thisptr, uint32 addr) -> sint32 {
			MemoryLayer& ml = *(MemoryLayer *)thisptr;

			return ml.mpParent->ReadControl<false>(ml, addr);
		};

		handlers.mpWriteHandler = [](void *thisptr, uint32 addr, uint8 value) -> bool {
			MemoryLayer& ml = *(MemoryLayer *)thisptr;

			return ml.mpParent->WriteControl(ml, addr, value);
		};

		ml.mpPhysLayer = mpMemMan->CreateLayer(pri, handlers, address >> 8, size >> 8);

		ml.mEnabledModes = memoryLayerMode;
		mpMemMan->SetLayerModes(ml.mpPhysLayer, (ATMemoryAccessMode)memoryLayerMode);
	} else
		throw ATCDCompileError(*initializers, "Memory layer type not specified.");

	if (const ATCDVMDataValue *cartModeRef = mlMembers.Optional("cart_mode")) {
		const VDStringSpanA cartMode(ParseRequiredString(*cartModeRef));

		if (cartMode == "left") {
			ml.mbAutoRD5 = true;
			ml.mbRD5Active = true;
		} else if (cartMode == "right")
			ml.mbAutoRD4 = true;
		else if (cartMode == "cctl")
			ml.mbAutoCCTL = true;
		else if (cartMode == "auto") {
			if (address >= 0xA000 && address < 0xC000 && 0xC000 - address <= size) {
				ml.mbAutoRD5 = true;
				ml.mbRD5Active = true;
			} else if (address >= 0x8000 && address < 0xA000 && 0xA000 - address <= size) {
				ml.mbAutoRD4 = true;
			} else if (address >= 0xD500 && address < 0xD600 && 0xD600 - address <= size) {
				ml.mbAutoCCTL = true;
			} else
				throw ATCDCompileError(*cartModeRef, "Cannot use 'auto' mode as memory layer address range does not map to a cartridge region.");
		} else
			throw ATCDCompileError(*cartModeRef, "Invalid cartridge mode.");
	}

	VDStringA layerName(mlMembers.RequiredString("name"));

	// Currently the layer name is used by the debugger, which does not expect localized
	// names -- so filter out loc chars for now. We also need to copy the name as the memory
	// manager expects a static name.
	size_t nameLen = layerName.size();
	char *name8 = (char *)mConfigAllocator.Allocate(nameLen + 1);

	for(size_t i = 0; i < nameLen; ++i) {
		char c = layerName[i];

		if (c < 0x20 || c >= 0x7F)
			c = '_';

		name8[i] = c;
	}

	name8[nameLen] = 0;

	if (ml.mpPhysLayer)
		mpMemMan->SetLayerName(ml.mpPhysLayer, name8);

	mlMembers.AssertNoUnused();
	return true;
}

bool ATDeviceCustom::OnDefineSIODevice(ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers) {
	if (!mpSIODeviceTable)
		mpSIODeviceTable = mConfigAllocator.Allocate<SIODeviceTable>();

	if (!initializers)
		throw ATCDCompileError("SIODevice object must be initialized.");
		
	if (!initializers->IsDataObject())
		throw ATCDCompileError(*initializers, "SIODevice initializer must be an object.");

	MemberParser devMembers(*initializers);

	uint32 deviceId = devMembers.RequiredUint32("device_id");
	if (deviceId >= 0x100)
		throw ATCDCompileError(*initializers, "Invalid SIO device ID.");

	const ATCDVMDataValue *deviceCountNode = devMembers.Optional("device_count");
	uint32 deviceIdCount = deviceCountNode ? ParseRequiredUint32(*deviceCountNode) : 1;

	if (0x100 - deviceId < deviceIdCount)
		throw ATCDCompileError(*initializers, "Invalid SIO device ID range.");

	SIODevice *device = mConfigAllocator.Allocate<SIODevice>();

	if (!mpCompiler->DefineObjectVariable(name, device))
		return false;

	device->mbAllowAccel = true;

	for(uint32 i=0; i<deviceIdCount; ++i) {
		if (mpSIODeviceTable->mpDevices[deviceId + i])
			throw ATCDCompileError(*initializers, "SIO device already defined.");

		mpSIODeviceTable->mpDevices[deviceId + i] = device;
	}

	const auto *devAllowAccelNode = devMembers.Optional("allow_accel");
	if (devAllowAccelNode)
		device->mbAllowAccel = ParseBool(*devAllowAccelNode);

	const auto& commandsNode = devMembers.Required("commands");
	if (!commandsNode.IsArray())
		throw ATCDCompileError(*initializers, "SIO device requires commands array.");

	for(const ATCDVMDataValue& commandNode : commandsNode.AsArray()) {
		MemberParser commandMembers(commandNode);
		const auto& commandIdNode = commandMembers.Required("id");
		bool isCopy = false;
		uint32 commandId = 0;

		SIOCommand *cmd;
		if (const ATCDVMDataValue *copyFromNode = commandMembers.Optional("copy_from")) {
			uint32 srcId = ParseRequiredUint32(*copyFromNode);

			if (srcId >= 0x100)
				throw ATCDCompileError(*copyFromNode, "Invalid source command ID.");

			cmd = device->mpCommands[srcId];
			if (!cmd)
				throw ATCDCompileError(*copyFromNode, "Source command is not defined.");

			isCopy = true;
		} else {
			cmd = mConfigAllocator.Allocate<SIOCommand>();
			cmd->mbAllowAccel = true;
		}

		const ATCDVMDataValue *cmdAllowAccelNode = commandMembers.Optional("allow_accel");
		if (cmdAllowAccelNode)
			cmd->mbAllowAccel = ParseBool(*cmdAllowAccelNode);

		if (commandIdNode.IsString() && !strcmp(commandIdNode.AsString(), "default")) {
			for(SIOCommand*& cmdEntry : device->mpCommands) {
				if (!cmdEntry)
					cmdEntry = cmd;
			}
		} else {
			commandId = ParseRequiredUint32(commandIdNode);

			if (commandId >= 0x100)
				throw ATCDCompileError(commandIdNode, "Invalid SIO command ID.");

			if (device->mpCommands[commandId])
				throw ATCDCompileError(commandIdNode, "Conflicting command ID in SIO device command list.");

			device->mpCommands[commandId] = cmd;
		}

		if (isCopy)
			continue;

		if (const ATCDVMDataValue *autoTransferNode = commandMembers.Optional("auto_transfer")) {
			MemberParser autoTransferMembers(*autoTransferNode);

			const auto& segmentNode = autoTransferMembers.Required("segment");

			if (!segmentNode.IsRuntimeObject<Segment>())
				throw ATCDCompileError(segmentNode, "'segment' member must be set to an object of type Segment");

			Segment *segment = segmentNode.AsRuntimeObject<Segment>(mVMDomain);

			const VDStringSpanA mode(ParseRequiredString(autoTransferMembers.Required("mode")));
			if (mode == "read") {
				cmd->mbAutoTransferWrite = false;
			} else if (mode == "write") {
				cmd->mbAutoTransferWrite = true;
			} else
				throw ATCDCompileError(*autoTransferNode, "Unknown auto-transfer mode.");

			const ATCDVMDataValue *offsetNode = autoTransferMembers.Optional("offset");
			const uint32 offset = offsetNode ? ParseRequiredUint32(*offsetNode) : 0;
			const uint32 length = autoTransferMembers.RequiredUint32("length");

			if (length < 1 || length > 8192)
				throw ATCDCompileError(*autoTransferNode, "Invalid transfer length (must be 1-8192 bytes).");

			if (offset >= segment->mSize || segment->mSize - offset < length)
				throw ATCDCompileError(*autoTransferNode, "Offset/length specifies range outside of segment.");

			cmd->mpAutoTransferSegment = segment;
			cmd->mAutoTransferOffset = offset;
			cmd->mAutoTransferLength = length;
		} else {
			cmd->mpScript = ParseScriptOpt(kATCDVMTypeVoid, commandMembers.Optional("script"));
		}

		commandMembers.AssertNoUnused();
	}

	return true;
}

bool ATDeviceCustom::OnDefineControllerPort(ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers) {
	if (!initializers)
		throw ATCDCompileError("ControllerPort object must be initialized.");

	const uint32 portIndex = ParseRequiredUint32(*initializers);

	if (portIndex >= 4)
		throw ATCDCompileError(*initializers, "Invalid controller port index.");

	if (mpControllerPorts[portIndex])
		throw ATCDCompileError(*initializers, "Controller port is already bound.");

	ControllerPort *cport = mConfigAllocator.Allocate<ControllerPort>();
	mpControllerPorts[portIndex] = cport;

	if (!mpCompiler->DefineObjectVariable(name, cport))
		return false;

	cport->mpPortController = portIndex & 2 ? g_sim.GetPortControllerB() : g_sim.GetPortControllerA();
	cport->mbPort2 = (portIndex & 1) != 0;
	cport->Init();
	return true;
}

bool ATDeviceCustom::OnDefinePBIDevice(ATCustomDeviceCompiler& compiler, const char *name, const ATCDVMDataValue *initializers) {
	if (mPBIDeviceId)
		throw ATCDCompileError("PBI device already defined");

	if (!initializers)
		throw ATCDCompileError("PBIDevice object requires initialization");

	MemberParser members(*initializers);

	uint32 deviceId = members.RequiredUint32("device_id");

	if (!deviceId || deviceId >= 0x100 || (deviceId & (deviceId - 1)))
		throw ATCDCompileError(*initializers, "PBI device ID must be a power of two byte value.");

	mPBIDeviceId = (uint8)deviceId;

	PBIDevice *dev = mConfigAllocator.Allocate<PBIDevice>();
	if (!compiler.DefineObjectVariable(name, dev))
		return false;

	members.AssertNoUnused();
	return true;
}

const ATCDVMFunction *ATDeviceCustom::ParseScriptOpt(const ATCDVMTypeInfo& returnType, const ATCDVMDataValue *value) {
	if (!value)
		return nullptr;

	return ParseScript(returnType, *value, ATCDConditionalMask::None);
}

const ATCDVMFunction *ATDeviceCustom::ParseScript(const ATCDVMTypeInfo& returnType, const ATCDVMDataValue& value, ATCDConditionalMask conditionalMask) {
	if (!value.IsScript())
		throw ATCDCompileError(value, "Expected script function");

	const ATCDVMFunction *func = mpCompiler->DeferCompile(returnType, *value.mpScript, ATCDVMFunctionFlags::None, conditionalMask);

	if (!func)
		throw MyError("%s", mpCompiler->GetError());

	return func;
}

vdvector_view<uint8> ATDeviceCustom::ParseBlob(const ATCDVMDataValue& value) {
	uint32 size = 1;
	if (value.IsArray()) {
		size = value.mLength;

		if (!size)
			throw ATCDCompileError(value, "Array cannot be empty.");
	} else if (value.IsString()) {
		size = strlen(value.mpStrValue);
		if (!size)
			throw ATCDCompileError(value, "String cannot be empty.");
	}

	uint8 *data = (uint8 *)mConfigAllocator.Allocate(size, 4);

	if (value.IsArray()) {
		for(uint32 i = 0; i < size; ++i)
			data[i] = ParseRequiredUint8(value.mpArrayElements[i]);
	} else if (value.IsString()) {
		const char *s = value.mpStrValue;

		for(uint32 i = 0; i < size; ++i) {
			char c = s[i];

			if (c < 0x20 || c >= 0x7F)
				throw ATCDCompileError(value, "String data must be ASCII.");

			data[i] = (uint8)c;
		}
	} else
		data[0] = ParseRequiredUint8(value);

	return vdvector_view(data, size);
}

uint8 ATDeviceCustom::ParseRequiredUint8(const ATCDVMDataValue& value) {
	const uint32 v32 = ParseRequiredUint32(value);

	if (v32 != (uint8)v32)
		throw ATCDCompileError(value, "Value out of range.");

	return (uint8)v32;
}

uint32 ATDeviceCustom::ParseRequiredUint32(const ATCDVMDataValue& value) {
	if (!value.IsInteger() || value.mIntValue < 0)
		throw ATCDCompileError("Value out of range");

	return value.mIntValue;
}

const char *ATDeviceCustom::ParseRequiredString(const ATCDVMDataValue& value) {
	if (!value.IsString())
		throw ATCDCompileError(value, "Value out of range");

	return value.mpStrValue;
}

bool ATDeviceCustom::ParseBool(const ATCDVMDataValue& value) {
	if (!value.IsInteger())
		throw ATCDCompileError(value, "Expected boolean value");

	return value.mIntValue != 0;
}

void ATDeviceCustom::LoadDependency(const ATCDVMDataValue& value, ATVFSFileView **view) {
	const char *name = ParseRequiredString(value);

	const VDStringW nameW = VDTextU8ToW(VDStringSpanA(name));
	VDStringW path = nameW;
	VDStringW basePath;
	VDStringW subPath;
	for(;;) {
		auto pathType = ATParseVFSPath(path.c_str(), basePath, subPath);
		if (pathType == kATVFSProtocol_None)
			break;

		path = basePath;

		if (pathType == kATVFSProtocol_File)
			break;
	}

	if (wcschr(path.c_str(), '/') || wcschr(path.c_str(), '\\') || wcschr(path.c_str(), ':') || wcschr(path.c_str(), '%'))
		throw ATCDCompileError(value, "Source file must be a local filename with no directory component.");

	OpenViewWithTracking(ATMakeVFSPath(mResourceBasePath.c_str(), nameW.c_str()).c_str(), view);
}

void ATDeviceCustom::ClearFileTracking() {
	mTrackedFiles.clear();
}

void ATDeviceCustom::OpenViewWithTracking(const wchar_t *path, ATVFSFileView **view) {
	if (mbHotReload) {
		const wchar_t *path2 = path;

		VDStringW basePath, subPath, prevPath;
		VDFile f;

		for(;;) {
			auto protocol = ATParseVFSPath(path2, basePath, subPath);

			if (protocol == kATVFSProtocol_None)
				break;

			if (protocol == kATVFSProtocol_File) {
				FileTrackingInfo fti {};

				if (f.openNT(path2)) {
					fti.mSize = (uint64)f.size();
					fti.mTimestamp = f.getLastWriteTime().mTicks;
				}

				// store an entry even if the file open fails, as we still want to retry if the file
				// becomes readable
				mTrackedFiles.insert_as(path).first->second = fti;

				// leave the file open so the read lock persists, and maybe we can save some file open overhead
				break;
			}

			prevPath.swap(basePath);
			path2 = prevPath.c_str();
		}
	}

	ATVFSOpenFileView(path, false, view);
}

bool ATDeviceCustom::CheckForTrackedChanges() {
	if (!mbHotReload)
		return false;

	for(const auto& trackedFile : mTrackedFiles) {
		VDFile f;
		uint64 fsize = 0;
		uint64 timestamp = 0;

		if (f.openNT(trackedFile.first.c_str())) {
			fsize = (uint64)f.size();
			timestamp = f.getLastWriteTime().mTicks;
		}

		if (fsize != trackedFile.second.mSize || timestamp != trackedFile.second.mTimestamp)
			return true;
	}

	return false;
}

void ATDeviceCustom::UpdateLayerModes(MemoryLayer& ml) {
	uint8 modes = ml.mEnabledModes;

	if (ml.mbAutoPBI && !mbPBIDeviceSelected)
		modes = 0;

	mpMemMan->SetLayerModes(ml.mpPhysLayer, (ATMemoryAccessMode)modes);

	if (ml.mbAutoRD5) {
		bool rd5Active = (modes != 0);

		if (ml.mbRD5Active != rd5Active) {
			ml.mbRD5Active = rd5Active;
			mpCartPort->OnLeftWindowChanged(mCartId, IsLeftCartActive());
		}
	}
}

///////////////////////////////////////////////////////////////////////////

void ATDeviceCustom::Segment::VMCallClear(sint32 value) {
	if (mbReadOnly)
		return;

	memset(mpData, value, mSize);
}

void ATDeviceCustom::Segment::VMCallFill(sint32 offset, sint32 value, sint32 size) {
	if (offset < 0 || size <= 0 || mbReadOnly)
		return;

	if ((uint32)offset >= mSize || mSize - (uint32)offset < (uint32)size)
		return;

	memset(mpData + offset, value, size);
}

void ATDeviceCustom::Segment::VMCallCopy(sint32 destOffset, Segment *srcSegment, sint32 srcOffset, sint32 size) {
	if (destOffset < 0 || srcOffset < 0 || size <= 0 || mbReadOnly)
		return;

	uint32 udoffset = (uint32)destOffset;
	uint32 usoffset = (uint32)srcOffset;
	uint32 usize = (uint32)size;

	if (udoffset >= mSize || mSize - udoffset < usize)
		return;

	if (usoffset >= srcSegment->mSize || srcSegment->mSize - usoffset < usize)
		return;

	memmove(mpData + udoffset, srcSegment->mpData + usoffset, usize);
}

void ATDeviceCustom::Segment::VMCallWriteByte(sint32 offset, sint32 value) {
	if (offset >= 0 && (uint32)offset < mSize && !mbReadOnly)
		mpData[offset] = (uint8)value;
}

sint32 ATDeviceCustom::Segment::VMCallReadByte(sint32 offset) {
	if (offset >= 0 && (uint32)offset < mSize)
		return mpData[offset];
	else
		return 0;
}

///////////////////////////////////////////////////////////////////////////

void ATDeviceCustom::MemoryLayer::VMCallSetOffset(sint32 offset) {
	if (mpPhysLayer && mpSegment) {
		if (!(offset & 0xFF) && offset >= 0 && (uint32)offset <= mMaxOffset)
			mpParent->mpMemMan->SetLayerMemory(mpPhysLayer, mpSegment->mpData + offset);
	}
}

void ATDeviceCustom::MemoryLayer::VMCallSetSegmentAndOffset(Segment *seg, sint32 offset) {
	if (!seg)
		return;

	if (mpPhysLayer && !(offset & 0xFF) && offset >= 0 && (uint32)offset < seg->mSize && seg->mSize - (uint32)offset >= mSize) {
		mpSegment = seg;
		mMaxOffset = seg->mSize - mSize;

		mpParent->mpMemMan->SetLayerMemory(mpPhysLayer, seg->mpData + offset);
	}
}

void ATDeviceCustom::MemoryLayer::VMCallSetModes(sint32 read, sint32 write) {
	if (!mpPhysLayer)
		return;

	uint32 modes = 0;

	if (write)
		modes |= kATMemoryAccessMode_W;

	if (read)
		modes |= kATMemoryAccessMode_AR;

	mpParent->mpMemMan->SetLayerModes(mpPhysLayer, (ATMemoryAccessMode)modes);

	if (mbAutoRD5) {
		bool rd5Active = (modes != 0);

		if (mbRD5Active != rd5Active) {
			mbRD5Active = rd5Active;
			mpParent->mpCartPort->OnLeftWindowChanged(mpParent->mCartId, mpParent->IsLeftCartActive());
		}
	}
}

void ATDeviceCustom::MemoryLayer::VMCallSetReadOnly(sint32 ro) {
	if (mpPhysLayer)
		mpParent->mpMemMan->SetLayerReadOnly(mpPhysLayer, ro != 0);
}

///////////////////////////////////////////////////////////////////////////

sint32 ATDeviceCustom::Network::VMCallSendMessage(sint32 param1, sint32 param2) {
	return mpParent->SendNetCommand(param1, param2, NetworkCommand::ScriptEventSend);
}

sint32 ATDeviceCustom::Network::VMCallPostMessage(sint32 param1, sint32 param2) {
	return mpParent->PostNetCommand(param1, param2, NetworkCommand::ScriptEventPost);
}

///////////////////////////////////////////////////////////////////////////

void ATDeviceCustom::SIO::Reset() {
	mSendChecksum = 0;
	mRecvChecksum = 0;
	mRecvLast = 0;
}

void ATDeviceCustom::SIO::VMCallAck() {
	if (!mbValid)
		return;

	if (mbDataFrameReceived) {
		mbDataFrameReceived = false;

		// enforce data-to-ACK delay (850us minimum)
		mpParent->mpSIOMgr->Delay(1530);
	}

	mpParent->mpSIOMgr->SendACK();
}

void ATDeviceCustom::SIO::VMCallNak() {
	if (!mbValid)
		return;

	if (mbDataFrameReceived) {
		mbDataFrameReceived = false;

		// enforce data-to-NAK delay (850us minimum)
		mpParent->mpSIOMgr->Delay(1530);
	}

	mpParent->mpSIOMgr->SendNAK();
}

void ATDeviceCustom::SIO::VMCallError() {
	if (!mbValid)
		return;

	mpParent->mpSIOMgr->SendError();
}

void ATDeviceCustom::SIO::VMCallComplete() {
	if (!mbValid)
		return;

	mpParent->mpSIOMgr->SendComplete();
}

void ATDeviceCustom::SIO::VMCallSendFrame(Segment *seg, sint32 offset, sint32 length) {
	if (!mbValid)
		return;

	if (!seg || offset < 0 || length <= 0 || length > 8192)
		return;

	uint32 uoffset = (uint32)offset;
	uint32 ulength = (uint32)length;
	if (uoffset > seg->mSize || seg->mSize - uoffset < ulength)
		return;

	mpParent->mpSIOMgr->SendData(seg->mpData + offset, ulength, true);
	mpParent->mpSIOMgr->InsertFence((uint32)SerialFenceId::ScriptDelay);
	mpParent->mVMThreadSIO.Suspend();
}

void ATDeviceCustom::SIO::VMCallRecvFrame(sint32 length) {
	if (!mbValid)
		return;

	if (length <= 0 || length > 8192)
		return;

	mbDataFrameReceived = true;
	mpParent->mpSIOMgr->ReceiveData((uint32)SerialFenceId::ScriptReceive, (uint32)length, true);
	mpParent->mVMThreadSIO.Suspend();
}

void ATDeviceCustom::SIO::VMCallDelay(sint32 cycles) {
	if (!mbValid)
		return;

	if (cycles <= 0 || cycles > 64*1024*1024)
		return;

	mpParent->mpSIOMgr->Delay((uint32)cycles);
	mpParent->mpSIOMgr->InsertFence((uint32)SerialFenceId::ScriptDelay);
	mpParent->mVMThreadSIO.Suspend();
}

void ATDeviceCustom::SIO::VMCallEnableRaw(sint32 enable0) {
	if (!mpParent->mbInitedSIO)
		return;

	const bool enable = (enable0 != 0);

	if (mpParent->mbInitedRawSIO != enable) {
		mpParent->mbInitedRawSIO = enable;

		if (enable)
			mpParent->mpSIOMgr->AddRawDevice(mpParent);
		else
			mpParent->mpSIOMgr->RemoveRawDevice(mpParent);
	}
}

void ATDeviceCustom::SIO::VMCallSetProceed(sint32 asserted) {
	if (!mpParent->mbInitedRawSIO)
		return;

	mpParent->mpSIOMgr->SetSIOProceed(mpParent, asserted != 0);
}

void ATDeviceCustom::SIO::VMCallSetInterrupt(sint32 asserted) {
	if (!mpParent->mbInitedRawSIO)
		return;

	mpParent->mpSIOMgr->SetSIOInterrupt(mpParent, asserted != 0);
}

sint32 ATDeviceCustom::SIO::VMCallCommandAsserted() {
	if (!mpParent->mbInitedSIO)
		return false;

	return mpParent->mpSIOMgr->IsSIOCommandAsserted();
}

sint32 ATDeviceCustom::SIO::VMCallMotorAsserted() {
	if (!mpParent->mbInitedSIO)
		return false;

	return mpParent->mpSIOMgr->IsSIOMotorAsserted();
}

void ATDeviceCustom::SIO::VMCallSendRawByte(sint32 c, sint32 cyclesPerBit, ATCDVMDomain& domain) {
	if (!mpParent->mbInitedRawSIO)
		return;

	if (cyclesPerBit < 4 || cyclesPerBit > 100000)
		return;

	ATDeviceCustom& self = *static_cast<Domain&>(domain).mpParent;
	domain.mpActiveThread->Suspend(&self.mpRawSendAbortFn);

	const uint32 threadIndex = domain.mpActiveThread->mThreadIndex;

	mpParent->mRawSendQueue.push_back(RawSendInfo { (sint32)threadIndex, (uint32)cyclesPerBit, (uint8)c });

	if (!mpParent->mpEventRawSend)
		mpParent->SendNextRawByte();
}

sint32 ATDeviceCustom::SIO::VMCallRecvRawByte(ATCDVMDomain& domain) {
	if (!mpParent->mbInitedRawSIO)
		return 0;

	mpParent->mVMThreadRawRecvQueue.Suspend(*domain.mpActiveThread);
	return 0;
}

sint32 ATDeviceCustom::SIO::VMCallWaitCommand(ATCDVMDomain& domain) {
	if (!mpParent->mbInitedRawSIO)
		return 0;

	mpParent->mVMThreadSIOCommandAssertQueue.Suspend(*domain.mpActiveThread);

	return 0;
}

sint32 ATDeviceCustom::SIO::VMCallWaitCommandOff(ATCDVMDomain& domain) {
	if (!mpParent->mbInitedRawSIO)
		return 0;

	if (mpParent->mpSIOMgr->IsSIOCommandAsserted())
		mpParent->mVMThreadSIOCommandOffQueue.Suspend(*domain.mpActiveThread);

	return 0;
}

sint32 ATDeviceCustom::SIO::VMCallWaitMotorChanged(ATCDVMDomain& domain) {
	if (!mpParent->mbInitedRawSIO)
		return 0;

	mpParent->mVMThreadSIOMotorChangedQueue.Suspend(*domain.mpActiveThread);

	return 0;
}

void ATDeviceCustom::SIO::VMCallResetRecvChecksum() {
	mRecvChecksum = 0;
	mRecvLast = 0;
}

void ATDeviceCustom::SIO::VMCallResetSendChecksum() {
	mSendChecksum = 0;
}

sint32 ATDeviceCustom::SIO::VMCallGetRecvChecksum() {
	return mRecvChecksum ? (mRecvChecksum - 1) % 255 + 1 : 0;
}

sint32 ATDeviceCustom::SIO::VMCallCheckRecvChecksum() {
	uint8 computedChk = mRecvChecksum - mRecvLast ? (uint8)((mRecvChecksum - mRecvLast - 1) % 255 + 1) : (uint8)0;

	return mRecvLast == computedChk;
}

sint32 ATDeviceCustom::SIO::VMCallGetSendChecksum() {
	return mSendChecksum ? (mSendChecksum - 1) % 255 + 1 : 0;
}

///////////////////////////////////////////////////////////////////////////

void ATDeviceCustom::Clock::Reset() {
	mLocalTimeCaptureTimestamp = 0;
	mLocalTime = {};
}

void ATDeviceCustom::Clock::VMCallCaptureLocalTime() {
	uint64 t = mpParent->mpScheduler->GetTick64();

	if (mLocalTimeCaptureTimestamp != t) {
		mLocalTimeCaptureTimestamp = t;

		mLocalTime = VDGetLocalDate(VDGetCurrentDate());
	}
}

sint32 ATDeviceCustom::Clock::VMCallLocalYear() {
	return mLocalTime.mYear;
}

sint32 ATDeviceCustom::Clock::VMCallLocalMonth() {
	return mLocalTime.mMonth;
}

sint32 ATDeviceCustom::Clock::VMCallLocalDay() {
	return mLocalTime.mDay;
}

sint32 ATDeviceCustom::Clock::VMCallLocalDayOfWeek() {
	return mLocalTime.mDayOfWeek;
}

sint32 ATDeviceCustom::Clock::VMCallLocalHour() {
	return mLocalTime.mHour;
}

sint32 ATDeviceCustom::Clock::VMCallLocalMinute() {
	return mLocalTime.mMinute;
}

sint32 ATDeviceCustom::Clock::VMCallLocalSecond() {
	return mLocalTime.mSecond;
}

///////////////////////////////////////////////////////////////////////////

void ATDeviceCustom::ControllerPort::Init() {
	if (mPortInput < 0) {
		mPortInput = mpPortController->AllocatePortInput(mbPort2, -1);

		mInputMask = 0;

		mPotA = 229;
		mPotB = 229;
	}
}

void ATDeviceCustom::ControllerPort::Shutdown() {
	if (mPortInput >= 0) {
		ResetPotPositions();
		mbPaddleASet = false;
		mbPaddleBSet = false;

		mpPortController->FreePortInput(mPortInput);
		mPortInput = -1;
	}
}

void ATDeviceCustom::ControllerPort::Enable() {
	if (!mbEnabled) {
		mbEnabled = true;

		Reapply();
	}
}

void ATDeviceCustom::ControllerPort::Disable() {
	if (mbEnabled) {
		mbEnabled = false;

		Reapply();
	}
}

void ATDeviceCustom::ControllerPort::Reapply() {
	if (mPortInput < 0)
		return;

	if (mbEnabled) {
		mpPortController->SetPortInput(mPortInput, mInputMask);

		if (mbPaddleASet)
			mpPortController->SetPotPosition(mbPort2 ? 2 : 0, mPotA);

		if (mbPaddleBSet)
			mpPortController->SetPotPosition(mbPort2 ? 3 : 1, mPotB);
	} else {
		mpPortController->SetPortInput(mPortInput, 0);
		ResetPotPositions();
	}
}

void ATDeviceCustom::ControllerPort::ResetPotPositions() {
	if (mbPaddleASet)
		mpPortController->SetPotPosition(mbPort2 ? 2 : 0, 229);

	if (mbPaddleBSet)
		mpPortController->SetPotPosition(mbPort2 ? 3 : 1, 229);
}

void ATDeviceCustom::ControllerPort::VMCallSetPaddleA(sint32 pos) {
	mbPaddleASet = true;

	if (pos >= 228)
		pos = 228;
	else if (pos < 1)
		pos = 1;

	if (mPotA != pos) {
		mPotA = pos;

		mpPortController->SetPotPosition(mbPort2 ? 2 : 0, mPotA);
	}
}

void ATDeviceCustom::ControllerPort::VMCallSetPaddleB(sint32 pos) {
	mbPaddleBSet = true;

	if (pos >= 228)
		pos = 228;
	else if (pos < 1)
		pos = 1;

	if (mPotB != pos) {
		mPotB = pos;

		mpPortController->SetPotPosition(mbPort2 ? 3 : 1, mPotB);
	}
}

void ATDeviceCustom::ControllerPort::VMCallSetTrigger(sint32 asserted) {
	uint32 newMask = mInputMask;

	if (asserted)
		newMask |= 0x100;
	else
		newMask &= ~UINT32_C(0x100);

	if (mInputMask != newMask) {
		mInputMask = newMask;

		if (mbEnabled)
			mpPortController->SetPortInput(mPortInput, mInputMask);
	}
}

void ATDeviceCustom::ControllerPort::VMCallSetDirs(sint32 mask) {
	uint32 newMask = mInputMask ^ ((mask ^ mInputMask) & 15);

	if (mInputMask != newMask) {
		mInputMask = newMask;

		if (mbEnabled)
			mpPortController->SetPortInput(mPortInput, mInputMask);
	}
}

///////////////////////////////////////////////////////////////////////////

void ATDeviceCustom::Debug::VMCallLog(const char *str) {
	VDStringA s;
	s = str;
	s += '\n';
	g_ATLCCustomDev <<= s.c_str();
}

void ATDeviceCustom::Debug::VMCallLogInt(const char *str, sint32 v) {
	VDStringA s;
	s = str;
	s.append_sprintf("%d\n", v);
	g_ATLCCustomDev <<= s.c_str();
}

///////////////////////////////////////////////////////////////////////////

sint32 ATDeviceCustom::ScriptThread::VMCallIsRunning() {
	return mVMThread.mbSuspended || mpParent->mVMDomain.mpActiveThread == &mVMThread;
}

void ATDeviceCustom::ScriptThread::VMCallRun(const ATCDVMFunction *function) {
	if (mpParent->mVMDomain.mpActiveThread == &mVMThread)
		return;

	mVMThread.Abort();
	mVMThread.StartVoid(*function);

	mpParent->mVMThreadRunQueue.Suspend(mVMThread);
}

void ATDeviceCustom::ScriptThread::VMCallInterrupt() {
	if (mpParent->mVMDomain.mpActiveThread == &mVMThread)
		return;

	mVMThread.Abort();
}

void ATDeviceCustom::ScriptThread::VMCallSleep(sint32 cycles, ATCDVMDomain& domain) {
	if (cycles <= 0)
		return;

	ATDeviceCustom& self = *static_cast<Domain&>(domain).mpParent;
	domain.mpActiveThread->Suspend(&self.mpSleepAbortFn);
	self.mSleepHeap.push_back(SleepInfo { domain.mpActiveThread->mThreadIndex, self.mpScheduler->GetTick64() + (uint32)cycles });
	std::push_heap(self.mSleepHeap.begin(), self.mSleepHeap.end(), SleepInfoPred());

	self.UpdateThreadSleep();
}
