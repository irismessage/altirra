//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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
#include <vd2/system/hash.h>
#include <vd2/system/int128.h>
#include <vd2/system/math.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/audiosource.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/deviceindicators.h>
#include <at/atcore/logging.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/wraptime.h>
#include <at/atcpu/memorymap.h>
#include "820full.h"
#include "audiosampleplayer.h"
#include "debuggerlog.h"
#include "firmwaremanager.h"
#include "memorymanager.h"
#include "modem.h"

void ATCreateDevice820Full(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevice820Full> p(new ATDevice820Full);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefPrinter820Full = { "820full", "820full", L"820 40-Column Printer (full emulation)", ATCreateDevice820Full };

/////////////////////////////////////////////////////////////////////////////

ATDevice820Full::TargetProxy::TargetProxy(ATDevice820Full& parent)
	: mParent(parent)
{
}
	
uint32 ATDevice820Full::TargetProxy::GetTime() const {
	return ATSCHEDULER_GETTIME(&mParent.mDriveScheduler);
}

/////////////////////////////////////////////////////////////////////////////

ATDevice820Full::ATDevice820Full()
	: mTargetProxy(*this)
	, mCoProc(false, false)
{
	mBreakpointsImpl.BindBPHandler(mCoProc);
	mBreakpointsImpl.SetStepHandler(this);

	mDriveScheduler.SetRate(VDFraction(1000000, 1));
	mLineDriveTimeLast = mDriveScheduler.GetTick();

	mTargetProxy.Init(mCoProc);
	InitTargetControl(mTargetProxy, mDriveScheduler.GetRate().asDouble(), kATDebugDisasmMode_6502, &mBreakpointsImpl, this);

	mSerialCmdQueue.SetOnDriveCommandStateChanged(
		[this](bool asserted) {
			mRIOT.SetInputB(asserted ? 0x08 : 0x00, 0x08);
		}
	);
}

ATDevice820Full::~ATDevice820Full() {
}

void *ATDevice820Full::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceScheduling::kTypeID: return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceFirmware::kTypeID: return static_cast<IATDeviceFirmware *>(this);
		case IATDeviceSIO::kTypeID: return static_cast<IATDeviceSIO *>(this);
		case ATRIOT6532Emulator::kTypeID: return &mRIOT;
	}

	void *p = ATDiskDriveDebugTargetControl::AsInterface(iid);
	if (p)
		return p;

	return ATDevice::AsInterface(iid);
}

void ATDevice820Full::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPrinter820Full;
}

void ATDevice820Full::Init() {
	// We need to do this early to ensure that the clock divisor is set before we perform init processing.
	ResetTargetControl();

	mSerialXmitQueue.Init(mpScheduler, mpSIOMgr);
	mSerialCmdQueue.Init(&mDriveScheduler, mpSIOMgr);

	mRIOT.Init(&mDriveScheduler);
	mRIOT.SetInputA(0xFF, 0xFF);
	mRIOT.SetInputB(0xFF, 0xFF);

	// Unlike the other coprocessor types, we don't actually need read/write maps. This just
	// makes our life easier as we can leverage the default impl.
	ATCoProcMemoryMapView mmap(mCoProc.GetReadMap(), mCoProc.GetWriteMap());

	mmap.Clear(mDummyRead, mDummyWrite);

	mRIOTMemoryReadHandler.BindMethod<&ATDevice820Full::OnRIOTMemoryRead>(this);
	mRIOTMemoryWriteHandler.BindMethod<&ATDevice820Full::OnRIOTMemoryWrite>(this);
	mRIOTRegistersReadHandler.BindMethods<&ATDevice820Full::OnRIOTRegistersRead, &ATDevice820Full::OnRIOTRegistersDebugRead>(this);
	mRIOTRegistersWriteHandler.BindMethod<&ATDevice820Full::OnRIOTRegistersWrite>(this);

	mmap.SetHandlers(0x00, 0x02, mRIOTMemoryReadHandler, mRIOTMemoryWriteHandler);
	mmap.SetHandlers(0x02, 0x02, mRIOTRegistersReadHandler, mRIOTRegistersWriteHandler);
	mmap.SetHandlers(0x04, 0x02, mRIOTMemoryReadHandler, mRIOTMemoryWriteHandler);
	mmap.SetHandlers(0x06, 0x02, mRIOTRegistersReadHandler, mRIOTRegistersWriteHandler);

	mmap.SetReadMem(0x08, 0x08, mROM);
	mmap.MirrorFwd(0x10, 0xF0, 0x00);

	mPrinterSoundSource.Init(*GetService<IATAudioMixer>(), *GetService<IATDeviceSchedulingService>()->GetMachineScheduler(), "820");

	ATPrinterGraphicsSpec spec {};
	spec.mPageWidthMM = 98.425f;			// 3+7/8" wide paper
	spec.mPageVBorderMM = 8.0f;				// vertical border
	spec.mDotRadiusMM = 0.22f;				// guess for dot radius
	spec.mVerticalDotPitchMM = 0.44704f;	// 0.0176" vertical pitch
	spec.mbBit0Top = true;
	spec.mNumPins = 7;

	mpPrinterGraphicalOutput = GetService<IATPrinterOutputManager>()->CreatePrinterGraphicalOutput(spec);
}

