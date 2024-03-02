//	Altirra - Atari 800/800XL emulator
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
#include <math.h>
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/math.h>
#include <at/atcore/audiomixer.h>
#include <at/atcore/cio.h>
#include <at/atcore/deviceport.h>
#include <at/atcore/enumparseimpl.h>
#include <at/atcore/randomization.h>
#include <at/atcore/vfs.h>
#include <at/atio/cassetteimage.h>
#include <at/atio/wav.h>
#include "cassette.h"
#include "cpu.h"
#include "cpumemory.h"
#include "console.h"
#include "debuggerlog.h"
#include "ksyms.h"
#include "trace.h"
#include "tracetape.h"

using namespace nsVDWinFormats;

ATDebuggerLogChannel g_ATLCCas(true, false, "CAS", "Cassette I/O");
ATDebuggerLogChannel g_ATLCCasData(false, true, "CASDATA", "Cassette data");
ATDebuggerLogChannel g_ATLCCasData2(false, true, "CASDATA2", "Cassette data (extra verbose)");
ATDebuggerLogChannel g_ATLCCasDirectData(false, true, "CASDRDATA", "Cassette direct data");
ATDebuggerLogChannel g_ATLCCasDecoder(false, true, "CASDEC", "Cassette data decoder");

///////////////////////////////////////////////////////////////////////////////

AT_DEFINE_ENUM_TABLE_BEGIN(ATCassetteTurboMode)
	{ kATCassetteTurboMode_None, "none" },
	{ kATCassetteTurboMode_CommandControl, "commandControl" },
	{ kATCassetteTurboMode_ProceedSense, "proceedSense" },
	{ kATCassetteTurboMode_InterruptSense, "interruptSense" },
	{ kATCassetteTurboMode_KSOTurbo2000, "ksoturbo2000" },
	{ kATCassetteTurboMode_Always, "always" },
AT_DEFINE_ENUM_TABLE_END(ATCassetteTurboMode, kATCassetteTurboMode_None)

AT_DEFINE_ENUM_TABLE_BEGIN(ATCassettePolarityMode)
	{ kATCassettePolarityMode_Normal, "normal" },
	{ kATCassettePolarityMode_Inverted, "inverted" },
AT_DEFINE_ENUM_TABLE_END(ATCassettePolarityMode, kATCassettePolarityMode_Normal)

AT_DEFINE_ENUM_TABLE_BEGIN(ATCassetteDirectSenseMode)
	{ ATCassetteDirectSenseMode::LowSpeed, "lowspeed" },
	{ ATCassetteDirectSenseMode::Normal, "normal" },
	{ ATCassetteDirectSenseMode::HighSpeed, "highspeed" },
	{ ATCassetteDirectSenseMode::MaxSpeed, "maxspeed" },
AT_DEFINE_ENUM_TABLE_END(ATCassetteDirectSenseMode, ATCassetteDirectSenseMode::Normal)


///////////////////////////////////////////////////////////////////////////////

namespace {
	enum {
		kATCassetteEventId_ProcessBit = 1,
		kATCassetteEventId_Record = 2,
		kATCassetteEventId_PortUpdate = 3
	};

	// At 600 baud, a byte is completed every 1/60th of a second. We support recording
	// down to about 150 baud. That means we need to wait at least a 15th of a second
	// before writing a gap. POKEY does give us a heads up when a byte starts, though,
	// so we don't need to accommodate the whole byte time, just a possible gap in between.

	// Minimum delay between bytes (end to start) before we put a gap on the tape, in cycles.
	// This is used when we know we need to write a gap, because POKEY is telling us it
	// is starting a new byte.
	const uint32 kRecordMinDelayForGap = 2000;

	// How often we check for committing gaps to tape, in scanlines. We really only need
	// this so that observers of the tape can see blank tape being put down.
	const uint32 kRecordPollPeriod = 500;

	// Maximum delay from end of last byte before we force a gap on tape, in cycles. This
	// is used in case POKEY doesn't finish a byte in expected time and we need to blank
	// the tape that has passed. It needs to be conservative enough to accommodate a byte
	// at 150 baud (~120k cycles).
	const uint32 kRecordMaxDelayForBlank = 140000;
}

///////////////////////////////////////////////////////////////////////////

void ATCassetteEmulator::SlidingWindowCursor::Update(IATCassetteImage& image, uint32 pos) {
	// Approximate limit to prevent forward searches from being too expensive within a single
	// tick, i.e. suddenly scan 40 seconds of FSK-encoded tape. This doesn't need to be precise.
	static constexpr uint32 kSearchLimit = 10000;

	const auto getExtBitSum = [](IATCassetteImage& image, uint32 pos, uint32 offset, uint32 window, bool bypassFSK) -> uint32 {
		uint32 preroll = 0;

		if (pos < offset) {
			preroll = offset - pos;
			pos = offset;
		}

		return preroll + image.GetBitSum(pos - offset, window - preroll, bypassFSK);
	};

	while (pos >= mNextTransition) {
		// check if we need to initialize state after a seek
		if (!mNextTransition) {
			const uint32 headOffset = mWindow - mOffset;

			// initialize head cursor
			mHeadCursor.mCurrentValue = image.GetBit(pos + headOffset, mbFSKBypass);

			const auto nextHead = image.FindNextBit(pos + headOffset + 1, pos + kSearchLimit, !mHeadCursor.mCurrentValue, mbFSKBypass);
			VDASSERT(nextHead.mPos >= headOffset);
			mHeadCursor.mNextTransition = nextHead.mPos - headOffset;
			mHeadCursor.mNextValue = nextHead.mBit;

			// initialize tail cursor -- this may be before the start, in which case we must assume it is mark tone
			mTailCursor.mCurrentValue = pos < mOffset || image.GetBit(pos - mOffset, mbFSKBypass);

			const auto nextTail = image.FindNextBit(std::max(pos, mOffset) - mOffset, pos + kSearchLimit, !mTailCursor.mCurrentValue, mbFSKBypass);
			mTailCursor.mNextTransition = nextTail.mPos + mOffset;
			mTailCursor.mNextValue = nextTail.mBit;

			// initialize decoding state -- just use a simple threshold to start
			mNextCount = getExtBitSum(image, pos, mOffset, mWindow, mbFSKBypass);
			mCurrentValue = mNextCount >= (mWindow >> 1);
			mNextTransition = pos;

			VDASSERT(mHeadCursor.mNextTransition >= pos);
			VDASSERT(mTailCursor.mNextTransition >= pos);
		}

		// assert that sum is still correct
		[[maybe_unused]] uint32 chk;
		VDASSERT(((chk = getExtBitSum(image, mNextTransition, mOffset, mWindow, mbFSKBypass)), mNextCount == chk));

		// flip decoded bit state if we crossed one of the thresholds
		if (mNextCount < mThresholdLo)
			mCurrentValue = false;
		else if (mNextCount > mThresholdHi)
			mCurrentValue = true;

		// update next cursor if it has hit a transition
		if (mNextTransition == mHeadCursor.mNextTransition) {
			mHeadCursor.mCurrentValue = mHeadCursor.mNextValue;
			VDASSERT(image.GetBit(mNextTransition + mWindow - mOffset, mbFSKBypass) == mHeadCursor.mCurrentValue);

			const auto nextHead = image.FindNextBit(mNextTransition + mWindow - mOffset + 1, mNextTransition + kSearchLimit, !mHeadCursor.mCurrentValue, mbFSKBypass);
			VDASSERT(nextHead.mPos >= mWindow - mOffset);
			mHeadCursor.mNextTransition = nextHead.mPos - mWindow + mOffset;
			mHeadCursor.mNextValue = nextHead.mBit;

			VDASSERT(image.GetBit(nextHead.mPos, mbFSKBypass) == mHeadCursor.mNextValue);
		} else {
			VDASSERT(mNextTransition < mHeadCursor.mNextTransition);
		}

		// update tail cursor if it has hit a transition
		if (mNextTransition == mTailCursor.mNextTransition) {
			mTailCursor.mCurrentValue = mTailCursor.mNextValue;
			VDASSERT(image.GetBit(mNextTransition - mOffset, mbFSKBypass) == mTailCursor.mCurrentValue);

			const auto nextTail = image.FindNextBit(mNextTransition - mOffset + 1, mNextTransition + kSearchLimit, !mTailCursor.mCurrentValue, mbFSKBypass);
			mTailCursor.mNextTransition = nextTail.mPos + mOffset;

			// clamp to avoid rolling around if we got +inf
			if (mTailCursor.mNextTransition < nextTail.mPos)
				mTailCursor.mNextTransition = ~UINT32_C(0);

			mTailCursor.mNextValue = nextTail.mBit;

			VDASSERT(image.GetBit(nextTail.mPos, mbFSKBypass) == mTailCursor.mNextValue);
		} else {
			VDASSERT(mNextTransition < mTailCursor.mNextTransition);
		}

		// compute current slope and how far we can project until either the head or tail changes polarity
		const sint32 slope = (mHeadCursor.mCurrentValue ? 1 : 0) - (mTailCursor.mCurrentValue ? 1 : 0);
		uint32 dist = std::min(mHeadCursor.mNextTransition, mTailCursor.mNextTransition) - mNextTransition;

		// check if the current projection would cross a threshold and truncate it
		// if so
		if (mCurrentValue) {
			if (slope < 0 && mNextCount >= mThresholdLo)
				dist = std::min(dist, mNextCount - mThresholdLo + 1);
		} else {
			if (slope > 0 && mNextCount <= mThresholdHi)
				dist = std::min(dist, mThresholdHi - mNextCount + 1);
		}

		// project forward to next transition point
		VDASSERT(dist <= ~mNextTransition);
		mNextTransition += dist;

		mNextCount += (uint32)slope * dist;
		VDASSERT(mNextCount <= mWindow);

		// assert that sum is correct after transition
		[[maybe_unused]] uint32 chk2;
		VDASSERT(((chk2 = getExtBitSum(image, mNextTransition, mOffset, mWindow, mbFSKBypass)) == mNextCount));
	}

	[[maybe_unused]] uint32 chk3;
	VDASSERT(((chk3 = getExtBitSum(image, pos, mOffset, mWindow, mbFSKBypass)), (mCurrentValue ? chk3 >= mThresholdLo : chk3 <= mThresholdHi)));
}

