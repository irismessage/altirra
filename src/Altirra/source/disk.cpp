﻿//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/math.h>
#include <vd2/system/binary.h>
#include <vd2/system/strutil.h>
#include <at/atcore/deviceindicators.h>
#include <at/atcore/media.h>
#include <at/atcore/randomization.h>
#include <at/atcore/sioutils.h>
#include <at/atcore/snapshotimpl.h>
#include <at/atcore/vfs.h>
#include <at/atcore/wraptime.h>
#include "disk.h"
#include "diskprofile.h"
#include "console.h"
#include "cpu.h"
#include "savestate.h"
#include "simulator.h"
#include "debuggerlog.h"
#include "audiosampleplayer.h"
#include "cio.h"
#include "trace.h"
#include "uirender.h"

extern ATLogChannel g_ATLCDisk;
extern ATLogChannel g_ATLCDiskCmd;
extern ATLogChannel g_ATLCDiskData;

namespace {
	// Cycles/second. This is only correct for NTSC, but it's close enough
	// for PAL for disk emulation purposes.
	static constexpr int kCyclesPerSecond = 7159090/4;

	static constexpr int kCyclesPerDiskRotation_288RPM = (kCyclesPerSecond * 60 + 144) / 288;

	// Cycles for a fake rotation as seen by the FDC. Neither the 810 nor the 1050
	// use the real index pulse; they fake it with the RIOT.
	//
	// 810: ~522ms
	// 1050: ~236ms
	//
	static constexpr int kCyclesPerFakeRot_810		= (kCyclesPerSecond * 522 + 5000) / 10000;
	static constexpr int kCyclesPerFakeRot_1050		= (kCyclesPerSecond * 236 + 5000) / 10000;

	static constexpr int kCyclesPerSIOByte_Happy_Native_Fast = 564;			// 315 cycles/byte @ 1MHz
	static constexpr int kCyclesPerSIOBit_Happy_Native_Fast = 47;		// Native high speed (26 cycles/bit @ 1MHz)

	// Delay from end of ACK byte until FDC command is sent.
	// 810: ~1608 cycles @ 500KHz = ~5756 cycles @ 1.79MHz.
	static constexpr int kCyclesFDCCommandDelay = 5756;

	// Rotational delay in cycles when accurate sector timing is disabled.
	static constexpr int kCyclesRotationalDelay_Fast = 2000;

	// Time from end of sector read to start of Complete byte (FDC reset and checksum):
	//	810: ~2568 cycles @ 500KHz = 9192 cycles
	//	1050: ~270 cycles @ 1MHz = 483 cycles
	//
	static constexpr uint32 kCyclesPostReadDelay_Fast = 1000;
	static constexpr uint32 kCyclesPostReadDelay_810 = 9192;
	static constexpr uint32 kCyclesPostReadDelay_1050 = 483;

	enum {
		kATDiskEventMotorOff = 1
	};

	static constexpr uint8 kDefaultPERCOM[]={
	//	trk   step  spt         sd-1  enc   bps         online
		0x28, 0x01, 0x00, 0x12, 0x00, 0x00, 0x00, 0x80, 0xFF, 0x00, 0x00, 0x00
	};

	static constexpr uint8 kDefaultPERCOMED[]={
		0x28, 0x01, 0x00, 0x1A, 0x00, 0x04, 0x00, 0x80, 0xFF, 0x00, 0x00, 0x00
	};
}

///////////////////////////////////////////////////////////////////////////

ATDiskEmulator::ATDiskEmulator() {
}

ATDiskEmulator::~ATDiskEmulator() {
	Shutdown();
}

void ATDiskEmulator::Init(int unit, ATDiskInterface *dif, ATScheduler *sched, ATScheduler *slowsched, ATAudioSamplePlayer *mixer) {
	mpDiskInterface = dif;
	dif->AddClient(this);

	mpAudioSyncMixer = mixer;
	mpRotationSoundGroup = mixer->CreateGroup(ATAudioGroupDesc().Mix(kATAudioMix_Drive));
	mpStepSoundGroup = mixer->CreateGroup(ATAudioGroupDesc().Mix(kATAudioMix_Drive).RemoveSupercededSounds());

	mLastRotationUpdateCycle = ATSCHEDULER_GETTIME(sched);
	mUnit = unit;
	mpScheduler = sched;
	mpSlowScheduler = slowsched;

	mpSIOMgr->AddDevice(this);

	Reset();

	OnDiskChanged(true);
	OnWriteModeChanged();
	OnTimingModeChanged();
	OnAudioModeChanged();
}

void ATDiskEmulator::Shutdown() {
	if (mpSIOMgr) {
		mpSIOMgr->RemoveDevice(this);
		mpSIOMgr = nullptr;
	}

	if (mpDiskInterface) {
		mpDiskInterface->RemoveClient(this);
		mpDiskInterface = nullptr;
	}

	mpRotationSoundGroup = nullptr;
	mpStepSoundGroup = nullptr;
	mpAudioSyncMixer = nullptr;
}

void ATDiskEmulator::Rename(int unit) {
	mUnit = unit;
}

void ATDiskEmulator::SetEnabled(bool enabled) {
	if (mbEnabled == enabled)
		return;

	// If the drive is being disabled, reset it so it releases status indicators on
	// the disk interface. This must be done before we flip the enabled flag.
	if (!enabled)
		Reset();

	mbEnabled = enabled;
}

void ATDiskEmulator::SetEmulationMode(ATDiskEmulationMode mode) {
	if (mEmuMode == mode)
		return;

	mEmuMode = mode;
	ComputeSupportedProfile();
}

void ATDiskEmulator::Reset() {
	AbortCommand();

	if (mpSlowScheduler)
		mpSlowScheduler->UnsetEvent(mpMotorOffEvent);

	if (mpAudioSyncMixer)
		mpRotationSoundGroup->StopAllSounds();

	mTransferLength = 0;
	mPhantomSectorCounter = 0;
	mRotationalCounter = 0;

	// We use 288 RPM as the basis for random rotation; there will be a little
	// overlap at 300 RPM, but that's fine.
	uint32 rotationOffset = ATRandomizeAdvanceFast(g_ATRandomizationSeeds.mDiskStartPos) % kCyclesPerDiskRotation_288RPM;

	mLastRotationUpdateCycle = ATSCHEDULER_GETTIME(mpScheduler) - rotationOffset;
	mRotations = 0;
	mbLastOpError = false;

	// Power-on status with no disk is $5F for 1050, $FF for XF551. The
	// get status command will handle the not ready bit.
	if (mEmuMode == kATDiskEmulationMode_XF551)
		mFDCStatus = 0xFF;
	else
		mFDCStatus = 0xDF;

	mActiveCommand = 0;
	mCustomCodeState = 0;

	if (mEmuMode == kATDiskEmulationMode_810)
		mCurrentTrack = mTrackCount ? mTrackCount - 1 : 0;
	else
		mCurrentTrack = 0;

	for(ExtVirtSectors::iterator it(mExtVirtSectors.begin()), itEnd(mExtVirtSectors.end()); it!=itEnd; ++it) {
		ExtVirtSector& vsi = *it;

		vsi.mPhantomSectorCounter = 0;
	}

	mWeakBitLFSR = 1;

	ComputeSupportedProfile();

	if (mpDiskInterface->IsDiskLoaded()) {
		ComputeGeometry();
		ComputePERCOMBlock();
	} else
		memcpy(mPERCOM, kDefaultPERCOM, 12);

	// clear activity counter
	if (mbEnabled) {
		mpDiskInterface->SetShowMotorActive(false);
		mpDiskInterface->SetShowActivity(false, 0);
	}

	memset(mDriveRAM, 0, sizeof mDriveRAM);
}

namespace {
	uint8 Checksum(const uint8 *p, int len) {
		uint32 checksum = 0;
		for(int i=0; i<len; ++i) {
			checksum += p[i];
			checksum += (checksum >> 8);
			checksum &= 0xff;
		}

		return (uint8)checksum;
	}
}

void ATDiskEmulator::SetForcedPhantomSector(uint16 sector, uint8 index, int order) {
	if (!sector || sector >= mExtVirtSectors.size())
		return;

	IATDiskImage *image = mpDiskInterface->GetDiskImage();
	if (!image)
		return;

	ATDiskVirtualSectorInfo vsi;
	image->GetVirtualSectorInfo(sector - 1, vsi);

	mExtPhysSectors[vsi.mStartPhysSector + index].mForcedOrder = (sint8)order;
}

int ATDiskEmulator::GetForcedPhantomSector(uint16 sector, uint8 index) {
	if (!sector || sector >= mExtVirtSectors.size())
		return -1;

	IATDiskImage *image = mpDiskInterface->GetDiskImage();
	if (!image)
		return -1;

	ATDiskVirtualSectorInfo vsi;
	image->GetVirtualSectorInfo(sector - 1, vsi);
	if (index >= vsi.mNumPhysSectors)
		return -1;

	return mExtPhysSectors[vsi.mStartPhysSector + index].mForcedOrder;
}

void ATDiskEmulator::SetTraceContext(ATTraceContext *context) {
	mpTraceContext = context;

	if (context) {
		ATTraceCollection *coll = context->mpCollection;

		VDStringW name;
		name.sprintf(L"Disk %u", mUnit + 1);
		mpTraceChannel = coll->AddGroup(name.c_str())->AddFormattedChannel(context->mBaseTime, context->mBaseTickScale, L"Commands");
	} else {
		mpTraceChannel = nullptr;
	}
}

class ATSaveStateDisk final : public ATSnapExchangeObject<ATSaveStateDisk> {
public:
	template<typename T>
	void Exchange(T& rw) {
		rw.Transfer("idle_timer", &mIdleTimer);
		rw.Transfer("active_command_id", &mActiveCommandId);
		rw.Transfer("active_command_state", &mActiveCommandState);
		rw.Transfer("active_command_sector", &mActiveCommandSector);
		rw.Transfer("active_command_timer", &mActiveCommandTimer);
		rw.Transfer("rotational_pos", &mRotationalPos);
		rw.Transfer("current_track", &mCurrentTrack);
		rw.Transfer("active_command", &mpActiveCommand);

		if constexpr (rw.IsReader) {
			// Allow for up to a ~10 minute idle time -- beyond that consider it broken.
			if (mIdleTimer >= 0x40000000)
				throw ATInvalidSaveStateException();

			// Disks can only have 65,535 logical sectors -- so they will never have that
			// many tracks.
			if (mCurrentTrack >= 65535)
				throw ATInvalidSaveStateException();
		}
	}

	uint32 mIdleTimer = 0;
	uint32 mActiveCommandTimer = 0;
	uint8 mActiveCommandId = 0;
	uint32 mActiveCommandState = 0;
	uint16 mActiveCommandSector = 0;
	float mRotationalPos = 0;
	uint32 mCurrentTrack = 0;

	vdrefptr<IATObjectState> mpActiveCommand;
};

ATSERIALIZATION_DEFINE(ATSaveStateDisk);

