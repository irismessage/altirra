//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2016 Avery Lee
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
#include <vd2/system/math.h>
#include <at/atcore/consoleoutput.h>
#include <at/atcore/crc.h>
#include <at/atcore/logging.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/randomization.h>
#include <at/atemulation/diskutils.h>
#include "diskinterface.h"
#include "disktrace.h"
#include "fdc.h"
#include "trace.h"

extern ATLogChannel g_ATLCDisk;
ATLogChannel g_ATLCFDCCommand(false, false, "FDCCMD", "Floppy drive controller commands");
ATLogChannel g_ATLCFDCCommandFI(false, false, "FDCCMDFI", "Floppy drive controller extra forced interrupt commands");
ATLogChannel g_ATLCFDCData(false, false, "FDCDATA", "Floppy drive controller data transfer");

ATFDCEmulator::ATFDCEmulator() {
	mpFnDrqChange = [](bool drq) {};
	mpFnIrqChange = [](bool drq) {};
	mpFnStep = [](bool inward) {};
	mpFnMotorChange = [](bool active) {};
	mpFnHeadLoadChange = [](bool loaded) {};
}

ATFDCEmulator::~ATFDCEmulator() {
	Shutdown();
}

void ATFDCEmulator::Init(ATScheduler *sch, float rpm, float periodAdjustFactor, Type type) {
	double schRate = sch->GetRate().asDouble();

	mpScheduler = sch;
	mType = type;

	// ~4ms for index pulse, per Tandon TM-50 manual
	mCyclesPerIndexPulse = VDRoundToInt32(schRate * 0.004);

	SetSpeeds(rpm, periodAdjustFactor, false);

	// Standard times for WD1771/279X at 2MHz (which we adjust below for 1MHz).
	static constexpr double kStepTimeSecs[4] = {
		 3.0 / 1000.0,
		 6.0 / 1000.0,
		10.0 / 1000.0,
		15.0 / 1000.0,
	};

	// Standard times for WD1770 at 8MHz.
	static constexpr double kStepTimeSecs1770[4] = {
		 6.0 / 1000.0,
		12.0 / 1000.0,
		20.0 / 1000.0,
		30.0 / 1000.0,
	};

	// Standard times for WD1772 at 8MHz.
	static constexpr double kStepTimeSecs1772[4] = {
		2.0 / 1000.0,
		3.0 / 1000.0,
		5.0 / 1000.0,
		6.0 / 1000.0,
	};

	VDASSERTCT(vdcountof(kStepTimeSecs) == vdcountof(mCycleStepTable));
	VDASSERTCT(vdcountof(kStepTimeSecs1772) == vdcountof(mCycleStepTable));

	// If the disk is spinning faster than usual, as it does in the XF551, then the
	// FDC is up-clocked by the same amount... and step delays are correspondingly
	// shorter.

	if (mType == kType_1772) { 
		for(int i=0; i<4; ++i)
			mCycleStepTable[i] = VDRoundToInt(schRate * kStepTimeSecs1772[i] * periodAdjustFactor);
	} else if (mType == kType_1770) { 
		for(int i=0; i<4; ++i)
			mCycleStepTable[i] = VDRoundToInt(schRate * kStepTimeSecs1770[i] * periodAdjustFactor);
	} else {
		for(int i=0; i<4; ++i)
			mCycleStepTable[i] = VDRoundToInt(schRate * kStepTimeSecs[i] * periodAdjustFactor);
	}

	// The 1771 uses a 10ms head delay at 2MHz (which doubles to 20ms at the 810's 1MHz).
	// The 279X uses 15ms at 2MHz (which doubles to 30ms at the 1050's 1MHz).
	float headLoadDelaySecs = 10.0f;

	switch(mType) {
		case kType_1770:		// 30ms at 8MHz
			headLoadDelaySecs = 0.030f;
			break;

		case kType_1772:		// 15ms at 8MHz (data sheets inconsistent -- some say 30ms)
			headLoadDelaySecs = 0.015f;
			break;

		case kType_1771:		// 10ms at 2MHz (doubled on 810 due to 1MHz clock)
			headLoadDelaySecs = 0.020f;
			break;

		default:
		case kType_2793:
		case kType_2797:		// 15ms at 2MHz (doubled on 1050 due to 1MHz clock)
			headLoadDelaySecs = 0.030f;
			break;
	}

	mCyclesHeadLoadDelay = VDRoundToInt(schRate * headLoadDelaySecs * periodAdjustFactor);

	// When head load delay is disabled, it takes about 210us for the FDC to assert DRQ on
	// a real 1050.
	float writeTrackFastDelaySecs = 210e-6f;
	mCyclesWriteTrackFastDelay = VDRoundToInt(schRate * writeTrackFastDelaySecs * periodAdjustFactor);
}

void ATFDCEmulator::Shutdown() {
	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpStateEvent);
		mpScheduler->UnsetEvent(mpAutoIndexOnEvent);
		mpScheduler->UnsetEvent(mpAutoIndexOffEvent);
		mpScheduler->UnsetEvent(mpAutoMotorIdleEvent);
		mpScheduler = nullptr;
	}
}

void ATFDCEmulator::DumpStatus(ATConsoleOutput& out) {
	out("Command register: $%02X", mRegCommand);
	out("Command status:   state %u (%u cycles to transition)", (unsigned)mState, mpStateEvent ? mpScheduler->GetTicksToEvent(mpStateEvent) : 0);
	out("Track register:   $%02X (physical track: %.1f)", mRegTrack, (float)mPhysHalfTrack / 2.0f);
	out("Sector register:  $%02X", mRegSector);
	out("Status register:  $%02X (%s)", DebugReadByte(0), mbRegStatusTypeI ? "type I" : "type II-IV");
	out("Data register:    $%02X", mRegData);
	out("Index pulse:      %s (auto index %s)", mbIndexPulse ? "asserted" : "negated", mbAutoIndexPulseEnabled ? "enabled" : "disabled");
	out("Motor:            %s", mbMotorRunning ? "running" : "off");
	out("Disk geometry:    %s encoding, %u tracks, %u sectors per track, %u sides, %u boot sectors"
		, mDiskGeometry.mbMFM ? "MFM" : "FM"
		, mDiskGeometry.mTrackCount
		, mDiskGeometry.mSectorsPerTrack
		, mDiskGeometry.mSideCount
		, mDiskGeometry.mBootSectorCount
	);
	out("INTRQ:            %s", mbIrqPending ? "asserted" : "negated");
	out("DRQ:              %s", mbDataReadPending || mbDataWritePending ? "asserted" : "negated");
}

void ATFDCEmulator::Reset() {
	AbortCommand();

	mRotPos = ATRandomizeAdvanceFast(g_ATRandomizationSeeds.mDiskStartPos) % mCyclesPerRotation;
	mRotTimeBase = mpScheduler->GetTick64();
	mRotations = 0;

	if (mpRotationTracer)
		mpRotationTracer->Warp(mRotPos);

	mState = kState_Idle;
	mRegCommand = 0xD8;		// force interrupt immediate
	mRegTrack = 0;
	mRegSector = 0;
	mRegStatus = 0;
	mRegData = 0;

	if (mbIrqPending) {
		mbIrqPending = false;
		mpFnIrqChange(false);
	}

	mbDataReadPending = false;
	mbRegStatusTypeI = true;

	mpFnDrqChange(false);

	mbMotorEnabled = false;
	mpFnMotorChange(false);

	if (mpRotationTracer)
		mpRotationTracer->SetMotorRunning(false);

	mbHeadLoaded = false;
	mpFnHeadLoadChange(false);

	mWeakBitLFSR = 1;
	mbDiskChangeStartupHackFDCCommandReceived = false;

	mpScheduler->UnsetEvent(mpStateEvent);

	if (mpDiskInterface)
		mpDiskInterface->SetShowActivity(false, 0);

	UpdateDensity();
}

void ATFDCEmulator::SetAccurateTimingEnabled(bool enabled) {
	mbUseAccurateTiming = enabled;
}

void ATFDCEmulator::SetMotorRunning(bool running) {
	if (mbMotorRunning == running)
		return;

	UpdateRotationalPosition();
	mbMotorRunning = running;

	UpdateAutoIndexPulse();

	if (mpRotationTracer)
		mpRotationTracer->SetMotorRunning(running);
}

void ATFDCEmulator::SetMotorSpeed(float rpm) {
	SetSpeeds(rpm, mPeriodAdjustFactor, mbDoubleClock);
}

void ATFDCEmulator::SetCurrentTrack(uint32 halfTrack, bool track0) {
	if (mPhysHalfTrack != halfTrack) {
		mPhysHalfTrack = halfTrack;

		g_ATLCFDC("Physical track is now %.1f\n", (float)halfTrack / 2.0f);
	}

	mbTrack0 = track0;

	if (mpRotationTracer)
		mpRotationTracer->SetTrack(halfTrack & 1 ? -1 : (int)(halfTrack >> 1));
}

void ATFDCEmulator::SetSide(bool side2) {
	mbSide2 = side2;
}

void ATFDCEmulator::SetDensity(bool mfm) {
	if (mbMFM != mfm) {
		mbMFM = mfm;

		UpdateDensity();

		g_ATLCFDC("Density encoding now set to %s\n", mfm ? "MFM" : "FM");
	}
}

void ATFDCEmulator::SetAutoIndexPulse(bool enabled) {
	SetAutoIndexPulse(enabled, enabled);
}

void ATFDCEmulator::SetAutoIndexPulse(bool enabled, bool connected) {
	if (mbAutoIndexPulseEnabled != enabled || mbAutoIndexPulseConnected != connected) {
		mbAutoIndexPulseEnabled = enabled;
		mbAutoIndexPulseConnected = connected;

		UpdateAutoIndexPulse();
	}
}

void ATFDCEmulator::SetSpeeds(float rpm, float periodAdjustFactor, bool doubleClock) {
	mPeriodAdjustFactor = periodAdjustFactor;

	// Cycles per second for the scheduler. Note that this is not necessarily the same as the rate at which
	// the FDC is clocked.
	const double schRate = mpScheduler->GetRate().asDouble();

	// Cycles per rotation of the disk. This depends only on RPM and not on FDC clock rate.
	sint32 newCyclesPerRotation = VDRoundToInt32(schRate * 60.0 / rpm);

	if (mCyclesPerRotation != newCyclesPerRotation) {
		UpdateRotationalPosition();

		float rotPosFrac = (float)mRotPos / (float)mCyclesPerRotation;

		mCyclesPerRotation = newCyclesPerRotation;
		mRotationPeriodSecs = 60.0f / rpm;

		mRotPos = VDRoundToInt32(rotPosFrac * mCyclesPerRotation);

		if (mRotPos >= mCyclesPerRotation)
			mRotPos -= mCyclesPerRotation;
	}

	SetDoubleClock(doubleClock);
}

