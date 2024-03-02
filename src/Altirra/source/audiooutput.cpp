//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2010 Avery Lee
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

#include "stdafx.h"
#include <vd2/system/math.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/time.h>
#include <vd2/Riza/audioout.h>
#include <vd2/Riza/audioformat.h>
#include "audiofilters.h"
#include "audiosource.h"
#include "audiooutput.h"
#include "uirender.h"

class ATAudioOutput final : public IATAudioOutput {
	ATAudioOutput(ATAudioOutput&) = delete;
	ATAudioOutput& operator=(const ATAudioOutput&) = delete;

public:
	ATAudioOutput();
	virtual ~ATAudioOutput();

	virtual	void Init();

	ATAudioApi GetApi();
	virtual void SetApi(ATAudioApi api);

	virtual void SetAudioTap(IATAudioTap *tap);

	virtual IATUIRenderer *GetStatusRenderer() { return mpUIRenderer; }
	virtual void SetStatusRenderer(IATUIRenderer *uir);

	virtual void AddSyncAudioSource(IATSyncAudioSource *src);
	virtual void RemoveSyncAudioSource(IATSyncAudioSource *src);

	virtual void SetCyclesPerSecond(double cps, double repeatfactor);

	virtual bool GetMute();
	virtual void SetMute(bool mute);

	virtual float GetVolume();
	virtual void SetVolume(float vol);

	virtual int GetLatency();
	virtual void SetLatency(int ms);

	virtual int GetExtraBuffer();
	virtual void SetExtraBuffer(int ms);

	virtual void SetFiltersEnabled(bool enable) {
		mFilters[0].SetActiveMode(enable);
		mFilters[1].SetActiveMode(enable);
	}

	virtual void Pause();
	virtual void Resume();

	virtual void WriteAudio(
		const float *left,
		const float *right,
		uint32 count, bool pushAudio, uint32 timestamp);

protected:
	void InternalWriteAudio(const float *left, const float *right, uint32 count, bool pushAudio, uint32 timestamp);
	void RecomputeBuffering();
	void ReinitAudio();

	enum {
		// 1271 samples is the max (35568 cycles/frame / 28 cycles/sample + 1). We add a little bit here
		// to round it out. We need a 16 sample holdover in order to run the FIR filter.
		//
		// This should match the write size used by POKEY. However, if it doesn't, we just split the
		// write, so it'll still work.
		kBufferSize = 1536,

		// The filter needs to keep far enough ahead that there are enough samples to cover the
		// resampler plus the resampler step. The resampler itself needs 7 samples; we add another 9
		// samples to support about an 8:1 ratio.
		kFilterOffset = 16,

		// The prefilter needs to run ahead by the FIR kernel width (nominally 16 + 1 samples).
		kPreFilterOffset = kFilterOffset + ATAudioFilter::kFilterOverlap * 2,

		kSourceBufferSize = (kBufferSize + kPreFilterOffset + 15) & ~15,
	};

	uint32	mBufferLevel = 0;
	uint32	mFilteredSampleCount = 0;
	uint64	mResampleAccum = 0;
	sint64	mResampleRate = 0;
	int		mSamplingRate = 48000;
	ATAudioApi	mApi = kATAudioApi_WaveOut;
	uint32	mPauseCount = 0;
	uint32	mLatencyTargetMin = 0;
	uint32	mLatencyTargetMax = 0;
	int		mLatency = 100;
	int		mExtraBuffer = 100;
	bool	mbMute = false;

	bool	mbFilterStereo = false;
	uint32	mFilterMonoSamples = 0;

	uint32	mRepeatAccum = 0;
	uint32	mRepeatInc = 0;

	uint32	mCheckCounter = 0;
	uint32	mMinLevel = 0;
	uint32	mMaxLevel = 0;
	uint32	mUnderflowCount = 0;
	uint32	mOverflowCount = 0;
	uint32	mDropCounter = 0;

	uint32	mWritePosition = 0;

	uint32	mProfileCounter = 0;
	uint32	mProfileBlockStartPos = 0;
	uint64	mProfileBlockStartTime = 0;

	vdautoptr<IVDAudioOutput>	mpAudioOut;
	IATAudioTap *mpAudioTap = nullptr;
	IATUIRenderer *mpUIRenderer = nullptr;

	ATUIAudioStatus	mAudioStatus = {};

	ATAudioFilter	mFilters[2];

	typedef vdfastvector<IATSyncAudioSource *> SyncAudioSources;
	SyncAudioSources mSyncAudioSources;

	float	mSourceBuffer[2][kBufferSize] = {};
	sint16	mOutputBuffer[kBufferSize * 2] = {};
};

ATAudioOutput::ATAudioOutput() {
}

