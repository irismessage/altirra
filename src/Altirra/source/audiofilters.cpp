#include "stdafx.h"
#include <vd2/system/cpuaccel.h>
#include <vd2/system/math.h>
#include <intrin.h>
#include "audiofilters.h"

extern "C" __declspec(align(16)) const float gVDCaptureAudioResamplingKernel[32][8] = {
	{+0x0000,+0x0000,+0x0000,+0x4000,+0x0000,+0x0000,+0x0000,+0x0000 },
	{-0x000a,+0x0052,-0x0179,+0x3fe2,+0x019f,-0x005b,+0x000c,+0x0000 },
	{-0x0013,+0x009c,-0x02cc,+0x3f86,+0x0362,-0x00c0,+0x001a,+0x0000 },
	{-0x001a,+0x00dc,-0x03f9,+0x3eef,+0x054a,-0x012c,+0x002b,+0x0000 },
	{-0x001f,+0x0113,-0x0500,+0x3e1d,+0x0753,-0x01a0,+0x003d,+0x0000 },
	{-0x0023,+0x0141,-0x05e1,+0x3d12,+0x097c,-0x021a,+0x0050,-0x0001 },
	{-0x0026,+0x0166,-0x069e,+0x3bd0,+0x0bc4,-0x029a,+0x0066,-0x0001 },
	{-0x0027,+0x0182,-0x0738,+0x3a5a,+0x0e27,-0x031f,+0x007d,-0x0002 },
	{-0x0028,+0x0197,-0x07b0,+0x38b2,+0x10a2,-0x03a7,+0x0096,-0x0003 },
	{-0x0027,+0x01a5,-0x0807,+0x36dc,+0x1333,-0x0430,+0x00af,-0x0005 },
	{-0x0026,+0x01ab,-0x083f,+0x34db,+0x15d5,-0x04ba,+0x00ca,-0x0007 },
	{-0x0024,+0x01ac,-0x085b,+0x32b3,+0x1886,-0x0541,+0x00e5,-0x0008 },
	{-0x0022,+0x01a6,-0x085d,+0x3068,+0x1b40,-0x05c6,+0x0101,-0x000b },
	{-0x001f,+0x019c,-0x0846,+0x2dfe,+0x1e00,-0x0644,+0x011c,-0x000d },
	{-0x001c,+0x018e,-0x0819,+0x2b7a,+0x20c1,-0x06bb,+0x0136,-0x0010 },
	{-0x0019,+0x017c,-0x07d9,+0x28e1,+0x2380,-0x0727,+0x014f,-0x0013 },
	{-0x0016,+0x0167,-0x0788,+0x2637,+0x2637,-0x0788,+0x0167,-0x0016 },
	{-0x0013,+0x014f,-0x0727,+0x2380,+0x28e1,-0x07d9,+0x017c,-0x0019 },
	{-0x0010,+0x0136,-0x06bb,+0x20c1,+0x2b7a,-0x0819,+0x018e,-0x001c },
	{-0x000d,+0x011c,-0x0644,+0x1e00,+0x2dfe,-0x0846,+0x019c,-0x001f },
	{-0x000b,+0x0101,-0x05c6,+0x1b40,+0x3068,-0x085d,+0x01a6,-0x0022 },
	{-0x0008,+0x00e5,-0x0541,+0x1886,+0x32b3,-0x085b,+0x01ac,-0x0024 },
	{-0x0007,+0x00ca,-0x04ba,+0x15d5,+0x34db,-0x083f,+0x01ab,-0x0026 },
	{-0x0005,+0x00af,-0x0430,+0x1333,+0x36dc,-0x0807,+0x01a5,-0x0027 },
	{-0x0003,+0x0096,-0x03a7,+0x10a2,+0x38b2,-0x07b0,+0x0197,-0x0028 },
	{-0x0002,+0x007d,-0x031f,+0x0e27,+0x3a5a,-0x0738,+0x0182,-0x0027 },
	{-0x0001,+0x0066,-0x029a,+0x0bc4,+0x3bd0,-0x069e,+0x0166,-0x0026 },
	{-0x0001,+0x0050,-0x021a,+0x097c,+0x3d12,-0x05e1,+0x0141,-0x0023 },
	{+0x0000,+0x003d,-0x01a0,+0x0753,+0x3e1d,-0x0500,+0x0113,-0x001f },
	{+0x0000,+0x002b,-0x012c,+0x054a,+0x3eef,-0x03f9,+0x00dc,-0x001a },
	{+0x0000,+0x001a,-0x00c0,+0x0362,+0x3f86,-0x02cc,+0x009c,-0x0013 },
	{+0x0000,+0x000c,-0x005b,+0x019f,+0x3fe2,-0x0179,+0x0052,-0x000a },
};