void ATFDCEmulator::SetDoubleClock(bool doubleClock) {
	// Set if we have a 279X in undivided 2MHz mode for 8" drives.
	mbDoubleClock = doubleClock;

	// 4us per bit cell, 2 bit cells per data bit, 8 data bits per byte = 64us/byte. However,
	// the 1771/279X FDC is spec'd for 2MHz, so when the 1050 runs it at 1MHz the timings are
	// doubled. This is compensated for by wiring the FDC in 8" mode so it thinks it's using
	// 2us FM / 1us MFM timings, which then get doubled back to 4us / 2us.

	static constexpr double kRawBytesPerSecondFM = 1000000.0 / 64.0;
	static constexpr double kRawBytesPerSecondMFM = 1000000.0 / 32.0;
	const double clockFactor = (doubleClock ? 0.5 : 1.0) * mPeriodAdjustFactor;

	const double schRate = mpScheduler->GetRate().asDouble();
	mBytesPerSecondFM = kRawBytesPerSecondFM / clockFactor;
	mBytesPerSecondMFM = kRawBytesPerSecondMFM / clockFactor;
	mCyclesPerByteFM_FX16 = VDRoundToInt(schRate / mBytesPerSecondFM * 0x1p16f);
	mCyclesPerByteMFM_FX16 = VDRoundToInt(schRate / mBytesPerSecondMFM * 0x1p16f);

	UpdateDensity();
}

void ATFDCEmulator::SetDiskImage(IATDiskImage *image, bool diskReady) {
	SetDiskImage(image);
	SetDiskImageReady(diskReady);
}

void ATFDCEmulator::SetDiskImage(IATDiskImage *image) {
	mpDiskImage = image;

	if (image)
		mDiskGeometry = image->GetGeometry();
	else
		mDiskGeometry = {};

	UpdateAutoIndexPulse();

	if (mpRotationTracer)
		mpRotationTracer->SetDiskImage(image);
}

void ATFDCEmulator::SetDiskImageReady(std::optional<bool> diskReady) {
	mbDiskReady = diskReady.value_or(mpDiskImage != nullptr);
}

void ATFDCEmulator::SetDiskInterface(ATDiskInterface *diskIf) {
	if (mpDiskInterface != diskIf) {
		if (mpDiskInterface)
			mpDiskInterface->SetShowActivity(false, 0);

		mpDiskInterface = diskIf;
	}
}

void ATFDCEmulator::SetDiskChangeStartupHackEnabled(bool enabled) {
	if (mbDiskChangeStartupHack != enabled && mbDiskReady) {
		mbDiskChangeStartupHack = enabled;

		if (enabled) {
			mDiskChangeStartupHackBaseTime = mpScheduler->GetTick64();
			mbDiskChangeStartupHackFDCCommandReceived = false;
		}
	}
}

void ATFDCEmulator::SetDDBootSectorMode(DDBootSectorMode mode) {
	mBootSectorMode = mode;
}

void ATFDCEmulator::SetSideMapping(SideMapping mapping, uint32 numTracks) {
	mSideMapping = mapping;
	mSideMappingTrackCount = numTracks;
}

uint8 ATFDCEmulator::DebugReadByte(uint8 address) const {
	switch(address & 3) {
		case 0:
		default: {
			uint8 v = mRegStatus;

			// If the last command was a type I status, read constantly updated status.
			// Note that NOT READY is always up to date, even for type II-III. The 1050
			// relies on this in its idle loop.

			if (mbRegStatusTypeI) {
				v &= 0x39;

				// update write protect
				if (ModifyWriteProtect(mbWriteProtectOverride.value_or(mpDiskImage && !mpDiskInterface->IsDiskWritable())))
					v |= 0x40;

				if (mType == kType_1770 || mType == kType_1772) {
					// spin up completed (type I, 1770/1772 only)
					//
					// we must NOT keep updating this bit -- force interrupt needs to be able
					// to clear it in idle state for the XF551 to report correct status
				} else {
					// head loaded (type I, 1771/179X/279X)
					v &= 0xDF;
					if (mbHeadLoaded)
						v |= 0x20;
				}

				// update track 0
				if (mbTrack0)
					v |= 0x04;

				// update index
				if (mbIndexPulse)
					v |= 0x02;
			} else {
				v &= 0x7F;
			}

			// update not ready / motor on
			if (mType == kType_1770 || mType == kType_1772) {
				if (mbMotorEnabled)
					v |= 0x80;
			} else {
				if (!mbDiskReady)
					v |= 0x80;
				else if (mbDiskChangeStartupHack && !mbDiskChangeStartupHackFDCCommandReceived) {
					const uint64 ticksPassed = mpScheduler->GetTick64() - mDiskChangeStartupHackBaseTime;
					const double secondsPassed = (double)ticksPassed * mpScheduler->GetRate().AsInverseDouble();
					const double blinksPassed = secondsPassed * 20.0;

					if (blinksPassed - floor(blinksPassed) >= 0.5)
						v |= 0x80;
				}

			}

			return v;
		}

		case 1:
			return mRegTrack;

		case 2:
			return mRegSector;

		case 3:
			return mRegData;
	}
}

uint8 ATFDCEmulator::ReadByte(uint8 address) {
	uint8 v = DebugReadByte(address);

	switch(address & 3) {
		case 0:
			// reading status clears IRQ request
			if (mbIrqPending) {
				mbIrqPending = false;

				mpFnIrqChange(false);
			}

			break;

		case 3:
			if (mbDataReadPending) {
				mbDataReadPending = false;

				mpFnDrqChange(false);
			}

			if (g_ATLCFDCData.IsEnabled())
				g_ATLCFDCData("Read byte[%4u]: $%02X\n", mLogDataByteIndex++, v);

			mRegStatus &= ~0x02;
			break;
	}

	return v;
}

void ATFDCEmulator::WriteByte(uint8 address, uint8 value) {
	switch(address & 3) {
		case 0:
		default:
			// check if the controller is busy
			if (mState != kState_Idle) {
				// yes -- only force interrupt is accepted
				if ((value & 0xF0) != 0xD0)
					break;

				g_ATLCFDCCommandFI("Force Interrupt issued -- interrupting command\n");

				// if DRQ is pending, force lost data -- the XF551 is very fast at doing a force
				// interrupt after reading the last byte, but on actual hardware it isn't enough
				// to prevent lost data even though it writes the FI command is under a FM byte's
				// time
				if (mState == kState_ReadSector_TransferByte) {
					// set DRQ
					if (!mbDataReadPending) {
						mbDataReadPending = true;

						mpFnDrqChange(true);
					}

					// force lost data and DRQ
					mRegStatus |= 0x06;
				}

				// abort existing command
				AbortCommand();

				// clear BUSY bit; leave all others
				mRegStatus &= 0xFE;

				// clear activity indicator
				if (mpDiskInterface)
					mpDiskInterface->SetShowActivity(false, 0);

				SetMotorIdleTimer();

				// if immediate interrupt is requested, assert IRQ
				if (value & 0x08) {
					if (!mbIrqPending) {
						mbIrqPending = true;
						mpFnIrqChange(true);
					}
				}
			} else {
				// deassert IRQ
				if (mbIrqPending) {
					mbIrqPending = false;
					mpFnIrqChange(false);
				}

				mRegCommand = value;
				SetTransition(kState_BeginCommand, 1);

				if (value >= 0x80 && (value & 0xF0) != 0xD0)
					mbDiskChangeStartupHackFDCCommandReceived = true;
			}
			break;

		case 1:
			mRegTrack = value;
			break;

		case 2:
			mRegSector = value;
			break;

		case 3:
			g_ATLCFDCData("Write byte: $%02X\n", value);

			mRegData = value;
			if (mbDataWritePending) {
				mbDataWritePending = false;

				mpFnDrqChange(false);

				mRegStatus &= 0xFD;

				if (mState == kState_WriteTrack_InitialDrq) {
					mpScheduler->UnsetEvent(mpStateEvent);
					RunStateMachine();
				}
			}
			break;
	}
}

void ATFDCEmulator::OnIndexPulse(bool asserted) {
	if (mbManualIndexPulse == asserted)
		return;

	mbManualIndexPulse = asserted;

	UpdateIndexPulse();
}

void ATFDCEmulator::SetTraceContext(ATTraceContext *context, uint64 baseTick, double secondsPerTick) {
	if (context) {
		ATTraceCollection *coll = context->mpCollection;
		ATTraceGroup *group = coll->AddGroup(L"FDC");

		mpTraceChannelCommands = group->AddFormattedChannel(baseTick, secondsPerTick, L"Commands");
		
		mpRotationTracer = new ATDiskRotationTracer;
		mpRotationTracer->Init(*mpScheduler, baseTick, mCyclesPerRotation, *group);
		mpRotationTracer->SetDiskImage(mpDiskImage);

		UpdateRotationalPosition();
		mpRotationTracer->SetMotorRunning(mbMotorRunning);
		mpRotationTracer->Warp(mRotPos);
	} else {
		mpRotationTracer->Flush();
		mpRotationTracer->Shutdown();
		mpRotationTracer = nullptr;

		if (mpTraceChannelCommands)
			mpTraceChannelCommands->TruncateLastEvent(mpScheduler->GetTick64());

		mpTraceChannelCommands = nullptr;
	}
}

void ATFDCEmulator::UpdateIndexPulse() {
	const bool indexPulse = mbManualIndexPulse || (mbAutoIndexPulse && mbAutoIndexPulseConnected);
	if (mbIndexPulse == indexPulse)
		return;

	mbIndexPulse = indexPulse;

	if (!indexPulse)
		return;

	++mActiveOpIndexMarks;

	switch(mState) {
		case kState_Idle:
			if (mbHeadLoaded && ++mIdleIndexPulses >= 15) {
				mbHeadLoaded = false;
				mpFnHeadLoadChange(false);
			}
			break;

		case kState_ReadSector_TransferFirstByte:
		case kState_ReadSector_TransferFirstByteNever:
			if (uint32 neededRevs = (mType == kType_1771 ? 2 : 5); mActiveOpIndexMarks >= neededRevs) {
				UpdateRotationalPosition();
				g_ATLCFDC("Timing out read sector/address command -- sector not found after %u revs (pos=%.2f)\n", neededRevs, (mRotations % 100) + (float)mRotPos / (float)mCyclesPerRotation);

				// We need to preserve address CRC errors in the Never case.
				if (mState == kState_ReadSector_TransferFirstByte)
					mActiveSectorStatus = 0x10;

				SetTransition(kState_ReadSector_TransferComplete, 1);
			}
			break;

		case kState_ReadTrack_WaitIndexPulse:
			if (mpDiskInterface)
				mpDiskInterface->SetShowActivity(true, mPhysHalfTrack >> 1);

			SetTransition(kState_ReadTrack_TransferByte, 1);
			break;

		case kState_ReadTrack_TransferByte:
			SetTransition(kState_ReadTrack_Complete, 1);
			break;

		case kState_WriteTrack_WaitIndexPulse:
			if (mpDiskInterface)
				mpDiskInterface->SetShowActivity(true, mPhysHalfTrack >> 1);

			SetTransition(kState_WriteTrack_TransferByte, 1);
			break;

		case kState_WriteTrack_TransferByte:
		case kState_WriteTrack_TransferByteAfterCRC:
			SetTransition(kState_WriteTrack_Complete, 1);
			break;
	}
}

