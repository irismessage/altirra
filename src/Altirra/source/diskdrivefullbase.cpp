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

#include "stdafx.h"
#include <vd2/system/int128.h>
#include <at/atcore/devicesio.h>
#include <at/atcore/wraptime.h>
#include <at/atcpu/history.h>
#include <at/atcpu/memorymap.h>
#include "diskdrivefullbase.h"
#include "firmwaremanager.h"

ATDiskDriveAudioPlayer::ATDiskDriveAudioPlayer() {
}

ATDiskDriveAudioPlayer::~ATDiskDriveAudioPlayer() {
	VDASSERT(!mpAudioMixer);
}

void ATDiskDriveAudioPlayer::Shutdown() {
	mpRotationSoundGroup = nullptr;
	mpStepSoundGroup = nullptr;
	mpAudioMixer = nullptr;
}

void ATDiskDriveAudioPlayer::SetRotationSoundEnabled(bool enabled) {
	if (!mpAudioMixer)
		return;

	if (enabled) {
		if (!mpRotationSoundGroup->IsAnySoundQueued())
			mpAudioMixer->GetSamplePlayer().AddLoopingSound(*mpRotationSoundGroup, 0, kATAudioSampleId_DiskRotation, 1.0f);
	} else {
		mpRotationSoundGroup->StopAllSounds();
	}
}

void ATDiskDriveAudioPlayer::PlayStepSound(ATAudioSampleId sampleId, float volume, uint32 cycleDelay) {
	if (!mpAudioMixer)
		return;

	mpAudioMixer->GetSamplePlayer().AddSound(*mpStepSoundGroup, cycleDelay, sampleId, volume);
}

void ATDiskDriveAudioPlayer::InitAudioOutput(IATAudioMixer *mixer) {
	mpAudioMixer = mixer;

	IATSyncAudioSamplePlayer& sp = mixer->GetSamplePlayer();
	mpRotationSoundGroup = sp.CreateGroup(ATAudioGroupDesc().Mix(kATAudioMix_Drive));
	mpStepSoundGroup = sp.CreateGroup(ATAudioGroupDesc().Mix(kATAudioMix_Drive).RemoveSupercededSounds());
}

///////////////////////////////////////////////////////////////////////////

ATDiskDriveChangeHandler::ATDiskDriveChangeHandler()
	: mpUpdateWriteProtect([](auto) {})
	, mpUpdateDiskReady([](auto) {})
{
}

ATDiskDriveChangeHandler::~ATDiskDriveChangeHandler() {
	Shutdown();
}

void ATDiskDriveChangeHandler::Init(ATScheduler *scheduler) {
	static constexpr float kDiskChangeStepMS = 50.0f;

	mpScheduler = scheduler;
	mCyclesPerStep = (uint32_t)(0.5f + scheduler->GetRate().asDouble() * (kDiskChangeStepMS / 1000.0f));
}

void ATDiskDriveChangeHandler::Shutdown() {
	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpEvent);
		mpScheduler = nullptr;
	}
}

void ATDiskDriveChangeHandler::SetOutputStateFns(vdfunction<void(std::optional<bool>)> wpOverrideChange, vdfunction<void(std::optional<bool>)> readyOverrideChange) {
	mpUpdateWriteProtect = std::move(wpOverrideChange);
	mpUpdateDiskReady = std::move(readyOverrideChange);
}

void ATDiskDriveChangeHandler::Reset() {
	if (mpScheduler)
		mpScheduler->UnsetEvent(mpEvent);

	mDiskChangeState = 0;
	ForceOutputUpdate();
}

void ATDiskDriveChangeHandler::ChangeDisk() {
	mDiskChangeState = 0;
	mpScheduler->SetEvent(1, this, 1, mpEvent);
}

void ATDiskDriveChangeHandler::ForceOutputUpdate() const {
	mpUpdateWriteProtect(GetWriteProtectState());
	mpUpdateDiskReady(GetDiskReadyState());
}

void ATDiskDriveChangeHandler::OnScheduledEvent(uint32 id) {
	mpEvent = nullptr;

	if (++mDiskChangeState >= 4)
		mDiskChangeState = 0;
	else
		mpScheduler->SetEvent(mCyclesPerStep, this, 1, mpEvent);

	mpUpdateWriteProtect(GetWriteProtectState());

	switch(mDiskChangeState) {
		case 1:
		case 0:
			mpUpdateDiskReady(GetDiskReadyState());
			break;

		case 2:
		case 3:
			break;
	}
}