///////////////////////////////////////////////////////////////////////////

ATCassetteEmulator::ATCassetteEmulator() {
	mDirectSenseMode = ATCassetteDirectSenseMode::Normal;
	mTurboDecodeAlgorithm = ATCassetteTurboDecodeAlgorithm::PeakFilter;
}

ATCassetteEmulator::~ATCassetteEmulator() {
	VDASSERT(!mpScheduler);
}

float ATCassetteEmulator::GetLength() const {
	return mLength / kATCassetteDataSampleRate;
}

float ATCassetteEmulator::GetPosition() const {
	return mPosition / kATCassetteDataSampleRate;
}

sint32 ATCassetteEmulator::GetSampleCycleOffset() const {
	if (!mpPlayEvent)
		return 0;

	return mLastSampleOffset - mpScheduler->GetTicksToEvent(mpPlayEvent);
}

float ATCassetteEmulator::GetLastStopPosition() const {
	return mLastStopPosition / kATCassetteDataSampleRate;
}

void ATCassetteEmulator::Init(ATPokeyEmulator *pokey, ATScheduler *sched, ATScheduler *slowsched, IATAudioMixer *mixer, ATDeferredEventManager *defmgr, IATDeviceSIOManager *sioMgr, IATDevicePortManager *portMgr) {
	mpPokey = pokey;
	mpSIOMgr = sioMgr;
	mpPortMgr = portMgr;
	mpScheduler = sched;
	mpSlowScheduler = slowsched;
	mpAudioMixer = mixer;

	UpdateDirectSenseParameters();

	PositionChanged.Init(defmgr);
	PlayStateChanged.Init(defmgr);
	TapePeaksUpdated.Init(defmgr);
	TapeDirtyStateChanged.Init(defmgr);

	mixer->AddSyncAudioSource(this);

	mbFSKDecoderEnabled = true;
	mBitCursor.mbFSKBypass = false;

	ResetCursors();
	RewindToStart();
	ColdReset();
}

void ATCassetteEmulator::Shutdown() {
	TapeDirtyStateChanged.Shutdown();
	TapePeaksUpdated.Shutdown();
	PositionChanged.Shutdown();
	PlayStateChanged.Shutdown();

	if (mpPortMgr) {
		if (mPortInput >= 0) {
			mpPortMgr->FreeInput(mPortInput);
			mPortInput = -1;
		}

		if (mPortOutput >= 0) {
			mpPortMgr->FreeOutput(mPortOutput);
			mPortOutput = -1;
		}

		mpPortMgr = nullptr;
	}

	if (mpSIOMgr) {
		if (mbRegisteredRawSIO) {
			mpSIOMgr->RemoveRawDevice(this);
			mbRegisteredRawSIO = false;
			mbTurboProceedAsserted = false;
			mbTurboInterruptAsserted = false;
		}

		mpSIOMgr = nullptr;
	}

	if (mpSlowScheduler) {
		mpSlowScheduler->UnsetEvent(mpRecordEvent);
		mpSlowScheduler = NULL;
	}

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpPortUpdateEvent);
		mpScheduler->UnsetEvent(mpPlayEvent);
		mpScheduler = NULL;
	}

	if (mpAudioMixer) {
		mpAudioMixer->RemoveSyncAudioSource(this);
		mpAudioMixer = nullptr;
	}

	TapeChanging.InvokeAll();
	TapeChanging.Clear();

	if (mpImage) {
		mpImage->Release();
		mpImage = NULL;
	}

	TapeChanged.InvokeAll();
	TapeChanged.Clear();

	vdsaferelease <<= mpTraceChannelFSK, mpTraceChannelTurbo;
}

void ATCassetteEmulator::ColdReset() {
	mbOutputBit = false;
	mbMotorEnable = false;
	mbPlayEnable = false;
	mbDataLineState = false;
	mSIOPhase = 0;
	mDataByte = 0;
	mDataBitCounter = 0;

	mbAudioEventOpen = false;
	mAudioPosition = 0;
	mAudioEvents.clear();

	if (mbAutoRewind)
		RewindToStart();

	Play();
}
	
void ATCassetteEmulator::LoadNew() {
	TapeChanging.InvokeAll();

	UnloadInternal();

	ATCreateNewCassetteImage(&mpImage);
	mPosition = 0;
	mAudioPosition = 0;
	mLength = 0;
	mAudioLength = 0;

	TapeChanged.InvokeAll();

	PositionChanged.NotifyDeferred();
	PlayStateChanged.NotifyDeferred();
}

void ATCassetteEmulator::Load(const wchar_t *fn) {
	vdrefptr<ATVFSFileView> view;

	ATVFSOpenFileView(fn, false, ~view);

	UnloadInternal();

	ATCassetteLoadContext ctx;
	ctx.mTurboDecodeAlgorithm = mTurboDecodeAlgorithm;

	vdrefptr<IATCassetteImage> image;
	ATLoadCassetteImage(view->GetStream(), nullptr, ctx, ~image);

	Load(image, fn, true);
}

void ATCassetteEmulator::Load(IATCassetteImage *image, const wchar_t *path, bool persistent) {
	TapeChanging.InvokeAll();

	try {
		UnloadInternal();

		mpImage = image;
		mpImage->AddRef();
		mImagePath = path ? path : L"";
		mbImagePersistent = persistent;

		mLength = mpImage->GetDataLength();
		mAudioLength = mpImage->GetAudioLength();

		mPosition = ~UINT32_C(0);
		SeekToBitPos(0);
	} catch(...) {
		TapeChanged.InvokeAll();
		throw;
	}

	TapeChanged.InvokeAll();
}

