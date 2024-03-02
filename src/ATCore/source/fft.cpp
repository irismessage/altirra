//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#include <stdafx.h>
#include <math.h>
#include <bit>
#include <intrin.h>
#include <at/atcore/fft.h>

/////////////////////////////////////////////////////////////////////////////

void ATFFT_DIT_Radix2_Scalar(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIF_Radix2_Scalar(float *y0, const float *w4, int N, int logStep);

void ATFFT_DIT_Radix8_Scalar(float *dst0, const float *y0, const int *order0, int N);
void ATFFT_DIF_Radix8_Scalar(float *dst0, const float *src0, const int *order0, int N);

void ATFFT_DIT_R2C_Scalar(float *dst0, const float *src0, const float *w, int N);
void ATFFT_DIF_C2R_Scalar(float *dst0, const float *x, const float *w, int N);

void ATFFT_MultiplyAdd_Scalar(float *VDRESTRICT dst, const float *VDRESTRICT src1, const float *VDRESTRICT src2, int N);

/////////////////////////////////////////////////////////////////////////////

void ATFFT_DIT_Radix2_SSE2(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIT_Radix4_SSE2(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIF_Radix2_SSE2(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIF_Radix4_SSE2(float *y0, const float *w4, int N, int logStep);

void ATFFT_DIT_Radix8_SSE2(float *dst0, const float *y0, const int *order0, int N);
void ATFFT_DIF_Radix8_SSE2(float *dst0, const float *src0, const int *order0, int N);

void ATFFT_DIT_R2C_SSE2(float *dst0, const float *src0, const float *w, int N);
void ATFFT_DIF_C2R_SSE2(float *dst0, const float *x, const float *w, int N);

void ATFFT_MultiplyAdd_SSE2(float *VDRESTRICT dst, const float *VDRESTRICT src1, const float *VDRESTRICT src2, int N);

/////////////////////////////////////////////////////////////////////////////

void ATFFT_DIT_Radix2_NEON(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIT_Radix4_NEON(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIF_Radix2_NEON(float *y0, const float *w4, int N, int logStep);
void ATFFT_DIF_Radix4_NEON(float *y0, const float *w4, int N, int logStep);

void ATFFT_DIT_Radix8_NEON(float *dst0, const float *y0, const int *order0, int N);
void ATFFT_DIF_Radix8_NEON(float *dst0, const float *src0, const int *order0, int N);

void ATFFT_DIT_R2C_NEON(float *dst0, const float *src0, const float *w, int N);
void ATFFT_DIF_C2R_NEON(float *dst0, const float *x, const float *w, int N);

///////////////////////////////////////////////////////////////////////////

void ATFFTBase::Init(float *dst, int numRadix4, int numRadix2, bool useVec4, int *fwdorder, int N) {
	const int N2 = N >> 1;
	const int N4 = N >> 2;
	const int N16 = N >> 4;
	const int N32 = N >> 5;
	float wstep = -3.1415926535897932f / (float)N2;
	int step = N16;
	
	// radix-4
	while(numRadix4--) {
		int i = 0;

		step >>= 1;

		if (useVec4) {
			while(i<N4) {
				for(int j=0; j<4; ++j) {
					dst[0] = cosf((float)i * wstep);
					dst[4] = sinf((float)i * wstep);
					dst[8] = cosf((float)i * wstep * 2);
					dst[12] = sinf((float)i * wstep * 2);
					dst[16] = cosf((float)i * wstep * 3);
					dst[20] = sinf((float)i * wstep * 3);
					++dst;
					i += step;
				}
				dst += 20;
			}
		} else {
			while(i<N4) {
				*dst++ = cosf((float)i * wstep);
				*dst++ = sinf((float)i * wstep);
				*dst++ = cosf((float)i * wstep * 2);
				*dst++ = sinf((float)i * wstep * 2);
				*dst++ = cosf((float)i * wstep * 3);
				*dst++ = sinf((float)i * wstep * 3);
				i += step;
			}
		}

		step >>= 1;
	}

	while(numRadix2--) {
		int i = 0;

		// radix-2
		if (useVec4) {
			while(i<N2) {
				for(int j=0; j<4; ++j) {
					dst[0] = cosf((float)i * wstep);
					dst[4] = sinf((float)i * wstep);
					++dst;
					i += step;
				}
				dst += 4;
			}
		} else {
			while(i<N2) {
				*dst++ = cosf((float)i * wstep);
				*dst++ = sinf((float)i * wstep);
				i += step;
			}
		}

		step >>= 1;
	}

	for(int i=0; i<N2; ++i) {
		*dst++ = cosf((float)i * wstep);
		*dst++ = sinf((float)i * wstep);
	}

	int *fwd = fwdorder;
	int idx = 0;
	for(int i=0; i<N16; ++i) {
		*fwd++ = idx*16;

		int bit = N32;
		while(idx & bit)
			bit >>= 1;

		idx = idx + bit*2 - N/16 + bit;
	}
}

void ATFFTBase::ForwardImpl(float *dst, const float *src, float *work, float *wtab, int numRadix4, int numRadix2, bool useVec4, int *fwdorder, int log2N) {
	const int N = 1 << log2N;

	// run initial radix-8 stage, including bit reversal reordering
	#if defined(ATFFT_USE_NEON)
		ATFFT_DIT_Radix8_NEON(work, src, fwdorder, N);
	#elif defined(ATFFT_USE_SSE2)
		ATFFT_DIT_Radix8_SSE2(work, src, fwdorder, N);
	#else
		ATFFT_DIT_Radix8_Scalar(work, src, fwdorder, N);
	#endif

	// run radix-4 stages
	float *w = wtab;
	int lstep = 4;

	for(int i=0; i<numRadix4; ++i) {
		#if defined(ATFFT_USE_NEON)
			ATFFT_DIT_Radix4_NEON(work, w, N, lstep);
		#elif defined(ATFFT_USE_SSE2)
			ATFFT_DIT_Radix4_SSE2(work, w, N, lstep);
		#endif

		w += ptrdiff_t(3) << lstep;
		lstep += 2;
	}
		
	// run radix-2 stages
	for(int i=0; i<numRadix2; ++i) {
		#if defined(ATFFT_USE_SSE2)
			ATFFT_DIT_Radix2_SSE2(work, w, N, lstep);
		#elif defined(ATFFT_USE_NEON)
			ATFFT_DIT_Radix2_NEON(work, w, N, lstep);
		#else
			ATFFT_DIT_Radix2_Scalar(work, w, N, lstep);
		#endif

		w += ptrdiff_t(1) << lstep;
		++lstep;
	}

	// final reordering for real-to-complex transform
	#if defined(ATFFT_USE_NEON)
		ATFFT_DIT_R2C_NEON(dst, work, w, N);
	#elif defined(ATFFT_USE_SSE2)
		ATFFT_DIT_R2C_SSE2(dst, work, w, N);
	#else
		ATFFT_DIT_R2C_Scalar(dst, work, w, N);
	#endif
}

void ATFFTBase::InverseImpl(float *dst, const float *src, float *work, float *wtabend, int numRadix4, int numRadix2, bool useVec4, int *fwdorder, int log2N) {
	// initial complex-to-real conversion
	const int N = 1 << log2N;
	const float *w = wtabend - N;
	#ifdef ATFFT_USE_NEON
		ATFFT_DIF_C2R_NEON(work, src, w, N);
	#elif defined(ATFFT_USE_SSE2)
		ATFFT_DIF_C2R_SSE2(work, src, w, N);
	#else
		ATFFT_DIF_C2R_Scalar(work, src, w, N);
	#endif

	// decimation in frequency (DIF) loops

	// run radix-2 stages
	int lstep = log2N - 1;

	for(int i=0; i<numRadix2; ++i) {
		w -= ptrdiff_t(1) << lstep;

		#if defined(ATFFT_USE_SSE2)
			ATFFT_DIF_Radix2_SSE2(work, w, N, lstep);
		#elif defined(ATFFT_USE_NEON)
			ATFFT_DIF_Radix2_NEON(work, w, N, lstep);
		#else
			ATFFT_DIF_Radix2_Scalar(work, w, N, lstep);
		#endif
		
		--lstep;
	}

	// run radix-4 stages
	for(int i=0; i<numRadix4; ++i) {
		--lstep;
		w -= ptrdiff_t(3) << lstep;

		#if defined(ATFFT_USE_NEON)
			ATFFT_DIF_Radix4_NEON(work, w, N, lstep);
		#elif defined(ATFFT_USE_SSE2)
			ATFFT_DIF_Radix4_SSE2(work, w, N, lstep);
		#endif

		--lstep;
	}

	#ifdef ATFFT_USE_NEON
		ATFFT_DIF_Radix8_NEON(dst, work, fwdorder, N);
	#elif defined(ATFFT_USE_SSE2)
		ATFFT_DIF_Radix8_SSE2(dst, work, fwdorder, N);
	#else
		ATFFT_DIF_Radix8_Scalar(dst, work, fwdorder, N);
	#endif
}

void ATFFTBase::MultiplyAddImpl(float *dst, const float *src1, const float *src2, int N) {
	#if defined(ATFFT_USE_SSE2)
		ATFFT_MultiplyAdd_SSE2(dst, src1, src2, N);
	#else
		ATFFT_MultiplyAdd_Scalar(dst, src1, src2, N);
	#endif
}

