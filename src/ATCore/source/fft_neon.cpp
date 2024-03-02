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
#include <vd2/system/vdtypes.h>
#include <arm_neon.h>

template<int T_LogStep>
void ATFFT_DIT_Radix2_NEON(float *y0, const float *w4, int N) {
	constexpr int step = 1 << T_LogStep;
	float *VDRESTRICT y1 = y0;
	float *VDRESTRICT y2 = y0 + step;
	const float *VDRESTRICT w1 = w4;

	for(int i=0; i<N; i += step*2) {
		const float *VDRESTRICT w2 = w1;

		for(int j=0; j<step/8; ++j) {
			float32x4_t r0 = vld1q_f32(y1);
			float32x4_t i0 = vld1q_f32(y1+4);
			float32x4_t r1 = vld1q_f32(y2);
			float32x4_t i1 = vld1q_f32(y2+4);
			float32x4_t rw = vld1q_f32(w2);
			float32x4_t iw = vld1q_f32(w2+4);

			float32x4_t rt1 = vfmsq_f32(vmulq_f32(r1, rw), i1, iw);
			float32x4_t it1 = vfmaq_f32(vmulq_f32(r1, iw), i1, rw);

			float32x4_t r2 = vaddq_f32(r0, rt1);
			float32x4_t i2 = vaddq_f32(i0, it1);
			float32x4_t r3 = vsubq_f32(r0, rt1);
			float32x4_t i3 = vsubq_f32(i0, it1);

			vst1q_f32(y1  , r2);
			vst1q_f32(y1+4, i2);
			vst1q_f32(y2  , r3);
			vst1q_f32(y2+4, i3);

			w2 += 8;
			y1 += 8;
			y2 += 8;
		}

		y1 = y2;
		y2 += step;
	}
}

template<int T_LogStep>
void ATFFT_DIT_Radix4_NEON(float *y0, const float *w4, int N) {
	constexpr int step = 1 << T_LogStep;
	const float *VDRESTRICT w1 = w4;

	for(int i=0; i<N; i += step*4) {
		const float *VDRESTRICT w2 = w1;

		float *VDRESTRICT y1 = y0;
		float *VDRESTRICT y2 = y1 + step;
		float *VDRESTRICT y3 = y2 + step;
		float *VDRESTRICT y4 = y3 + step;

		y0 += step*4;

		for(int j=0; j<step/8; ++j) {
			float32x4_t r1  = vld1q_f32(y2);
			float32x4_t i1  = vld1q_f32(y2+4);
			float32x4_t rw1 = vld1q_f32(w2+8);		// note permutation 1 <-> 2 on twiddle
			float32x4_t iw1 = vld1q_f32(w2+12);
			float32x4_t rt1 = vfmsq_f32(vmulq_f32(r1, rw1), i1, iw1);
			float32x4_t it1 = vfmaq_f32(vmulq_f32(r1, iw1), i1, rw1);

			float32x4_t r0 = vld1q_f32(y1);
			float32x4_t r4 = vaddq_f32(r0 , rt1);
			float32x4_t r5 = vsubq_f32(r0 , rt1);
			float32x4_t i0 = vld1q_f32(y1+4);
			float32x4_t i4 = vaddq_f32(i0 , it1);
			float32x4_t i5 = vsubq_f32(i0 , it1);

			float32x4_t r2  = vld1q_f32(y3);
			float32x4_t i2  = vld1q_f32(y3+4);
			float32x4_t rw2 = vld1q_f32(w2);
			float32x4_t iw2 = vld1q_f32(w2+4);
			float32x4_t rt2 = vfmsq_f32(vmulq_f32(r2, rw2), i2, iw2);
			float32x4_t it2 = vfmaq_f32(vmulq_f32(r2, iw2), i2, rw2);

			float32x4_t r3  = vld1q_f32(y4);
			float32x4_t i3  = vld1q_f32(y4+4);
			float32x4_t rw3 = vld1q_f32(w2+16);
			float32x4_t iw3 = vld1q_f32(w2+20);			
			float32x4_t rt3 = vfmsq_f32(vmulq_f32(r3, rw3), i3, iw3);
			float32x4_t it3 = vfmaq_f32(vmulq_f32(r3, iw3), i3, rw3);

			float32x4_t r6 = vaddq_f32(rt2, rt3);
			float32x4_t r7 = vsubq_f32(rt2, rt3);
			float32x4_t i6 = vaddq_f32(it2, it3);
			float32x4_t i7 = vsubq_f32(it2, it3);

			float32x4_t r8 = vaddq_f32(r4, r6);
			float32x4_t i8 = vaddq_f32(i4, i6);
			float32x4_t rA = vsubq_f32(r4, r6);
			float32x4_t iA = vsubq_f32(i4, i6);

			float32x4_t r9 = vaddq_f32(r5, i7);
			float32x4_t i9 = vsubq_f32(i5, r7);
			float32x4_t rB = vsubq_f32(r5, i7);
			float32x4_t iB = vaddq_f32(i5, r7);

			vst1q_f32(y1  , r8);
			vst1q_f32(y1+4, i8);
			vst1q_f32(y2  , r9);
			vst1q_f32(y2+4, i9);
			vst1q_f32(y3  , rA);
			vst1q_f32(y3+4, iA);
			vst1q_f32(y4  , rB);
			vst1q_f32(y4+4, iB);

			w2 += 24;
			y1 += 8;
			y2 += 8;
			y3 += 8;
			y4 += 8;
		}
	}
}