void ATCassetteEmulator::Unload() {
	if (!mpImage)
		return;

	FlushRecording(ATSCHEDULER_GETTIME(mpScheduler), true);

	TapeChanging.InvokeAll();
	UnloadInternal();
	PositionChanged.NotifyDeferred();
	TapeChanged.InvokeAll();
	PlayStateChanged.NotifyDeferred();
}

void ATCassetteEmulator::UnloadInternal() {
	if (mpImage) {
		mpImage->Release();
		mpImage = NULL;
	}

	mImagePath.clear();
	mbImagePersistent = false;

	SetImageClean();

	mPosition = 0;
	mLength = 0;
	mAudioPosition = 0;
	mAudioLength = 0;
	mbAudioEventOpen = false;
	mAudioEvents.clear();

	UpdateMotorState();

	mNeedBasic.reset();
	ResetCursors();
}

void ATCassetteEmulator::SetImagePersistent(const wchar_t *fn) {
	if (mpImage) {
		mbImagePersistent = true;
		mImagePath = fn;
	}
}

void ATCassetteEmulator::SetImageClean() {
	if (mbImageDirty) {
		mbImageDirty = false;

		TapeDirtyStateChanged.NotifyDeferred();
	}
}

void ATCassetteEmulator::SetImageDirty() {
	if (!mbImageDirty) {
		mbImageDirty = true;
		mNeedBasic.reset();

		TapeDirtyStateChanged.NotifyDeferred();
	}
}

void ATCassetteEmulator::SetLoadDataAsAudioEnable(bool enable) {
	mbLoadDataAsAudio = enable;
}

void ATCassetteEmulator::SetRandomizedStartEnabled(bool enable) {
	mbRandomizedStartEnabled = enable;
}

void ATCassetteEmulator::SetDirectSenseMode(ATCassetteDirectSenseMode mode) {
	if (mDirectSenseMode != mode) {
		mDirectSenseMode = mode;

		UpdateDirectSenseParameters();
		ResetCursors();
	}
}

void ATCassetteEmulator::SetTurboMode(ATCassetteTurboMode mode) {
	if (mTurboMode == mode)
		return;

	mTurboMode = mode;

	switch(mode) {
		case kATCassetteTurboMode_None:
		case kATCassetteTurboMode_CommandControl:
		case kATCassetteTurboMode_ProceedSense:
		case kATCassetteTurboMode_InterruptSense:
		case kATCassetteTurboMode_KSOTurbo2000:
			break;

		default:
			mode = kATCassetteTurboMode_None;
			if (mTurboMode == mode)
				return;
			break;
	}

	mbFSKControlEnabled = mTurboMode == kATCassetteTurboMode_CommandControl;

	UpdateFSKDecoderEnabled();

	if (mTurboMode != kATCassetteTurboMode_ProceedSense) {
		if (mbTurboProceedAsserted) {
			mpSIOMgr->SetSIOProceed(this, false);
			mbTurboProceedAsserted = false;
		}
	}

	if (mTurboMode != kATCassetteTurboMode_InterruptSense) {
		if (mbTurboInterruptAsserted) {
			mpSIOMgr->SetSIOInterrupt(this, false);
			mbTurboInterruptAsserted = false;
		}
	}

	UpdateInvertData();

	if (mode == kATCassetteTurboMode_KSOTurbo2000) {
		if (mPortInput < 0) {
			mPortInput = mpPortMgr->AllocInput();
			mpPortMgr->SetInput(mPortInput, ~UINT32_C(0));
		}

		if (mPortOutput < 0) {
			mPortOutput = mpPortMgr->AllocOutput(
				[](void *data, uint32 outputState) {
					ATCassetteEmulator *thisPtr = (ATCassetteEmulator *)data;
					bool newPortMotorState = !(outputState & 0x20);

					if (thisPtr->mbPortMotorState != newPortMotorState) {
						thisPtr->mbPortMotorState = newPortMotorState;

						thisPtr->UpdateMotorState();
					}
				},
				this, 0x20);
		}
	} else {
		if (mPortInput >= 0) {
			mpPortMgr->FreeInput(mPortInput);
			mPortInput = -1;
		}

		if (mPortOutput >= 0) {
			mpPortMgr->FreeOutput(mPortOutput);
			mPortOutput = -1;
		}

		mpScheduler->UnsetEvent(mpPortUpdateEvent);

		if (mbPortMotorState) {
			mbPortMotorState = false;

			UpdateMotorState();
		}
	}
}

ATCassettePolarityMode ATCassetteEmulator::GetPolarityMode() const {
	return mbInvertTurboData ? kATCassettePolarityMode_Inverted : kATCassettePolarityMode_Normal;
}

void ATCassetteEmulator::SetPolarityMode(ATCassettePolarityMode mode) {
	mbInvertTurboData = (mode == kATCassettePolarityMode_Inverted);

	UpdateInvertData();
}

bool ATCassetteEmulator::IsVBIAvoidanceEnabled() const {
	return mbVBIAvoidanceEnabled;
}

void ATCassetteEmulator::SetVBIAvoidanceEnabled(bool enable) {
	mbVBIAvoidanceEnabled = enable;
}

void ATCassetteEmulator::SetNextVerticalBlankTime(uint64 t) {
	mNextVBITime = t;
}

void ATCassetteEmulator::SetTraceContext(ATTraceContext *context) {
	mpTraceContext = context;

	if (context) {
		ATTraceCollection *coll = context->mpCollection;

		// Because we may be playing slightly off ideal rate for PAL/SECAM, put the actual data rate into
		// the trace.
		const double samplesPerSecond = mpScheduler->GetRate().asDouble() * (1.0 / (double)kATCassetteCyclesPerDataSample);

		vdrefptr<ATTraceChannelTape> channelFSK { new ATTraceChannelTape(context->mBaseTime, context->mBaseTickScale, L"FSK", false, samplesPerSecond) };
		vdrefptr<ATTraceChannelTape> channelTurbo { new ATTraceChannelTape(context->mBaseTime, context->mBaseTickScale, L"Turbo", true, samplesPerSecond) };

		ATTraceGroup *group = coll->AddGroup(L"Tape", kATTraceGroupType_Tape);

		group->AddChannel(channelFSK);
		mpTraceChannelFSK = channelFSK.release();

		group->AddChannel(channelTurbo);
		mpTraceChannelTurbo = channelTurbo.release();

		mbTraceMotorRunning = false;
		mbTraceRecord = false;

		UpdateTraceState();
	} else {
		if (mpScheduler) {
			const uint64 t = mpScheduler->GetTick64();

			if (mpTraceChannelFSK)
				mpTraceChannelFSK->TruncateLastEvent(t);

			if (mpTraceChannelTurbo)
				mpTraceChannelTurbo->TruncateLastEvent(t);
		}


		vdsaferelease <<= mpTraceChannelFSK, mpTraceChannelTurbo;
	}
}

void ATCassetteEmulator::Stop() {
	if (!mbPlayEnable && !mbRecordEnable)
		return;

	mbPlayEnable = false;
	mbRecordEnable = false;
	UpdateMotorState();
	UpdateRawSIODevice();

	PlayStateChanged.NotifyDeferred();
}

void ATCassetteEmulator::Play() {
	if (mbPlayEnable)
		return;

	mbPlayEnable = true;
	mbRecordEnable = false;
	UpdateMotorState();
	UpdateRawSIODevice();

	PlayStateChanged.NotifyDeferred();
}

void ATCassetteEmulator::Record() {
	if (mbRecordEnable)
		return;

	mbPlayEnable = false;
	mbRecordEnable = true;
	UpdateMotorState();
	UpdateRawSIODevice();

	mWriteCursor.mPosition = mPosition;

	PlayStateChanged.NotifyDeferred();
}

void ATCassetteEmulator::SetPaused(bool paused) {
	if (mbPaused == paused)
		return;

	mbPaused = paused;

	UpdateMotorState();
	UpdateRawSIODevice();

	if (mbRecordEnable) {
		mWriteCursor.mPosition = mPosition;
	}


	PlayStateChanged.NotifyDeferred();
}