ATAudioOutput::~ATAudioOutput() {
}

void ATAudioOutput::Init() {
	memset(mSourceBuffer, 0, sizeof mSourceBuffer);

	mbFilterStereo = false;
	mFilterMonoSamples = 0;

	mBufferLevel = 0;
	mResampleAccum = 0;

	mCheckCounter = 0;
	mMinLevel = 0xFFFFFFFFU;
	mMaxLevel = 0;
	mUnderflowCount = 0;
	mOverflowCount = 0;
	mDropCounter = 0;

	mWritePosition = 0;

	mProfileBlockStartPos = 0;
	mProfileBlockStartTime = VDGetPreciseTick();
	mProfileCounter = 0;

	mLatencyTargetMin = (mSamplingRate * 10 / 1000) * 4;
	mLatencyTargetMax = (mSamplingRate * 100 / 1000) * 4;

	RecomputeBuffering();
	ReinitAudio();

	SetCyclesPerSecond(1789772.5, 1.0);
}

ATAudioApi ATAudioOutput::GetApi() {
	return mApi;
}

void ATAudioOutput::SetApi(ATAudioApi api) {
	if (mApi == api)
		return;

	mApi = api;
	ReinitAudio();
}

void ATAudioOutput::SetAudioTap(IATAudioTap *tap) {
	mpAudioTap = tap;
}

void ATAudioOutput::SetStatusRenderer(IATUIRenderer *uir) {
	if (mpUIRenderer != uir) {
		if (mpUIRenderer)
			mpUIRenderer->SetAudioStatus(NULL);

		mpUIRenderer = uir;
	}
}

void ATAudioOutput::AddSyncAudioSource(IATSyncAudioSource *src) {
	mSyncAudioSources.push_back(src);
}

void ATAudioOutput::RemoveSyncAudioSource(IATSyncAudioSource *src) {
	SyncAudioSources::iterator it(std::find(mSyncAudioSources.begin(), mSyncAudioSources.end(), src));

	if (it != mSyncAudioSources.end())
		mSyncAudioSources.erase(it);
}

void ATAudioOutput::SetCyclesPerSecond(double cps, double repeatfactor) {
	mAudioStatus.mExpectedRate = cps / 28.0;
	mResampleRate = (sint64)(0.5 + 4294967296.0 * mAudioStatus.mExpectedRate / (double)mSamplingRate);

	mRepeatInc = VDRoundToInt(repeatfactor * 65536.0);
}

bool ATAudioOutput::GetMute() {
	return mbMute;
}

void ATAudioOutput::SetMute(bool mute) {
	mbMute = mute;
}

float ATAudioOutput::GetVolume() {
	return mFilters[0].GetScale();
}

void ATAudioOutput::SetVolume(float vol) {
	mFilters[0].SetScale(vol);
	mFilters[1].SetScale(vol);
}

int ATAudioOutput::GetLatency() {
	return mLatency;
}

void ATAudioOutput::SetLatency(int ms) {
	if (ms < 10)
		ms = 10;
	else if (ms > 500)
		ms = 500;

	if (mLatency == ms)
		return;

	mLatency = ms;

	RecomputeBuffering();
}

int ATAudioOutput::GetExtraBuffer() {
	return mExtraBuffer;
}

void ATAudioOutput::SetExtraBuffer(int ms) {
	if (ms < 10)
		ms = 10;
	else if (ms > 500)
		ms = 500;

	if (mExtraBuffer == ms)
		return;

	mExtraBuffer = ms;

	RecomputeBuffering();
}

void ATAudioOutput::Pause() {
	if (!mPauseCount++)
		mpAudioOut->Stop();
}

void ATAudioOutput::Resume() {
	if (!--mPauseCount)
		mpAudioOut->Start();
}

void ATAudioOutput::WriteAudio(
	const float *left,
	const float *right,
	uint32 count,
	bool pushAudio,
	uint32 timestamp)
{
	if (!count)
		return;

	mWritePosition += count;

	for(;;) {
		uint32 tc = kBufferSize - mBufferLevel;
		if (tc > count)
			tc = count;

		InternalWriteAudio(left, right, tc, pushAudio, timestamp);

		count -= tc;
		if (!count)
			break;

		timestamp += 28 * tc;
		left += tc;
		if (right)
			right += tc;
	}
}

