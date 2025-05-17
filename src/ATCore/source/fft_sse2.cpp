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

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
#include <intrin.h>

template<int T_LogStep>
void ATFFT_DIT_Radix2_SSE2(float *y0, const float *w4, int N) {
	constexpr int step = 1 << T_LogStep;
	float *__restrict y1 = y0;
	const float *__restrict w1 = w4;

	for(int i=0; i<N; i += step*2) {
		const float *__restrict w2 = w1;

		for(int j=0; j<step/8; ++j) {
			__m128 r0 = _mm_load_ps(y1);
			__m128 i0 = _mm_load_ps(y1+4);
			__m128 r1 = _mm_load_ps(y1+step);
			__m128 i1 = _mm_load_ps(y1+step+4);
			__m128 rw = _mm_load_ps(w2);
			__m128 iw = _mm_load_ps(w2+4);

			__m128 rt1 = _mm_sub_ps(_mm_mul_ps(r1, rw), _mm_mul_ps(i1, iw));
			__m128 it1 = _mm_add_ps(_mm_mul_ps(r1, iw), _mm_mul_ps(i1, rw));

			__m128 r2 = _mm_add_ps(r0, rt1);
			__m128 i2 = _mm_add_ps(i0, it1);
			__m128 r3 = _mm_sub_ps(r0, rt1);
			__m128 i3 = _mm_sub_ps(i0, it1);

			_mm_store_ps(y1       , r2);
			_mm_store_ps(y1+4     , i2);
			_mm_store_ps(y1+step  , r3);
			_mm_store_ps(y1+step+4, i3);

			w2 += 8;
			y1 += 8;
		}

		y1 += step;
	}
}

template<int T_LogStep>
void ATFFT_DIT_Radix4_SSE2(float *y0, const float *w4, int N) {
	constexpr int step = 1 << T_LogStep;
	const float *__restrict w1 = w4;

	constexpr int step2 = step*2;
	constexpr int step3 = step*3;

	float *__restrict y1 = y0;

	for(int i=0; i<N; i += step*4) {
		const float *__restrict w2 = w1;

		for(int j=0; j<step/8; ++j) {
			__m128 r1  = _mm_load_ps(y1+step);
			__m128 i1  = _mm_load_ps(y1+step+4);
			__m128 rw1 = _mm_load_ps(w2+8);		// note permutation 1 <-> 2 on twiddle
			__m128 iw1 = _mm_load_ps(w2+12);
			__m128 rt1 = _mm_sub_ps(_mm_mul_ps(r1, rw1), _mm_mul_ps(i1, iw1));
			__m128 it1 = _mm_add_ps(_mm_mul_ps(r1, iw1), _mm_mul_ps(i1, rw1));

			__m128 r0  = _mm_load_ps(y1);
			__m128 r4 = _mm_add_ps(r0 , rt1);
			__m128 r5 = _mm_sub_ps(r0 , rt1);
			__m128 i0  = _mm_load_ps(y1+4);
			__m128 i4 = _mm_add_ps(i0 , it1);
			__m128 i5 = _mm_sub_ps(i0 , it1);

			__m128 r2  = _mm_load_ps(y1+step2);
			__m128 i2  = _mm_load_ps(y1+step2+4);
			__m128 rw2 = _mm_load_ps(w2);
			__m128 iw2 = _mm_load_ps(w2+4);
			__m128 rt2 = _mm_sub_ps(_mm_mul_ps(r2, rw2), _mm_mul_ps(i2, iw2));
			__m128 it2 = _mm_add_ps(_mm_mul_ps(r2, iw2), _mm_mul_ps(i2, rw2));

			__m128 r3  = _mm_load_ps(y1+step3);
			__m128 i3  = _mm_load_ps(y1+step3+4);
			__m128 rw3 = _mm_load_ps(w2+16);
			__m128 iw3 = _mm_load_ps(w2+20);			
			__m128 rt3 = _mm_sub_ps(_mm_mul_ps(r3, rw3), _mm_mul_ps(i3, iw3));
			__m128 it3 = _mm_add_ps(_mm_mul_ps(r3, iw3), _mm_mul_ps(i3, rw3));

			__m128 r6 = _mm_add_ps(rt2, rt3);
			__m128 r7 = _mm_sub_ps(rt2, rt3);
			__m128 i6 = _mm_add_ps(it2, it3);
			__m128 i7 = _mm_sub_ps(it2, it3);

			__m128 r8 = _mm_add_ps(r4, r6);
			__m128 i8 = _mm_add_ps(i4, i6);
			__m128 rA = _mm_sub_ps(r4, r6);
			__m128 iA = _mm_sub_ps(i4, i6);

			__m128 r9 = _mm_add_ps(r5, i7);
			__m128 i9 = _mm_sub_ps(i5, r7);
			__m128 rB = _mm_sub_ps(r5, i7);
			__m128 iB = _mm_add_ps(i5, r7);

			_mm_store_ps(y1        , r8);
			_mm_store_ps(y1+4      , i8);
			_mm_store_ps(y1+step   , r9);
			_mm_store_ps(y1+step+4 , i9);
			_mm_store_ps(y1+step2  , rA);
			_mm_store_ps(y1+step2+4, iA);
			_mm_store_ps(y1+step3  , rB);
			_mm_store_ps(y1+step3+4, iB);

			w2 += 24;
			y1 += 8;
		}

		y1 += step*3;
	}
}