void ATFFT_DIT_Radix8_NEON(float *dst0, const float *y0, const int *order0, int N) {
	float *VDRESTRICT dst = dst0;
	const float *VDRESTRICT y = y0;
	const int *VDRESTRICT order = order0;

	// sqrt(2)/2
	const int step0 = N/8;

	constexpr float r2d2 = 0.70710678118654752440084436210485f;
	uint32x4_t sign_0101 = vreinterpretq_u32_u64(vmovq_n_u64(0x80000000'00000000));

	const float twiddle_ctab[4] = { 1,  r2d2,  0, -r2d2 };
	const float twiddle_stab[4] = { 0, -r2d2, -1, -r2d2 };
	float32x4_t twiddle_c = vld1q_f32(twiddle_ctab);
	float32x4_t twiddle_s = vld1q_f32(twiddle_stab);

	for(int i=0; i<N/16; ++i) {
		float32x4_t x04 = vcombine_f32(vld1_f32(&y[0 * step0]), vld1_f32(&y[1 * step0]));
		float32x4_t x26 = vcombine_f32(vld1_f32(&y[2 * step0]), vld1_f32(&y[3 * step0]));
		float32x4_t x15 = vcombine_f32(vld1_f32(&y[4 * step0]), vld1_f32(&y[5 * step0]));
		float32x4_t x37 = vcombine_f32(vld1_f32(&y[6 * step0]), vld1_f32(&y[7 * step0]));
		y += 2;

		// first butterfly
		float32x4_t a_r0i0r4i4 = vaddq_f32(x04, x15);
		float32x4_t a_r1i1r5i5 = vsubq_f32(x04, x15);
		float32x4_t a_r2i2r6i6 = vaddq_f32(x26, x37);
		float32x4_t a_r3i3r7i7 = vsubq_f32(x26, x37);

		// second butterfly (elements 3/7 rotated by -j)
		float32x4_t at_r3i3r7i7 = vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(vrev64q_f32(a_r3i3r7i7)), sign_0101));

		float32x4_t b_r0i0r4i4 = vaddq_f32(a_r0i0r4i4, a_r2i2r6i6);
		float32x4_t b_r1i1r5i5 = vaddq_f32(a_r1i1r5i5, at_r3i3r7i7);
		float32x4_t b_r2i2r6i6 = vsubq_f32(a_r0i0r4i4, a_r2i2r6i6);
		float32x4_t b_r3i3r7i7 = vsubq_f32(a_r1i1r5i5, at_r3i3r7i7);

		// transpose
		float32x4_t b_r0i0r1i1 = vcombine_f32(vget_low_f32 (b_r0i0r4i4), vget_low_f32 (b_r1i1r5i5));
		float32x4_t b_r2i2r3i3 = vcombine_f32(vget_low_f32 (b_r2i2r6i6), vget_low_f32 (b_r3i3r7i7));
		float32x4_t b_r4i4r5i5 = vcombine_f32(vget_high_f32(b_r0i0r4i4), vget_high_f32(b_r1i1r5i5));
		float32x4_t b_r6i6r7i7 = vcombine_f32(vget_high_f32(b_r2i2r6i6), vget_high_f32(b_r3i3r7i7));

		float32x4_t b_r0r1r2r3 = vuzp1q_f32(b_r0i0r1i1, b_r2i2r3i3);
		float32x4_t b_i0i1i2i3 = vuzp2q_f32(b_r0i0r1i1, b_r2i2r3i3);
		float32x4_t b_r4r5r6r7 = vuzp1q_f32(b_r4i4r5i5, b_r6i6r7i7);
		float32x4_t b_i4i5i6i7 = vuzp2q_f32(b_r4i4r5i5, b_r6i6r7i7);

		// third butterfly (elements 4-7 rotated by w0-w3)
		float32x4_t bt_r4r5r6r7 = vfmsq_f32(vmulq_f32(b_r4r5r6r7, twiddle_c), b_i4i5i6i7, twiddle_s);
		float32x4_t bt_i4i5i6i7 = vfmaq_f32(vmulq_f32(b_r4r5r6r7, twiddle_s), b_i4i5i6i7, twiddle_c);

		float32x4_t c_r0r1r2r3 = vaddq_f32(b_r0r1r2r3, bt_r4r5r6r7);
		float32x4_t c_i0i1i2i3 = vaddq_f32(b_i0i1i2i3, bt_i4i5i6i7);
		float32x4_t c_r4r5r6r7 = vsubq_f32(b_r0r1r2r3, bt_r4r5r6r7);
		float32x4_t c_i4i5i6i7 = vsubq_f32(b_i0i1i2i3, bt_i4i5i6i7);

		float *VDRESTRICT dst2 = &dst[*order++];
		vst1q_f32(&dst2[ 0], c_r0r1r2r3);
		vst1q_f32(&dst2[ 4], c_i0i1i2i3);
		vst1q_f32(&dst2[ 8], c_r4r5r6r7);
		vst1q_f32(&dst2[12], c_i4i5i6i7);
	}
}