void ATCassetteEmulator::RewindToStart() {
	uint32 pos = 0;

	if (mbRandomizedStartEnabled) {
		pos = ATRandomizeAdvanceFast(g_ATRandomizationSeeds.mCassetteStartPos);

		// randomize to 1/10th sec. for equal distribution within frame (6 vblanks for
		// NTSC, 5 for PAL)
		pos = pos % (uint32)(kATCassetteDataSampleRate / 10.0f);
	}

	mJitterSeed = pos;
	if (!mJitterSeed)
		mJitterSeed = 1;

	SeekToBitPos(pos);
}

void ATCassetteEmulator::SeekToTime(float seconds) {
	if (seconds < 0.0f)
		seconds = 0.0f;

	uint32 pos = VDRoundToInt(seconds * kATCassetteDataSampleRate);

	SeekToBitPos(pos);
}

void ATCassetteEmulator::SeekToBitPos(uint32 bitPos) {
	if (mPosition == bitPos)
		return;

	// flush pending recording
	if (mpRecordEvent) {
		FlushRecording(ATSCHEDULER_GETTIME(mpScheduler), true);

		TapePeaksUpdated.NotifyDeferred();
	}

	mPosition = bitPos;

	// compute new audio position from data position
	uint32 newAudioPos = mPosition;

	// clamp positions and kill or recreate events as appropriate
	if (mPosition >= mLength) {
		mPosition = mLength;

		if (mpPlayEvent) {
			mpScheduler->RemoveEvent(mpPlayEvent);
			mpPlayEvent = NULL;
		}
	}

	SeekAudio(newAudioPos);
	UpdateMotorState();

	ResetCursors();

	if (mbRecordEnable && mpImage)
		mWriteCursor.mPosition = mPosition;

	if (mPortInput >= 0) {
		mpScheduler->UnsetEvent(mpPortUpdateEvent);

		mpPortMgr->SetInput(mPortInput, ~UINT32_C(0));
		mbPortCurrentPolarity = true;
		ScheduleNextPortTransition();
	}

	PositionChanged.NotifyDeferred();
}

void ATCassetteEmulator::SkipForward(float seconds) {
	// compute tape offset
	sint32 bitsToSkip = VDRoundToInt(seconds * kATCassetteDataSampleRate);

	SeekToBitPos(mPosition + bitsToSkip);
}

uint32 ATCassetteEmulator::OnPreModifyTape() {
	if (mpRecordEvent) {
		FlushRecording(ATSCHEDULER_GETTIME(mpScheduler), true);
	}

	return mPosition;
}

void ATCassetteEmulator::OnPostModifyTape(uint32 newPos) {
	if (!mpImage)
		return;

	mPosition = ~newPos;
	mLength = mpImage->GetDataLength();
	mAudioLength = mpImage->GetAudioLength();

	SetImageDirty();

	SeekToBitPos(newPos);
	TapePeaksUpdated.NotifyDeferred();
}

uint8 ATCassetteEmulator::ReadBlock(uint16 bufadr0, uint16 len, ATCPUEmulatorMemory *mpMem) {
	if (!mbPlayEnable)
		return 0x8A;	// timeout

	// We need to turn this on/off through the PIA instead of doing it directly,
	// or else the two get out of sync. This breaks the SIECOD loader.
	mpMem->WriteByte(ATKernelSymbols::PACTL, (mpMem->ReadByte(ATKernelSymbols::PACTL) & 0xC7) + 0x38);

	uint32 offset = 0;
	uint32 sum = 0;
	uint8 actualChecksum = 0;
	uint8 status = 0x01;	// complete

	constexpr uint32 kMaxDataBitsPerSyncBit = (uint32)(0.5f + kATCassetteDataSampleRate / 600.0f) * 2;
	constexpr uint32 kSyncGapTimeout = kMaxDataBitsPerSyncBit * 3;

	uint32 syncGapTimeLeft = kSyncGapTimeout;
	uint32 syncMarkTimeout = kMaxDataBitsPerSyncBit;
	int syncBitsLeft = 20;
	uint32 syncStart = 0;
	float idealBaudRate = 0.0f;
	int framingErrors = 0;
	uint32 firstFramingError = 0;
	int lastReceivedByte = -1;

	uint32 maxPos = std::min<uint32>(mPosition + (uint32)(kATCassetteDataSampleRate * 60), mLength);
	uint16 bufadr = bufadr0;

	while(offset <= len) {
		if (mPosition >= maxPos) {
			g_ATLCCas("Timeout receiving block while waiting for sync mark (pos = %.3fs)\n", (float)mPosition / (float)kATCassetteDataSampleRate);
			PositionChanged.NotifyDeferred();
			ResyncAudio();
			return 0x8A;	// timeout
		}

		UpdateDirectSense(0);
		++mPosition;

		if (syncGapTimeLeft > 0) {
			if (!mbDataLineState)
				syncGapTimeLeft = kSyncGapTimeout;
			else
				--syncGapTimeLeft;

			continue;
		}

		bool expected = (syncBitsLeft & 1) != 0;

		if (expected != mbDataLineState) {
			if (--syncMarkTimeout <= 0) {
				syncMarkTimeout = kMaxDataBitsPerSyncBit;

				if (syncBitsLeft < 15) {
					//VDDEBUG("CAS: Sync timeout; restarting.\n");
					syncGapTimeLeft = kSyncGapTimeout;
				}

				syncBitsLeft = 20;
			}
		} else {
			if (syncBitsLeft == 20) {
				syncStart = mPosition;
			}

			--syncBitsLeft;
			syncMarkTimeout = kMaxDataBitsPerSyncBit;

			if (syncBitsLeft == 0)
				break;
		}
	}

	// compute baud rate divisor
	//
	// bitDelta / 19 = ticks_per_bit
	// divisor = cycles_per_bit = 440 * ticks_per_bit
	//
	// baud = bits_per_second = cycles_per_second / cycles_per_bit
	//		= 7159090 / 4 / (440 * ticks_per_bit)
	//		= 7159090 / (1760 * ticks_per_bit)
	//
	// Note that we have to halve the divisor since you're supposed to set the
	// timer such that the frequency is the intended baud rate, so that it
	// rolls over TWICE for each bit.

	uint32 bitDelta = mPosition - syncStart;
	PokeyChangeSerialRate(VDRoundToInt32(kATCassetteCyclesPerDataSample / 19.0f * 0.5f * (float)bitDelta));

	mSIOPhase = 0;
	mbOutputBit = true;
	idealBaudRate = kATCassetteDataSampleRate * 19.0f / (float)bitDelta;
	//VDDEBUG("CAS: Sync mark found. Computed baud rate = %.2f baud\n", idealBaudRate);

	mpMem->WriteByte(bufadr++, 0x55);
	mpMem->WriteByte(bufadr++, 0x55);
	sum = 0x55*2;
	offset = 2;
	framingErrors = 0;
	firstFramingError = 0;
	lastReceivedByte = 0x55;

	int firstFramingErrorOffset = 0;
	uint8 controlByte = 0;

	while(offset <= len) {
		if (mPosition >= maxPos) {
			g_ATLCCas("Timeout receiving block after receiving %u/%u bytes (pos = %.3fs)\n", offset, len, (float)mPosition / (float)kATCassetteDataSampleRate);
			PositionChanged.NotifyDeferred();
			UpdateDirectSense(-1);
			ResyncAudio();
			return 0x8A;	// timeout
		}

		BitResult r = ProcessBit();

		if (r == kBR_NoOutput)
			continue;

		if (r == kBR_FramingError) {
			++framingErrors;
			firstFramingError = mPosition;
			firstFramingErrorOffset = offset;
			continue;
		}

		VDASSERT(r == kBR_ByteReceived);
		
		lastReceivedByte = mDataByte;

		if (offset < len) {
			if (g_ATLCCasData.IsEnabled())
				g_ATLCCasData("Receiving byte[%3d]: %02X (accelerated) (pos=%.3fs)\n", offset - 2, mDataByte, (float)mPosition / (float)kATCassetteDataSampleRate);

			if (offset == 2)
				controlByte = mDataByte;

			mpMem->WriteByte(bufadr++, mDataByte);
			sum += mDataByte;
			++offset;
		} else {
			sum = (sum & 0xff) + ((sum >> 8) & 0xff) + ((sum >> 16) & 0xff);
			sum = (sum & 0xff) + ((sum >> 8) & 0xff);
			actualChecksum = (uint8)((sum & 0xff) + ((sum >> 8) & 0xff));
			uint8 readChecksum = mDataByte;

			mpMem->WriteByte(ATKernelSymbols::CHKSUM, readChecksum);

			if (actualChecksum != readChecksum) {
				status = 0x8F;		// checksum error

				g_ATLCCas("Checksum error encountered (got $%02X, expected $%02X).\n", readChecksum, actualChecksum);
				g_ATLCCas("Sector sync pos: %.3f s | End pos: %.3f s | Control: $%02X | Baud rate: %.2f baud | Framing errors: %d (first at %.02f, offset %d)\n"
					, (float)syncStart / (float)kATCassetteDataSampleRate
					, (float)mPosition / (float)kATCassetteDataSampleRate
					, controlByte
					, idealBaudRate
					, framingErrors
					, (float)firstFramingError / (float)kATCassetteDataSampleRate
					, firstFramingErrorOffset
					);
			}

			++offset;
		}
	}

	ResetCursors();
	UpdateDirectSense(-1);

	// set SERIN to the last received byte (needed by Misja + Fred)
	if (lastReceivedByte >= 0)
		mpPokey->SetSERIN((uint8)lastReceivedByte);

	// resync audio position
	ResyncAudio();

	g_ATLCCas("Read complete | Status %02X | Buffer $%04X len $%04X | Control=%02X, position=%.2fs (cycle %u), baud=%.2fs, checksum=%02X\n"
		, status
		, bufadr0
		, len
		, controlByte
		, mPosition / kATCassetteDataSampleRate, mPosition, idealBaudRate, actualChecksum);

	// check if long inter-record gaps (IRGs) are enabled
	uint8 daux2 = mpMem->ReadByte(0x030B);
	if (!(daux2 & 0x80)) {
		// We need to turn this on/off through the PIA instead of doing it directly,
		// or else the two get out of sync. This breaks the SIECOD loader.
		mpMem->WriteByte(ATKernelSymbols::PACTL, (mpMem->ReadByte(ATKernelSymbols::PACTL) & 0xC7) + 0x30);
	}

	return status;
}