///////////////////////////////////////////////////////////////////////////

ATDiskDriveSerialBitTransmitQueue::ATDiskDriveSerialBitTransmitQueue() {
}

ATDiskDriveSerialBitTransmitQueue::~ATDiskDriveSerialBitTransmitQueue() {
	VDASSERT(!mpSIOMgr);

	Shutdown();
}

void ATDiskDriveSerialBitTransmitQueue::Init(ATScheduler *masterScheduler, IATDeviceSIOManager *sioMgr) {
	mpSIOMgr = sioMgr;
	mpMasterScheduler = masterScheduler;
}

void ATDiskDriveSerialBitTransmitQueue::Shutdown() {
	Reset();
	mpMasterScheduler = nullptr;
	mpSIOMgr = nullptr;
}


void ATDiskDriveSerialBitTransmitQueue::SetMaxCyclesPerBit(uint32 cpb) {
	mMaxCyclesPerBit = cpb;
}

void ATDiskDriveSerialBitTransmitQueue::Reset() {
	// clear transmission
	mpMasterScheduler->UnsetEvent(mpEventTransmit);

	if (!mbTransmitCurrentBit) {
		mbTransmitCurrentBit = true;
		mpSIOMgr->SetRawInput(true);
	}

	mTransmitHead = 0;
	mTransmitTail = 0;
	mTransmitCyclesPerBit = 0;
	mTransmitPhase = 0;
	mTransmitShiftRegister = 0;
}

void ATDiskDriveSerialBitTransmitQueue::AddTransmitBit(uint32 masterTime, bool polarity) {
	static_assert(!(kTransmitQueueSize & (kTransmitQueueSize - 1)), "mTransmitQueue size not pow2");

	// mask since we only store 31 bits in the queue
	masterTime &= 0x7FFFFFFF;

	// check if previous transition is at same time
	const uint32 queueLen = (mTransmitTail - mTransmitHead) & kTransmitQueueMask;
	if (queueLen) {
		auto& prevEdge = mTransmitQueue[(mTransmitTail - 1) & kTransmitQueueMask];

		// check if this event is at or before the last event in the queue
		if (!((prevEdge.mTime - masterTime) & UINT32_C(0x40000000))) {
			// check if we've gone backwards in time and drop event if so
			if (prevEdge.mTime == masterTime) {
				// same time -- overwrite event
				prevEdge.mBit = polarity;
			} else {
				VDASSERT(!"Dropping new event earlier than tail in transmit queue.");
			}

			return;
		}
	}

	// check if we have room for a new event
	if (queueLen >= kTransmitQueueMask) {
		VDASSERT(!"Transmit queue full.");
		return;
	}

	// add the new event
	auto& newEdge = mTransmitQueue[mTransmitTail++ & kTransmitQueueMask];

	newEdge.mTime = masterTime;
	newEdge.mBit = polarity;

	// queue next event if needed
	QueueNextTransmitEvent();
}

void ATDiskDriveSerialBitTransmitQueue::OnScheduledEvent(uint32 id) {
	mpEventTransmit = nullptr;
	OnTransmitEvent();
}