void ATDiskEmulator::SaveState(IATObjectState **pp) const {
	const uint32 t = mMotorOffTime - mpScheduler->GetTick();
	vdrefptr<ATSaveStateDisk> state(new ATSaveStateDisk);

	if (mpMotorOffEvent)
		state->mIdleTimer = mMotorOffTime - t;
	else
		state->mIdleTimer = 0;

	if (mActiveCommand)
		state->mActiveCommandTimer = t - mActiveCommandStartTime;
	else
		state->mActiveCommandTimer = 0;

	state->mRotationalPos = (float)GetUpdatedRotationalCounter() / (float)mpProfile->mCyclesPerDiskRotation;
	state->mActiveCommandId = mActiveCommand;
	state->mActiveCommandState = mActiveCommandState;
	state->mActiveCommandSector = mActiveCommandSector;

	state->mCurrentTrack = mCurrentTrack;

	mpSIOMgr->SaveActiveCommandState(this, ~state->mpActiveCommand);

	*pp = state.release();
}

void ATDiskEmulator::LoadState(const IATObjectState& state0) {
	const ATSaveStateDisk& state = atser_cast<const ATSaveStateDisk&>(state0);
	const uint32 t = mpScheduler->GetTick(); 

	if (state.mIdleTimer > 0) {
		TurnOnMotor();

		mMotorOffTime = t + state.mIdleTimer;
		SetMotorEvent();
	} else {
		TurnOffMotor();
	}

	uint32 rotPosCycles = (uint32)((state.mRotationalPos - floorf(state.mRotationalPos)) * (float)mpProfile->mCyclesPerDiskRotation + 0.5f);

	if (rotPosCycles < mRotationalCounter)
		++mRotations;

	mRotationalCounter = rotPosCycles;
	mLastRotationUpdateCycle = ATSCHEDULER_GETTIME(mpScheduler);

	mActiveCommand = state.mActiveCommandId;
	mActiveCommandState = state.mActiveCommandState;
	mActiveCommandSector = state.mActiveCommandSector;
	mCurrentTrack = state.mCurrentTrack;

	if (mActiveCommand)
		mActiveCommandStartTime = t - state.mActiveCommandTimer;

	mpSIOMgr->LoadActiveCommandState(this, state.mpActiveCommand);
}

void ATDiskEmulator::OnScheduledEvent(uint32 id) {
	if (id == kATDiskEventMotorOff) {
		mpMotorOffEvent = nullptr;

		// Motor idling is driven by the main loop of the drive, and as such can't
		// occur during command processing. Furthermore, the timer is also usually
		// driven in software, and so can't even count down outside of idle. If
		// we hit the timer during a command, requeue the event and retry later;
		// once the command ends the timer will be readjusted.

		const uint32 t = mpScheduler->GetTick();
		if (mActiveCommand) {
			mpSlowScheduler->SetEvent(16, this, kATDiskEventMotorOff, mpMotorOffEvent);
		} else if (ATWrapTime{t} >= mMotorOffTime)
			TurnOffMotor();
		else
			SetMotorEvent();
	}
}

void ATDiskEmulator::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
}

IATDeviceSIO::CmdResponse ATDiskEmulator::OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) {
	if (!mbEnabled)
		return kCmdResponse_NotHandled;

	// check if it's us
	if (cmd.mDevice != (uint8)(0x31 + mUnit))
		return kCmdResponse_NotHandled;
			
	// Check the speed of the command frame and make sure the transmission is at the
	// correct rate. It must be either 19,200 baud (divisor=$28) or the high speed
	// command rate for the XF551 and IndusGT. We allow up to a 5% deviation in transfer rate.
	if (cmd.mbStandardRate) {
		mbCommandFrameHighSpeed = false;
	} else {
		if (!mpProfile->mbSupportedCmdFrameHighSpeed || cmd.mCyclesPerBit < mpProfile->mHighSpeedCmdFrameRateLo || cmd.mCyclesPerBit > mpProfile->mHighSpeedCmdFrameRateHi) {
			g_ATLCDiskCmd("Rejecting command sent at wrong rate (cycles per bit = %d, expected [%d,%d])\n", cmd.mCyclesPerBit, mpProfile->mHighSpeedCmdFrameRateLo, mpProfile->mHighSpeedCmdFrameRateHi);
			return kCmdResponse_NotHandled;
		} else {
			mbCommandFrameHighSpeed = true;
		}
	}

	mpDiskInterface->SetShowActivity(true, mLastSector);

	UpdateRotationalCounter();

	// interpret the command
	mbCommandFrameHighSpeed = !cmd.mbStandardRate;

	g_ATLCDiskCmd("Processing command: Unit %02X, Command %02X, Aux data %02X %02X%s\n"
		, cmd.mDevice
		, cmd.mCommand
		, cmd.mAUX[0]
		, cmd.mAUX[1]
		, mbCommandFrameHighSpeed ? " (high-speed command frame)" : ""
	);
	const uint8 command = cmd.mCommand;
	bool highSpeed = mbCommandFrameHighSpeed || (command & 0x80) != 0;

	UpdateRotationalCounter();
	mActiveCommandStartRotPos = (float)mRotations + (float)mRotationalCounter / (float)mpProfile->mCyclesPerDiskRotation;
	mActiveCommandStartTime = mpScheduler->GetTick();

	// check if this is a 1050 Turbo command
	mActiveCommandSector = cmd.mAUX[0] + cmd.mAUX[1] * 256;

	if (mEmuMode == kATDiskEmulationMode_1050Turbo && (mActiveCommandSector & 0x8000)) {
		switch(command) {
			case 0x4E:	// read PERCOM block
			case 0x4F:	// write PERCOM block
			case 0x52:	// read
			case 0x53:	// status (used by MyPicoDOS to autodetect)
			case 0x50:	// put (without verify)
			case 0x57:	// write (with verify)
				mActiveCommandSector &= 0x7FFF;
				mbCommandFrameHighSpeed = true;
				highSpeed = true;
				break;
		}
	}

	mOriginalDevice = cmd.mDevice;
	mOriginalCommand = command;
	mbActiveCommandHighSpeed = highSpeed;
	mActiveCommandState = 0;
	mbActiveCommandWait = false;
	mpSIOMgr->BeginCommand();

	// reject all high speed commands if not XF551 or generic

	if (!mpProfile->mbSupportedCmdHighSpeed && (command & 0x80))
		goto unsupported_command;

	mLastAccelTimeSkew = mpSIOMgr->GetAccelTimeSkew();

	switch(command) {
		case 0x53:	// status
		case 0xD3:	// status (XF551 high speed)
			mActiveCommand = 0x53;
			break;

		case 0x52:	// read
		case 0xD2:	// read (XF551 high speed)
			mActiveCommand = 0x52;
			break;

		case 0x72:	// read (Happy high speed)
			mActiveCommand = 0x72;
			mbActiveCommandHighSpeed = true;
			break;

		case 0x21:	// format
		case 0x22:	// format disk medium density
		case 0x66:	// format disk skewed
		case 0xA1:	// format (high speed skew, XF551/Synchromesh)
		case 0xA2:	// format disk medium density (high speed)
		case 0xA3:	// format boot tracks with normal skew (Synchromesh only)
		case 0xE6:	// format disk skewed (high speed)
			mActiveCommand = 0x21;
			break;

		case 0x50:	// put (without verify)
		case 0xD0:	// put (without verify) (XF551 high speed)
			mActiveCommand = 0x50;
			break;

		case 0x70:	// put (without verify) (Happy high speed)
			mActiveCommand = 0x50;
			break;

		case 0x57:	// write (with verify)
		case 0xD7:	// write (with verify) (XF551 high speed)
			mActiveCommand = 0x57;
			break;

		case 0x77:	// write (with verify) (Happy high speed)
			mActiveCommand = 0x77;
			mbActiveCommandHighSpeed = true;
			break;

		case 0x4E:	// read PERCOM block
		case 0xCE:	// read PERCOM block (XF551 high speed)
			mActiveCommand = 0x4E;
			break;

		case 0x4F:	// write PERCOM block
		case 0xCF:	// write PERCOM block (XF551 high speed)
			mActiveCommand = 0x4F;
			break;

		case 0x3F:	// get high speed index
			mActiveCommand = 0x3F;
			break;

		case 0x48:
			mActiveCommand = 0x48;
			break;

		case 0x58:
			mActiveCommand = 0x58;
			break;

		case 0x54:
			mActiveCommand = 0x54;
			break;

		case 0x28:	// Happy head positioning test recalibrate
		case 0x29:	// Happy head positioning test seek
		case 0x2D:	// Happy RPM test
		case 0x51:
			mActiveCommand = command;
			break;

		default:
unsupported_command:
			ProcessUnsupportedCommand();
			break;
	}

	if (mActiveCommand)
		ProcessCommand();

	mpDiskInterface->SetShowActivity(mActiveCommand != 0, mLastSector);
	return kCmdResponse_Start;
}

void ATDiskEmulator::OnSerialAbortCommand() {
	if (mActiveCommand) {
		ExtendMotorTimeoutBy(mpScheduler->GetTick() - mActiveCommandStartTime);

		AbortCommand();
	}
}

void ATDiskEmulator::OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) {
	if (!checksumOK) {
		mpSIOMgr->FlushQueue();
		BeginTransferNAKData();
		EndCommand();
		return;
	}

	memcpy(mReceivePacket, data, len);
	mbReceiveChecksumOK = checksumOK;
}

void ATDiskEmulator::OnSerialFence(uint32 id) {
	if (id == (uint32)0 - 1) {
		ExtendMotorTimeoutBy(mpScheduler->GetTick() - mActiveCommandStartTime);

		UpdateAccelTimeSkew();
		mpDiskInterface->SetShowActivity(false, mLastSector);

		if (mpTraceChannel) {
			if (mOriginalCommand == 0x52) {
				mpTraceChannel->AddTickEvent(mpSIOMgr->GetCommandDeassertTime(), mpScheduler->GetTick64(),
					[sec = (uint16)mActiveCommandSector](VDStringW& s) {
						s.sprintf(L"Read %u", sec);
					},
					kATTraceColor_IO_Read
				);
			} else {
				mpTraceChannel->AddTickEvent(mpSIOMgr->GetCommandDeassertTime(), mpScheduler->GetTick64(),
					[c = (uint8)mOriginalCommand, sec = (uint16)mActiveCommandSector](VDStringW& s) {
						s.sprintf(L"%02X:%u", c, sec);
					},
					kATTraceColor_IO_Default
				);
			}
		}
	} else {
		mActiveCommandState = id;
		mbActiveCommandWait = false;
		ProcessCommand();
	}
}

IATDeviceSIO::CmdResponse ATDiskEmulator::OnSerialAccelCommand(const ATDeviceSIORequest& request) {
	return OnSerialBeginCommand(request);
}

void ATDiskEmulator::OnDiskChanged(bool mediaRemoved) {
	InitSectorInfoArrays();
	ComputeGeometry();
	ComputePERCOMBlock();
	mCurrentTrack = mTrackCount - 1;
}

void ATDiskEmulator::OnWriteModeChanged() {
	const auto mode = mpDiskInterface->GetWriteMode();

	mbWriteEnabled = (mode & kATMediaWriteMode_AllowWrite) != 0;
	mbFormatEnabled = (mode & kATMediaWriteMode_AllowFormat) != 0;
}

