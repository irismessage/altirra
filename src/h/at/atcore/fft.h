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

#ifndef f_AT_ATCORE_FFT_H
#define f_AT_ATCORE_FFT_H

#include <bit>

#if defined(_M_IX86) || defined(_M_X64)
#define ATFFT_USE_SSE2
#define ATFFT_USE_RADIX_4
#else
#define ATFFT_USE_NEON
#define ATFFT_USE_RADIX_4
#endif

class ATFFTBase {
protected:
	static void Init(float *wtab, int numRadix4, int numRadix2, bool useVec4, int *fwdorder, int N);

	static void ForwardImpl(float *dst, const float *src, float *work, float *wtab, int numRadix4, int numRadix2, bool useVec4, int *fwdorder, int log2N);
	static void InverseImpl(float *dst, const float *src, float *work, float *wtabend, int numRadix4, int numRadix2, bool useVec4, int *fwdorder, int log2N);
	static void MultiplyAddImpl(float *dst, const float *src1, const float *src2, int N);
};

// Fast Fourier Transform implementation.
//
// Computes the forward DFT from real data to complex frequencies (sign -1), and the
// inverse DFT from complex frequencies to real data (sign +1). The inverse transform
// is unnormalized with a scaling factor of N/2. This means that:
//
//   - ifft(fft(x)) = (N/2)*x
//   - conv(x,y) = (N/2)*ifft(fft(x)*fft(y))
//               = ifft(fft(x*(N/2))*fft(y))
//
// The frequency domain order is: X[0], X[N/2], real(X[1]), imag(X[1]).... All terms
// except the DC and Nyquist terms represent a pair of symmetric +/- frequency bins,
// which must be taken into account if measuring energy -- this is responsible for
// ifft([1...]) returning half the energy of ifft([0 0 1...]), because in the second
// case there are actually a pair X[1] and X[-1] bins set to 1.
//
// Input and output arrays do not have to be aligned, though it is slightly better if
// they are aligned to 16. It is much more important for the FFT object itself to be
// aligned (natural alignment is 64 for cache line alignment).
//
template<int N>
class ATFFT : public ATFFTBase {
	static_assert(N >= 16 && N <= 8192 && (N & (N - 1)) == 0);

public:
	ATFFT() {
		ATFFTBase::Init(wtab2, kNumStagesRadix4, kNumStagesRadix2, kUseVec, fwdorder, N);
	}

	// Forward transform from time to frequency domain. This is a real-to-complex (r2c)
	// in-place transform. The first two output values are real X[0] and X[N/2], followed by
	// pairs of real/imag for X[1], X[2]... X[N/2-1]. This just passes the same pointer
	// to the out of place version.
	void Forward(float *x) {
		Forward(x, x);
	}

	// Forward transform, out of place.
	void Forward(float *dst, const float *src) {
		ForwardImpl(dst, src, work, wtab2, kNumStagesRadix4, kNumStagesRadix2, kUseVec, fwdorder, kSizeBits);
	}

	// Inverse transform from frequency to time domain. This is a complex-to-real (c2r)
	// in-place transform. The first two input values are real X[0] and X[N/2], followed by
	// pairs of real/imag for X[1], X[2]... X[N/2-1]. This just passes the same pointer
	// to the out of place version.
	void Inverse(float *x) {
		Inverse(x, x);
	}

	// Inverse transform, out of place.
	void Inverse(float *dst, const float *src) {
		InverseImpl(dst, src, work, std::end(wtab2), kNumStagesRadix4, kNumStagesRadix2, kUseVec, fwdorder, kSizeBits);
	}

	void MultiplyAdd(float *dst, const float *src1, const float *src2) {
		MultiplyAddImpl(dst, src1, src2, N);
	}

private:
	static constexpr int kSizeBits = std::countr_zero((unsigned)N);

#if defined(ATFFT_USE_SSE2) || defined(ATFFT_USE_NEON)
	static constexpr bool kUseVec = true;
#else
	static constexpr bool kUseVec = false;
#endif

#ifdef ATFFT_USE_RADIX_4
	static constexpr int kNumStagesRadix4 = (kSizeBits - 4) >> 1;
#else
	static constexpr int kNumStagesRadix4 = 0;
#endif

	static constexpr int kNumStagesRadix2 = kSizeBits - 4 - kNumStagesRadix4 * 2;

	// Each radix-4 stage has 6 elements per twiddle factor, of which there are 8
	// at the first stage.
	static constexpr int kWTabSizeRadix4 = ((1 << (2*kNumStagesRadix4)) - 1) * 48 / 3;

	// Each radix-2 stage has 2 elements per twiddle factor, of which there are 8
	// at the first stage.
	static constexpr int kWTabSizeRadix2 = ((1 << (2*kNumStagesRadix4 + kNumStagesRadix2)) - (1 << (2*kNumStagesRadix4))) * 16;

	// This contains the twiddle factors for the forward transform in DIT order,
	// followed by the twiddles for the C2R/R2C conversion (N). They are combined
	// so as to avoid a zero-length array if no radix-2/4 stages are needed.
	alignas(64)
	float wtab2[kWTabSizeRadix4 + kWTabSizeRadix2 + N];

	int fwdorder[N/16];
	
	alignas(64)
	float work[N];
};

#endif
