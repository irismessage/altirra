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
#include <at/atcore/deviceserial.h>
#include <at/atcore/deviceindicators.h>
#include <at/atcore/logging.h>
#include <at/atcore/propertyset.h>
#include <at/atcpu/memorymap.h>
#include "1029full.h"
#include "debuggerlog.h"
#include "firmwaremanager.h"
#include "memorymanager.h"

void ATCreateDevice1029Full(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevice1029Full> p(new ATDevice1029Full);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefPrinter1029Full = { "1029full", nullptr, L"1029 80-Column Printer (full emulation)", ATCreateDevice1029Full };

/////////////////////////////////////////////////////////////////////////////

ATDevice1029Full::TargetProxy::TargetProxy(ATDevice1029Full& parent)
	: mParent(parent)
{
}

std::pair<const uintptr *, const uintptr *> ATDevice1029Full::TargetProxy::GetReadWriteMaps() const {
	return { mParent.mDebugReadMap, mParent.mDebugWriteMap };
}

void ATDevice1029Full::TargetProxy::SetHistoryBuffer(ATCPUHistoryEntry *harray) {
	mParent.mCoProc.SetHistoryBuffer(harray);
}

uint32 ATDevice1029Full::TargetProxy::GetHistoryCounter() const {
	return mParent.mCoProc.GetHistoryCounter();
}
	
uint32 ATDevice1029Full::TargetProxy::GetTime() const {
	return mParent.mCoProc.GetTime();
}

uint32 ATDevice1029Full::TargetProxy::GetStepStackLevel() const {
	return mParent.mCoProc.GetStepStackLevel();
}

void ATDevice1029Full::TargetProxy::GetExecState(ATCPUExecState& state) const {
	mParent.mCoProc.GetExecState(state);
}

void ATDevice1029Full::TargetProxy::SetExecState(const ATCPUExecState& state) {
	mParent.mCoProc.SetExecState(state);
}

/////////////////////////////////////////////////////////////////////////////

ATDevice1029Full::ATDevice1029Full()
	: mTargetProxy(*this)
{
	mBreakpointsImpl.BindBPHandler(mCoProc);
	mBreakpointsImpl.SetStepHandler(this);

	mCoProc.SetProgramBanks(mROM[0], mROM[1]);

	// The 8048 in the 1029 runs T-states at 10MHz with 15 T-states per
	// machine cycle, with the majority of instructions only 1-2 machine
	// cycles. To keep accounting simple we run the drive coprocessor clock
	// at 10MHz / 15, but need to report 10MHz as the displayed clock
	// speed.
	InitTargetControl(mTargetProxy, 10000000.0 / 15.0, kATDebugDisasmMode_8048, &mBreakpointsImpl, this);
	ApplyDisplayCPUClockMultiplier(15.0);
	
	mSerialCmdQueue.SetOnDriveCommandStateChanged(
		[this](bool bit) {
			mbCommandState = bit;
		}
	);
}

ATDevice1029Full::~ATDevice1029Full() {
}

void *ATDevice1029Full::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceScheduling::kTypeID: return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceFirmware::kTypeID: return static_cast<IATDeviceFirmware *>(this);
		case IATDeviceSIO::kTypeID: return static_cast<IATDeviceSIO *>(this);
	}

	void *p = ATDiskDriveDebugTargetControl::AsInterface(iid);
	if (p)
		return p;

	return ATDevice::AsInterface(iid);
}

void ATDevice1029Full::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPrinter1029Full;
}

