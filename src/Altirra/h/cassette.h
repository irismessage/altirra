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

class ATCassetteEmulator : public IATSchedulerCallback {
public:
	ATCassetteEmulator();
	~ATCassetteEmulator();

	float GetPosition() const;

	void Init(ATPokeyEmulator *pokey, ATScheduler *sched);
	void ColdReset();

	bool IsLoaded() const { return mLength || mAudioLength; }

	void Load(const wchar_t *fn);

	void SetMotorEnable(bool enable);

	void Stop();
	void Play();
	void RewindToStart();

	uint8 ReadBlock(uint16 bufadr, uint16 len, ATCPUEmulatorMemory *mpMem);

	void OnScheduledEvent(uint32 id);

protected:
	void UpdateMotorState();
	void ParseWAVE(VDFile& file);
	void ParseCAS(VDFile& file);

	uint32	mAudioPosition;
	uint32	mAudioLength;
	uint32	mPosition;
	uint32	mLength;
	uint32	mTargetCycle;

	bool	mbMotorEnable;
	bool	mbPlayEnable;
	bool	mbOutputBit;
	int		mSIOPhase;
	uint8	mDataByte;

	ATEvent *mpPlayEvent;
	ATEvent *mpAudioEvent;

	ATPokeyEmulator *mpPokey;
	ATScheduler *mpScheduler;

	typedef vdfastvector<uint32> Bitstream;
	Bitstream	mBitstream;

	typedef vdfastvector<uint8> AudioStream;
	AudioStream	mAudioStream;
};

#endif