void ATAudioOutput::InternalWriteAudio(
	const float *left,
	const float *right,
	uint32 count,
	bool pushAudio,
	uint32 timestamp)
{
	VDASSERT(count > 0);
	VDASSERT(mBufferLevel + count <= kBufferSize);

	// check if any sync sources need stereo mixing
	bool needStereo = right != nullptr;

	if (!needStereo) {
		for(IATSyncAudioSource *src : mSyncAudioSources) {
			if (src->RequiresStereoMixingNow()) {
				needStereo = true;
				break;
			}
		}
	}

	// if we need stereo and aren't currently doing stereo filtering, copy channel state over now
	if (needStereo && !mbFilterStereo) {
		mFilters[1].CopyState(mFilters[0]);
		memcpy(mSourceBuffer[1], mSourceBuffer[0], sizeof(float) * mBufferLevel);
		mbFilterStereo = true;
	}

	// copy in samples
	float *const dstLeft = &mSourceBuffer[0][mBufferLevel];
	float *const dstRight = mbFilterStereo ? &mSourceBuffer[1][mBufferLevel] : nullptr;

	memcpy(dstLeft + kPreFilterOffset, left, sizeof(float) * count);

	if (mbFilterStereo) {
		if (right)
			memcpy(dstRight + kPreFilterOffset, right, sizeof(float) * count);
		else
			memcpy(dstRight + kPreFilterOffset, left, sizeof(float) * count);
	}


	// run audio sources
	{
		ATSyncAudioMixInfo mixInfo;
		mixInfo.mStartTime = timestamp;
		mixInfo.mCount = count;
		mixInfo.mpLeft = dstLeft + kPreFilterOffset;
		mixInfo.mpRight = dstRight ? dstRight + kPreFilterOffset : nullptr;

		for(IATSyncAudioSource *src : mSyncAudioSources)
			src->WriteAudio(mixInfo);
	}

	// filter channels
	for(int ch=0; ch<(mbFilterStereo ? 2 : 1); ++ch) {
		mFilters[ch].PreFilter(&mSourceBuffer[ch][mBufferLevel + kPreFilterOffset], count);
		mFilters[ch].Filter(&mSourceBuffer[ch][mBufferLevel + kFilterOffset], count);
	}

	// if we're filtering stereo and getting mono, check if it's safe to switch over
	if (mbFilterStereo && !needStereo && mFilters[0].CloseTo(mFilters[1], 1e-10f)) {
		mFilterMonoSamples += count;

		if (mFilterMonoSamples >= kBufferSize)
			mbFilterStereo = false;
	} else {
		mFilterMonoSamples = 0;
	}

	// Send filtered samples to the audio tap.
	if (mpAudioTap) {
		if (mbFilterStereo)
			mpAudioTap->WriteRawAudio(mSourceBuffer[0] + mBufferLevel + kFilterOffset, mSourceBuffer[1] + mBufferLevel + kFilterOffset, count, timestamp);
		else
			mpAudioTap->WriteRawAudio(mSourceBuffer[0] + mBufferLevel + kFilterOffset, NULL, count, timestamp);
	}

	mBufferLevel += count;
	VDASSERT(mBufferLevel <= kBufferSize);

	// Determine how many samples we can produce via resampling.
	uint32 resampleAvail = mBufferLevel + kFilterOffset;
	uint32 resampleCount = 0;

	uint64 limit = ((uint64)(resampleAvail - 8) << 32) + 0xFFFFFFFFU;

	if (limit >= mResampleAccum) {
		resampleCount = (uint32)((limit - mResampleAccum) / mResampleRate + 1);

		if (resampleCount) {
			if (resampleCount > (sizeof(mOutputBuffer)/sizeof(mOutputBuffer[0]) >> 1)) {
				VDASSERT(!"Resample count too high.");

				resampleCount = sizeof(mOutputBuffer)/sizeof(mOutputBuffer[0]) >> 1;
			}

			if (mbMute) {
				mResampleAccum += mResampleRate * resampleCount;
				memset(mOutputBuffer, 0, sizeof(mOutputBuffer[0]) * resampleCount * 2);
			} else if (mbFilterStereo)
				mResampleAccum = ATFilterResampleStereo(mOutputBuffer, mSourceBuffer[0], mSourceBuffer[1], resampleCount, mResampleAccum, mResampleRate);
			else
				mResampleAccum = ATFilterResampleMonoToStereo(mOutputBuffer, mSourceBuffer[0], resampleCount, mResampleAccum, mResampleRate);

			// determine if we can now shift down the source buffer
			uint32 shift = (uint32)(mResampleAccum >> 32);

			if (shift > mBufferLevel)
				shift = mBufferLevel; 

			if (shift) {
				uint32 bytesToShift = sizeof(float) * (mBufferLevel - shift + kPreFilterOffset);

				memmove(mSourceBuffer[0], mSourceBuffer[0] + shift, bytesToShift);

				if (mbFilterStereo)
					memmove(mSourceBuffer[1], mSourceBuffer[1] + shift, bytesToShift);

				mBufferLevel -= shift;
				mResampleAccum -= (uint64)shift << 32;
			}
		}
	}

	uint32 bytes = mpAudioOut->EstimateHWBufferLevel();

	if (mMinLevel > bytes)
		mMinLevel = bytes;

	if (mMaxLevel < bytes)
		mMaxLevel = bytes;

	bool dropBlock = false;
	if (++mCheckCounter >= 15) {
		mCheckCounter = 0;

		// None				See if we can remove data to lower latency
		// Underflow		Do nothing; we already add data for this
		// Overflow			Do nothing; we may be in turbo
		// Under+overflow	Increase spread

		bool tryDrop = false;
		if (!mUnderflowCount) {
			if (mMinLevel > mLatencyTargetMin + resampleCount * 8) {
				tryDrop = true;
			}
		}

		if (tryDrop) {
			if (++mDropCounter >= 10) {
				mDropCounter = 0;
				dropBlock = true;
			}
		} else {
			mDropCounter = 0;
		}

		if (mpUIRenderer) {
			mAudioStatus.mMeasuredMin = mMinLevel;
			mAudioStatus.mMeasuredMax = mMaxLevel;
			mAudioStatus.mTargetMin = mLatencyTargetMin;
			mAudioStatus.mTargetMax = mLatencyTargetMax;
			mAudioStatus.mbStereoMixing = mbFilterStereo;

			mpUIRenderer->SetAudioStatus(&mAudioStatus);
		}

		mMinLevel = 0xFFFFFFFU;
		mMaxLevel = 0;
		mUnderflowCount = 0;
		mOverflowCount = 0;
	}

	if (++mProfileCounter >= 200) {
		mProfileCounter = 0;
		uint64 t = VDGetPreciseTick();

		mAudioStatus.mIncomingRate = (double)(mWritePosition - mProfileBlockStartPos) / (double)(t - mProfileBlockStartTime) * VDGetPreciseTicksPerSecond();

		mProfileBlockStartPos = mWritePosition;
		mProfileBlockStartTime = t;
	}

	if (bytes < mLatencyTargetMin) {
		++mAudioStatus.mUnderflowCount;
		++mUnderflowCount;
		mpAudioOut->Write(mOutputBuffer, resampleCount * 4);
		mDropCounter = 0;
		dropBlock = false;
	}

	if (dropBlock) {
		++mAudioStatus.mDropCount;
	} else {
		if (bytes < mLatencyTargetMin + mLatencyTargetMax) {
			if (pushAudio || true) {
				mRepeatAccum += mRepeatInc;

				uint32 count = mRepeatAccum >> 16;
				mRepeatAccum &= 0xffff;

				if (count > 10)
					count = 10;

				while(count--)
					mpAudioOut->Write(mOutputBuffer, resampleCount * 4);
			}
		} else {
			++mOverflowCount;
			++mAudioStatus.mOverflowCount;
		}
	}

	mpAudioOut->Flush();
}