void ATDevice1029Full::Init() {
	mDriveScheduler.SetRate(VDFraction(10000000, 15));

	// We need to do this early to ensure that the clock divisor is set before we perform init processing.
	ResetTargetControl();

	mSerialXmitQueue.Init(mpScheduler, mpSIOMgr);
	mSerialCmdQueue.Init(&mDriveScheduler, mpSIOMgr);

	mCoProc.SetPortReadHandler([this](uint8 port, uint8 output) -> uint8 { return OnReadPort(port, output); });
	mCoProc.SetPortWriteHandler([this](uint8 port, uint8 data) { OnWritePort(port, data); });
	mCoProc.SetXRAMReadHandler([this](uint8 addr) { return OnReadXRAM(addr); });
	mCoProc.SetXRAMWriteHandler([this](uint8 addr, uint8 v) { return OnWriteXRAM(addr, v); });
	mCoProc.SetT0ReadHandler([this]() { return OnReadT0(); });
	mCoProc.SetT1ReadHandler([this]() { return OnReadT1(); });

	// Unlike the other coprocessor types, we don't actually need read/write maps. This just
	// makes our life easier as we can leverage the default impl.
	ATCoProcMemoryMapView mmap(mDebugReadMap, mDebugWriteMap);

	mmap.Clear(mDummyRead, mDummyWrite);
	mmap.SetReadMem(0x00, 0x08, mROM[0]);
	mmap.SetReadMem(0x08, 0x08, mROM[1]);
	mmap.SetMemory(0x10, 0x01, mCoProc.GetInternalRAM());
	mmap.SetMemory(0x20, 0x04, mXRAM);

	mPrinterSoundSource.Init(*GetService<IATAudioMixer>(), *GetService<IATDeviceSchedulingService>()->GetMachineScheduler(), "1029");

	ATPrinterGraphicsSpec spec {};
	spec.mPageWidthMM = 215.9f;			// 8.5" wide paper
	spec.mPageVBorderMM = 8.0f;			// vertical border
	spec.mDotRadiusMM = 0.28f;			// guess for dot radius
	spec.mVerticalDotPitchMM = kVerticalDotPitchMM;		// 0.0176" vertical pitch
	spec.mbBit0Top = true;
	spec.mNumPins = 7;

	mpPrinterGraphicalOutput = GetService<IATPrinterOutputManager>()->CreatePrinterGraphicalOutput(spec);
}

void ATDevice1029Full::Shutdown() {
	mpPrinterGraphicalOutput = nullptr;

	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
	mDriveScheduler.UnsetEvent(mpEventDriveDotFallingEdge);

	ShutdownTargetControl();

	mpFwMgr = nullptr;

	mSerialCmdQueue.Shutdown();
	mSerialXmitQueue.Shutdown();

	if (mpSIOMgr) {
		mpSIOMgr->RemoveRawDevice(this);
		mpSIOMgr = nullptr;
	}

	mPrinterSoundSource.Shutdown();
}

void ATDevice1029Full::WarmReset() {
	// If the computer resets, its transmission is interrupted.
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
}

void ATDevice1029Full::ComputerColdReset() {
	WarmReset();
}

void ATDevice1029Full::PeripheralColdReset() {
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);

	mSerialXmitQueue.Reset();
	mSerialCmdQueue.Reset();
	
	mbDirectReceiveOutput = true;
	mbDirectTransmitOutput = true;

	mPrintHeadPosition = 0;
	mPrintDotPosition = 0;
	mLastHeadUpdateTime = mDriveScheduler.GetTick();
	mLastDotUpdateTime = mDriveScheduler.GetTick();

	OnWritePort(0, 0xFF);
	OnWritePort(1, 0xFF);

	mCoProc.ColdReset();
	OnReadyStateChanged(mpSIOMgr->IsSIOReadyAsserted());

	ResetTargetControl();
	UpdateHeadSounds();

	memset(mXRAM, 0xFF, sizeof mXRAM);

	WarmReset();
}

void ATDevice1029Full::InitFirmware(ATFirmwareManager *fwman) {
	mpFwMgr = fwman;

	ReloadFirmware();
}