void ATDiskDriveSerialBitTransmitQueue::OnTransmitEvent() {
	// drain transmit queue entries until we're up to date
	const uint32 t = ATSCHEDULER_GETTIME(mpMasterScheduler);

	while((mTransmitHead ^ mTransmitTail) & kTransmitQueueMask) {
		const auto& nextEdge = mTransmitQueue[mTransmitHead & kTransmitQueueMask];

		if ((t - nextEdge.mTime) & UINT32_C(0x40000000))
			break;

		bool bit = (nextEdge.mBit != 0);

		if (mbTransmitCurrentBit != bit) {
			mbTransmitCurrentBit = bit;
			mpSIOMgr->SetRawInput(bit);
		}

		++mTransmitHead;
	}

	const uint32 resetCounter = mpSIOMgr->GetRecvResetCounter();

	if (mTransmitResetCounter != resetCounter) {
		mTransmitResetCounter = resetCounter;

		mTransmitCyclesPerBit = 0;
	}

	// check if we're waiting for a start bit
	if (!mTransmitCyclesPerBit) {
		if (!mbTransmitCurrentBit) {
			// possible start bit -- reset transmission
			mTransmitCyclesPerBit = 0;
			mTransmitShiftRegister = 0;
			mTransmitPhase = 0;

			const uint32 cyclesPerBit = mpSIOMgr->GetCyclesPerBitRecv();

			// reject transmission speed below ~16000 baud (reduced for ATR8000) or above ~178Kbaud
			if (cyclesPerBit < 10 || cyclesPerBit > mMaxCyclesPerBit) {
				QueueNextTransmitEvent();
				return;
			}

			mTransmitCyclesPerBit = cyclesPerBit;

			// queue event to half bit in
			mpMasterScheduler->SetEvent(cyclesPerBit >> 1, this, kEventId_Transmit, mpEventTransmit);
		}
		return;
	}

	// check for a bogus start bit
	if (mTransmitPhase == 0 && mbTransmitCurrentBit) {
		mTransmitCyclesPerBit = 0;

		QueueNextTransmitEvent();
		return;
	}

	// send byte to POKEY if done
	if (++mTransmitPhase == 10) {
		mpSIOMgr->SendRawByte(mTransmitShiftRegister, mTransmitCyclesPerBit, false, !mbTransmitCurrentBit, false);
		mTransmitCyclesPerBit = 0;
		QueueNextTransmitEvent();
		return;
	}

	// shift new bit into shift register
	mTransmitShiftRegister = (mTransmitShiftRegister >> 1) + (mbTransmitCurrentBit ? 0x80 : 0);

	// queue another event one bit later
	mpMasterScheduler->SetEvent(mTransmitCyclesPerBit, this, kEventId_Transmit, mpEventTransmit);
}

void ATDiskDriveSerialBitTransmitQueue::QueueNextTransmitEvent() {
	// exit if we already have an event queued
	if (mpEventTransmit)
		return;

	// exit if transmit queue is empty
	if (!((mTransmitHead ^ mTransmitTail) & kTransmitQueueMask))
		return;

	const auto& nextEvent = mTransmitQueue[mTransmitHead & kTransmitQueueMask];
	uint32 delta = (nextEvent.mTime - ATSCHEDULER_GETTIME(mpMasterScheduler)) & UINT32_C(0x7FFFFFFF);

	mpEventTransmit = mpMasterScheduler->AddEvent((uint32)(delta - 1) & UINT32_C(0x40000000) ? 1 : delta, this, kEventId_Transmit);
}

//////////////////////////////////////////////////////////////////////////////

ATDiskDriveSerialByteTransmitQueue::ATDiskDriveSerialByteTransmitQueue() {
}

ATDiskDriveSerialByteTransmitQueue::~ATDiskDriveSerialByteTransmitQueue() {
	VDASSERT(!mpSIOMgr);
	Shutdown();
}

void ATDiskDriveSerialByteTransmitQueue::Init(ATScheduler *masterScheduler, IATDeviceSIOManager *sioMgr) {
	mpSIOMgr = sioMgr;
	mpMasterScheduler = masterScheduler;
}

void ATDiskDriveSerialByteTransmitQueue::Shutdown() {
	Reset();
	mpMasterScheduler = nullptr;
	mpSIOMgr = nullptr;
}


void ATDiskDriveSerialByteTransmitQueue::Reset() {
	mTransmitHead = 0;
	mTransmitTail = 0;

	if (mpMasterScheduler) {
		mpMasterScheduler->UnsetEvent(mpEventTransmit);
	}
}

void ATDiskDriveSerialByteTransmitQueue::AddTransmitByte(uint32 masterTime, uint8 data, uint32 cyclesPerBit) {
	VDASSERT(cyclesPerBit);
	static_assert(!(kTransmitQueueSize & (kTransmitQueueSize - 1)), "mTransmitQueue size not pow2");

	// check if previous transition is at same time
	const uint32 queueLen = (mTransmitTail - mTransmitHead) & kTransmitQueueMask;
	if (queueLen) {
		auto& prevEdge = mTransmitQueue[(mTransmitTail - 1) & kTransmitQueueMask];

		// check if this event is at or before the last event in the queue
		if (!((prevEdge.mTime - masterTime) & UINT32_C(0x80000000))) {
			// check if we've gone backwards in time and drop event if so
			if (prevEdge.mTime == masterTime) {
				// same time -- overwrite event
				prevEdge.mData = data;
				prevEdge.mCyclesPerBit = cyclesPerBit;
			} else {
				VDASSERT(!"Dropping new event earlier than tail in transmit queue.");
			}

			return;
		}
	}

	// check if we have room for a new event
	if (queueLen >= kTransmitQueueMask) {
		VDASSERT(!"Transmit queue full.");
		return;
	}

	// add the new event
	auto& newEdge = mTransmitQueue[mTransmitTail++ & kTransmitQueueMask];

	newEdge.mTime = masterTime;
	newEdge.mData = data;
	newEdge.mCyclesPerBit = cyclesPerBit;

	// queue next event if needed
	QueueNextTransmitEvent();
}