void ATFDCEmulator::OnScheduledEvent(uint32 id) {
	if (id == kEventId_StateMachine) {
		mpStateEvent = nullptr;

		RunStateMachine();
	} else if (id == kEventId_AutoIndexOn) {
		mpAutoIndexOnEvent = nullptr;

		mbAutoIndexPulse = true;

		if (mpFnIndexChange)
			mpFnIndexChange(true);

		UpdateIndexPulse();
		UpdateAutoIndexPulse();
	} else if (id == kEventId_AutoIndexOff) {
		mpAutoIndexOffEvent = nullptr;

		mbAutoIndexPulse = false;

		if (mpFnIndexChange)
			mpFnIndexChange(false);

		UpdateIndexPulse();
		UpdateAutoIndexPulse();
	} else if (id == kEventId_AutoMotorIdle) {
		mpAutoMotorIdleEvent = nullptr;

		if (mbMotorEnabled) {
			mbMotorEnabled = false;
			mpFnMotorChange(false);
		}
	}
}

void ATFDCEmulator::AbortCommand() {
	if (mpTraceChannelCommands)
		mpTraceChannelCommands->TruncateLastEvent(mpScheduler->GetTick64());

	// Firmware may just abort a write track command rather than waiting for the second
	// index mark; we still have to handle the format command.
	if (!mWriteTrackBuffer.empty()) {
		FinalizeWriteTrack();
		mWriteTrackBuffer.clear();
	}

	// If we were in the middle of writing a sector, do partial write now. This is needed by
	// the Happy 810 copier, which interrupts a 4K sector write to force a CRC error.
	if (mState == kState_WriteSector_TransferByte) {
		// force data CRC error
		mActivePhysSectorStatus &= ~0x08;

		if (mpDiskImage && mActivePhysSector < mpDiskImage->GetPhysicalSectorCount() && mpDiskInterface->IsDiskWritable()) {
			try {
				mpDiskImage->WritePhysicalSector(mActivePhysSector, mTransferBuffer, mTransferIndex, mActivePhysSectorStatus);
				mpDiskInterface->OnDiskModified();
			} catch(...) {
				// mark write fault
				mRegStatus |= 0x20;
			}
		} else {
			// mark write fault
			mRegStatus |= 0x20;
		}
	}

	mState = kState_Idle;
	mpScheduler->UnsetEvent(mpStateEvent);
}

