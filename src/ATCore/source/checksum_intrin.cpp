//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2018 Avery Lee
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

#include <stdafx.h>

#if VD_CPU_X86 || VD_CPU_X64

#include <vd2/system/binary.h>
#include <vd2/system/cpuaccel.h>
#include <at/atcore/checksum.h>
#include <at/atcore/internal/checksum.h>
#include <intrin.h>

void ATChecksumUpdateSHA256_Scalar(ATChecksumStateSHA256& VDRESTRICT state, const void *src, size_t numBlocks) {
	using namespace nsATChecksum;

	uint32 W[64];

	const char *VDRESTRICT src2 = (const char *)src;

	while(numBlocks--) {
		memcpy(W, src2, 64);
		src2 += 64;

		for(uint32 i = 0; i < 16; ++i)
			W[i] = VDFromBE32(W[i]);

		for(uint32 i = 16; i < 64; ++i)
			W[i] = (_rotr(W[i-2], 17) ^ _rotr(W[i-2], 19) ^ (W[i-2] >> 10))
				+ W[i-7]
				+ (_rotr(W[i-15], 7) ^ _rotr(W[i-15], 18) ^ (W[i-15] >> 3))
				+ W[i-16];

		ATChecksumStateSHA256 state2(state);

		for(uint32 i = 0; i < 64; ++i) {
			uint32 T1
				= state2.H[7]
				+ _rotr(_rotr(_rotr(state2.H[4], 27) ^ state2.H[4], 18) ^ state2.H[4], 25)
				+ ((state2.H[4] & state2.H[5]) ^ (~state2.H[4] & state2.H[6])) + K[i] + W[i];

			uint32 T2
				= _rotr(_rotr(_rotr(state2.H[0], 21) ^ state2.H[0], 23) ^ state2.H[0], 22)
				+ ((state2.H[0] & state2.H[1]) ^ (state2.H[0] & state2.H[2]) ^ (state2.H[1] & state2.H[2]));

			state2.H[7] = state2.H[6];
			state2.H[6] = state2.H[5];
			state2.H[5] = state2.H[4];
			state2.H[4] = state2.H[3] + T1;
			state2.H[3] = state2.H[2];
			state2.H[2] = state2.H[1];
			state2.H[1] = state2.H[0];
			state2.H[0] = T1 + T2;
		}

		state.H[0] += state2.H[0];
		state.H[1] += state2.H[1];
		state.H[2] += state2.H[2];
		state.H[3] += state2.H[3];
		state.H[4] += state2.H[4];
		state.H[5] += state2.H[5];
		state.H[6] += state2.H[6];
		state.H[7] += state2.H[7];
	}
}