void ATDevice820Full::Shutdown() {
	mpPrinterGraphicalOutput = nullptr;

	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);

	ShutdownTargetControl();

	mpFwMgr = nullptr;

	mRIOT.Shutdown();

	mSerialCmdQueue.Shutdown();
	mSerialXmitQueue.Shutdown();

	if (mpSIOMgr) {
		mpSIOMgr->RemoveRawDevice(this);
		mpSIOMgr = nullptr;
	}

	mPrinterSoundSource.Shutdown();
}

void ATDevice820Full::WarmReset() {
	// If the computer resets, its transmission is interrupted.
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
}

void ATDevice820Full::ComputerColdReset() {
	WarmReset();
}

void ATDevice820Full::PeripheralColdReset() {
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);

	mSerialXmitQueue.Reset();
	mSerialCmdQueue.Reset();
	
	mCoProc.ColdReset();
	mRIOT.Reset();

	ResetTargetControl();

	OnReadyStateChanged(mpSIOMgr->IsSIOReadyAsserted());

	UpdateDriveMotorState();

	WarmReset();
}

void ATDevice820Full::InitFirmware(ATFirmwareManager *fwman) {
	mpFwMgr = fwman;

	ReloadFirmware();
}

bool ATDevice820Full::ReloadFirmware() {
	const vduint128 oldHash = VDHash128(mROM, sizeof mROM);

	uint8 rom[2048] = {};

	uint32 len = 0;
	bool usable = false;

	mpFwMgr->LoadFirmware(mpFwMgr->GetFirmwareOfType(kATFirmwareType_820, true), rom, 0, 2048, nullptr, &len, nullptr, nullptr, &usable);

	memcpy(mROM, rom, 2048);

	const vduint128 newHash = VDHash128(mROM, sizeof mROM);

	mbFirmwareUsable = usable;

	return oldHash != newHash;
}

const wchar_t *ATDevice820Full::GetWritableFirmwareDesc(uint32 idx) const {
	return nullptr;
}

bool ATDevice820Full::IsWritableFirmwareDirty(uint32 idx) const {
	return false;
}

void ATDevice820Full::SaveWritableFirmware(uint32 idx, IVDStream& stream) {
}

ATDeviceFirmwareStatus ATDevice820Full::GetFirmwareStatus() const {
	return mbFirmwareUsable ? ATDeviceFirmwareStatus::OK : ATDeviceFirmwareStatus::Missing;
}

void ATDevice820Full::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

