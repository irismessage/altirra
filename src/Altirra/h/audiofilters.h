#ifndef f_AT_AUDIOFILTERS_H
#define f_AT_AUDIOFILTERS_H

uint64 ATFilterResampleMono(sint16 *d, const float *s, uint32 count, uint64 accum, sint64 inc);
uint64 ATFilterResampleMonoToStereo(sint16 *d, const float *s, uint32 count, uint64 accum, sint64 inc);
uint64 ATFilterResampleStereo(sint16 *d, const float *s1, const float *s2, uint32 count, uint64 accum, sint64 inc);

void ATFilterComputeSymmetricFIR_8_32F(float *dst, const float *src, size_t n, const float *kernel);

class ATAudioFilter {
public:
	enum { kFilterOverlap = 8 };

	ATAudioFilter();

	float GetScale() const;
	void SetScale(float scale);

	void PreFilter(float * VDRESTRICT dst, uint32 count);
	void Filter(float * VDRESTRICT dst, const float * VDRESTRICT src, uint32 count);

protected:
	float	mHiPassAccum;
	float	mScale;
	float	mRawScale;

	float	mLoPassCoeffs[kFilterOverlap];
};

#endif
