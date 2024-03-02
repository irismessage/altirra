#ifndef f_AT_AUDIOFILTERS_H
#define f_AT_AUDIOFILTERS_H

extern "C" VDALIGN(16) const sint16 gATAudioResamplingKernel63To44[65][64];

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

	void SetActiveMode(bool active);

	void PreFilter(float * VDRESTRICT dst, uint32 count);
	void Filter(float *dst, const float *src, uint32 count);

protected:
	float	mHiPassAccum;
	float	mHiCoeff;
	float	mScale;
	float	mRawScale;

	float	mLoPassCoeffs[kFilterOverlap];
};

#endif
