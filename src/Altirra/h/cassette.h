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

#include <at/atcore/deferredevent.h>
#include <at/atcore/scheduler.h>
#include "pokey.h"
#include "audiosource.h"

class VDFile;

class ATCPUEmulatorMemory;
class IVDRandomAccessStream;
class IATAudioOutput;
class IATCassetteImage;

class ATCassetteEmulator final : public IATSchedulerCallback, public IATPokeyCassetteDevice, public IATSyncAudioSource {
public:
	ATCassetteEmulator();
	~ATCassetteEmulator();

	IATCassetteImage *GetImage() const { return mpImage; }

	float GetLength() const;
	float GetPosition() const;
	uint32 GetSampleLen() const { return mLength; }
	uint32 GetSamplePos() const { return mPosition; }

	void Init(ATPokeyEmulator *pokey, ATScheduler *sched, ATScheduler *slowsched, IATAudioOutput *audioOut, ATDeferredEventManager *defmgr);
	void Shutdown();
	void ColdReset();

	bool IsLoaded() const { return mpImage != nullptr; }
	bool IsStopped() const { return !mbPlayEnable && !mbRecordEnable; }
	bool IsPlayEnabled() const { return mbPlayEnable; }
	bool IsRecordEnabled() const { return mbRecordEnable; }
	bool IsPaused() const { return mbPaused; }
	bool IsMotorEnabled() const { return mbMotorEnable; }
	bool IsMotorRunning() const { return mbMotorRunning; }
	bool IsLogDataEnabled() const { return mbLogData; }
	bool IsLoadDataAsAudioEnabled() const { return mbLoadDataAsAudio; }

	void LoadNew();
	void Load(const wchar_t *fn);
	void Load(IVDRandomAccessStream& stream);
	void Unload();

	void SetLogDataEnable(bool enable);
	void SetLoadDataAsAudioEnable(bool enable);
	void SetMotorEnable(bool enable);
	void SetRandomizedStartEnabled(bool enable);

	void Stop();
	void Play();
	void Record();
	void SetPaused(bool paused);
	void RewindToStart();

	void SeekToTime(float seconds);
	void SeekToBitPos(uint32 bitPos);
	void SkipForward(float seconds);

	uint8 ReadBlock(uint16 bufadr, uint16 len, ATCPUEmulatorMemory *mpMem);
	uint8 WriteBlock(uint16 bufadr, uint16 len, ATCPUEmulatorMemory *mpMem);

	void OnScheduledEvent(uint32 id);

public:
	ATDeferredEvent PositionChanged;
	ATDeferredEvent PlayStateChanged;
	ATDeferredEvent TapeChanging;
	ATDeferredEvent TapeChanged;
	ATDeferredEvent TapePeaksUpdated;

protected:
	void PokeyChangeSerialRate(uint32 divisor) override;
	void PokeyResetSerialInput() override;
	void PokeyBeginCassetteData(uint8 skctl) override;
	bool PokeyWriteCassetteData(uint8 c, uint32 cyclesPerBit) override;

protected:
	bool SupportsStereoMixing() const override { return false; }
	bool RequiresStereoMixingNow() const override { return false; }
	void WriteAudio(const ATSyncAudioMixInfo& mixInfo) override;

protected:
	void UnloadInternal();
	void UpdateMotorState();

	enum BitResult {
		kBR_NoOutput,
		kBR_ByteReceived,
		kBR_FramingError
	};

	BitResult ProcessBit();

	void StartAudio();
	void StopAudio();
	void SeekAudio(uint32 pos);
	
	void FlushRecording(uint32 t, bool force);
	void UpdateRecordingPosition();

	uint32	mAudioPosition = 0;
	uint32	mAudioLength = 0;
	uint32	mPosition = 0;
	uint32	mLength = 0;

	bool	mbLogData = false;
	bool	mbLoadDataAsAudio = false;
	bool	mbMotorEnable = false;
	bool	mbMotorRunning = false;
	bool	mbPlayEnable = false;
	bool	mbRecordEnable = false;
	bool	mbPaused = false;
	bool	mbDataLineState = false;
	bool	mbOutputBit = false;
	int		mSIOPhase = 0;
	uint8	mDataByte = 0;
	uint8	mThresholdZeroBit = 0;
	uint8	mThresholdOneBit = 0;

	bool	mbDataBitEdge = false;		// True if we are waiting for the edge of a data bit, false if we are sampling.
	int		mDataBitCounter = 0;
	int		mDataBitHalfPeriod = 0;
	uint32	mAveragingPeriod = 0;

	bool	mbRandomizedStartEnabled = false;

	ATEvent *mpPlayEvent = nullptr;
	ATEvent *mpRecordEvent = nullptr;
	uint32	mRecordLastTime = 0;

	ATPokeyEmulator *mpPokey = nullptr;
	ATScheduler *mpScheduler = nullptr;
	ATScheduler *mpSlowScheduler = nullptr;
	IATAudioOutput *mpAudioOutput = nullptr;

	IATCassetteImage *mpImage = nullptr;

	struct AudioEvent {
		uint32	mStartTime;
		uint32	mStopTime;
		uint32	mPosition;
	};

	typedef vdfastvector<AudioEvent> AudioEvents;
	AudioEvents mAudioEvents;
	bool mbAudioEventOpen = false;
};

#endif
