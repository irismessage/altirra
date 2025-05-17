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
#include <at/atcpu/memorymap.h>
#include "1025full.h"
#include "debuggerlog.h"
#include "firmwaremanager.h"
#include "memorymanager.h"

void ATCreateDevice1025Full(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevice1025Full> p(new ATDevice1025Full);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefPrinter1025Full = { "1025full", nullptr, L"1025 80-Column Printer (full emulation)", ATCreateDevice1025Full };

/////////////////////////////////////////////////////////////////////////////

ATDevice1025Full::TargetProxy::TargetProxy(ATDevice1025Full& parent)
	: mParent(parent)
{
}

std::pair<const uintptr *, const uintptr *> ATDevice1025Full::TargetProxy::GetReadWriteMaps() const {
	return { mParent.mDebugReadMap, mParent.mDebugWriteMap };
}

void ATDevice1025Full::TargetProxy::SetHistoryBuffer(ATCPUHistoryEntry *harray) {
	mParent.mCoProc.SetHistoryBuffer(harray);
}

uint32 ATDevice1025Full::TargetProxy::GetHistoryCounter() const {
	return mParent.mCoProc.GetHistoryCounter();
}
	
uint32 ATDevice1025Full::TargetProxy::GetTime() const {
	return mParent.mCoProc.GetTime();
}

uint32 ATDevice1025Full::TargetProxy::GetStepStackLevel() const {
	return mParent.mCoProc.GetStepStackLevel();
}

void ATDevice1025Full::TargetProxy::GetExecState(ATCPUExecState& state) const {
	mParent.mCoProc.GetExecState(state);
}

void ATDevice1025Full::TargetProxy::SetExecState(const ATCPUExecState& state) {
	mParent.mCoProc.SetExecState(state);
}

/////////////////////////////////////////////////////////////////////////////

ATDevice1025Full::ATDevice1025Full()
	: mTargetProxy(*this)
{
	mBreakpointsImpl.BindBPHandler(mCoProc);
	mBreakpointsImpl.SetStepHandler(this);

	mCoProc.SetProgramROM(mXROM);

	// The 8051 in the 1025 runs T-states at 7.37MHz with 12 T-states per
	// machine cycle, with the majority of instructions only 1-4 machine
	// cycles. To keep accounting simple we run the drive coprocessor clock
	// at 7.37MHz / 12, but need to report 7.37MHz as the displayed clock
	// speed.
	InitTargetControl(mTargetProxy, 7370000.0 / 12.0, kATDebugDisasmMode_8051, &mBreakpointsImpl, this);
	ApplyDisplayCPUClockMultiplier(12.0);
	
	mSerialCmdQueue.SetOnDriveCommandStateChanged(
		[this](bool bit) {
			if (bit)
				mCoProc.AssertIrq();
			else
				mCoProc.NegateIrq();
		}
	);
}

ATDevice1025Full::~ATDevice1025Full() {
}

void *ATDevice1025Full::AsInterface(uint32 iid) {
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

void ATDevice1025Full::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPrinter1025Full;
}

void ATDevice1025Full::Init() {
	mDriveScheduler.SetRate(VDFraction(7370000, 12));

	// We need to do this early to ensure that the clock divisor is set before we perform init processing.
	ResetTargetControl();

	mSerialXmitQueue.Init(mpScheduler, mpSIOMgr);
	mSerialCmdQueue.Init(&mDriveScheduler, mpSIOMgr);

	mCoProc.SetPortReadHandler([this](uint8 port, uint8 output) -> uint8 { return OnReadPort(port, output); });
	mCoProc.SetPortWriteHandler([this](uint8 port, uint8 data) { OnWritePort(port, data); });
	mCoProc.SetXRAMReadHandler([this](uint16 addr) { return OnReadXRAM(addr); });
	mCoProc.SetXRAMWriteHandler([this](uint16 addr, uint8 v) { return OnWriteXRAM(addr, v); });
	mCoProc.SetT0ReadHandler([this]() { return OnReadT0(); });
	mCoProc.SetT1ReadHandler([this]() { return OnReadT1(); });
	mCoProc.SetSerialXmitHandler([this](uint8 v) { OnSerialXmit(v); });

	// Unlike the other coprocessor types, we don't actually need read/write maps. This just
	// makes our life easier as we can leverage the default impl.
	ATCoProcMemoryMapView mmap(mDebugReadMap, mDebugWriteMap);

	mmap.Clear(mDummyRead, mDummyWrite);
	mmap.SetReadMem(0x00, 0x10, mXROM);
	mmap.SetMemory(0x10, 0x01, mCoProc.GetInternalRAM());

	mPrinterSoundSource.Init(*GetService<IATAudioMixer>(), *GetService<IATDeviceSchedulingService>()->GetMachineScheduler(), "1025");

	ATPrinterGraphicsSpec spec {};
	spec.mPageWidthMM = 215.9f;				// 8.5" wide paper
	spec.mPageVBorderMM = 8.0f;				// vertical border
	spec.mDotRadiusMM = 0.28f;				// guess for dot radius
	spec.mVerticalDotPitchMM = 0.44704f;	// 0.0176" vertical pitch
	spec.mbBit0Top = true;
	spec.mNumPins = 7;

	mpPrinterGraphicalOutput = GetService<IATPrinterOutputManager>()->CreatePrinterGraphicalOutput(spec);
}

void ATDevice1025Full::Shutdown() {
	mpPrinterGraphicalOutput = nullptr;

	mDriveScheduler.UnsetEvent(mpEventDriveReceiveByte);
	mDriveScheduler.UnsetEvent(mpEventDriveSentByte);

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

void ATDevice1025Full::WarmReset() {
	// If the computer resets, its transmission is interrupted.
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveByte);
}

void ATDevice1025Full::ComputerColdReset() {
	WarmReset();
}

void ATDevice1025Full::PeripheralColdReset() {
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveByte);
	mDriveScheduler.UnsetEvent(mpEventDriveSentByte);

	mSerialXmitQueue.Reset();
	mSerialCmdQueue.Reset();
	
	mbDirectReceiveOutput = true;
	mbDirectTransmitOutput = true;

	OnWritePort(0, 0xFF);
	OnWritePort(1, 0xFF);

	mCoProc.ColdReset();

	ResetTargetControl();

	WarmReset();
}