void ATFFT_DIT_Radix8_SSE2(float *dst0, const float *y0, const uint32 *order0, int N) {
	float *__restrict dst = dst0;
	const float *__restrict y = y0;
	const uint32 *__restrict order = order0;

	// sqrt(2)/2
	const int step0 = N/8;

	constexpr float r2d2 = 0.70710678118654752440084436210485f;
	__m128 sign_0101 = _mm_castsi128_ps(_mm_set_epi32(0x80000000, 0, 0x80000000, 0));

	__m128 twiddle_c = _mm_set_ps(-r2d2,  0,  r2d2, 1);
	__m128 twiddle_s = _mm_set_ps(-r2d2, -1, -r2d2, 0);

	for(int i=0; i<N/16; ++i) {
		__m128 x04 = _mm_castpd_ps(_mm_loadh_pd(_mm_load_sd((const double *)&y[0 * step0]), (const double *)&y[1 * step0]));
		__m128 x26 = _mm_castpd_ps(_mm_loadh_pd(_mm_load_sd((const double *)&y[2 * step0]), (const double *)&y[3 * step0]));
		__m128 x15 = _mm_castpd_ps(_mm_loadh_pd(_mm_load_sd((const double *)&y[4 * step0]), (const double *)&y[5 * step0]));
		__m128 x37 = _mm_castpd_ps(_mm_loadh_pd(_mm_load_sd((const double *)&y[6 * step0]), (const double *)&y[7 * step0]));
		y += 2;

		// first butterfly
		__m128 a_r0i0r4i4 = _mm_add_ps(x04, x15);
		__m128 a_r1i1r5i5 = _mm_sub_ps(x04, x15);
		__m128 a_r2i2r6i6 = _mm_add_ps(x26, x37);
		__m128 a_r3i3r7i7 = _mm_sub_ps(x26, x37);

		// second butterfly (elements 3/7 rotated by -j)
		__m128 at_r3i3r7i7 = _mm_xor_ps(_mm_shuffle_ps(a_r3i3r7i7, a_r3i3r7i7, _MM_SHUFFLE(2, 3, 0, 1)), sign_0101);

		__m128 b_r0i0r4i4 = _mm_add_ps(a_r0i0r4i4, a_r2i2r6i6);
		__m128 b_r1i1r5i5 = _mm_add_ps(a_r1i1r5i5, at_r3i3r7i7);
		__m128 b_r2i2r6i6 = _mm_sub_ps(a_r0i0r4i4, a_r2i2r6i6);
		__m128 b_r3i3r7i7 = _mm_sub_ps(a_r1i1r5i5, at_r3i3r7i7);

		// transpose
		__m128 b_r0i0r1i1 = _mm_shuffle_ps(b_r0i0r4i4, b_r1i1r5i5, _MM_SHUFFLE(1, 0, 1, 0));
		__m128 b_r2i2r3i3 = _mm_shuffle_ps(b_r2i2r6i6, b_r3i3r7i7, _MM_SHUFFLE(1, 0, 1, 0));
		__m128 b_r4i4r5i5 = _mm_shuffle_ps(b_r0i0r4i4, b_r1i1r5i5, _MM_SHUFFLE(3, 2, 3, 2));
		__m128 b_r6i6r7i7 = _mm_shuffle_ps(b_r2i2r6i6, b_r3i3r7i7, _MM_SHUFFLE(3, 2, 3, 2));

		__m128 b_r0r1r2r3 = _mm_shuffle_ps(b_r0i0r1i1, b_r2i2r3i3, _MM_SHUFFLE(2, 0, 2, 0));
		__m128 b_i0i1i2i3 = _mm_shuffle_ps(b_r0i0r1i1, b_r2i2r3i3, _MM_SHUFFLE(3, 1, 3, 1));
		__m128 b_r4r5r6r7 = _mm_shuffle_ps(b_r4i4r5i5, b_r6i6r7i7, _MM_SHUFFLE(2, 0, 2, 0));
		__m128 b_i4i5i6i7 = _mm_shuffle_ps(b_r4i4r5i5, b_r6i6r7i7, _MM_SHUFFLE(3, 1, 3, 1));

		// third butterfly (elements 4-7 rotated by w0-w3)
		__m128 bt_r4r5r6r7 = _mm_sub_ps(_mm_mul_ps(b_r4r5r6r7, twiddle_c), _mm_mul_ps(b_i4i5i6i7, twiddle_s));
		__m128 bt_i4i5i6i7 = _mm_add_ps(_mm_mul_ps(b_r4r5r6r7, twiddle_s), _mm_mul_ps(b_i4i5i6i7, twiddle_c));

		__m128 c_r0r1r2r3 = _mm_add_ps(b_r0r1r2r3, bt_r4r5r6r7);
		__m128 c_i0i1i2i3 = _mm_add_ps(b_i0i1i2i3, bt_i4i5i6i7);
		__m128 c_r4r5r6r7 = _mm_sub_ps(b_r0r1r2r3, bt_r4r5r6r7);
		__m128 c_i4i5i6i7 = _mm_sub_ps(b_i0i1i2i3, bt_i4i5i6i7);

		float *__restrict dst2 = &dst[*order++];
		_mm_store_ps(&dst2[ 0], c_r0r1r2r3);
		_mm_store_ps(&dst2[ 4], c_i0i1i2i3);
		_mm_store_ps(&dst2[ 8], c_r4r5r6r7);
		_mm_store_ps(&dst2[12], c_i4i5i6i7);
	}
}

