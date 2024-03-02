//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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

#ifdef _MSC_VER
#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma optimize("gt", on)
#endif

#include <intrin.h>

template<int Order>
void ATFLACReconstructLPC_Narrow_SSE2_Impl(sint32 *__restrict y, uint32 n, const sint32 *__restrict lpcCoeffs, int qlpShift) {
	__m128i pipe0, pipe1;
	__m128i coeff0, coeff1;
	
	// load warm-up samples and coefficients and pack to s16
	const auto loadPack = [](const void *p) {
		const __m128i *pv = (const __m128i *)p;

		return _mm_packs_epi32(pv[0], pv[1]);
	};

	const auto loaduPack = [](const void *p) {
		const __m128i *pv = (const __m128i *)p;

		return _mm_packs_epi32(_mm_loadu_si128(&pv[0]), _mm_loadu_si128(&pv[1]));
	};

	pipe0 = loaduPack(y);
	coeff0 = loadPack(lpcCoeffs);

	if constexpr (Order > 8) {
		pipe1 = loaduPack(y + 8);
		coeff1 = loadPack(lpcCoeffs + 8);
	}

	// left align the pipeline and coefficients to make insertion at the tail easier; we don't
	// care about beyond the head since it will be multiplied by 0s we're shifting into coeff0
	constexpr int offset = ((16 - Order) & 7) * 2;
	if constexpr (offset != 0) {
		if constexpr (Order > 8) {
			pipe1 = _mm_or_si128(_mm_slli_si128(pipe1, offset), _mm_srli_si128(pipe0, 16 - offset));
			coeff1 = _mm_or_si128(_mm_slli_si128(coeff1, offset), _mm_srli_si128(coeff0, 16 - offset));
		}

		pipe0 = _mm_slli_si128(pipe0, offset);
		coeff0 = _mm_slli_si128(coeff0, offset);
	}

	__m128i vshift = _mm_cvtsi32_si128(qlpShift);

	// run the pipeline -- note that we must specifically NOT using saturating adds here,
	// as we can rely on temporary overflows getting corrected back into range for the final
	// sum pre-shift
	for(uint32 i = 0; i < n; ++i) {
		// pair-wise madds, 0-7
		__m128i predv = _mm_madd_epi16(pipe0, coeff0);

		// pair-wise madds, 8-15
		if constexpr (Order > 8)
			predv = _mm_add_epi32(predv, _mm_madd_epi16(pipe1, coeff1));

		// horizontal add
		__m128 tmp = _mm_castsi128_ps(predv);

		if constexpr (Order > 4)
			predv = _mm_add_epi32(predv, _mm_castps_si128(_mm_movehl_ps(tmp, tmp)));
		else
			predv = _mm_castps_si128(_mm_movehl_ps(tmp, tmp));

		if constexpr (Order > 2)
			predv = _mm_add_epi32(predv, _mm_srli_epi64(predv, 32));
		else
			predv = _mm_srli_epi64(predv, 32);

		// apply quantization shift
		predv = _mm_sra_epi32(predv, vshift);

		// add residual
		__m128i v = _mm_add_epi32(predv, _mm_cvtsi32_si128(y[Order]));

		// write new sample
		y[Order] = _mm_cvtsi128_si32(v);
		++y;

		// shift and insert into pipeline
		if constexpr (Order > 8) {
			pipe0 = _mm_or_si128(_mm_srli_si128(pipe0, 2), _mm_slli_si128(pipe1, 14));
			pipe1 = _mm_or_si128(_mm_srli_si128(pipe1, 2), _mm_slli_si128(v, 14));
		} else {
			pipe0 = _mm_or_si128(_mm_srli_si128(pipe0, 2), _mm_slli_si128(v, 14));
		}
	}
}