void ATDiskEmulator::OnTimingModeChanged() {
	mbAccurateSectorTiming = mpDiskInterface->IsAccurateSectorTimingEnabled();
}

void ATDiskEmulator::OnAudioModeChanged() {
	mbDriveSoundsEnabled = mpDiskInterface->AreDriveSoundsEnabled();

	if (!mbDriveSoundsEnabled && mpRotationSoundGroup)
		mpRotationSoundGroup->StopAllSounds();
}

bool ATDiskEmulator::IsImageSupported(const IATDiskImage& image) const {
	return true;
}

void ATDiskEmulator::UpdateAccelTimeSkew() {
	uint32 ats = mpSIOMgr->GetAccelTimeSkew();

	if (mLastAccelTimeSkew != ats) {
		mRotationalCounter += (ats - mLastAccelTimeSkew);

		if (mRotationalCounter >= mpProfile->mCyclesPerDiskRotation) {
			mRotationalCounter -= mpProfile->mCyclesPerDiskRotation;
			++mRotations;
		}

		mLastAccelTimeSkew = ats;
	}
}

void ATDiskEmulator::InitSectorInfoArrays() {
	IATDiskImage *image = mpDiskInterface->GetDiskImage();
	if (!image) {
		mTotalSectorCount = 0;
		return;
	}

	const uint32 physCount = image->GetPhysicalSectorCount();
	mExtPhysSectors.resize(physCount);

	for(uint32 i = 0; i < physCount; ++i) {
		ExtPhysSector& psi = mExtPhysSectors[i];

		psi.mForcedOrder = -1;
	}

	const uint32 virtCount = image->GetVirtualSectorCount();
	mExtVirtSectors.resize(virtCount);

	mTotalSectorCount = virtCount;

	for(uint32 i=0; i<virtCount; ++i) {
		ExtVirtSector& vsi = mExtVirtSectors[i];

		vsi.mPhantomSectorCounter = 0;
	}

	mBootSectorCount = image->GetBootSectorCount();
	mSectorSize = image->GetSectorSize();
	mbAccurateSectorPrediction = (image->GetTimingMode() == kATDiskTimingMode_UsePrecise);
}

void ATDiskEmulator::SetupTransferSpeed(bool highSpeed) {
	if (highSpeed) {
		// Special case for Happy commands
		switch(mActiveCommand) {
			case 0x70:
			case 0x72:
			case 0x77:
				mCyclesPerSIOBitCurrent = kCyclesPerSIOBit_Happy_Native_Fast;
				mCyclesPerSIOByteCurrent = kCyclesPerSIOByte_Happy_Native_Fast;
				break;

			default:
				mCyclesPerSIOBitCurrent = mpProfile->mCyclesPerSIOBitHighSpeed;
				mCyclesPerSIOByteCurrent = mpProfile->mCyclesPerSIOByteHighSpeed;
				break;
		}
	} else {
		mCyclesPerSIOBitCurrent = mpProfile->mCyclesPerSIOBit;
		mCyclesPerSIOByteCurrent = mpProfile->mCyclesPerSIOByte;
	}

	mpSIOMgr->SetTransferRate(mCyclesPerSIOBitCurrent, mCyclesPerSIOByteCurrent);
}

void ATDiskEmulator::BeginTransferACKCmd() {
	SetupTransferSpeed(mbCommandFrameHighSpeed);
	mpSIOMgr->Delay(mpProfile->mCyclesToACKSent);
	mpSIOMgr->SendACK();
}

void ATDiskEmulator::BeginTransferACK() {
	SetupTransferSpeed(mbActiveCommandHighSpeed);
	mpSIOMgr->Delay(mpProfile->mCyclesToACKSent);
	mpSIOMgr->SendACK();
}

void ATDiskEmulator::BeginTransferComplete() {
	SetupTransferSpeed(mbActiveCommandHighSpeed);
	mpSIOMgr->SendComplete(false);
}

void ATDiskEmulator::BeginTransferError() {
	SetupTransferSpeed(mbActiveCommandHighSpeed);
	mpSIOMgr->SendError(false);
}

void ATDiskEmulator::BeginTransferNAKCommand() {
	SetupTransferSpeed(mbCommandFrameHighSpeed);

	// Typically the drive firmware does the following:
	//
	//	- Process the command.
	//	- Wait for the command line to deassert.
	//	- Send the NAK.
	//
	// It is possible for the first step to take longer than the computer to
	// deassert the command line, depending on the speed of the SIO routine.
	// Therefore, we must compute both delays from the end of the command frame
	// (precisely, the last data bit) and from the command line deasserting
	// and take the maximum of the two.
	//
	const uint64 t = mpSIOMgr->GetCommandQueueTime();
	const uint64 targetTime = std::max<uint64>(mpSIOMgr->GetCommandFrameEndTime() + mpProfile->mCyclesToNAKFromFrameEnd, mpSIOMgr->GetCommandDeassertTime() + mpProfile->mCyclesToNAKFromCmdDeassert);

	if (targetTime > t)
		mpSIOMgr->Delay((uint32)(targetTime - t));

	mpSIOMgr->SendNAK();
}

void ATDiskEmulator::BeginTransferNAKData() {
	// NAKs are only sent in response to the command itself and therefore must be sent at
	// command frame speed.
	SetupTransferSpeed(mbCommandFrameHighSpeed);
	mpSIOMgr->Delay(mpProfile->mCyclesToACKSent);
	mpSIOMgr->SendNAK();
}

void ATDiskEmulator::SendResult(bool successful, uint32 length) {
	if (successful)
		BeginTransferComplete();
	else
		BeginTransferError();

	if (mbActiveCommandHighSpeed)
		mpSIOMgr->Delay(mpProfile->mCyclesCEToDataFrameHighSpeed + ((length * mpProfile->mCyclesCEToDataFrameHighSpeedPBDiv256 + 128) >> 8));
	else
		mpSIOMgr->Delay(mpProfile->mCyclesCEToDataFrame + ((length * mpProfile->mCyclesCEToDataFramePBDiv256 + 128) >> 8));

	Send(length);
}

void ATDiskEmulator::Send(uint32 length) {
	mTransferLength = length;

	if (length) {
		SetupTransferSpeed(mbActiveCommandHighSpeed);
		mpSIOMgr->SendData(mSendPacket, length, true);
	}

	++mActiveCommandState;
}

void ATDiskEmulator::BeginReceive(uint32 len) {
	SetupTransferSpeed(mbActiveCommandHighSpeed);
	mpSIOMgr->ReceiveData(0, len, true);
	++mActiveCommandState;
}

void ATDiskEmulator::WarpOrDelay(uint32 cycles, uint32 minCycles) {
	if (!mbAccurateSectorTiming && cycles > minCycles) {
		mRotationalCounter += cycles - minCycles;
		if (mRotationalCounter >= mpProfile->mCyclesPerDiskRotation) {
			mRotationalCounter -= mpProfile->mCyclesPerDiskRotation;
			++mRotations;
		}

		cycles = minCycles;
	}

	mpSIOMgr->Delay(cycles);
}

// This routine delays from the beginning of the stop bit of the last byte rather than
// the end of the stop bit or byte. This models optimizations in drive firmware where
// the firmware sets the output high for the stop bit and then immediately resumes
// processing, since there is no further work needed on the serial hardware. This
// simplifies specification of post-transmission delays with high speed since the
// width of the stop bit varies but the work done after the start of the stop bit
// largely doesn't.
void ATDiskEmulator::WarpOrDelayFromStopBit(uint32 cycles) {
	uint32 delay = std::max<uint32>(cycles, mCyclesPerSIOBitCurrent + 1) - mCyclesPerSIOBitCurrent;

	WarpOrDelay(delay, delay);
}

void ATDiskEmulator::Wait(uint32 nextState) {
	mbActiveCommandWait = true;
	mpSIOMgr->InsertFence(nextState);
}

void ATDiskEmulator::EndCommand() {
	mActiveCommand = 0;

	mpSIOMgr->InsertFence((uint32)0 - 1);
	mpSIOMgr->EndCommand();
}

void ATDiskEmulator::AbortCommand() {
	mActiveCommand = 0;
}

uint32 ATDiskEmulator::GetUpdatedRotationalCounter() const {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	uint32 dt = t - mLastRotationUpdateCycle;

	return (mRotationalCounter + dt) % mpProfile->mCyclesPerDiskRotation;
}

void ATDiskEmulator::UpdateRotationalCounter() {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	uint32 dt = t - mLastRotationUpdateCycle;
	mLastRotationUpdateCycle = t;

	mRotationalCounter += dt;

	if (mRotationalCounter >= mpProfile->mCyclesPerDiskRotation) {
		uint32 rotations = mRotationalCounter / mpProfile->mCyclesPerDiskRotation;
		mRotationalCounter %= mpProfile->mCyclesPerDiskRotation;
		mRotations += rotations;
	}
}

void ATDiskEmulator::ProcessUnsupportedCommand() {
	BeginTransferNAKCommand();

	const char *extraDesc = "";

	switch(mOriginalCommand) {
		case 0x21:
		case 0x22:
		case 0x28:
		case 0x29:
		case 0x2D:
		case 0x3F:
		case 0x48:
		case 0x4E:
		case 0x4F:
		case 0x51:
		case 0x54:
		case 0x58:
		case 0x66:
		case 0x70:
		case 0x72:
		case 0x77:
			extraDesc = "; not supported by current profile";
			break;
	}

	uint8 aux[2];
	VDWriteUnalignedLEU16(aux, mActiveCommandSector);
	g_ATLCDisk("Unsupported command %02X (%s%s)\n", mOriginalCommand, ATDecodeSIOCommand(mOriginalDevice, mOriginalCommand, aux), extraDesc);

	EndCommand();
}

void ATDiskEmulator::ProcessCommand() {
	while(mActiveCommand && !mbActiveCommandWait) {
		UpdateAccelTimeSkew();

		switch(mActiveCommand) {
			case 0x21:
				ProcessCommandFormat();
				break;

			case 0x3F:
				ProcessCommandGetHighSpeedIndex();
				break;

			case 0x48:
				ProcessCommandHappy();
				break;

			case 0x4E:
				ProcessCommandReadPERCOMBlock();
				break;

			case 0x4F:
				ProcessCommandWritePERCOMBlock();
				break;

			case 0x52:
			case 0x72:
				ProcessCommandRead();
				break;

			case 0x53:
				ProcessCommandStatus();
				break;

			case 0x50:		// put
			case 0x57:		// write
			case 0x70:		// Happy high-speed put
			case 0x77:		// Happy high-speed write
				ProcessCommandWrite();
				break;

			case 0x58:		// Indus GT execute
				ProcessCommandExecuteIndusGT();
				break;

			case 0x51:
				ProcessCommandHappyQuiet();
				break;

			case 0x54:
				ProcessCommandHappyRAMTest();
				break;

			case 0x28:
			case 0x29:
				ProcessCommandHappyHeadPosTest();
				break;

			case 0x2D:
				ProcessCommandHappyRPMTest();
				break;
		}
	}
}