void ATFFT_DIT_R2C_SSE2(float *dst0, const float *src0, const float *w, int N) {
	const float *__restrict w2 = w;
	const float *__restrict src = src0;
	float *__restrict dst = dst0;

	__m128 half = _mm_set1_ps(0.5f);

	__m128 prevr1 = _mm_setzero_ps();
	__m128 previ1 = _mm_setzero_ps();

	for(int i=0; i<N/2; i += 8) {
		__m128 rl = _mm_load_ps(&src[i]);
		__m128 il = _mm_load_ps(&src[i+4]);
		__m128 rh = _mm_load_ps(&src[N-i-8]);
		__m128 ih = _mm_load_ps(&src[N-i-4]);

		rh = _mm_shuffle_ps(rh, rh, _MM_SHUFFLE(1, 2, 3, 0));
		ih = _mm_shuffle_ps(ih, ih, _MM_SHUFFLE(1, 2, 3, 0));

		if (i) {
			rh = _mm_move_ss(rh, _mm_load_ss(&src[N-i]));
			ih = _mm_move_ss(ih, _mm_load_ss(&src[N-i+4]));
		}

		// Xe[n] = (Z[k] + Z*[N-k])/2
		__m128 rxe = _mm_mul_ps(_mm_add_ps(rl, rh), half);
		__m128 ixe = _mm_mul_ps(_mm_sub_ps(il, ih), half);

		// Xo[n] = -j(Z[k] - Z*[N-k])/2 * w[k]
		__m128 rxo = _mm_add_ps(il, ih);
		__m128 ixo = _mm_sub_ps(rh, rl);

		__m128 rw = _mm_load_ps(w2);
		__m128 iw = _mm_load_ps(w2+4);
		w2 += 8;

		// = (r3*rw - i3*iw, r3*iw + i3*rw)
		__m128 rxo2 = _mm_sub_ps(_mm_mul_ps(rxo, rw), _mm_mul_ps(ixo, iw));
		__m128 ixo2 = _mm_add_ps(_mm_mul_ps(rxo, iw), _mm_mul_ps(ixo, rw));

		// final butterfly and conjugation
		__m128 r0 = _mm_add_ps(rxe, rxo2);
		__m128 i0 = _mm_add_ps(ixe, ixo2);
		__m128 r1 = _mm_sub_ps(rxe, rxo2);
		__m128 i1 = _mm_sub_ps(ixo2, ixe);

		// interleave and store low
		_mm_storeu_ps(&dst[i+0], _mm_unpacklo_ps(r0, i0));
		_mm_storeu_ps(&dst[i+4], _mm_unpackhi_ps(r0, i0));

		// interleave, reverse, and store high
		r1 = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(0, 1, 2, 3));
		i1 = _mm_shuffle_ps(i1, i1, _MM_SHUFFLE(0, 1, 2, 3));

		__m128 hi0 = _mm_unpacklo_ps(r1, i1);
		__m128 hi1 = _mm_unpackhi_ps(r1, i1);

		_mm_storeu_ps(&dst[N-i-6], hi0);

		if (i)
			_mm_storeu_ps(&dst[N-i-2], hi1);
		else
			_mm_storel_pi((__m64 *)&dst[N-i-2], hi1);
	}

	float rz = src[0];
	float iz = src[4];
	dst[0] = rz + iz;
	dst[1] = rz - iz;

	dst[N/2] = src[N/2];
	dst[N/2+1] = -src[N/2+4];
}