template<bool T_SSSE3>
void ATChecksumUpdateSHA256_SSE2(ATChecksumStateSHA256& VDRESTRICT state, const void *src, size_t numBlocks) {
	using namespace nsATChecksum;

	alignas(16) uint32 W[64];

	const char *VDRESTRICT src2 = (const char *)src;

	const __m128i byteSwapMask = _mm_set_epi32(0x0C0D0E0F, 0x08090A0B, 0x04050607, 0x00010203);

	while(numBlocks--) {
		__m128i v0 = _mm_loadu_si128((const __m128i *)(src2 + 0));
		__m128i v1 = _mm_loadu_si128((const __m128i *)(src2 + 16));
		__m128i v2 = _mm_loadu_si128((const __m128i *)(src2 + 32));
		__m128i v3 = _mm_loadu_si128((const __m128i *)(src2 + 48));

		if constexpr(T_SSSE3) {
			v0 = _mm_shuffle_epi8(v0, byteSwapMask);
			v1 = _mm_shuffle_epi8(v1, byteSwapMask);
			v2 = _mm_shuffle_epi8(v2, byteSwapMask);
			v3 = _mm_shuffle_epi8(v3, byteSwapMask);
		} else {
			v0 = _mm_shufflehi_epi16(_mm_shufflelo_epi16(_mm_or_si128(_mm_slli_epi16(v0, 8), _mm_srli_epi16(v0, 8)), 0xB1), 0xB1);
			v1 = _mm_shufflehi_epi16(_mm_shufflelo_epi16(_mm_or_si128(_mm_slli_epi16(v1, 8), _mm_srli_epi16(v1, 8)), 0xB1), 0xB1);
			v2 = _mm_shufflehi_epi16(_mm_shufflelo_epi16(_mm_or_si128(_mm_slli_epi16(v2, 8), _mm_srli_epi16(v2, 8)), 0xB1), 0xB1);
			v3 = _mm_shufflehi_epi16(_mm_shufflelo_epi16(_mm_or_si128(_mm_slli_epi16(v3, 8), _mm_srli_epi16(v3, 8)), 0xB1), 0xB1);
		}

		_mm_store_si128((__m128i *)&W[0], _mm_add_epi32(v0, _mm_load_si128((const __m128i *)&K[0])));
		_mm_store_si128((__m128i *)&W[4], _mm_add_epi32(v1, _mm_load_si128((const __m128i *)&K[4])));
		_mm_store_si128((__m128i *)&W[8], _mm_add_epi32(v2, _mm_load_si128((const __m128i *)&K[8])));
		_mm_store_si128((__m128i *)&W[12], _mm_add_epi32(v3, _mm_load_si128((const __m128i *)&K[12])));

		for(uint32 i = 16; i < 64; i += 4) {
			__m128i back16 = v0;
			__m128i back15;
			__m128i back7;

			if constexpr(T_SSSE3) {
				back15 = _mm_alignr_epi8(v1, v0, 4);
				back7 = _mm_alignr_epi8(v3, v2, 4);
			} else {
				back15 = _mm_or_si128(_mm_srli_si128(v0, 4), _mm_slli_si128(v1, 12));
				back7 = _mm_or_si128(_mm_srli_si128(v2, 4), _mm_slli_si128(v3, 12));
			}

			// ((W[i-15] >>> 7) ^ (W[i-15] >>> 18) ^ (W[i-15] >> 3)
			back15 = _mm_xor_si128(
				_mm_xor_si128(
					_mm_or_si128(_mm_srli_epi32(back15, 7), _mm_slli_epi32(back15, 32-7)),
					_mm_or_si128(_mm_srli_epi32(back15, 18), _mm_slli_epi32(back15, 32-18))
				),
				_mm_srli_epi32(back15, 3)
			);

			__m128i vt = _mm_add_epi32(_mm_add_epi32(back16, back7), back15);

			// 11.30 cpb
			// compute low two words of ((W[i-2] >>> 17) ^ (W[i-2] >>> 19) ^ (W[i-2] >> 10))
			__m128i back2lo = _mm_shuffle_epi32(v3, 0xFA);
			back2lo = _mm_xor_si128(_mm_xor_si128(_mm_srli_epi64(back2lo, 17), _mm_srli_epi64(back2lo, 19)), _mm_srli_epi32(back2lo, 10));
			back2lo = _mm_add_epi32(_mm_shuffle_epi32(back2lo, 0x88), vt);

			// compute high two words of ((W[i-2] >>> 17) ^ (W[i-2] >>> 19) ^ (W[i-2] >> 10))
			__m128i back2hi = _mm_shuffle_epi32(back2lo, 0x50);
			back2hi = _mm_xor_si128(_mm_xor_si128(_mm_srli_epi64(back2hi, 17), _mm_srli_epi64(back2hi, 19)), _mm_srli_epi32(back2hi, 10));
			back2hi = _mm_add_epi32(_mm_shuffle_epi32(back2hi, 0x88), vt);

			__m128i v4 = _mm_castpd_si128(_mm_move_sd(_mm_castsi128_pd(back2hi), _mm_castsi128_pd(back2lo)));

			_mm_store_si128((__m128i *)&W[i], _mm_add_epi32(v4, _mm_load_si128((const __m128i *)&K[i])));

			v0 = v1;
			v1 = v2;
			v2 = v3;
			v3 = v4;
		}

		src2 += 64;

		ATChecksumStateSHA256 state2(state);

		for(uint32 i = 0; i < 64; ++i) {
			uint32 T1
				= state2.H[7]
				+ _rotr(_rotr(_rotr(state2.H[4], 27) ^ state2.H[4], 18) ^ state2.H[4], 25)
				+ (state2.H[6] ^ ((state2.H[5] ^ state2.H[6]) & state2.H[4])) + W[i];

			uint32 T2
				= _rotr(_rotr(_rotr(state2.H[0], 21) ^ state2.H[0], 23) ^ state2.H[0], 22)
				+ ((state2.H[0] & state2.H[1]) ^ (state2.H[0] & state2.H[2]) ^ (state2.H[1] & state2.H[2]));

			state2.H[7] = state2.H[6];
			state2.H[6] = state2.H[5];
			state2.H[5] = state2.H[4];
			state2.H[4] = state2.H[3] + T1;
			state2.H[3] = state2.H[2];
			state2.H[2] = state2.H[1];
			state2.H[1] = state2.H[0];
			state2.H[0] = T1 + T2;
		}

		state.H[0] += state2.H[0];
		state.H[1] += state2.H[1];
		state.H[2] += state2.H[2];
		state.H[3] += state2.H[3];
		state.H[4] += state2.H[4];
		state.H[5] += state2.H[5];
		state.H[6] += state2.H[6];
		state.H[7] += state2.H[7];
	}
}