void ATFDCEmulator::RunStateMachine() {
	static const char *const kCommandNames[]={
		"restore",
		"seek",
		"step",
		"step",
		"step in",
		"step in",
		"step out",
		"step out",
		"read sector",
		"read sector",
		"write sector",
		"write sector",
		"read address",
		"force interrupt",
		"read track",
		"write track",
	};

	static_assert(vdcountof(kCommandNames) == 16, "array size wrong");

	switch(mState) {
		case kState_Idle:
			break;

		case kState_BeginCommand:
			mLogDataByteIndex = 0;

			switch(mRegCommand & 0xF0) {
				case 0xD0:
					g_ATLCFDCCommandFI("Beginning command $%02X (force interrupt)\n", mRegCommand);
					break;

				case 0x10:
					g_ATLCFDCCommand("Beginning command $%02X (seek) - track %u -> %u (on track %.1f, %s)\n", mRegCommand, mRegTrack, mRegData, (float)mPhysHalfTrack / 2.0f, mbMFM ? "MFM" : "FM");
					break;

				default:
					g_ATLCFDCCommand("Beginning command $%02X (%s) - track %u, side %u, sector %u (on track %.1f, %s)\n", mRegCommand, kCommandNames[mRegCommand >> 4], mRegTrack, mbSide2 ? 1 : 0, mRegSector, (float)mPhysHalfTrack / 2.0f, mbMFM ? "MFM" : "FM");
					break;
			}

			mIdleIndexPulses = 0;

			mRegStatus &= 0x80;
			mRegStatus |= 0x01;

			// clear DRQ if it is set (needed to prevent DRQ leaking from previous cmd)
			if (mbDataReadPending || mbDataWritePending) {
				mbDataReadPending = false;
				mbDataWritePending = false;
				mpFnDrqChange(false);
			}

			if ((mRegCommand & 0xF0) == 0xD0) {
				// change status type immediately
				mbRegStatusTypeI = true;
				mRegStatus |= 0x01;
			} else {

				// change status type immediately
				mbRegStatusTypeI = (mRegCommand < 0x80);

				// spin up motor (1770/1772)
				if (mType == kType_1770 || mType == kType_1772) {
					ClearMotorIdleTimer();

					if (!mbMotorEnabled) {
						mbMotorEnabled = true;
						mpFnMotorChange(true);

						if (!(mRegCommand & 8)) {
							SetTransition(kState_DispatchCommand, mCyclesPerRotation * 6);
							break;
						}
					}
				}

				// load head (179X/279X)
				if (mType == kType_2793 || mType == kType_2797) {
					bool headLoadState = false;

					if (mRegCommand < 0x80) {
						// Type I commands load head if h (bit 3) is set.
						if (mRegCommand & 0x08)
							headLoadState = true;
					} else {
						// Type II/III commands load the head with an optional 15ms delay
						// depending on the E bit (bit 2).
						headLoadState = true;
					}

					if (mbHeadLoaded != headLoadState) {
						mbHeadLoaded = headLoadState;

						mpFnHeadLoadChange(headLoadState);
					}
				}
			}

			mState = kState_DispatchCommand;
			[[fallthrough]];

		case kState_DispatchCommand:
			mActiveOpIndexMarks = 0;

			if (mType == kType_1770 || mType == kType_1772) {
				// spin up completed (type I, 1770/1772 only)
				if (mRegCommand < 0x80)
					mRegStatus |= 0x20;
			} else if (mType == kType_2797) {
				// side select output update for type II/III commands (2797 only)
				switch(mRegCommand & 0xF0) {
					case 0x80: case 0x90:		// read sector
					case 0xA0: case 0xB0:		// write sector
					case 0xC0:					// read address
					case 0xE0:					// read track
					case 0xF0:					// write track
						mbSide2 = (mRegCommand & 0x02) != 0;
						break;

					default:
						break;
				}
			}

			switch(mRegCommand & 0xF0) {
				case 0x00: SetTransition(kState_Restore, 1); break;
				case 0x10: SetTransition(kState_Seek, 1); break;
				case 0x20:
				case 0x30: SetTransition(kState_Step, 1); break;
				case 0x40:
				case 0x50: SetTransition(kState_StepIn, 1); break;
				case 0x60:
				case 0x70: SetTransition(kState_StepOut, 1); break;
				case 0x80:
				case 0x90: SetTransition(kState_ReadSector, 1); break;
				case 0xA0:
				case 0xB0: SetTransition(kState_WriteSector, 1); break;
				case 0xC0: SetTransition(kState_ReadAddress, 1); break;
				case 0xD0: SetTransition(kState_ForceInterrupt, 1); break;
				case 0xE0: SetTransition(kState_ReadTrack, 1); break;
				case 0xF0: SetTransition(kState_WriteTrack, 1); break;
			}

			if (mpTraceChannelCommands) {
				const uint64 t = mpScheduler->GetTick64();

				switch((mRegCommand >> 4) & 15) {
					case 0:
						mpTraceChannelCommands->AddOpenTickEventM(t, kATTraceColor_IO_Default, L"Restore");
						break;
					case 1:
						mpTraceChannelCommands->AddOpenTickEventM(t, kATTraceColor_IO_Default, L"Seek");
						break;
					case 2:
					case 3:
						mpTraceChannelCommands->AddOpenTickEventF(t, kATTraceColor_IO_Default, L"Step %u -> %u", mRegTrack, mRegData);
						break;
					case 4:
					case 5:
						mpTraceChannelCommands->AddOpenTickEventM(t, kATTraceColor_IO_Default, L"Step in");
						break;
					case 6:
					case 7:
						mpTraceChannelCommands->AddOpenTickEventM(t, kATTraceColor_IO_Default, L"Step out");
						break;
					case 8:
					case 9:
						mpTraceChannelCommands->AddOpenTickEventF(t, kATTraceColor_IO_Read, L"Read sector %u:%u", mRegTrack, mRegSector);
						break;
					case 10:
					case 11:
						mpTraceChannelCommands->AddOpenTickEventF(t, kATTraceColor_IO_Write, L"Write sector %u:%u", mRegTrack, mRegSector);
						break;
					case 12:
						mpTraceChannelCommands->AddOpenTickEventF(t, kATTraceColor_IO_Read, L"Read address %u", mRegTrack);
						break;
					case 13:
						mpTraceChannelCommands->AddOpenTickEventM(t, kATTraceColor_IO_Default, L"Force interrupt");
						break;
					case 14:
						mpTraceChannelCommands->AddOpenTickEventF(t, kATTraceColor_IO_Read, L"Read track %u", mRegTrack);
						break;
					case 15:
						mpTraceChannelCommands->AddOpenTickEventF(t, kATTraceColor_IO_Write, L"Write track %u", mRegTrack);
						break;
				}
			}
			break;

		case kState_EndCommand:
			if (mpTraceChannelCommands)
				mpTraceChannelCommands->TruncateLastEvent(mpScheduler->GetTick64());

			// We must NOT clear DRQ here for the XF551 to properly report the DRQ bit.

			mRegStatus &= 0x7E;		// clear BUSY and NOT READY

			if (!mbDiskReady)
				mRegStatus |= 0x80;

			((mRegCommand & 0xF0) == 0xD0 ? g_ATLCFDCCommandFI : g_ATLCFDCCommand)("Ending command $%02X: status=$%02X\n", mRegCommand, mRegStatus);

			mWriteTrackBuffer.clear();

			mState = kState_Idle;

			// Set INTRQ.
			//
			// Force Interrupt with bits 0-3 all clear is a special case -- it is
			// documented as not setting the interrupt.
			if (!mbIrqPending && mRegCommand != 0xD0) {
				mbIrqPending = true;
				mpFnIrqChange(true);
			}

			if (mpDiskInterface)
				mpDiskInterface->SetShowActivity(false, 0);

			SetMotorIdleTimer();
			break;

		case kState_Restore:
			mOpCount = 256;

			SetTransition(kState_Restore_Step, 1);
			break;

		case kState_Restore_Step:
			if (mbTrack0) {
				mRegTrack = 0;
				SetTransition(kState_EndCommand, 1);
			} else if (--mOpCount > 0) {
				SetTransition(kState_Restore_Step, mCycleStepTable[mRegCommand & 3]);
				mpFnStep(false);
			} else {
				// set seek error status bit
				mRegStatus |= 0x10;

				SetTransition(kState_EndCommand, 1);
			}
			break;

		case kState_StepIn:
			if (mRegCommand & 0x10)
				++mRegTrack;

			SetTransition(kState_EndCommand, mCycleStepTable[mRegCommand & 3]);
			mpFnStep(true);
			break;
		case kState_StepOut:
			if (mRegCommand & 0x10)
				--mRegTrack;
			SetTransition(kState_EndCommand, mCycleStepTable[mRegCommand & 3]);
			mpFnStep(false);
			break;

		case kState_Seek:
			if (mRegTrack != mRegData) {
				SetTransition(kState_Seek, mCycleStepTable[mRegCommand & 3]);
				mpFnStep(mRegTrack < mRegData);

				if (mRegTrack < mRegData)
					++mRegTrack;
				else
					--mRegTrack;
			} else {
				SetTransition(kState_EndCommand, 1);
			}
			break;

		case kState_Step:
			SetTransition(kState_EndCommand, 1);
			break;

		case kState_ReadTrack: {
			// clear Lost Data, bits 4-5 per official flowchart, and clear DRQ too
			mRegStatus &= 0xC9;

			// check for disk not ready
			if (!mbDiskReady) {
				mRegStatus |= 0x80;
				SetTransition(kState_EndCommand, 1);
				break;
			}

			// render the track
			const float bytesPerSecond = mbMFM ? mBytesPerSecondMFM : mBytesPerSecondFM;
			mReadTrackBitLength = (uint32)(0.5f + bytesPerSecond * mRotationPeriodSecs * 16.0);

			mReadTrackBuffer.resize((mReadTrackBitLength + 7 + 32) >> 3);


			ATDiskRenderRawTrack(mReadTrackBuffer.data(), mReadTrackBitLength, nullptr, *mpDiskImage, mPhysHalfTrack >> 1, mbMFM, false /*hd*/, true);
			mReadTrackIndex = 0;

			// wait for head load if E=1
			if (mRegCommand & 4) {
				// E=1 wait for head load
				SetTransition(kState_ReadTrack_WaitHeadLoad, mCyclesHeadLoadDelay);
			} else {
				// E=0 don't wait for head load (not measured -- reuse write track timing)
				SetTransition(kState_ReadTrack_WaitHeadLoad, mCyclesWriteTrackFastDelay);
			}
			break;
		}

		case kState_ReadTrack_WaitHeadLoad:
			// wait for next index mark
			mActiveOpIndexMarks = 0;

			mState = kState_ReadTrack_WaitIndexPulse;
			break;

		case kState_ReadTrack_WaitIndexPulse:
			break;

		case kState_ReadTrack_TransferByte: {
			// load next byte from the track buffer
			static constexpr uint8 kPackBits5140[16] {
				0x00, 0x01, 0x10, 0x11,
				0x02, 0x03, 0x12, 0x13,
				0x20, 0x21, 0x30, 0x31,
				0x22, 0x23, 0x32, 0x33,
			};

			const auto compressDataBits = [](uint32 v) constexpr {
				return kPackBits5140[((v >> 16) + (v >> 23)) & 15] + kPackBits5140[((v >> 20) + (v >> 27)) & 15]*4;
			};

			static_assert(compressDataBits(0x00000000) == 0x00);
			static_assert(compressDataBits(0x00010000) == 0x01);
			static_assert(compressDataBits(0x00040000) == 0x02);
			static_assert(compressDataBits(0x00100000) == 0x04);
			static_assert(compressDataBits(0x00400000) == 0x08);
			static_assert(compressDataBits(0x01000000) == 0x10);
			static_assert(compressDataBits(0x04000000) == 0x20);
			static_assert(compressDataBits(0x10000000) == 0x40);
			static_assert(compressDataBits(0x40000000) == 0x80);
			static_assert(compressDataBits(0x55550000) == 0xFF);

			// extract 32 bit window
			const auto extractBitWindow = [this] {
				return VDReadUnalignedBEU32(&mReadTrackBuffer[mReadTrackIndex >> 3]) << (mReadTrackIndex & 7);
			};

			// compress data bits and set data register
			mRegData = compressDataBits(extractBitWindow() & 0x55550000);

			// check for lost data
			if (mbDataReadPending)
				mRegStatus |= 0x04;

			// set DRQ
			if (!mbDataReadPending) {
				mbDataReadPending = true;

				mpFnDrqChange(true);
			}

			mRegStatus |= 0x02;

			// Compute the number of bits to advance.
			//
			// The FDC returns another byte either after 16 bit cells or when a sync mark is detected,
			// whichever is sooner.
			uint32 bitCount = 0;

			for(;;) {
				if (++mReadTrackIndex >= mReadTrackBitLength)
					mReadTrackIndex = 0;

				if (++bitCount >= 16)
					break;

				const uint32 bitWindow = extractBitWindow() >> 16;

				if (mbMFM) {
					// 4489: MFM 'A1' sync (IDAM/DAM)
					// 5224: MFM 'C2' sync (IAM)
					if (bitWindow == 0x4489 || bitWindow == 0x5224)
						break;
				} else {
					if (bitWindow == 0xF56A ||	// FM 'F8' DAM
						bitWindow == 0xF56B ||	// FM 'F9' DAM
						bitWindow == 0xF56E ||	// FM 'FA' DAM
						bitWindow == 0xF56F ||	// FM 'FB' DAM
						bitWindow == 0xF77A ||	// FM 'FC' IAM
						bitWindow == 0xF57E)	// FM 'FE' IDAM
					{
						break;
					}
				}
			}

			// compute number of cycles to next byte returned
			mCyclesPerByteAccum_FX16 += (mCyclesPerByte_FX16 * bitCount + 8) >> 4;
			SetTransition(kState_ReadTrack_TransferByte, mCyclesPerByteAccum_FX16 >> 16);
			mCyclesPerByteAccum_FX16 &= 0xFFFF;
			break;
		}

		case kState_ReadTrack_Complete:
			SetTransition(kState_EndCommand, 1);
			break;

		case kState_WriteTrack:
			// clear DRQ, Lost Data, bits 4-5 per official flowchart
			mRegStatus &= 0xC9;

			// check for disk not ready
			if (!mbDiskReady) {
				mRegStatus |= 0x80;
				SetTransition(kState_EndCommand, 1);
				break;
			}

			// check for write protect
			if (mpDiskImage && mpDiskInterface) {
				bool isFormatBlocked = mbWriteProtectOverride.value_or(!mpDiskInterface->IsFormatAllowed());
				bool isFormatBlockedModified = ModifyWriteProtect(isFormatBlocked);

				if (isFormatBlocked && !isFormatBlockedModified) {
					// disk is write protected and we're pretending it isn't -- try to write enable
					if (mpDiskInterface->TryEnableWrite()) {
						if (mpFnWriteEnabled)
							mpFnWriteEnabled();
					} else {
						// couldn't write enable -- fail out
						mRegStatus |= 0x40;
						SetTransition(kState_EndCommand, 1);
						break;
					}
				} else if (isFormatBlockedModified) {
					// disk is write protected or we're pretending it is -- fail out
					mRegStatus |= 0x40;
					SetTransition(kState_EndCommand, 1);
					break;
				}
			}

			mWriteTrackBuffer.resize(kWriteTrackBufferSize, 0);
			mWriteTrackIndex = 0;

			// wait for head load if E=1
			if (mRegCommand & 4) {
				// E=1 wait for head load
				SetTransition(kState_WriteTrack_WaitHeadLoad, mCyclesHeadLoadDelay);
			} else {
				// E=0 don't wait for head load
				SetTransition(kState_WriteTrack_WaitHeadLoad, mCyclesWriteTrackFastDelay);
			}
			break;

		case kState_WriteTrack_WaitHeadLoad:
			// set DRQ immediately -- 810 relies on being able to write first byte without
			// waiting for first index mark, since it has the RIOT timer IRQ
			// disabled
			if (!mbDataWritePending) {
				mbDataWritePending = true;
				mRegStatus |= 0x02;
				mpFnDrqChange(true);
			}

			// wait three byte times, per official flowchart
			SetTransition(kState_WriteTrack_InitialDrq, mCyclesPerByte * 3);
			break;

		case kState_WriteTrack_InitialDrq:
			// check if initial DRQ was serviced
			if (mbDataWritePending) {
				// no -- set data lost flag and end command
				mRegStatus |= 0x04;
				SetTransition(kState_EndCommand, 1);
				break;
			}

			// wait for next index mark
			mActiveOpIndexMarks = 0;

			mState = kState_WriteTrack_WaitIndexPulse;
			break;

		case kState_WriteTrack_WaitIndexPulse:
			break;

		case kState_WriteTrack_TransferByte:
		case kState_WriteTrack_TransferByteAfterCRC:
			// Check for lost data; WD docs say that $00 is written if data lost.
			if (mbDataWritePending) {
				mRegStatus |= 0x04;
				mRegData = 0;
				mCyclesPerByteAccum_FX16 += mCyclesPerByte_FX16;
				SetTransition(kState_WriteTrack_TransferByte, mCyclesPerByteAccum_FX16 >> 16);
				mCyclesPerByteAccum_FX16 &= 0xFFFF;
			} else {
				// Check the byte that got written for write timing:
				//
				//	$F7 - two CRC bytes
				//	$F8-FB - data address mark (DAM)
				//	$FC - index address mark
				//	$FE - ID address mark (IDAM)
				//
				// A CRC cannot follow another CRC. Attempting to write F7 F7 results in the
				// second F7 simply being written out as a data byte with one-byte timing.
				// A CRC can be written out after that, however, so F7 F7 F7 writes a
				// CRC, an F7, and another CRC.

				if (mRegData == 0xF7 && mState != kState_WriteTrack_TransferByteAfterCRC) {
					mCyclesPerByteAccum_FX16 += mCyclesPerByte_FX16 * 2;
					SetTransition(kState_WriteTrack_TransferByteAfterCRC, mCyclesPerByteAccum_FX16 >> 16);
				} else {
					mCyclesPerByteAccum_FX16 += mCyclesPerByte_FX16;
					SetTransition(kState_WriteTrack_TransferByte, mCyclesPerByteAccum_FX16 >> 16);
				}

				mCyclesPerByteAccum_FX16 &= 0xFFFF;

				mbDataWritePending = true;
				mRegStatus |= 0x02;
				mpFnDrqChange(true);
			}

			mWriteTrackBuffer[mWriteTrackIndex++] = mRegData;
			if (mWriteTrackIndex >= kWriteTrackBufferSize)
				mWriteTrackIndex = 0;
			break;

		case kState_WriteTrack_Complete:
			if (mpDiskImage && !mpDiskInterface->IsFormatAllowed())
				mRegStatus |= 0x40;

			FinalizeWriteTrack();
			SetTransition(kState_EndCommand, 1);
			break;

		case kState_ReadAddress: {
			mTransferIndex = 0;
			mTransferLength = 0;
			mbDataReadPending = false;

			// check for disk not ready
			if (!mbDiskReady) {
				mRegStatus |= 0x80;
				SetTransition(kState_EndCommand, 1);
				break;
			}

			UpdateRotationalPosition();

			uint32 delay = mCyclesPerRotation * 10;		// This just needs to be big enough for 5 index marks to occur.

			mActiveSectorStatus = 0x10;

			if (mpDiskImage && mbMotorRunning && !(mPhysHalfTrack & 1)) {
				// find next sector in rotational order on the entire track
				const float posf = (float)mRotPos / (float)mCyclesPerRotation;
				float bestDistance = 2.0f;
				float bestRotPos = 0;
				uint32 bestVSec = 0;
				uint32 bestVSecCount = 0;
				uint32 bestPhysSec = 0;
				uint32 bestPSecOffset = 0;
				int bestWeakDataOffset = -1;
				uint32 bestSectorSize = 0;

				for(uint32 sector = 1; sector <= mDiskGeometry.mSectorsPerTrack; ++sector) {
					const uint32 vsec = GetSelectedVSec(sector);

					ATDiskVirtualSectorInfo vsi {};

					if (vsec && vsec <= mpDiskImage->GetVirtualSectorCount())
						mpDiskImage->GetVirtualSectorInfo(vsec - 1, vsi);

					if (vsi.mNumPhysSectors) {
						for(uint32 i=0; i<vsi.mNumPhysSectors; ++i) {
							ATDiskPhysicalSectorInfo psi {};

							mpDiskImage->GetPhysicalSectorInfo(vsi.mStartPhysSector + i, psi);

							// toss sector if it doesn't match current density
							if (mbMFM != psi.mbMFM)
								continue;

							// skip missing sectors and sectors with address CRC errors
							if (!(psi.mFDCStatus & 0x10))
								continue;

							float distance = psi.mRotPos - posf;
							distance -= floorf(distance);

							if (distance < bestDistance) {
								bestDistance = distance;
								bestPhysSec = vsi.mStartPhysSector + i;
								bestRotPos = psi.mRotPos;

								// We need uninverted status since we're in the FDC (inversion is
								// handled in the 810 emulation code).
								mActiveSectorStatus = ~psi.mFDCStatus;
								bestWeakDataOffset = psi.mWeakDataOffset;
								bestSectorSize = psi.mPhysicalSize;
								bestVSec = vsec;
								bestVSecCount = vsi.mNumPhysSectors;
								bestPSecOffset = i;
							}
						}
					}
				}

				if (bestVSec) {
					// check for a long sector not actually stored in the image
					if ((mActiveSectorStatus & 0x02) && bestSectorSize < 256)
						bestSectorSize = 256;

					// Synthesize the six bytes returned for the Read Address command.
					PSecAddress addr = VSecToPSec(bestVSec);
					mTransferBuffer[0] = addr.mTrack;
					mTransferBuffer[1] = addr.mSide;
					mTransferBuffer[2] = addr.mSector;
					mTransferBuffer[3] = (bestSectorSize >= 1024 ? 3 : bestSectorSize >= 512 ? 2 : bestSectorSize >= 256 ? 1 : 0);

					// start CRC calculation with sync bytes (MFM only) and IDAM included
					uint16 crc = ATComputeCRC16(mbMFM ? 0xB230 : 0xEF21, mTransferBuffer, 4);

					VDWriteUnalignedBEU16(&mTransferBuffer[4], crc);

					mTransferLength = 6;

					if (mbUseAccurateTiming) {
						// Compute rotational delay. It takes about 26 raw bytes in the standard 810
						// format from start of address to start of first byte, so including the
						// first byte, that's 26*8*4 = 832 cycles of latency until first DRQ.
						delay = VDRoundToInt(bestDistance * (float)mCyclesPerRotation + 26 * mCyclesPerByte);
					} else {
						// Warp disk to start of sector data field.
						mRotPos = VDRoundToInt(bestRotPos * (float)mCyclesPerRotation) + 26 * mCyclesPerByte;

						if (mRotPos >= mCyclesPerRotation)
							mRotPos -= mCyclesPerRotation;

						if (mpRotationTracer)
							mpRotationTracer->Warp(mRotPos);

						// Use short delay.
						delay = 300;
					}

					if (mpDiskInterface)
						mpDiskInterface->SetShowActivity(true, bestVSec);

					if (g_ATLCDisk.IsEnabled() && bestVSec) {
						g_ATLCDisk("Reading address <%02X %02X %02X %02X %02X %02X> vsec=%3d (%d/%d) (trk=%d), psec=%3d, rot=%.2f >> %.2f >> %.2f%s.\n"
								, mTransferBuffer[0]
								, mTransferBuffer[1]
								, mTransferBuffer[2]
								, mTransferBuffer[3]
								, mTransferBuffer[4]
								, mTransferBuffer[5]
								, bestVSec
								, bestPSecOffset + 1
								, bestVSecCount
								, mPhysHalfTrack >> 1
								, (uint32)bestPhysSec
								, (float)mRotPos / (float)mCyclesPerRotation
								, bestRotPos
								, (float)mRotPos / (float)mCyclesPerRotation
								,  bestWeakDataOffset >= 0 ? " (w/weak bits)"
									: (mActiveSectorStatus & 0x04) ? " (w/long sector)"		// must use lost data as DRQ differs between drives
									: (mActiveSectorStatus & 0x08) ? " (w/CRC error)"
									: (mActiveSectorStatus & 0x10) ? " (w/missing data field)"
									: (mActiveSectorStatus & 0x20) ? " (w/deleted sector)"
									: ""
								);
					}

					// Don't flag a CRC without missing record since the data
					// frame is not validated on a Read Address.
					if ((mActiveSectorStatus & 0x18) == 0x08)
						mActiveSectorStatus &= ~0x08;
				}
			}

			if (mTransferLength)
				SetTransition(kState_ReadSector_TransferFirstByte, delay);
			else if (mActiveSectorStatus & 0x10)
				SetTransition(kState_ReadSector_TransferFirstByteNever, delay);
			else
				SetTransition(kState_ReadSector_TransferComplete, delay);
			break;
		}

		case kState_ReadSector: {
			mTransferIndex = 0;
			mTransferLength = 0;
			mbDataReadPending = false;

			UpdateRotationalPosition();

			uint32 delay = mCyclesPerRotation * 10;		// This just needs to be big enough for 5 index marks to occur.

			mActiveSectorStatus = 0x10;

			if (mpDiskImage && mRegSector > 0 && mRegSector <= mDiskGeometry.mSectorsPerTrack && mbMotorRunning && !(mPhysHalfTrack & 1)) {
				uint32 vsec = GetSelectedVSec(mRegSector);

				ATDiskVirtualSectorInfo vsi {};

				if (vsec <= mpDiskImage->GetVirtualSectorCount())
					mpDiskImage->GetVirtualSectorInfo(vsec - 1, vsi);

				if (vsi.mNumPhysSectors) {
					// find next sector in rotational order
					const float posf = (float)mRotPos / (float)mCyclesPerRotation;
					float bestDistance = 2.0f;
					float bestRotPos = 0;
					uint32 bestPhysSec = 0;
					int bestWeakDataOffset = -1;
					bool foundValidAddress = false;

					for(uint32 i=0; i<vsi.mNumPhysSectors; ++i) {
						ATDiskPhysicalSectorInfo psi {};

						mpDiskImage->GetPhysicalSectorInfo(vsi.mStartPhysSector + i, psi);

						// toss sector if density doesn't match
						if (psi.mbMFM != mbMFM)
							continue;

						// Sectors with no data field ($EF) and sectors with a bad address CRC ($E7) are mostly
						// ignored by the FDC, with it continuing to timeout -- exception is whether a bad
						// address CRC is signaled at timeout. This depends on the last address field read.
						if (psi.mFDCStatus & 0x18) {
							if (!foundValidAddress) {
								foundValidAddress = true;
								mActiveSectorStatus = 0x10;
							}
						} else {
							if (!foundValidAddress) {
								mActiveSectorStatus = 0x18;
							}
						}

						// sectors with no data field or a bad address field are skipped by the FDc
						if (!(psi.mFDCStatus & 0x10))
							continue;

						float distance = psi.mRotPos - posf;
						distance -= floorf(distance);

						if (distance < bestDistance) {
							bestDistance = distance;
							bestPhysSec = vsi.mStartPhysSector + i;
							bestRotPos = psi.mRotPos;

							// We need uninverted status since we're in the FDC (inversion is
							// handled in the 810 emulation code).
							mActiveSectorStatus = ~psi.mFDCStatus;
							bestWeakDataOffset = psi.mWeakDataOffset;
							mTransferLength = std::min<uint32>(psi.mPhysicalSize, vdcountof(mTransferBuffer));
						}
					}

					// check for a long sector not actually stored in the image
					if ((mActiveSectorStatus & 0x04) && mTransferLength < 256)
						mTransferLength = 256;

					memset(mTransferBuffer, 0xFF, mTransferLength);
					mpDiskImage->ReadPhysicalSector(bestPhysSec, mTransferBuffer, mTransferLength);

					// check for a boot sector on a double density disk
					if (vsec <= mDiskGeometry.mBootSectorCount && mbMFM && mDiskGeometry.mSectorSize > 128) {
						// extend it to 256 bytes if needed as usually written on physical disk
						if (mTransferLength == 128)
							mTransferLength = 256;

						if (mBootSectorMode == DDBootSectorMode::Swapped)
							std::rotate(mTransferBuffer, mTransferBuffer + 128, mTransferBuffer + 256);
					}

					// apply weak bits
					if (bestWeakDataOffset >= 0) {
						for(uint32 i = (uint32)bestWeakDataOffset; i < mTransferLength; ++i) {
							mTransferBuffer[i] ^= (uint8)mWeakBitLFSR;

							mWeakBitLFSR = (mWeakBitLFSR << 8) + (0xff & ((mWeakBitLFSR >> (28 - 8)) ^ (mWeakBitLFSR >> (31 - 8))));
						}
					}

					// We have to invert the data, because all Atari drives write data inverted.
					for(uint32 i=0; i<mTransferLength; ++i)
						mTransferBuffer[i] = ~mTransferBuffer[i];

					if (mbUseAccurateTiming) {
						// Compute rotational delay. It takes about 26 raw bytes in the standard 810
						// format from start of address to start of first byte, so including the
						// first byte, that's 26*8*4 = 832 cycles of latency until first DRQ.
						delay = VDRoundToInt(bestDistance * (float)mCyclesPerRotation + 26 * mCyclesPerByte);
					} else {
						// Warp disk to start of sector data field.
						mRotPos = VDRoundToInt(bestRotPos * (float)mCyclesPerRotation) + 26 * mCyclesPerByte;

						if (mRotPos >= mCyclesPerRotation)
							mRotPos -= mCyclesPerRotation;

						if (mpRotationTracer)
							mpRotationTracer->Warp(mRotPos);

						// Use short delay.
						delay = 1000;
					}

					if (mpDiskInterface)
						mpDiskInterface->SetShowActivity(true, vsec);

					if (g_ATLCDisk.IsEnabled()) {
						g_ATLCDisk("Reading vsec=%3d (%d/%d) (trk=%d), psec=%3d, rot=%5.2f >>[%+4.2f]>> %.2f.%s%s%s%s%s\n"
								, vsec
								, (uint32)bestPhysSec - vsi.mStartPhysSector + 1
								, vsi.mNumPhysSectors
								, (vsec - 1) / mDiskGeometry.mSectorsPerTrack
								, (uint32)bestPhysSec
								, (float)mRotPos / (float)mCyclesPerRotation + (mRotations % 100)
								, bestDistance
								, bestRotPos
								,  bestWeakDataOffset >= 0 ? " (weak)" : ""
								, (mActiveSectorStatus & 0x04) ? " (long)" : ""		// must use lost data as DRQ differs between drives
								, ((mActiveSectorStatus & 0x18) == 0x18) ? " (address CRC error)" : ""
								, (mActiveSectorStatus & 0x08) ? " (data CRC error)" : ""
								, ((mActiveSectorStatus & 0x10) == 0x10) ? " (missing data field)" : ""
								, (mActiveSectorStatus & 0x20) ? " (deleted sector)" : ""
								);
					}
				}
			}

			if (mTransferLength)
				SetTransition(kState_ReadSector_TransferFirstByte, delay);
			else if (mActiveSectorStatus & 0x10)
				SetTransition(kState_ReadSector_TransferFirstByteNever, delay);
			else
				SetTransition(kState_ReadSector_TransferComplete, delay);
			break;
		}

		case kState_ReadSector_TransferFirstByteNever:
			// we just sit here and wait for 5 index marks
			break;

		case kState_ReadSector_TransferFirstByte:
			// Immediately transfer record type bit into status register, as this will get
			// updated as soon as the DAM is processed, and needs to stick if the read sector
			// command is interrupted. Otherwise, the XF551 fails to report the deleted DAM
			// for a deleted + CRC sector.
			if (mType == kType_1771)
				mRegStatus = (mRegStatus & ~0x60) + (mActiveSectorStatus & 0x20)*3;
			else
				mRegStatus = (mRegStatus & ~0x20) + (mActiveSectorStatus & 0x20);

			[[fallthrough]];

		case kState_ReadSector_TransferByte:
			// check for lost data
			if (mbDataReadPending)
				mRegStatus |= 0x04;

			// set DRQ
			if (!mbDataReadPending) {
				mbDataReadPending = true;

				mpFnDrqChange(true);
			}

			mRegStatus |= 0x02;

			mRegData = mTransferBuffer[mTransferIndex];
			if (++mTransferIndex >= mTransferLength)
				SetTransition(kState_ReadSector_TransferComplete, mCyclesPerByte * 2);
			else {
				mCyclesPerByteAccum_FX16 += mCyclesPerByte_FX16;
				SetTransition(kState_ReadSector_TransferByte, mCyclesPerByteAccum_FX16 >> 16);
				mCyclesPerByteAccum_FX16 &= 0xFFFF;
			}
			break;

		case kState_ReadSector_TransferComplete:
			// Update status based on sector read:
			//
			//	bit 7 (not ready)			drop - recomputed
			//	bit 6 (record type)			drop - replicated from bit 5 (1771) or cleared (others)
			//	bit 5 (record type)			replicate to bit 5/6 (1771 only)
			//	bit 4 (record not found)	keep
			//	bit 3 (CRC error)			keep
			//	bit 2 (lost data)			drop - driven by sequencer
			//	bit 1 (DRQ)					drop - driven by sequencer
			//	bit 0 (busy)				drop - driven by sequencer
			mRegStatus = (mRegStatus & ~0x78) + (mActiveSectorStatus & 0x38);

			if (mType == kType_1771)
				mRegStatus += (mRegStatus & 0x20) * 2;

			// If this is a Read Address command, we need to copy the track address
			// into the sector register, per the WDC manual.
			if ((mRegCommand & 0xF0) == 0xC0)
				mRegSector = mTransferBuffer[0];

			SetTransition(kState_EndCommand, 1);
			break;

		case kState_WriteSector: {
			mTransferIndex = 0;
			mTransferLength = 0;

			// reset WP, RT, RNF, CRC, LD, and DRQ per start of type II flowchart
			mRegStatus &= 0x81;

			UpdateRotationalPosition();

			const bool isWriteProtected = mbWriteProtectOverride.value_or(mpDiskInterface && !mpDiskInterface->IsDiskWritable());
			const bool isWriteProtectedModified = ModifyWriteProtect(isWriteProtected);

			if (isWriteProtected && !isWriteProtectedModified) {
				// disk is write protected, but we want to pretend it isn't -- write to enable writes
				if (mpDiskInterface && mpDiskInterface->TryEnableWrite()) {
					if (mpFnWriteEnabled)
						mpFnWriteEnabled();
				} else {
					// failed
					mRegStatus |= 0x40;
					SetTransition(kState_EndCommand, 1);
					break;
				}
			} else if (isWriteProtectedModified) {
				// disk is write protected or we're pretending it is -- fail
				mRegStatus |= 0x40;
				SetTransition(kState_EndCommand, 1);
				break;
			}

			uint32 delay = mCyclesPerRotation * 2;

			mActiveSectorStatus = 0x10;
			mActivePhysSectorStatus = 0xFF;

			if (mpDiskImage && mRegSector > 0 && mRegSector <= mDiskGeometry.mSectorsPerTrack && mbMotorRunning && !(mPhysHalfTrack & 1)) {
				uint32 vsec = GetSelectedVSec(mRegSector);

				ATDiskVirtualSectorInfo vsi {};

				if (vsec <= mpDiskImage->GetVirtualSectorCount())
					mpDiskImage->GetVirtualSectorInfo(vsec - 1, vsi);

				if (vsi.mNumPhysSectors) {
					// find next sector in rotational order
					const float posf = (float)mRotPos / (float)mCyclesPerRotation;
					float bestDistance = 2.0f;
					float bestRotPos = 0;
					uint32 bestPhysSec = 0;
					int bestWeakDataOffset = -1;
					bool foundValidAddressField = false;

					for(uint32 i=0; i<vsi.mNumPhysSectors; ++i) {
						ATDiskPhysicalSectorInfo psi {};

						mpDiskImage->GetPhysicalSectorInfo(vsi.mStartPhysSector + i, psi);

						// toss sector if it doesn't match current density
						if (psi.mbMFM != mbMFM)
							continue;

						// Sectors with no data field are fine, as the FDC writes a new DAM and
						// data field anyway -- they will get fixed. But sectors with a bad address field
						// aren't, those will cause the FDC to time out with bits 3 and 4 set (note that
						// our status is inverted here).
						if ((psi.mFDCStatus & 0x18) == 0) {
							if (!foundValidAddressField)
								mActiveSectorStatus = 0x18;

							continue;
						}

						foundValidAddressField = true;

						float distance = psi.mRotPos - posf;
						distance -= floorf(distance);

						if (distance < bestDistance) {
							bestDistance = distance;
							bestPhysSec = vsi.mStartPhysSector + i;
							bestRotPos = psi.mRotPos;

							// We need uninverted status since we're in the FDC (inversion is
							// handled in the 810 emulation code).
							mActiveSectorStatus = ~psi.mFDCStatus;

							// clear RNF bit if set, as a missing data field can't occur during a write, and
							// we have a valid address field at this point.
							mActiveSectorStatus &= 0xEF;

							bestWeakDataOffset = psi.mWeakDataOffset;
							mTransferLength = std::min<uint32>(psi.mPhysicalSize, vdcountof(mTransferBuffer));
						}
					}

					mActiveVirtSector = vsec;
					mActivePhysSector = bestPhysSec;

					// check for a long sector not actually stored in the image
					if ((mActiveSectorStatus & 0x04) && mTransferLength < 256)
						mTransferLength = 256;

					// check for non-standard sector size mode being used
					if (mType == kType_1771) {
						if (!(mRegCommand & 0x08)) {
							// force a CRC error on the written sector
							mActivePhysSectorStatus &= ~0x08;

							mTransferLength = 4096;
						}
					}

					// copy written data mark (we only use LSB as diskimage uses 1050 rules)
					if (mRegCommand & 0x01) {
						mActivePhysSectorStatus &= ~0x20;
					}

					if (mbUseAccurateTiming) {
						// Compute rotational delay. It takes about 26 raw bytes in the standard 810
						// format from start of address to start of first byte, so including the
						// first byte, that's 26*8*4 = 832 cycles of latency until first DRQ.
						delay = VDRoundToInt(bestDistance * (float)mCyclesPerRotation + 832);
					} else {
						// Warp disk to start of sector data field.
						mRotPos = VDRoundToInt(bestRotPos * (float)mCyclesPerRotation) + 832;

						if (mRotPos >= mCyclesPerRotation)
							mRotPos -= mCyclesPerRotation;

						if (mpRotationTracer)
							mpRotationTracer->Warp(mRotPos);

						// Use short delay.
						delay = 1000;
					}

					if (mpDiskInterface)
						mpDiskInterface->SetShowActivity(true, vsec);

					if (g_ATLCDisk.IsEnabled()) {
						g_ATLCDisk("Writing vsec=%3d (%d/%d) (trk=%d), psec=%3d, rot=%.2f >> [+%.2f] >> %.2f%s.\n"
								, vsec
								, (uint32)bestPhysSec - vsi.mStartPhysSector + 1
								, vsi.mNumPhysSectors
								, (vsec - 1) / mDiskGeometry.mSectorsPerTrack
								, (uint32)bestPhysSec
								, posf
								, bestDistance
								, bestRotPos
								,  bestWeakDataOffset >= 0 ? " (w/weak bits)"
									: (mActiveSectorStatus & 0x04) ? " (w/long sector)"		// must use DRQ as lost data differs between drives
									: (mActiveSectorStatus & 0x08) ? " (w/CRC error)"
									: (mActiveSectorStatus & 0x10) ? " (w/missing data field)"
									: (mActiveSectorStatus & 0x20) ? " (w/deleted sector)"
									: ""
								);
					}
				}
			}

			if (mActiveSectorStatus & 0x10)
				SetTransition(kState_WriteSector_TransferComplete, delay);
			else
				SetTransition(kState_WriteSector_InitialDrq, delay);
			break;
		}

		case kState_WriteSector_InitialDrq:
			// Set DRQ and wait 9 bytes
			mbDataWritePending = true;
			mRegStatus |= 0x02;
			mpFnDrqChange(true);
			SetTransition(kState_WriteSector_CheckInitialDrq, 9 * mCyclesPerByte);
			break;

		case kState_WriteSector_CheckInitialDrq:
			if (mbDataWritePending) {
				// No initial data -- signal lost data and abort the command immediately
				mRegStatus |= 0x04;
				SetTransition(kState_WriteSector_TransferComplete, 1);
			} else {
				// Write DAM; DRQ does not get set again until after this occurs
				SetTransition(kState_WriteSector_TransferByte, 2 * mCyclesPerByte);
			}
			break;

		case kState_WriteSector_TransferByte:
			// Check for lost data and transfer to disk buffer; WD docs say that $00 is
			// written if data lost. Note that we need to invert the data as the disk image
			// interface expects computer-type noninverted data.
			if (mbDataWritePending) {
				if (!(mRegStatus & 0x04)) {
					g_ATLCFDC("Lost data condition detected during write at offset %u -- stuffing $00\n", mTransferIndex);

					mRegStatus |= 0x04;
				}

				mTransferBuffer[mTransferIndex] = 0xFF;
			} else
				mTransferBuffer[mTransferIndex] = ~mRegData;

			// check if we have more bytes to write
			++mTransferIndex;
			if (mTransferIndex < mTransferLength) {
				// Yes, we do -- set DRQ, then wait a byte's time while this byte is
				// being written
				if (!mbDataWritePending) {
					mbDataWritePending = true;
					mRegStatus |= 0x02;

					mpFnDrqChange(true);
				}

				SetTransition(kState_WriteSector_TransferByte, mCyclesPerByte);
			} else {
				// No, we don't. Write the sector to persistent storage now, and delay four
				// bytes for the write timing for the remaining data (byte just transferred,
				// CRC 1 and 2, and $FF). Recheck the write state in case it was changed in
				// between.
				if (mpDiskImage && mActivePhysSector < mpDiskImage->GetPhysicalSectorCount() && mpDiskInterface->IsDiskWritable()) {
					// check if this is a boot sector and we're in swapped mode -- if so,
					// we need to swap the sector data around
					const auto& geo = mpDiskImage->GetGeometry();

					if (mTransferLength == 256 && mActiveVirtSector < geo.mBootSectorCount && geo.mSectorSize > 128) {
						if (mBootSectorMode == DDBootSectorMode::Swapped)
							std::rotate(mTransferBuffer, mTransferBuffer + 128, mTransferBuffer + 256);
					}

					try {
						mpDiskImage->WritePhysicalSector(mActivePhysSector, mTransferBuffer, mTransferLength, mActivePhysSectorStatus);
						mpDiskInterface->OnDiskModified();
					} catch(...) {
						// mark write fault
						mRegStatus |= 0x20;
					}
				} else {
					// mark write fault
					mRegStatus |= 0x20;
				}

				SetTransition(kState_WriteSector_TransferComplete, mCyclesPerByte * 4);
			}
			break;

		case kState_WriteSector_TransferComplete:
			// Update status based on sector write:
			//
			//	bit 7 (not ready)			drop - recomputed
			//	bit 6 (write protect)		drop - recomputed
			//	bit 5 (write fault)			drop - recomputed
			//	bit 4 (record not found)	keep
			//	bit 3 (CRC error)			keep
			//	bit 2 (lost data)			drop - driven by sequencer
			//	bit 1 (DRQ)					drop - driven by sequencer
			//	bit 0 (busy)				drop - driven by sequencer
			mRegStatus = (mRegStatus & ~0x18) + (mActiveSectorStatus & 0x18);

			SetTransition(kState_EndCommand, 1);
			break;

		case kState_ForceInterrupt:
			// The XF551 needs the motor spin-up bit cleared and the motor on bit set for a
			// idle force interrupt to report status properly. This means, incidentally, that
			// the XF551 doesn't ever report deleted record or record not found!

			mRegStatus |= 0x10;
			mRegStatus &= ~0xDF;

			SetTransition(kState_EndCommand, 16);
			break;
	}
}