void ATFFT_DIF_C2R_SSE2(float *dst0, const float *x, const float *w, int N) {
	float *__restrict dst = dst0;
	
	const __m128 half = _mm_set1_ps(0.5f);
	const __m128 nhalf = _mm_set1_ps(-0.5f);
	const __m128 sign = _mm_castsi128_ps(_mm_set1_epi32(0x80000000));

	__m128 prevr1;
	__m128 previ1;

	{
		__m128 a01 = _mm_loadu_ps(&x[0]);
		__m128 a23 = _mm_loadu_ps(&x[4]);
		__m128 a45 = _mm_loadh_pi(_mm_loadl_pi(_mm_setzero_ps(), (const __m64 *)&x[N-2]), (const __m64 *)x);
		__m128 a67 = _mm_loadu_ps(&x[N-6]);

		// deinterleave and reverse high half
		__m128 rl = _mm_shuffle_ps(a01, a23, _MM_SHUFFLE(2, 0, 2, 0));
		__m128 il = _mm_shuffle_ps(a01, a23, _MM_SHUFFLE(3, 1, 3, 1));
		__m128 rh = _mm_shuffle_ps(a45, a67, _MM_SHUFFLE(0, 2, 0, 2));
		__m128 ih = _mm_shuffle_ps(a45, a67, _MM_SHUFFLE(1, 3, 1, 3));

		// Xe[n] = (X[k] + X*[N/2-k])/2
		__m128 rxe = _mm_mul_ps(_mm_add_ps(rl, rh), half);
		__m128 ixe = _mm_mul_ps(_mm_sub_ps(il, ih), half);

		// Xo[n] = j(X[k] - X*[N/2-k])/2 * w[k]
		__m128 rxo = _mm_add_ps(il, ih);
		__m128 ixo = _mm_sub_ps(rl, rh);

		__m128 rw = _mm_load_ps(w);
		__m128 iw = _mm_load_ps(w+4);
		w += 8;

		__m128 rxo2 = _mm_sub_ps(_mm_mul_ps(ixo, iw), _mm_mul_ps(rxo, rw));
		__m128 ixo2 = _mm_add_ps(_mm_mul_ps(ixo, rw), _mm_mul_ps(rxo, iw));

		__m128 r0 = _mm_add_ps(rxe, rxo2);
		__m128 i0 = _mm_add_ps(ixe, ixo2);
		__m128 r1 = _mm_sub_ps(rxe, rxo2);
		__m128 i1 = _mm_sub_ps(ixo2, ixe);		// - for conj

		_mm_store_ps(&dst[0], r0);
		_mm_store_ps(&dst[4], i0);

		// re-reverse high half and rotate it for the one element shift
		prevr1 = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(1, 2, 3, 0));
		previ1 = _mm_shuffle_ps(i1, i1, _MM_SHUFFLE(1, 2, 3, 0));
	}

	for(int i=8; i<N/2; i += 8) {
		__m128 a01 = _mm_loadu_ps(&x[i]);
		__m128 a23 = _mm_loadu_ps(&x[i+4]);
		__m128 a45 = _mm_loadu_ps(&x[N-i-2]);
		__m128 a67 = _mm_loadu_ps(&x[N-i-6]);

		// deinterleave and reverse high half
		__m128 rl = _mm_shuffle_ps(a01, a23, _MM_SHUFFLE(2, 0, 2, 0));
		__m128 il = _mm_shuffle_ps(a01, a23, _MM_SHUFFLE(3, 1, 3, 1));
		__m128 rh = _mm_shuffle_ps(a45, a67, _MM_SHUFFLE(0, 2, 0, 2));
		__m128 ih = _mm_shuffle_ps(a45, a67, _MM_SHUFFLE(1, 3, 1, 3));

		// Xe[n] = (X[k] + X*[N/2-k])/2
		__m128 rxe = _mm_mul_ps(_mm_add_ps(rl, rh), half);
		__m128 ixe = _mm_mul_ps(_mm_sub_ps(il, ih), half);

		// Xo[n] = j(X[k] - X*[N/2-k])/2 * w[k]
		__m128 rxo = _mm_add_ps(il, ih);
		__m128 ixo = _mm_sub_ps(rl, rh);

		__m128 rw = _mm_load_ps(w);
		__m128 iw = _mm_load_ps(w+4);
		w += 8;

		__m128 rxo2 = _mm_sub_ps(_mm_mul_ps(ixo, iw), _mm_mul_ps(rxo, rw));
		__m128 ixo2 = _mm_add_ps(_mm_mul_ps(ixo, rw), _mm_mul_ps(rxo, iw));

		__m128 r0 = _mm_add_ps(rxe, rxo2);
		__m128 i0 = _mm_add_ps(ixe, ixo2);
		__m128 r1 = _mm_sub_ps(rxe, rxo2);
		__m128 i1 = _mm_sub_ps(ixo2, ixe);		// - for conj

		_mm_store_ps(&dst[i], r0);
		_mm_store_ps(&dst[i+4], i0);

		// re-reverse high half and rotate it for the one element shift
		r1 = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(1, 2, 3, 0));
		i1 = _mm_shuffle_ps(i1, i1, _MM_SHUFFLE(1, 2, 3, 0));

		_mm_store_ps(&dst[N-i], _mm_move_ss(prevr1, r1));
		_mm_store_ps(&dst[N-i+4], _mm_move_ss(previ1, i1));

		prevr1 = r1;
		previ1 = i1;
	}

	_mm_store_ps(&dst[N/2], _mm_move_ss(prevr1, _mm_load_ss(&x[N/2])));
	_mm_store_ps(&dst[N/2+4], _mm_move_ss(previ1, _mm_xor_ps(_mm_load_ss(&x[N/2+1]), sign)));

	dst[0] = (x[0] + x[1]) * 0.5f;
	dst[4] = (x[0] - x[1]) * 0.5f;
}