void ATDevice1025Full::InitFirmware(ATFirmwareManager *fwman) {
	mpFwMgr = fwman;

	ReloadFirmware();
}

bool ATDevice1025Full::ReloadFirmware() {
	uint32 len = 0;
	const vduint128 oldXHash = VDHash128(mXROM, sizeof mXROM);

	memset(mXROM, 0, sizeof mXROM);

	bool xromUsable = false;
	mpFwMgr->LoadFirmware(mpFwMgr->GetFirmwareOfType(kATFirmwareType_1025, true), mXROM, 0, 0x1000, nullptr, &len, nullptr, nullptr, &xromUsable);

	// replicate 4K to 64K
	for(uint32 offset = 0x1000; offset < 0x10000; offset += 0x1000)
		memcpy(&mXROM[offset], &mXROM[0], 0x1000);

	mXROM[0x10000] = mXROM[0];
	mXROM[0x10001] = mXROM[1];
	mXROM[0x10002] = mXROM[2];
	mXROM[0x10003] = mXROM[3];


	const vduint128 newXHash = VDHash128(mXROM, sizeof mXROM);

	mbFirmwareUsable = xromUsable;

	return oldXHash != newXHash;
}

const wchar_t *ATDevice1025Full::GetWritableFirmwareDesc(uint32 idx) const {
	return nullptr;
}

bool ATDevice1025Full::IsWritableFirmwareDirty(uint32 idx) const {
	return false;
}

void ATDevice1025Full::SaveWritableFirmware(uint32 idx, IVDStream& stream) {
}

ATDeviceFirmwareStatus ATDevice1025Full::GetFirmwareStatus() const {
	return mbFirmwareUsable ? ATDeviceFirmwareStatus::OK : ATDeviceFirmwareStatus::Missing;
}

void ATDevice1025Full::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

void ATDevice1025Full::OnScheduledEvent(uint32 id) {
	if (id == kEventId_DriveReceiveByte) {
		mpEventDriveReceiveByte = nullptr;

		mCoProc.ReceiveSerialByte(mSerialPendingReceiveByte);
	} else if (id == kEventId_DriveSentByte) {
		mpEventDriveSentByte = nullptr;

		mCoProc.SendSerialByteDone();
	} else
		return ATDiskDriveDebugTargetControl::OnScheduledEvent(id);
}