void ATFDCEmulator::SetTransition(State nextState, uint32 ticks) {
	mState = nextState;
	mpScheduler->SetEvent(ticks, this, kEventId_StateMachine, mpStateEvent);
}

void ATFDCEmulator::UpdateRotationalPosition() {
	const uint64 t = mpScheduler->GetTick64();

	if (mbMotorRunning) {
		uint64 newPos = mRotPos + (t - mRotTimeBase);
		mRotPos = newPos % mCyclesPerRotation;
		mRotations += (uint32)(newPos / mCyclesPerRotation);
	}

	mRotTimeBase = t;
}

void ATFDCEmulator::UpdateAutoIndexPulse() {
	if (mbMotorRunning && mbAutoIndexPulseEnabled && mpDiskImage != nullptr) {
		if (!mpAutoIndexOnEvent && !mpAutoIndexOffEvent) {
			UpdateRotationalPosition();

			if (mRotPos < mCyclesPerIndexPulse)
				mpAutoIndexOffEvent = mpScheduler->AddEvent(mCyclesPerIndexPulse - mRotPos, this, kEventId_AutoIndexOff);
			else
				mpAutoIndexOnEvent = mpScheduler->AddEvent(mCyclesPerRotation - mRotPos, this, kEventId_AutoIndexOn);
		}
	} else {
		if (mbAutoIndexPulse) {
			mbAutoIndexPulse = false;
			UpdateIndexPulse();
		}

		mpScheduler->UnsetEvent(mpAutoIndexOnEvent);
		mpScheduler->UnsetEvent(mpAutoIndexOffEvent);
	}
}