template<int T_LogStep>
void ATFFT_DIF_Radix2_SSE2(float *y0, const float *w, int N) {
	constexpr int step = 1 << T_LogStep;
	float *__restrict y1 = y0;

	for(int i=0; i<N; i += step*2) {
		const float *__restrict w2 = w;

		for(int j=0; j<step/8; ++j) {
			__m128 r0 = _mm_load_ps(y1);
			__m128 i0 = _mm_load_ps(y1+4);
			__m128 r1 = _mm_load_ps(y1+step);
			__m128 i1 = _mm_load_ps(y1+step+4);
			__m128 rw = _mm_load_ps(w2);
			__m128 iw = _mm_load_ps(w2+4);

			__m128 r2 = _mm_add_ps(r0, r1);
			__m128 i2 = _mm_add_ps(i0, i1);
			__m128 r3 = _mm_sub_ps(r0, r1);
			__m128 i3 = _mm_sub_ps(i0, i1);

			__m128 rt3 = _mm_add_ps(_mm_mul_ps(r3, rw), _mm_mul_ps(i3, iw));
			__m128 it3 = _mm_sub_ps(_mm_mul_ps(i3, rw), _mm_mul_ps(r3, iw));

			_mm_store_ps(y1       , r2);
			_mm_store_ps(y1+4     , i2);
			_mm_store_ps(y1+step  , rt3);
			_mm_store_ps(y1+step+4, it3);

			w2 += 8;
			y1 += 8;
		}

		y1 += step;
	}
}

template<int T_LogStep>
void ATFFT_DIF_Radix4_SSE2(float *y0, const float *w4, int N) {
	constexpr int step = 1 << T_LogStep;
	constexpr int step2 = step*2;
	constexpr int step3 = step*3;

	const float *__restrict w1 = w4;
	float *__restrict y1 = y0;

	for(int i=0; i<N; i += step*4) {
		const float *__restrict w2 = w1;

		for(int j=0; j<step/8; ++j) {
			__m128 r0  = _mm_load_ps(y1);			__m128 i0  = _mm_load_ps(y1+4);
			__m128 r1  = _mm_load_ps(y1+step);		__m128 i1  = _mm_load_ps(y1+step+4);
			__m128 r2  = _mm_load_ps(y1+step2);		__m128 i2  = _mm_load_ps(y1+step2+4);
			__m128 r3  = _mm_load_ps(y1+step3);		__m128 i3  = _mm_load_ps(y1+step3+4);

			__m128 r4 = _mm_add_ps(r0, r2);			__m128 i4 = _mm_add_ps(i0, i2);
			__m128 r5 = _mm_add_ps(r1, r3);			__m128 i5 = _mm_add_ps(i1, i3);
			__m128 r6 = _mm_sub_ps(r0, r2);			__m128 i6 = _mm_sub_ps(i0, i2);
			// *j
			__m128 r7 = _mm_sub_ps(i3, i1);			__m128 i7 = _mm_sub_ps(r1, r3);

			__m128 r8 = _mm_add_ps(r4, r5);			__m128 i8 = _mm_add_ps(i4, i5);
			__m128 r9 = _mm_sub_ps(r4, r5);			__m128 i9 = _mm_sub_ps(i4, i5);
			__m128 rA = _mm_add_ps(r6, r7);			__m128 iA = _mm_add_ps(i6, i7);
			__m128 rB = _mm_sub_ps(r6, r7);			__m128 iB = _mm_sub_ps(i6, i7);

			__m128 rw1 = _mm_load_ps(w2+8);		// note permutation 1 <-> 2 on twiddle
			__m128 iw1 = _mm_load_ps(w2+12);
			__m128 rt9 = _mm_add_ps(_mm_mul_ps(r9, rw1), _mm_mul_ps(i9, iw1));
			__m128 it9 = _mm_sub_ps(_mm_mul_ps(i9, rw1), _mm_mul_ps(r9, iw1));

			__m128 rw2 = _mm_load_ps(w2);
			__m128 iw2 = _mm_load_ps(w2+4);
			__m128 rtA = _mm_add_ps(_mm_mul_ps(rA, rw2), _mm_mul_ps(iA, iw2));
			__m128 itA = _mm_sub_ps(_mm_mul_ps(iA, rw2), _mm_mul_ps(rA, iw2));

			__m128 rw3 = _mm_load_ps(w2+16);
			__m128 iw3 = _mm_load_ps(w2+20);			
			__m128 rtB = _mm_add_ps(_mm_mul_ps(rB, rw3), _mm_mul_ps(iB, iw3));
			__m128 itB = _mm_sub_ps(_mm_mul_ps(iB, rw3), _mm_mul_ps(rB, iw3));

			_mm_store_ps(y1        , r8);
			_mm_store_ps(y1+4      , i8);
			_mm_store_ps(y1+step   , rt9);
			_mm_store_ps(y1+step+4 , it9);
			_mm_store_ps(y1+step2  , rtA);
			_mm_store_ps(y1+step2+4, itA);
			_mm_store_ps(y1+step3  , rtB);
			_mm_store_ps(y1+step3+4, itB);

			w2 += 24;
			y1 += 8;
		}

		y1 += step*3;
	}
}