bool ATDevice1029Full::ReloadFirmware() {
	const vduint128 oldIHash = VDHash128(mROM, sizeof mROM);

	uint8 irom[4096] = {};

	uint32 len = 0;
	bool iromUsable = false;

	mpFwMgr->LoadFirmware(mpFwMgr->GetFirmwareOfType(kATFirmwareType_1029, true), irom, 0, sizeof irom, nullptr, &len, nullptr, nullptr, &iromUsable);

	memcpy(mROM[0], irom, 2048);
	memcpy(mROM[1], irom + 2048, 2048);
	mROM[0][2048] = mROM[0][0];
	mROM[0][2049] = mROM[0][0];
	mROM[1][2048] = mROM[1][0];
	mROM[1][2049] = mROM[1][0];

	const vduint128 newIHash = VDHash128(mROM, sizeof mROM);

	mbFirmwareUsable = iromUsable;

	return oldIHash != newIHash;
}

const wchar_t *ATDevice1029Full::GetWritableFirmwareDesc(uint32 idx) const {
	return nullptr;
}

bool ATDevice1029Full::IsWritableFirmwareDirty(uint32 idx) const {
	return false;
}

void ATDevice1029Full::SaveWritableFirmware(uint32 idx, IVDStream& stream) {
}

ATDeviceFirmwareStatus ATDevice1029Full::GetFirmwareStatus() const {
	return mbFirmwareUsable ? ATDeviceFirmwareStatus::OK : ATDeviceFirmwareStatus::Missing;
}

void ATDevice1029Full::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

void ATDevice1029Full::OnScheduledEvent(uint32 id) {
	if (id == kEventId_DriveReceiveBit) {
		mpEventDriveReceiveBit = nullptr;
		mReceiveShiftRegister >>= 1;

		if (mReceiveShiftRegister >= 2) {
			mReceiveTimingAccum += mReceiveTimingStep;
			mpEventDriveReceiveBit = mDriveScheduler.AddEvent(mReceiveTimingAccum >> 10, this, kEventId_DriveReceiveBit);
			mReceiveTimingAccum &= 0x3FF;
		}
	} else if (id == kEventId_DriveDotFallingEdge) {
		mpEventDriveDotFallingEdge = nullptr;

		mCoProc.AssertHighToLowT1();

		UpdateDotEvent();
	} else if (id == kEventId_DriveHeadRetract) {
		mpEventDriveHeadRetract = nullptr;

		UpdateHeadPosition();
		VDASSERT(mPrintHeadPosition == 0);

		mPrinterSoundSource.EnableRetractSound(false);
		mPrinterSoundSource.PlayHomeSound();
	} else
		return ATDiskDriveDebugTargetControl::OnScheduledEvent(id);
}

void ATDevice1029Full::OnReadyStateChanged(bool asserted) {
	if (asserted)
		mCoProc.NegateIrq();
	else
		mCoProc.AssertIrq();
}

void ATDevice1029Full::OnCommandStateChanged(bool asserted) {
	AddCommandEdge(asserted);
}

void ATDevice1029Full::OnMotorStateChanged(bool asserted) {
}

void ATDevice1029Full::OnBeginReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	Sync();

	mReceiveShiftRegister = (c + c + 0x200) * 2 + 1;

	// 381/1024 approximation to ratio between system clock (1.789MHz) and device
	// machine cycle rate (666.7KHz).
	mReceiveTimingAccum = 0x200;
	mReceiveTimingStep = cyclesPerBit * 381;

	mDriveScheduler.SetEvent(1, this, kEventId_DriveReceiveBit, mpEventDriveReceiveBit);
}

void ATDevice1029Full::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
}

void ATDevice1029Full::OnTruncateByte() {
	mReceiveShiftRegister = 1;
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
}

void ATDevice1029Full::OnSendReady() {
}