///////////////////////////////////////////////////////////////////////////
// Status command ($58/$D8)
//
// On the 810:
//		ACK - 322 device cycles
//		Setup - 155 device cycles
//		Transmit C + status bytes
//
// There is NO delay between C and the status bytes on the 810. The SIO
// routine must be ready to receive the data frame immediately.
//
// The 1050 issues a force interrupt command to the FDC twice before sending
// the C/E + data frame. We don't currently have timing for this.
//
void ATDiskEmulator::ProcessCommandStatus() {
	BeginTransferACKCmd();

	WarpOrDelayFromStopBit(mpProfile->mCyclesACKStopBitToStatusComplete);

	uint8 status = 0;

	// We need to check the sector size in the PERCOM block and not the physical
	// disk for this value. This is required as SmartDOS 8.2D does a Write PERCOM
	// Block command and then depends on FM/MFM selection being reflected in the
	// result.
	if (mPERCOM[6])		// sector size high byte
		status += 0x20;

	if (mbLastOpError)
		status += 0x04;

	if (!mbWriteEnabled)
		status += 0x08;

	if (mSideCount > 1)
		status += 0x40;

	if (mSectorsPerTrack == 26)
		status += 0x80;

	if (mpMotorOffEvent)
		status += 0x10;

	mSendPacket[0] = status;
	mSendPacket[1] = mFDCStatus;

	if (!mpProfile->mbSupportedNotReady)
		mSendPacket[1] |= 0x80;
	else if (mTotalSectorCount == 0)
		mSendPacket[1] &= 0x7F;

	mSendPacket[2] = mEmuMode == kATDiskEmulationMode_XF551 ? 0xfe : 0xe0;
	mSendPacket[3] = 0x00;

	SendResult(true, 4);
	EndCommand();
}

bool ATDiskEmulator::ProcessCommandReadWriteCommon(bool isWrite) {
	const uint32 sector = mActiveCommandSector;

	switch(mActiveCommandState) {
		case 10:
			// turn on the motor if needed
			if (TurnOnMotor())
				WarpOrDelay(7159090/8);

			// delay until FDC command issued
			WarpOrDelay(kCyclesFDCCommandDelay);
			Wait(11);
			break;

		case 11: {
			// check if we need to seek (spt=0 may be true if we lost the disk)
			uint32 track = mSectorsPerTrack ? (sector - 1) / mSectorsPerTrack : 0;
			int trackDelta = (int)track - (int)mCurrentTrack;
			uint32 tracksToStep = (uint32)abs(trackDelta);

			mCurrentTrack = track;

			uint32 opDelay = 0;
			if (tracksToStep) {
				// The 1050 drive does an extra pair of half steps after a forward seek, one forward
				// and one backward. This ensures that tracks are always read or written after the
				// head has seeked backwards. The 810 does not do this.
				if (trackDelta > 0 && mpProfile->mbReverseOnForwardSeeks) {
					PlaySeekSound(opDelay, tracksToStep + 1);
					opDelay += (tracksToStep + 1) * mpProfile->mCyclesPerTrackStep;
				} else {
					PlaySeekSound(opDelay, tracksToStep);
					opDelay += tracksToStep * mpProfile->mCyclesPerTrackStep;
				}

				opDelay += mpProfile->mCyclesForHeadSettle;
			}

			WarpOrDelay(opDelay);
			Wait(13);
			break;
		}

		case 13:
		case 14:
		case 15:
		case 16: {
			UpdateRotationalCounter();

			// get virtual sector information
			IATDiskImage *image = mpDiskInterface->GetDiskImage();

			ATDiskVirtualSectorInfo vsi {};
			if (image)
				image->GetVirtualSectorInfo(sector - 1, vsi);

			ExtVirtSector& evs = mExtVirtSectors[sector - 1];

			// check if we have any sectors
			ATDiskPhysicalSectorInfo psi;
			uint32 postSeekPosition = 0;
			uint32 physSector;

			if (vsi.mNumPhysSectors) {
				// choose a physical sector
				if (mbAccurateSectorPrediction || mbAccurateSectorTiming) {
					// compute post-seek rotational position
					postSeekPosition = mRotationalCounter % mpProfile->mCyclesPerDiskRotation;

					uint32 bestDelay = 0xFFFFFFFFU;
					uint8 bestStatus = 0;

					physSector = vsi.mStartPhysSector;

					for(uint32 i=0; i<vsi.mNumPhysSectors; ++i) {
						ATDiskPhysicalSectorInfo psi;
						image->GetPhysicalSectorInfo(vsi.mStartPhysSector + i, psi);

						const ExtPhysSector& eps = mExtPhysSectors[vsi.mStartPhysSector + i];

						// compute sector's rotational position in cycles
						uint32 sectorPos = VDRoundToInt(psi.mRotPos * mpProfile->mCyclesPerDiskRotation);

						// compute rotational delay to sector
						uint32 delay = sectorPos < postSeekPosition ? sectorPos + mpProfile->mCyclesPerDiskRotation - postSeekPosition : sectorPos - postSeekPosition;

						if (eps.mForcedOrder == evs.mPhantomSectorCounter) {
							physSector = vsi.mStartPhysSector + i;
							mPhantomSectorCounter = i;

							if (++evs.mPhantomSectorCounter >= vsi.mNumPhysSectors)
								evs.mPhantomSectorCounter = 0;
							break;
						}

						if (delay < bestDelay) {
							bestDelay = delay;
							bestStatus = psi.mFDCStatus;

							physSector = vsi.mStartPhysSector + i;
							mPhantomSectorCounter = i;
						}
					}
				} else {
					uint32 phantomIdx = evs.mPhantomSectorCounter;
					for(uint32 i=0; i<vsi.mNumPhysSectors; ++i) {
						const ExtPhysSector& eps = mExtPhysSectors[vsi.mStartPhysSector + i];

						if (eps.mForcedOrder == evs.mPhantomSectorCounter) {
							phantomIdx = i;
							break;
						}
					}

					physSector = vsi.mStartPhysSector + phantomIdx;

					if (++evs.mPhantomSectorCounter >= vsi.mNumPhysSectors)
						evs.mPhantomSectorCounter = 0;
				}

				image->GetPhysicalSectorInfo(physSector, psi);

				// Set FDC status.
				//
				// The lost data condition (bit 2) needs to be handled specially here, as it results
				// from reading a long sector, and the 810 and 1050 differ in behavior. The 810 reads
				// status immediately after 128 bytes, so it will see DRQ but not lost data, and not
				// a CRC error as the CRC hasn't been read yet. A 1050, on the other hand, will wait
				// until the sector has finished reading, so lost data and DRQ won't be on, but
				// the CRC flag will be correct. The Music Studio and Fight Night are sensitive to
				// behavior here.
				if (isWrite) {
					mFDCStatus = 0xFF;
				} else {
					mFDCStatus = psi.mFDCStatus;

					// Clone the deleted bit to both record type bits for the 810 for reads, since
					// currently our FDC status is for the 1050.
					switch(mEmuMode) {
						case kATDiskEmulationMode_Generic:
						case kATDiskEmulationMode_Generic57600:
						case kATDiskEmulationMode_810:
							mFDCStatus &= 0xBF;
							mFDCStatus += (mFDCStatus & 0x20) << 1;
							break;
					}

					// Record Not Found + CRC means that the address header had a CRC error. In
					// that case, we never make it to the data frame, so we need to skip this logic.
					if (!(mFDCStatus & 0x18)) {
						// Ignore long sector related errors.
						mFDCStatus |= 0x06;
					} else {
						switch(mEmuMode) {
							case kATDiskEmulationMode_Generic:
							case kATDiskEmulationMode_Generic57600:
							case kATDiskEmulationMode_810:
							case kATDiskEmulationMode_Happy810:
								if (!(mFDCStatus & 0x04)) {
									// assert BSY and DRQ -- read still pending, byte ready
									mFDCStatus &= ~0x03;

									// negate CRC (not read yet) and lost data (not two bytes passed yet)
									mFDCStatus |= 0x0C;
								}
								break;

							case kATDiskEmulationMode_FastestPossible:
							case kATDiskEmulationMode_Happy1050:
							case kATDiskEmulationMode_1050:
							case kATDiskEmulationMode_XF551:
							case kATDiskEmulationMode_USDoubler:
							case kATDiskEmulationMode_Speedy1050:
							case kATDiskEmulationMode_IndusGT:
							case kATDiskEmulationMode_1050Turbo:
								if (!(mFDCStatus & 0x04)) {
									// assert DRQ (byte not read)
									mFDCStatus &= ~0x02;

									// negate BSY (read is completed)
									mFDCStatus |= 0x01;
								}
								break;
						}
					}
				}

				// set rotational delay
				mTransferCompleteRotPos = VDRoundToInt(psi.mRotPos * mpProfile->mCyclesPerDiskRotation);
				mActiveCommandPhysSector = physSector;
			} else {
				// indicate missing track/sector (record not found)
				mFDCStatus = 0xEF;
				mActiveCommandPhysSector = -1;
				mTransferCompleteRotPos = mRotationalCounter;
			}


			const bool missingSector = !(mFDCStatus & 0x10);

			// compute seek time
			uint32 secondByteDelay = 0;

			// If we have the sector, add rotational delay from the post seek position to the
			// sector's position; otherwise, add two revs for the FDC's attempt to find it.
			if (missingSector) {
				if (mpProfile->mbRetryMode1050)
					secondByteDelay += kCyclesPerFakeRot_1050;
				else
					secondByteDelay += kCyclesPerFakeRot_810;
			} else {
				if (postSeekPosition > mTransferCompleteRotPos)
					secondByteDelay += mpProfile->mCyclesPerDiskRotation;

				secondByteDelay += mTransferCompleteRotPos - postSeekPosition;
			}

			// Check if we got an error.
			const uint32 maxRetryState = (mpProfile->mbRetryMode1050 ? 14 : 16);
			if (mFDCStatus != 0xFF && mActiveCommandState < maxRetryState) {
				// Check if we're modeling the 810 or 1050. The 810 does four tries overall
				// with a possible recalibrate between the first two and second two attempts;
				// the 1050 does two tries with a recalibrate or restep in between.
				if (!mpProfile->mbRetryMode1050) {
					// 810 -- add another (fake) rotation
					if (missingSector)
						secondByteDelay += kCyclesPerFakeRot_810;
					else
						secondByteDelay += mpProfile->mCyclesPerDiskRotation;
				}

				// Compute the restep/recalibration delay.
				if (missingSector) {
					// Missing sector -- we'll be recalibrating.
					//
					// The 1050 has a track 0 sensor, so it only does the necessary number of steps
					// when recalibrating. The 810, on the other hand, doesn't have one and just
					// steps back 43 tracks.
					const uint32 restoreSteps = mpProfile->mbRetryMode1050 ? mCurrentTrack : 43;
					const uint32 seekTime1 = restoreSteps ? restoreSteps * mpProfile->mCyclesPerTrackStep + mpProfile->mCyclesForHeadSettle : 0;

					if (restoreSteps) {
						PlaySeekSound(secondByteDelay, restoreSteps);
						secondByteDelay += seekTime1;
					}

					// compute time to seek back -- no rotational delay to get back to sector, it
					// doesn't exist (sectors don't magically reappear in our model).
					if (mCurrentTrack) {
						const uint32 tracksToStep = mpProfile->mbReverseOnForwardSeeks ? mCurrentTrack+1 : mCurrentTrack;

						PlaySeekSound(secondByteDelay, tracksToStep);
						secondByteDelay += tracksToStep * mpProfile->mCyclesPerTrackStep + mpProfile->mCyclesForHeadSettle;
					}

					// ...and do another fake rotation.
					if (mpProfile->mbRetryMode1050)
						secondByteDelay += kCyclesPerFakeRot_1050;
					else
						secondByteDelay += kCyclesPerFakeRot_810;
				} else {
					// Found sector but read with error. For the 810, we'll just fail three
					// more times. For the 1050, we need to do a half step in and back, then
					// retry once.
					if (mpProfile->mbRetryMode1050) {
						// seek in and out
						PlaySeekSound(secondByteDelay, 1);

						// apply seek delay
						uint32 reseekDelay = mpProfile->mCyclesPerTrackStep + mpProfile->mCyclesForHeadSettle;

						secondByteDelay += reseekDelay;
					}
				}

				// delay and then retry
				WarpOrDelay(secondByteDelay);
				Wait(mActiveCommandState + 1);
				break;
			}

			WarpOrDelay(secondByteDelay);
			Wait(20);
			break;
		}

		default:
			return false;
	}

	return true;
}