void ATFilterNormalizeKernel(float *v, size_t n, float scale) {
	float sum = 0;

	for(size_t i=0; i<n; ++i)
		sum += v[i];

	scale /= sum;

	for(size_t i=0; i<n; ++i)
		v[i] *= scale;
}

uint64 ATFilterResampleMono(sint16 *d, const float *s, uint32 count, uint64 accum, sint64 inc) {
	do {
		const float *s2 = s + (accum >> 32);
		const float *f = gVDCaptureAudioResamplingKernel[(uint32)accum >> 27];

		accum += inc;

		float v = s2[0]*f[0]
				+ s2[1]*f[1]
				+ s2[2]*f[2]
				+ s2[3]*f[3]
				+ s2[4]*f[4]
				+ s2[5]*f[5]
				+ s2[6]*f[6]
				+ s2[7]*f[7];

		*d++ = VDClampedRoundFixedToInt16Fast(v * (1.0f / 16384.0f));
	} while(--count);

	return accum;
}

uint64 ATFilterResampleMonoToStereo(sint16 *d, const float *s, uint32 count, uint64 accum, sint64 inc) {
	do {
		const float *s2 = s + (accum >> 32);
		const float *f = gVDCaptureAudioResamplingKernel[(uint32)accum >> 27];

		accum += inc;

		float v = s2[0]*f[0]
				+ s2[1]*f[1]
				+ s2[2]*f[2]
				+ s2[3]*f[3]
				+ s2[4]*f[4]
				+ s2[5]*f[5]
				+ s2[6]*f[6]
				+ s2[7]*f[7];

		d[0] = d[1] = VDClampedRoundFixedToInt16Fast(v * (1.0f / 16384.0f));
		d += 2;
	} while(--count);

	return accum;
}

uint64 ATFilterResampleStereo(sint16 *d, const float *s1, const float *s2, uint32 count, uint64 accum, sint64 inc) {
	do {
		const float *r1 = s1 + (accum >> 32);
		const float *r2 = s2 + (accum >> 32);
		const float *f = gVDCaptureAudioResamplingKernel[(uint32)accum >> 27];

		accum += inc;

		float a = r1[0]*f[0]
				+ r1[1]*f[1]
				+ r1[2]*f[2]
				+ r1[3]*f[3]
				+ r1[4]*f[4]
				+ r1[5]*f[5]
				+ r1[6]*f[6]
				+ r1[7]*f[7];

		float b = r2[0]*f[0]
				+ r2[1]*f[1]
				+ r2[2]*f[2]
				+ r2[3]*f[3]
				+ r2[4]*f[4]
				+ r2[5]*f[5]
				+ r2[6]*f[6]
				+ r2[7]*f[7];

		d[0] = VDClampedRoundFixedToInt16Fast(a * (1.0f / 16384.0f));
		d[1] = VDClampedRoundFixedToInt16Fast(b * (1.0f / 16384.0f));
		d += 2;
	} while(--count);

	return accum;
}

void ATFilterComputeSymmetricFIR_8_32F_Scalar(float *dst, const float *src, size_t n, const float *kernel) {
	const float k0 = kernel[0];
	const float k1 = kernel[1];
	const float k2 = kernel[2];
	const float k3 = kernel[3];
	const float k4 = kernel[4];
	const float k5 = kernel[5];
	const float k6 = kernel[6];
	const float k7 = kernel[7];

	do {
		float v = src[7] * k0
				+ (src[ 6] + src[ 8]) * k1
				+ (src[ 5] + src[ 9]) * k2
				+ (src[ 4] + src[10]) * k3
				+ (src[ 3] + src[11]) * k4
				+ (src[ 2] + src[12]) * k5
				+ (src[ 1] + src[13]) * k6
				+ (src[ 0] + src[14]) * k7;

		++src;

		*dst++ = v;
	} while(--n);
}

