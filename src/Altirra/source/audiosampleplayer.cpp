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

#include <stdafx.h>
#include <at/atcore/fft.h>
#include "audiosampleplayer.h"
#include "oshelper.h"
#include "resource.h"

///////////////////////////////////////////////////////////////////////////

class ATAudioConvolutionOutput {
public:
	static constexpr int kConvSize = 4096;
	static constexpr int kMaxFrameSize = 1536;
	static constexpr int kMaxSampleSize = kConvSize - kMaxFrameSize;
	static constexpr float kFFTScale = (float)kConvSize / 2;

	void PreTransformSample(float *sample);
	void AccumulateImpulses(const float *impulseFrame, const float *sampleXform);
	void Commit(float *dstL, float *dstR, uint32 len);

	uint32 mBaseOffset = 0;
	uint32 mOverlapSamples = 0;
	bool mbHasImpulses = false;

	ATFFT<4096> mFFT;

	alignas(16) float mXformBuffer[kConvSize] {};
	alignas(16) float mAccumBuffer[kConvSize] {};
	alignas(16) float mOverlapBuffer[kConvSize] {};
};

void ATAudioConvolutionOutput::PreTransformSample(float *sample) {
	mFFT.Forward(sample);

	constexpr float scale = 1.0f / (32767.0f * kFFTScale);
	
	for(int i=0; i<kConvSize; ++i)
		sample[i] *= scale;
}

void ATAudioConvolutionOutput::AccumulateImpulses(const float *impulseFrame, const float *sampleXform) {
	mFFT.Forward(mXformBuffer, impulseFrame);

	// multiply spectra of impulse train and sound sample
	// first two values are real DC and fsc*0.5, rest are complex
	const float *VDRESTRICT src1 = mXformBuffer;
	const float *VDRESTRICT src2 = sampleXform;
	float *VDRESTRICT dst = mAccumBuffer;

	dst[0] += src1[0] * src2[0];
	dst[1] += src1[1] * src2[1];
	dst += 2;
	src1 += 2;
	src2 += 2;

	for(int i=0; i<kConvSize/2-1; ++i) {
		const float r1 = *src1++;
		const float i1 = *src1++;
		const float r2 = *src2++;
		const float i2 = *src2++;

		*dst++ += r1*r2 - i1*i2;
		*dst++ += r1*i2 + r2*i1;
	}

	mbHasImpulses = true;
}

void ATAudioConvolutionOutput::Commit(float *dstL, float *dstR, uint32 len) {
	if (mbHasImpulses) {
		mbHasImpulses = false;

		mFFT.Inverse(mAccumBuffer);

		for(uint32 i=0; i<kConvSize - mBaseOffset; ++i)
			mOverlapBuffer[mBaseOffset + i] += mAccumBuffer[i];

		for(uint32 i=kConvSize - mBaseOffset; i < kConvSize; ++i)
			mOverlapBuffer[mBaseOffset + i - kConvSize] += mAccumBuffer[i];

		memset(mAccumBuffer, 0, sizeof mAccumBuffer);
		mOverlapSamples = kConvSize;
	}
	
	if (!mOverlapSamples)
		return;

	float *VDRESTRICT dstL2 = dstL;
	float *VDRESTRICT dstR2 = dstR;
	float *VDRESTRICT src = &mOverlapBuffer[mBaseOffset];
	uint32 alen = len > mOverlapSamples ? mOverlapSamples : len;
	uint32 tc = kConvSize - mBaseOffset;

	if (dstR2) {
		if (tc >= alen) {
			for(uint32 i=0; i<alen; ++i) {
				*dstL2++ += *src;
				*dstR2++ += *src;
				*src++ = 0;
			}
		} else {
			for(uint32 i=0; i<tc; ++i) {
				*dstL2++ += *src;
				*dstR2++ += *src;
				*src++ = 0;
			}

			src = mOverlapBuffer;
			for(uint32 i=tc; i<alen; ++i) {
				*dstL2++ += *src;
				*dstR2++ += *src;
				*src++ = 0;
			}
		}
	} else {
		if (tc >= alen) {
			for(uint32 i=0; i<alen; ++i) {
				*dstL2++ += *src;
				*src++ = 0;
			}
		} else {
			for(uint32 i=0; i<tc; ++i) {
				*dstL2++ += *src;
				*src++ = 0;
			}

			src = mOverlapBuffer;
			for(uint32 i=tc; i<alen; ++i) {
				*dstL2++ += *src;
				*src++ = 0;
			}
		}
	}

	mOverlapSamples -= alen;
	mBaseOffset += alen;
	mBaseOffset &= kConvSize - 1;
}