void ATFFT_DIT_R2C_NEON(float *dst0, const float *src0, const float *w, int N) {
	const float *VDRESTRICT w2 = w;
	const float *VDRESTRICT src = src0;
	float *VDRESTRICT dst = dst0;

	float32x4_t half = vmovq_n_f32(0.5f);

	float32x4_t prevr1 = vmovq_n_f32(0);
	float32x4_t previ1 = vmovq_n_f32(0);

	for(int i=0; i<N/2; i += 8) {
		float32x4_t rl = vld1q_f32(&src[i]);
		float32x4_t il = vld1q_f32(&src[i+4]);
		float32x4_t rh = vld1q_f32(&src[N-i-8]);
		float32x4_t ih = vld1q_f32(&src[N-i-4]);

		if (i) {
			rh = vextq_f32(rh, vld1q_f32(&src[N-i  ]), 1);
			ih = vextq_f32(ih, vld1q_f32(&src[N-i+4]), 1);
		} else {
			rh = vextq_f32(rh, rh, 1);
			ih = vextq_f32(ih, ih, 1);
		}

		rh = vrev64q_f32(rh);
		ih = vrev64q_f32(ih);
		rh = vcombine_f32(vget_high_f32(rh), vget_low_f32(rh));
		ih = vcombine_f32(vget_high_f32(ih), vget_low_f32(ih));

		rl = vmulq_f32(rl, half);
		il = vmulq_f32(il, half);
		rh = vmulq_f32(rh, half);
		ih = vmulq_f32(ih, half);

		// Xe[n] = (Z[k] + Z*[N-k])/2
		float32x4_t rxe = vaddq_f32(rl, rh);
		float32x4_t ixe = vsubq_f32(il, ih);

		// Xo[n] = -j(Z[k] - Z*[N-k])/2 * w[k]
		float32x4_t rxo = vaddq_f32(il, ih);
		float32x4_t ixo = vsubq_f32(rh, rl);

		float32x4_t twiddlel = vld1q_f32(w2);
		float32x4_t twiddleh = vld1q_f32(w2+4);
		w2 += 8;

		// = (r3*rw - i3*iw, r3*iw + i3*rw)
		float32x4_t rw = vuzp1q_f32(twiddlel, twiddleh);
		float32x4_t iw = vuzp2q_f32(twiddlel, twiddleh);

		float32x4_t rxo2 = vfmsq_f32(vmulq_f32(rxo, rw), ixo, iw);
		float32x4_t ixo2 = vfmaq_f32(vmulq_f32(rxo, iw), ixo, rw);

		// final butterfly and conjugation
		float32x4_t r0 = vaddq_f32(rxe, rxo2);
		float32x4_t i0 = vaddq_f32(ixe, ixo2);
		float32x4_t r1 = vsubq_f32(rxe, rxo2);
		float32x4_t i1 = vsubq_f32(ixo2, ixe);

		// interleave and store low
		vst1q_f32(&dst[i+0], vzip1q_f32(r0, i0));
		vst1q_f32(&dst[i+4], vzip2q_f32(r0, i0));

		// interleave, reverse, and store high
		r1 = vrev64q_f32(r1);
		i1 = vrev64q_f32(i1);
		r1 = vcombine_f32(vget_high_f32(r1), vget_low_f32(r1));
		i1 = vcombine_f32(vget_high_f32(i1), vget_low_f32(i1));

		float32x4_t hi0 = vzip1q_f32(r1, i1);
		float32x4_t hi1 = vzip2q_f32(r1, i1);

		vst1q_f32(&dst[N-i-6], hi0);

		if (i)
			vst1q_f32(&dst[N-i-2], hi1);
		else
			vst1_f32(&dst[N-i-2], vget_low_f32(hi1));
	}

	float rz = src[0];
	float iz = src[4];
	dst[0] = rz + iz;
	dst[1] = rz - iz;

	dst[N/2] = src[N/2];
	dst[N/2+1] = -src[N/2+4];
}