uint8 ATCassetteEmulator::WriteBlock(uint16 bufadr, uint16 len, ATCPUEmulatorMemory *mpMem) {
	// check if we're in record and actually have the motor running
	if (!mpRecordEvent || !mpImage) {
		// The computer can't actually tell whether the motor is running... so what
		// happens is that the computer thinks it's written out a full sector and
		// the data goes nowhere. Boo.
		return kATCIOStat_Success;
	}

	// mark tape dirty, if not already
	SetImageDirty();

	// flush any blank time accumulated up to this point
	FlushRecording(ATSCHEDULER_GETTIME(mpScheduler), true);

	// check if we're doing long IRGs
	uint8 daux2 = mpMem->ReadByte(ATKernelSymbols::DAUX2);
	const bool longIRG = !(daux2 & 0x80);

	// write pre-record write tone (PRWT): 0.25s / 3.0s
	mpImage->WriteBlankData(mWriteCursor, longIRG ? (uint32)(kATCassetteDataSampleRate * 3) : (uint32)(kATCassetteDataSampleRate / 4), false);

	// write data
	uint32 checksum = 0;

	for(uint32 i = 0; i < len; ++i) {
		const uint8 c = mpMem->ReadByte((uint16)(bufadr + i));

		checksum += c;
		checksum += (checksum >> 8);
		checksum &= 0xff;

		mpImage->WriteStdData(mWriteCursor, c, 600, false);
	}

	// write checksum
	mpImage->WriteStdData(mWriteCursor, (uint8)checksum, 600, false);

	// write post-record gap (1.0s)
	if (longIRG)
		mpImage->WriteBlankData(mWriteCursor, (uint32)kATCassetteDataSampleRate, false);

	mRecordLastTime = ATSCHEDULER_GETTIME(mpScheduler);

	return kATCIOStat_Success;
}

std::optional<bool> ATCassetteEmulator::AutodetectBasicNeeded() {
	// see if we have the result already cached or don't have an image
	if (mNeedBasic.has_value())
		return mNeedBasic.value();

	if (!mpImage)
		return std::optional<bool>();

	// Try to decode the first data block and see if we have a binary or BASIC loader.

	// start 2 seconds in in case there's junk in the leader
	uint32 pos = (uint32)VDRoundToInt32(kATCassetteDataSampleRate * 2);

	// do a quick scan for the first start bit, and back off by a bit at 600 baud
	constexpr uint32 kTapeTicks600 = (kATCassetteDataSampleRate + 300) / 600;
	constexpr uint32 kLeaderTimeout = kATCassetteDataSampleRate * 30;
	pos = std::max<uint32>(mpImage->FindNextBit(pos, kLeaderTimeout, false, false).mPos, kTapeTicks600) - kTapeTicks600;

	// scan forward with decoding at 600 baud and see if we can pull out 7 bytes within
	// about 150 bit periods (70 nominal required)
	constexpr uint32 kReadTimeout = kTapeTicks600 * 150;
	constexpr uint32 kThresholdZero = kTapeTicks600 / 3;
	bool currentBit = true;
	uint32 posLimit = std::min<uint32>(pos, ~kReadTimeout) + kReadTimeout;
	uint32 state = 0;
	uint8 shiftRegister = 0;
	bool framingError = false;
	size_t bufIdx = 0;
	uint8 buf[7];

	for(; pos < posLimit; ++pos) {
		currentBit = mpImage->GetBit(pos, kTapeTicks600, kThresholdZero, currentBit, false);

		switch(state) {
			case 0:
				if (!currentBit)
					state = 1;
				break;

			case kTapeTicks600 >> 1:
				if (currentBit)
					state = 0;
				++state;
				break;

			case kTapeTicks600 * 1 + (kTapeTicks600 >> 1):
			case kTapeTicks600 * 2 + (kTapeTicks600 >> 1):
			case kTapeTicks600 * 3 + (kTapeTicks600 >> 1):
			case kTapeTicks600 * 4 + (kTapeTicks600 >> 1):
			case kTapeTicks600 * 5 + (kTapeTicks600 >> 1):
			case kTapeTicks600 * 6 + (kTapeTicks600 >> 1):
			case kTapeTicks600 * 7 + (kTapeTicks600 >> 1):
			case kTapeTicks600 * 8 + (kTapeTicks600 >> 1):
				shiftRegister = (shiftRegister >> 1) + (currentBit ? 0x80 : 0x00);
				++state;
				break;

			case kTapeTicks600 * 9 + (kTapeTicks600 >> 1):
				if (!currentBit)
					framingError = true;

				if (bufIdx < vdcountof(buf)) {
					buf[bufIdx++] = shiftRegister;
					if (bufIdx >= vdcountof(buf))
						pos = posLimit - 1;
				}
				state = 0;
				break;

			default:
				++state;
				break;
		}
	}

	// cache unknown result
	mNeedBasic = std::optional<bool>();

	// check if we cleanly decoded all bytes
	if (!framingError && bufIdx == vdcountof(buf)) {
		// For BASIC, the first bytes should be: 55 55 FA/FC 00 00 xx 01.
		// For binary, the first bytes should be: 55 55 FA/FC xx yy zz ww where ww >= $04,
		// since it is the high byte of the boot address.

		// check sync bytes and cassette block mode (55 55 FA/FC)
		if (buf[0] == 0x55 && buf[1] == 0x55 && (buf[2] == 0xFA || buf[2] == 0xFC)) {
			// BASIC: 00 00 x0 01.
			// Third byte should normally be 00 but may not be due to
			// BASIC rev. B bug, so we only check if it is a multiple of 16.
			//
			// Binary program: xx yy zz ww, where ww >= $04.
			// This checks that the boot address is valid.

			if (buf[3] == 0 && buf[4] == 0 && (buf[5] & 0xF0) == 0 && buf[6] == 0x01) {
				mNeedBasic = std::optional<bool>(true);
			} else if (buf[6] >= 0x04) {
				mNeedBasic = std::optional<bool>(false);
			}
		}
	}

	return mNeedBasic.value();
}