///////////////////////////////////////////////////////////////////////////

class ATAudioConvolutionPlayer final : public IATSyncAudioConvolutionPlayer {
public:
	ATAudioConvolutionPlayer(ATAudioSampleId sampleId) : mSampleId(sampleId) {}

	void Init(ATAudioSamplePlayer& parent, ATAudioConvolutionOutput& output, const sint16 *sample, uint32 len, uint32 baseTime);
	void Shutdown();

	ATAudioSampleId GetSampleId() const { return mSampleId; }
	void CommitFrame(uint32 nextTime);
	
	int AddRef() override;
	int Release() override;

	void Play(uint32 t, float volume) override;

private:
	const ATAudioSampleId mSampleId;
	int mRefCount = 0;
	uint32 mBaseTime = 0;
	bool mbHasImpulse = false;
	ATAudioSamplePlayer *mpParent = nullptr;
	ATAudioConvolutionOutput *mpOutput = nullptr;

	alignas(16) float mSampleBuffer[ATAudioConvolutionOutput::kConvSize] {};
	alignas(16) float mImpulseBuffer[ATAudioConvolutionOutput::kConvSize] {};
};

void ATAudioConvolutionPlayer::Init(ATAudioSamplePlayer& parent, ATAudioConvolutionOutput& output, const sint16 *sample, uint32 len, uint32 baseTime) {
	mpParent = &parent;
	mpOutput = &output;
	mBaseTime = baseTime;

	len = std::min<uint32>(len, ATAudioConvolutionOutput::kMaxSampleSize);

	for(uint32 i=0; i<len; ++i)
		mSampleBuffer[i] = (float)sample[i];

	mpOutput->PreTransformSample(mSampleBuffer);
}

void ATAudioConvolutionPlayer::Shutdown() {
	mpParent = nullptr;
	mpOutput = nullptr;
}

void ATAudioConvolutionPlayer::CommitFrame(uint32 nextTime) {
	if (mbHasImpulse) {
		mbHasImpulse = false;

		if (mpOutput)
			mpOutput->AccumulateImpulses(mImpulseBuffer, mSampleBuffer);

		memset(mImpulseBuffer, 0, sizeof(float) * ATAudioConvolutionOutput::kMaxFrameSize);
	}

	mBaseTime = nextTime;
}

int ATAudioConvolutionPlayer::AddRef() {
	return ++mRefCount;
}

int ATAudioConvolutionPlayer::Release() {
	int rc = --mRefCount;
	if (rc == 1) {
		if (mpParent)
			mpParent->RemoveConvolutionPlayer(*this);
	} else if (rc == 0)
		delete this;

	return rc;
}

void ATAudioConvolutionPlayer::Play(uint32 t, float volume) {
	uint32 tickOffset = t - mBaseTime;

	if (tickOffset >= (ATAudioConvolutionOutput::kMaxFrameSize - 1) * 28)
		return;

	uint32 sampleOffset = tickOffset / 28;
	float subOffset = (float)(tickOffset % 28) / 28.0f;

	mImpulseBuffer[sampleOffset] += volume - volume * subOffset;
	mImpulseBuffer[sampleOffset + 1] += volume * subOffset;
	mbHasImpulse = true;
}

///////////////////////////////////////////////////////////////////////////

class ATAudioSoundGroup final : public IATAudioSoundGroup, public vdlist_node {
public:
	ATAudioSoundGroup(ATAudioSamplePlayer& parent) : mpParent(&parent) {}

	int AddRef() override;
	int Release() override;

	bool IsAnySoundQueued() const override;
	void StopAllSounds() override;

	int mRefCount = 0;
	ATAudioSamplePlayer *mpParent;

	ATAudioGroupDesc mDesc;

	// List of active sounds in the group. This is an unsorted list unless supercede
	// mode is enabled, in which case that policy results in this being sorted.
	vdlist<ATAudioSound> mSounds;
};

int ATAudioSamplePlayer::SoundGroup::AddRef() {
	VDASSERT(mRefCount >= 0);

	return ++mRefCount;
}

int ATAudioSamplePlayer::SoundGroup::Release() {
	int rc = --mRefCount;
	VDASSERT(rc >= 0);

	if (!rc) {
		if (mpParent)
			mpParent->CleanupGroup(*this);

		delete this;
	}

	return rc;
}