void ATFFT_DIF_C2R_NEON(float *dst0, const float *x, const float *w, int N) {
	float *VDRESTRICT dst = dst0;
	
	const float32x4_t half = vmovq_n_f32(0.5f);
	const float32x4_t nhalf = vmovq_n_f32(-0.5f);
	const uint32x4_t sign = vmovq_n_u32(0x80000000);

	float32x4_t prevr1;
	float32x4_t previ1;

	{
		float32x4_t a01 = vld1q_f32(&x[0]);
		float32x4_t a23 = vld1q_f32(&x[4]);
		float32x4_t a45 = vcombine_f32(vld1_f32(&x[N-2]), vld1_f32(x));
		float32x4_t a67 = vld1q_f32(&x[N-6]);

		// deinterleave and reverse high half
		float32x4_t rl = vuzp1q_f32(a01, a23);
		float32x4_t il = vuzp2q_f32(a01, a23);
		float32x4_t rh = vrev64q_f32(vuzp1q_f32(a45, a67));
		float32x4_t ih = vrev64q_f32(vuzp2q_f32(a45, a67));

		// Xe[n] = (X[k] + X*[N/2-k])/2
		float32x4_t rxe = vaddq_f32(rl, rh);
		float32x4_t ixe = vsubq_f32(il, ih);

		// Xo[n] = j(X[k] - X*[N/2-k])/2 * w[k]
		float32x4_t rxo = vaddq_f32(il, ih);
		float32x4_t ixo = vsubq_f32(rl, rh);

		rxe = vmulq_f32(rxe, half);
		ixe = vmulq_f32(ixe, half);
		rxo = vmulq_f32(rxo, nhalf);
		ixo = vmulq_f32(ixo, half);

		float32x4_t twiddle0 = vld1q_f32(w);
		float32x4_t twiddle1 = vld1q_f32(w+4);
		w += 8;

		float32x4_t rw = vuzp1q_f32(twiddle0, twiddle1);
		float32x4_t iw = vuzp2q_f32(twiddle0, twiddle1);

		float32x4_t rxo2 = vfmaq_f32(vmulq_f32(rxo, rw), ixo, iw);
		float32x4_t ixo2 = vfmsq_f32(vmulq_f32(ixo, rw), rxo, iw);

		float32x4_t r0 = vaddq_f32(rxe, rxo2);
		float32x4_t i0 = vaddq_f32(ixe, ixo2);
		float32x4_t r1 = vsubq_f32(rxe, rxo2);
		float32x4_t i1 = vsubq_f32(ixo2, ixe);		// - for conj

		vst1q_f32(&dst[0], r0);
		vst1q_f32(&dst[4], i0);

		// re-reverse high half and stash it for pipeline
		r1 = vrev64q_f32(r1);
		i1 = vrev64q_f32(i1);
		r1 = vcombine_f32(vget_high_f32(r1), vget_low_f32(r1));
		i1 = vcombine_f32(vget_high_f32(i1), vget_low_f32(i1));

		prevr1 = r1;
		previ1 = i1;
	}

	for(int i=8; i<N/2; i += 8) {
		float32x4_t a01 = vld1q_f32(&x[i]);
		float32x4_t a23 = vld1q_f32(&x[i+4]);
		float32x4_t a45 = vld1q_f32(&x[N-i-2]);
		float32x4_t a67 = vld1q_f32(&x[N-i-6]);

		// deinterleave and reverse high half
		float32x4_t rl = vuzp1q_f32(a01, a23);
		float32x4_t il = vuzp2q_f32(a01, a23);
		float32x4_t rh = vrev64q_f32(vuzp1q_f32(a45, a67));
		float32x4_t ih = vrev64q_f32(vuzp2q_f32(a45, a67));

		// Xe[n] = (X[k] + X*[N/2-k])/2
		float32x4_t rxe = vaddq_f32(rl, rh);
		float32x4_t ixe = vsubq_f32(il, ih);

		// Xo[n] = j(X[k] - X*[N/2-k])/2 * w[k]
		float32x4_t rxo = vaddq_f32(il, ih);
		float32x4_t ixo = vsubq_f32(rl, rh);

		rxe = vmulq_f32(rxe, half);
		ixe = vmulq_f32(ixe, half);
		rxo = vmulq_f32(rxo, nhalf);
		ixo = vmulq_f32(ixo, half);

		float32x4_t twiddle0 = vld1q_f32(w);
		float32x4_t twiddle1 = vld1q_f32(w+4);
		w += 8;

		float32x4_t rw = vuzp1q_f32(twiddle0, twiddle1);
		float32x4_t iw = vuzp2q_f32(twiddle0, twiddle1);

		float32x4_t rxo2 = vfmaq_f32(vmulq_f32(rxo, rw), ixo, iw);
		float32x4_t ixo2 = vfmsq_f32(vmulq_f32(ixo, rw), rxo, iw);

		float32x4_t r0 = vaddq_f32(rxe, rxo2);
		float32x4_t i0 = vaddq_f32(ixe, ixo2);
		float32x4_t r1 = vsubq_f32(rxe, rxo2);
		float32x4_t i1 = vsubq_f32(ixo2, ixe);		// - for conj

		vst1q_f32(&dst[i], r0);
		vst1q_f32(&dst[i+4], i0);

		// re-reverse high half and rotate it for the one element shift
		r1 = vrev64q_f32(r1);
		i1 = vrev64q_f32(i1);
		r1 = vcombine_f32(vget_high_f32(r1), vget_low_f32(r1));
		i1 = vcombine_f32(vget_high_f32(i1), vget_low_f32(i1));

		vst1q_f32(&dst[N-i], vextq_f32(r1, prevr1, 3));
		vst1q_f32(&dst[N-i+4], vextq_f32(i1, previ1, 3));

		prevr1 = r1;
		previ1 = i1;
	}

	vst1q_f32(&dst[N/2], vsetq_lane_f32(x[N/2], vextq_f32(prevr1, prevr1, 3), 0));
	vst1q_f32(&dst[N/2+4], vsetq_lane_f32(-x[N/2+1], vextq_f32(previ1, previ1, 3), 0));

	dst[0] = (x[0] + x[1]) * 0.5f;
	dst[4] = (x[0] - x[1]) * 0.5f;
}