void ATFFT_DIF_Radix8_SSE2(float *dst0, const float *src0, const uint32 *order0, int N) {
	constexpr float r2d2 = 0.70710678118654752440084436210485f;

	const uint32 *__restrict order = order0;
	float *__restrict dst = dst0;
	const float *__restrict src = src0;

	__m128 twiddle_c = _mm_set_ps(-r2d2,  0,  r2d2, 1);
	__m128 twiddle_s = _mm_set_ps(-r2d2, -1, -r2d2, 0);
	__m128 sign_1010 = _mm_castsi128_ps(_mm_set_epi32(0, 0x80000000, 0, 0x80000000));

	for(int i=0; i<N/16; ++i) {
		const float *__restrict src2 = &src[*order++];
		__m128 a_r0r1r2r3 = _mm_load_ps(src2 +  0);
		__m128 a_i0i1i2i3 = _mm_load_ps(src2 +  4);
		__m128 a_r4r5r6r7 = _mm_load_ps(src2 +  8);
		__m128 a_i4i5i6i7 = _mm_load_ps(src2 + 12);

		// DFT-8 stage
		__m128 b_r0r1r2r3 = _mm_add_ps(a_r0r1r2r3, a_r4r5r6r7);
		__m128 b_i0i1i2i3 = _mm_add_ps(a_i0i1i2i3, a_i4i5i6i7);
		__m128 b_r4r5r6r7 = _mm_sub_ps(a_r0r1r2r3, a_r4r5r6r7);
		__m128 b_i4i5i6i7 = _mm_sub_ps(a_i0i1i2i3, a_i4i5i6i7);

		// *w(1,8) = (1+j)*sqrt(2)/2
		// *w(2,8) = j
		// *w(3,8) = (-1+j)*sqrt(2)/2
		__m128 bt_r4r5r6r7 = _mm_add_ps(_mm_mul_ps(b_r4r5r6r7, twiddle_c), _mm_mul_ps(b_i4i5i6i7, twiddle_s));
		__m128 bt_i4i5i6i7 = _mm_sub_ps(_mm_mul_ps(b_i4i5i6i7, twiddle_c), _mm_mul_ps(b_r4r5r6r7, twiddle_s));

		// transpose
		__m128 c_r0i0r1i1 = _mm_unpacklo_ps(b_r0r1r2r3, b_i0i1i2i3);
		__m128 c_r2i2r3i3 = _mm_unpackhi_ps(b_r0r1r2r3, b_i0i1i2i3);
		__m128 c_r4i4r5i5 = _mm_unpacklo_ps(bt_r4r5r6r7, bt_i4i5i6i7);
		__m128 c_r6i6r7i7 = _mm_unpackhi_ps(bt_r4r5r6r7, bt_i4i5i6i7);

		__m128 c_r0i0r4i4 = _mm_shuffle_ps(c_r0i0r1i1, c_r4i4r5i5, _MM_SHUFFLE(1, 0, 1, 0));
		__m128 c_r1i1r5i5 = _mm_shuffle_ps(c_r0i0r1i1, c_r4i4r5i5, _MM_SHUFFLE(3, 2, 3, 2));
		__m128 c_r2i2r6i6 = _mm_shuffle_ps(c_r2i2r3i3, c_r6i6r7i7, _MM_SHUFFLE(1, 0, 1, 0));
		__m128 c_r3i3r7i7 = _mm_shuffle_ps(c_r2i2r3i3, c_r6i6r7i7, _MM_SHUFFLE(3, 2, 3, 2));

		// DFT-4 stage
		__m128 d_r0i0r4i4 = _mm_add_ps(c_r0i0r4i4, c_r2i2r6i6);
		__m128 d_r1i1r5i5 = _mm_add_ps(c_r1i1r5i5, c_r3i3r7i7);
		__m128 d_r2i2r6i6 = _mm_sub_ps(c_r0i0r4i4, c_r2i2r6i6);
		__m128 d_r3i3r7i7 = _mm_sub_ps(c_r1i1r5i5, c_r3i3r7i7);

		// rotate elements 3 and 7 by +j
		d_r3i3r7i7 = _mm_xor_ps(_mm_shuffle_ps(d_r3i3r7i7, d_r3i3r7i7, _MM_SHUFFLE(2, 3, 0, 1)), sign_1010);

		// DFT-2 stage
		__m128 e_r0i0r4i4 = _mm_add_ps(d_r0i0r4i4, d_r1i1r5i5);
		__m128 e_r1i1r5i5 = _mm_sub_ps(d_r0i0r4i4, d_r1i1r5i5);
		__m128 e_r2i2r6i6 = _mm_add_ps(d_r2i2r6i6, d_r3i3r7i7);
		__m128 e_r3i3r7i7 = _mm_sub_ps(d_r2i2r6i6, d_r3i3r7i7);

		_mm_storel_pi((__m64 *)(dst + (N/8)*0), e_r0i0r4i4);
		_mm_storeh_pi((__m64 *)(dst + (N/8)*1), e_r0i0r4i4);
		_mm_storel_pi((__m64 *)(dst + (N/8)*2), e_r2i2r6i6);
		_mm_storeh_pi((__m64 *)(dst + (N/8)*3), e_r2i2r6i6);
		_mm_storel_pi((__m64 *)(dst + (N/8)*4), e_r1i1r5i5);
		_mm_storeh_pi((__m64 *)(dst + (N/8)*5), e_r1i1r5i5);
		_mm_storel_pi((__m64 *)(dst + (N/8)*6), e_r3i3r7i7);
		_mm_storeh_pi((__m64 *)(dst + (N/8)*7), e_r3i3r7i7);

		dst += 2;
	}
}

/////////////////////////////////////////////////////////////////////////////