bool ATAudioSamplePlayer::SoundGroup::IsAnySoundQueued() const {
	return !mSounds.empty();
}

void ATAudioSamplePlayer::SoundGroup::StopAllSounds() {
	if (mpParent)
		mpParent->StopGroupSounds(*this);
}

///////////////////////////////////////////////////////////////////////////

struct ATAudioSound final : public vdlist_node {
	ATSoundId mId {};
	float mVolume = 0;
	uint64 mStartTime = 0;
	uint64 mEndTime = 0;
	uint32 mLoopPeriod = 0;
	uint32 mLength = 0;
	ATAudioMix mMix {};
	bool mbEndValid = false;
	const sint16 *mpSample = nullptr;
	IATAudioSampleSource *mpSource = nullptr;
	vdrefptr<IVDRefCount> mpOwner;

	// This needs to be a weak pointer; all sounds in a group are implicitly
	// soft-stopped when the group is released. It will be null between when
	// the group is released and the soft-stop completes.
	ATAudioSoundGroup *mpGroup = nullptr;
};

///////////////////////////////////////////////////////////////////////////

void ATAudioSamplePool::Init() {
	static const uint32 kResIds[]={
		IDR_DISK_SPIN,
		IDR_TRACK_STEP,
		IDR_TRACK_STEP_2,
		IDR_TRACK_STEP_2,
		IDR_TRACK_STEP_3,
		IDR_SPEAKER_STEP
	};

	static const float kBaseVolumes[]={
		0.05f,
		0.4f,
		0.8f,
		0.8f,
		0.4f,
		1.0f
	};

	static_assert(vdcountof(kResIds) == vdcountof(mSamples), "Sample array mismatch");
	static_assert(vdcountof(kBaseVolumes) == vdcountof(mSamples), "Sample array mismatch");

	vdfastvector<uint8> data;
	for(size_t i=0; i<vdcountof(kResIds); ++i) {
		if (i + 1 == kATAudioSampleId_DiskStep2H) {
			// special case
			auto samp = mSamples[kATAudioSampleId_DiskStep2 - 1];
			samp.mLength >>= 1;
			mSamples[i] = samp;
		} else {
			ATLoadMiscResource(kResIds[i], data);

			const size_t len = data.size() * sizeof(data[0]);
			sint16 *p = (sint16 *)mAllocator.Allocate(len);
			memcpy(p, data.data(), len);

			mSamples[i] = { p, (uint32)(len / sizeof(sint16)), kBaseVolumes[i] };
		}
	}
}

void ATAudioSamplePool::Shutdown() {
	for(ATAudioSampleDesc& desc : mSamples)
		desc = {};

	mFreeSounds.clear();
	mAllocator.Clear();
}

const ATAudioSampleDesc *ATAudioSamplePool::GetSample(ATAudioSampleId sampleId) const {
	const uint32 index = (uint32)((uint32)sampleId - 1);
	
	if (index >= vdcountof(mSamples))
		return nullptr;

	return &mSamples[index];
}

ATAudioSound *ATAudioSamplePool::AllocateSound() {
	if (mFreeSounds.empty())
		mFreeSounds.push_back(mAllocator.Allocate<ATAudioSound>());

	ATAudioSound *s = mFreeSounds.back();
	mFreeSounds.pop_back();

	return s;
}

void ATAudioSamplePool::FreeSound(ATAudioSound *s) {
	s->mpSample = nullptr;
	s->mpSource = nullptr;
	s->mpOwner = nullptr;

	if (s->mpGroup) {
		s->mpGroup->mSounds.erase(s);
		s->mpGroup = nullptr;
	}

	mFreeSounds.push_back(s);
}

///////////////////////////////////////////////////////////////////////////

ATAudioSamplePlayer::ATAudioSamplePlayer(ATAudioSamplePool& pool)
	: mPool(pool)
{
}

ATAudioSamplePlayer::~ATAudioSamplePlayer() {
}

void ATAudioSamplePlayer::Init(ATScheduler *sch) {
	mpScheduler = sch;
	mBaseTime = sch->GetTick();
}

void ATAudioSamplePlayer::Shutdown() {
	for(SoundGroup *group : mGroups) {
		group->mpParent = nullptr;
	}

	for(ATAudioSound *s : mSounds) {
		mPool.FreeSound(s);
	}

	mGroups.clear();
	mSounds.clear();

	while(!mConvoPlayers.empty()) {
		mConvoPlayers.back()->Shutdown();
		mConvoPlayers.back()->Release();
		mConvoPlayers.pop_back();
	}

	mpScheduler = NULL;
}