void ATDiskDriveSerialByteTransmitQueue::OnScheduledEvent(uint32 id) {
	mpEventTransmit = nullptr;
	OnTransmitEvent();
}

void ATDiskDriveSerialByteTransmitQueue::OnTransmitEvent() {
	// drain transmit queue entries until we're up to date
	const uint32 t = ATSCHEDULER_GETTIME(mpMasterScheduler);

	while((mTransmitHead ^ mTransmitTail) & kTransmitQueueMask) {
		const auto& nextEdge = mTransmitQueue[mTransmitHead & kTransmitQueueMask];

		if ((t - nextEdge.mTime) & UINT32_C(0x80000000))
			break;

		mpSIOMgr->SendRawByte(nextEdge.mData, nextEdge.mCyclesPerBit, false, false, true);

		++mTransmitHead;
	}

	QueueNextTransmitEvent();
}

void ATDiskDriveSerialByteTransmitQueue::QueueNextTransmitEvent() {
	// exit if we already have an event queued
	if (mpEventTransmit)
		return;

	// exit if transmit queue is empty
	if (!((mTransmitHead ^ mTransmitTail) & kTransmitQueueMask))
		return;

	const auto& nextEvent = mTransmitQueue[mTransmitHead & kTransmitQueueMask];
	const uint32 delta = nextEvent.mTime - ATSCHEDULER_GETTIME(mpMasterScheduler);

	mpEventTransmit = mpMasterScheduler->AddEvent((uint32)(delta - 1) & UINT32_C(0x80000000) ? 1 : delta, this, kEventId_Transmit);
}

///////////////////////////////////////////////////////////////////////////

ATDiskDriveSerialCommandQueue::ATDiskDriveSerialCommandQueue()
	: mpOnDriveCommandStateChanged([](bool) {})
{
}

ATDiskDriveSerialCommandQueue::~ATDiskDriveSerialCommandQueue() {
	VDASSERT(!mpSIOMgr);
	Shutdown();
}

void ATDiskDriveSerialCommandQueue::Init(ATScheduler *driveScheduler, IATDeviceSIOManager *sioMgr) {
	mpSIOMgr = sioMgr;
	mpDriveScheduler = driveScheduler;
}

void ATDiskDriveSerialCommandQueue::Shutdown() {
	Reset();
	mpDriveScheduler = nullptr;
	mpSIOMgr = nullptr;
}

void ATDiskDriveSerialCommandQueue::SetOnDriveCommandStateChanged(vdfunction<void(bool)> fn) {
	mpOnDriveCommandStateChanged = std::move(fn);
}

void ATDiskDriveSerialCommandQueue::Reset() {
	mCommandQueue.clear();

	mbDriveCommandState = false;

	if (mpDriveScheduler) {
		mpDriveScheduler->UnsetEvent(mpEventDriveCommandChange);
	}
}

void ATDiskDriveSerialCommandQueue::OnScheduledEvent(uint32 id) {
	mpEventDriveCommandChange = nullptr;
	OnCommandChangeEvent();
}

void ATDiskDriveSerialCommandQueue::AddCommandEdge(uint32 driveTime, uint32 polarity) {
	// check if previous transition is at same time
	while(!mCommandQueue.empty()) {
		auto& prevEdge = mCommandQueue.back();

		// check if this event is at or before the last event in the queue
		if ((prevEdge.mTime - driveTime) & UINT32_C(0x80000000)) {
			// The previous edge is before the time of this edge, so we're good.
			break;
		}

		// check if we've gone backwards in time and drop event if so
		if (prevEdge.mTime == driveTime) {
			// same time -- overwrite event and exit
			prevEdge.mBit = polarity;
			return;
		}
			
		if (polarity && !prevEdge.mBit) {
			// If we're asserting /COMMAND, allow it to supercede earlier deassert events.
			mCommandQueue.pop_back();
		} else {
			VDASSERT(!"Dropping new event earlier than tail in command change queue.");
			return;
		}
	}

	// add the new event
	mCommandQueue.push_back( { driveTime, polarity } );

	// queue next event if needed
	QueueNextCommandEvent();
}