void ATDiskEmulator::ProcessCommandRead() {
	if (ProcessCommandReadWriteCommon(false))
		return;

	const uint32 sector = mActiveCommandSector;

	switch(mActiveCommandState) {
		case 0: {
			mLastSector = sector;

			mpDiskInterface->CheckSectorBreakpoint(sector);

			// check if we have a happy drive memory request
			if (mEmuMode == kATDiskEmulationMode_Happy810 && sector > 720) {
				// Address must be in $0800-1380.
				if (sector < 0x0800 || sector > 0x1380) {
					// NAK the command.
					BeginTransferNAKCommand();
					WarpOrDelay(500, 500);
					SendResult(false, mSectorSize);
					EndCommand();
					return;
				}

				// Copy data from drive RAM.
				memcpy(mSendPacket, mDriveRAM + (sector - 0x0800), mSectorSize);
				BeginTransferACKCmd();
				WarpOrDelay(500, 500);
				SendResult(true, mSectorSize);
				EndCommand();
				return;
			} else if (mEmuMode == kATDiskEmulationMode_Happy1050 && sector >= 0x8000) {
				// Address must be in $8000-97FF.
				if (sector >= 0x9800) {
					// NAK the command.
					BeginTransferNAKCommand();
					WarpOrDelay(500, 500);
					SendResult(false, mSectorSize);
					EndCommand();
					return;
				}

				// Copy data from drive RAM.
				memcpy(mSendPacket, mDriveRAM + (sector - 0x8000), mSectorSize);
				BeginTransferACKCmd();
				WarpOrDelay(500, 500);
				SendResult(true, mSectorSize);
				EndCommand();
				return;
			}

			// check if we actually have a disk; if not, we still allow sectors 1-720, but
			// report them as missing
			IATDiskImage *image = mpDiskInterface->GetDiskImage();

			if (!image && sector >= 1 && sector <= 720) {
				mbLastOpError = true;

				// A real 1050 reports 14/94 5F E0 00 when there is no disk in the
				// drive. Bit 7 of drive status depends on the last FM/MFM state;
				// bit 4 of drive status and bit 6 of FDC status updates dynamically
				// based on the write protect sensor. Bit 2 of FDC status (lost data)
				// retains the previous state.
				mFDCStatus &= 0x7F;
				mFDCStatus |= 0x32;

				// sector not found....
				BeginTransferACKCmd();

				// if the drive supports the not ready signal, return error immediately,
				// otherwise simulate rotations trying to find a sector
				if (!mpProfile->mbSupportedNotReady)
					WarpOrDelay(mpProfile->mCyclesPerDiskRotation * 2, 1000);

				// don't clear the sector buffer!
				SendResult(false, 128);

				g_ATLCDisk("Reporting missing sector %d (no disk in drive).\n", sector);
				EndCommand();
				return;
			}

			if (!sector || sector > (uint32)mTotalSectorCount) {
				// NAK the command immediately -- the 810 and 1050 both NAK commands
				// with invalid sector numbers.

				mbLastOpError = true;
				SetupTransferSpeed(mbCommandFrameHighSpeed);
				mpSIOMgr->SendNAK();
				g_ATLCDisk("Error reading sector %d.\n", sector);
				EndCommand();
				return;
			}

			BeginTransferACKCmd();
			mActiveCommandState = 10;
			break;
		}

		case 20: {
			IATDiskImage *image = mpDiskInterface->GetDiskImage();

			if (!image)
				mActiveCommandPhysSector = -1;

			ATDiskPhysicalSectorInfo psi = {};
			if (mActiveCommandPhysSector >= 0)
				image->GetPhysicalSectorInfo((uint32)mActiveCommandPhysSector, psi);

			// Warp disk to beginning of sector, if it isn't already there.
			UpdateRotationalCounter();

			mRotationalCounter = mTransferCompleteRotPos;

			// Check if this is a boot sector. If so, force the length to 128 bytes per protocol.
			uint32 transferLength = mSectorSize;

			if (image && sector <= image->GetBootSectorCount())
				transferLength = 128;

			// Add time to read sector and compute checksum.
			//
			// sector read: ~130 bytes at 125Kbits/sec = ~8.3ms = ~14891 cycles
			const uint32 waitLength = mpProfile->mbWaitForLongSectors ? psi.mPhysicalSize : transferLength;
			const uint32 sectorReadDelay = ((mbMFM ? 7445 : 14891) * waitLength + 64) / 128;

			// FDC reset and checksum: ~2568 cycles @ 500KHz = 9192 cycles
			const uint32 postReadDelay = mbMFM ? kCyclesPostReadDelay_1050 : kCyclesPostReadDelay_810;

			WarpOrDelay(sectorReadDelay + postReadDelay, kCyclesPostReadDelay_Fast);

			// check for missing sector
			// note: must send ACK (41) + ERROR (45) -- BeachHead expects to get DERROR from SIO
			if (mActiveCommandPhysSector < 0 || psi.mImageSize == 0) {
				mbLastOpError = true;

				// sector not found....
				// don't clear the sector buffer!
				SendResult(false, transferLength);

				g_ATLCDisk("Reporting missing sector %d.\n", sector);
				EndCommand();
				return;
			}

			const uint32 readLength = psi.mPhysicalSize;

			try {
				image->ReadPhysicalSector((uint32)mActiveCommandPhysSector, mSendPacket, readLength);
			} catch(const MyError&) {
				// wipe sector and report CRC error
				memset(mSendPacket, 0, readLength);
				mFDCStatus = 0xF7;
			}
			
			mbLastOpError = (mFDCStatus != 0xFF);

			// check for CRC error
			// must return data on CRC error -- Koronis Rift requires this
			bool successful = true;
			if (~mFDCStatus & 0x2E) {
				successful = false;

				// Check if we should emulate weak bits.
				if (psi.mWeakDataOffset >= 0) {
					for(int i = psi.mWeakDataOffset; i < (int)readLength; ++i) {
						mSendPacket[i] ^= (uint8)mWeakBitLFSR;

						mWeakBitLFSR = (mWeakBitLFSR << 8) + (0xff & ((mWeakBitLFSR >> (28 - 8)) ^ (mWeakBitLFSR >> (31 - 8))));
					}
				}
			}

			SendResult(successful, transferLength);

			if (g_ATLCDisk.IsEnabled()) {
				Wait(21);
				break;
			}

			[[fallthrough]];
		}

		case 21:
			if (g_ATLCDisk.IsEnabled()) {
				if (IATDiskImage *image = mpDiskInterface->GetDiskImage()) {
					UpdateRotationalCounter();

					ATDiskVirtualSectorInfo vsi = {};
					image->GetVirtualSectorInfo(mActiveCommandSector - 1, vsi);

					ATDiskPhysicalSectorInfo psi = {};
					image->GetPhysicalSectorInfo(mActiveCommandPhysSector, psi);

					g_ATLCDisk("Reading vsec=%3d (%d/%d) (trk=%d), psec=%3d, chk=%02x, rot=%.2f >> %.2f >> %.2f%s.\n"
							, sector
							, (uint32)mActiveCommandPhysSector - vsi.mStartPhysSector + 1
							, vsi.mNumPhysSectors
							, (sector - 1) / mSectorsPerTrack
							, (uint32)mActiveCommandPhysSector
							, Checksum(mSendPacket, mTransferLength)
							, mActiveCommandStartRotPos
							, psi.mRotPos
							, (float)mRotationalCounter / (float)mpProfile->mCyclesPerDiskRotation
							,  psi.mWeakDataOffset >= 0 ? " (w/weak bits)"
								: !(mFDCStatus & 0x02) ? " (w/long sector)"		// must use DRQ as lost data differs between drives
								: !(mFDCStatus & 0x08) ? " (w/CRC error)"
								: !(mFDCStatus & 0x10) ? " (w/missing sector)"
								: !(mFDCStatus & 0x20) ? " (w/deleted sector)"
								: ""
							);
				}
			}

			EndCommand();
			break;
	}
}