void ATChecksumUpdateSHA256_SHA(ATChecksumStateSHA256& VDRESTRICT state, const void *src, size_t numBlocks) {
	using namespace nsATChecksum;

	const char *VDRESTRICT src2 = (const char *)src;

	const __m128i byteSwapMask = _mm_set_epi32(0x0C0D0E0F, 0x08090A0B, 0x04050607, 0x00010203);

	// Our state array places A in H[0] and H in H[7].
	// In terms Intel's intrinsic descriptions, with (w3,w2,w1,w0), we need
	// two state vectors: a = (C,D,G,H) and b = (A,B,E,F).

	// (A,B,C,D), (E,F,G,H)
	__m128i rawstateABCD = _mm_shuffle_epi32(_mm_loadu_si128((const __m128i *)&state.H[0]), 0x1B);
	__m128i rawstateEFGH = _mm_shuffle_epi32(_mm_loadu_si128((const __m128i *)&state.H[4]), 0x1B);

	// (C,D,G,H), (A,B,E,F)
	__m128i stateCDGH = _mm_unpacklo_epi64(rawstateEFGH, rawstateABCD);
	__m128i stateABEF = _mm_unpackhi_epi64(rawstateEFGH, rawstateABCD);

	const __m128i *VDRESTRICT Kv = (const __m128i *)K;

	while(numBlocks--) {
		__m128i v0 = _mm_loadu_si128((const __m128i *)(src2 + 0));
		__m128i v1 = _mm_loadu_si128((const __m128i *)(src2 + 16));
		__m128i v2 = _mm_loadu_si128((const __m128i *)(src2 + 32));
		__m128i v3 = _mm_loadu_si128((const __m128i *)(src2 + 48));
		src2 += 64;

		v0 = _mm_shuffle_epi8(v0, byteSwapMask);
		v1 = _mm_shuffle_epi8(v1, byteSwapMask);
		v2 = _mm_shuffle_epi8(v2, byteSwapMask);
		v3 = _mm_shuffle_epi8(v3, byteSwapMask);

		__m128i istateABEF = stateABEF;
		__m128i istateCDGH = stateCDGH;
		__m128i jstateABEF = stateABEF;
		__m128i jstateCDGH = stateCDGH;
		__m128i w_k;

		w_k = _mm_add_epi32(v0, Kv[0]);
		jstateCDGH = _mm_sha256rnds2_epu32(istateCDGH, istateABEF, w_k);
		jstateABEF = _mm_sha256rnds2_epu32(istateABEF, jstateCDGH, _mm_shuffle_epi32(w_k, 0xEE));

		w_k = _mm_add_epi32(v1, Kv[1]);
		istateCDGH = _mm_sha256rnds2_epu32(jstateCDGH, jstateABEF, w_k);
		istateABEF = _mm_sha256rnds2_epu32(jstateABEF, istateCDGH, _mm_shuffle_epi32(w_k, 0xEE));

		w_k = _mm_add_epi32(v2, Kv[2]);
		jstateCDGH = _mm_sha256rnds2_epu32(istateCDGH, istateABEF, w_k);
		jstateABEF = _mm_sha256rnds2_epu32(istateABEF, jstateCDGH, _mm_shuffle_epi32(w_k, 0xEE));

		w_k = _mm_add_epi32(v3, Kv[3]);
		istateCDGH = _mm_sha256rnds2_epu32(jstateCDGH, jstateABEF, w_k);
		istateABEF = _mm_sha256rnds2_epu32(jstateABEF, istateCDGH, _mm_shuffle_epi32(w_k, 0xEE));

		for(uint32 i = 4; i < 16; i += 4) {
			// msg1 = W[i-16] + S0(W[i-15])
			// msg2 = (msg1 + W[i-7]) + S1(W[i-2])
			__m128i v4 = _mm_sha256msg2_epu32(_mm_add_epi32(_mm_alignr_epi8(v3, v2, 4), _mm_sha256msg1_epu32(v0, v1)), v3);
			__m128i w_k4 = _mm_add_epi32(v4, Kv[i]);
			v0 = v4;
			jstateCDGH = _mm_sha256rnds2_epu32(istateCDGH, istateABEF, w_k4);
			jstateABEF = _mm_sha256rnds2_epu32(istateABEF, jstateCDGH, _mm_shuffle_epi32(w_k4, 0xEE));

			__m128i v5 = _mm_sha256msg2_epu32(_mm_add_epi32(_mm_alignr_epi8(v4, v3, 4), _mm_sha256msg1_epu32(v1, v2)), v4);
			__m128i w_k5 = _mm_add_epi32(v5, Kv[i+1]);
			v1 = v5;
			istateCDGH = _mm_sha256rnds2_epu32(jstateCDGH, jstateABEF, w_k5);
			istateABEF = _mm_sha256rnds2_epu32(jstateABEF, istateCDGH, _mm_shuffle_epi32(w_k5, 0xEE));

			__m128i v6 = _mm_sha256msg2_epu32(_mm_add_epi32(_mm_alignr_epi8(v5, v4, 4), _mm_sha256msg1_epu32(v2, v3)), v5);
			__m128i w_k6 = _mm_add_epi32(v6, Kv[i+2]);
			v2 = v6;
			jstateCDGH = _mm_sha256rnds2_epu32(istateCDGH, istateABEF, w_k6);
			jstateABEF = _mm_sha256rnds2_epu32(istateABEF, jstateCDGH, _mm_shuffle_epi32(w_k6, 0xEE));

			__m128i v7 = _mm_sha256msg2_epu32(_mm_add_epi32(_mm_alignr_epi8(v6, v5, 4), _mm_sha256msg1_epu32(v3, v4)), v6);
			__m128i w_k7 = _mm_add_epi32(v7, Kv[i+3]);
			v3 = v7;
			istateCDGH = _mm_sha256rnds2_epu32(jstateCDGH, jstateABEF, w_k7);
			istateABEF = _mm_sha256rnds2_epu32(jstateABEF, istateCDGH, _mm_shuffle_epi32(w_k7, 0xEE));
		}

		stateABEF = _mm_add_epi32(stateABEF, istateABEF);
		stateCDGH = _mm_add_epi32(stateCDGH, istateCDGH);
	}

	// (D,C,B,A), (H,G,F,E)
	_mm_storeu_si128((__m128i *)&state.H[0], _mm_shuffle_epi32(_mm_unpackhi_epi64(stateCDGH, stateABEF), 0x1B));
	_mm_storeu_si128((__m128i *)&state.H[4], _mm_shuffle_epi32(_mm_unpacklo_epi64(stateCDGH, stateABEF), 0x1B));
}