void ATDiskDriveSerialCommandQueue::QueueNextCommandEvent() {
	// exit if we already have an event queued
	if (mpEventDriveCommandChange)
		return;

	// exit if transmit queue is empty
	if (mCommandQueue.empty())
		return;

	const auto& nextEvent = mCommandQueue.front();
	uint32 delta = (nextEvent.mTime - ATSCHEDULER_GETTIME(mpDriveScheduler)) & UINT32_C(0x7FFFFFFF);

	mpEventDriveCommandChange = mpDriveScheduler->AddEvent((uint32)(delta - 1) & UINT32_C(0x40000000) ? 1 : delta, this, kEventId_DriveCommandChange);
}

void ATDiskDriveSerialCommandQueue::OnCommandChangeEvent() {
	// drain command queue entries until we're up to date
	const uint32 t = ATSCHEDULER_GETTIME(mpDriveScheduler);

	while(!mCommandQueue.empty()) {
		const auto& nextEdge = mCommandQueue.front();

		if ((t - nextEdge.mTime) & UINT32_C(0x40000000))
			break;

		bool bit = (nextEdge.mBit != 0);

		if (mbDriveCommandState != bit) {
			mbDriveCommandState = bit;

			mpOnDriveCommandStateChanged(bit);
		}

		mCommandQueue.pop_front();
	}

	// if we still have events, queue another event
	if (!mCommandQueue.empty()) {
		mpDriveScheduler->SetEvent(mCommandQueue.front().mTime - t, this, kEventId_DriveCommandChange, mpEventDriveCommandChange);
	}
}

///////////////////////////////////////////////////////////////////////////

ATDiskDriveFirmwareControl::~ATDiskDriveFirmwareControl() {
	VDASSERT(!mpFwMgr);
}

void ATDiskDriveFirmwareControl::Init(void *mem, uint32 len, ATFirmwareType type) {
	mpFirmware = mem;
	mFirmwareLen = len;
	mFirmwareType = type;
}

void ATDiskDriveFirmwareControl::Shutdown() {
	mpFwMgr = nullptr;
}

void ATDiskDriveFirmwareControl::InitFirmware(ATFirmwareManager *fwman) {
	mpFwMgr = fwman;

	ReloadFirmware();
}

bool ATDiskDriveFirmwareControl::ReloadFirmware() {
	const uint64 id = mpFwMgr->GetFirmwareOfType(mFirmwareType, true);
	
	uint32 len = 0;
	bool changed = false;
	mpFwMgr->LoadFirmware(id, mpFirmware, 0, mFirmwareLen, &changed, &len, nullptr, nullptr, &mbFirmwareUsable);

	return changed;
}

const wchar_t *ATDiskDriveFirmwareControl::GetWritableFirmwareDesc(uint32 idx) const {
	return nullptr;
}

bool ATDiskDriveFirmwareControl::IsWritableFirmwareDirty(uint32 idx) const {
	return false;
}

void ATDiskDriveFirmwareControl::SaveWritableFirmware(uint32 idx, IVDStream& stream) {
}

ATDeviceFirmwareStatus ATDiskDriveFirmwareControl::GetFirmwareStatus() const {
	return mbFirmwareUsable ? ATDeviceFirmwareStatus::OK : ATDeviceFirmwareStatus::Missing;
}

///////////////////////////////////////////////////////////////////////////

ATDiskDriveDebugTargetControl::~ATDiskDriveDebugTargetControl() {
	VDASSERT(!mpSlowScheduler);
	VDASSERT(!mpScheduler);
}

void ATDiskDriveDebugTargetControl::InitTargetControl(IATDiskDriveDebugTargetProxy& proxy, double timestampFrequency, ATDebugDisasmMode disasmMode, ATDebugTargetBreakpointsBase *bpImpl, IATDevice *parentDevice) {
	mpProxy = &proxy;
	mTimestampFrequency = timestampFrequency;
	mDisplayFrequency = timestampFrequency;
	mpBreakpointsImpl = bpImpl;
	mDisasmMode = disasmMode;

	const auto rwmaps = proxy.GetReadWriteMaps();
	mpReadMap = rwmaps.first;
	mpWriteMap = rwmaps.second;

	mDisplayName = "Device MCU";

	if (parentDevice) {
		ATDeviceInfo devInfo;
		parentDevice->GetDeviceInfo(devInfo);

		mDisplayName.append(" (");
		mDisplayName.append(VDTextWToA(devInfo.mpDef->mpName).c_str());
		mDisplayName.append(")");
	}
}