void ATDevice1029Full::Sync() {
	const uint32 limit = AccumSubCycles();

	mDriveScheduler.SetStopTime(limit);

	for(;;) {
		if (!mCoProc.GetCyclesLeft()) {
			uint32 tc = ATSCHEDULER_GETTIMETONEXT(&mDriveScheduler);
			if (tc <= 0)
				break;

			uint32 tc2 = mCoProc.GetTStatesPending();

			if (!tc2)
				tc2 = 1;

			if (tc > tc2)
				tc = tc2;

			ATSCHEDULER_ADVANCE_N(&mDriveScheduler, tc);
			mCoProc.AddCycles(tc);
		}

		mCoProc.Run();

		if (mCoProc.GetCyclesLeft()) {
			ScheduleImmediateResume();
			break;
		}
	}

	FlushStepNotifications();
}

void ATDevice1029Full::AddCommandEdge(uint32 polarity) {
	mSerialCmdQueue.AddCommandEdge(MasterTimeToDriveTime(), polarity);
}

void ATDevice1029Full::AddTransmitEdge(bool polarity) {
	mSerialXmitQueue.AddTransmitBit(DriveTimeToMasterTime() + mSerialXmitQueue.kTransmitLatency, polarity);
}

bool ATDevice1029Full::OnReadT0() const {
	// T0 = inverted SIO DATA OUT (computer -> peripheral).
	return (mReceiveShiftRegister & 1) == 0;
}

bool ATDevice1029Full::OnReadT1() const {
	return GetDotSignalState().mbCurrentDotSignal;
}

void ATDevice1029Full::OnT1CounterEnabled(bool enabled) {
	if (mbDotCountingEnabled == enabled)
		return;

	mbDotCountingEnabled = enabled;

	UpdateDotEvent();
}

uint8 ATDevice1029Full::OnReadPort(uint8 addr, uint8 output) {
	// P2.0-P2.3:	A8-A11 for program ROM, A8-A9 for XRAM
	// P2.4:		NC
	// P2.5:		/TEST (1 = self-test disabled)
	// P2.6:		SIO COMMAND (0 = asserted)
	// P2.7:		Home sensor (1 = home)
	if (addr == 0) {
		return output;
	} else {
		uint8 v = 0x7F;

		if (!mbCommandState)
			v -= 0x40;

		if (ComputeHeadPosition() < kHomeSensorCycles)
			v += 0x80;

		return v;
	}
}

