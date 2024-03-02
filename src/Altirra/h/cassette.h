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

#ifndef AT_CASSETTE_H
#define AT_CASSETTE_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <optional>
#include <at/atcore/audiosource.h>
#include <at/atcore/enumparse.h>
#include <at/atcore/deferredevent.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/devicesio.h>
#include <at/atio/cassetteimage.h>
#include <at/ataudio/pokey.h>

class VDFile;

class ATCPUEmulatorMemory;
class IVDRandomAccessStream;
class IATAudioMixer;
class IATCassetteImage;
struct ATTraceContext;
class ATTraceChannelTape;

enum class ATCassetteTurboDecodeAlgorithm : uint8;

enum ATCassetteTurboMode : uint8 {
	kATCassetteTurboMode_None,
	kATCassetteTurboMode_CommandControl,
	kATCassetteTurboMode_ProceedSense,
	kATCassetteTurboMode_InterruptSense,
	kATCassetteTurboMode_KSOTurbo2000,
	kATCassetteTurboMode_Always
};

AT_DECLARE_ENUM_TABLE(ATCassetteTurboMode);

enum ATCassettePolarityMode : uint8 {
	kATCassettePolarityMode_Normal,
	kATCassettePolarityMode_Inverted
};

AT_DECLARE_ENUM_TABLE(ATCassettePolarityMode);

enum class ATCassetteDirectSenseMode : uint8 {
	Normal,
	LowSpeed,
	HighSpeed,
	MaxSpeed
};

AT_DECLARE_ENUM_TABLE(ATCassetteDirectSenseMode);