void ATCassetteEmulator::OnScheduledEvent(uint32 id) {
	if (id == kATCassetteEventId_ProcessBit) {
		mpPlayEvent = nullptr;

		UpdateDirectSense(0);
		const auto result = ProcessBit();
		
		if (mPosition < mLength) {
			static_assert(kATCassetteCyclesPerDataSample == 56, "Re-check jitter parameters");

			// Update xorshift32 RNG
			uint32 newDelay = kATCassetteCyclesPerDataSample;

			if (mTurboMode == kATCassetteTurboMode_None) {
				mJitterPRNG ^= mJitterPRNG >> 1;
				mJitterPRNG ^= mJitterPRNG << 3;
				mJitterPRNG ^= mJitterPRNG >> 10;

				const uint32 newPeriod = kATCassetteCyclesPerDataSample - 16 + (mJitterPRNG & 31);
				newDelay += newPeriod - mLastSampleOffset;
				mLastSampleOffset = newPeriod;
			}

			mpScheduler->SetEvent(newDelay, this, kATCassetteEventId_ProcessBit, mpPlayEvent);
		}

		if (result == kBR_ByteReceived) {
			mpPokey->ReceiveSIOByte(mDataByte, 0, false, false, false, false);

			if (g_ATLCCasData.IsEnabled()) {
				g_ATLCCasData("Receiving byte[%3d]: %02X (pos=%.3fs)\n", mLogIndex, mDataByte, (float)mPosition / (float)kATCassetteDataSampleRate);
				mLogIndex = (mLogIndex + 1) % 1000;
			}
		}

	} else if (id == kATCassetteEventId_Record) {
		mpRecordEvent = mpScheduler->AddEvent(kRecordPollPeriod, this, kATCassetteEventId_Record);

		const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

		FlushRecording(t, false);
	} else if (id == kATCassetteEventId_PortUpdate) {
		mpPortUpdateEvent = nullptr;

		ScheduleNextPortTransition();
	}
}

void ATCassetteEmulator::PokeyChangeSerialRate(uint32 divisor) {
	uint32 averagingPeriod = (divisor + (kATCassetteCyclesPerDataSample >> 1)) / kATCassetteCyclesPerDataSample;
	if (averagingPeriod < 1)
		averagingPeriod = 1;

	if (mAveragingPeriod != averagingPeriod) {
		mAveragingPeriod = averagingPeriod;

		ResetCursors();

		mThresholdZeroBit = VDFloorToInt(mAveragingPeriod * 0.45f);
		if (mThresholdZeroBit < 1)
			mThresholdZeroBit = 1;

		mThresholdOneBit = mAveragingPeriod - mThresholdZeroBit;

		mBitCursor.mWindow = averagingPeriod;
		mBitCursor.mOffset = averagingPeriod >> 1;
		mBitCursor.mThresholdLo = mThresholdZeroBit;
		mBitCursor.mThresholdHi = mThresholdOneBit;
	}

	if (mDataBitHalfPeriod != divisor) {
		mDataBitHalfPeriod = divisor;

		if (mbMotorEnable || mbPortMotorState) {
			if (g_ATLCCasData.IsEnabled())
				g_ATLCCasData("[%.1f] Setting divisor to %d / %.2f baud (avper = %d, thresholds = %d,%d)\n"
					, (float)mPosition / 6.77944f
					, divisor
					, 1789772.5f / 2.0f / (float)divisor
					, mAveragingPeriod
					, mThresholdZeroBit
					, mThresholdOneBit);

			mLogIndex = 0;
		}
	}
}

void ATCassetteEmulator::PokeyResetSerialInput() {
	mSIOPhase = 0;

	if (g_ATLCCasDecoder.IsEnabled())
		g_ATLCCasDecoder("[%.1f / %d] Serial input hardware reset\n", (float)mPosition * kATCassetteMSPerDataSample, mPosition);
}

void ATCassetteEmulator::PokeyBeginCassetteData(uint8 skctl) {
	if (!mpRecordEvent)
		return;

	// check if this is possibly a valid cassette byte
	if (!(skctl & 8)) {
		// nope, two-tone mode is off
		return;
	}

	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	const uint32 delay = t - mRecordLastTime;

	if (delay >= kRecordMinDelayForGap)
		FlushRecording(t, true);
}

bool ATCassetteEmulator::PokeyWriteCassetteData(uint8 c, uint32 cyclesPerBit) {
	if (mbRecordEnable && mbMotorRunning) {
		if (mpImage) {
			SetImageDirty();

			mpImage->WriteStdData(mWriteCursor, c, VDRoundToInt32(7159090.0f / 4.0f / cyclesPerBit), false);

			UpdateRecordingPosition();
		}

		mRecordLastTime = ATSCHEDULER_GETTIME(mpScheduler);
		return true;
	}

	return false;
}

void ATCassetteEmulator::WriteAudio(const ATSyncAudioMixInfo& mixInfo) {
	const uint32 startTime = mixInfo.mStartTime;
	float *dst = mixInfo.mpLeft;
	uint32 n = mixInfo.mCount;

	VDASSERT(n > 0);

	// fix end for currently open event if there is one
	if (mbAudioEventOpen)
		mAudioEvents.back().mStopTime = ATSCHEDULER_GETTIME(mpScheduler);

	const bool haveAudio = mpImage && (mpImage->IsAudioPresent() || mbLoadDataAsAudio);

	uint32 t = startTime;
	uint32 t2 = t + n * kATCyclesPerSyncSample;
	AudioEvents::const_iterator it(mAudioEvents.begin()), itEnd(mAudioEvents.end());

	const float sampleScale = mixInfo.mpMixLevels[kATAudioMix_Cassette];

	for(; it != itEnd; ++it) {
		const AudioEvent& ev = *it;

		// discard event if it is too early
		if ((sint32)(ev.mStopTime - t) <= 0)
			continue;

		// check if we are before the start time and skip samples if so
		if ((sint32)(ev.mStartTime - t) > 0) {
			const uint32 toSkip = (ev.mStartTime - t + kATCyclesPerSyncSample - 1) / kATCyclesPerSyncSample;

			if (toSkip >= n)
				break;

			n -= toSkip;
			t += kATCyclesPerSyncSample * toSkip;
			dst += toSkip;
		}

		// stop time is earlier of range stop and end time
		uint32 stopTime = ev.mStopTime;

		if ((sint32)(stopTime - t2) > 0)
			stopTime = t2;

		// check if we have any time left in the event at all and skip if we don't
		if ((sint32)(t - stopTime) >= 0)
			continue;

		// compute prestepped position
		uint32 pos = ev.mPosition + (t - ev.mStartTime) / kATCassetteCyclesPerAudioSample;
		uint32 posfrac = (t - ev.mStartTime) % kATCassetteCyclesPerAudioSample;

		// skip if we have no samples left on tape
		if (pos >= mAudioLength)
			continue;

		// compute how many samples we're going to render
		uint32 toRender = (stopTime - t + kATCyclesPerSyncSample - 1) / kATCyclesPerSyncSample;

		if (toRender > n)
			toRender = n;

		// render samples
		if (haveAudio)
			mpImage->AccumulateAudio(dst, pos, posfrac, toRender, sampleScale);

		n -= toRender;
		t += kATCyclesPerSyncSample * toRender;
	}

	// delete all events, except for the last one if it's open
	if (mbAudioEventOpen)
		mAudioEvents.erase(mAudioEvents.begin(), mAudioEvents.end() - 1);
	else
		mAudioEvents.clear();
}

void ATCassetteEmulator::OnCommandStateChanged(bool asserted) {
	mbFSKDecoderRequested = !asserted;

	if (mbFSKControlEnabled) {
		UpdateFSKDecoderEnabled();
		UpdateInvertData();
	}
}