void ATDiskDriveDebugTargetControl::ApplyDisplayCPUClockMultiplier(double f) {
	mDisplayFrequency *= f;
}

void ATDiskDriveDebugTargetControl::ResetTargetControl() {
	mLastSync = ATSCHEDULER_GETTIME(mpScheduler);
	mLastSyncDriveTime = ATSCHEDULER_GETTIME(&mDriveScheduler);
	mLastSyncDriveTimeF32 = 0;
	mRawTimestampToDriveAdjust = mLastSyncDriveTime - mpProxy->GetTime();

	mpScheduler->UnsetEvent(mpImmRunEvent);

	const auto systemRate = mpScheduler->GetRate();
	mSystemCyclesPerDriveCycleF32 = VDRoundToInt64(0x1.0p+32 / mTimestampFrequency * systemRate.asDouble());
	mDriveCyclesPerSystemCycleF32 = VDRoundToInt64(0x1.0p+32 * mTimestampFrequency * systemRate.AsInverseDouble());

	mDriveCycleAccumF32 = 0;
	mDriveCycleLimit = ATSCHEDULER_GETTIME(&mDriveScheduler);
}

void ATDiskDriveDebugTargetControl::ShutdownTargetControl() {
	if (mpSlowScheduler) {
		mpSlowScheduler->UnsetEvent(mpRunEvent);
		mpSlowScheduler = nullptr;
	}

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpImmRunEvent);
		mpScheduler = nullptr;
	}
}