void ATDevice1029Full::OnWritePort(uint8 addr, uint8 output) {
	// P1.7: /MOT - motor enable
	// P1.6: /PIN - print pin
	// P1.5: /HC - home solenoid control
	// P1.4: /LFPW - line feed power
	// P1.3: Feed motor phase bit 1
	// P1.2: Feed motor phase bit 0
	// P1.1: Lamp
	// P1.0: SIO data in (to computer)
	if (addr == 0) {
		const uint8 delta = mP1 ^ output;

		if (delta) {
			mP1 = output;

			// update SIO data in
			if (delta & 0x01)
				AddTransmitEdge((output & 0x01) != 0);

			// if motor was changed and counting was enabled, restart the event
			if (delta & 0x80) {
				if (mbDotCountingEnabled)
					UpdateDotEvent();
			}

			// check if motor or home solenoid modes are changing
			if (delta & 0xA0) {
				UpdateHeadPosition();
				UpdateDotPosition();
				UpdateHeadSounds();

				// If retracting the head, set event so we can play head stop sound
				if (!(output & 0x20)) {
					const uint32 stopDelay = (ComputeHeadPosition() + 1) >> 1;

					if (stopDelay)
						mDriveScheduler.SetEvent(stopDelay, this, kEventId_DriveHeadRetract, mpEventDriveHeadRetract);
					else
						mDriveScheduler.UnsetEvent(mpEventDriveHeadRetract);
				}

				// When toggling the motor, reset the dot position to align to the
				// current head position; this is a little bit of a cheat but ensures correct
				// positioning. Presumably, in the actual mechanism, this is related to
				// how the gears are engaged by the head solenoid.
				if (delta & 0x80)
					mPrintDotPosition = mPrintHeadPosition;
			}

			// check if pin being fired
			if (delta & ~output & 0x40) {
				mPrinterSoundSource.AddPinSound(ConvertRawTimestamp(mCoProc.GetTime()), 1);

				if (mpPrinterGraphicalOutput) {
					const int dotGroupOffset = ComputeDotPosition(mDriveScheduler.GetTick() + kCyclesPerDotGroup) % kCyclesPerDotGroup;
					const int dotIndex = dotGroupOffset / kCyclesPerDotPulse;

					if (dotIndex < 7) {
						static constexpr sint32 kHorizontalOffsetCycles = 130000;

						// Characters are 5 columns wide with one column in between, and printed at
						// 10 CPI. Therefore, we need to cover one inch per 60 columns.
						static constexpr float kMMPerCycle = 25.4f / 60.0f / (float)kCyclesPerDotGroup;

						sint32 headPos = (sint32)ComputeHeadPosition() - (sint32)kCyclesPerDotPulse * dotIndex - kHorizontalOffsetCycles;

						mpPrinterGraphicalOutput->Print(
							(float)headPos * kMMPerCycle,
							1 << dotIndex
						);
					}
				}
			}

			// check for feed
			if (delta & 0x10) {
				static constexpr uint8 kInitialPhaseLookup[4] {
					0, 1, 3, 2
				};

				if (!(output & 0x10)) {
					// Line feed motor being energized -- set initial phase.
					mFeedPosition = kInitialPhaseLookup[(output & 0x0C) >> 2];
				}
			} else if ((delta & 0x0C) && !(output & 0x10)) {
				// forward phase sequence: 00, 04, 0C, 08
				static constexpr sint8 kPhaseLookup[4][4] {
					{  0, +1, -1,  0 },
					{ -1,  0,  0, +1 },
					{  0, -1, +1,  0 },
					{ +1,  0,  0, -1 },
				};

				const sint8 step = kPhaseLookup[mFeedPosition][(output & 0x0C) >> 2];

				if (step) {
					mFeedPosition = (mFeedPosition + step) & 3;

					if (mpPrinterGraphicalOutput) {
						// The 1029 steps by 16 phases for 9 LPI and 24 phases for 6 LPI.
						// The 9 LPI mode in particular is used by graphics mode, so it should align
						// lines back-to-back.
						mpPrinterGraphicalOutput->FeedPaper(step * kVerticalDotPitchMM * 7.0f / 16.0f);
					}
				}
			}
		}
	} else {
		mP2 = output;
	}
}

uint8 ATDevice1029Full::OnReadXRAM(uint8 addr) const {
	return mXRAM[addr + ((mP2 & 3) << 8)] | 0xF0;
}

void ATDevice1029Full::OnWriteXRAM(uint8 addr, uint8 v) {
	mXRAM[addr + ((mP2 & 3) << 8)] = v | 0xF0;
}

void ATDevice1029Full::UpdateHeadPosition() {
	const uint32 t = mDriveScheduler.GetTick();
	const uint32 pos = ComputeHeadPosition(t);
	mPrintHeadPosition = pos;
	mLastHeadUpdateTime = t;
}

uint32 ATDevice1029Full::ComputeHeadPosition() const {
	return ComputeHeadPosition(mDriveScheduler.GetTick());
}

uint32 ATDevice1029Full::ComputeHeadPosition(uint32 t) const {
	uint32 dt = t - mLastHeadUpdateTime;

	// check if home solenoid and/or motors are on
	if (!(mP1 & 0x20)) {
		// head being retracted, motor disengaged
		dt *= 2;
		return std::max<uint32>(mPrintHeadPosition, dt) - dt;
	} else if (!(mP1 & 0x80)) {
		// head engaged to motor, motor on
		return mPrintHeadPosition + dt;
	} else {
		// head engaged to motor, motor off
		return mPrintHeadPosition;
	}
}