void ATDiskEmulator::ProcessCommandWrite() {
	if (ProcessCommandReadWriteCommon(true))
		return;

	switch(mActiveCommandState) {
		case 0: {
			mLastSector = mActiveCommandSector;

			mpDiskInterface->CheckSectorBreakpoint(mActiveCommandSector);

			// check if we have a happy drive memory request
			if (mEmuMode == kATDiskEmulationMode_Happy810 && mActiveCommandSector >= 0x0800) {
				// Address must be in $0800-137F.
				if (mActiveCommandSector < 0x0800 || mActiveCommandSector > 0x1380) {
					// NAK the command.
					mbLastOpError = true;
					BeginTransferNAKCommand();
					SendResult(false, mSectorSize);
					EndCommand();
					return;
				}

				mbLastOpError = false;
				BeginTransferACKCmd();
				BeginReceive(mSectorSize);
				Wait(60);
				return;
			} else if (mEmuMode == kATDiskEmulationMode_Happy1050 && mActiveCommandSector >= 0x8000) {
				// Address must be in $8000-97FF.
				if (mActiveCommandSector >= 0x9800) {
					// NAK the command.
					mbLastOpError = true;
					BeginTransferNAKCommand();
					SendResult(false, mSectorSize);
					EndCommand();
					return;
				}

				mbLastOpError = false;
				BeginTransferACKCmd();
				BeginReceive(mSectorSize);
				Wait(50);
				return;
			}

			IATDiskImage *image = mpDiskInterface->GetDiskImage();
			if (!image || !mActiveCommandSector || mActiveCommandSector > (uint32)mTotalSectorCount) {
				// NAK the command immediately -- the 810 and 1050 both NAK commands
				// with invalid sector numbers.
				mbLastOpError = true;
				BeginTransferNAKCommand();
				EndCommand();
				g_ATLCDisk("Error writing sector %d.\n", mActiveCommandSector);
				break;
			}

			mbLastOpError = false;
			BeginTransferACKCmd();

			// wait for remaining data
			BeginReceive(image->GetSectorSize(mActiveCommandSector - 1));

			g_ATLCDisk("Sent ACK, now waiting for write data.\n");
			// enter common path to turn on motor and seek
			Wait(10);
			break;
		}

		case 20: {
			// check if we don't have a disk anymore; if so, report record not found
			IATDiskImage *image = mpDiskInterface->GetDiskImage();
			if (!image) {
				mFDCStatus = 0xEF;
				mbLastOpError = true;

				BeginTransferError();
				EndCommand();
				return;
			}

			// check if disk is write protected -- 810 and 1050 do this post-seek
			if (!mbWriteEnabled) {
				mFDCStatus = 0xBF;
				mbLastOpError = true;

				BeginTransferError();
				EndCommand();
				return;
			}

			// get virtual sector information
			ATDiskVirtualSectorInfo vsi;
			image->GetVirtualSectorInfo(mActiveCommandSector - 1, vsi);

			ATDiskPhysicalSectorInfo psi;
			image->GetPhysicalSectorInfo(vsi.mStartPhysSector, psi);

			// commit data to physical sector
			g_ATLCDisk("Writing vsec=%3u, psec=%3u.\n", mActiveCommandSector, vsi.mStartPhysSector);

			const uint32 writeLength = psi.mPhysicalSize;

			try {
				image->WritePhysicalSector(vsi.mStartPhysSector, mReceivePacket, writeLength);

				// set FDC status
				mFDCStatus = 0xFF;
				mbLastOpError = false;
			} catch(const MyError&) {
				mFDCStatus = 0xF7;	// crc error
				mbLastOpError = true;
			}

			mpDiskInterface->OnDiskModified();

			uint32 rotDelay = kCyclesRotationalDelay_Fast;

			if (mbAccurateSectorTiming) {
				// compute rotational delay
				UpdateRotationalCounter();
				uint32 rotPos = VDRoundToInt(psi.mRotPos * mpProfile->mCyclesPerDiskRotation);

				rotDelay = rotPos < mRotationalCounter ? (rotPos - mRotationalCounter) + mpProfile->mCyclesPerDiskRotation : (rotPos - mRotationalCounter);

				// add verify delay if we're doing write w/verify
				if (mActiveCommand == 'W')
					rotDelay += mpProfile->mCyclesPerDiskRotation;

				rotDelay += 10000;	// fudge factor
			}

			// If acceleration is disabled, wait a minimum of 400us + std byte to give SIO
			// enough time to turn around between ACK and Complete. The SIO spec says 250us,
			// but the stock OS really needs more time than that because it enables the IRQ
			// too early.
			WarpOrDelay(rotDelay, (716+932) - std::min<uint32>(932, mpProfile->mCyclesPerSIOByte));

			if (mbLastOpError)
				BeginTransferError();
			else
				BeginTransferComplete();

			EndCommand();
			break;
		}

		case 50:
			BeginTransferACK();
			WarpOrDelay(500, 500);
			BeginTransferComplete();

			// Enable custom commands if the write is >=$9600; hooking requires
			// writing somewhere within $9600-97FF.
			if (mActiveCommandSector >= 0x9600)
				mCustomCodeState = 1;

			memcpy(mDriveRAM + (mActiveCommandSector - 0x8000), mReceivePacket, mSectorSize);
			EndCommand();
			break;

		case 60:
			BeginTransferACK();
			WarpOrDelay(500, 500);
			BeginTransferComplete();

			memcpy(mDriveRAM + (mActiveCommandSector - 0x0800), mReceivePacket, mSectorSize);
			EndCommand();
			break;
	}
}

void ATDiskEmulator::ProcessCommandReadPERCOMBlock() {
	if (!mpProfile->mbSupportedCmdPERCOM)
		return ProcessUnsupportedCommand();

	BeginTransferACKCmd();

	WarpOrDelayFromStopBit(mpProfile->mCyclesACKStopBitToReadPERCOMComplete);

	memcpy(mSendPacket, mPERCOM, 12);

	if (mEmuMode == kATDiskEmulationMode_XF551) {
		mSendPacket[1] = 0;		// step rate 0
		mSendPacket[8] = 1;		// drive active
	}

	const int sectorSize = VDReadUnalignedBEU16(&mPERCOM[6]);
	const int sectorCount = mPERCOM[0] * (sint32)VDReadUnalignedBEU16(&mPERCOM[2]) * (mPERCOM[4] + 1);
	g_ATLCDisk("Reading PERCOM data: %u sectors of %u bytes each, %u boot sectors\n", sectorCount, sectorSize, sectorSize > 256 ? 0 : 3);

	SendResult(true, 12);
	EndCommand();
}

void ATDiskEmulator::ProcessCommandWritePERCOMBlock() {
	switch(mActiveCommandState) {
		case 0:
			if (!mpProfile->mbSupportedCmdPERCOM)
				return ProcessUnsupportedCommand();

			BeginTransferACKCmd();
			BeginReceive(12);

			g_ATLCDisk("Sent ACK, now waiting for PERCOM block data.\n");
			Wait(1);
			break;

		case 1: {
			// validate PERCOM data
			bool valid = SetPERCOMData(mReceivePacket);

			if (!valid) {
				BeginTransferError();
				EndCommand();
				return;
			}

			mFDCStatus = 0xFF;
			mbLastOpError = false;

			WarpOrDelay(450, 450);
			BeginTransferComplete();
			EndCommand();
			break;
		}

		default:
			ProcessUnhandledCommandState();
			break;
	}
}

void ATDiskEmulator::ProcessCommandGetHighSpeedIndex() {
	if (!mpProfile->mbSupportedCmdGetHighSpeedIndex)
		return ProcessUnsupportedCommand();

	BeginTransferACKCmd();

	mSendPacket[0] = mpProfile->mHighSpeedIndex;

	WarpOrDelay(450, 450);

	SendResult(true, 1);
	EndCommand();
}

void ATDiskEmulator::ProcessCommandHappy() {
	if (mEmuMode != kATDiskEmulationMode_Happy1050)
		return ProcessUnsupportedCommand();

	BeginTransferACKCmd();

	WarpOrDelay(450, 450);

	BeginTransferComplete();

	EndCommand();
}

void ATDiskEmulator::ProcessCommandHappyQuiet() {
	if (mEmuMode == kATDiskEmulationMode_Happy810) {
		// The "Q" command isn't actually implemented in the Happy 810 firmware; it's just
		// a vector that can be taken over when uploading code. Since we can't actually emulate
		// that, for now we pretend it's a command that sends back a sector. This lie is enough
		// to get past the diagnostics.
		BeginTransferACKCmd();

		WarpOrDelay(450, 450);

		memset(mSendPacket, 0, 0x80);
		SendResult(true, 0x80);
		EndCommand();
	} else if (mEmuMode == kATDiskEmulationMode_Happy1050) {
		BeginTransferACKCmd();

		WarpOrDelay(450, 450);

		TurnOffMotor();

		BeginTransferComplete();

		EndCommand();
	} else
		return ProcessUnsupportedCommand();
}

void ATDiskEmulator::ProcessCommandHappyRAMTest() {
	if (mEmuMode != kATDiskEmulationMode_Happy1050 || !mCustomCodeState)
		return ProcessUnsupportedCommand();

	BeginTransferACKCmd();

	WarpOrDelay(450, 450);

	memset(mSendPacket, 0, 128);

	SendResult(true, 128);
	EndCommand();
}

void ATDiskEmulator::ProcessCommandHappyHeadPosTest() {
	switch(mActiveCommandState) {
		case 0: {
			if (mEmuMode != kATDiskEmulationMode_Happy1050 || !mCustomCodeState)
				return ProcessUnsupportedCommand();

			BeginTransferACKCmd();

			WarpOrDelay(450, 450);

			uint32 track = 0;
			if (mActiveCommand == 0x29)
				track = ~mActiveCommandSector & 0xff;

			if (track >= 40) {
				BeginTransferError();
				EndCommand();
				break;
			}

			// motor on
			if (TurnOnMotor())
				WarpOrDelay(7159090/8);

			// check if we need to seek
			int trackDelta = (int)track - (int)mCurrentTrack;
			uint32 tracksToStep = (uint32)abs(trackDelta);

			mCurrentTrack = track;

			uint32 opDelay = 0;
			if (tracksToStep) {
				if (trackDelta > 0) {
					PlaySeekSound(opDelay, tracksToStep + 1);
					opDelay += (tracksToStep + 1) * mpProfile->mCyclesPerTrackStep;
				} else {
					PlaySeekSound(opDelay, tracksToStep);
					opDelay += tracksToStep * mpProfile->mCyclesPerTrackStep;
				}

				opDelay += mpProfile->mCyclesForHeadSettle;
			}

			WarpOrDelay(opDelay);
			Wait(1);
			break;
		}

		case 1:
			BeginTransferComplete();
			EndCommand();
			break;

		default:
			ProcessUnhandledCommandState();
			break;
	}
}

void ATDiskEmulator::ProcessCommandHappyRPMTest() {
	switch(mActiveCommandState) {
		case 0:
			if (mEmuMode != kATDiskEmulationMode_Happy1050 || !mCustomCodeState)
				return ProcessUnsupportedCommand();

			BeginTransferACKCmd();

			WarpOrDelay(450, 450);
			Wait(1);
			break;

		case 1:
			// Wait for the index mark, then another index mark.
			UpdateRotationalCounter();
			WarpOrDelay(mpProfile->mCyclesPerDiskRotation + (mpProfile->mCyclesPerDiskRotation - mRotationalCounter));

			memset(mSendPacket, 0, 128);

			// The return value is related as follows:
			// rval = 2000000 / (RPM - 0.25)
			VDWriteUnalignedLEU16(mSendPacket, 6952);

			SendResult(true, 128);
			EndCommand();
			break;

		default:
			ProcessUnhandledCommandState();
			break;
	}
}

