//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef f_AT_AUDIOFILTERS_H
#define f_AT_AUDIOFILTERS_H

#include <math.h>
#include <vd2/system/vdalloc.h>

class ATAudioFilterIIR;

struct ATAudioFilterKernel63To44 {
	alignas(16) sint16 mFilter[65][64];
};

extern "C" const ATAudioFilterKernel63To44 gATAudioResamplingKernel63To44;

class ATFastMathScope {
public:
	ATFastMathScope();
	~ATFastMathScope();

private:
	unsigned mPrevValue;
};

uint64 ATFilterResampleMono16(sint16 *d, const float *s, uint32 count, uint64 accum, sint64 inc, bool interp);
uint64 ATFilterResampleMonoToStereo16(sint16 *d, const float *s, uint32 count, uint64 accum, sint64 inc, bool interp);
uint64 ATFilterResampleStereo16(sint16 *d, const float *s1, const float *s2, uint32 count, uint64 accum, sint64 inc, bool interp);

void ATFilterComputeSymmetricFIR_8_32F(float *dst, size_t n, const float *kernel);

class ATAudioFilter {
	ATAudioFilter(const ATAudioFilter&) = delete;
	ATAudioFilter& operator=(const ATAudioFilter&) = delete;

public:
	enum { kFilterOverlap = 16 };

	ATAudioFilter();
	~ATAudioFilter();

	void SetLPF(float fc, bool useFIR32);

	void CopyState(const ATAudioFilter& src) {
		mHiPassAccum = src.mHiPassAccum;
	}

	bool CloseTo(const ATAudioFilter& src, float threshold) {
		return fabsf(src.mHiPassAccum - mHiPassAccum) < threshold && fabsf(src.mDiffHistory - mDiffHistory) < threshold;
	}

	float GetScale() const;
	void SetScale(float scale);

	void SetActiveMode(bool active);

	void PreFilter(float * VDRESTRICT dst, uint32 count, float dcLevel);

	// Run a high-pass filter on adjacent differences rather than normal samples. This takes advantage
	// of Direct Form I by cancelling the adjacent difference operation on the input with a running sum
	// operation of a previous stage; this avoids the need to expand edges to pulses. PreFilter1()
	// is the optional pre-differencing step, while PreFilter2() is the integration step.
	void PreFilterDiff(float * VDRESTRICT dst, uint32 count);
	void PreFilterEdges(float * VDRESTRICT dst, uint32 count, float dcLevel);

	// Run low-pass antialiasing filter. An input of N samples generates an output of N-M*2 samples where
	// M = kFilterOverlap, ahead by M samples. For example, with M=16, x[0] is replaced by y[16].
	void Filter(float *dst, uint32 count);

protected:
	float	mHiPassAccum = 0;
	float	mHiCoeff = 0;
	float	mScale = 0;
	float	mDiffHistory = 0;
	bool	mbUseFIR16 = false;

	float	mLoPassCoeffs[kFilterOverlap];

	vdautoptr<ATAudioFilterIIR> mpIIR;
};

#endif