template<int T_LogStep>
void ATFFT_DIF_Radix2_NEON(float *y0, const float *w, int N) {
	constexpr int step = 1 << T_LogStep;
	float *VDRESTRICT y1 = y0;
	float *VDRESTRICT y2 = y0 + step;

	for(int i=0; i<N; i += step*2) {
		const float *VDRESTRICT w2 = w;

		for(int j=0; j<step/8; ++j) {
			float32x4_t r0 = vld1q_f32(y1);
			float32x4_t i0 = vld1q_f32(y1+4);
			float32x4_t r1 = vld1q_f32(y2);
			float32x4_t i1 = vld1q_f32(y2+4);
			float32x4_t rw = vld1q_f32(w2);
			float32x4_t iw = vld1q_f32(w2+4);

			float32x4_t r2 = vaddq_f32(r0, r1);
			float32x4_t i2 = vaddq_f32(i0, i1);
			float32x4_t r3 = vsubq_f32(r0, r1);
			float32x4_t i3 = vsubq_f32(i0, i1);

			float32x4_t rt3 = vfmaq_f32(vmulq_f32(r3, rw), i3, iw);
			float32x4_t it3 = vfmsq_f32(vmulq_f32(i3, rw), r3, iw);

			vst1q_f32(y1  , r2);
			vst1q_f32(y1+4, i2);
			vst1q_f32(y2  , rt3);
			vst1q_f32(y2+4, it3);

			w2 += 8;
			y1 += 8;
			y2 += 8;
		}

		y1 = y2;
		y2 += step;
	}
}

