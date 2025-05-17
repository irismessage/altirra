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
#include <vd2/system/binary.h>

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
#include <intrin.h>

template<int T_LogStep>
VD_CPU_TARGET("avx2,fma")
void ATFFT_DIT_Radix2_AVX2(float *y0, const float *w4, int N) {
	constexpr int step = 1 << T_LogStep;
	static_assert(step >= 16);

	float *__restrict y1 = y0;
	const float *__restrict w1 = w4;

	__m256 two = _mm256_set1_ps(2.0f);

	for(int i=0; i<N; i += step*2) {
		const float *__restrict w2 = w1;

		for(int j=0; j<step/16; ++j) {
			__m256 r0 = _mm256_load_ps(y1);
			__m256 i0 = _mm256_load_ps(y1+8);
			__m256 r1 = _mm256_load_ps(y1+step);
			__m256 i1 = _mm256_load_ps(y1+step+8);
			__m256 rw = _mm256_load_ps(w2);
			__m256 iw = _mm256_load_ps(w2+8);

			__m256 r2 = _mm256_fmsub_ps(r1, rw, _mm256_fmsub_ps(i1, iw, r0));
			__m256 i2 = _mm256_fmadd_ps(r1, iw, _mm256_fmadd_ps(i1, rw, i0));
			__m256 r3 = _mm256_fmsub_ps(r0, two, r2);
			__m256 i3 = _mm256_fmsub_ps(i0, two, i2);

			_mm256_store_ps(y1       , r2);
			_mm256_store_ps(y1+8     , i2);
			_mm256_store_ps(y1+step  , r3);
			_mm256_store_ps(y1+step+8, i3);

			w2 += 16;
			y1 += 16;
		}

		y1 += step;
	}
}

VD_CPU_TARGET("avx2,fma")
void ATFFT_DIT_Radix4_4_AVX2(float *y0, const float *w4, int N) {
	constexpr int step = 16;
	constexpr int step2 = step*2;
	constexpr int step3 = step*3;

	const float *__restrict w1 = w4;
	float *__restrict y1 = y0;

	__m256 rw1 = _mm256_load_ps(w1+16);		// note permutation 1 <-> 2 on twiddle
	__m256 iw1 = _mm256_load_ps(w1+24);
	__m256 rw2 = _mm256_load_ps(w1);
	__m256 iw2 = _mm256_load_ps(w1+8);
	__m256 rw3 = _mm256_load_ps(w1+32);
	__m256 iw3 = _mm256_load_ps(w1+40);
	__m256 two = _mm256_set1_ps(2.0f);

	for(int i=0; i<N; i += step*4) {
		__m256 r1  = _mm256_load_ps(y1+step);
		__m256 i1  = _mm256_load_ps(y1+step+8);

		__m256 r0  = _mm256_load_ps(y1);
		__m256 i0  = _mm256_load_ps(y1+8);
		__m256 r4 = _mm256_fmsub_ps(r1, rw1, _mm256_fmsub_ps(i1, iw1, r0));
		__m256 i4 = _mm256_fmadd_ps(r1, iw1, _mm256_fmadd_ps(i1, rw1, i0));
		__m256 r5 = _mm256_fmsub_ps(r0, two, r4);
		__m256 i5 = _mm256_fmsub_ps(i0, two, i4);

		__m256 r2  = _mm256_load_ps(y1+step*2);
		__m256 i2  = _mm256_load_ps(y1+step*2+8);
		__m256 rt2 = _mm256_fmsub_ps(r2, rw2, _mm256_mul_ps(i2, iw2));
		__m256 it2 = _mm256_fmadd_ps(r2, iw2, _mm256_mul_ps(i2, rw2));

		__m256 r3  = _mm256_load_ps(y1+step*3);
		__m256 i3  = _mm256_load_ps(y1+step*3+8);
		__m256 r6 = _mm256_fmsub_ps(r3, rw3, _mm256_fmsub_ps(i3, iw3, rt2));
		__m256 i6 = _mm256_fmadd_ps(r3, iw3, _mm256_fmadd_ps(i3, rw3, it2));

		__m256 r7 = _mm256_fmsub_ps(rt2, two, r6);
		__m256 i7 = _mm256_fmsub_ps(it2, two, i6);

		__m256 r8 = _mm256_add_ps(r4, r6);
		__m256 i8 = _mm256_add_ps(i4, i6);
		__m256 rA = _mm256_sub_ps(r4, r6);
		__m256 iA = _mm256_sub_ps(i4, i6);

		__m256 r9 = _mm256_add_ps(r5, i7);
		__m256 i9 = _mm256_sub_ps(i5, r7);
		__m256 rB = _mm256_sub_ps(r5, i7);
		__m256 iB = _mm256_add_ps(i5, r7);

		_mm256_store_ps(y1        , r8);
		_mm256_store_ps(y1+8      , i8);
		_mm256_store_ps(y1+step   , r9);
		_mm256_store_ps(y1+step+8 , i9);
		_mm256_store_ps(y1+step2  , rA);
		_mm256_store_ps(y1+step2+8, iA);
		_mm256_store_ps(y1+step3  , rB);
		_mm256_store_ps(y1+step3+8, iB);

		y1 += 16;
		y1 += step*3;
	}
}

