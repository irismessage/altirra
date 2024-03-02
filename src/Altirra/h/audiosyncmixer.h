//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2012 Avery Lee
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

#ifndef f_AT_AUDIOSYNCMIXER_H
#define f_AT_AUDIOSYNCMIXER_H

#include <vd2/system/linearalloc.h>
#include "audiosource.h"
#include "scheduler.h"

class ATAudioSyncMixer : public IATSyncAudioSource {
	ATAudioSyncMixer(const ATAudioSyncMixer&);
	ATAudioSyncMixer& operator=(const ATAudioSyncMixer&);
public:
	ATAudioSyncMixer();
	~ATAudioSyncMixer();

	void Init(ATScheduler *sch);
	void Shutdown();

	uint32 AddSound(uint32 delay, const sint16 *sample, uint32 len, float volume);
	uint32 AddLoopingSound(uint32 delay, const sint16 *sample, uint32 len, float volume);
	void StopSound(uint32 id);
	void StopSound(uint32 id, uint32 time);

public:
	virtual void WriteAudio(uint32 startTime, float *dstLeft, float *dstRightOpt, uint32 n);

protected:
	ATScheduler *mpScheduler;
	uint32 mNextSoundId;

	struct Sound {
		uint32 mId;
		uint32 mStartTime;
		uint32 mLoopPeriod;
		uint32 mEndTime;
		uint32 mLength;
		float mVolume;
		bool mbEndValid;
		const sint16 *mpSample;
	};

	struct SoundPred;

	typedef vdfastvector<Sound *> Sounds;
	Sounds mSounds;
	Sounds mFreeSounds;

	VDLinearAllocator mAllocator;
};

#endif	// f_AT_AUDIOSYNCMIXER_H