void ATDevice820Full::OnScheduledEvent(uint32 id) {
	if (id == kEventId_DriveReceiveBit) {
		mRIOT.SetInputB(mReceiveShiftRegister & 1 ? 0 : 0x04, 0x04);
		mReceiveShiftRegister >>= 1;
		mpEventDriveReceiveBit = nullptr;

		if (mReceiveShiftRegister) {
			mReceiveTimingAccum += mReceiveTimingStep;
			mpEventDriveReceiveBit = mDriveScheduler.AddEvent(mReceiveTimingAccum >> 10, this, kEventId_DriveReceiveBit);
			mReceiveTimingAccum &= 0x3FF;
		}
	} else if (id == kEventId_DriveMotor) {
		mpEventDriveMotor = nullptr;

		UpdateDriveMotorState();
	} else
		return ATDiskDriveDebugTargetControl::OnScheduledEvent(id);
}

void ATDevice820Full::OnCommandStateChanged(bool asserted) {
	if (mbCommandState != asserted) {
		mbCommandState = asserted;

		AddCommandEdge(asserted);
	}
}

void ATDevice820Full::OnMotorStateChanged(bool asserted) {
}

void ATDevice820Full::OnReadyStateChanged(bool asserted) {
	mRIOT.SetInputB(asserted ? 0 : 0x20, 0x20);
}

void ATDevice820Full::OnBeginReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	// ignore bytes with invalid clocking
	if (!cyclesPerBit)
		return;

	Sync();

	mReceiveShiftRegister = c + c + 0x200;

	// 572/1024 ~= 1.00MHz / 1.79MHz
	mReceiveTimingAccum = 0x200;
	mReceiveTimingStep = cyclesPerBit * 572;

	mDriveScheduler.SetEvent(1, this, kEventId_DriveReceiveBit, mpEventDriveReceiveBit);
}

void ATDevice820Full::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
}

void ATDevice820Full::OnTruncateByte() {
	mReceiveShiftRegister = 1;
	mRIOT.SetInputB(0, 0x04);
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
}

void ATDevice820Full::OnSendReady() {
}

uint8 ATDevice820Full::OnRIOTMemoryRead(uint32 addr) const {
	return addr & 0x80 ? mRIOTRAM[addr & 0x7F] : 0xFF;
}

void ATDevice820Full::OnRIOTMemoryWrite(uint32 addr, uint8 val) {
	if (addr & 0x80)
		mRIOTRAM[addr & 0x7F] = val;
}

uint8 ATDevice820Full::OnRIOTRegistersDebugRead(uint32 addr) const {
	return mRIOT.DebugReadByte((uint8)addr);
}

uint8 ATDevice820Full::OnRIOTRegistersRead(uint32 addr) {
	return mRIOT.ReadByte((uint8)addr);
}

void ATDevice820Full::OnRIOTRegistersWrite(uint32 addr, uint8 val) {
	// check for a write to DRA or DDRA
	if ((addr & 6) == 0) {
		const uint8 outprev = mRIOT.ReadOutputA();
		mRIOT.WriteByte((uint8)addr, val);
		const uint8 outnext = mRIOT.ReadOutputA();
		const uint8 delta = outnext ^ outprev;

		if (delta & 0xFE) {
			const uint8 pinsDown = (outprev & ~outnext) >> 1;
			
			if (pinsDown) {
				int numPinsDown = 0;

				for(uint8 t = pinsDown; t; t &= t - 1)
					++numPinsDown;

				mPrinterSoundSource.AddPinSound(ConvertRawTimestamp(mDriveScheduler.GetTick()), numPinsDown);

				if (mpPrinterGraphicalOutput) {
					// 10.75 inches/second -> 273.05 mm/sec -> 0.00027305 mm/usec

					UpdateDriveMotorState();
					
					uint32 lineCycleOffset = mLineDriveTimeOffset;

					if (lineCycleOffset < kDriveCyclesLineStartOffset)
						lineCycleOffset += kDriveCyclesLinePeriod;

					lineCycleOffset -= kDriveCyclesLineStartOffset;

					if (lineCycleOffset >= kDriveCyclesLinePeriodHalf)
						lineCycleOffset = kDriveCyclesLinePeriod - lineCycleOffset;

					float x = 0.00027305f * (float)lineCycleOffset;

					mpPrinterGraphicalOutput->Print(x, pinsDown);
				}
			}
		}

		if (delta & 0x01) {
			const bool newMotorRunning = (outnext & 1) == 0;
			if (mbMotorRunning != newMotorRunning) {
				mbMotorRunning = newMotorRunning;

				if (newMotorRunning)
					mLineDriveTimeLast = ATSCHEDULER_GETTIME(&mDriveScheduler);

				UpdateDriveMotorState();
			}
		}

		return;
	}

	// check for a write to DRB or DDRB
	if ((addr & 6) == 2) {
		const uint8 outprev = mRIOT.ReadOutputB();
		mRIOT.WriteByte((uint8)addr, val);
		const uint8 outnext = mRIOT.ReadOutputB();
		const uint8 delta = outnext ^ outprev;

		if (delta & 0x10)
			mSerialXmitQueue.AddTransmitBit(DriveTimeToMasterTime() + mSerialXmitQueue.kTransmitLatency, (outnext & 0x10) != 0);

		return;
	}

	mRIOT.WriteByte((uint8)addr, val);
}