ATSoundId ATAudioSamplePlayer::AddSound(IATAudioSoundGroup& soundGroup, uint32 delay, ATAudioSampleId sampleId, float volume) {
	const ATAudioSampleDesc *sample = mPool.GetSample(sampleId);

	if (!sample)
		return ATSoundId::Invalid;

	return AddSound(soundGroup, delay, sample->mpData, sample->mLength, sample->mBaseVolume * volume);
}

ATSoundId ATAudioSamplePlayer::AddLoopingSound(IATAudioSoundGroup& soundGroup, uint32 delay, ATAudioSampleId sampleId, float volume) {
	const ATAudioSampleDesc *sample = mPool.GetSample(sampleId);

	if (!sample)
		return ATSoundId::Invalid;

	return AddLoopingSound(soundGroup, delay, sample->mpData, sample->mLength, sample->mBaseVolume * volume);
}

ATSoundId ATAudioSamplePlayer::AddSound(IATAudioSoundGroup& soundGroup, uint32 delay, const sint16 *sample, uint32 len, float volume) {
	const uint64 t = mpScheduler->GetTick64() + delay;

	Sound *s = mPool.AllocateSound();
	s->mLoopPeriod = 0;
	s->mEndTime = t + kATCyclesPerSyncSample * len;
	s->mLength = len;
	s->mVolume = volume * (1.0f / 32767.0f);
	s->mpSample = sample;
	s->mbEndValid = true;

	return StartSound(s, soundGroup, t);
}

ATSoundId ATAudioSamplePlayer::AddLoopingSound(IATAudioSoundGroup& soundGroup, uint32 delay, const sint16 *sample, uint32 len, float volume) {
	const uint64 t = mpScheduler->GetTick64() + delay;

	Sound *s = mPool.AllocateSound();
	s->mLoopPeriod = len;
	s->mEndTime = 0;
	s->mLength = len;
	s->mVolume = volume * (1.0f / 32767.0f);
	s->mpSample = sample;
	s->mbEndValid = false;

	return StartSound(s, soundGroup, t);
}

ATSoundId ATAudioSamplePlayer::AddSound(IATAudioSoundGroup& soundGroup, uint32 delay, IATAudioSampleSource *src, IVDRefCount *owner, uint32 len, float volume) {
	const uint64 t = mpScheduler->GetTick64() + delay;

	Sound *s = mPool.AllocateSound();
	s->mLoopPeriod = 0;
	s->mEndTime = t + kATCyclesPerSyncSample * len;
	s->mLength = len;
	s->mVolume = volume;
	s->mpSource = src;
	s->mpOwner = owner;

	s->mbEndValid = true;

	return StartSound(s, soundGroup, t);
}

ATSoundId ATAudioSamplePlayer::AddLoopingSound(IATAudioSoundGroup& soundGroup, uint32 delay, IATAudioSampleSource *src, IVDRefCount *owner, float volume) {
	const uint64 t = mpScheduler->GetTick64() + delay;

	Sound *s = mPool.AllocateSound();
	s->mLoopPeriod = 0;
	s->mEndTime = t;
	s->mLength = 0;
	s->mVolume = volume;
	s->mpSource = src;
	s->mpOwner = owner;

	s->mbEndValid = false;

	return StartSound(s, soundGroup, t);
}

vdrefptr<IATAudioSoundGroup> ATAudioSamplePlayer::CreateGroup(const ATAudioGroupDesc& desc) {
	vdrefptr<IATAudioSoundGroup> group(new SoundGroup(*this));

	static_cast<SoundGroup *>(group.get())->mDesc = desc;
	mGroups.push_back(static_cast<SoundGroup *>(group.get()));

	return group;
}

void ATAudioSamplePlayer::ForceStopSound(ATSoundId id) {
	auto it = mSounds.begin(), itEnd = mSounds.end();

	for(; it != itEnd; ++it) {
		Sound *s = *it;
		if (s->mId != id)
			continue;

		*it = mSounds.back();
		mSounds.pop_back();

		mPool.FreeSound(s);
		return;
	}
}

void ATAudioSamplePlayer::StopSound(ATSoundId id) {
	StopSound(id, mpScheduler->GetTick64());
}