template<int T_LogStep>
VD_CPU_TARGET("avx2,fma")
void ATFFT_DIT_Radix4_AVX2(float *y0, const float *w4, int N) {
	constexpr int step = 1 << T_LogStep;
	static_assert(step >= 16);

	constexpr int step2 = step*2;
	constexpr int step3 = step*3;

	const float *__restrict w1 = w4;
	float *__restrict y1 = y0;
	
	__m256 two = _mm256_set1_ps(2.0f);

	for(int i=0; i<N; i += step*4) {
		const float *__restrict w2 = w1;

		for(int j=0; j<step/16; ++j) {
			__m256 r1  = _mm256_load_ps(y1+step);
			__m256 i1  = _mm256_load_ps(y1+step+8);
			__m256 rw1 = _mm256_load_ps(w2+16);		// note permutation 1 <-> 2 on twiddle
			__m256 iw1 = _mm256_load_ps(w2+24);

			__m256 r0  = _mm256_load_ps(y1);
			__m256 i0  = _mm256_load_ps(y1+8);
			__m256 r4 = _mm256_fmsub_ps(r1, rw1, _mm256_fmsub_ps(i1, iw1, r0));
			__m256 i4 = _mm256_fmadd_ps(r1, iw1, _mm256_fmadd_ps(i1, rw1, i0));
			__m256 r5 = _mm256_fmsub_ps(r0, two, r4);
			__m256 i5 = _mm256_fmsub_ps(i0, two, i4);

			__m256 r2  = _mm256_load_ps(y1+step*2);
			__m256 i2  = _mm256_load_ps(y1+step*2+8);
			__m256 rw2 = _mm256_load_ps(w2);
			__m256 iw2 = _mm256_load_ps(w2+8);
			__m256 rt2 = _mm256_fmsub_ps(r2, rw2, _mm256_mul_ps(i2, iw2));
			__m256 it2 = _mm256_fmadd_ps(r2, iw2, _mm256_mul_ps(i2, rw2));

			__m256 r3  = _mm256_load_ps(y1+step*3);
			__m256 i3  = _mm256_load_ps(y1+step*3+8);
			__m256 rw3 = _mm256_load_ps(w2+32);
			__m256 iw3 = _mm256_load_ps(w2+40);			

			__m256 r6 = _mm256_fmsub_ps(r3, rw3, _mm256_fmsub_ps(i3, iw3, rt2));
			__m256 i6 = _mm256_fmadd_ps(r3, iw3, _mm256_fmadd_ps(i3, rw3, it2));
			__m256 r7 = _mm256_fmsub_ps(rt2, two, r6);
			__m256 i7 = _mm256_fmsub_ps(it2, two, i6);

			__m256 r8 = _mm256_add_ps(r4, r6);
			__m256 i8 = _mm256_add_ps(i4, i6);
			__m256 rA = _mm256_sub_ps(r4, r6);
			__m256 iA = _mm256_sub_ps(i4, i6);

			__m256 r9 = _mm256_add_ps(r5, i7);
			__m256 i9 = _mm256_sub_ps(i5, r7);
			__m256 rB = _mm256_sub_ps(r5, i7);
			__m256 iB = _mm256_add_ps(i5, r7);

			_mm256_store_ps(y1        , r8);
			_mm256_store_ps(y1+8      , i8);
			_mm256_store_ps(y1+step   , r9);
			_mm256_store_ps(y1+step+8 , i9);
			_mm256_store_ps(y1+step2  , rA);
			_mm256_store_ps(y1+step2+8, iA);
			_mm256_store_ps(y1+step3  , rB);
			_mm256_store_ps(y1+step3+8, iB);

			w2 += 48;
			y1 += 16;
		}

		y1 += step*3;
	}
}

VD_CPU_TARGET("avx2,fma")
void ATFFT_DIT_Radix8_AVX2(float *dst0, const float *y0, const uint32 *order0, int N) {
	float *__restrict dst = dst0;
	const char *__restrict y = (const char *)y0;
	const uint32 *__restrict order = order0;

	const ptrdiff_t step0 = (N/8) * 4;

	// sqrt(2)/2
	constexpr float r2d2 = 0.70710678118654752440084436210485f;

	__m256 sign_0101 = _mm256_castsi256_ps(_mm256_set_epi32(0x80000000, 0, 0x80000000, 0, 0x80000000, 0, 0x80000000, 0));

	__m256 twiddle_c = _mm256_set_ps(-r2d2,  0,  r2d2, 1, -r2d2,  0,  r2d2, 1);
	__m256 twiddle_s = _mm256_set_ps(-r2d2, -1, -r2d2, 0, -r2d2, -1, -r2d2, 0);
	__m256 two = _mm256_set1_ps(2.0f);

	for(int i=0; i<N/32; ++i) {
		__m256 x04 = _mm256_loadu2_m128((const float *)(y + 1 * step0), (const float *)(y + 0 * step0));
		__m256 x26 = _mm256_loadu2_m128((const float *)(y + 3 * step0), (const float *)(y + 2 * step0));
		__m256 x15 = _mm256_loadu2_m128((const float *)(y + 5 * step0), (const float *)(y + 4 * step0));
		__m256 x37 = _mm256_loadu2_m128((const float *)(y + 7 * step0), (const float *)(y + 6 * step0));

		x04 = _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(x04), _MM_SHUFFLE(3, 1, 2, 0)));
		x26 = _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(x26), _MM_SHUFFLE(3, 1, 2, 0)));
		x15 = _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(x15), _MM_SHUFFLE(3, 1, 2, 0)));
		x37 = _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(x37), _MM_SHUFFLE(3, 1, 2, 0)));
		y += 16;

		// first butterfly
		__m256 a_r0i0r4i4 = _mm256_add_ps(x04, x15);
		__m256 a_r1i1r5i5 = _mm256_sub_ps(x04, x15);
		__m256 a_r2i2r6i6 = _mm256_add_ps(x26, x37);
		__m256 a_r3i3r7i7 = _mm256_sub_ps(x26, x37);

		// second butterfly (elements 3/7 rotated by -j)
		__m256 at_r3i3r7i7 = _mm256_xor_ps(_mm256_shuffle_ps(a_r3i3r7i7, a_r3i3r7i7, _MM_SHUFFLE(2, 3, 0, 1)), sign_0101);

		__m256 b_r0i0r4i4 = _mm256_add_ps(a_r0i0r4i4, a_r2i2r6i6);
		__m256 b_r1i1r5i5 = _mm256_add_ps(a_r1i1r5i5, at_r3i3r7i7);
		__m256 b_r2i2r6i6 = _mm256_sub_ps(a_r0i0r4i4, a_r2i2r6i6);
		__m256 b_r3i3r7i7 = _mm256_sub_ps(a_r1i1r5i5, at_r3i3r7i7);

		// transpose
		__m256 b_r0i0r1i1 = _mm256_shuffle_ps(b_r0i0r4i4, b_r1i1r5i5, _MM_SHUFFLE(1, 0, 1, 0));
		__m256 b_r2i2r3i3 = _mm256_shuffle_ps(b_r2i2r6i6, b_r3i3r7i7, _MM_SHUFFLE(1, 0, 1, 0));
		__m256 b_r4i4r5i5 = _mm256_shuffle_ps(b_r0i0r4i4, b_r1i1r5i5, _MM_SHUFFLE(3, 2, 3, 2));
		__m256 b_r6i6r7i7 = _mm256_shuffle_ps(b_r2i2r6i6, b_r3i3r7i7, _MM_SHUFFLE(3, 2, 3, 2));

		__m256 b_r0r1r2r3 = _mm256_shuffle_ps(b_r0i0r1i1, b_r2i2r3i3, _MM_SHUFFLE(2, 0, 2, 0));
		__m256 b_i0i1i2i3 = _mm256_shuffle_ps(b_r0i0r1i1, b_r2i2r3i3, _MM_SHUFFLE(3, 1, 3, 1));
		__m256 b_r4r5r6r7 = _mm256_shuffle_ps(b_r4i4r5i5, b_r6i6r7i7, _MM_SHUFFLE(2, 0, 2, 0));
		__m256 b_i4i5i6i7 = _mm256_shuffle_ps(b_r4i4r5i5, b_r6i6r7i7, _MM_SHUFFLE(3, 1, 3, 1));

		// third butterfly (elements 4-7 rotated by w0-w3)
		__m256 c_r0r1r2r3 = _mm256_fmsub_ps(b_r4r5r6r7, twiddle_c, _mm256_fmsub_ps(b_i4i5i6i7, twiddle_s, b_r0r1r2r3));
		__m256 c_i0i1i2i3 = _mm256_fmadd_ps(b_r4r5r6r7, twiddle_s, _mm256_fmadd_ps(b_i4i5i6i7, twiddle_c, b_i0i1i2i3));
		__m256 c_r4r5r6r7 = _mm256_fmsub_ps(b_r0r1r2r3, two, c_r0r1r2r3);
		__m256 c_i4i5i6i7 = _mm256_fmsub_ps(b_i0i1i2i3, two, c_i0i1i2i3);

		float *__restrict dst2 = &dst[*order++];
		_mm256_store_ps(&dst2[ 0], _mm256_permute2f128_ps(c_r0r1r2r3, c_r4r5r6r7, 0x20));
		_mm256_store_ps(&dst2[ 8], _mm256_permute2f128_ps(c_i0i1i2i3, c_i4i5i6i7, 0x20));

		dst2 = &dst[*order++];
		_mm256_store_ps(&dst2[ 0], _mm256_permute2f128_ps(c_r0r1r2r3, c_r4r5r6r7, 0x31));
		_mm256_store_ps(&dst2[ 8], _mm256_permute2f128_ps(c_i0i1i2i3, c_i4i5i6i7, 0x31));
	}
}