void ATCassetteEmulator::OnMotorStateChanged(bool asserted) {
	mbMotorEnable = asserted;
	UpdateMotorState();
}

void ATCassetteEmulator::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
}

void ATCassetteEmulator::OnSendReady() {
}

void ATCassetteEmulator::UpdateRawSIODevice() {
	bool shouldRegister = !mbPaused && (mbPlayEnable || mbRecordEnable);

	if (mbRegisteredRawSIO != shouldRegister) {
		mbRegisteredRawSIO = shouldRegister;

		if (shouldRegister) {
			mpSIOMgr->AddRawDevice(this);

			OnCommandStateChanged(mpSIOMgr->IsSIOCommandAsserted());
			OnMotorStateChanged(mpSIOMgr->IsSIOMotorAsserted());
		} else {
			if (mbTurboProceedAsserted) {
				mbTurboProceedAsserted = false;

				mpSIOMgr->SetSIOProceed(this, false);
			}

			mpSIOMgr->RemoveRawDevice(this);
		}
	}
}

void ATCassetteEmulator::UpdateMotorState() {
	const bool motorCanMove = !mbPaused && (mbMotorEnable || mbPortMotorState) && mpImage;

	bool prevRunning = mbMotorRunning;
	mbMotorRunning = (mbPlayEnable || mbRecordEnable) && motorCanMove;

	if (mbMotorRunning && mbPlayEnable) {
		if (mPosition < mLength) {
			if (!mpPlayEvent) {
				mJitterPRNG = mPosition + mJitterSeed;

				if (!mJitterPRNG)
					mJitterPRNG = 1;

				mLastSampleOffset = kATCassetteCyclesPerDataSample;
				mpPlayEvent = mpScheduler->AddEvent(1, this, kATCassetteEventId_ProcessBit);
			}

			if (mTurboMode == kATCassetteTurboMode_KSOTurbo2000 && !mpPortUpdateEvent) {
				mPortCurrentPosition = mPosition;
				mbPortCurrentPolarity = true;
				ScheduleNextPortTransition();
			}
		}

		StartAudio();
	} else {
		if (prevRunning) {
			mLastStopCycle = mpScheduler->GetTick64();
			mLastStopPosition = mPosition;
		}

		mpScheduler->UnsetEvent(mpPlayEvent);
		mpScheduler->UnsetEvent(mpPortUpdateEvent);

		StopAudio();

		if (mPortInput >= 0)
			mpPortMgr->SetInput(mPortInput, ~UINT32_C(0));
	}

	if (mbMotorRunning && mbRecordEnable) {
		if (!mpRecordEvent) {
			mpRecordEvent = mpScheduler->AddEvent(kRecordPollPeriod, this, kATCassetteEventId_Record);

			mRecordLastTime = ATSCHEDULER_GETTIME(mpScheduler);
		}
	} else {
		if (mpRecordEvent) {
			FlushRecording(ATSCHEDULER_GETTIME(mpScheduler), true);

			mpSlowScheduler->RemoveEvent(mpRecordEvent);
			mpRecordEvent = nullptr;
		}
	}

	UpdateTraceState();
}

void ATCassetteEmulator::UpdateInvertData() {
	mbInvertData = !mbFSKDecoderEnabled && mbInvertTurboData;
}

void ATCassetteEmulator::UpdateFSKDecoderEnabled() {
	bool fskDecoderEnabled = (!mbFSKControlEnabled || mbFSKDecoderRequested) && mTurboMode != kATCassetteTurboMode_Always;

	if (mbFSKDecoderEnabled != fskDecoderEnabled) {
		mbFSKDecoderEnabled = fskDecoderEnabled;

		mBitCursor.mbFSKBypass = !fskDecoderEnabled;
		ResetCursors();
	}
}

void ATCassetteEmulator::ResetCursors() {
	mDirectCursor.mWindow = mDirectSenseWindow;
	mDirectCursor.mOffset = mDirectSenseWindow/2;
	mDirectCursor.mThresholdLo = mDirectSenseThreshold;
	mDirectCursor.mThresholdHi = mDirectSenseWindow - mDirectSenseThreshold;
	mDirectCursor.mbFSKBypass = false;

	mDirectCursor.Reset();
	mBitCursor.Reset();
	mTurboCursor.Reset();
}

void ATCassetteEmulator::UpdateDirectSense(sint32 posOffset) {
	const uint32 pos = mPosition + (uint32)posOffset;

	// The sync mark has to be read before the baud rate is set, so we force the averaging
	// period to ~2000 baud.
	bool newDataLineState = true;
	bool turboState = true;
	
	if (mpImage) {
		if (mbFSKDecoderEnabled) {
			mDirectCursor.Update(*mpImage, pos);

			newDataLineState = mDirectCursor.mCurrentValue;
		}

		if (!mbFSKDecoderEnabled || (mTurboMode == kATCassetteTurboMode_ProceedSense || mTurboMode == kATCassetteTurboMode_InterruptSense)) {
			if (pos >= mTurboCursor.mNextTransition) {
				if (!mTurboCursor.mNextTransition)
					mTurboCursor.mNextValue = mpImage->GetBit(pos, true);

				mTurboCursor.mCurrentValue = mTurboCursor.mNextValue;

				const auto nextTransition = mpImage->FindNextBit(pos + 1, pos + 10000, !mTurboCursor.mCurrentValue, true);
				mTurboCursor.mNextTransition = nextTransition.mPos;
				mTurboCursor.mNextValue = nextTransition.mBit;
			}

			if (!mbFSKDecoderEnabled)
				newDataLineState = mTurboCursor.mCurrentValue;

			turboState = mTurboCursor.mCurrentValue != mbInvertTurboData;
		}
	}

	if (mbDataLineState != newDataLineState) {
		mbDataLineState = newDataLineState;

		if (g_ATLCCasDirectData.IsEnabled())
			g_ATLCCasDirectData("[%.1f] Direct data line is now %d\n", (float)mPosition * kATCassetteMSPerDataSample, mbDataLineState);

		if (mbVBIAvoidanceEnabled) {
			static constexpr uint32 kHazardTimeLen = 114;
			uint64 hazardTimeStart = mNextVBITime - 114;

			uint64 t = mpScheduler->GetTick64();
			if ((uint64)(t - hazardTimeStart) < kHazardTimeLen)
				mpPokey->SetDataLine(!mbDataLineState, mNextVBITime);
			else
				mpPokey->SetDataLine(mbDataLineState);
		} else
			mpPokey->SetDataLine(mbDataLineState);
	}

	if (mbRegisteredRawSIO) {
		switch(mTurboMode) {
			case kATCassetteTurboMode_ProceedSense:
				{
					const bool assertProceed = turboState;

					if (mbTurboProceedAsserted != assertProceed) {
						mbTurboProceedAsserted = assertProceed;

						mpSIOMgr->SetSIOProceed(this, assertProceed);
					}
				}
				break;

			case kATCassetteTurboMode_InterruptSense:
				{
					const bool assertInterrupt = turboState;

					if (mbTurboInterruptAsserted != assertInterrupt) {
						mbTurboInterruptAsserted = assertInterrupt;

						mpSIOMgr->SetSIOInterrupt(this, assertInterrupt);
					}
				}
				break;

			default:
				break;
		}
	}
}