void ATAudioSamplePlayer::StopSound(ATSoundId id, uint64 time) {
	auto it = mSounds.begin(), itEnd = mSounds.end();

	for(; it != itEnd; ++it) {
		Sound *s = *it;

		if (s->mId != id)
			continue;

		// check if we're killing the sound before it starts
		if (time <= s->mStartTime) {
			*it = mSounds.back();
			mSounds.pop_back();

			mPool.FreeSound(s);
			return;
		}

		// check if we're trying to kill a one-shot after it would already end
		if (s->mbEndValid && time >= s->mEndTime)
			return;

		// mark new end time and exit
		s->mEndTime = time;
		s->mbEndValid = true;
		return;
	}
}

vdrefptr<IATSyncAudioConvolutionPlayer> ATAudioSamplePlayer::CreateConvolutionPlayer(ATAudioSampleId sampleId) {
	for(ATAudioConvolutionPlayer *cplayer : mConvoPlayers) {
		if (cplayer->GetSampleId() == sampleId)
			return vdrefptr(cplayer);
	}

	if (!mpConvoOutput)
		mpConvoOutput = new ATAudioConvolutionOutput;

	vdrefptr<ATAudioConvolutionPlayer> cp(new ATAudioConvolutionPlayer(sampleId));

	const ATAudioSampleDesc *desc = mPool.GetSample(sampleId);

	mConvoPlayers.push_back(cp);
	cp->AddRef();

	cp->Init(*this, *mpConvoOutput, desc->mpData, desc->mLength, mBaseTime);

	return cp;
}

vdrefptr<IATSyncAudioConvolutionPlayer> ATAudioSamplePlayer::CreateConvolutionPlayer(const sint16 *sample, uint32 len) {
	if (!mpConvoOutput)
		mpConvoOutput = new ATAudioConvolutionOutput;

	vdrefptr<ATAudioConvolutionPlayer> cp(new ATAudioConvolutionPlayer(kATAudioSampleId_None));

	mConvoPlayers.push_back(cp);
	cp->AddRef();

	cp->Init(*this, *mpConvoOutput, sample, len, mBaseTime);

	return cp;
}

void ATAudioSamplePlayer::WriteAudio(const ATSyncAudioMixInfo& mixInfo) {
	uint64 startTime = mixInfo.mStartTime;
	float *dstL = mixInfo.mpLeft;
	float *dstR = mixInfo.mpRight;		// normally null, but the edge player may be asked to do stereo
	uint32 n = mixInfo.mCount;

	const uint64 endTime = startTime + (uint64)n * (uint32)kATCyclesPerSyncSample;

	// process one-shot sounds
	auto it = mSounds.begin(), itEnd = mSounds.end();
	while(it != itEnd) {
		Sound *s = *it;

		// drop sounds that we've already passed
		if (s->mEndTime <= startTime && s->mbEndValid) {
			// end time has already passed -- free the sound
			*it = mSounds.back();
			mSounds.pop_back();
			itEnd = mSounds.end();
			
			mPool.FreeSound(s);
			continue;
		}
		
		++it;

		// skip if sound hasn't happened yet
		if (s->mStartTime >= endTime)
			continue;

		// check for the sample starting behind the current window
		uint32 len = s->mLoopPeriod ? n + s->mLoopPeriod : s->mLength;
		uint32 dstOffset = 0;
		uint32 srcOffset = 0;
		
		if (s->mStartTime < startTime) {
			// count the number of samples we need to skip between the sound start and the
			// window start
			uint64 skippedSamples = (startTime - s->mStartTime) / (uint32)kATCyclesPerSyncSample;

			if (skippedSamples) {
				// if looping is enabled, wrap offset within loop
				if (s->mLoopPeriod)
					skippedSamples %= s->mLoopPeriod;

				// if we skipped everything, skip the sound -- this can happen if it only has
				// a fractional sample left
				if (len && skippedSamples >= len)
					continue;

				const uint32 shift = (uint32)skippedSamples;
				srcOffset = shift;
				len -= shift;

				VDASSERT(s->mLoopPeriod || !s->mLength || srcOffset <= s->mLength);
			}
		} else {
			// sound starts within window -- set destination offset
			dstOffset = (s->mStartTime - startTime) / (uint32)kATCyclesPerSyncSample;
		}

		// convert infinite sample length at this point
		if (!len)
			len = n;

		// check if the sound will be truncated due to ending before the window -- note that
		// this may not match the end of a one-shot if the sound has been stopped
		uint32 mixEnd = n;

		if (s->mbEndValid && s->mEndTime < endTime) {
			mixEnd = (s->mEndTime - startTime) / (sint32)kATCyclesPerSyncSample;

			if (mixEnd <= dstOffset) {
				VDASSERT(mixEnd >= dstOffset);
				continue;
			}
		}

		// clip sound to end of current mixing window
		if (len > mixEnd - dstOffset)
			len = mixEnd - dstOffset;

		if (!len)
			continue;

		// mix samples
		const float vol = s->mVolume * mixInfo.mpMixLevels[s->mMix];
		float *VDRESTRICT dstL2 = dstL + dstOffset;
		float *VDRESTRICT dstR2 = dstR ? dstR + dstOffset : nullptr;

		if (s->mpSource) {
			// mix source
			s->mpSource->MixAudio(dstL2, len, vol, srcOffset, mixInfo.mMixingRate);

			if (dstR2)
				s->mpSource->MixAudio(dstR2, len, vol, srcOffset, mixInfo.mMixingRate);
		} else {
			// direct sample -- mix it ourselves
			const sint16 *VDRESTRICT src = s->mpSample + srcOffset;
			const uint32 loopPeriod = s->mLoopPeriod;

			while(len) {
				// compute the length of the block to mix
				uint32 blockLen = len;

				// if looping is enabled, make sure this block does not
				// cross a loop boundary
				if (loopPeriod && blockLen > loopPeriod - srcOffset)
					blockLen = loopPeriod - srcOffset;

				len -= blockLen;

				// mix this block (must count up for MSVC to vectorize....)
				if (dstR2) {
					// mono-to-stereo (expected for edge mixing)
					for(uint32 i=0; i<blockLen; ++i) {
						const float sample = (float)*src++ * vol;
						*dstL2++ += sample;
						*dstR2++ += sample;
					}
				} else {
					// mono (expected for normal mixing)
					for(uint32 i=0; i<blockLen; ++i) {
						const float sample = (float)*src++ * vol;
						*dstL2++ += sample;
					}
				}

				src = s->mpSample;
				srcOffset = 0;
			}
		}
	}

	// process convolution sounds
	if (mpConvoOutput) {
		for(ATAudioConvolutionPlayer *player : mConvoPlayers) {
			player->CommitFrame((uint32)endTime);
		}

		mpConvoOutput->Commit(mixInfo.mpLeft, mixInfo.mpRight, mixInfo.mCount);
	}

	mBaseTime = (uint32)endTime;
}