VD_CPU_TARGET("avx2,fma")
void ATFFT_DIT_R2C_AVX2(float *dst0, const float *src0, const float *w, int N) {
	const float *__restrict w2 = w;
	const float *__restrict src = src0;
	float *__restrict dst = dst0;

	__m256 half = _mm256_set1_ps(0.5f);

	__m256 prevr1 = _mm256_setzero_ps();
	__m256 previ1 = _mm256_setzero_ps();

	{
		__m256 rl = _mm256_load_ps(&src[0]);
		__m256 il = _mm256_load_ps(&src[8]);
		__m256 rh = _mm256_castsi256_ps(_mm256_insert_epi32(_mm256_load_si256((const __m256i *)&src[N-15]), 0, 7));
		__m256 ih = _mm256_castsi256_ps(_mm256_insert_epi32(_mm256_load_si256((const __m256i *)&src[N-7]), 0, 7));

		// reverse high vector
		rh = _mm256_shuffle_ps(rh, rh, _MM_SHUFFLE(0, 1, 2, 3));
		ih = _mm256_shuffle_ps(ih, ih, _MM_SHUFFLE(0, 1, 2, 3));
		rh = _mm256_permute2f128_ps(rh, rh, 0x01);
		ih = _mm256_permute2f128_ps(ih, ih, 0x01);

		// Xe[n] = (Z[k] + Z*[N-k])/2
		__m256 rxe = _mm256_mul_ps(_mm256_add_ps(rl, rh), half);
		__m256 ixe = _mm256_mul_ps(_mm256_sub_ps(il, ih), half);

		// Xo[n] = -j(Z[k] - Z*[N-k])/2 * w[k]
		__m256 rxo = _mm256_add_ps(il, ih);
		__m256 ixo = _mm256_sub_ps(rh, rl);

		__m256 rw = _mm256_load_ps(w2);
		__m256 iw = _mm256_load_ps(w2+8);
		w2 += 16;

		__m256 rxo2 = _mm256_fmsub_ps(rxo, rw, _mm256_mul_ps(ixo, iw));
		__m256 ixo2 = _mm256_fmadd_ps(rxo, iw, _mm256_mul_ps(ixo, rw));

		// final butterfly and conjugation
		__m256 r0 = _mm256_add_ps(rxe, rxo2);
		__m256 i0 = _mm256_add_ps(ixe, ixo2);
		__m256 r1 = _mm256_sub_ps(rxe, rxo2);
		__m256 i1 = _mm256_sub_ps(ixo2, ixe);

		// interleave and store low
		__m256 lo0 = _mm256_unpacklo_ps(r0, i0);
		__m256 lo1 = _mm256_unpackhi_ps(r0, i0);

		_mm256_storeu_ps(&dst[0], _mm256_permute2f128_ps(lo0, lo1, 0x20));
		_mm256_storeu_ps(&dst[8], _mm256_permute2f128_ps(lo0, lo1, 0x31));

		// interleave, reverse, and store high
		r1 = _mm256_shuffle_ps(r1, r1, _MM_SHUFFLE(0, 1, 2, 3));
		i1 = _mm256_shuffle_ps(i1, i1, _MM_SHUFFLE(0, 1, 2, 3));

		__m256 hi0 = _mm256_unpacklo_ps(r1, i1);
		__m256 hi1 = _mm256_unpackhi_ps(r1, i1);

		_mm_storeu_ps(&dst[N-14], _mm256_extractf128_ps(hi0, 1));
		_mm_storeu_ps(&dst[N-10], _mm256_extractf128_ps(hi1, 1));

		__m128 last0 = _mm256_extractf128_ps(hi0, 0);
		__m128 last1 = _mm256_extractf128_ps(hi1, 0);

		_mm_storeu_ps(&dst[N-6], last0);
		_mm_storel_pi((__m64 *)&dst[N-2], last1);
	}

	__m256 two = _mm256_set1_ps(2.0f);

	for(int i=16; i<N/2; i += 16) {
		__m256 rl = _mm256_load_ps(&src[i]);
		__m256 il = _mm256_load_ps(&src[i+8]);
		__m256 rh = _mm256_castsi256_ps(_mm256_insert_epi32(_mm256_load_si256((const __m256i *)&src[N-i-15]), *(int *)&src[N-i], 7));
		__m256 ih = _mm256_castsi256_ps(_mm256_insert_epi32(_mm256_load_si256((const __m256i *)&src[N-i-7]), *(int *)&src[N-i+8], 7));

		// reverse high vector
		rh = _mm256_shuffle_ps(rh, rh, _MM_SHUFFLE(0, 1, 2, 3));
		ih = _mm256_shuffle_ps(ih, ih, _MM_SHUFFLE(0, 1, 2, 3));
		rh = _mm256_permute2f128_ps(rh, rh, 0x01);
		ih = _mm256_permute2f128_ps(ih, ih, 0x01);

		// Xe[n] = (Z[k] + Z*[N-k])/2
		__m256 rxe = _mm256_mul_ps(_mm256_add_ps(rl, rh), half);
		__m256 ixe = _mm256_mul_ps(_mm256_sub_ps(il, ih), half);

		// Xo[n] = -j(Z[k] - Z*[N-k])/2 * w[k]
		__m256 rxo = _mm256_add_ps(il, ih);
		__m256 ixo = _mm256_sub_ps(rh, rl);

		__m256 rw = _mm256_load_ps(w2);
		__m256 iw = _mm256_load_ps(w2+8);
		w2 += 16;

		// final butterfly and conjugation
		__m256 r0 = _mm256_fmsub_ps(rxo, rw, _mm256_fmsub_ps(ixo, iw, rxe));
		__m256 i0 = _mm256_fmadd_ps(rxo, iw, _mm256_fmadd_ps(ixo, rw, ixe));
		__m256 r1 = _mm256_fmsub_ps(rxe, two, r0);
		__m256 i1 = _mm256_fnmadd_ps(ixe, two, i0);

		// interleave and store low
		__m256 lo0 = _mm256_unpacklo_ps(r0, i0);
		__m256 lo1 = _mm256_unpackhi_ps(r0, i0);

		_mm256_storeu_ps(&dst[i+0], _mm256_permute2f128_ps(lo0, lo1, 0x20));
		_mm256_storeu_ps(&dst[i+8], _mm256_permute2f128_ps(lo0, lo1, 0x31));

		// interleave, reverse, and store high
		r1 = _mm256_shuffle_ps(r1, r1, _MM_SHUFFLE(0, 1, 2, 3));
		i1 = _mm256_shuffle_ps(i1, i1, _MM_SHUFFLE(0, 1, 2, 3));

		__m256 hi0 = _mm256_unpacklo_ps(r1, i1);
		__m256 hi1 = _mm256_unpackhi_ps(r1, i1);

		_mm_storeu_ps(&dst[N-i-14], _mm256_extractf128_ps(hi0, 1));
		_mm_storeu_ps(&dst[N-i-10], _mm256_extractf128_ps(hi1, 1));
		_mm_storeu_ps(&dst[N-i-6], _mm256_castps256_ps128(hi0));
		_mm_storeu_ps(&dst[N-i-2], _mm256_castps256_ps128(hi1));
	}

	// correct DC and Nyquist terms
	float rz = src[0];
	float iz = src[8];
	dst[0] = rz + iz;
	dst[1] = rz - iz;

	dst[N/2] = src[N/2];
	dst[N/2+1] = -src[N/2+8];
}

