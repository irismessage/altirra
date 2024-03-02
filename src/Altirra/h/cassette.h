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

#include "pokey.h"
#include "scheduler.h"

class VDFile;

class ATBiquadFilter {
public:
	void Init(float fc);
	void Reset();

	float Advance(float x);

protected:
	float a0;
	float a1;
	float a2;
	float b1;
	float b2;
	float w1;
	float w2;
};

class ATCPUEmulatorMemory;
class IVDRandomAccessStream;

class ATCassetteEmulator : public IATSchedulerCallback, public IATPokeyCassetteDevice {
public:
	ATCassetteEmulator();
	~ATCassetteEmulator();

	float GetLength() const;
	float GetPosition() const;

	void Init(ATPokeyEmulator *pokey, ATScheduler *sched);
	void ColdReset();

	bool IsLoaded() const { return mLength || mAudioLength; }
	bool IsMotorRunning() const { return mpPlayEvent != NULL; }
	bool IsLogDataEnabled() const { return mbLogData; }
	bool IsLoadDataAsAudioEnabled() const { return mbLoadDataAsAudio; }

	void Load(const wchar_t *fn);
	void Load(IVDRandomAccessStream& stream);
	void Unload();

	void SetLogDataEnable(bool enable);
	void SetLoadDataAsAudioEnable(bool enable);
	void SetMotorEnable(bool enable);

	void Stop();
	void Play();
	void RewindToStart();

	void SeekToTime(float seconds);
	void SeekToBitPos(uint32 bitPos);
	void SkipForward(float seconds);

	uint8 ReadBlock(uint16 bufadr, uint16 len, ATCPUEmulatorMemory *mpMem);

	void OnScheduledEvent(uint32 id);

protected:
	void PokeyChangeSerialRate(uint32 divisor);
	void PokeyResetSerialInput();

protected:
	void UpdateMotorState();

	enum BitResult {
		kBR_NoOutput,
		kBR_ByteReceived,
		kBR_FramingError
	};

	BitResult ProcessBit();

	void ParseWAVE(IVDRandomAccessStream& stream);
	void ParseCAS(IVDRandomAccessStream& stream);

	void ConvertDataToAudio();

	uint32	mAudioPosition;
	uint32	mAudioLength;
	uint32	mPosition;
	uint32	mLength;

	bool	mbLogData;
	bool	mbLoadDataAsAudio;
	bool	mbMotorEnable;
	bool	mbPlayEnable;
	bool	mbDataLineState;
	bool	mbOutputBit;
	int		mSIOPhase;
	uint8	mDataByte;
	uint8	mThresholdZeroBit;
	uint8	mThresholdOneBit;

	bool	mbDataBitEdge;		// True if we are waiting for the edge of a data bit, false if we are sampling.
	int		mDataBitCounter;
	int		mDataBitHalfPeriod;
	uint32	mAveragingPeriod;

	ATEvent *mpPlayEvent;
	ATEvent *mpAudioEvent;

	ATPokeyEmulator *mpPokey;
	ATScheduler *mpScheduler;

	typedef vdfastvector<uint8> Bitstream;
	Bitstream	mBitstream;

	typedef vdfastvector<uint8> AudioStream;
	AudioStream	mAudioStream;
};

#endif