template<int Order>
void ATFLACReconstructLPC_Narrow_SSSE3_Impl(sint32 *__restrict y, uint32 n, const sint32 *__restrict lpcCoeffs, int qlpShift) {
	__m128i pipe0, pipe1;
	__m128i coeff0, coeff1;
	
	// load warm-up samples and coefficients and pack to s16
	const auto loadPack = [](const void *p) {
		const __m128i *pv = (const __m128i *)p;

		return _mm_packs_epi32(pv[0], pv[1]);
	};

	const auto loaduPack = [](const void *p) {
		const __m128i *pv = (const __m128i *)p;

		return _mm_packs_epi32(_mm_loadu_si128(&pv[0]), _mm_loadu_si128(&pv[1]));
	};

	pipe0 = loaduPack(y);
	coeff0 = loadPack(lpcCoeffs);

	if constexpr (Order > 8) {
		pipe1 = loaduPack(y + 8);
		coeff1 = loadPack(lpcCoeffs + 8);
	}

	// left align the pipeline and coefficients to make insertion at the tail easier; we don't
	// care about beyond the head since it will be multiplied by 0s we're shifting into coeff0
	constexpr int offset = ((16 - Order) & 7) * 2;
	if constexpr (offset != 0) {
		if constexpr (Order > 8) {
			pipe1 = _mm_or_si128(_mm_slli_si128(pipe1, offset), _mm_srli_si128(pipe0, 16 - offset));
			coeff1 = _mm_or_si128(_mm_slli_si128(coeff1, offset), _mm_srli_si128(coeff0, 16 - offset));
		}

		pipe0 = _mm_slli_si128(pipe0, offset);
		coeff0 = _mm_slli_si128(coeff0, offset);
	}

	__m128i vshift = _mm_cvtsi32_si128(qlpShift);

	// run the pipeline -- note that we must specifically NOT using saturating adds here,
	// as we can rely on temporary overflows getting corrected back into range for the final
	// sum pre-shift
	for(uint32 i = 0; i < n; ++i) {
		// pair-wise madds, 0-7
		__m128i predv = _mm_madd_epi16(pipe0, coeff0);

		// pair-wise madds, 8-15
		if constexpr (Order > 8)
			predv = _mm_add_epi32(predv, _mm_madd_epi16(pipe1, coeff1));

		// horizontal add
		__m128 tmp = _mm_castsi128_ps(predv);

		if constexpr (Order > 4)
			predv = _mm_add_epi32(predv, _mm_castps_si128(_mm_movehl_ps(tmp, tmp)));
		else
			predv = _mm_castps_si128(_mm_movehl_ps(tmp, tmp));

		if constexpr (Order > 2)
			predv = _mm_add_epi32(predv, _mm_srli_epi64(predv, 32));
		else
			predv = _mm_srli_epi64(predv, 32);

		// apply quantization shift
		predv = _mm_sra_epi32(predv, vshift);

		// add residual
		__m128i v = _mm_add_epi32(predv, _mm_cvtsi32_si128(y[Order]));

		// write new sample
		y[Order] = _mm_cvtsi128_si32(v);
		++y;

		// shift and insert into pipeline
		if constexpr (Order > 8) {
			pipe0 = _mm_alignr_epi8(pipe1, pipe0, 2);
			pipe1 = _mm_alignr_epi8(v, pipe1, 2);
		} else {
			pipe0 = _mm_alignr_epi8(v, pipe0, 2);
		}
	}
}