void ATChecksumUpdateSHA256_Optimized(ATChecksumStateSHA256& VDRESTRICT state, const void *src, size_t numBlocks) {
	// On an i5-6200U Skylake, BCrypt built-in SHA256 hashes a 64K block
	// in 14.8 cycles/byte, using scalar code. Our routines here take 14.2 cpb with
	// SSE2 and 12.73 cpb with SSSE3, so they're competitive and a bit faster. On
	// Windows 10, BCrypt does use SHA extensions and our SHA code is only about 1%
	// faster on a Ryzen 3700X, but its also convenient to not have to hit that API.

	if (SSE2_enabled) {
		auto ext = CPUGetEnabledExtensions();

		// This code path currently works according to SDE but we're going to keep it
		// disabled until 4.0 just to be sure.
		if ((ext & (CPUF_SUPPORTS_SSSE3 | CPUF_SUPPORTS_SHA)) == (CPUF_SUPPORTS_SSSE3 | CPUF_SUPPORTS_SHA))
			ATChecksumUpdateSHA256_SHA(state, src, numBlocks);
		else if (ext & CPUF_SUPPORTS_SSSE3)
			ATChecksumUpdateSHA256_SSE2<true>(state, src, numBlocks);
		else
			ATChecksumUpdateSHA256_SSE2<false>(state, src, numBlocks);
	} else {
		ATChecksumUpdateSHA256_Scalar(state, src, numBlocks);
	}
}

#endif