void ATDevice1029Full::UpdateDotPosition() {
	const uint32 t = mDriveScheduler.GetTick();
	const uint32 pos = ComputeDotPosition(t);
	mPrintDotPosition = pos;
	mLastDotUpdateTime = t;
}

uint32 ATDevice1029Full::ComputeDotPosition() const {
	return ComputeDotPosition(mDriveScheduler.GetTick());
}

uint32 ATDevice1029Full::ComputeDotPosition(uint32 t) const {
	uint32 dt = t - mLastDotUpdateTime;

	// check if motor is on
	if (!(mP1 & 0x80)) {
		// head engaged to motor, motor on
		return mPrintDotPosition + dt;
	} else {
		// head engaged to motor, motor off
		return mPrintDotPosition;
	}
}

void ATDevice1029Full::UpdateDotEvent() {
	if (mbDotCountingEnabled && !(mP1 & 0x80))
		mDriveScheduler.SetEvent(GetDotSignalState().mDriveTicksToNextFallingEdge, this, kEventId_DriveDotFallingEdge, mpEventDriveDotFallingEdge);
	else
		mDriveScheduler.UnsetEvent(mpEventDriveDotFallingEdge);
}

ATDevice1029Full::DotSignalState ATDevice1029Full::GetDotSignalState() const {
	// T1 reads the /DOT signal from the dot timing wheel. The basic mechanism
	// is shared with other printers, and in particular described in the service
	// manuals for the CBM MPS-801 and Amstrad DMP1, which use a similar
	// Seikosha Uni-Hammer mech.
	//
	// The mechanism sends a dot clock to the microcontroller so it can time
	// the hammer as bumps move downward on the platen. This allows one hammer
	// to punch all 7 dots in a column sequentially. The hammer is angled
	// slightly to counter the head movement during this process. A periodic
	// pause in the dot clock lets the MCU synchronize to column alignment.
	//
	// The 1029's mechanism does differ from the MPS-801 and DMP1s' in a couple
	// of important ways:
	//
	// - The DOT signal is inverted. Pulses are active high on the 1029, due
	//   to an extra inverter.
	//
	// - The 1029 has the ~0.9ms pause between every column, not every fourth.
	//   The two extra dot pulses between columns doesn't exist as the MCU
	//   expects to be able to emit dots for each column back-to-back. Thus,
	//   there are only 7 pulses between gaps instead of 34.
	//
	// It is not entirely clear how exactly the dot sensor plate and platen
	// rotation relate to the head movement. The firmware definitely expects
	// dot pulses to arrive when the motor is spinning even if it is disengaged,
	// but the way it synchronizes timing also implies that the head movement
	// starts on a particular offset with the dot sensor plate, or else lines
	// are slightly misaligned relative to each other.

	const uint32 headPos = ComputeDotPosition();
	const uint32 groupOffset = headPos % kCyclesPerDotGroup;
	if (groupOffset >= kCyclesPerDotPulse * 6 + kCyclesPerDotPulseLow) {
		return DotSignalState {
			.mDriveTicksToNextFallingEdge = kCyclesPerDotGroup - groupOffset,
			.mbCurrentDotSignal = false
		};
	}

	const uint32 pulseOffset = groupOffset % kCyclesPerDotPulse;

	return DotSignalState {
		.mDriveTicksToNextFallingEdge = kCyclesPerDotPulse - pulseOffset,
		.mbCurrentDotSignal = pulseOffset < kCyclesPerDotPulseLow
	};
}

void ATDevice1029Full::UpdateHeadSounds() {
	// Enable platen if head is moving to the right
	const bool homeSolenoidEngaged = !(mP1 & 0x20);
	const bool motorEnabled = !(mP1 & 0x80);

	mPrinterSoundSource.EnablePlatenSound(!homeSolenoidEngaged && motorEnabled);
	mPrinterSoundSource.EnableRetractSound(homeSolenoidEngaged && ComputeHeadPosition() > 0);
}