void ATAudioOutput::RecomputeBuffering() {
	mLatencyTargetMin = ((mLatency * mSamplingRate + 500) / 1000) * 4;
	mLatencyTargetMax = mLatencyTargetMin + ((mExtraBuffer * mSamplingRate + 500) / 1000) * 4;
}

void ATAudioOutput::ReinitAudio() {
	if (mApi == kATAudioApi_DirectSound)
		mpAudioOut = VDCreateAudioOutputDirectSoundW32();
	else
		mpAudioOut = VDCreateAudioOutputWaveOutW32();

	if (mpAudioOut) {
		const uint32 preferredSamplingRate = mpAudioOut->GetPreferredSamplingRate(nullptr);

		if (preferredSamplingRate == 0)
			mSamplingRate = 48000;
		else if (preferredSamplingRate < 44100)
			mSamplingRate = 44100;
		else if (preferredSamplingRate > 48000)
			mSamplingRate = 48000;
		else
			mSamplingRate = preferredSamplingRate;

		nsVDWinFormats::WaveFormatEx wfex;
		wfex.mFormatTag = nsVDWinFormats::kWAVE_FORMAT_PCM;
		wfex.mChannels = 2;
		wfex.mSamplesPerSec = mSamplingRate;
		wfex.mBlockAlign = 4;
		wfex.mAvgBytesPerSec = wfex.mSamplesPerSec * wfex.mBlockAlign;
		wfex.mBitsPerSample = 16;
		mpAudioOut->Init(kBufferSize * 4, 30, (const tWAVEFORMATEX *)&wfex, NULL);
		mpAudioOut->Start();
	}
}

///////////////////////////////////////////////////////////////////////////

IATAudioOutput *ATCreateAudioOutput() {
	return new ATAudioOutput;
}