void ATFilterComputeSymmetricFIR_8_32F_SSE(float *dst, const float *src, size_t n, const float *kernel) {
	__m128 zero = _mm_setzero_ps();
	__m128 x0 = zero;
	__m128 x1 = zero;
	__m128 x2 = zero;
	__m128 x3 = zero;
	__m128 f0;
	__m128 f1;
	__m128 f2;
	__m128 f3;

	// init filter
	__m128 k0 = _mm_loadu_ps(kernel + 0);
	__m128 k1 = _mm_loadu_ps(kernel + 4);

	f0 = _mm_shuffle_ps(k1, k1, _MM_SHUFFLE(0, 1, 2, 3));
	f1 = _mm_shuffle_ps(k0, k0, _MM_SHUFFLE(0, 1, 2, 3));
	f2 = _mm_move_ss(k0, k1);
	f2 = _mm_shuffle_ps(f2, f2, _MM_SHUFFLE(0, 3, 2, 1));
	f3 = _mm_move_ss(k1, zero);
	f3 = _mm_shuffle_ps(f3, f3, _MM_SHUFFLE(0, 3, 2, 1));

	// prime
	for(int i=0; i<14; ++i) {
		x0 = _mm_move_ss(x0, x1);
		x0 = _mm_shuffle_ps(x0, x0, _MM_SHUFFLE(0, 3, 2, 1));
		x1 = _mm_move_ss(x1, x2);
		x1 = _mm_shuffle_ps(x1, x1, _MM_SHUFFLE(0, 3, 2, 1));
		x2 = _mm_move_ss(x2, x3);
		x2 = _mm_shuffle_ps(x2, x2, _MM_SHUFFLE(0, 3, 2, 1));
		x3 = _mm_move_ss(x3, zero);
		x3 = _mm_shuffle_ps(x3, x3, _MM_SHUFFLE(0, 3, 2, 1));

		__m128 s = _mm_load1_ps(src++);
		x0 = _mm_add_ps(x0, _mm_mul_ps(f0, s));
		x1 = _mm_add_ps(x1, _mm_mul_ps(f1, s));
		x2 = _mm_add_ps(x2, _mm_mul_ps(f2, s));
		x3 = _mm_add_ps(x3, _mm_mul_ps(f3, s));
	}

	// pipeline
	do {
		x0 = _mm_move_ss(x0, x1);
		x0 = _mm_shuffle_ps(x0, x0, _MM_SHUFFLE(0, 3, 2, 1));
		x1 = _mm_move_ss(x1, x2);
		x1 = _mm_shuffle_ps(x1, x1, _MM_SHUFFLE(0, 3, 2, 1));
		x2 = _mm_move_ss(x2, x3);
		x2 = _mm_shuffle_ps(x2, x2, _MM_SHUFFLE(0, 3, 2, 1));
		x3 = _mm_move_ss(x3, zero);
		x3 = _mm_shuffle_ps(x3, x3, _MM_SHUFFLE(0, 3, 2, 1));

		__m128 s = _mm_load1_ps(src++);
		x0 = _mm_add_ps(x0, _mm_mul_ps(f0, s));
		x1 = _mm_add_ps(x1, _mm_mul_ps(f1, s));
		x2 = _mm_add_ps(x2, _mm_mul_ps(f2, s));
		x3 = _mm_add_ps(x3, _mm_mul_ps(f3, s));

		_mm_store_ss(dst++, x0);
	} while(--n);
}