void ATFDCEmulator::SetMotorIdleTimer() {
	if ((mType != kType_1770 && mType != kType_1772) || !mbMotorEnabled)
		return;

	mpScheduler->SetEvent(mCyclesPerRotation * 9, this, kEventId_AutoMotorIdle, mpAutoMotorIdleEvent);
}

void ATFDCEmulator::ClearMotorIdleTimer() {
	if (mType != kType_1770 && mType != kType_1772)
		return;

	mpScheduler->UnsetEvent(mpAutoMotorIdleEvent);
}

void ATFDCEmulator::UpdateDensity() {
	mCyclesPerByte_FX16 = mbMFM ? mCyclesPerByteMFM_FX16 : mCyclesPerByteFM_FX16;
	mCyclesPerByte = (mCyclesPerByte_FX16 + 0x8000) >> 16;
}

void ATFDCEmulator::FinalizeWriteTrack() {
	if (!mpDiskImage || !mpDiskInterface->IsFormatAllowed())
		return;

	// compute the number of bytes we can fit in the track, and thus when the track will wrap -- we expect
	// this to occur for most drives, which will deliberately overrun the track to ensure that it is fully
	// written
	const float bytesPerSec = mbMFM ? mBytesPerSecondMFM : mBytesPerSecondFM;

	const uint32 maxBytesPerTrack = (uint32)(mRotationPeriodSecs * bytesPerSec);

	const uint32 endPos = mWriteTrackIndex;

	// $F7 bytes actually produce two raw bytes, so we can easily have more than the allowed
	// number of bytes in the track. Work backwards from the end until we have the max number
	// of bytes; everything before that would have been overwritten by track wrap.
	uint32 startPos = endPos;
	uint32 extraCrcLen = 0;
	uint32 validLen = 0;

	while(validLen + extraCrcLen < maxBytesPerTrack) {
		if (startPos == 0)
			startPos = kWriteTrackBufferSize;
		--startPos;

		++validLen;
		if (mWriteTrackBuffer[startPos] == 0xF7)
			++extraCrcLen;
	}

	if (g_ATLCFDCWTData.IsEnabled() && endPos > 0) {
		g_ATLCFDCWTData("Estimating %u raw bytes per track (%s encoding, %.1f RPM%s)\n", maxBytesPerTrack, mbMFM ? "MFM" : "FM", 60.0f / mRotationPeriodSecs, mbDoubleClock ? ", double clock" : "");

		uint32 crcs = 0;
		sint32 lastIDAM = -1;

		VDStringA s;
		for(uint32 i=0; i<endPos; ++i) {
			const uint8 c = mWriteTrackBuffer[i];

			if (i == startPos) {
				g_ATLCFDCWTData("--- track wrap point ---\n");
			}

			const char *desc = nullptr;
			uint32 count = 1;

			if (mbMFM) {
				switch(c) {
					case 0xF5:	desc = "  sync ($A1)"; break;
					case 0xF6:	desc = "  index sync ($C2)"; break;
					case 0xF7:	desc = "  CRC"; break;
					case 0xF8:	desc = "  DAM (deleted)"; break;
					case 0xF9:	desc = "  DAM (user defined)"; break;
					case 0xFA:	desc = "  DAM (user defined)"; break;
					case 0xFB:	desc = "  DAM"; break;
					case 0xFE:	desc = "  IDAM"; break;
					default:	break;
				}
			} else {
				switch(c) {
					case 0xF7:	desc = "  CRC"; break;
					case 0xF8:	desc = "  DAM (deleted)"; break;
					case 0xF9:	desc = "  DAM (user defined)"; break;
					case 0xFA:	desc = "  DAM (user defined)"; break;
					case 0xFB:	desc = "  DAM"; break;
					case 0xFC:	desc = "  index mark"; break;
					case 0xFE:	desc = "  IDAM"; break;
					default:	break;
				}
			}

			// see if we can spot a run or a pairwise run
			uint32 maxCount = endPos - i;

			// don't allow runs to cross the track overwrite point
			if (i < startPos)
				maxCount = std::min<uint32>(maxCount, startPos - i);

			while(count < maxCount && mWriteTrackBuffer[i + count] == c)
				++count;

			// if we couldn't detect a run, check if we can get a pairwise run
			uint32 pairCount = 1;
			uint8 d = 0;

			if (count == 1 && maxCount >= 2 && !desc) {		// avoid pairwise runs for specials
				uint32 maxPairCount = maxCount >> 1;

				d = mWriteTrackBuffer[i + 1];

				while(pairCount < maxPairCount && mWriteTrackBuffer[i + pairCount*2] == c && mWriteTrackBuffer[i + pairCount*2 + 1] == d)
					++pairCount;
			}

			if (pairCount > 1) {
				s.sprintf("%3u x $%02X%02X", pairCount, c, d);
				i += pairCount*2 - 1;
			} else {
				s.sprintf("%3u x $%02X", count, c);
				i += count - 1;
			}

			if (c == 0xFE) {
				const sint32 pos = (sint32)(i + crcs);

				if (i + 6 < endPos)
					s.append_sprintf(" track %u, sector %u", mWriteTrackBuffer[i+1], mWriteTrackBuffer[i+3]);

				if (lastIDAM >= 0) {
					const sint32 rawSectorLen = pos - lastIDAM;
					const float msPerByte = 1000.0f / bytesPerSec;

					s.append_sprintf(" (+%u encoded bytes since last / %.3f ms per sector)", rawSectorLen, (float)rawSectorLen * msPerByte );
				}

				lastIDAM = pos;
			}

			if (c == 0xF7)
				++crcs;

			s += '\n';

			g_ATLCFDCWTData <<= s.c_str();
		}

		g_ATLCFDCWTData("Total %u bytes written (%u raw bytes)\n", endPos, endPos + crcs);
	}

	// Rotate the buffer around so the valid area is contiguous.
	std::rotate(mWriteTrackBuffer.begin(), mWriteTrackBuffer.begin() + startPos, mWriteTrackBuffer.end());

	// Parse out the valid sectors.
	enum ParseState {
		kParseState_WaitForSync,
		kParseState_WaitForIdAddressMark,
		kParseState_ProcessIdRecord,
		kParseState_WaitForDataAddressMarkSync,
		kParseState_WaitForDataAddressMarkSync1,
		kParseState_WaitForDataAddressMarkSync2,
		kParseState_WaitForDataAddressMark,
		kParseState_ProcessDataRecord,
	};

	ParseState parseState = kParseState_WaitForSync;
	uint32 parseStateCounter = 0;
	uint32 parseSectorLength = 0;
	uint8 idField[5] = {};

	struct ParsedSector {
		uint8 mId;
		uint32 mStart;
		uint32 mLen;
	};

	vdfastvector<ParsedSector> parsedSectors;
	const uint32 syncLimit = (mbMFM ? 43 : 30);

	g_ATLCFDC("Start sector parse:\n");

	const uint8 *const src = mWriteTrackBuffer.data();
	for(uint32 i = 0; i < validLen; ++i) {
		const uint8 c = src[i];

		switch(parseState) {
			case kParseState_WaitForSync:
				if (mbMFM) {
					if (c == 0xF5) {
						if (++parseStateCounter == 3)
							parseState = kParseState_WaitForIdAddressMark;
					} else
						parseStateCounter = 0;
				} else {
					if (c == 0x00)
						parseState = kParseState_WaitForIdAddressMark;
				}
				break;

			case kParseState_WaitForIdAddressMark:
				if (c == 0xFE) {
					parseState = kParseState_ProcessIdRecord;
					parseStateCounter = 0;
				} else {
					if (mbMFM) {
						parseState = kParseState_WaitForSync;
						parseStateCounter = (c == 0xF5) ? 1 : 0;
					} else {
						if (c != 0x00) {
							parseState = kParseState_WaitForSync;
							parseStateCounter = 0;
						}
					}
				}
				break;

			case kParseState_ProcessIdRecord:
				idField[parseStateCounter++] = c;

				if (parseStateCounter >= 5) {
					parseState = kParseState_WaitForSync;
					parseStateCounter = 0;

					// check if track field is valid
					if (idField[0] != (mPhysHalfTrack >> 1))
						break;

					// check if head, sector, and sector length fields are valid
					if ((uint8)(idField[1] + 1) >= 0xF8
						|| (uint8)(idField[2] + 1) >= 0xF8
						|| (uint8)(idField[3] + 1) >= 0xF8)
						break;

					// check if CRC is valid
					if (idField[4] != 0xF7)
						break;

					parseSectorLength = 128 << (idField[3] & 0x03);

					parseState = kParseState_WaitForDataAddressMarkSync;

					// The Happy copier does something very evil, which is that it writes out
					// the ID address field but not the data field, instead just padding out
					// a bunch of $00s, relying on subsequent Write Sector commands to write
					// the DAM. This means we have to establish the sectors just based on the
					// ID alone.
					parsedSectors.push_back({ idField[2], i, parseSectorLength });
				}
				break;

			case kParseState_WaitForDataAddressMarkSync:
				if (++parseStateCounter > syncLimit) {
					parseState = kParseState_WaitForSync;
					parseStateCounter = 0;
				} else if (mbMFM) {
					if (c == 0xF5)
						parseState = kParseState_WaitForDataAddressMarkSync1;
				} else {
					if (c == 0x00)
						parseState = kParseState_WaitForDataAddressMark;
				}
				break;

			case kParseState_WaitForDataAddressMarkSync1:
				if (++parseStateCounter > syncLimit) {
					parseState = kParseState_WaitForSync;
					parseStateCounter = 0;
				} else {
					if (c == 0xF5)
						parseState = kParseState_WaitForDataAddressMarkSync2;
					else
						parseState = kParseState_WaitForDataAddressMarkSync;
				}
				break;

			case kParseState_WaitForDataAddressMarkSync2:
				if (++parseStateCounter > syncLimit) {
					parseState = kParseState_WaitForSync;
					parseStateCounter = 0;
				} else {
					if (c == 0xF5)
						parseState = kParseState_WaitForDataAddressMark;
					else
						parseState = kParseState_WaitForDataAddressMarkSync;
				}
				break;

			case kParseState_WaitForDataAddressMark:
				if (++parseStateCounter > syncLimit) {
					parseState = kParseState_WaitForSync;
					parseStateCounter = 0;
				} else if ((c & 0xFC) == 0xF8) {
					parseState = kParseState_ProcessDataRecord;
					parseStateCounter = 0;

					// fix up data offset for last sector
					parsedSectors.back().mStart = i + 1;
				} else if (mbMFM) {
					if (c == 0xF5)
						parseState = kParseState_WaitForDataAddressMarkSync1;
					else
						parseState = kParseState_WaitForDataAddressMarkSync;
				} else {
					if (c != 0x00)
						parseState = kParseState_WaitForDataAddressMarkSync;
				}
				break;

			case kParseState_ProcessDataRecord:
				if (parseStateCounter < parseSectorLength) {
					if (c < 0xF7 || c == 0xFF) {
						++parseStateCounter;
						break;
					}
				} else {
					if (c == 0xF7) {
						g_ATLCFDC("Found sector %u (%u bytes)\n", idField[2], 128 << idField[3]);
					}
				}

				parseState = kParseState_WaitForSync;
				parseStateCounter = 0;
				break;
		};
	}

	// Try to guess the overall format of the track.
	uint32 sectorsPerTrack = 18;
	uint32 bytesPerSector = 128;

	if (mbDoubleClock) {
		for(const auto& psec : parsedSectors) {
			if (psec.mLen > 128)
				bytesPerSector = 256;

			if (psec.mId > 18)
				sectorsPerTrack = 26;
		}
	} else if (mbMFM) {
		bytesPerSector = 256;

		uint32 maxId = 0;
		uint32 maxSize = 0;
		uint32 minSize = ~0;
		for(const auto& psec : parsedSectors) {
			maxId = std::max<uint32>(maxId, psec.mId);
			maxSize = std::max<uint32>(maxSize, psec.mLen);
			minSize = std::min<uint32>(minSize, psec.mLen);
		}

		if (minSize == 512 && maxSize == 512 && maxId && maxId <= 9) {
			bytesPerSector = 512;
			sectorsPerTrack = maxId;
		} else if (maxId > 18) {
			bytesPerSector = 128;
			sectorsPerTrack = 26;
		}
	}

	g_ATLCFDC("End track with %u sectors - using geometry: %u sectors of %u bytes\n", (unsigned)parsedSectors.size(), sectorsPerTrack, bytesPerSector);

	// Bin the physical sectors according to virtual sectors.
	ATDiskVirtualSectorInfo newVirtSectors[26] = {};
	vdfastvector<ATDiskPhysicalSectorInfo> newPhysSectors;
	newPhysSectors.reserve(parsedSectors.size());

	if (!parsedSectors.empty()) {
		UpdateRotationalPosition();

		for(uint32 i=0; i<sectorsPerTrack; ++i) {
			newVirtSectors[i].mStartPhysSector = (uint32)newPhysSectors.size();

			// if we're on side 2 of a disk that uses reversed sector ordering, we must reverse
			// the order of virtual sectors on this track
			uint32 psecId = i + 1;

			if (mbSide2 && mSideMapping != SideMapping::Side2Forward && mSideMapping != SideMapping::Side2ReversedTracks)
				psecId = sectorsPerTrack - i;

			for(const auto& parsedSector : parsedSectors) {
				if (parsedSector.mId != psecId)
					continue;

				++newVirtSectors[i].mNumPhysSectors;

				ATDiskPhysicalSectorInfo psi {};

				psi.mbDirty = true;
				psi.mDiskOffset = -1;
				psi.mFDCStatus = 0xFF;
				psi.mOffset = parsedSector.mStart;
				psi.mRotPos = ((float)mRotPos - validLen + (float)parsedSector.mStart * mCyclesPerByte) / (float)mCyclesPerRotation;
				psi.mRotPos -= floorf(psi.mRotPos);
				psi.mPhysicalSize = parsedSector.mLen;
				psi.mImageSize = parsedSector.mLen;
				psi.mWeakDataOffset = -1;
				psi.mbMFM = mbMFM;

				newPhysSectors.push_back(psi);
			}
		}
	}

	// Invert the track data, since the disk image is inverted from the FDC (thanks to the 810).
	for(uint8& c : mWriteTrackBuffer)
		c = ~c;

	// Check if the disk image has the geometry that we're expecting. If not, nuke it from orbit
	// and replace it with a new image. Note that the disk change notification will recurse back
	// into us to change the disk image pointer, so careful.
	const auto& geo = mpDiskImage->GetGeometry();

	bool isHD = mbDoubleClock && mPeriodAdjustFactor < 1.5f;

	if (geo.mSectorSize != bytesPerSector || geo.mbMFM != mbMFM || geo.mSectorsPerTrack != sectorsPerTrack || geo.mbHighDensity != isHD) {
		ATDiskGeometryInfo newGeometry = {};
		newGeometry.mSectorSize = bytesPerSector;
		newGeometry.mBootSectorCount = bytesPerSector > 256 ? 0 : 3;
		newGeometry.mTrackCount = 40;
		newGeometry.mSectorsPerTrack = sectorsPerTrack;
		newGeometry.mSideCount = 1;
		newGeometry.mbMFM = mbMFM;
		newGeometry.mbHighDensity = isHD;
		newGeometry.mTotalSectorCount = newGeometry.mTrackCount * newGeometry.mSideCount * newGeometry.mSectorsPerTrack;

		mpDiskInterface->FormatDisk(newGeometry);
	}

	mpDiskImage->FormatTrack(GetSelectedVSec(0), sectorsPerTrack, newVirtSectors, (uint32)newPhysSectors.size(), newPhysSectors.data(), mWriteTrackBuffer.data());
	mpDiskInterface->OnDiskChanged(false);
}