void ATFFT_DIT_Radix2_SSE2(float *y0, const float *w4, int N, int logStep) {
	switch(logStep) {
		case  4: ATFFT_DIT_Radix2_SSE2< 4>(y0, w4, N); break;
		case  5: ATFFT_DIT_Radix2_SSE2< 5>(y0, w4, N); break;
		case  6: ATFFT_DIT_Radix2_SSE2< 6>(y0, w4, N); break;
		case  7: ATFFT_DIT_Radix2_SSE2< 7>(y0, w4, N); break;
		case  8: ATFFT_DIT_Radix2_SSE2< 8>(y0, w4, N); break;
		case  9: ATFFT_DIT_Radix2_SSE2< 9>(y0, w4, N); break;
		case 10: ATFFT_DIT_Radix2_SSE2<10>(y0, w4, N); break;
		case 11: ATFFT_DIT_Radix2_SSE2<11>(y0, w4, N); break;
		case 12: ATFFT_DIT_Radix2_SSE2<12>(y0, w4, N); break;
	}
}

void ATFFT_DIT_Radix4_SSE2(float *y0, const float *w4, int N, int logStep) {
	switch(logStep) {
		case  4: ATFFT_DIT_Radix4_SSE2< 4>(y0, w4, N); break;
		case  6: ATFFT_DIT_Radix4_SSE2< 6>(y0, w4, N); break;
		case  8: ATFFT_DIT_Radix4_SSE2< 8>(y0, w4, N); break;
		case 10: ATFFT_DIT_Radix4_SSE2<10>(y0, w4, N); break;
		case 12: ATFFT_DIT_Radix4_SSE2<12>(y0, w4, N); break;
	}
}

void ATFFT_DIF_Radix2_SSE2(float *y0, const float *w4, int N, int logStep) {
	switch(logStep) {
		case  4: ATFFT_DIF_Radix2_SSE2< 4>(y0, w4, N); break;
		case  5: ATFFT_DIF_Radix2_SSE2< 5>(y0, w4, N); break;
		case  6: ATFFT_DIF_Radix2_SSE2< 6>(y0, w4, N); break;
		case  7: ATFFT_DIF_Radix2_SSE2< 7>(y0, w4, N); break;
		case  8: ATFFT_DIF_Radix2_SSE2< 8>(y0, w4, N); break;
		case  9: ATFFT_DIF_Radix2_SSE2< 9>(y0, w4, N); break;
		case 10: ATFFT_DIF_Radix2_SSE2<10>(y0, w4, N); break;
		case 11: ATFFT_DIF_Radix2_SSE2<11>(y0, w4, N); break;
		case 12: ATFFT_DIF_Radix2_SSE2<12>(y0, w4, N); break;
	}
}

void ATFFT_DIF_Radix4_SSE2(float *y0, const float *w4, int N, int logStep) {
	switch(logStep) {
		case  4: ATFFT_DIF_Radix4_SSE2< 4>(y0, w4, N); break;
		case  6: ATFFT_DIF_Radix4_SSE2< 6>(y0, w4, N); break;
		case  8: ATFFT_DIF_Radix4_SSE2< 8>(y0, w4, N); break;
		case 10: ATFFT_DIF_Radix4_SSE2<10>(y0, w4, N); break;
		case 12: ATFFT_DIF_Radix4_SSE2<12>(y0, w4, N); break;
	}
}

/////////////////////////////////////////////////////////////////////////////

void ATFFT_MultiplyAdd_SSE2(float *VDRESTRICT dst, const float *VDRESTRICT src1, const float *VDRESTRICT src2, int N) {
	const int N2 = N >> 1;
	const __m128 inv_even = _mm_castsi128_ps(_mm_set_epi32(0, 0x80000000, 0, 0x80000000));
	for(int i=0; i<N2; ++i) {
		const __m128 ri1 = _mm_load_ps(src1);
		src1 += 4;

		const __m128 ri2 = _mm_load_ps(src2);
		src2 += 4;

		//    r      i
		//  r1*r2  i1*r2
		// -i1*i2  r1*i2

		const __m128 rr2 = _mm_shuffle_ps(ri2, ri2, 0b0'10'10'00'00);
		const __m128 ii2 = _mm_shuffle_ps(ri2, ri2, 0b0'11'11'01'01);
		const __m128 nir1 = _mm_xor_ps(_mm_shuffle_ps(ri1, ri1, 0b0'10'11'00'01), inv_even);

		_mm_store_ps(dst,
			_mm_add_ps(
				_mm_load_ps(dst),
				_mm_add_ps(_mm_mul_ps(ri1, rr2), _mm_mul_ps(nir1, ii2))
			)
		);

		dst += 4;
	}

	// patch DC and Nyquist values
	// first two values are real DC and fsc*0.5, rest are complex
	dst -= 4*N2;
	src1 -= 4*N2;
	src2 -= 4*N2;

	const __m128 zero = _mm_setzero_ps();
	_mm_storel_pi((__m64 *)dst,
		_mm_add_ps(
			_mm_loadl_pi(zero, (const __m64 *)dst),
			_mm_mul_ps(
				_mm_loadl_pi(zero, (const __m64 *)src1),
				_mm_loadl_pi(zero, (const __m64 *)src2)
			)
		)
	);
}

/////////////////////////////////////////////////////////////////////////////