VD_CPU_TARGET("avx2,fma")
void ATFFT_DIF_C2R_AVX2(float *dst0, const float *x, const float *w, int N) {
	float *__restrict dst = dst0;
	
	const __m256 half = _mm256_set1_ps(0.5f);
	const __m256 nhalf = _mm256_set1_ps(-0.5f);
	const __m256 sign = _mm256_castsi256_ps(_mm256_set1_epi32(0x80000000));

	__m256 prevr1;
	__m256 previ1;
	__m256 two = _mm256_set1_ps(2.0f);

	{
		__m256 a01 = _mm256_loadu_ps(&x[0]);
		__m256 a23 = _mm256_loadu_ps(&x[8]);
		__m256 a54 = _mm256_castps128_ps256(_mm_loadu_ps(&x[N-6]));
		a54 = _mm256_insertf128_ps(a54, _mm_castsi128_ps(_mm_loadu_si64(&x[N-2])), 1);
		__m256 a76 = _mm256_loadu_ps(&x[N-14]);

		// deinterleave and reverse high half
		__m256 a02 = _mm256_permute2f128_ps(a01, a23, 0x20);
		__m256 a13 = _mm256_permute2f128_ps(a01, a23, 0x31);
		__m256 rl = _mm256_shuffle_ps(a02, a13, _MM_SHUFFLE(2, 0, 2, 0));
		__m256 il = _mm256_shuffle_ps(a02, a13, _MM_SHUFFLE(3, 1, 3, 1));

		__m256 a46 = _mm256_permute2f128_ps(a54, a76, 0x31);
		__m256 a57 = _mm256_permute2f128_ps(a54, a76, 0x20);
		__m256 rh = _mm256_shuffle_ps(a46, a57, _MM_SHUFFLE(0, 2, 0, 2));
		__m256 ih = _mm256_shuffle_ps(a46, a57, _MM_SHUFFLE(1, 3, 1, 3));

		// Xe[n] = (X[k] + X*[N/2-k])/2
		__m256 rxe = _mm256_mul_ps(_mm256_add_ps(rl, rh), half);
		__m256 ixe = _mm256_mul_ps(_mm256_sub_ps(il, ih), half);

		// Xo[n] = j(X[k] - X*[N/2-k])/2 * w[k]
		__m256 rxo = _mm256_add_ps(il, ih);
		__m256 ixo = _mm256_sub_ps(rl, rh);

		__m256 rw = _mm256_load_ps(w);
		__m256 iw = _mm256_load_ps(w+8);
		w += 16;

		__m256 r0 = _mm256_fmsub_ps(ixo, iw, _mm256_fmsub_ps(rxo, rw, rxe));
		__m256 i0 = _mm256_fmadd_ps(ixo, rw, _mm256_fmadd_ps(rxo, iw, ixe));

		__m256 r1 = _mm256_fmsub_ps(rxe, two, r0);
		__m256 i1 = _mm256_fnmadd_ps(ixe, two, i0);		// - for conj

		_mm256_store_ps(&dst[0], r0);
		_mm256_store_ps(&dst[8], i0);

		// re-reverse high half and rotate it for the one element shift
		r1 = _mm256_shuffle_ps(r1, r1, _MM_SHUFFLE(0, 1, 2, 3));
		i1 = _mm256_shuffle_ps(i1, i1, _MM_SHUFFLE(0, 1, 2, 3));

		__m256 r1x = _mm256_permute2f128_ps(r1, r1, 0x01);
		__m256 i1x = _mm256_permute2f128_ps(i1, i1, 0x01);

		prevr1 = _mm256_castsi256_ps(_mm256_alignr_epi8(_mm256_castps_si256(r1x), _mm256_castps_si256(r1), 12));
		previ1 = _mm256_castsi256_ps(_mm256_alignr_epi8(_mm256_castps_si256(i1x), _mm256_castps_si256(i1), 12));
	}

	for(int i=16; i<N/2; i += 16) {
		__m256 a01 = _mm256_loadu_ps(&x[i]);
		__m256 a23 = _mm256_loadu_ps(&x[i+8]);
		__m256 a54 = _mm256_loadu_ps(&x[N-i-6]);
		__m256 a76 = _mm256_loadu_ps(&x[N-i-14]);

		// deinterleave and reverse high half
		__m256 a02 = _mm256_permute2f128_ps(a01, a23, 0x20);
		__m256 a13 = _mm256_permute2f128_ps(a01, a23, 0x31);
		__m256 rl = _mm256_shuffle_ps(a02, a13, _MM_SHUFFLE(2, 0, 2, 0));
		__m256 il = _mm256_shuffle_ps(a02, a13, _MM_SHUFFLE(3, 1, 3, 1));

		__m256 a46 = _mm256_permute2f128_ps(a54, a76, 0x31);
		__m256 a57 = _mm256_permute2f128_ps(a54, a76, 0x20);
		__m256 rh = _mm256_shuffle_ps(a46, a57, _MM_SHUFFLE(0, 2, 0, 2));
		__m256 ih = _mm256_shuffle_ps(a46, a57, _MM_SHUFFLE(1, 3, 1, 3));

		// Xe[n] = (X[k] + X*[N/2-k])/2
		__m256 rxe = _mm256_mul_ps(_mm256_add_ps(rl, rh), half);
		__m256 ixe = _mm256_mul_ps(_mm256_sub_ps(il, ih), half);

		// Xo[n] = j(X[k] - X*[N/2-k])/2 * w[k]
		__m256 rxo = _mm256_add_ps(il, ih);
		__m256 ixo = _mm256_sub_ps(rl, rh);

		__m256 rw = _mm256_load_ps(w);
		__m256 iw = _mm256_load_ps(w+8);
		w += 16;

		__m256 r0 = _mm256_fmsub_ps(ixo, iw, _mm256_fmsub_ps(rxo, rw, rxe));
		__m256 i0 = _mm256_fmadd_ps(ixo, rw, _mm256_fmadd_ps(rxo, iw, ixe));

		__m256 r1 = _mm256_fmsub_ps(rxe, two, r0);
		__m256 i1 = _mm256_fnmadd_ps(ixe, two, i0);		// - for conj

		_mm256_store_ps(&dst[i], r0);
		_mm256_store_ps(&dst[i+8], i0);

		// re-reverse high half and rotate it for the one element shift
		r1 = _mm256_shuffle_ps(r1, r1, _MM_SHUFFLE(0, 1, 2, 3));
		i1 = _mm256_shuffle_ps(i1, i1, _MM_SHUFFLE(0, 1, 2, 3));

		__m256 r1x = _mm256_permute2f128_ps(r1, r1, 0x01);
		__m256 i1x = _mm256_permute2f128_ps(i1, i1, 0x01);

		r1 = _mm256_castsi256_ps(_mm256_alignr_epi8(_mm256_castps_si256(r1x), _mm256_castps_si256(r1), 12));
		i1 = _mm256_castsi256_ps(_mm256_alignr_epi8(_mm256_castps_si256(i1x), _mm256_castps_si256(i1), 12));

		_mm256_store_ps(&dst[N-i], _mm256_blend_ps(prevr1, r1, 1));
		_mm256_store_ps(&dst[N-i+8], _mm256_blend_ps(previ1, i1, 1));

		prevr1 = r1;
		previ1 = i1;
	}

	_mm256_store_ps(&dst[N/2], _mm256_blend_ps(prevr1, _mm256_broadcast_ss(&x[N/2]), 1));
	_mm256_store_ps(&dst[N/2+8], _mm256_blend_ps(previ1, _mm256_xor_ps(_mm256_broadcast_ss(&x[N/2+1]), sign), 1));

	// patch DC and Nyquist values
	dst[0] = (x[0] + x[1]) * 0.5f;
	dst[8] = (x[0] - x[1]) * 0.5f;
}