template<int T_LogStep>
void ATFFT_DIF_Radix4_NEON(float *y0, const float *w4, int N) {
	constexpr int step = 1 << T_LogStep;
	const float *VDRESTRICT w1 = w4;

	for(int i=0; i<N; i += step*4) {
		const float *VDRESTRICT w2 = w1;

		float *VDRESTRICT y1 = y0;
		float *VDRESTRICT y2 = y1 + step;
		float *VDRESTRICT y3 = y2 + step;
		float *VDRESTRICT y4 = y3 + step;

		y0 += step*4;

		for(int j=0; j<step/8; ++j) {
			float32x4_t r0  = vld1q_f32(y1);			float32x4_t i0  = vld1q_f32(y1+4);
			float32x4_t r1  = vld1q_f32(y2);			float32x4_t i1  = vld1q_f32(y2+4);
			float32x4_t r2  = vld1q_f32(y3);			float32x4_t i2  = vld1q_f32(y3+4);
			float32x4_t r3  = vld1q_f32(y4);			float32x4_t i3  = vld1q_f32(y4+4);

			float32x4_t r4 = vaddq_f32(r0, r2);			float32x4_t i4 = vaddq_f32(i0, i2);
			float32x4_t r5 = vaddq_f32(r1, r3);			float32x4_t i5 = vaddq_f32(i1, i3);
			float32x4_t r6 = vsubq_f32(r0, r2);			float32x4_t i6 = vsubq_f32(i0, i2);
			// *j
			float32x4_t r7 = vsubq_f32(i3, i1);			float32x4_t i7 = vsubq_f32(r1, r3);

			float32x4_t r8 = vaddq_f32(r4, r5);			float32x4_t i8 = vaddq_f32(i4, i5);
			float32x4_t r9 = vsubq_f32(r4, r5);			float32x4_t i9 = vsubq_f32(i4, i5);
			float32x4_t rA = vaddq_f32(r6, r7);			float32x4_t iA = vaddq_f32(i6, i7);
			float32x4_t rB = vsubq_f32(r6, r7);			float32x4_t iB = vsubq_f32(i6, i7);

			float32x4_t rw1 = vld1q_f32(w2+8);		// note permutation 1 <-> 2 on twiddle
			float32x4_t iw1 = vld1q_f32(w2+12);
			float32x4_t rt9 = vfmaq_f32(vmulq_f32(r9, rw1), i9, iw1);
			float32x4_t it9 = vfmsq_f32(vmulq_f32(i9, rw1), r9, iw1);

			float32x4_t rw2 = vld1q_f32(w2);
			float32x4_t iw2 = vld1q_f32(w2+4);
			float32x4_t rtA = vfmaq_f32(vmulq_f32(rA, rw2), iA, iw2);
			float32x4_t itA = vfmsq_f32(vmulq_f32(iA, rw2), rA, iw2);

			float32x4_t rw3 = vld1q_f32(w2+16);
			float32x4_t iw3 = vld1q_f32(w2+20);			
			float32x4_t rtB = vfmaq_f32(vmulq_f32(rB, rw3), iB, iw3);
			float32x4_t itB = vfmsq_f32(vmulq_f32(iB, rw3), rB, iw3);

			vst1q_f32(y1  , r8);
			vst1q_f32(y1+4, i8);
			vst1q_f32(y2  , rt9);
			vst1q_f32(y2+4, it9);
			vst1q_f32(y3  , rtA);
			vst1q_f32(y3+4, itA);
			vst1q_f32(y4  , rtB);
			vst1q_f32(y4+4, itB);

			w2 += 24;
			y1 += 8;
			y2 += 8;
			y3 += 8;
			y4 += 8;
		}
	}
}