void ATDevice1025Full::OnCommandStateChanged(bool asserted) {
	if (mbCommandState != asserted) {
		mbCommandState = asserted;

		AddCommandEdge(asserted);
	}
}

void ATDevice1025Full::OnMotorStateChanged(bool asserted) {
}

void ATDevice1025Full::OnBeginReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
}

void ATDevice1025Full::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	// If byte is not within +/-5% of 19200 baud, discard it
	if (!cyclesPerBit)
		return;

	if (cyclesPerBit < 87 || cyclesPerBit > 97)
		return;

	Sync();

	mSerialPendingReceiveByte = c;
	mDriveScheduler.SetEvent(1, this, kEventId_DriveReceiveByte, mpEventDriveReceiveByte);
}

void ATDevice1025Full::OnTruncateByte() {
}

void ATDevice1025Full::OnSendReady() {
}

void ATDevice1025Full::Sync() {
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

void ATDevice1025Full::AddCommandEdge(uint32 polarity) {
	mSerialCmdQueue.AddCommandEdge(MasterTimeToDriveTime(), polarity);
}

bool ATDevice1025Full::OnReadT0() const {
	// T0 = SIO COMMAND (active low)
	return !mpSIOMgr->IsSIOCommandAsserted();
}

bool ATDevice1025Full::OnReadT1() const {
	// T1 = /PE (paper end); true = have paper
	return true;
}

uint8 ATDevice1025Full::OnReadPort(uint8 addr, uint8 output) {
	if (addr == 0)
		return mP0;
	else
		return mP1;
}

void ATDevice1025Full::OnWritePort(uint8 addr, uint8 output) {
	if (addr == 0) {
	} else {
		const uint8 delta = mP1 ^ output;

		if (delta) {
			mP1 = output;

			uint32 t = mDriveScheduler.GetTick();
			const uint8 pinsDown = delta & ~output & 0x7F;

			if (pinsDown) {
				int numPinsDown = 0;

				for(uint8 t = pinsDown; t; t &= t - 1)
					++numPinsDown;

				mPrinterSoundSource.AddPinSound(ConvertRawTimestamp(mCoProc.GetTime()), numPinsDown);

				if (mpPrinterGraphicalOutput) {
					// 1025 Service Manual p.1-3 specifies a step of 2.54mm (1/10") for every
					// 4 steps (30d). However, the firmware does half-steps in 16.5 cpi mode.
					//
					// Worse yet, the firmware times the print head to locate columns at fractional
					// steps, so we must interpolate the print head position.
					static constexpr float kHalfStepOffset = 0.697f;

					uint32 timeSinceStep = t - mLastHeadStepTime;

					float x = (float)(mPrintHeadPosition >> 1) + ((mPrintHeadPosition & 1) ? kHalfStepOffset : 0);

					if (timeSinceStep < mLastHeadStepPeriod) {
						float frac = (float)timeSinceStep / (float)mLastHeadStepPeriod;

						int pos0 = mPrintHeadPosition - mLastHeadStepDistance;
						float x0 = (float)(pos0 >> 1) + (pos0 & 1 ? kHalfStepOffset : 0);

						x = x0 + (x - x0) * frac;
					}

					static constexpr float kMMPerStep = (2.54f / 4.0f);
					static constexpr float kLeftMarginMM = 2.5f;

					mpPrinterGraphicalOutput->Print(
						kLeftMarginMM + x * kMMPerStep,
						pinsDown
					);
				}
			}
		}
	}
}

uint8 ATDevice1025Full::OnReadXRAM(uint16 addr) const {
	// P2.7 connected to /CE, so 8155 only responds to A15=0.
	if (addr & 0x8000)
		return 0xFF;

	// P2.6 connected to IO / /M
	if (!(addr & 0x4000))
		return m8155RAM[addr & 0xFF];

	// An I/O address is being accessed -- only A2-A0 have meaning.
	//	0 - command/status register
	//	1 - Port A
	//	2 - Port B
	//	3 - Port C
	//	4 - Timer low
	//	5 - Timer high / mode

	switch(addr & 7) {
		case 1:
			return m8155PortA;

		case 2:
			return m8155PortB;

		case 3:	// Port C
			// PC0 - HP (Home Position)
			// PC1 - SEL (Print Switch)
			// PC2 - Pulled up
			// PC3 - SP1 jumper
			// PC4 - Pulled up
			// PC5 - Pulled up
			return 0b0'1111'1110 + (mPrintHeadPosition == 0 ? 1 : 0);

		default:
			return 0xFF;
	}
}

void ATDevice1025Full::OnWriteXRAM(uint16 addr, uint8 v) {
	if (addr & 0x8000)
		return;

	if (!(addr & 0x4000)) {
		m8155RAM[addr & 0xFF] = v;
		return;
	}

	switch(addr & 7) {
		case 1:
			if (m8155PortA != v) {
				m8155PortA = v;

				const uint32 t = mDriveScheduler.GetTick();

				if (!(v & 1)) {
					static constexpr sint8 kSpaceHeadOffset[16] {
						-1,
						-1,
						-1,
						 4,		// 0011 -> 4
						-1,
						-1,
						 2,		// 0110 -> 2
						 3,		// 0111 -> 3
						-1,
						 6,		// 1001 -> 6
						-1,
						 5,		// 1011 -> 5
						 0,		// 1100 -> 0
						 7,		// 1101 -> 7
						 1,		// 1110 -> 1
						-1,
					};

					sint8 phase = kSpaceHeadOffset[v >> 4];

					if (phase >= 0) {
						uint8 delta = ((uint8)phase - mPrintHeadPosition) & 7;

						switch(delta) {
							case 6:
								if (mPrintHeadPosition > 0)
									--mPrintHeadPosition;
								[[fallthrough]];
							case 7:
								if (mPrintHeadPosition > 0)
									--mPrintHeadPosition;
								break;
							case 2:
								++mPrintHeadPosition;
								[[fallthrough]];
							case 1:
								++mPrintHeadPosition;
								break;
							default:
								break;
						}

						mLastHeadStepTime = t;

						// The 1025 firmware depends on being able to interpolate dots by timing the print head while
						// it is moving. It uses a periods as follows:
						//
						//	- 4120 cycles when double phase stepping to return home
						//	- 4060 cycles when double stepping at 5/10 cpi
						//	- 2040 cycles when stepping to a double phase at 16.5 cpi
						//	- 4700 cycles when stepping to a single phase at 16.5 cpi
						//	- 1350 cycles when printing columns at 10/16.5 cpi
						//	- 2700 cycles when printing columns at 5 cpi

						mLastHeadStepPeriod = 4060;

						if (delta == 7 || delta == 1) {
							if (mPrintHeadPosition & 1)
								mLastHeadStepPeriod = 4700;
							else
								mLastHeadStepPeriod = 2040;
						}

						mLastHeadStepDistance = delta > 4 ? (int)delta - 8 : (int)delta;
					}
				}
			}
			break;

		case 2:
			if (m8155PortB != v) {
				m8155PortB = v;

				if (!(v & 1)) {
					static constexpr sint8 kFeedOffset[16] {
						-1, -1, -1,  2,		// 0011 -> 2
						-1, -1,  1, -1,		// 0110 -> 1
						-1,  3, -1, -1,		// 1001 -> 3
						 0, -1, -1, -1,		// 1100 -> 0
					};

					sint8 phase = kFeedOffset[v >> 4];

					if (phase >= 0) {
						uint8 delta = ((uint8)phase - mFeedPosition) & 3;

						if (delta == 3) {
							--mFeedPosition;
						} else if (delta == 1) {
							++mFeedPosition;

							if (mpPrinterGraphicalOutput) {
								// 1025 Service Manual p.1-5 says that the paper advances 4.33 mm (1/6")
								// every time the pulse motor advances 24 steps (180d). This is a convenient
								// spec.
								mpPrinterGraphicalOutput->FeedPaper(4.33f / 24.0f);
							}

							mPrinterSoundSource.ScheduleSound(kATAudioSampleId_Printer1025Feed, false, 0, 0, 1.0f);
						}
					}
				}
			}
			break;

		default:
			break;
	}
}

void ATDevice1025Full::OnSerialXmit(uint8 v) {
	mSerialXmitQueue.AddTransmitByte(DriveTimeToMasterTime() + mSerialXmitQueue.kTransmitLatency, v, 93);

	mDriveScheduler.SetEvent(930, this, kEventId_DriveSentByte, mpEventDriveSentByte);
}