void ATDiskEmulator::ProcessCommandExecuteIndusGT() {
	switch(mActiveCommandState) {
		case 0:
			if (mEmuMode != kATDiskEmulationMode_IndusGT)
				return ProcessUnsupportedCommand();

			mbLastOpError = false;
				
			// There's a lot of voodoo here that needs explaining.
			//
			// The Indus GT allows code to be uploaded to the drive at address 7F00h. This is
			// done by sending AUX1=len, AUX2=odd. The custom code is then called with
			// AUX2 even.
			//
			// Since we don't have actual Z80 emulation, we detect the code fragments uploaded
			// via AUX1 even and emulate the behavior on AUX1 odd. There are a few code fragments
			// that we are interested in:
			//
			//	Version check:
			//		Returns the major/minor version of the drive in a two-byte packet. We
			//		return $01 20 for 1.20.
			//
			//	Synchromesh loader:
			//		Uploads $0367 bytes to the drive at address 7B84h. This is done in ascending
			//		order, but strangely the first packet is the short packet ($67 bytes) and
			//		the remaining packets are $100 bytes. The last packet is an empty packet.
			//
			//	SuperSynchromesh loader:
			//		Uploads $0369 bytes to the drive at address 7B84h, similarly to the Synchromesh
			//		loader. After this, another $02F5 bytes are uploaded at 7840h to support the
			//		RamCharger.
			//
			//	SuperSynchromesh loader (SDX INDUS.SYS version):
			//		Similar to SuperSynchromesh, but with the RamCharger part omitted.

			if (mActiveCommandSector & 0x100) {
				BeginTransferACKCmd();

				mActiveCommandSector &= 0xFF;

				// wait for remaining data
				BeginReceive(mActiveCommandSector);

				g_ATLCDisk("Sent ACK, now waiting for upload data.\n");
				Wait(3);
				return;
			}
			
			if (mActiveCommandSector >= 0x100) {
				BeginTransferNAKCommand();
				EndCommand();
				break;
			}

			switch(mCustomCodeState) {
				default:
				case 0:
					BeginTransferACKCmd();
					BeginTransferError();
					EndCommand();
					return;

				case 1:
					BeginTransferACKCmd();
					mSendPacket[0] = 0x20;
					mSendPacket[1] = 0x01;
					SendResult(true, 2);
					mCustomCodeState = 0;
					EndCommand();
					return;
					break;

				case 2:
				case 3:
				case 4:
				case 5:
				case 7:
				case 8:
				case 9:
				case 10:
				case 12:
				case 13:
				case 14:
				case 16:
				case 17:
				case 18:
				case 19:
					BeginTransferACKCmd();
					mActiveCommandState = 10;
					mActiveCommandSector = 0x100;

					if (mCustomCodeState == 2)
						mActiveCommandSector = 0x67;
					if (mCustomCodeState == 7 || mCustomCodeState == 16)
						mActiveCommandSector = 0x69;
					if (mCustomCodeState == 12)
						mActiveCommandSector = 0xF5;
					break;

				case 6:
				case 11:
				case 15:
				case 20:
					BeginTransferACKCmd();
					mpSIOMgr->Delay(1000);
					BeginTransferComplete();

					if (mCustomCodeState == 6) {
						g_ATLCDisk("Firmware upload recognized. Enabling Synchromesh operation.\n");
						mpProfile = &ATGetDiskProfileIndusGTSynchromesh();
					} else if (mCustomCodeState == 11 || mCustomCodeState == 20) {
						g_ATLCDisk("Firmware upload recognized. Enabling SuperSynchromesh operation.\n");

						mpProfile = &ATGetDiskProfileIndusGTSuperSynchromesh();
						if (mCustomCodeState == 11)
							mCustomCodeState = 12;
						else
							mCustomCodeState = 0;
					} else {
						g_ATLCDisk("Firmware upload recognized. Ignoring RamCharger firmware.\n");
						mCustomCodeState = 0;
					}

					EndCommand();
					return;
			}

			// wait for remaining data
			BeginReceive(mActiveCommandSector);

			g_ATLCDisk("Sent ACK, now waiting for upload data (%u bytes).\n", mActiveCommandSector);
			Wait(10);

			break;

		case 3: {
			mCustomCodeState = 0;

			bool successful = false;

			// The Indus GT accepts arbitrary code uploads to 7F00h, but we can't
			// support that since we aren't actually emulating the Z80. Instead, we
			// cheat and identify+simulate the code upload. The fragments we recognize:
			//
			//	Length	Checksum	Desc
			//	$19		$52			Version check (returns major/minor version)
			//	$8A		$EB			Synchromesh uploader
			//	$E5		$45			SuperSynchromesh uploader
			//	$E5		$F0			SuperSynchromesh uploader w/o RamCharger (used by SDX)
			const uint8 checksum = Checksum(mReceivePacket, mActiveCommandSector);

			if (mActiveCommandSector == 0x19 && checksum == 0x52) {
				mCustomCodeState = 1;
				g_ATLCDisk("Accepting code upload: Indus GT version check.\n");
				successful = true;
			} else if (mActiveCommandSector == 0x8A && checksum == 0xEB) {
				mCustomCodeState = 2;
				g_ATLCDisk("Accepting code upload: Indus GT Synchromesh firmware loader.\n");
				successful = true;
			} else if (mActiveCommandSector == 0xE5 && checksum == 0x45) {
				mCustomCodeState = 7;
				g_ATLCDisk("Accepting code upload: Indus GT SuperSynchromesh + RamCharger firmware loader.\n");
				successful = true;
			} else if (mActiveCommandSector == 0xE5 && checksum == 0xF0) {
				mCustomCodeState = 16;
				g_ATLCDisk("Accepting code upload: Indus GT SuperSynchromesh firmware loader.\n");
				successful = true;
			} else {
				g_ATLCDisk("Rejecting unknown code upload: len=$%04X bytes, checksum=$%02X\n", mActiveCommandSector, checksum);
			}

			WarpOrDelay(1000);
			
			if (successful)
				BeginTransferComplete();
			else
				BeginTransferError();

			EndCommand();
			break;
		}

		case 10: {
			static const uint8 kCodeChecksums[] = {
				// state 2 - Synchromesh
				0x49,
				0x22,
				0xA0,
				0xCF,
				0x00,

				// state 7 - SuperSynchromesh
				0xDA,
				0xBA,
				0x62,
				0xF5,
				0x00,

				// state 12 - RamCharger
				0xA4,
				0xEE,
				0x96,
				0x00,

				// state 16 - SuperSynchromesh patched
				0x6E,
				0xBA,
				0x62,
				0xF5,
				0x00,
			};

			const uint8 checksum = Checksum(mReceivePacket, mActiveCommandSector);
			const uint8 expected = kCodeChecksums[mCustomCodeState - 2];

			if (checksum != expected) {
				g_ATLCDisk("Rejecting Synchromesh code upload: state=%d, chksum=$%02X, expected=$%02X\n", mCustomCodeState, checksum, expected);

				mCustomCodeState = 0;
				WarpOrDelay(1000);
				BeginTransferError();
				EndCommand();
				return;
			}

			++mCustomCodeState;

			WarpOrDelay(1000);
			BeginTransferComplete();
			EndCommand();
			break;
		}

		default:
			ProcessUnhandledCommandState();
			break;
	}
}

void ATDiskEmulator::ProcessCommandFormat() {
	switch(mActiveCommandState) {
		case 0:
			if (mOriginalCommand == 0xA3) {
				if (!mpProfile->mbSupportedCmdFormatBoot)
					return ProcessUnsupportedCommand();
			} else if ((mOriginalCommand & 0x7F) == 0x22) {
				if (mEmuMode == kATDiskEmulationMode_810)
					return ProcessUnsupportedCommand();
			} else if ((mOriginalCommand & 0x7F) == 0x66) {
				if (!mpProfile->mbSupportedCmdFormatSkewed)
					return ProcessUnsupportedCommand();
			} else if ((mOriginalCommand & 0x7F) == 0x21 && mEmuMode == kATDiskEmulationMode_XF551) {
				// The XF551 does not allow command $21 to format medium density, forcing single
				// density instead.
				if (mPERCOM[3] >= 26) {
					// force XF551 single density
					mPERCOM[2] = 0;		// spt high
					mPERCOM[3] = 18;	// spt low
					mPERCOM[4] = 0;		// sides minus one
					mPERCOM[5] = 0;		// FM/MFM encoding
					mPERCOM[6] = 0;		// bps high
					mPERCOM[7] = 128;	// bps low
				}
			}

			TurnOnMotor();

			// Disable high speed operation if we're getting an XF551 command -- the high bit
			// is used for sector skew and not high speed. This must NOT be done for the Indus
			// GT since the Synchromesh and SuperSynchromesh firmwares use high speed all the
			// way through.
			if (mOriginalCommand == 0xA1 && mEmuMode == kATDiskEmulationMode_XF551)
				mbActiveCommandHighSpeed = false;

			if (!mbWriteEnabled || !mbFormatEnabled) {
				if (mbWriteEnabled)
					g_ATLCDisk("FORMAT COMMAND RECEIVED. Blocking due to format-protected disk!\n");
				else
					g_ATLCDisk("FORMAT COMMAND RECEIVED. Blocking due to read-only disk!\n");

				// The FORMAT command always sends an ACK first and then sends ERROR instead of
				// COMPLETE if the disk is write protected. In that case, we need to send a data
				// frame.
				const uint32 sectorSize = ((mOriginalCommand & 0x7F) == 0x22) ? 128 : mSectorSize;
				BeginTransferACK();
				mSendPacket[0] = 0xFF;		// Sector terminator (sector buffer data)
				mSendPacket[1] = 0xFF;
				memset(mSendPacket + 2, 0, sectorSize - 2);
				mSendPacket[sectorSize] = 0xFF;
				mbLastOpError = true;

				// Assert FDC status bit 6 (write protect).
				mFDCStatus = 0xBF;

				SendResult(false, sectorSize);
				EndCommand();
				return;
			}

			mbLastOpError = false;
			BeginTransferACKCmd();

			// Check if we are doing the format skewed command. If so, we must wait for a data frame.
			if ((mOriginalCommand & 0x7F) == 0x66) {
				// wait for remaining data
				BeginReceive(128);

				g_ATLCDisk("Sent ACK, now waiting for PERCOM and sector skew data.\n");
				Wait(2);
			} else {
				Wait(3);
			}
			break;

		case 2:
			// validate PERCOM data
			if (!SetPERCOMData(mReceivePacket)) {
				BeginTransferError();
				EndCommand();
			}

			Wait(3);
			break;

		case 3: {
			// If we are doing this on an 810 or 1050, reset the PERCOM block to default.
			if ((mOriginalCommand & 0x7F) == 0x22) {
				memcpy(mPERCOM, kDefaultPERCOMED, sizeof mPERCOM);
			} else {
				switch(mEmuMode) {
					case kATDiskEmulationMode_810:
					case kATDiskEmulationMode_1050:
						memcpy(mPERCOM, kDefaultPERCOM, sizeof mPERCOM);
						break;
				}
			}

			int formatSectorSize = VDReadUnalignedBEU16(&mPERCOM[6]);
			int formatTracks = mPERCOM[0];
			int formatSides = mPERCOM[4] + 1;
			int formatSectorsPerTrack = VDReadUnalignedBEU16(&mPERCOM[2]);
			int formatSectorCount = formatTracks * formatSectorsPerTrack * formatSides;
			int formatBootSectorCount = formatSectorSize >= 512 ? 0 : 3;

			if (mOriginalCommand == 0xA3) {
				g_ATLCDisk("Boot track format command received. Silently ignoring as we don't support partially formatted disks.\n");
			} else {
				g_ATLCDisk("FORMAT COMMAND RECEIVED. Reformatting disk as %u sectors of %u bytes each.\n", formatSectorCount, formatSectorSize);

				ATDiskGeometryInfo geometry {};
				geometry.mSectorSize = formatSectorSize;
				geometry.mBootSectorCount = formatBootSectorCount;
				geometry.mTotalSectorCount = formatSectorCount;
				geometry.mTrackCount = formatTracks;
				geometry.mSectorsPerTrack = formatSectorsPerTrack;
				geometry.mSideCount = formatSides;
				geometry.mbMFM = (mPERCOM[5] & 4) != 0;
				geometry.mbHighDensity = (mPERCOM[5] & 2) != 0;
				mpDiskInterface->FormatDisk(geometry);
			}

			mActiveCommandSector = 0;

			if (mbAccurateSectorTiming) {
				mpSIOMgr->Delay(1000);
				Wait(4);
			} else {
				mpSIOMgr->Delay(1000000);
				Wait(5);
			}
			break;
		}

		case 4:
			{
				// Check if the number of tracks is not realistic; if not, just do a fake format.
				// Check if we're done.
				if ((mActiveCommandSector == 0 && (mTrackCount < 40 || mTrackCount > 80)) || mActiveCommandSector >= mTrackCount * 2) {
					Wait(5);
					break;
				}

				// Keep the motor running
				TurnOnMotor();

				// Seek to track
				uint32 track = mActiveCommandSector % mTrackCount;

				uint32 tracksToStep = (uint32)abs((int)mCurrentTrack - (int)track);
				PlaySeekSound(0, tracksToStep);
				mCurrentTrack = track;

				// Update activity
				mpDiskInterface->SetShowActivity(true, track);

				// update state for next track
				++mActiveCommandSector;

				// delay for at least two revs regardless; we are assuming seek delay to blow a rev or more and then index-to-index format
				uint32 delay = tracksToStep ? mpProfile->mCyclesPerTrackStep * tracksToStep + mpProfile->mCyclesForHeadSettle : 1;

				delay = ((delay - 1) / mpProfile->mCyclesPerDiskRotation + 2) * mpProfile->mCyclesPerDiskRotation;

				mpSIOMgr->Delay(delay);
				Wait(4);
			}
			break;

		case 5:
			memset(mSendPacket, 0xFF, mSectorSize);
			SendResult(true, mSectorSize);

			EndCommand();
			break;

		default:
			ProcessUnhandledCommandState();
			break;
	}
}