template<int T_LogStep>
VD_CPU_TARGET("avx2,fma")
void ATFFT_DIF_Radix2_AVX2(float *y0, const float *w, int N) {
	constexpr int step = 1 << T_LogStep;
	static_assert(step >= 16);

	float *__restrict y1 = y0;

	for(int i=0; i<N; i += step*2) {
		const float *__restrict w2 = w;

		for(int j=0; j<step/16; ++j) {
			__m256 r0 = _mm256_load_ps(y1);
			__m256 i0 = _mm256_load_ps(y1+8);
			__m256 r1 = _mm256_load_ps(y1+step);
			__m256 i1 = _mm256_load_ps(y1+step+8);
			__m256 rw = _mm256_load_ps(w2);
			__m256 iw = _mm256_load_ps(w2+8);

			__m256 r2 = _mm256_add_ps(r0, r1);
			__m256 i2 = _mm256_add_ps(i0, i1);
			__m256 r3 = _mm256_sub_ps(r0, r1);
			__m256 i3 = _mm256_sub_ps(i0, i1);

			__m256 rt3 = _mm256_fmadd_ps(r3, rw, _mm256_mul_ps(i3, iw));
			__m256 it3 = _mm256_fmsub_ps(i3, rw, _mm256_mul_ps(r3, iw));

			_mm256_store_ps(y1       , r2);
			_mm256_store_ps(y1+8     , i2);
			_mm256_store_ps(y1+step  , rt3);
			_mm256_store_ps(y1+step+8, it3);

			w2 += 16;
			y1 += 16;
		}

		y1 += step;
	}
}

template<int T_LogStep>
VD_CPU_TARGET("avx2,fma")
void ATFFT_DIF_Radix4_AVX2(float *y0, const float *w4, int N) {
	constexpr int step = 1 << T_LogStep;
	static_assert(step >= 16);

	constexpr int step2 = step*2;
	constexpr int step3 = step*3;

	const float *__restrict w1 = w4;

	for(int i=0; i<N; i += step*4) {
		const float *__restrict w2 = w1;

		float *__restrict y1 = y0;

		y0 += step*4;

		for(int j=0; j<step/16; ++j) {
			__m256 r0  = _mm256_load_ps(y1);			__m256 i0  = _mm256_load_ps(y1+8);
			__m256 r1  = _mm256_load_ps(y1+step);		__m256 i1  = _mm256_load_ps(y1+step+8);
			__m256 r2  = _mm256_load_ps(y1+step2);		__m256 i2  = _mm256_load_ps(y1+step2+8);
			__m256 r3  = _mm256_load_ps(y1+step3);		__m256 i3  = _mm256_load_ps(y1+step3+8);

			__m256 r4 = _mm256_add_ps(r0, r2);			__m256 i4 = _mm256_add_ps(i0, i2);
			__m256 r5 = _mm256_add_ps(r1, r3);			__m256 i5 = _mm256_add_ps(i1, i3);
			__m256 r6 = _mm256_sub_ps(r0, r2);			__m256 i6 = _mm256_sub_ps(i0, i2);
			// *j
			__m256 r7 = _mm256_sub_ps(i3, i1);			__m256 i7 = _mm256_sub_ps(r1, r3);

			__m256 r8 = _mm256_add_ps(r4, r5);			__m256 i8 = _mm256_add_ps(i4, i5);
			__m256 r9 = _mm256_sub_ps(r4, r5);			__m256 i9 = _mm256_sub_ps(i4, i5);
			__m256 rA = _mm256_add_ps(r6, r7);			__m256 iA = _mm256_add_ps(i6, i7);
			__m256 rB = _mm256_sub_ps(r6, r7);			__m256 iB = _mm256_sub_ps(i6, i7);

			__m256 rw1 = _mm256_load_ps(w2+16);		// note permutation 1 <-> 2 on twiddle
			__m256 iw1 = _mm256_load_ps(w2+24);
			__m256 rt9 = _mm256_fmadd_ps(r9, rw1, _mm256_mul_ps(i9, iw1));
			__m256 it9 = _mm256_fmsub_ps(i9, rw1, _mm256_mul_ps(r9, iw1));

			__m256 rw2 = _mm256_load_ps(w2);
			__m256 iw2 = _mm256_load_ps(w2+8);
			__m256 rtA = _mm256_fmadd_ps(rA, rw2, _mm256_mul_ps(iA, iw2));
			__m256 itA = _mm256_fmsub_ps(iA, rw2, _mm256_mul_ps(rA, iw2));

			__m256 rw3 = _mm256_load_ps(w2+32);
			__m256 iw3 = _mm256_load_ps(w2+40);			
			__m256 rtB = _mm256_fmadd_ps(rB, rw3, _mm256_mul_ps(iB, iw3));
			__m256 itB = _mm256_fmsub_ps(iB, rw3, _mm256_mul_ps(rB, iw3));

			_mm256_store_ps(y1        , r8);
			_mm256_store_ps(y1+8      , i8);
			_mm256_store_ps(y1+step   , rt9);
			_mm256_store_ps(y1+step+8 , it9);
			_mm256_store_ps(y1+step2  , rtA);
			_mm256_store_ps(y1+step2+8, itA);
			_mm256_store_ps(y1+step3  , rtB);
			_mm256_store_ps(y1+step3+8, itB);

			w2 += 48;
			y1 += 16;
		}
	}
}