void ATDevice820Full::UpdateDriveMotorState() {
	if (!mbMotorRunning) {
		mDriveScheduler.UnsetEvent(mpEventDriveMotor);
		return;
	}

	const uint32 dt = ATSCHEDULER_GETTIME(&mDriveScheduler);
	const uint32 delta = dt - mLineDriveTimeLast;
	mLineDriveTimeLast = dt;

	mLineDriveTimeOffset += delta;

	if (mLineDriveTimeOffset >= kDriveCyclesLinePeriod) {
		mLineDriveTimeOffset -= kDriveCyclesLinePeriod;

		if (mLineDriveTimeOffset >= kDriveCyclesLinePeriod) {
			VDFAIL("Drive time offset too high");
			mLineDriveTimeOffset = 0;
		}

		if (mpPrinterGraphicalOutput) {
			// 1/6" -> 4.2333 mm
			mpPrinterGraphicalOutput->FeedPaper(4.23333f);
		}
	}

	// update print ready
	mRIOT.SetInputB(mLineDriveTimeOffset >= kDriveCyclesLinePrintReady ? 1 : 0, 1);

	// update event
	if (mLineDriveTimeOffset >= kDriveCyclesLinePrintReady)
		mDriveScheduler.SetEvent(kDriveCyclesLinePeriod - mLineDriveTimeOffset, this, kEventId_DriveMotor, mpEventDriveMotor);
	else
		mDriveScheduler.SetEvent(kDriveCyclesLinePrintReady - mLineDriveTimeOffset, this, kEventId_DriveMotor, mpEventDriveMotor);
}

void ATDevice820Full::Sync() {
	uint32 newDriveCycleLimit = AccumSubCycles();

	bool ranToCompletion = true;

	VDASSERT(mDriveScheduler.mNextEventCounter >= 0xFF000000);
	if (ATSCHEDULER_GETTIME(&mDriveScheduler) - newDriveCycleLimit >= 0x80000000) {
		mDriveScheduler.SetStopTime(newDriveCycleLimit);
		ranToCompletion = mCoProc.Run(mDriveScheduler);

		VDASSERT(ATWrapTime{ATSCHEDULER_GETTIME(&mDriveScheduler)} <= newDriveCycleLimit);
	}

	if (!ranToCompletion)
		ScheduleImmediateResume();

	FlushStepNotifications();
}

void ATDevice820Full::AddTransmitEdge(bool polarity) {
	mSerialXmitQueue.AddTransmitBit(DriveTimeToMasterTime() + mSerialXmitQueue.kTransmitLatency, polarity);
}

void ATDevice820Full::AddCommandEdge(uint32 polarity) {
	mSerialCmdQueue.AddCommandEdge(MasterTimeToDriveTime(), polarity);
}
