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

#include "stdafx.h"
#include <chrono>
#include <array>
#include <at/atcore/devicepbi.h>
#include <at/atcore/logging.h>
#include <at/atcore/randomization.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/sioutils.h>
#include <at/atcpu/co8048.h>
#include <at/atcpu/memorymap.h>
#include "1450xld.h"
#include "fdc.h"
#include "firmwaremanager.h"
#include "memorymanager.h"
#include "simulator.h"
#include "diskinterface.h"
#include "diskdrivefullbase.h"

extern ATSimulator g_sim;

ATLogChannel g_ATLC1450XLDisk(false, false, "1450XLDISK", "1450XLD Parallel Disk Interface");
ATLogChannel g_ATLC1450XLDiskIO(false, false, "1450XLDISKIO", "1450XLD Parallel Disk Interface I/O");

///////////////////////////////////////////////////////////////////////////////

extern constexpr ATDeviceDefinition g_ATDeviceDef1450XLDiskController {
	"1450xldisk",
	nullptr,
	L"1450XLD Disk Controller",
	[](const ATPropertySet& pset, IATDevice **dev) {
		*dev = vdrefptr(new AT1450XLDiskDevice(false, false)).release();
	}
};

extern constexpr ATDeviceDefinition g_ATDeviceDef1450XLDiskControllerFull {
	"1450xldiskfull",
	nullptr,
	L"1450XLD Disk Controller (full emulation)",
	[](const ATPropertySet& pset, IATDevice **dev) {
		*dev = vdrefptr(new AT1450XLDiskDevice(true, false)).release();
	}
};

extern constexpr ATDeviceDefinition g_ATDeviceDef1450XLTONGDiskControllerFull {
	"1450xltongdiskfull",
	nullptr,
	L"1450XLD \"TONG\" Disk Controller (full emulation)",
	[](const ATPropertySet& pset, IATDevice **dev) {
		*dev = vdrefptr(new AT1450XLDiskDevice(true, true)).release();
	}
};

class AT1450XLDiskDevice::FullEmulation final : public ATDiskDriveDebugTargetControl, public IATDiskDriveDebugTargetProxy {
public:
	FullEmulation(AT1450XLDiskDevice& parent, bool isTONG) : mParent(parent), mbIsTONG(isTONG) {}

	void *AsInterface(uint32 iid) {
		if (iid == ATFDCEmulator::kTypeID)
			return &mFDC;

		return ATDiskDriveDebugTargetControl::AsInterface(iid);
	}

	void Init();

	ATScheduler& GetDriveScheduler() { return mDriveScheduler; }

public:
	std::pair<const uintptr *, const uintptr *> GetReadWriteMaps() const override;
	void SetHistoryBuffer(ATCPUHistoryEntry *harray) override;
	uint32 GetHistoryCounter() const override;
	uint32 GetTime() const override;
	uint32 GetStepStackLevel() const override;

	using ATDiskDriveDebugTargetControl::GetExecState;
	void GetExecState(ATCPUExecState& state) const override;
	void SetExecState(const ATCPUExecState& state) override;

	void Sync() override;

public:
	bool OnReadT0() const;
	bool OnReadT1() const;
	uint8 OnReadPort(uint8 addr, uint8 output);
	void OnWritePort(uint8 addr, uint8 output);
	uint8 OnReadXRAM(uint8 addr);
	void OnWriteXRAM(uint8 addr, uint8 val);
	uint8 OnReadBus();

	AT1450XLDiskDevice& mParent;
	ATDeviceFirmwareStatus mControllerFirmwareStatus = ATDeviceFirmwareStatus::Invalid;
	const bool mbIsTONG = false;
	bool mbInited = false;

	uint8 mPorts[2] {};
	uint8 mWriteLatch {};

	ATCoProc8048 mCoProc;
	ATFDCEmulator mFDC;

	uint8 mControllerRAM[256] {};
	uint8 mControllerFirmware[4096] {};

	ATDebugTargetBreakpointsImplT<0x1000> mBreakpointsImpl;

	uintptr mDebugReadMap[256] {};
	uintptr mDebugWriteMap[256] {};
	uint8 mDummyRead[256] {};
	uint8 mDummyWrite[256] {};
};

void AT1450XLDiskDevice::FullEmulation::Init() {
	mbInited = true;

	ATCoProcMemoryMapView mmap(mDebugReadMap, mDebugWriteMap);
	mmap.Clear(mDummyRead, mDummyWrite);
	mmap.SetReadMem(0x00, 0x10, mControllerFirmware);
	mmap.SetMemory(0x10, 0x01, mCoProc.GetInternalRAM());
	mmap.SetMemory(0x11, 0x01, mControllerRAM);

	mBreakpointsImpl.BindBPHandler(mCoProc);
	mBreakpointsImpl.SetStepHandler(this);

	// The 8040 is clocked at 10MHz, but it runs 15 T-states to each machine cycle, so
	// we run the scheduler at 10MHz/15 = 667KHz.
	mDriveScheduler.SetRate(VDFraction(10000000, 15));

	InitTargetControl(*this, 10'000'000.0f / 15.0f, kATDebugDisasmMode_8048, &mBreakpointsImpl, &mParent);
	ApplyDisplayCPUClockMultiplier(15.0);

	ResetTargetControl();

	mFDC.Init(&mDriveScheduler, 288.0f, 1.0f, ATFDCEmulator::kType_2797);
	mFDC.SetAutoIndexPulse(true);
	mFDC.SetDDBootSectorMode(ATFDCEmulator::DDBootSectorMode::Swapped);
	mFDC.SetSideMapping(ATFDCEmulator::SideMapping::Side2ReversedTracks, 40);
	mFDC.SetDiskInterface(mParent.mpDiskInterface);
	mFDC.SetOnDrqChange(
		[this](bool drq) {
			if (drq)
				mCoProc.NegateIrq();
			else
				mCoProc.AssertIrq();
		}
	);
	mFDC.SetOnIrqChange([this](bool irq) {  });
	mFDC.SetOnStep(std::bind_front(&AT1450XLDiskDevice::OnFDCStep, &mParent));

	mCoProc.SetProgramBanks(mControllerFirmware, mControllerFirmware + 2048);
	mCoProc.SetPortReadHandler([this](uint8 port, uint8 output) -> uint8 { return OnReadPort(port, output); });
	mCoProc.SetPortWriteHandler([this](uint8 port, uint8 data) { OnWritePort(port, data); });
	mCoProc.SetXRAMReadHandler([this](uint8 addr) -> uint8 { return OnReadXRAM(addr); });
	mCoProc.SetXRAMWriteHandler([this](uint8 addr, uint8 data) { OnWriteXRAM(addr, data); });
	mCoProc.SetT0ReadHandler([this]() { return OnReadT0(); });
	mCoProc.SetT1ReadHandler([this]() { return OnReadT1(); });
	mCoProc.SetBusReadHandler([this]() { return OnReadBus(); });
}