#ifdef VD_CPU_X86
void __declspec(naked) __cdecl ATFilterComputeSymmetricFIR_8_32F_SSE_asm(float *dst, const float *src, size_t n, const float *kernel) {
	__asm {
	mov	edx, esp
	and	esp, -16
	sub	esp, 80
	mov	[esp+64], edx

	mov	eax, [edx+16]
	mov	ecx, [edx+8]

	xorps	xmm7, xmm7

	movups	xmm0, [eax+16]
	movups	xmm1, [eax]
	movaps	xmm2, xmm1
	movaps	xmm3, xmm0
	shufps	xmm0, xmm0, 1bh
	shufps	xmm1, xmm1, 1bh
	movss	xmm2, xmm3
	shufps	xmm2, xmm2, 39h
	movss	xmm3, xmm7
	shufps	xmm3, xmm3, 39h
	movaps	[esp], xmm0
	movaps	[esp+16], xmm1
	movaps	[esp+32], xmm2
	movaps	[esp+48], xmm3

	mov	eax, 14
	xorps	xmm0, xmm0
	xorps	xmm1, xmm1
	xorps	xmm2, xmm2
	xorps	xmm3, xmm3
ploop:
	movss	xmm0, xmm1
	shufps	xmm0, xmm0, 39h
	movss	xmm1, xmm2
	shufps	xmm1, xmm1, 39h
	movss	xmm2, xmm3
	shufps	xmm2, xmm2, 39h
	movss	xmm3, xmm7
	shufps	xmm3, xmm3, 39h

	movss	xmm6, [ecx]
	add		ecx, 4
	shufps	xmm6, xmm6, 0

	movaps	xmm4, xmm6
	mulps	xmm4, [esp+0]
	addps	xmm0, xmm4

	movaps	xmm5, xmm6
	mulps	xmm5, [esp+16]
	addps	xmm1, xmm5

	movaps	xmm4, xmm6
	mulps	xmm4, [esp+32]
	addps	xmm2, xmm4

	mulps	xmm6, [esp+48]
	addps	xmm3, xmm6

	dec		eax
	jne		ploop
	
	mov	eax, [edx+12]
	mov	edx, [edx+4]
xloop:
	movss	xmm0, xmm1
	shufps	xmm0, xmm0, 39h
	movss	xmm1, xmm2
	shufps	xmm1, xmm1, 39h
	movss	xmm2, xmm3
	shufps	xmm2, xmm2, 39h
	movss	xmm3, xmm7
	shufps	xmm3, xmm3, 39h

	movss	xmm6, [ecx]
	add		ecx, 4
	shufps	xmm6, xmm6, 0

	movaps	xmm4, xmm6
	mulps	xmm4, [esp+0]
	addps	xmm0, xmm4

	movaps	xmm5, xmm6
	mulps	xmm5, [esp+16]
	addps	xmm1, xmm5

	movaps	xmm4, xmm6
	mulps	xmm4, [esp+32]
	addps	xmm2, xmm4

	mulps	xmm6, [esp+48]
	addps	xmm3, xmm6

	movss	[edx], xmm0
	add	edx, 4

	dec	eax
	jne	xloop

	mov	esp, [esp+64]
	ret
	}
}
#endif

void ATFilterComputeSymmetricFIR_8_32F(float *dst, const float *src, size_t n, const float *kernel) {
#ifdef VD_CPU_X86
	if (SSE_enabled) {
		ATFilterComputeSymmetricFIR_8_32F_SSE_asm(dst, src, n, kernel);
		return;
	}
#endif

	ATFilterComputeSymmetricFIR_8_32F_Scalar(dst, src, n, kernel);
}

///////////////////////////////////////////////////////////////////////////

ATAudioFilter::ATAudioFilter()
	: mHiPassAccum(0)
{
	SetScale(1.0f);

	// Set up a FIR low pass filter, Blackman window, 15KHz cutoff (63920Hz sampling rate)
	const float fc = 15000.0f / 63920.0f;
	float sum = 0.5f;
	mLoPassCoeffs[0] = 1.0f;

	for(int i=1; i<kFilterOverlap; ++i) {
		float x = (float)i * nsVDMath::kfPi;
		float y = x / 128.0f * 2.0f;
		float w = 0.42f + 0.5f * cosf(y) + 0.08f * cosf(y+y);

		float f = sinf(2.0f * x * fc) / (2.0f * x * fc) * w;

		mLoPassCoeffs[i] = f;

		sum += f;
	}

	float scale = 0.5f / sum;

	for(int i=0; i<kFilterOverlap; ++i)
		mLoPassCoeffs[i] *= scale;
}

float ATAudioFilter::GetScale() const {
	return mRawScale;
}

void ATAudioFilter::SetScale(float scale) {
	// We accumulate 28 cycles worth of output per sample, and each output can
	// be from 0-60 (4*15). We ignore the speaker and cassette inputs in order
	// to get a little more range.
	mRawScale = scale;
	mScale = scale * (1.0f / (60.0f * 28.0f));
}

void ATAudioFilter::PreFilter(float * VDRESTRICT dst, uint32 count) {
//	const float kHiPass = 0.0055f;
	const float kHiPass = 0.0001f;

	const float scale = mScale;
	float hiAccum = mHiPassAccum;

	do {
		float v0 = *dst;
		float v1 = v0 - hiAccum;
		hiAccum += v1 * kHiPass;

		*dst++ = v1 * scale;
	} while(--count);

	mHiPassAccum = hiAccum;
}

void ATAudioFilter::Filter(float *dst, const float *src, uint32 count) {
	ATFilterComputeSymmetricFIR_8_32F(dst, src, count, mLoPassCoeffs);
}