ATSoundId ATAudioSamplePlayer::StartSound(Sound *s, IATAudioSoundGroup& soundGroup, uint64 startTime) {
	s->mId = (ATSoundId)mNextSoundId;
	mNextSoundId += 2;

	SoundGroup& soundGroupImpl = static_cast<SoundGroup&>(soundGroup);
	auto& sounds = soundGroupImpl.mSounds;

	// If the remove-superceded-sounds option is enabled on the group, stop any sounds that would start
	// on or after this sound's start time.
	if (soundGroupImpl.mDesc.mbRemoveSupercededSounds) {
		while(!sounds.empty()) {
			Sound& lastSound = *sounds.back();
			if (lastSound.mStartTime < startTime)
				break;

			// Force stop is fine here as we're guaranteed that the conflicting sound hasn't started
			// yet (the start time for the new sound can't be in the past).
			ForceStopSound(lastSound.mId);
		}
	}

	soundGroupImpl.mSounds.push_back(s);

	s->mpGroup = &soundGroupImpl;
	s->mMix = soundGroupImpl.mDesc.mAudioMix;
	s->mStartTime = startTime;

	try {
		mSounds.push_back(s);
	} catch(...) {
		mPool.FreeSound(s);
		throw;
	}

	return s->mId;
}

void ATAudioSamplePlayer::CleanupGroup(SoundGroup& group) {
	group.mpParent = nullptr;
	mGroups.erase(&group);

	StopGroupSounds(group);
}

void ATAudioSamplePlayer::StopGroupSounds(SoundGroup& group) {
	for(Sound *sound : group.mSounds) {
		// must remove the sound from the group before we try to soft-stop it, so that
		// StopSound() doesn't invalidate our iterators
		sound->mpGroup = nullptr;

		StopSound(sound->mId);
	}

	group.mSounds.clear();
}

void ATAudioSamplePlayer::RemoveConvolutionPlayer(ATAudioConvolutionPlayer& cplayer) {
	cplayer.Shutdown();

	auto it = std::find(mConvoPlayers.begin(), mConvoPlayers.end(), &cplayer);
	if (it != mConvoPlayers.end()) {
		*it = mConvoPlayers.back();
		mConvoPlayers.pop_back();

		cplayer.Release();
	}
}
