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

#ifndef f_AT_AUDIOSAMPLEPLAYER_H
#define f_AT_AUDIOSAMPLEPLAYER_H

#include <vd2/system/linearalloc.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/audiosource.h>
#include <at/atcore/audiomixer.h>

struct ATAudioSampleDesc {
	const sint16 *mpData;
	uint32 mLength;
	float mBaseVolume;
};

struct ATAudioSound;
class ATAudioSoundGroup;
class ATAudioConvolutionOutput;
class ATAudioConvolutionPlayer;

class ATAudioSamplePool {
public:
	void Init();
	void Shutdown();

	const ATAudioSampleDesc *GetSample(ATAudioSampleId sampleId) const;
	ATAudioSound *AllocateSound();
	void FreeSound(ATAudioSound *sound);

private:
	vdfastvector<ATAudioSound *> mFreeSounds;
	VDLinearAllocator mAllocator;

	ATAudioSampleDesc mSamples[6];
};

class ATAudioSamplePlayer final : public IATSyncAudioSource, public IATSyncAudioSamplePlayer {
	ATAudioSamplePlayer(const ATAudioSamplePlayer&) = delete;
	ATAudioSamplePlayer& operator=(const ATAudioSamplePlayer&) = delete;
public:
	ATAudioSamplePlayer(ATAudioSamplePool& pool);
	~ATAudioSamplePlayer();

	void Init(ATScheduler *sch);
	void Shutdown();

public:
	IATSyncAudioSource& AsSource() { return *this; }

	ATSoundId AddSound(IATAudioSoundGroup& soundGroup, uint32 delay, ATAudioSampleId sampleId, float volume) override;
	ATSoundId AddLoopingSound(IATAudioSoundGroup& soundGroup, uint32 delay, ATAudioSampleId sampleId, float volume) override;

	ATSoundId AddSound(IATAudioSoundGroup& soundGroup, uint32 delay, const sint16 *sample, uint32 len, float volume);
	ATSoundId AddLoopingSound(IATAudioSoundGroup& soundGroup, uint32 delay, const sint16 *sample, uint32 len, float volume);

	ATSoundId AddSound(IATAudioSoundGroup& soundGroup, uint32 delay, IATAudioSampleSource *src, IVDRefCount *owner, uint32 len, float volume) override;
	ATSoundId AddLoopingSound(IATAudioSoundGroup& soundGroup, uint32 delay, IATAudioSampleSource *src, IVDRefCount *owner, float volume) override;

	vdrefptr<IATAudioSoundGroup> CreateGroup(const ATAudioGroupDesc& desc) override;

	void ForceStopSound(ATSoundId id) override;
	void StopSound(ATSoundId id) override;
	void StopSound(ATSoundId id, uint64 time) override;

	vdrefptr<IATSyncAudioConvolutionPlayer> CreateConvolutionPlayer(ATAudioSampleId sampleId);
	vdrefptr<IATSyncAudioConvolutionPlayer> CreateConvolutionPlayer(const sint16 *sample, uint32 len);

public:
	bool RequiresStereoMixingNow() const override { return false; }
	void WriteAudio(const ATSyncAudioMixInfo& mixInfo) override;

private:
	using SoundGroup = ATAudioSoundGroup;
	using Sound = ATAudioSound;
	friend ATAudioSoundGroup;
	friend ATAudioConvolutionPlayer;

	ATSoundId StartSound(Sound *s, IATAudioSoundGroup& soundGroup, uint64 startTime);
	void CleanupGroup(SoundGroup& group);
	void StopGroupSounds(SoundGroup& group);

	void RemoveConvolutionPlayer(ATAudioConvolutionPlayer& cplayer);

	ATScheduler *mpScheduler = nullptr;
	uint32 mNextSoundId = 1;
	uint32 mBaseTime = 0;

	typedef vdfastvector<Sound *> Sounds;
	Sounds mSounds;
	Sounds mFreeSounds;

	vdlist<SoundGroup> mGroups;

	vdautoptr<ATAudioConvolutionOutput> mpConvoOutput;
	vdfastvector<ATAudioConvolutionPlayer *> mConvoPlayers;

	ATAudioSamplePool& mPool;
};

#endif	// f_AT_AUDIOSYNCMIXER_H