void ATFLACReconstructLPC_Narrow_SSE2(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift, int order) {
	switch(order) {
		case  2: ATFLACReconstructLPC_Narrow_SSE2_Impl< 2>(y, n, lpcCoeffs, qlpShift); break;
		case  3: ATFLACReconstructLPC_Narrow_SSE2_Impl< 3>(y, n, lpcCoeffs, qlpShift); break;
		case  4: ATFLACReconstructLPC_Narrow_SSE2_Impl< 4>(y, n, lpcCoeffs, qlpShift); break;
		case  5: ATFLACReconstructLPC_Narrow_SSE2_Impl< 5>(y, n, lpcCoeffs, qlpShift); break;
		case  6: ATFLACReconstructLPC_Narrow_SSE2_Impl< 6>(y, n, lpcCoeffs, qlpShift); break;
		case  7: ATFLACReconstructLPC_Narrow_SSE2_Impl< 7>(y, n, lpcCoeffs, qlpShift); break;
		case  8: ATFLACReconstructLPC_Narrow_SSE2_Impl< 8>(y, n, lpcCoeffs, qlpShift); break;
		case  9: ATFLACReconstructLPC_Narrow_SSE2_Impl< 9>(y, n, lpcCoeffs, qlpShift); break;
		case 10: ATFLACReconstructLPC_Narrow_SSE2_Impl<10>(y, n, lpcCoeffs, qlpShift); break;
		case 11: ATFLACReconstructLPC_Narrow_SSE2_Impl<11>(y, n, lpcCoeffs, qlpShift); break;
		case 12: ATFLACReconstructLPC_Narrow_SSE2_Impl<12>(y, n, lpcCoeffs, qlpShift); break;
		case 13: ATFLACReconstructLPC_Narrow_SSE2_Impl<13>(y, n, lpcCoeffs, qlpShift); break;
		case 14: ATFLACReconstructLPC_Narrow_SSE2_Impl<14>(y, n, lpcCoeffs, qlpShift); break;
		case 15: ATFLACReconstructLPC_Narrow_SSE2_Impl<15>(y, n, lpcCoeffs, qlpShift); break;
		case 16: ATFLACReconstructLPC_Narrow_SSE2_Impl<16>(y, n, lpcCoeffs, qlpShift); break;
	}
}

void ATFLACReconstructLPC_Narrow_SSSE3(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift, int order) {
	switch(order) {
		case  2: ATFLACReconstructLPC_Narrow_SSSE3_Impl< 2>(y, n, lpcCoeffs, qlpShift); break;
		case  3: ATFLACReconstructLPC_Narrow_SSSE3_Impl< 3>(y, n, lpcCoeffs, qlpShift); break;
		case  4: ATFLACReconstructLPC_Narrow_SSSE3_Impl< 4>(y, n, lpcCoeffs, qlpShift); break;
		case  5: ATFLACReconstructLPC_Narrow_SSSE3_Impl< 5>(y, n, lpcCoeffs, qlpShift); break;
		case  6: ATFLACReconstructLPC_Narrow_SSSE3_Impl< 6>(y, n, lpcCoeffs, qlpShift); break;
		case  7: ATFLACReconstructLPC_Narrow_SSSE3_Impl< 7>(y, n, lpcCoeffs, qlpShift); break;
		case  8: ATFLACReconstructLPC_Narrow_SSSE3_Impl< 8>(y, n, lpcCoeffs, qlpShift); break;
		case  9: ATFLACReconstructLPC_Narrow_SSSE3_Impl< 9>(y, n, lpcCoeffs, qlpShift); break;
		case 10: ATFLACReconstructLPC_Narrow_SSSE3_Impl<10>(y, n, lpcCoeffs, qlpShift); break;
		case 11: ATFLACReconstructLPC_Narrow_SSSE3_Impl<11>(y, n, lpcCoeffs, qlpShift); break;
		case 12: ATFLACReconstructLPC_Narrow_SSSE3_Impl<12>(y, n, lpcCoeffs, qlpShift); break;
		case 13: ATFLACReconstructLPC_Narrow_SSSE3_Impl<13>(y, n, lpcCoeffs, qlpShift); break;
		case 14: ATFLACReconstructLPC_Narrow_SSSE3_Impl<14>(y, n, lpcCoeffs, qlpShift); break;
		case 15: ATFLACReconstructLPC_Narrow_SSSE3_Impl<15>(y, n, lpcCoeffs, qlpShift); break;
		case 16: ATFLACReconstructLPC_Narrow_SSSE3_Impl<16>(y, n, lpcCoeffs, qlpShift); break;
	}
}