class ATCassetteEmulator final
	: public IATSchedulerCallback
	, public IATPokeyCassetteDevice
	, public IATSyncAudioSource
	, public IATDeviceRawSIO
{
public:
	ATCassetteEmulator();
	~ATCassetteEmulator();

	IATCassetteImage *GetImage() const { return mpImage; }
	const wchar_t *GetPath() const { return mImagePath.c_str(); }
	bool IsImagePersistent() const { return mbImagePersistent; }
	bool IsImageDirty() const { return mbImageDirty; }

	float GetLength() const;
	float GetPosition() const;
	uint32 GetSampleLen() const { return mLength; }
	uint32 GetSamplePos() const { return mPosition; }

	// Return the number of cycles since the leading edge of the current sample.
	sint32 GetSampleCycleOffset() const;

	uint64 GetLastStopCycle() const { return mLastStopCycle; }
	float GetLastStopPosition() const;
	uint32 GetLastStopSamplePos() const { return mLastStopPosition; }

	void Init(ATPokeyEmulator *pokey, ATScheduler *sched, ATScheduler *slowsched, IATAudioMixer *mixer, ATDeferredEventManager *defmgr, IATDeviceSIOManager *sioMgr, IATDevicePortManager *portMgr);
	void Shutdown();
	void ColdReset();

	bool IsLoaded() const { return mpImage != nullptr; }
	bool IsStopped() const { return !mbPlayEnable && !mbRecordEnable; }
	bool IsPlayEnabled() const { return mbPlayEnable; }
	bool IsRecordEnabled() const { return mbRecordEnable; }
	bool IsPaused() const { return mbPaused; }
	bool IsMotorEnabled() const { return mbMotorEnable; }
	bool IsMotorRunning() const { return mbMotorRunning; }
	bool IsLoadDataAsAudioEnabled() const { return mbLoadDataAsAudio; }
	bool IsAutoRewindEnabled() const { return mbAutoRewind; }

	void LoadNew();
	void Load(const wchar_t *fn);
	void Load(IATCassetteImage *image, const wchar_t *path, bool persistent);
	void Unload();
	void SetImagePersistent(const wchar_t *fn);
	void SetImageClean();
	void SetImageDirty();

	void SetLoadDataAsAudioEnable(bool enable);
	void SetRandomizedStartEnabled(bool enable);
	void SetAutoRewindEnabled(bool enable) { mbAutoRewind = enable; }

	ATCassetteTurboDecodeAlgorithm GetTurboDecodeAlgorithm() const { return mTurboDecodeAlgorithm; }
	void SetTurboDecodeAlgorithm(ATCassetteTurboDecodeAlgorithm algorithm) { mTurboDecodeAlgorithm = algorithm; }

	ATCassetteDirectSenseMode GetDirectSenseMode() const { return mDirectSenseMode; }
	void SetDirectSenseMode(ATCassetteDirectSenseMode mode);

	bool IsTurboDecodingEnabled() const { return !mbFSKDecoderEnabled || mbPortMotorState; }

	ATCassetteTurboMode GetTurboMode() const { return mTurboMode; }
	void SetTurboMode(ATCassetteTurboMode turboMode);

	ATCassettePolarityMode GetPolarityMode() const;
	void SetPolarityMode(ATCassettePolarityMode mode);

	bool IsVBIAvoidanceEnabled() const;
	void SetVBIAvoidanceEnabled(bool enable);

	void SetNextVerticalBlankTime(uint64 t);

	void SetTraceContext(ATTraceContext *context);

	void Stop();
	void Play();
	void Record();
	void SetPaused(bool paused);
	void RewindToStart();

	void SeekToTime(float seconds);
	void SeekToBitPos(uint32 bitPos);
	void SkipForward(float seconds);

	uint32 OnPreModifyTape();
	void OnPostModifyTape(uint32 newPos);

	uint8 ReadBlock(uint16 bufadr, uint16 len, ATCPUEmulatorMemory *mpMem);
	uint8 WriteBlock(uint16 bufadr, uint16 len, ATCPUEmulatorMemory *mpMem);

	std::optional<bool> AutodetectBasicNeeded();

public:
	void OnScheduledEvent(uint32 id) override;

public:
	ATDeferredEvent PositionChanged;
	ATDeferredEvent PlayStateChanged;
	ATNotifyList<const vdfunction<void()> *> TapeChanging;
	ATNotifyList<const vdfunction<void()> *> TapeChanged;
	ATDeferredEvent TapePeaksUpdated;
	ATDeferredEvent TapeDirtyStateChanged;
	ATNotifyList<const vdfunction<void(uint32 /* startBitLeadingEdgePos */, uint32 /* stopBitSamplePos */, uint8 /* dataByte */, bool /* framingError */, uint32 /* cyclesPerHalfBit */)> *> ByteDecoded;

public:
	void PokeyChangeSerialRate(uint32 divisor) override;
	void PokeyResetSerialInput() override;
	void PokeyBeginCassetteData(uint8 skctl) override;
	bool PokeyWriteCassetteData(uint8 c, uint32 cyclesPerBit) override;

public:
	bool RequiresStereoMixingNow() const override { return false; }
	void WriteAudio(const ATSyncAudioMixInfo& mixInfo) override;

public:
	void OnCommandStateChanged(bool asserted) override;
	void OnMotorStateChanged(bool asserted) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnSendReady() override;

private:
	void UnloadInternal();
	void UpdateRawSIODevice();
	void UpdateMotorState();
	void UpdateInvertData();
	void UpdateFSKDecoderEnabled();

	struct SampleCursor {
		uint32 mNextTransition;		// sample position of next transition; 0 = uninitialized
		bool mCurrentValue;			// current decoded bit, valid until next transition
		bool mNextValue;			// next decoded bit (at next transition)

		void Reset() { mNextTransition = 0; }
	};

	struct SlidingWindowCursor {
		SampleCursor mHeadCursor;	// cursor for sample at window end (first sample after window)
		SampleCursor mTailCursor;	// cursor for sample at window start (first sample within window)
		uint32 mNextCount;			// 1-bit sum within window at next transition
		uint32 mNextTransition;		// sample position of next transition (0 = uninitialized)
		bool mCurrentValue;			// current decoded bit, valid until next transition
		bool mbFSKBypass;			// true if FSK decoder should be bypassed (turbo enabled)
		uint32 mThresholdLo;		// current sample switches to 0 when count < lo
		uint32 mThresholdHi;		// current sample switches to 1 when count > hi
		uint32 mWindow;				// number of samples in window
		uint32 mOffset;				// offset from start of window to center sample

		void Reset() {
			mHeadCursor.Reset();
			mTailCursor.Reset();
			mNextTransition = 0;
		}

		void Update(IATCassetteImage& image, uint32 pos);
	};

	void ResetCursors();
	void UpdateDirectSense(sint32 posOffset);

	enum BitResult {
		kBR_NoOutput,
		kBR_ByteReceived,
		kBR_FramingError
	};

	BitResult ProcessBit();

	void StartAudio();
	void StopAudio();
	void SeekAudio(uint32 pos);
	void ResyncAudio();
	
	void FlushRecording(uint32 t, bool force);
	void UpdateRecordingPosition();

	void UpdateTraceState();
	void UpdateDirectSenseParameters();
	void ScheduleNextPortTransition();

	uint32	mAudioPosition = 0;
	uint32	mAudioLength = 0;
	uint32	mPosition = 0;
	uint32	mLength = 0;
	uint32	mLastSampleOffset = 0;
	uint32	mJitterPRNG = 0;
	uint32	mJitterSeed = 0;

	SlidingWindowCursor mDirectCursor {};
	SlidingWindowCursor mBitCursor {};
	SampleCursor	mTurboCursor {};

	bool	mbLoadDataAsAudio = false;
	bool	mbAutoRewind = true;
	bool	mbMotorEnable = false;
	bool	mbMotorRunning = false;
	bool	mbPlayEnable = false;
	bool	mbRecordEnable = false;
	bool	mbPaused = false;
	bool	mbDataLineState = false;
	bool	mbOutputBit = false;
	bool	mbInvertData = false;
	bool	mbInvertTurboData = false;
	bool	mbFSKDecoderEnabled = true;
	bool	mbFSKDecoderRequested = true;
	bool	mbFSKControlEnabled = false;
	int		mSIOPhase = 0;
	uint8	mDataByte = 0;
	uint8	mThresholdZeroBit = 0;
	uint8	mThresholdOneBit = 0;

	ATCassetteDirectSenseMode mDirectSenseMode {};
	uint8	mDirectSenseWindow = 0;
	uint8	mDirectSenseThreshold = 0;

	bool	mbDataBitEdge = false;		// True if we are waiting for the edge of a data bit, false if we are sampling.
	int		mDataBitCounter = 0;
	int		mDataBitHalfPeriod = 0;
	uint32	mStartBitPosition = 0;
	uint32	mAveragingPeriod = 0;

	bool	mbRandomizedStartEnabled = false;
	bool	mbVBIAvoidanceEnabled = false;
	uint64	mNextVBITime = 0;

	ATEvent *mpPlayEvent = nullptr;
	ATEvent *mpRecordEvent = nullptr;
	uint32	mRecordLastTime = 0;

	uint64	mLastStopCycle = 0;
	uint32	mLastStopPosition = 0;

	ATPokeyEmulator *mpPokey = nullptr;
	ATScheduler *mpScheduler = nullptr;
	ATScheduler *mpSlowScheduler = nullptr;
	IATAudioMixer *mpAudioMixer = nullptr;

	IATCassetteImage *mpImage = nullptr;
	VDStringW mImagePath;
	bool	mbImagePersistent = false;
	bool	mbImageDirty = false;

	ATCassetteWriteCursor mWriteCursor {};

	IATDeviceSIOManager *mpSIOMgr = nullptr;
	bool	mbRegisteredRawSIO = false;

	IATDevicePortManager *mpPortMgr = nullptr;
	int		mPortInput = -1;
	int		mPortOutput = -1;
	ATEvent *mpPortUpdateEvent = nullptr;
	bool	mbPortCurrentPolarity = false;
	bool	mbPortMotorState = false;
	uint32	mPortCurrentPosition = 0;

	ATCassetteTurboMode mTurboMode = kATCassetteTurboMode_None;
	bool	mbTurboProceedAsserted = false;
	bool	mbTurboInterruptAsserted = false;
	ATCassetteTurboDecodeAlgorithm mTurboDecodeAlgorithm {};

	struct AudioEvent {
		uint32	mStartTime;
		uint32	mStopTime;
		uint32	mPosition;
	};

	typedef vdfastvector<AudioEvent> AudioEvents;
	AudioEvents mAudioEvents;
	bool mbAudioEventOpen = false;

	ATTraceContext *mpTraceContext = nullptr;
	ATTraceChannelTape *mpTraceChannelFSK = nullptr;
	ATTraceChannelTape *mpTraceChannelTurbo = nullptr;
	bool mbTraceMotorRunning = false;
	bool mbTraceRecord = false;
	uint32 mLogIndex = 0;

	// Slightly weird optional trinary (we may have cached that we don't know....)
	std::optional<std::optional<bool>> mNeedBasic;
};

#endif