void ATFFT_IMDCT_PreTransform_SSE2(float *dst, const float *src, const float *w, size_t N) {
	size_t N2 = N/2;

	const float *VDRESTRICT w1 = w;
	const float *VDRESTRICT w2 = w + N - 8;

	const float *VDRESTRICT src1 = src;
	const float *VDRESTRICT src2 = src + N - 8;

	float *VDRESTRICT dst1 = dst;
	float *VDRESTRICT dst2 = dst + N - 8;

	for(size_t i = 0; i < N2; i += 8) {
		const __m128 wr1 = _mm_load_ps(w1  );
		const __m128 wi1 = _mm_load_ps(w1+4);
		const __m128 wr2 = _mm_load_ps(w2  );
		const __m128 wi2 = _mm_load_ps(w2+4);
		w1 += 8;
		w2 -= 8;

		__m128 i6r1i7r0 = _mm_loadu_ps(&src1[0]);
		__m128 i4r3i5r2 = _mm_loadu_ps(&src1[4]);
		__m128 i2r5i3r4 = _mm_loadu_ps(&src2[0]);
		__m128 i0r7i1r6 = _mm_loadu_ps(&src2[4]);
		src1 += 8;
		src2 -= 8;

		__m128 r3210 = _mm_shuffle_ps(i6r1i7r0, i4r3i5r2, 0b10001000);
		__m128 i3210 = _mm_shuffle_ps(i0r7i1r6, i2r5i3r4, 0b01110111);

		__m128 i7654 = _mm_shuffle_ps(i4r3i5r2, i6r1i7r0, 0b01110111);
		__m128 r7654 = _mm_shuffle_ps(i2r5i3r4, i0r7i1r6, 0b10001000);

		__m128 fr3210 = _mm_sub_ps(_mm_mul_ps(r3210, wr1), _mm_mul_ps(i3210, wi1));
		__m128 fi3210 = _mm_add_ps(_mm_mul_ps(r3210, wi1), _mm_mul_ps(i3210, wr1));
		__m128 fr7654 = _mm_sub_ps(_mm_mul_ps(r7654, wr2), _mm_mul_ps(i7654, wi2));
		__m128 fi7654 = _mm_add_ps(_mm_mul_ps(r7654, wi2), _mm_mul_ps(i7654, wr2));

		_mm_store_ps(&dst1[0], _mm_unpacklo_ps(fr3210, fi3210));
		_mm_store_ps(&dst1[4], _mm_unpackhi_ps(fr3210, fi3210));
		_mm_store_ps(&dst2[0], _mm_unpacklo_ps(fr7654, fi7654));
		_mm_store_ps(&dst2[4], _mm_unpackhi_ps(fr7654, fi7654));
		dst1 += 8;
		dst2 -= 8;
	}
}

void ATFFT_IMDCT_PostTransform_SSE2(float *dst, const float *src, const float *w, size_t N) {
	// generate 3rd and 2nd quarters of full IMDCT output, in that order
	const size_t N2 = N/2;

	const float *VDRESTRICT src1 = src;
	const float *VDRESTRICT src2 = src + N - 8;
	const float *VDRESTRICT w1 = w;
	const float *VDRESTRICT w2 = w1 + N - 8;
	float *VDRESTRICT dst1 = dst + N2 - 8;
	float *VDRESTRICT dst2 = dst + N2;

	for(size_t i = 0; i < N2; i += 8) {
		const __m128 w1r = _mm_load_ps(&w1[0]);
		const __m128 w1i = _mm_load_ps(&w1[4]);
		const __m128 w2r = _mm_load_ps(&w2[0]);
		const __m128 w2i = _mm_load_ps(&w2[4]);
		w1 += 8;
		w2 -= 8;

		__m128 r1 = _mm_load_ps(&src1[0]);
		__m128 i1 = _mm_load_ps(&src1[4]);
		__m128 r2 = _mm_load_ps(&src2[0]);
		__m128 i2 = _mm_load_ps(&src2[4]);
		src1 += 8;
		src2 -= 8;

		__m128 r3 = _mm_sub_ps(_mm_mul_ps(i1, w1i), _mm_mul_ps(r1, w1r));	// negated
		__m128 i3 = _mm_add_ps(_mm_mul_ps(r1, w1i), _mm_mul_ps(i1, w1r));
		__m128 r4 = _mm_sub_ps(_mm_mul_ps(i2, w2i), _mm_mul_ps(r2, w2r));	// negated
		__m128 i4 = _mm_add_ps(_mm_mul_ps(r2, w2i), _mm_mul_ps(i2, w2r));

		r4 = _mm_shuffle_ps(r4, r4, 0b00011011);
		r3 = _mm_shuffle_ps(r3, r3, 0b00011011);

		// write third quarter
		_mm_storeu_ps(&dst1[0], _mm_unpacklo_ps(i4, r3));
		_mm_storeu_ps(&dst1[4], _mm_unpackhi_ps(i4, r3));
		dst1 -= 8;

		// write second quarter
		_mm_storeu_ps(&dst2[0], _mm_unpacklo_ps(i3, r4));
		_mm_storeu_ps(&dst2[4], _mm_unpackhi_ps(i3, r4));
		dst2 += 8;
	}
}

#endif