void ATFFT_DIF_Radix8_NEON(float *dst0, const float *src0, const int *order0, int N) {
	constexpr float r2d2 = 0.70710678118654752440084436210485f;

	const int *VDRESTRICT order = order0;
	float *VDRESTRICT dst = dst0;
	const float *VDRESTRICT src = src0;

	const float twiddle_ctab[4] = { 1,  r2d2,  0, -r2d2 };
	const float twiddle_stab[4] = { 0, -r2d2, -1, -r2d2 };
	float32x4_t twiddle_c = vld1q_f32(twiddle_ctab);
	float32x4_t twiddle_s = vld1q_f32(twiddle_stab);

	const uint32_t sign_1010_tab[4] = {0x80000000, 0, 0x80000000, 0};
	uint32x4_t sign_1010 = vld1q_u32(sign_1010_tab);

	for(int i=0; i<N/16; ++i) {
		const float *VDRESTRICT src2 = &src[*order++];
		float32x4_t a_r0r1r2r3 = vld1q_f32(src2 +  0);
		float32x4_t a_i0i1i2i3 = vld1q_f32(src2 +  4);
		float32x4_t a_r4r5r6r7 = vld1q_f32(src2 +  8);
		float32x4_t a_i4i5i6i7 = vld1q_f32(src2 + 12);

		// DFT-8 stage
		float32x4_t b_r0r1r2r3 = vaddq_f32(a_r0r1r2r3, a_r4r5r6r7);
		float32x4_t b_i0i1i2i3 = vaddq_f32(a_i0i1i2i3, a_i4i5i6i7);
		float32x4_t b_r4r5r6r7 = vsubq_f32(a_r0r1r2r3, a_r4r5r6r7);
		float32x4_t b_i4i5i6i7 = vsubq_f32(a_i0i1i2i3, a_i4i5i6i7);

		// *w(1,8) = (1+j)*sqrt(2)/2
		// *w(2,8) = j
		// *w(3,8) = (-1+j)*sqrt(2)/2
		float32x4_t bt_r4r5r6r7 = vfmaq_f32(vmulq_f32(b_r4r5r6r7, twiddle_c), b_i4i5i6i7, twiddle_s);
		float32x4_t bt_i4i5i6i7 = vfmsq_f32(vmulq_f32(b_i4i5i6i7, twiddle_c), b_r4r5r6r7, twiddle_s);

		// transpose
		float32x4_t c_r0i0r1i1 = vzip1q_f32(b_r0r1r2r3, b_i0i1i2i3);
		float32x4_t c_r2i2r3i3 = vzip2q_f32(b_r0r1r2r3, b_i0i1i2i3);
		float32x4_t c_r4i4r5i5 = vzip1q_f32(bt_r4r5r6r7, bt_i4i5i6i7);
		float32x4_t c_r6i6r7i7 = vzip2q_f32(bt_r4r5r6r7, bt_i4i5i6i7);

		float32x4_t c_r0i0r4i4 = vcombine_f32(vget_low_f32 (c_r0i0r1i1), vget_low_f32 (c_r4i4r5i5));
		float32x4_t c_r1i1r5i5 = vcombine_f32(vget_high_f32(c_r0i0r1i1), vget_high_f32(c_r4i4r5i5));
		float32x4_t c_r2i2r6i6 = vcombine_f32(vget_low_f32 (c_r2i2r3i3), vget_low_f32 (c_r6i6r7i7));
		float32x4_t c_r3i3r7i7 = vcombine_f32(vget_high_f32(c_r2i2r3i3), vget_high_f32(c_r6i6r7i7));

		// DFT-4 stage
		float32x4_t d_r0i0r4i4 = vaddq_f32(c_r0i0r4i4, c_r2i2r6i6);
		float32x4_t d_r1i1r5i5 = vaddq_f32(c_r1i1r5i5, c_r3i3r7i7);
		float32x4_t d_r2i2r6i6 = vsubq_f32(c_r0i0r4i4, c_r2i2r6i6);
		float32x4_t d_r3i3r7i7 = vsubq_f32(c_r1i1r5i5, c_r3i3r7i7);

		// rotate elements 3 and 7 by +j
		d_r3i3r7i7 = vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(vrev64q_f32(d_r3i3r7i7)), sign_1010));

		// DFT-2 stage
		float32x4_t e_r0i0r4i4 = vaddq_f32(d_r0i0r4i4, d_r1i1r5i5);
		float32x4_t e_r1i1r5i5 = vsubq_f32(d_r0i0r4i4, d_r1i1r5i5);
		float32x4_t e_r2i2r6i6 = vaddq_f32(d_r2i2r6i6, d_r3i3r7i7);
		float32x4_t e_r3i3r7i7 = vsubq_f32(d_r2i2r6i6, d_r3i3r7i7);

		vst1_f32(dst + (N/8)*0, vget_low_f32 (e_r0i0r4i4));
		vst1_f32(dst + (N/8)*1, vget_high_f32(e_r0i0r4i4));
		vst1_f32(dst + (N/8)*2, vget_low_f32 (e_r2i2r6i6));
		vst1_f32(dst + (N/8)*3, vget_high_f32(e_r2i2r6i6));
		vst1_f32(dst + (N/8)*4, vget_low_f32 (e_r1i1r5i5));
		vst1_f32(dst + (N/8)*5, vget_high_f32(e_r1i1r5i5));
		vst1_f32(dst + (N/8)*6, vget_low_f32 (e_r3i3r7i7));
		vst1_f32(dst + (N/8)*7, vget_high_f32(e_r3i3r7i7));

		dst += 2;
	}
}