VDNOINLINE uint16 ATFLACUpdateCRC16_PCMUL(uint16 crc16v, const void *buf, size_t n) {
	const uint8 *__restrict p = (const uint8 *)buf;

	__m128i endianSwap = _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);

	const auto updateSmall = [=](__m128i crc16, const uint8 *__restrict p, size_t n) -> __m128i {
		__m128i buf = _mm_setzero_si128();

		memcpy(buf.m128i_u8 + 16 - n, p, n);

		__m128i v = _mm_shuffle_epi8(buf, endianSwap);

		// table of x^8^(i+1) mod P
		static constexpr uint32 k[23] {
			0x0100, 0x8005, 0x8603, 0x8017, 0x9403, 0x807B, 0xF803, 0x8113, 0x1006, 0x8663, 0xE017, 0x9543, 0x407E, 0xFF83, 0x8102, 0x0106, 0x8605, 0x8617, 0x9417, 0x947B, 0xF87B, 0xF913, 0x1116
		};

		//    crc16*x^bits mod P
		//	= loquad(crc16)*x^bits + hiquad(crc16)*x^(bits+64) mod P
		__m128i crc16_lo = _mm_clmulepi64_si128(crc16, _mm_cvtsi32_si128(k[n-1]), 0x00);
		__m128i crc16_hi = _mm_clmulepi64_si128(crc16, _mm_cvtsi32_si128(k[n+7]), 0x01);
		return _mm_xor_si128(_mm_xor_si128(v, crc16_lo), crc16_hi);
	};

	// load CRC16 to vector reg
	__m128i crc16 = _mm_cvtsi32_si128(crc16v);

	// align the start
	const uint32 startOffset = (uint32)((uintptr_t)p & 15);
	if (startOffset) {
		uint32 preAlign = (uint32)std::min<size_t>(16 - startOffset, n);

		crc16 = updateSmall(crc16, p, preAlign);

		p += preAlign;
		n -= preAlign;
	}

	// fold 128 bits (16 bytes) at a time
	__m128i k16 = _mm_set_epi64x(0x1666, 0x0106);		// x^192 mod P, x^128 mod P
	size_t nv = n >> 4;
	while(nv--) {
		__m128i v = _mm_shuffle_epi8(*(const __m128i *)p, endianSwap);
		p += 16;

		__m128i crc16_lo = _mm_clmulepi64_si128(crc16, k16, 0x00);
		__m128i crc16_hi = _mm_clmulepi64_si128(crc16, k16, 0x11);

		crc16 = _mm_xor_si128(_mm_xor_si128(v, crc16_lo), crc16_hi);
	}

	// handle leftover bytes
	if (n & 15)
		crc16 = updateSmall(crc16, p, n & 15);

	// fold from 128-bit to 80-bit: crc16[127..64]*(x^64 mod P) + crc16[63..0]
	crc16 = _mm_xor_si128(_mm_and_si128(crc16, _mm_set_epi64x(0, -1)), _mm_clmulepi64_si128(crc16, _mm_cvtsi32_si128(0x8113), 0x01));

	// fold from 80-bit to 48-bit: crc16[79..48]*(x^48 mod P) + crc16[47..0]
	crc16 = _mm_xor_si128(crc16, _mm_clmulepi64_si128(_mm_slli_si128(crc16, 2), _mm_cvtsi32_si128(0x807B), 0x01));

	// fold from 48-bit to 32-bit: crc16[47..32]*(x^32 mod P) + crc16[15..0]
	crc16 = _mm_xor_si128(crc16, _mm_clmulepi64_si128(_mm_and_si128(_mm_srli_epi64(crc16, 32), _mm_set_epi32(0,0,0,0xFFFF)), _mm_cvtsi32_si128(0x8017), 0x00));

	// do Barrett reduction from 32-bit to 16-bit
	// T1 = (R / x^16) * (x^32/P)
	// T2 = (T1 / x^16) * P
	// C = R + T2 mod x^16
	__m128i P_mu = _mm_set_epi64x(0x18005,0x1FFFB);
	__m128i T1 = _mm_clmulepi64_si128(_mm_and_si128(_mm_srli_epi32(crc16, 16), _mm_set_epi32(0,0,0,0xFFFF)), P_mu, 0x00);
	__m128i T2 = _mm_clmulepi64_si128(_mm_srli_epi64(T1, 16), P_mu, 0x10);
	crc16 = _mm_xor_si128(crc16, T2);

	return (uint16)_mm_cvtsi128_si32(crc16);
}

#ifdef _MSC_VER
#pragma optimize("", on)
#pragma runtime_checks("", restore)
#pragma check_stack()
#endif

#endif