VD_CPU_TARGET("avx2,fma")
void ATFFT_DIF_Radix8_AVX2(float *dst0, const float *src0, const uint32 *order0, int N) {
	constexpr float r2d2 = 0.70710678118654752440084436210485f;

	const uint32 *__restrict order = order0;
	char *__restrict dst = (char *)dst0;
	const float *__restrict src = src0;

	__m256 twiddle_c = _mm256_set_ps(-r2d2,  0,  r2d2, 1, -r2d2,  0,  r2d2, 1);
	__m256 twiddle_s = _mm256_set_ps(-r2d2, -1, -r2d2, 0, -r2d2, -1, -r2d2, 0);
	__m256 sign_1010 = _mm256_castsi256_ps(_mm256_set_epi32(0, 0x80000000, 0, 0x80000000, 0, 0x80000000, 0, 0x80000000));
	const ptrdiff_t step = (N/8) * 4;

	for(int i=0; i<N/32; ++i) {
		const float *__restrict src2 = &src[*order++];
		__m256 a_r0r1r2r3 = _mm256_castps128_ps256(_mm_load_ps(src2 +  0));
		__m256 a_r4r5r6r7 = _mm256_castps128_ps256(_mm_load_ps(src2 +  4));
		__m256 a_i0i1i2i3 = _mm256_castps128_ps256(_mm_load_ps(src2 +  8));
		__m256 a_i4i5i6i7 = _mm256_castps128_ps256(_mm_load_ps(src2 + 12));

		src2 = &src[*order++];
		a_r0r1r2r3 = _mm256_insertf128_ps(a_r0r1r2r3, _mm_load_ps(src2 +  0), 1);
		a_r4r5r6r7 = _mm256_insertf128_ps(a_r4r5r6r7, _mm_load_ps(src2 +  4), 1);
		a_i0i1i2i3 = _mm256_insertf128_ps(a_i0i1i2i3, _mm_load_ps(src2 +  8), 1);
		a_i4i5i6i7 = _mm256_insertf128_ps(a_i4i5i6i7, _mm_load_ps(src2 + 12), 1);

		// DFT-8 stage
		__m256 b_r0r1r2r3 = _mm256_add_ps(a_r0r1r2r3, a_r4r5r6r7);
		__m256 b_i0i1i2i3 = _mm256_add_ps(a_i0i1i2i3, a_i4i5i6i7);
		__m256 b_r4r5r6r7 = _mm256_sub_ps(a_r0r1r2r3, a_r4r5r6r7);
		__m256 b_i4i5i6i7 = _mm256_sub_ps(a_i0i1i2i3, a_i4i5i6i7);

		// *w(1,8) = (1+j)*sqrt(2)/2
		// *w(2,8) = j
		// *w(3,8) = (-1+j)*sqrt(2)/2
		__m256 bt_r4r5r6r7 = _mm256_fmadd_ps(b_r4r5r6r7, twiddle_c, _mm256_mul_ps(b_i4i5i6i7, twiddle_s));
		__m256 bt_i4i5i6i7 = _mm256_fmsub_ps(b_i4i5i6i7, twiddle_c, _mm256_mul_ps(b_r4r5r6r7, twiddle_s));

		// transpose
		__m256 c_r0i0r1i1 = _mm256_unpacklo_ps(b_r0r1r2r3, b_i0i1i2i3);
		__m256 c_r2i2r3i3 = _mm256_unpackhi_ps(b_r0r1r2r3, b_i0i1i2i3);
		__m256 c_r4i4r5i5 = _mm256_unpacklo_ps(bt_r4r5r6r7, bt_i4i5i6i7);
		__m256 c_r6i6r7i7 = _mm256_unpackhi_ps(bt_r4r5r6r7, bt_i4i5i6i7);

		__m256 c_r0i0r4i4 = _mm256_shuffle_ps(c_r0i0r1i1, c_r4i4r5i5, _MM_SHUFFLE(1, 0, 1, 0));
		__m256 c_r1i1r5i5 = _mm256_shuffle_ps(c_r0i0r1i1, c_r4i4r5i5, _MM_SHUFFLE(3, 2, 3, 2));
		__m256 c_r2i2r6i6 = _mm256_shuffle_ps(c_r2i2r3i3, c_r6i6r7i7, _MM_SHUFFLE(1, 0, 1, 0));
		__m256 c_r3i3r7i7 = _mm256_shuffle_ps(c_r2i2r3i3, c_r6i6r7i7, _MM_SHUFFLE(3, 2, 3, 2));

		// DFT-4 stage
		__m256 d_r0i0r4i4 = _mm256_add_ps(c_r0i0r4i4, c_r2i2r6i6);
		__m256 d_r1i1r5i5 = _mm256_add_ps(c_r1i1r5i5, c_r3i3r7i7);
		__m256 d_r2i2r6i6 = _mm256_sub_ps(c_r0i0r4i4, c_r2i2r6i6);
		__m256 d_r3i3r7i7 = _mm256_sub_ps(c_r1i1r5i5, c_r3i3r7i7);

		// rotate elements 3 and 7 by +j
		d_r3i3r7i7 = _mm256_xor_ps(_mm256_shuffle_ps(d_r3i3r7i7, d_r3i3r7i7, _MM_SHUFFLE(2, 3, 0, 1)), sign_1010);

		// DFT-2 stage
		__m256 e_r0i0r4i4 = _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(_mm256_add_ps(d_r0i0r4i4, d_r1i1r5i5)), _MM_SHUFFLE(3,1,2,0)));
		__m256 e_r1i1r5i5 = _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(_mm256_sub_ps(d_r0i0r4i4, d_r1i1r5i5)), _MM_SHUFFLE(3,1,2,0)));
		__m256 e_r2i2r6i6 = _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(_mm256_add_ps(d_r2i2r6i6, d_r3i3r7i7)), _MM_SHUFFLE(3,1,2,0)));
		__m256 e_r3i3r7i7 = _mm256_castpd_ps(_mm256_permute4x64_pd(_mm256_castps_pd(_mm256_sub_ps(d_r2i2r6i6, d_r3i3r7i7)), _MM_SHUFFLE(3,1,2,0)));

		_mm_store_ps((float *)(dst + step*0), _mm256_castps256_ps128(e_r0i0r4i4));
		_mm_store_ps((float *)(dst + step*1), _mm256_extractf128_ps(e_r0i0r4i4, 1));
		_mm_store_ps((float *)(dst + step*2), _mm256_castps256_ps128(e_r2i2r6i6));
		_mm_store_ps((float *)(dst + step*3), _mm256_extractf128_ps(e_r2i2r6i6, 1));
		_mm_store_ps((float *)(dst + step*4), _mm256_castps256_ps128(e_r1i1r5i5));
		_mm_store_ps((float *)(dst + step*5), _mm256_extractf128_ps(e_r1i1r5i5, 1));
		_mm_store_ps((float *)(dst + step*6), _mm256_castps256_ps128(e_r3i3r7i7));
		_mm_store_ps((float *)(dst + step*7), _mm256_extractf128_ps(e_r3i3r7i7, 1));

		dst += 16;
	}
}