uint32 ATDiskDriveDebugTargetControl::DriveTimeToMasterTime() const {
	// convert drive time to computer time
	const uint32 currentDriveTime = ATSCHEDULER_GETTIME(&mDriveScheduler);
	const uint64 driveTimeSyncDeltaF32 = mLastSyncDriveTimeF32 - ((uint64)(currentDriveTime - mLastSyncDriveTime) << 32);
	VDASSERT(driveTimeSyncDeltaF32 < UINT64_C(0x80000000'00000000));

	const uint64 systemTimeSyncDelta = VDUMul64x64To128(driveTimeSyncDeltaF32, mSystemCyclesPerDriveCycleF32).getHi();
	VDASSERT(systemTimeSyncDelta < UINT64_C(0x80000000'00000000));

	const uint32 t = mLastSync - (uint32)systemTimeSyncDelta;
	return t;
}

uint32 ATDiskDriveDebugTargetControl::MasterTimeToDriveTime() const {
	const uint64 ct = (ATSCHEDULER_GETTIME(mpScheduler) - mLastSync) * mDriveCyclesPerSystemCycleF32;
	return mLastSyncDriveTime + (uint32)(ct >> 32);
}

uint64 ATDiskDriveDebugTargetControl::MasterTimeToDriveTime64(uint64 t) const {
	const uint64 currentSystemTime64 = mpScheduler->GetTick64();
	const uint64 currentDriveTime64 = mDriveScheduler.GetTick64();

	// compute system time delta from sync
	uint64 deltaSystemTime64 = ((uint32)currentSystemTime64 - mLastSync) + (t - currentSystemTime64);
	bool neg = false;

	if (deltaSystemTime64 >= UINT64_C(0x80000000'00000000)) {
		deltaSystemTime64 = UINT64_C(0) - deltaSystemTime64;
		neg = true;
	}

	// convert system time delta to drive time delta (64 * 32.32 fixed point)
	const uint64 deltaDriveTime64 = (uint64)(VDUMul64x64To128(deltaSystemTime64, mDriveCyclesPerSystemCycleF32) >> 32);

	// extend drive sync time to 64-bit
	const uint64 driveTime64 = currentDriveTime64 + (sint64)(sint32)(mLastSyncDriveTime - (uint32)currentDriveTime64);

	// add drive time delta to drive sync time to get drive time
	return neg ? driveTime64 - deltaDriveTime64 : driveTime64 + deltaDriveTime64;
}

uint32 ATDiskDriveDebugTargetControl::AccumSubCycles() {
	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	const uint32 cycles = t - mLastSync;
	VDASSERT(!(cycles & UINT32_C(0x80000000)));

	mLastSync = t;

	uint64 driveCyclesF32 = mDriveCycleAccumF32 + cycles * mDriveCyclesPerSystemCycleF32;
	mDriveCycleAccumF32 = (uint32)driveCyclesF32;

	mLastSyncDriveTime = mDriveCycleLimit;
	mLastSyncDriveTimeF32 = driveCyclesF32;

	mDriveCycleLimit += (uint32)(driveCyclesF32 >> 32);

	return mDriveCycleLimit;
}

void ATDiskDriveDebugTargetControl::ScheduleImmediateResume() {
	mpScheduler->SetEvent(1, this, kEventId_ImmRun, mpImmRunEvent);
}

void *ATDiskDriveDebugTargetControl::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceScheduling::kTypeID: return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceDebugTarget::kTypeID: return static_cast<IATDeviceDebugTarget *>(this);
		case IATDebugTargetBreakpoints::kTypeID: return static_cast<IATDebugTargetBreakpoints *>(mpBreakpointsImpl);
		case IATDebugTargetHistory::kTypeID: return static_cast<IATDebugTargetHistory *>(this);
		case IATDebugTargetExecutionControl::kTypeID: return static_cast<IATDebugTargetExecutionControl *>(this);

		default:
			return nullptr;
	}
}

void ATDiskDriveDebugTargetControl::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
	mpSlowScheduler = slowsch;

	mpSlowScheduler->SetEvent(1, this, 1, mpRunEvent);
}

void ATDiskDriveDebugTargetControl::OnScheduledEvent(uint32 id) {
	if (id == kEventId_Run) {
		mpRunEvent = mpSlowScheduler->AddEvent(1, this, 1);

		mDriveScheduler.UpdateTick64();
		Sync();
	} else if (id == kEventId_ImmRun) {
		mpImmRunEvent = nullptr;
		Sync();
	}
}

IATDebugTarget *ATDiskDriveDebugTargetControl::GetDebugTarget(uint32 index) {
	if (index == 0)
		return this;

	return nullptr;
}

const char *ATDiskDriveDebugTargetControl::GetName() {
	return mDisplayName.c_str();
}

ATDebugDisasmMode ATDiskDriveDebugTargetControl::GetDisasmMode() {
	return mDisasmMode;
}

float ATDiskDriveDebugTargetControl::GetDisplayCPUClock() const {
	return mDisplayFrequency;
}

sint32 ATDiskDriveDebugTargetControl::GetTimeSkew() {
	// The 810's CPU runs at 500KHz, while the computer runs at 1.79MHz. We use
	// a ratio of 229/64.

	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	const uint32 cot = ATSCHEDULER_GETTIME(&mDriveScheduler);
	const uint32 cycles = (t - mLastSync) + (((mDriveCycleLimit - cot) * mSystemCyclesPerDriveCycleF32 + ((mDriveCycleAccumF32 * mSystemCyclesPerDriveCycleF32) >> 32) + 0xFFFFFFFF) >> 32);

	return -(sint32)cycles;
}

uint32 ATDiskDriveDebugTargetControl::ConvertRawTimestamp(uint32 rawTimestamp) const {
	// mLastSync is the machine cycle at which all sub-cycles have been pushed into the
	// coprocessor, and the coprocessor's time base is the sub-cycle corresponding to
	// the end of that machine cycle.
	return mLastSync - (((mDriveCycleLimit - rawTimestamp - mRawTimestampToDriveAdjust) * mSystemCyclesPerDriveCycleF32 + ((mDriveCycleAccumF32 * mSystemCyclesPerDriveCycleF32) >> 32) + 0xFFFFFFFF) >> 32);
}

void ATDiskDriveDebugTargetControl::GetExecState(ATCPUExecState& state) {
	mpProxy->GetExecState(state);
}

void ATDiskDriveDebugTargetControl::SetExecState(const ATCPUExecState& state) {
	mpProxy->SetExecState(state);
}

uint8 ATDiskDriveDebugTargetControl::ReadByte(uint32 address) {
	uint8 v;
	ATCoProcReadMemory(mpReadMap, &v, address, 1);

	return v;
}

void ATDiskDriveDebugTargetControl::ReadMemory(uint32 address, void *dst, uint32 n) {
	ATCoProcReadMemory(mpReadMap, dst, address, n);
}

uint8 ATDiskDriveDebugTargetControl::DebugReadByte(uint32 address) {
	uint8 v;
	ATCoProcDebugReadMemory(mpReadMap, &v, address, 1);

	return v;
}

void ATDiskDriveDebugTargetControl::DebugReadMemory(uint32 address, void *dst, uint32 n) {
	ATCoProcDebugReadMemory(mpReadMap, dst, address, n);
}

void ATDiskDriveDebugTargetControl::WriteByte(uint32 address, uint8 value) {
	ATCoProcWriteMemory(mpWriteMap, &value, address, 1);
}

void ATDiskDriveDebugTargetControl::WriteMemory(uint32 address, const void *src, uint32 n) {
	ATCoProcWriteMemory(mpWriteMap, src, address, n);
}

bool ATDiskDriveDebugTargetControl::GetHistoryEnabled() const {
	return !mHistory.empty();
}

void ATDiskDriveDebugTargetControl::SetHistoryEnabled(bool enable) {
	if (enable) {
		if (mHistory.empty()) {
			mHistorySize = 0x20000;
			mHistoryMask = mHistorySize - 1;

			mHistory.resize(mHistorySize, ATCPUHistoryEntry());
			mpProxy->SetHistoryBuffer(mHistory.data());
		}
	} else {
		if (!mHistory.empty()) {
			decltype(mHistory) tmp;
			tmp.swap(mHistory);
			mHistory.clear();
			mpProxy->SetHistoryBuffer(nullptr);
		}
	}
}

std::pair<uint32, uint32> ATDiskDriveDebugTargetControl::GetHistoryRange() const {
	const uint32 hcnt = mpProxy->GetHistoryCounter();

	return std::pair<uint32, uint32>(hcnt - mHistorySize, hcnt);
}

uint32 ATDiskDriveDebugTargetControl::ExtractHistory(const ATCPUHistoryEntry **hparray, uint32 start, uint32 n) const {
	if (!n || mHistory.empty())
		return 0;

	const ATCPUHistoryEntry *hstart = mHistory.data();
	const ATCPUHistoryEntry *hend = hstart + mHistorySize;
	const ATCPUHistoryEntry *hsrc = hstart + (start & mHistoryMask);

	for(uint32 i=0; i<n; ++i) {
		*hparray++ = hsrc;

		if (++hsrc == hend)
			hsrc = hstart;
	}

	return n;
}

double ATDiskDriveDebugTargetControl::GetTimestampFrequency() const {
	return mTimestampFrequency;
}

void ATDiskDriveDebugTargetControl::Break() {
	CancelStep();
}

bool ATDiskDriveDebugTargetControl::StepInto(const vdfunction<void(bool)>& fn) {
	CancelStep();

	mpStepHandler = fn;
	mbStepOut = false;
	mStepStartTime = mpProxy->GetTime();
	mpBreakpointsImpl->SetStepActive(true);
	Sync();
	return true;
}

bool ATDiskDriveDebugTargetControl::StepOver(const vdfunction<void(bool)>& fn) {
	CancelStep();

	mpStepHandler = fn;
	mbStepOut = true;
	mStepStartTime = mpProxy->GetTime();
	mStepOutSP = mpProxy->GetStepStackLevel();
	mpBreakpointsImpl->SetStepActive(true);
	Sync();
	return true;
}

bool ATDiskDriveDebugTargetControl::StepOut(const vdfunction<void(bool)>& fn) {
	CancelStep();

	mpStepHandler = fn;
	mbStepOut = true;
	mStepStartTime = mpProxy->GetTime();
	mStepOutSP = mpProxy->GetStepStackLevel() + 1;
	mpBreakpointsImpl->SetStepActive(true);
	Sync();
	return true;
}

void ATDiskDriveDebugTargetControl::StepUpdate() {
	Sync();
}

void ATDiskDriveDebugTargetControl::RunUntilSynced() {
	CancelStep();
	Sync();
}

bool ATDiskDriveDebugTargetControl::CheckBreakpoint(uint32 pc) {
	if (mpProxy->GetTime() == mStepStartTime)
		return false;

	const bool bpHit = mpBreakpointsImpl->CheckBP(pc);

	if (!bpHit) {
		if (mbStepOut) {
			// Keep stepping if wrapped(s < s0).
			if ((mpProxy->GetStepStackLevel() - mStepOutSP) & 0x80000000)
				return false;
		}
	}

	mpBreakpointsImpl->SetStepActive(false);

	mbStepNotifyPending = true;
	mbStepNotifyPendingBP = bpHit;
	return true;
}

void ATDiskDriveDebugTargetControl::CancelStep() {
	if (mpStepHandler) {
		mpBreakpointsImpl->SetStepActive(false);

		auto p = std::move(mpStepHandler);
		mpStepHandler = nullptr;

		p(false);
	}
}

void ATDiskDriveDebugTargetControl::FlushStepNotifications2() {
	mbStepNotifyPending = false;

	auto p = std::move(mpStepHandler);
	mpStepHandler = nullptr;

	if (p)
		p(!mbStepNotifyPendingBP);
}