ATCassetteEmulator::BitResult ATCassetteEmulator::ProcessBit() {
	mBitCursor.Update(*mpImage, mPosition);
	const bool dataBit = mBitCursor.mCurrentValue != mbInvertData;

	++mPosition;

	PositionChanged.NotifyDeferred();

	if (dataBit != mbOutputBit) {
		mbOutputBit = dataBit;

		// Alright, we've seen a transition, so this must be the start of a data bit.
		// Set ourselves up to sample.
		if (mSIOPhase == 0 && !dataBit) {
			mSIOPhase = 1;

			mbDataBitEdge = true;
			mDataBitCounter = mDataBitHalfPeriod - (kATCassetteCyclesPerDataSample >> 1);
			mStartBitPosition = mPosition - 1;
			return kBR_NoOutput;
		} else if (mSIOPhase > 0 && g_ATLCCasData2.IsEnabled()) {
			g_ATLCCasData2("Phase error %+d/%d\n"
				, mbDataBitEdge ? mDataBitHalfPeriod - mDataBitCounter : mDataBitCounter
				, mDataBitHalfPeriod*2
			);
		}
	}

	if (mSIOPhase == 0)
		return kBR_NoOutput;

	mDataBitCounter += kATCassetteCyclesPerDataSample;
	if (mDataBitCounter < mDataBitHalfPeriod)
		return kBR_NoOutput;

	mDataBitCounter -= mDataBitHalfPeriod;

	if (mbDataBitEdge) {
		if (g_ATLCCasDecoder.IsEnabled())
			g_ATLCCasDecoder("[%.1f / %d] Bit cell %d start  - %d\n", (float)mPosition * kATCassetteMSPerDataSample, mPosition, mSIOPhase, dataBit);

		// We were expecting the leading edge of a bit and didn't see a transition.
		// Assume there is an invisible bit boundary and set ourselves up to sample
		// the data bit one half bit period from now.
		mbDataBitEdge = false;
		return kBR_NoOutput;
	}

	// Set ourselves up to look for another edge transition.
	mbDataBitEdge = true;

	// Time to sample the data bit.
	//
	// We are looking for:
	//
	//     ______________________________________________
	//     |    |    |    |    |    |    |    |    |
	//     | 0  | 1  | 2  | 3  | 4  | 5  | 6  | 7  |
	// ____|____|____|____|____|____|____|____|____|
	// start                                         stop

	if (g_ATLCCasDecoder.IsEnabled())
		g_ATLCCasDecoder("[%.1f / %d] Bit cell %d sample - %d\n", (float)mPosition * kATCassetteMSPerDataSample, mPosition, mSIOPhase, dataBit);

	if (mSIOPhase == 1) {
		// Check for start bit.
		if (!mbOutputBit) {
			mSIOPhase = 2;
		} else
			mSIOPhase = 0;
	} else {
		++mSIOPhase;
		if (mSIOPhase > 10) {
			mSIOPhase = 0;

			if (!ByteDecoded.IsEmpty()) {
				ByteDecoded.InvokeAll(mStartBitPosition, mPosition - 1, mDataByte, !mbOutputBit, mDataBitHalfPeriod);
			}

			// Check for stop bit.
			if (mbOutputBit) {
				// We got a mark -- send the byte on.
				return kBR_ByteReceived;
			} else {
				// Framing error -- drop the byte.
				return kBR_FramingError;
			}
		} else {
			mDataByte = (mDataByte >> 1) + (mbOutputBit ? 0x80 : 0x00);
		}
	}

	return kBR_NoOutput;
}

void ATCassetteEmulator::StartAudio() {
	if (mbAudioEventOpen)
		return;

	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	AudioEvent& newEvent = mAudioEvents.push_back();
	newEvent.mStartTime = t;
	newEvent.mStopTime = t;
	newEvent.mPosition = mAudioPosition;
	mbAudioEventOpen = true;
}

void ATCassetteEmulator::StopAudio() {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	if (mbAudioEventOpen) {
		mbAudioEventOpen = false;

		AudioEvent& prevEvent = mAudioEvents.back();
		if (t == prevEvent.mStartTime)
			mAudioEvents.pop_back();
		else {
			prevEvent.mStopTime = t;

			mAudioPosition = prevEvent.mPosition + (t - prevEvent.mStartTime) / kATCassetteCyclesPerAudioSample;

			if (mAudioPosition > mAudioLength)
				mAudioPosition = mAudioLength;
		}
	}
}

void ATCassetteEmulator::SeekAudio(uint32 pos) {
	if (pos >= mAudioLength) {
		StopAudio();
		mAudioPosition = mAudioLength;
		return;
	}

	if (mbAudioEventOpen)
		StopAudio();

	mAudioPosition = pos;

	if (mbAudioEventOpen)
		StartAudio();
}

void ATCassetteEmulator::ResyncAudio() {
	SeekAudio(mPosition);
}

void ATCassetteEmulator::FlushRecording(uint32 t, bool force) {
	if (force || t - mRecordLastTime > kRecordMaxDelayForBlank) {
		if (mpImage) {
			SetImageDirty();

			mpImage->WriteBlankData(mWriteCursor, VDRoundToInt((float)(t - mRecordLastTime) * kATCassetteDataSampleRate / (7159090.0f / 4.0f)), false);

			UpdateRecordingPosition();
		}

		mRecordLastTime = t;
	}
}

void ATCassetteEmulator::UpdateRecordingPosition() {
	uint32 pos = mWriteCursor.mPosition;
	uint32 delta = mPosition ^ pos;

	if (delta) {
		mPosition = pos;

		mLength = mpImage->GetDataLength();

		PositionChanged.NotifyDeferred();

		// check if we've crossed a sector -- if so, notify of changed peaks
		if (delta ^ UINT32_C(0xFFFFF000))
			TapePeaksUpdated.NotifyDeferred();
	}
}

void ATCassetteEmulator::UpdateTraceState() {
	if (!mpTraceChannelFSK)
		return;

	if (mbMotorRunning) {
		if (!mbTraceMotorRunning || mbTraceRecord != mbRecordEnable) {
			mbTraceMotorRunning = true;
			mbTraceRecord = mbRecordEnable;

			const auto eventType = mbRecordEnable
				? ATTraceChannelTape::kEventType_Record
				: ATTraceChannelTape::kEventType_Play;

			mpTraceChannelFSK->AddEvent(mpScheduler->GetTick64(), eventType, GetSamplePos());
			mpTraceChannelTurbo->AddEvent(mpScheduler->GetTick64(), eventType, GetSamplePos());
		}
	} else {
		if (mbTraceMotorRunning) {
			mbTraceMotorRunning = false;

			const uint64 t = mpScheduler->GetTick64();
			mpTraceChannelFSK->TruncateLastEvent(t);
			mpTraceChannelTurbo->TruncateLastEvent(t);
		}
	}
}

void ATCassetteEmulator::UpdateDirectSenseParameters() {
	switch(mDirectSenseMode) {
		case ATCassetteDirectSenseMode::LowSpeed:	// ~1KHz
			mDirectSenseWindow = 32;
			mDirectSenseThreshold = 12;
			break;
		case ATCassetteDirectSenseMode::Normal:		// ~2KHz
			mDirectSenseWindow = 16;
			mDirectSenseThreshold = 6;
			break;
		case ATCassetteDirectSenseMode::HighSpeed:	// ~4KHz
			mDirectSenseWindow = 8;
			mDirectSenseThreshold = 3;
			break;
		case ATCassetteDirectSenseMode::MaxSpeed:	// ~32KHz
			mDirectSenseWindow = 1;
			mDirectSenseThreshold = 1;
			break;
	}
}

void ATCassetteEmulator::ScheduleNextPortTransition() {
	if (!mpImage)
		return;

	if (mPortCurrentPosition < mLength && mbMotorRunning && mbPlayEnable) {
		for(int i=0; i<2; ++i) {
			auto [pos, bit] = mpImage->FindNextBit(mPortCurrentPosition, mPortCurrentPosition + 4000, !mbPortCurrentPolarity, true);

			if (pos > mLength)
				pos = mLength;

			uint32 distance = pos - mPortCurrentPosition;

			mPortCurrentPosition = pos;
			mbPortCurrentPolarity = bit;

			if (distance > 0) {
				// Set the inverse of the next bit on the current state. However, there are three inverters
				// between the tape and pin 4 (joystick right), so we need to uninvert it.
				mpPortMgr->SetInput(mPortInput, (mbInvertTurboData ? !bit : bit) ? ~UINT32_C(0) : ~UINT32_C(0x80));

				mpScheduler->SetEvent(distance * kATCassetteCyclesPerDataSample, this, kATCassetteEventId_PortUpdate, mpPortUpdateEvent);
				return;
			}
		}
	}

	mpScheduler->UnsetEvent(mpPortUpdateEvent);
}