/////////////////////////////////////////////////////////////////////////////

void ATFFT_DIT_Radix2_NEON(float *y0, const float *w4, int N, int logStep) {
	switch(logStep) {
		case  4: ATFFT_DIT_Radix2_NEON< 4>(y0, w4, N); break;
		case  5: ATFFT_DIT_Radix2_NEON< 5>(y0, w4, N); break;
		case  6: ATFFT_DIT_Radix2_NEON< 6>(y0, w4, N); break;
		case  7: ATFFT_DIT_Radix2_NEON< 7>(y0, w4, N); break;
		case  8: ATFFT_DIT_Radix2_NEON< 8>(y0, w4, N); break;
		case  9: ATFFT_DIT_Radix2_NEON< 9>(y0, w4, N); break;
		case 10: ATFFT_DIT_Radix2_NEON<10>(y0, w4, N); break;
		case 11: ATFFT_DIT_Radix2_NEON<11>(y0, w4, N); break;
		case 12: ATFFT_DIT_Radix2_NEON<12>(y0, w4, N); break;
	}
}

void ATFFT_DIT_Radix4_NEON(float *y0, const float *w4, int N, int logStep) {
	switch(logStep) {
		case  4: ATFFT_DIT_Radix4_NEON< 4>(y0, w4, N); break;
		case  6: ATFFT_DIT_Radix4_NEON< 6>(y0, w4, N); break;
		case  8: ATFFT_DIT_Radix4_NEON< 8>(y0, w4, N); break;
		case 10: ATFFT_DIT_Radix4_NEON<10>(y0, w4, N); break;
		case 12: ATFFT_DIT_Radix4_NEON<12>(y0, w4, N); break;
	}
}

void ATFFT_DIF_Radix2_NEON(float *y0, const float *w4, int N, int logStep) {
	switch(logStep) {
		case  4: ATFFT_DIF_Radix2_NEON< 4>(y0, w4, N); break;
		case  5: ATFFT_DIF_Radix2_NEON< 5>(y0, w4, N); break;
		case  6: ATFFT_DIF_Radix2_NEON< 6>(y0, w4, N); break;
		case  7: ATFFT_DIF_Radix2_NEON< 7>(y0, w4, N); break;
		case  8: ATFFT_DIF_Radix2_NEON< 8>(y0, w4, N); break;
		case  9: ATFFT_DIF_Radix2_NEON< 9>(y0, w4, N); break;
		case 10: ATFFT_DIF_Radix2_NEON<10>(y0, w4, N); break;
		case 11: ATFFT_DIF_Radix2_NEON<11>(y0, w4, N); break;
		case 12: ATFFT_DIF_Radix2_NEON<12>(y0, w4, N); break;
	}
}

void ATFFT_DIF_Radix4_NEON(float *y0, const float *w4, int N, int logStep) {
	switch(logStep) {
		case  4: ATFFT_DIF_Radix4_NEON< 4>(y0, w4, N); break;
		case  6: ATFFT_DIF_Radix4_NEON< 6>(y0, w4, N); break;
		case  8: ATFFT_DIF_Radix4_NEON< 8>(y0, w4, N); break;
		case 10: ATFFT_DIF_Radix4_NEON<10>(y0, w4, N); break;
		case 12: ATFFT_DIF_Radix4_NEON<12>(y0, w4, N); break;
	}
}