uint32 ATFDCEmulator::GetSelectedVSec(uint32 sector) const {
	uint32 vtrack = mPhysHalfTrack >> 1;

	switch(mSideMapping) {
		case SideMapping::Side2Reversed:
			if (mbSide2) {
				vtrack = (mSideMappingTrackCount * 2 - 1) - vtrack;

				if (sector && sector <= mDiskGeometry.mSectorsPerTrack)
					sector = (mDiskGeometry.mSectorsPerTrack + 1) - sector;
			}
			break;

		case SideMapping::Side2ReversedOffByOne:
			// The PERCOM RFD-40S1 firmware has a bug in the way that it computes physical track/sector addresses
			// on side 2 -- it computes vsec' = (total - vsec) instead of vsec' = (total+1 - vsec). On a double-sided
			// disk with 720 x 2 = 1440 sectors, this maps vsecs 721-1440 to 719-0 instead of 720-1. We compensate
			// for this the best that we can, though we can't fix the bug causing the last sector to be unusable.
			if (mbSide2) {
				vtrack = (mSideMappingTrackCount * 2 - 1) - vtrack;

				if (sector && sector <= mDiskGeometry.mSectorsPerTrack)
					sector = (mDiskGeometry.mSectorsPerTrack + 1) - sector;

				--sector;
			}
			break;

		case SideMapping::Side2ReversedTracks:
			if (mbSide2)
				vtrack = (mSideMappingTrackCount * 2 - 1) - vtrack;
			break;

		case SideMapping::Side2Forward:
			if (mbSide2)
				vtrack += mSideMappingTrackCount;
			break;
	}

	return vtrack * mDiskGeometry.mSectorsPerTrack + sector;
}