std::pair<const uintptr *, const uintptr *> AT1450XLDiskDevice::FullEmulation::GetReadWriteMaps() const {
	return { mDebugReadMap, mDebugWriteMap };
}

void AT1450XLDiskDevice::FullEmulation::SetHistoryBuffer(ATCPUHistoryEntry *harray) {
	mCoProc.SetHistoryBuffer(harray);
}

uint32 AT1450XLDiskDevice::FullEmulation::GetHistoryCounter() const {
	return mCoProc.GetHistoryCounter();
}
	
uint32 AT1450XLDiskDevice::FullEmulation::GetTime() const {
	return mCoProc.GetTime();
}

uint32 AT1450XLDiskDevice::FullEmulation::GetStepStackLevel() const {
	return mCoProc.GetStepStackLevel();
}

void AT1450XLDiskDevice::FullEmulation::GetExecState(ATCPUExecState& state) const {
	mCoProc.GetExecState(state);
}

void AT1450XLDiskDevice::FullEmulation::SetExecState(const ATCPUExecState& state) {
	mCoProc.SetExecState(state);
}

void AT1450XLDiskDevice::FullEmulation::Sync() {
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

bool AT1450XLDiskDevice::FullEmulation::OnReadT0() const {
	return mParent.mbControllerBusy;
}

bool AT1450XLDiskDevice::FullEmulation::OnReadT1() const {
	return mParent.mbAttn;
}

uint8 AT1450XLDiskDevice::FullEmulation::OnReadPort(uint8 addr, uint8 output) {
	addr &= 1;

	return mPorts[addr];
}

void AT1450XLDiskDevice::FullEmulation::OnWritePort(uint8 addr, uint8 output) {
	addr &= 1;
	if (mPorts[addr] == output)
		return;

	mPorts[addr] = output;

	if (addr == 0) {
		// P1.4 -> /DDEN
		mFDC.SetDensity(!(output & 0x10));

		// P1.7 -> /clear on busy flip-flop
		if (!(output & 0x80))
			mParent.mbControllerBusy = false;
	} else {
		// P2.6/7 -> drive select
		if (mbIsTONG)
			mParent.SetSelectedDrive(output >> 6);

		// P2.4 -> motor enable (1 = on)
		mParent.OnMotorChange();
		mFDC.SetMotorRunning((output & 0x10) != 0);
	}
}

uint8 AT1450XLDiskDevice::FullEmulation::OnReadXRAM(uint8 addr) {
	if (mbIsTONG) {
		// P1:
		// -11-----  RAM
		// -01-----  FDC
		// -00-----  Latch

		if (mPorts[0] & 0x20) {
			if (mPorts[0] & 0x40) {
				return mControllerRAM[addr];
			} else {
				// P1.0 and P1.1 select FDC register.
				return mFDC.ReadByte(mPorts[0] & 3);
			}
		} else {
			return mWriteLatch;
		}
	} else {
		// P1.3=1 enables RAM or FDC.
		if (mPorts[0] & 0x08) {
			// P1.5=1 selects RAM instead of FDC.
			if (mPorts[0] & 0x20) {
				// P1.6=1 selects RAM I/O instead of RAM memory.
				return mControllerRAM[addr];
			} else {
				// P1.0 and P1.1 select FDC register.
				return mFDC.ReadByte(mPorts[0] & 3);
			}
		} else {
			if (mPorts[1] & 0x20)
				return mWriteLatch;
			else {
				// If no output enables are set, read bus capacitance. The TONG
				// controller relies on this in its POST.
				return mParent.mReadLatch;
			}
		}
	}
}

void AT1450XLDiskDevice::FullEmulation::OnWriteXRAM(uint8 addr, uint8 val) {
	if (mbIsTONG) {
		if (mPorts[0] & 0x20) {
			if (mPorts[0] & 0x40) {
				// P1.6=1 selects RAM I/O instead of RAM memory.
				mControllerRAM[addr] = val;
			} else {
				// P1.0 and P1.1 select FDC register.
				mFDC.WriteByte(mPorts[0] & 3, val);
			}
		}
	} else {
		// P1.3=1 enables RAM or FDC.
		if (mPorts[0] & 0x08) {
			// P1.5=1 selects RAM instead of FDC.
			if (mPorts[0] & 0x20) {
				// P1.6=1 selects RAM I/O instead of RAM memory.
				mControllerRAM[addr] = val;
			} else {
				// P1.0 and P1.1 select FDC register.
				mFDC.WriteByte(mPorts[0] & 3, val);
			}
		}
	}

	// any write at all also clocks the read latch.
	mParent.mReadLatch = val;
}

uint8 AT1450XLDiskDevice::FullEmulation::OnReadBus() {
	if (mbIsTONG) {
		if (mPorts[0] & 0x20) {
			// P1.5=1 selects RAM instead of FDC.
			if (mPorts[0] & 0x40) {
				return 0xFF;
			} else {
				// P1.0 and P1.1 select FDC register.
				return mFDC.ReadByte(mPorts[0] & 3);
			}
		} else {
			return mWriteLatch;
		}
	} else {
		// P1.3=1 enables RAM or FDC.
		if (mPorts[0] & 0x08) {
			// P1.5=1 selects RAM instead of FDC.
			if (mPorts[0] & 0x20) {
				return 0xFF;
			} else {
				// P1.0 and P1.1 select FDC register.
				return mFDC.ReadByte(mPorts[0] & 3);
			}
		} else {
			if (mPorts[1] & 0x20)
				return mWriteLatch;
			else {
				// If no output enables are set, read bus capacitance. The TONG
				// controller relies on this in its POST.
				return mParent.mReadLatch;
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////

AT1450XLDiskDevice::AT1450XLDiskDevice(bool full, bool tong)
	: mbIsTONG(tong)
{
	if (full)
		mpFullEmulation = new FullEmulation(*this, tong);
}

void *AT1450XLDiskDevice::AsInterface(uint32 iid) {
	if (mpFullEmulation) {
		auto *p = mpFullEmulation->AsInterface(iid);

		if (p)
			return p;
	}

	if (iid == AT1450XLDiskDevice::kTypeID)
		return this;

	if (iid == IATDeviceAudioOutput::kTypeID)
		return static_cast<IATDeviceAudioOutput *>(&mAudioPlayer);

	return ATDeviceT::AsInterface(iid);
}

void AT1450XLDiskDevice::GetDeviceInfo(ATDeviceInfo& info) {
	if (mbIsTONG)
		info.mpDef = &g_ATDeviceDef1450XLTONGDiskControllerFull;
	else
		info.mpDef = mpFullEmulation ? &g_ATDeviceDef1450XLDiskControllerFull : &g_ATDeviceDef1450XLDiskController;
}

void AT1450XLDiskDevice::Init() {
	mpScheduler = GetService<IATDeviceSchedulingService>()->GetMachineScheduler();
	mpSlowScheduler = GetService<IATDeviceSchedulingService>()->GetSlowScheduler();

	mpPBIMgr = GetService<IATDevicePBIManager>();
	mpPBIMgr->AddDevice(this);

	mpDiskInterface = &g_sim.GetDiskInterface(0);
	mpDiskInterface->AddClient(this);

	if (mpFullEmulation) {
		mpFullEmulation->Init();

		mDiskChangeState = 0;
		OnDiskChanged(false);
		OnWriteModeChanged();
		OnTimingModeChanged();
		OnAudioModeChanged();
	}
}

void AT1450XLDiskDevice::Shutdown() {
	if (mpFullEmulation)
		mpFullEmulation->ShutdownTargetControl();

	if (mpDiskInterface) {
		mpDiskInterface->SetShowMotorActive(false);
		mpDiskInterface->SetShowActivity(false, 0);
		mpDiskInterface->RemoveClient(this);
		mpDiskInterface = nullptr;
	}

	mAudioPlayer.Shutdown();
	
	if (mpSlowScheduler) {
		ClearMotorTimeout();
		ClearReceiveTimeout();
		mpSlowScheduler->UnsetEvent(mpSlowEventOpDelay);
		mpSlowScheduler = nullptr;
	}

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpEventTransfer);
		mpScheduler = nullptr;
	}

	if (mpPBIMgr) {
		mpPBIMgr->RemoveDevice(this);
		mpPBIMgr = nullptr;
	}

	if (mpMemMgr) {
		mpMemMgr->DeleteLayerPtr(&mpMemLayerHardware);
		mpMemMgr->DeleteLayerPtr(&mpMemLayerFirmware);
		mpMemMgr = nullptr;
	}
}

void AT1450XLDiskDevice::ColdReset() {
	mCurrentTrack = 0;
	mReadLatch = 0;
	mWeakBitLFSR = g_ATRandomizationSeeds.mDiskStartPos;

	if (mpFullEmulation) {
		mCurrentTrack = 20;
		mpFullEmulation->mCoProc.ColdReset();
	}

	WarmReset();
}

void AT1450XLDiskDevice::WarmReset() {
	mDriveStatus = 0;
	mFDCStatus = 0xFF;

	if (mpFullEmulation) {
		mpFullEmulation->mCoProc.WarmReset();
		mpFullEmulation->mFDC.Reset();
		mpFullEmulation->ResetTargetControl();
	} else {
		// 2.3s
		SetOperationDelay(4116477);
	}

	mbControllerBusy = false;
	mbControllerBoot = true;
	mControllerState = ControllerState::Post;
}

void AT1450XLDiskDevice::SetDiskAttn(bool attn) {
	if (mbAttn == attn)
		return;

	if (mpFullEmulation && mpFullEmulation->mbInited)
		mpFullEmulation->Sync();

	if (g_ATLC1450XLDiskIO.IsEnabled()) {
		if (attn)
			g_ATLC1450XLDiskIO("Begin attention\n");
		else
			g_ATLC1450XLDiskIO("End attention\n");
	}

	mbAttn = attn;

	if (!mpFullEmulation && attn && mControllerState == ControllerState::WaitingForAttn && mbControllerBusy) {
		mControllerState = ControllerState::ReceiveCommandFrame;
		mbControllerBusy = false;
		mTransferIndex = 0;
		SetReceiveTimeout();
	}
}

sint32 AT1450XLDiskDevice::DebugReadByte(uint32 addr) const {
	addr &= 0xFC;

	if (addr == 0x14) {
		if (mpFullEmulation)
			mpFullEmulation->Sync();

		return mReadLatch;
	}

	return -1;
}

sint32 AT1450XLDiskDevice::ReadByte(uint32 addr) {
	sint32 v = DebugReadByte(addr);

	if (g_ATLC1450XLDiskIO.IsEnabled() && (addr & 0xFC) == 0x14)
		g_ATLC1450XLDiskIO("Read from controller: $%02X\n", v);

	return v;
}

bool AT1450XLDiskDevice::WriteByte(uint32 addr, uint8 value) {
	// $D110-D113 (W): Write latch

	addr &= 0xFC;

	if (addr == 0x10) {
		if (mpFullEmulation) {
			mpFullEmulation->Sync();

			if (g_ATLC1450XLDiskIO.IsEnabled())
				g_ATLC1450XLDiskIO("Write to controller: $%02X\n", value);

			if (mpFullEmulation->mPorts[0] & 0x80)
				mbControllerBusy = true;

			mpFullEmulation->mWriteLatch = value;
			return false;
		}

		if (g_ATLC1450XLDiskIO.IsEnabled())
			g_ATLC1450XLDiskIO("Write to controller: $%02X (phase %d)\n", value, (int)mControllerState);

		mbControllerBusy = true;
		switch(mControllerState) {
			case ControllerState::WaitingForCommandFrame:
				mTransferIndex = 0;

				SetReceiveTimeout();

				// The firmware first waits for BUSY and then for ATTN to assert.
				// It can proceed immediately if ATTN is already asserted as the
				// handler does, but BUSY is cleared without reading a byte, which
				// means that the first byte written is always discarded. The
				// second byte must be written after ATTN has been asserted.
				if (mbAttn || mbControllerBoot) {
					mbControllerBusy = false;
					mControllerState = ControllerState::ReceiveCommandFrame;

					if (value)
						mCommandBuffer[mTransferIndex++] = value;
				} else {
					mControllerState = ControllerState::WaitingForAttn;
				}
				break;

			case ControllerState::WaitingForAttn:
				break;

			case ControllerState::ReceiveCommandFrame:
				mCommandBuffer[mTransferIndex] = value;

				// The controller has a hack where it will discard device ID bytes
				// of $00 to handle desync.
				if (mTransferIndex || value)
					++mTransferIndex;

				if (mTransferIndex < 5) {
					mbControllerBusy = false;

					SetReceiveTimeout();
				} else {
					mbControllerBusy = false;
					mControllerState = ControllerState::WaitingForCommandFrame;
					ClearReceiveTimeout();

					const bool checksumOK = ATComputeSIOChecksum(mCommandBuffer, 4) == mCommandBuffer[4];

					g_ATLC1450XLDisk("Received command: $%02X %02X %02X %02X %02X%s\n"
						, mCommandBuffer[0]
						, mCommandBuffer[1]
						, mCommandBuffer[2]
						, mCommandBuffer[3]
						, mCommandBuffer[4]
						, checksumOK ? "" : " (checksum bad)"
					);
					
					if (!checksumOK) {
						// checksum fail - ignore the frame
						mDriveStatus |= 0x01;
						EndCommand();
					} else if (mCommandBuffer[0] == 0x31 || mCommandBuffer[0] == 0x32) {
						mCommandSubPhase = 0;
						PrepareOperation();
					} else {
						// not D1: or D2: -- delay ~227ms, clear BUSY and exit without sending anything
						mCommandBuffer[1] = 0;
						SetOperationDelay(1);
						//SetOperationDelay(406000);
					}
				}

				break;

			case ControllerState::SendCommandNAK:
				mReadLatch = 0x4E;
				mbControllerBusy = false;
				EndCommand();
				break;

			case ControllerState::SendCommandACK:
				mReadLatch = 0x41;
				mbControllerBusy = false;
				StartOperation();
				break;

			case ControllerState::ReceiveDataFrame:
				// Controller timing is 20-25 machine cycles from BUSY asserted to BUSY
				// cleared and another 16 cycles to loop back around again. This is
				// equivalent to 53.7-67.1 and 43 computer cycles, respectively.
				if (!mpEventTransfer) {
					uint64 timeSinceACK = mpScheduler->GetTick64() - mLastTransferTime;
					uint32 delay = 60;

					if (timeSinceACK < 43)
						delay += 43 - (uint32)timeSinceACK;

					mpScheduler->SetEvent(delay, this, (uint32)EventIds::Transfer, mpEventTransfer);
					mTransferBuffer[mTransferIndex] = value;
				}
				break;

			case ControllerState::SendDataNAK:
				mReadLatch = 0x4E;
				EndCommand();
				break;

			case ControllerState::SendDataACK:
				mReadLatch = 0x41;
				StartOperationPostReceive();
				break;

			case ControllerState::SendComplete:
			case ControllerState::SendError:
			case ControllerState::SendDataFrame:
				// The controller is actually rather slow at transfers. At a clock speed
				// of 10MHz (with 15 T-states per machine cycle), it takes 12-18 machine
				// cycles for the 8040 to push out a new byte and reset BUSY, and another
				// 23 cycles before it can recover to respond to BUSY being set again.
				// This corresponds to 32.2-48.3 computer cycles to BUSY dropping and
				// another 61.7 cycles of required delay.
				//
				// To simplify things we use a constant 40 cycles to BUSY deassert and
				// 62 cycles to recover.

				if (!mpEventTransfer) {
					uint64 timeSinceACK = mpScheduler->GetTick64() - mLastTransferTime;
					uint32 delay = 40;

					if (timeSinceACK < 62)
						delay += 62 - (uint32)timeSinceACK;

					mpScheduler->SetEvent(delay, this, (uint32)EventIds::Transfer, mpEventTransfer);
				}
				break;
		}
	}

	return false;
}

void AT1450XLDiskDevice::GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const {
	devInfo.mDeviceId = 0x01;
	devInfo.mbHasIrq = false;
}

void AT1450XLDiskDevice::SelectPBIDevice(bool enable) {
	mpMemMgr->EnableLayer(mpMemLayerFirmware, enable);
	mpMemMgr->EnableLayer(mpMemLayerHardware, enable);
}

bool AT1450XLDiskDevice::IsPBIOverlayActive() const {
	return true;
}

uint8 AT1450XLDiskDevice::ReadPBIStatus(uint8 busData, bool debugOnly) {
	if (mpFullEmulation && mbControllerBusy)
		mpFullEmulation->Sync();

	return (busData & 0xFE) | (mbControllerBusy ? 0x01 : 0x00);
}

void AT1450XLDiskDevice::InitFirmware(ATFirmwareManager *fwman) {
	mpFwMgr = fwman;

	ReloadFirmware();
}

bool AT1450XLDiskDevice::ReloadFirmware() {
	mFirmwareStatus = ATDeviceFirmwareStatus::Missing;

	const auto fwid = mpFwMgr->GetFirmwareOfType(kATFirmwareType_1450XLDiskHandler, true);

	bool changed = false;
	bool valid = false;
	mpFwMgr->LoadFirmware(fwid, mFirmware, 0, sizeof mFirmware, &changed, nullptr, nullptr, nullptr, &valid);

	if (fwid) {
		mFirmwareStatus = ATDeviceFirmwareStatus::Invalid;

		// check for PBI bytes
		if (mFirmware[3] == 0x80 && mFirmware[11] == 0x91)
			mFirmwareStatus = ATDeviceFirmwareStatus::OK;
	}

	if (mpFullEmulation) {
		const auto fwid2 = mpFwMgr->GetFirmwareOfType(mbIsTONG ? kATFirmwareType_1450XLTONGDiskController : kATFirmwareType_1450XLDiskController, true);

		mpFullEmulation->mControllerFirmwareStatus = ATDeviceFirmwareStatus::Missing;

		if (fwid2) {
			mpFwMgr->LoadFirmware(fwid2, mpFullEmulation->mControllerFirmware, 0, sizeof mpFullEmulation->mControllerFirmware, &changed, nullptr, nullptr, nullptr, &valid);

			if (valid)
				mpFullEmulation->mControllerFirmwareStatus = ATDeviceFirmwareStatus::OK;
			else
				mpFullEmulation->mControllerFirmwareStatus = ATDeviceFirmwareStatus::Invalid;
		}
	}

	return changed;
}

const wchar_t *AT1450XLDiskDevice::GetWritableFirmwareDesc(uint32 idx) const {
	return nullptr;
}

bool AT1450XLDiskDevice::IsWritableFirmwareDirty(uint32 idx) const {
	return false;
}

void AT1450XLDiskDevice::SaveWritableFirmware(uint32 idx, IVDStream& stream) {
}

ATDeviceFirmwareStatus AT1450XLDiskDevice::GetFirmwareStatus() const {
	if (mpFullEmulation) {
		switch(mpFullEmulation->mControllerFirmwareStatus) {
			case ATDeviceFirmwareStatus::Missing:
				return ATDeviceFirmwareStatus::Missing;

			case ATDeviceFirmwareStatus::Invalid:
				if (mFirmwareStatus != ATDeviceFirmwareStatus::Missing)
					return ATDeviceFirmwareStatus::Invalid;
				break;

			default:
				break;
		}
	}

	return mFirmwareStatus;
}

void AT1450XLDiskDevice::InitMemMap(ATMemoryManager *memmgr) {
	mpMemMgr = memmgr;

	mpMemLayerFirmware = mpMemMgr->CreateLayer(kATMemoryPri_PBI, mFirmware, 0xD8, 0x08, true);
	mpMemMgr->SetLayerName(mpMemLayerFirmware, "1450XLD Disk Controller Firmware");

	ATMemoryHandlerTable handlers {};
	handlers.mbPassAnticReads = true;
	handlers.mbPassReads = true;
	handlers.mbPassWrites = true;
	handlers.mpThis = this;
	handlers.BindDebugReadHandler<&AT1450XLDiskDevice::DebugReadByte>();
	handlers.BindReadHandler<&AT1450XLDiskDevice::ReadByte>();
	handlers.BindWriteHandler<&AT1450XLDiskDevice::WriteByte>();

	mpMemLayerHardware = mpMemMgr->CreateLayer(kATMemoryPri_PBI, handlers, 0xD1, 0x01);
	mpMemMgr->SetLayerName(mpMemLayerHardware, "1450XLD Disk Controller");
}

bool AT1450XLDiskDevice::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	if (index == 0) {
		lo = 0xD110;
		hi = 0xD11F;
		return true;
	}

	return false;
}

void AT1450XLDiskDevice::OnScheduledEvent(uint32 id) {
	if (id == (uint32)EventIds::ReceiveTimeout) {
		mpSlowEventReceiveTimeout = nullptr;

		switch(mControllerState) {
			case ControllerState::WaitingForAttn:
			case ControllerState::ReceiveCommandFrame:
				mControllerState = ControllerState::WaitingForCommandFrame;
				mbControllerBusy = false;
				g_ATLC1450XLDisk("Receive timeout expired\n");
				break;

			default:
				break;
		}
	} else if (id == (uint32)EventIds::MotorOff) {
		mpSlowEventMotorOff = nullptr;

		MotorOff();
	} else if (id == (uint32)EventIds::OperationDelay) {
		mpSlowEventOpDelay = nullptr;

		if (mControllerState == ControllerState::Post) {
			mControllerState = ControllerState::WaitingForCommandFrame;

			if (mbAttn && mbControllerBusy) {
				mControllerState = ControllerState::ReceiveCommandFrame;
				mbControllerBusy = false;
				mTransferIndex = 0;
				SetReceiveTimeout();
			}
		} else if (mControllerState == ControllerState::OperationDelay)
			SendOperationResult();
	} else if (id == (uint32)EventIds::Transfer) {
		mpEventTransfer = nullptr;
		mLastTransferTime = mpScheduler->GetTick64();
		mbControllerBusy = false;

		if (mControllerState == ControllerState::SendComplete) {
			mReadLatch = 0x43;

			if (mTransferLength)
				mControllerState = ControllerState::SendDataFrame;
			else
				EndCommand();
		} else if (mControllerState == ControllerState::SendError) {
			mReadLatch = 0x45;

			if (mTransferLength)
				mControllerState = ControllerState::SendDataFrame;
			else
				EndCommand();
		} else if (mControllerState == ControllerState::SendDataFrame) {
			mReadLatch = mTransferBuffer[mTransferIndex++];

			if (mTransferIndex == mTransferLength)
				EndCommand();
		} else if (mControllerState == ControllerState::ReceiveDataFrame) {
			if (++mTransferIndex >= mTransferLength) {
				if (ATComputeSIOChecksum(mTransferBuffer, mTransferIndex - 1) == mTransferBuffer[mTransferIndex - 1]) {
					mControllerState = ControllerState::SendDataACK;

					if (mbControllerBusy) {
						mbControllerBusy = false;
						mReadLatch = 0x41;
						EndCommand();
					}
				} else {
					mControllerState = ControllerState::SendDataNAK;

					if (mbControllerBusy) {
						mbControllerBusy = false;
						mReadLatch = 0x4E;
						StartOperationPostReceive();
					}
				}
			}
		}
	} else if (id == (uint32)EventIds::DiskChange) {
		mpDriveEventDiskChange = nullptr;

		static constexpr uint32 kDiskChangeStepMS = 50;

		switch(++mDiskChangeState) {
			case 1:		// disk being removed (write protect covered)
			case 2:		// disk removed (write protect clear)
			case 3:		// disk being inserted (write protect covered)
				mpFullEmulation->GetDriveScheduler().SetEvent(kDiskChangeStepMS, this, (uint32)EventIds::DiskChange, mpDriveEventDiskChange);
				break;

			case 4:		// disk inserted (write protect normal)
				mDiskChangeState = 0;
				break;
		}

		UpdateDiskStatus();
	}
}

void AT1450XLDiskDevice::OnDiskChanged(bool mediaRemoved) {
	if (!mpFullEmulation)
		return;

	if (mediaRemoved) {
		mDiskChangeState = 0;
		mpFullEmulation->GetDriveScheduler().SetEvent(1, this, (uint32)EventIds::DiskChange, mpDriveEventDiskChange);
	}

	UpdateDiskStatus();
}

void AT1450XLDiskDevice::OnWriteModeChanged() {
}

void AT1450XLDiskDevice::OnTimingModeChanged() {
	if (mpFullEmulation)
		mpFullEmulation->mFDC.SetAccurateTimingEnabled(mpDiskInterface->IsAccurateSectorTimingEnabled());
}

void AT1450XLDiskDevice::OnAudioModeChanged() {
	UpdateMotorStatus();
}

bool AT1450XLDiskDevice::IsImageSupported(const IATDiskImage& image) const {
	const auto& geo = image.GetGeometry();

	if (geo.mbHighDensity || geo.mTrackCount > 40)
		return false;

	if (mbIsTONG) {
		if (geo.mSectorSize == 256 && geo.mSectorsPerTrack != 18)
			return false;

		return true;
	}

	if (geo.mSideCount > 1)
		return false;

	if (geo.mSectorSize != 128)
		return false;

	if (geo.mSectorsPerTrack != (geo.mbMFM ? 26 : 18))
		return false;

	return true;
}

void AT1450XLDiskDevice::SetReceiveTimeout() {
	// ~10ms
	static constexpr uint32 kReceiveTimeoutScanlines = 178977 / 114;

	mpSlowScheduler->SetEvent(kReceiveTimeoutScanlines, this, (uint32)EventIds::ReceiveTimeout, mpSlowEventReceiveTimeout);
}

void AT1450XLDiskDevice::ClearReceiveTimeout() {
	if (mpSlowEventReceiveTimeout)
		mpSlowScheduler->UnsetEvent(mpSlowEventReceiveTimeout);
}

uint32 AT1450XLDiskDevice::MotorOn() {
	if (!mpSlowEventMotorOff) {
		SetMotorTimeout();
		UpdateMotorStatus();

		mMotorOnTime = mpScheduler->GetTick64();
		mDriveStatus |= 0x10;

		// motor-on delay is approximately 1 second
		return 178977;
	}

	// need to reset the motor timeout regardless
	SetMotorTimeout();
	return 0;
}

void AT1450XLDiskDevice::MotorOff() {
	mDriveStatus &= 0xEF;

	ClearMotorTimeout();
	UpdateMotorStatus();
}

void AT1450XLDiskDevice::SetMotorTimeout() {
	static constexpr uint32 kMotorTimeoutScanlines = 80069;

	mpSlowScheduler->SetEvent(kMotorTimeoutScanlines, this, (uint32)EventIds::MotorOff, mpSlowEventMotorOff);
}

void AT1450XLDiskDevice::ClearMotorTimeout() {
	if (mpSlowEventMotorOff)
		mpSlowScheduler->UnsetEvent(mpSlowEventMotorOff);
}

void AT1450XLDiskDevice::UpdateMotorStatus() {
	const bool audioEnabled = mpDiskInterface->AreDriveSoundsEnabled();
	const bool motorEnabled = mSelectedDriveIndex == 0 && (mpFullEmulation ? (mpFullEmulation->mPorts[1] & 0x10) != 0 : mpSlowEventMotorOff != nullptr);

	mAudioPlayer.SetRotationSoundEnabled(audioEnabled && motorEnabled);
	mpDiskInterface->SetShowMotorActive(motorEnabled);
}

void AT1450XLDiskDevice::PrepareOperation() {
	const uint8 cmd = mCommandBuffer[1];

	switch(cmd) {
		case 0x21:
		case 0x22:
		case 0x50:
		case 0x53:
		case 0x57:
			SendCommandACK();
			break;

		case 0x52:
			mbControllerBoot = false;
			SendCommandACK();
			break;

		default:
			// unsupported command -- NAK
			SendCommandNAK();
			break;
	}
}

void AT1450XLDiskDevice::SendCommandNAK() {
	// set bad command bit
	mDriveStatus |= 0x01;

	// this is entered with controller busy set, so it will immediately transfer
	mReadLatch = 0x4E;
	mbControllerBusy = false;
	EndCommand();
}

void AT1450XLDiskDevice::SendCommandACK() {
	// this is entered with controller busy set, so it will immediately transfer
	mReadLatch = 0x41;
	mbControllerBusy = false;
	StartOperation();
}

void AT1450XLDiskDevice::StartOperation() {
	const uint8 cmd = mCommandBuffer[1];

	if (cmd == 0x52) {
		// turn on the motor if not already on
		const bool useAccurateTiming = mpDiskInterface->IsAccurateSectorTimingEnabled();
		uint32 t = MotorOn();

		mbOperationSuccess = false;

		// check if we need to seek
		if (mpDiskInterface->IsDiskLoaded()) {
			const auto& geo = mpDiskInterface->GetDiskImage()->GetGeometry();
			const uint32 spt = geo.mSectorsPerTrack;

			const uint32 sector = (mCommandBuffer[2] + mCommandBuffer[3]*256) & 2047;
			const uint32 track = sector ? (sector - 1) / spt: 0;

			mpDiskInterface->SetShowActivity(true, sector);

			// check if we need to seek
			t = Seek(t, track);

			if (!useAccurateTiming)
				t = 0;

			bool success = false;
			IATDiskImage& img = *mpDiskInterface->GetDiskImage();

			int bestIndex = -1;
			uint32 bestPhysSector = 0;
			float bestDistance = 10.0f;
			uint8 bestStatus = 0;
			int bestWeakOffset = -1;

			if (sector <= img.GetVirtualSectorCount()) {
				// compute current rotational position, assuming 288 RPM
				const float rot = (float)((mpScheduler->GetTick64() - mMotorOnTime + t) % kCyclesPerRotation) / (float)kCyclesPerRotation;

				// find closest sector
				ATDiskVirtualSectorInfo vsi {};
				img.GetVirtualSectorInfo(sector - 1, vsi);

				for(uint32 i=0; i<vsi.mNumPhysSectors; ++i) {
					ATDiskPhysicalSectorInfo psi {};
					img.GetPhysicalSectorInfo(vsi.mStartPhysSector + i, psi);

					// if this sector has an address CRC, it will be shadowed by a sector
					// that doesn't
					if (!(psi.mFDCStatus & 0x18) && (bestStatus & 0x18))
						continue;

					// compute angular distance
					float dist = psi.mRotPos - rot;
					dist -= floorf(dist);

					if (dist < bestDistance) {
						bestIndex = (int)i;
						bestDistance = dist;
						bestStatus = psi.mFDCStatus;
						bestPhysSector = vsi.mStartPhysSector + bestIndex;
						bestWeakOffset = psi.mWeakDataOffset;
					}
				}
			}

			if (bestIndex >= 0) {
				img.ReadPhysicalSector(bestPhysSector, mTransferBuffer, 128);

				// Check if we should emulate weak bits.
				if (bestWeakOffset >= 0) {
					for(int i = bestWeakOffset; i < 128; ++i) {
						mTransferBuffer[i] ^= (uint8)mWeakBitLFSR;

						mWeakBitLFSR = (mWeakBitLFSR << 8) + (0xff & ((mWeakBitLFSR >> (28 - 8)) ^ (mWeakBitLFSR >> (31 - 8))));
					}
				}

				success = (bestStatus == 0xFF);

				// add delay from current position to angular start of sector
				if (useAccurateTiming)
					t += VDRoundToInt(bestDistance * kCyclesPerRotation);

				// add delay for the time to read the sector (~140 raw bytes at 32/64us)
				constexpr uint32 sectorReadTimeFM = 16036;
				t += geo.mbMFM ? sectorReadTimeFM/2 : sectorReadTimeFM;

				mFDCStatus = bestStatus;
			} else {
				// not found -- timeout in 5 revs
				if (useAccurateTiming)
					t += kCyclesPerRotation * 5;
				else
					t += kCyclesPerRotation / 10;

				mFDCStatus = 0xEF;
			}

			if (geo.mbMFM)
				mDriveStatus |= 0x20;
			else
				mDriveStatus &= ~0x20;

			mbOperationSuccess = success;
		} else {
			mFDCStatus = 0x7F;
		}

		SetOperationDelay(t);
	} else if (cmd == 0x53) {
		SendOperationResult();
	} else if (cmd == 0x50 || cmd == 0x57) {
		BeginReceive(128);
	} else if (cmd == 0x21 || cmd == 0x22) {
		const bool useAccurateTiming = mpDiskInterface->IsAccurateSectorTimingEnabled();

		uint32 t = MotorOn();

		t = Seek(t, 0);

		if (!useAccurateTiming)
			t = 0;

		SetOperationDelay(t);
	}
}

void AT1450XLDiskDevice::BeginReceive(uint32 len) {
	mControllerState = ControllerState::ReceiveDataFrame;
	mTransferIndex = 0;
	mTransferLength = len + 1;
	mbControllerBusy = false;
}

void AT1450XLDiskDevice::StartOperationPostReceive() {
	const uint8 cmd = mCommandBuffer[1];

	if (cmd == 0x50 || cmd == 0x57) {
		// turn on the motor if not already on
		const bool useAccurateTiming = mpDiskInterface->IsAccurateSectorTimingEnabled();
		uint32 t = MotorOn();

		mbOperationSuccess = false;
		mDriveStatus &= 0xF8;

		// check if we need to seek
		if (mpDiskInterface->IsDiskLoaded()) {
			if (!mpDiskInterface->IsDiskWritable()) {
				mDriveStatus |= 0x08;
				mFDCStatus = 0xF7;
			} else {
				const auto& geo = mpDiskInterface->GetDiskImage()->GetGeometry();
				const uint32 spt = geo.mSectorsPerTrack;

				const uint32 sector = (mCommandBuffer[2] + mCommandBuffer[3]*256) & 2047;
				const uint32 track = sector ? (sector - 1) / spt: 0;

				mpDiskInterface->SetShowActivity(true, sector);

				// check if we need to seek
				t = Seek(t, track);

				if (!useAccurateTiming)
					t = 0;

				bool success = false;
				IATDiskImage& img = *mpDiskInterface->GetDiskImage();

				int bestIndex = -1;
				uint32 bestPhysSector = 0;
				float bestDistance = 10.0f;
				uint8 bestStatus = 0xFF;

				if (sector <= img.GetVirtualSectorCount()) {
					// compute current rotational position, assuming 288 RPM
					const float rot = (float)((mpScheduler->GetTick64() - mMotorOnTime + t) % kCyclesPerRotation) / (float)kCyclesPerRotation;

					// find closest sector
					ATDiskVirtualSectorInfo vsi {};
					img.GetVirtualSectorInfo(sector - 1, vsi);

					for(uint32 i=0; i<vsi.mNumPhysSectors; ++i) {
						ATDiskPhysicalSectorInfo psi {};
						img.GetPhysicalSectorInfo(vsi.mStartPhysSector + i, psi);

						// if this sector has an address CRC, it will be shadowed by a sector
						// that doesn't
						if (!(psi.mFDCStatus & 0x18) && (bestStatus & 0x18))
							continue;

						// compute angular distance
						float dist = psi.mRotPos - rot;
						dist -= floorf(dist);

						if (dist < bestDistance) {
							bestIndex = (int)i;
							bestDistance = dist;
							bestStatus = psi.mFDCStatus;
							bestPhysSector = vsi.mStartPhysSector + bestIndex;
						}
					}
				}

				if (bestIndex >= 0) {
					img.WritePhysicalSector(bestPhysSector, mTransferBuffer, 128);

					success = true;

					// add delay from current position to angular start of sector
					if (useAccurateTiming)
						t += VDRoundToInt(bestDistance * kCyclesPerRotation);

					// add delay for the time to write the sector (~140 raw bytes at 32/64us)
					constexpr uint32 sectorReadTimeFM = 16036;
					t += geo.mbMFM ? sectorReadTimeFM/2 : sectorReadTimeFM;

					// if verify is on, add a rotation to read back
					t += kCyclesPerRotation;

					mFDCStatus = bestStatus;
				} else {
					// not found -- timeout in 5 revs
					if (useAccurateTiming)
						t += kCyclesPerRotation * 5;
					else
						t += kCyclesPerRotation / 10;

					mFDCStatus = 0xEF;
				}

				if (geo.mbMFM)
					mDriveStatus |= 0x20;
				else
					mDriveStatus &= ~0x20;

				mbOperationSuccess = success;
			}
		} else {
			mFDCStatus = 0x7F;
		}

		SetOperationDelay(t);
	}
}

void AT1450XLDiskDevice::SetOperationDelay(uint32 cycles) {
	uint32 scanlines = (cycles + 57) / 114;

	mpSlowScheduler->SetEvent(scanlines ? scanlines : 1, this, (uint32)EventIds::OperationDelay, mpSlowEventOpDelay);

	mControllerState = ControllerState::OperationDelay;
}

void AT1450XLDiskDevice::SendOperationResult() {
	const uint8 cmd = mCommandBuffer[1];

	if (cmd == 0) {
		EndCommand();
	} else if (cmd == 0x52) {
		BeginResultTransfer(mbOperationSuccess, 128);
	} else if (cmd == 0x53) {
		mTransferBuffer[0] = mDriveStatus;
		mTransferBuffer[1] = (mpDiskInterface->IsDiskLoaded() ? 0xFF : 0x7F) & (mFDCStatus | 0x80);
		mTransferBuffer[2] = 0xE0;
		mTransferBuffer[3] = 0x00;

		BeginResultTransfer(true, 4);
	} else if (cmd == 0x50 || cmd == 0x57) {
		BeginResultTransfer(mbOperationSuccess, 0);
	} else if (cmd == 0x21 || cmd == 0x22) {
		if (mCommandSubPhase == 0) {
			if (!mpDiskInterface->IsDiskWritable()) {
				if (mpDiskInterface->IsDiskLoaded()) {
					mDriveStatus |= 0x0C;
					mFDCStatus = 0xF7;
				} else {
					mDriveStatus |= 0x04;
					mFDCStatus = 0x7F;
				}

				memset(mTransferBuffer, 0, 128);
				BeginResultTransfer(false, 128);
				return;
			}

			mpDiskInterface->SetShowActivity(true, mCurrentTrack);

			if (mCurrentTrack == 0)
				mpDiskInterface->FormatDisk(cmd == 0x22 ? 1040 : 720, 3, 128);

			if (mCurrentTrack == 39 || !mpDiskInterface->IsAccurateSectorTimingEnabled()) {
				SetOperationDelay(Seek(kCyclesPerRotation, 0));
				++mCommandSubPhase;
			} else
				SetOperationDelay(Seek(kCyclesPerRotation, mCurrentTrack + 1));
		} else if (mCommandSubPhase == 1) {
			// The 1450XFD verifies at 2 revs/track for both SD and ED.
			mpDiskInterface->SetShowActivity(true, mCurrentTrack);

			if (mCurrentTrack == 39 || !mpDiskInterface->IsAccurateSectorTimingEnabled()) {
				SetOperationDelay(Seek(kCyclesPerRotation * 2, 0));
				++mCommandSubPhase;
			} else
				SetOperationDelay(Seek(kCyclesPerRotation * 2, mCurrentTrack + 1));
		} else {
			memset(mTransferBuffer, 0xFF, 128);
			BeginResultTransfer(true, 128);
		}
	}
}

void AT1450XLDiskDevice::BeginResultTransfer(bool success, uint32 dataLen) {
	// on success, clear command/data frame and operation error bits; on
	// failure, set the op error bit
	if (success)
		mDriveStatus &= 0xF8;
	else
		mDriveStatus |= 0x04;

	mTransferIndex = 0;

	if (dataLen) {
		mTransferBuffer[dataLen] = ATComputeSIOChecksum(mTransferBuffer, dataLen);
		mTransferLength = dataLen + 1;
	} else {
		mTransferLength = 0;
	}

	// the handler may already have set busy, in which case we can send now; otherwise,
	// wait for busy
	mControllerState = success ? ControllerState::SendComplete : ControllerState::SendError;

	if (mbControllerBusy) {
		mReadLatch = success ? 0x43 : 0x45;
		mbControllerBusy = false;

		if (mTransferLength)
			mControllerState = ControllerState::SendDataFrame;
		else
			EndCommand();
	}
}

void AT1450XLDiskDevice::EndCommand() {
	mpDiskInterface->SetShowActivity(false, 0);

	mbControllerBusy = false;
	mControllerState = ControllerState::WaitingForCommandFrame;

	mpSlowScheduler->UnsetEvent(mpSlowEventOpDelay);
}

uint32 AT1450XLDiskDevice::Seek(uint32 t, uint32 track) {
	const bool useAccurateTiming = mpDiskInterface->IsAccurateSectorTimingEnabled();

	if (mCurrentTrack == track)
		return t;

	// step at 3ms/track
	int steps = 0;

	do {
		if (mpDiskInterface->AreDriveSoundsEnabled())
			mAudioPlayer.PlayStepSound(kATAudioSampleId_DiskStep2, 0.3f + 0.7f * cosf((float)steps * nsVDMath::kfPi), t);

		t += 5369;

		if (mCurrentTrack < track)
			++mCurrentTrack;
		else
			--mCurrentTrack;

		// if accurate timing is off, avoid excessively long seek sounds
		++steps;
		if (!useAccurateTiming && steps >= 5)
			break;
	} while(mCurrentTrack != track);

	// add 170ms head settling delay
	t += 304261;

	mCurrentTrack = track;

	return t;
}

void AT1450XLDiskDevice::OnFDCStep(bool inward) {
	bool play = false;

	if (mSelectedDriveIndex) {
		// no disk -- we're never on track 0
		mpFullEmulation->mFDC.SetCurrentTrack(2, false);
	} else if (inward) {
		// step in (increasing track number)
		if (mCurrentTrack < 45U) {
			++mCurrentTrack;

			mpFullEmulation->mFDC.SetCurrentTrack(mCurrentTrack * 2, mCurrentTrack == 0);
		}

		play = true;
	} else {
		// step out (decreasing track number)
		if (mCurrentTrack > 0) {
			--mCurrentTrack;

			mpFullEmulation->mFDC.SetCurrentTrack(mCurrentTrack * 2, mCurrentTrack == 0);

			play = true;
		}
	}

	if (play) {
		const uint32 t = ATSCHEDULER_GETTIME(&mpFullEmulation->GetDriveScheduler());
	
		if (t - mLastStepSoundTime > 50000)
			mLastStepPhase = 0;

		if (mpDiskInterface->AreDriveSoundsEnabled())
			mAudioPlayer.PlayStepSound(kATAudioSampleId_DiskStep2, 0.3f + 0.7f * cosf((float)mLastStepPhase++ * nsVDMath::kfPi));

		mLastStepSoundTime = t;
	}
}

void AT1450XLDiskDevice::OnMotorChange() {
	UpdateMotorStatus();
}

void AT1450XLDiskDevice::SetSelectedDrive(uint8 driveIdx) {
	driveIdx &= 3;

	if (mSelectedDriveIndex != driveIdx) {
		mSelectedDriveIndex = driveIdx;

		UpdateDiskStatus();
	}
}

void AT1450XLDiskDevice::UpdateDiskStatus() {
	IATDiskImage *image = mSelectedDriveIndex ? nullptr : mpDiskInterface->GetDiskImage();

	mpFullEmulation->mFDC.SetDiskImage(image, (image != nullptr && mDiskChangeState == 0));
	mpFullEmulation->mFDC.SetAutoIndexPulse(mSelectedDriveIndex == 0);

	if (mSelectedDriveIndex)
		mpFullEmulation->mFDC.SetCurrentTrack(2, false);
	else
		mpFullEmulation->mFDC.SetCurrentTrack(mCurrentTrack * 2, mCurrentTrack == 0);

	UpdateWriteProtectStatus();
}

void AT1450XLDiskDevice::UpdateWriteProtectStatus() {
	mpFullEmulation->mFDC.SetWriteProtectOverride(mSelectedDriveIndex == 0 && mDiskChangeState ? std::optional<bool>((mDiskChangeState & 1) != 0) : std::nullopt);
}