/////////////////////////////////////////////////////////////////////////////

void ATFFT_DIT_Radix2_AVX2(float *y0, const float *w4, int N, int logStep) {
	switch(logStep) {
		case  4: ATFFT_DIT_Radix2_AVX2< 4>(y0, w4, N); break;
		case  5: ATFFT_DIT_Radix2_AVX2< 5>(y0, w4, N); break;
		case  6: ATFFT_DIT_Radix2_AVX2< 6>(y0, w4, N); break;
		case  7: ATFFT_DIT_Radix2_AVX2< 7>(y0, w4, N); break;
		case  8: ATFFT_DIT_Radix2_AVX2< 8>(y0, w4, N); break;
		case  9: ATFFT_DIT_Radix2_AVX2< 9>(y0, w4, N); break;
		case 10: ATFFT_DIT_Radix2_AVX2<10>(y0, w4, N); break;
		case 11: ATFFT_DIT_Radix2_AVX2<11>(y0, w4, N); break;
		case 12: ATFFT_DIT_Radix2_AVX2<12>(y0, w4, N); break;
	}
}

void ATFFT_DIT_Radix4_AVX2(float *y0, const float *w4, int N, int logStep) {
	switch(logStep) {
		case  4: ATFFT_DIT_Radix4_4_AVX2(y0, w4, N); break;
		case  6: ATFFT_DIT_Radix4_AVX2< 6>(y0, w4, N); break;
		case  8: ATFFT_DIT_Radix4_AVX2< 8>(y0, w4, N); break;
		case 10: ATFFT_DIT_Radix4_AVX2<10>(y0, w4, N); break;
		case 12: ATFFT_DIT_Radix4_AVX2<12>(y0, w4, N); break;
	}
}

void ATFFT_DIF_Radix2_AVX2(float *y0, const float *w4, int N, int logStep) {
	switch(logStep) {
		case  4: ATFFT_DIF_Radix2_AVX2< 4>(y0, w4, N); break;
		case  5: ATFFT_DIF_Radix2_AVX2< 5>(y0, w4, N); break;
		case  6: ATFFT_DIF_Radix2_AVX2< 6>(y0, w4, N); break;
		case  7: ATFFT_DIF_Radix2_AVX2< 7>(y0, w4, N); break;
		case  8: ATFFT_DIF_Radix2_AVX2< 8>(y0, w4, N); break;
		case  9: ATFFT_DIF_Radix2_AVX2< 9>(y0, w4, N); break;
		case 10: ATFFT_DIF_Radix2_AVX2<10>(y0, w4, N); break;
		case 11: ATFFT_DIF_Radix2_AVX2<11>(y0, w4, N); break;
		case 12: ATFFT_DIF_Radix2_AVX2<12>(y0, w4, N); break;
	}
}

void ATFFT_DIF_Radix4_AVX2(float *y0, const float *w4, int N, int logStep) {
	switch(logStep) {
		case  4: ATFFT_DIF_Radix4_AVX2< 4>(y0, w4, N); break;
		case  6: ATFFT_DIF_Radix4_AVX2< 6>(y0, w4, N); break;
		case  8: ATFFT_DIF_Radix4_AVX2< 8>(y0, w4, N); break;
		case 10: ATFFT_DIF_Radix4_AVX2<10>(y0, w4, N); break;
		case 12: ATFFT_DIF_Radix4_AVX2<12>(y0, w4, N); break;
	}
}

/////////////////////////////////////////////////////////////////////////////

VD_CPU_TARGET("avx2,fma")
void ATFFT_MultiplyAdd_AVX2(float *VDRESTRICT dst, const float *VDRESTRICT src1, const float *VDRESTRICT src2, int N) {
	const int N2 = N >> 1;
	for(int i=0; i<N2; ++i) {
		const __m256 ri1 = _mm256_load_ps(src1);
		src1 += 8;

		const __m256 ri2 = _mm256_load_ps(src2);
		src2 += 8;

		//    r      i
		//  r1*r2  i1*r2
		// -i1*i2  r1*i2

		_mm256_store_ps(
			dst,
			_mm256_fmsubadd_ps(
				ri1,
				_mm256_moveldup_ps(ri2),
				_mm256_mul_ps(
					_mm256_permute_ps(ri1, _MM_SHUFFLE(2, 3, 0, 1)),
					_mm256_movehdup_ps(ri2)
				)
			)
		);

		dst += 8;
	}

	// patch DC and Nyquist values
	// first two values are real DC and fsc*0.5, rest are complex
	dst -= 8*N2;
	src1 -= 8*N2;
	src2 -= 8*N2;

	const __m256 zero = _mm256_setzero_ps();
	_mm256_store_pd((double *)dst,
		_mm256_castps_pd(
			_mm256_fmadd_ps(
				_mm256_castpd_ps(_mm256_broadcast_sd((const double *)src1)),
				_mm256_castpd_ps(_mm256_broadcast_sd((const double *)src2)),
				_mm256_castpd_ps(_mm256_broadcast_sd((const double *)dst ))
			)
		)
	);
}

/////////////////////////////////////////////////////////////////////////////