void ATDiskEmulator::ProcessUnhandledCommandState() {
	EndCommand();
}

void ATDiskEmulator::ComputeGeometry() {
	IATDiskImage *image = mpDiskInterface->GetDiskImage();

	if (image) {
		const ATDiskGeometryInfo& info = image->GetGeometry();

		mTrackCount = info.mTrackCount;
		mSideCount = info.mSideCount;
		mbMFM = info.mbMFM;
		mbHighDensity = info.mbHighDensity;
		mSectorsPerTrack = info.mSectorsPerTrack;
	} else {
		mTrackCount = 0;
		mSideCount = 0;
		mbMFM = false;
		mbHighDensity = false;
		mSectorsPerTrack = 0;
	}
}

void ATDiskEmulator::ComputePERCOMBlock() {
	// Note that we do not enforce drive invariants (i.e. XF551) here; we do so in the
	// read PERCOM block command instead.

	// track count
	mPERCOM[0] = (uint8)mTrackCount;

	// step rate
	mPERCOM[1] = 0x01;

	// sectors per track
	mPERCOM[2] = (uint8)(mSectorsPerTrack >> 8);
	mPERCOM[3] = (uint8)(mSectorsPerTrack);

	// sides minus one
	mPERCOM[4] = mSideCount ? mSideCount - 1 : 0;

	// record method
	mPERCOM[5] = (mbMFM ? 4 : 0) + (mbHighDensity ? 2 : 0);

	// bytes per sector
	mPERCOM[6] = (uint8)(mSectorSize >> 8);
	mPERCOM[7] = (uint8)mSectorSize;

	// drive online
	mPERCOM[8] = 0xFF;

	// unused
	mPERCOM[9] = 0;
	mPERCOM[10] = 0;
	mPERCOM[11] = 0;
}

void ATDiskEmulator::ComputeSupportedProfile() {
	mpProfile = &ATGetDiskProfile(mEmuMode);
}

bool ATDiskEmulator::SetPERCOMData(const uint8 *data) {
	uint16 sectorSize;
	uint32 sectorCount;

	if (mEmuMode == kATDiskEmulationMode_XF551) {
		// The XF551 is very lax about PERCOM blocks: it simply checks the minimum
		// number of bytes to detect SD, ED, or DD formats.

		if (data[3] == 26) {
			// enhanced density
			mPERCOM[2] = 0;		// spt high
			mPERCOM[3] = 26;
			mPERCOM[4] = 0;		// sides minus one
			mPERCOM[5] = 4;		// FM/MFM encoding
			mPERCOM[6] = 0;		// bps high
			mPERCOM[7] = 128;		// bps low
		} else if (data[6] == 0) {
			// single density
			mPERCOM[2] = 0;		// spt high
			mPERCOM[3] = 18;		// spt low
			mPERCOM[4] = 0;		// sides minus one
			mPERCOM[5] = 0;		// FM/MFM encoding
			mPERCOM[6] = 0;		// bps high
			mPERCOM[7] = 128;		// bps low
		} else {
			if (data[4]) {
				// DSDD
				mPERCOM[ 4] = 1;		// sides minus one
			} else {
				// SSDD
				mPERCOM[ 4] = 0;		// sides minus one
			}

			mPERCOM[2] = 0;		// spt high
			mPERCOM[3] = 18;		// spt low
			mPERCOM[5] = 4;		// FM/MFM encoding
			mPERCOM[6] = 1;		// bps high
			mPERCOM[7] = 0;		// bps low
		}
		
		// force XF551 invariants
		mPERCOM[ 0] = 40;		// 40 tracks
		mPERCOM[ 1] = 0;		// step rate 0
		mPERCOM[ 8] = 1;		// drive active
		mPERCOM[ 9] = 0x41;		// reserved
		mPERCOM[10] = 0;		// reserved
		mPERCOM[11] = 0;		// reserved

		sectorSize = VDReadUnalignedBEU16(&mPERCOM[6]);
		sectorCount = mPERCOM[0] * (sint32)VDReadUnalignedBEU16(&mPERCOM[2]) * (mPERCOM[4] + 1);
	} else {
		sectorSize = VDReadUnalignedBEU16(&data[6]);
		sectorCount = data[0] * (sint32)VDReadUnalignedBEU16(&data[2]) * (data[4] + 1);

		if (data[0] == 0) {
			g_ATLCDisk("Invalid PERCOM data: tracks per sector = 0\n");
			return false;
		}
		
		if (data[2] == 0 && data[3] == 0) {
			g_ATLCDisk("Invalid PERCOM data: sectors per track = 0\n");
			return false;
		}
		
		if (data[4] >= 2) {
			g_ATLCDisk("Invalid PERCOM data: invalid sides encoded value %02x\n", data[4]);
			return false;
		}
		
		if (sectorCount > 65535) {
			g_ATLCDisk("Invalid PERCOM data: total sectors > 65535\n");
			return false;
		}
		
		if (sectorSize != 128 && sectorSize != 256 && sectorSize != 512 && sectorSize != 8192) {
			g_ATLCDisk("Invalid PERCOM data: unsupported sector size (%u)\n", sectorSize);
			return false;
		}

		memcpy(mPERCOM, data, 12);
	}

	g_ATLCDisk("Setting PERCOM data: %u sectors of %u bytes each, %u sides, %u boot sectors\n", sectorCount, sectorSize, mPERCOM[4]+1, sectorSize > 256 ? 0 : 3);
	return true;
}

void ATDiskEmulator::TurnOffMotor() {
	mpScheduler->UnsetEvent(mpMotorOffEvent);

	mpDiskInterface->SetShowMotorActive(false);

	if (mpRotationSoundGroup)
		mpRotationSoundGroup->StopAllSounds();

	if (mEmuMode == kATDiskEmulationMode_810) {
		uint32 endTrack = mTrackCount ? mTrackCount - 1 : 0;
		PlaySeekSound(0, abs((int)endTrack - (int)mCurrentTrack));
		mCurrentTrack = endTrack;
	}
}

bool ATDiskEmulator::TurnOnMotor(uint32 delay) {
	const bool spinUpDelay = !mpMotorOffEvent;
	
	ExtendMotorTimeoutTo(mpProfile->mCyclesToMotorOff);

	if (spinUpDelay) {
		mpDiskInterface->SetShowMotorActive(true);

		if (mpAudioSyncMixer && mbDriveSoundsEnabled && !mpRotationSoundGroup->IsAnySoundQueued())
			mpAudioSyncMixer->AddLoopingSound(*mpRotationSoundGroup, delay, kATAudioSampleId_DiskRotation, 1.0f);

		SetMotorEvent();
	}


	return spinUpDelay;
}

void ATDiskEmulator::ExtendMotorTimeoutBy(uint32 additionalDelay) {
	// We explicitly do NOT turn on the motor if it isn't already on, as this is
	// called from places that are merely simulating not being able to update the
	// idle timer. We also don't update the motor event here; it'll automatically
	// requeue itself when the event fires and the deadline hasn't been met.
	mMotorOffTime += additionalDelay;
}

void ATDiskEmulator::ExtendMotorTimeoutTo(uint32 delay) {
	mMotorOffTime = mpScheduler->GetTick() + delay;

	SetMotorEvent();
}

void ATDiskEmulator::SetMotorEvent() {
	const uint32 t = mpScheduler->GetTick();

	mpSlowScheduler->SetEvent(std::max<uint32>(1, (mMotorOffTime - t + 113) / 114), this, kATDiskEventMotorOff, mpMotorOffEvent);
}

void ATDiskEmulator::PlaySeekSound(uint32 stepDelay, uint32 tracksToStep) {
	if (!mbDriveSoundsEnabled)
		return;

	// limit step length in case we have a synthetic disk or aren't waiting
	if (mbAccurateSectorTiming) {
		if (tracksToStep > 80)
			tracksToStep = 80;
	} else {
		if (tracksToStep > 10)
			tracksToStep = 10;
	}

	if (mpProfile->mbSeekHalfTracks) {
		for(uint32 i=0; i<tracksToStep; ++i) {
			mpAudioSyncMixer->AddSound(*mpStepSoundGroup, stepDelay, kATAudioSampleId_DiskStep3, 1.0f);

			stepDelay += mpProfile->mCyclesPerTrackStep;
		}
	} else {
		switch(mEmuMode) {
		case kATDiskEmulationMode_810:
		case kATDiskEmulationMode_Happy810:
		case kATDiskEmulationMode_Generic:
		case kATDiskEmulationMode_Generic57600:
		case kATDiskEmulationMode_FastestPossible:
			for(uint32 i=0; i<tracksToStep; ++i) {
				mpAudioSyncMixer->AddSound(*mpStepSoundGroup, stepDelay, kATAudioSampleId_DiskStep1, 0.3f + 0.7f * sinf(i * nsVDMath::kfPi * 0.5f));

				stepDelay += mpProfile->mCyclesPerTrackStep;
			}
			break;

		default:
			for(uint32 i=0; i<tracksToStep; i += 2) {
				if (i + 2 > tracksToStep)
					mpAudioSyncMixer->AddSound(*mpStepSoundGroup, stepDelay, kATAudioSampleId_DiskStep2H, 1.0f);
				else
					mpAudioSyncMixer->AddSound(*mpStepSoundGroup, stepDelay, kATAudioSampleId_DiskStep2, 1.0f);

				stepDelay += mpProfile->mCyclesPerTrackStep * 2;
			}
			break;
		}
	}
}
