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
#include <at/atcore/scheduler.h>
#include "audiosource.h"

enum ATAudioMix {
	kATAudioMix_Drive,
	kATAudioMixCount
};

class ATAudioSyncMixer final : public IATSyncAudioSource {
	ATAudioSyncMixer(const ATAudioSyncMixer&) = delete;
	ATAudioSyncMixer& operator=(const ATAudioSyncMixer&) = delete;
public:
	ATAudioSyncMixer();
	~ATAudioSyncMixer();

	void Init(ATScheduler *sch);
	void Shutdown();

	float GetMixLevel(ATAudioMix mix) const;
	void SetMixLevel(ATAudioMix mix, float level);

	uint32 AddSound(ATAudioMix mix, uint32 delay, const sint16 *sample, uint32 len, float volume);
	uint32 AddLoopingSound(ATAudioMix mix, uint32 delay, const sint16 *sample, uint32 len, float volume);
	void StopSound(uint32 id);
	void StopSound(uint32 id, uint32 time);

public:
	bool SupportsStereoMixing() const override { return false; }
	bool RequiresStereoMixingNow() const override { return false; }
	void WriteAudio(const ATSyncAudioMixInfo& mixInfo) override;

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

	float mMixLevels[kATAudioMixCount];

	VDLinearAllocator mAllocator;
};

#endif	// f_AT_AUDIOSYNCMIXER_H