VD_CPU_TARGET("avx2,fma")
void ATFFT_IMDCT_PreTransform_AVX2(float *dst, const float *src, const float *w, size_t N) {
	size_t N2 = N/2;

	const float *VDRESTRICT w1 = w;
	const float *VDRESTRICT w2 = w + N - 16;

	const float *VDRESTRICT src1 = src;
	const float *VDRESTRICT src2 = src + N - 16;

	float *VDRESTRICT dst1 = dst;
	float *VDRESTRICT dst2 = dst + N - 16;

	for(size_t i = 0; i < N2; i += 16) {
		const __m256 wr1 = _mm256_load_ps(w1  );
		const __m256 wi1 = _mm256_load_ps(w1+8);
		const __m256 wr2 = _mm256_load_ps(w2  );
		const __m256 wi2 = _mm256_load_ps(w2+8);
		w1 += 16;
		w2 -= 16;

		__m256 r3iCr2iD_iEr1iFr0 = _mm256_loadu_ps(&src1[0]);
		__m256 i8r7i9r6_iAr5iBr4 = _mm256_loadu_ps(&src1[8]);
		__m256 i4rBi5rA_i6r9i7r8 = _mm256_loadu_ps(&src2[0]);
		__m256 i0rFi1rE_i2rDi3rC = _mm256_loadu_ps(&src2[8]);
		src1 += 16;
		src2 -= 16;

		// deinterleave
		__m256 iAr5iBr4_iEr1iFr0 = _mm256_permute2f128_ps(r3iCr2iD_iEr1iFr0, i8r7i9r6_iAr5iBr4, 0x20);
		__m256 i8r7i9r6_iCr3iDr2 = _mm256_permute2f128_ps(r3iCr2iD_iEr1iFr0, i8r7i9r6_iAr5iBr4, 0x31);
		__m256 i2rDi3rC_i6r9i7r8 = _mm256_permute2f128_ps(i4rBi5rA_i6r9i7r8, i0rFi1rE_i2rDi3rC, 0x20);
		__m256 i0rFi1rE_i4rBi5rA = _mm256_permute2f128_ps(i4rBi5rA_i6r9i7r8, i0rFi1rE_i2rDi3rC, 0x31);

		__m256 r76543210 = _mm256_shuffle_ps(iAr5iBr4_iEr1iFr0, i8r7i9r6_iCr3iDr2, _MM_SHUFFLE(2, 0, 2, 0));
		__m256 i32107654 = _mm256_shuffle_ps(i0rFi1rE_i4rBi5rA, i2rDi3rC_i6r9i7r8, _MM_SHUFFLE(1, 3, 1, 3));

		__m256 iBA98FEDC = _mm256_shuffle_ps(i8r7i9r6_iCr3iDr2, iAr5iBr4_iEr1iFr0, _MM_SHUFFLE(1, 3, 1, 3));
		__m256 rFEDCBA98 = _mm256_shuffle_ps(i2rDi3rC_i6r9i7r8, i0rFi1rE_i4rBi5rA, _MM_SHUFFLE(2, 0, 2, 0));

		__m256 i76543210 = _mm256_permute2f128_ps(i32107654, i32107654, 0x01);
		__m256 iFEDCBA98 = _mm256_permute2f128_ps(iBA98FEDC, iBA98FEDC, 0x01);

		__m256 fr76543210 = _mm256_fmsub_ps(r76543210, wr1, _mm256_mul_ps(i76543210, wi1));
		__m256 fi76543210 = _mm256_fmadd_ps(r76543210, wi1, _mm256_mul_ps(i76543210, wr1));
		__m256 frFEDCBA98 = _mm256_fmsub_ps(rFEDCBA98, wr2, _mm256_mul_ps(iFEDCBA98, wi2));
		__m256 fiFEDCBA98 = _mm256_fmadd_ps(rFEDCBA98, wi2, _mm256_mul_ps(iFEDCBA98, wr2));

		__m256 f5410 = _mm256_unpacklo_ps(fr76543210, fi76543210);
		__m256 f7632 = _mm256_unpackhi_ps(fr76543210, fi76543210);
		__m256 fDC98 = _mm256_unpacklo_ps(frFEDCBA98, fiFEDCBA98);
		__m256 fFEBA = _mm256_unpackhi_ps(frFEDCBA98, fiFEDCBA98);

		_mm256_store_ps(&dst1[0], _mm256_permute2f128_ps(f5410, f7632, 0x20));
		_mm256_store_ps(&dst1[8], _mm256_permute2f128_ps(f5410, f7632, 0x31));
		_mm256_store_ps(&dst2[0], _mm256_permute2f128_ps(fDC98, fFEBA, 0x20));
		_mm256_store_ps(&dst2[8], _mm256_permute2f128_ps(fDC98, fFEBA, 0x31));
		dst1 += 16;
		dst2 -= 16;
	}
}

VD_CPU_TARGET("avx2,fma")
void ATFFT_IMDCT_PostTransform_AVX2(float *dst, const float *src, const float *w, size_t N) {
	// generate 3rd and 2nd quarters of full IMDCT output, in that order
	const size_t N2 = N/2;

	const float *VDRESTRICT src1 = src;
	const float *VDRESTRICT src2 = src + N - 16;
	const float *VDRESTRICT w1 = w;
	const float *VDRESTRICT w2 = w1 + N - 16;
	float *VDRESTRICT dst1 = dst + N2 - 16;
	float *VDRESTRICT dst2 = dst + N2;

	for(size_t i = 0; i < N2; i += 16) {
		const __m256 w1r = _mm256_load_ps(&w1[0]);
		const __m256 w1i = _mm256_load_ps(&w1[8]);
		const __m256 w2r = _mm256_load_ps(&w2[0]);
		const __m256 w2i = _mm256_load_ps(&w2[8]);
		w1 += 16;
		w2 -= 16;

		__m256 r1 = _mm256_load_ps(&src1[0]);
		__m256 i1 = _mm256_load_ps(&src1[8]);
		__m256 r2 = _mm256_load_ps(&src2[0]);
		__m256 i2 = _mm256_load_ps(&src2[8]);
		src1 += 16;
		src2 -= 16;

		__m256 r3 = _mm256_fmsub_ps(i1, w1i, _mm256_mul_ps(r1, w1r));	// negated
		__m256 i3 = _mm256_fmadd_ps(r1, w1i, _mm256_mul_ps(i1, w1r));
		__m256 r4 = _mm256_fmsub_ps(i2, w2i, _mm256_mul_ps(r2, w2r));	// negated
		__m256 i4 = _mm256_fmadd_ps(r2, w2i, _mm256_mul_ps(i2, w2r));

		r4 = _mm256_shuffle_ps(r4, r4, _MM_SHUFFLE(0, 1, 2, 3));
		r3 = _mm256_shuffle_ps(r3, r3, _MM_SHUFFLE(0, 1, 2, 3));

		r4 = _mm256_permute2f128_ps(r4, r4, 0x01);
		r3 = _mm256_permute2f128_ps(r3, r3, 0x01);

		// write third quarter
		__m256 t0 = _mm256_unpacklo_ps(i4, r3);
		__m256 t1 = _mm256_unpackhi_ps(i4, r3);

		_mm256_storeu_ps(&dst1[0], _mm256_permute2f128_ps(t0, t1, 0x20));
		_mm256_storeu_ps(&dst1[8], _mm256_permute2f128_ps(t0, t1, 0x31));
		dst1 -= 16;

		// write second quarter
		__m256 t2 = _mm256_unpacklo_ps(i3, r4);
		__m256 t3 = _mm256_unpackhi_ps(i3, r4);

		_mm256_storeu_ps(&dst2[0], _mm256_permute2f128_ps(t2, t3, 0x20));
		_mm256_storeu_ps(&dst2[8], _mm256_permute2f128_ps(t2, t3, 0x31));
		dst2 += 16;
	}
}

#endif