ATFDCEmulator::PSecAddress ATFDCEmulator::VSecToPSec(uint32 vsec) const {
	uint8 track = (vsec - 1) / mDiskGeometry.mSectorsPerTrack;
	uint8 side = 0;
	uint8 sector = (vsec - 1) % mDiskGeometry.mSectorsPerTrack;

	if (track >= mSideMappingTrackCount) {
		track -= mSideMappingTrackCount;
		side = 1;

		switch(mSideMapping) {
			case SideMapping::Side2Reversed:
				track = (mSideMappingTrackCount - 1) - track;
				sector = mDiskGeometry.mSectorsPerTrack - sector;
				break;

			case SideMapping::Side2ReversedOffByOne:
				track = (mSideMappingTrackCount - 1) - track;
				sector = mDiskGeometry.mSectorsPerTrack - sector;

				if (!--sector) {
					sector = mDiskGeometry.mSectorsPerTrack;
					--track;
				}

				break;

			case SideMapping::Side2ReversedTracks:
				track = (mSideMappingTrackCount - 1) - track;
				break;

			case SideMapping::Side2Forward:
				break;

		}
	}

	return PSecAddress { track, side, (uint8)(sector + 1) };
}

bool ATFDCEmulator::ModifyWriteProtect(bool wp) const {
	switch(mWriteProtectOverride2) {
		case kATFDCWPOverride_None:
			break;

		case kATFDCWPOverride_Invert:
			wp = !wp;
			break;

		case kATFDCWPOverride_WriteProtect:
			wp = true;
			break;

		case kATFDCWPOverride_WriteEnable:
			wp = false;
			break;
	}

	return wp;
}
