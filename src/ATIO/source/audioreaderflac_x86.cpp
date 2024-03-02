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
	__m128i pipe0, coeff0;
	[[maybe_unused]] __m128i pipe1;
	[[maybe_unused]] __m128i coeff1;
	
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
VD_CPU_TARGET("ssse3")
void ATFLACReconstructLPC_Narrow_SSSE3_Impl(sint32 *__restrict y, uint32 n, const sint32 *__restrict lpcCoeffs, int qlpShift) {
	__m128i pipe0;
	__m128i coeff0;
	[[maybe_unused]] __m128i pipe1;
	[[maybe_unused]] __m128i coeff1;
	
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

template<int Order>
VD_CPU_TARGET("ssse3")
void ATFLACReconstructLPC_Medium_SSSE3_Impl(sint32 *__restrict y, uint32 n, const sint32 *__restrict lpcCoeffs, int qlpShift) {
	__m128i pipel0;
	__m128i pipeh0;
	__m128i coeff0;
	[[maybe_unused]] __m128i pipel1;
	[[maybe_unused]] __m128i pipeh1;
	[[maybe_unused]] __m128i coeff1;
	
	__m128i x0FFFd = _mm_set1_epi32(0x00000FFF);

	// load warm-up samples and coefficients and pack to s16
	const auto loadPack = [](const void *p) {
		const __m128i *pv = (const __m128i *)p;

		return _mm_packs_epi32(pv[0], pv[1]);
	};

	__m128i t0 = _mm_loadu_si128((const __m128i *)(y + 0));
	__m128i t1 = _mm_loadu_si128((const __m128i *)(y + 4));
	pipel0 = _mm_packs_epi32(_mm_and_si128(t0, x0FFFd), _mm_and_si128(t1, x0FFFd));
	pipeh0 = _mm_packs_epi32(_mm_srai_epi32(t0, 12), _mm_srai_epi32(t1, 12));
	coeff0 = loadPack(lpcCoeffs);

	if constexpr (Order > 8) {
		__m128i t2 = _mm_loadu_si128((const __m128i *)(y + 8));
		__m128i t3 = _mm_loadu_si128((const __m128i *)(y + 12));
		pipel1 = _mm_packs_epi32(_mm_and_si128(t2, x0FFFd), _mm_and_si128(t3, x0FFFd));
		pipeh1 = _mm_packs_epi32(_mm_srai_epi32(t2, 12), _mm_srai_epi32(t3, 12));
		coeff1 = loadPack(lpcCoeffs + 8);
	}

	// left align the pipeline and coefficients to make insertion at the tail easier; we don't
	// care about beyond the head since it will be multiplied by 0s we're shifting into coeff0
	constexpr int offset = ((16 - Order) & 7) * 2;
	if constexpr (offset != 0) {
		if constexpr (Order > 8) {
			pipel1 = _mm_or_si128(_mm_slli_si128(pipel1, offset), _mm_srli_si128(pipel0, 16 - offset));
			pipeh1 = _mm_or_si128(_mm_slli_si128(pipeh1, offset), _mm_srli_si128(pipeh0, 16 - offset));
			coeff1 = _mm_or_si128(_mm_slli_si128(coeff1, offset), _mm_srli_si128(coeff0, 16 - offset));
		}

		pipel0 = _mm_slli_si128(pipel0, offset);
		pipeh0 = _mm_slli_si128(pipeh0, offset);
		coeff0 = _mm_slli_si128(coeff0, offset);
	}

	__m128i vshift = _mm_cvtsi32_si128(qlpShift);
	__m128i ubias1 = _mm_set1_epi32(-0x7FFFFFFF - 1);
	__m128i ubias2 = _mm_set1_epi32((uint32)((UINT64_C(0x80000000) * ((1 << 12) + 1)) >> qlpShift));

	// run the pipeline -- note that we must specifically NOT using saturating adds here,
	// as we can rely on temporary overflows getting corrected back into range for the final
	// sum pre-shift
	for(uint32 i = 0; i < n; ++i) {
		// pair-wise madds, 0-7
		__m128i predvl = _mm_madd_epi16(pipel0, coeff0);
		__m128i predvh = _mm_madd_epi16(pipeh0, coeff0);

		// pair-wise madds, 8-15
		if constexpr (Order > 8) {
			predvl = _mm_add_epi32(predvl, _mm_madd_epi16(pipel1, coeff1));
			predvh = _mm_add_epi32(predvh, _mm_madd_epi16(pipeh1, coeff1));
		}

		// horizontal add
		__m128 tmpl = _mm_castsi128_ps(predvl);
		__m128 tmph = _mm_castsi128_ps(predvh);

		if constexpr (Order > 4) {
			predvl = _mm_add_epi32(predvl, _mm_castps_si128(_mm_movehl_ps(tmpl, tmpl)));
			predvh = _mm_add_epi32(predvh, _mm_castps_si128(_mm_movehl_ps(tmph, tmph)));
		} else {
			predvl = _mm_castps_si128(_mm_movehl_ps(tmpl, tmpl));
			predvh = _mm_castps_si128(_mm_movehl_ps(tmph, tmph));
		}

		if constexpr (Order > 2) {
			predvl = _mm_add_epi32(predvl, _mm_slli_epi64(predvl, 32));
			predvh = _mm_add_epi32(predvh, _mm_slli_epi64(predvh, 32));
		}

		// convert to unsigned as 64-bit arithmetic shifts aren't a thing in SSSE3
		predvl = _mm_add_epi32(predvl, ubias1);
		predvh = _mm_add_epi32(predvh, ubias1);

		// merge high-low
		__m128i predv = _mm_add_epi64(_mm_slli_epi64(_mm_srli_epi64(predvh, 32), 12), _mm_srli_epi64(predvl, 32));

		// apply quantization shift - shift is 0-31 so we don't need sign extension
		predv = _mm_srl_epi64(predv, vshift);

		// remove ubias and add residual
		__m128i v = _mm_add_epi32(predv, _mm_sub_epi32(_mm_cvtsi32_si128(y[Order]), ubias2));

		// write new sample
		y[Order] = _mm_cvtsi128_si32(v);
		++y;

		// shift and insert into pipeline
		__m128i vl = _mm_and_si128(v, x0FFFd);
		__m128i vh = _mm_srai_epi32(v, 12);
		if constexpr (Order > 8) {
			pipel0 = _mm_alignr_epi8(pipel1, pipel0, 2);
			pipeh0 = _mm_alignr_epi8(pipeh1, pipeh0, 2);
			pipel1 = _mm_alignr_epi8(vl, pipel1, 2);
			pipeh1 = _mm_alignr_epi8(vh, pipeh1, 2);
		} else {
			pipel0 = _mm_alignr_epi8(vl, pipel0, 2);
			pipeh0 = _mm_alignr_epi8(vh, pipeh0, 2);
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

void ATFLACReconstructLPC_Medium_SSSE3(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift, int order) {
	switch(order) {
		case  2: ATFLACReconstructLPC_Medium_SSSE3_Impl< 2>(y, n, lpcCoeffs, qlpShift); break;
		case  3: ATFLACReconstructLPC_Medium_SSSE3_Impl< 3>(y, n, lpcCoeffs, qlpShift); break;
		case  4: ATFLACReconstructLPC_Medium_SSSE3_Impl< 4>(y, n, lpcCoeffs, qlpShift); break;
		case  5: ATFLACReconstructLPC_Medium_SSSE3_Impl< 5>(y, n, lpcCoeffs, qlpShift); break;
		case  6: ATFLACReconstructLPC_Medium_SSSE3_Impl< 6>(y, n, lpcCoeffs, qlpShift); break;
		case  7: ATFLACReconstructLPC_Medium_SSSE3_Impl< 7>(y, n, lpcCoeffs, qlpShift); break;
		case  8: ATFLACReconstructLPC_Medium_SSSE3_Impl< 8>(y, n, lpcCoeffs, qlpShift); break;
		case  9: ATFLACReconstructLPC_Medium_SSSE3_Impl< 9>(y, n, lpcCoeffs, qlpShift); break;
		case 10: ATFLACReconstructLPC_Medium_SSSE3_Impl<10>(y, n, lpcCoeffs, qlpShift); break;
		case 11: ATFLACReconstructLPC_Medium_SSSE3_Impl<11>(y, n, lpcCoeffs, qlpShift); break;
		case 12: ATFLACReconstructLPC_Medium_SSSE3_Impl<12>(y, n, lpcCoeffs, qlpShift); break;
		case 13: ATFLACReconstructLPC_Medium_SSSE3_Impl<13>(y, n, lpcCoeffs, qlpShift); break;
		case 14: ATFLACReconstructLPC_Medium_SSSE3_Impl<14>(y, n, lpcCoeffs, qlpShift); break;
		case 15: ATFLACReconstructLPC_Medium_SSSE3_Impl<15>(y, n, lpcCoeffs, qlpShift); break;
		case 16: ATFLACReconstructLPC_Medium_SSSE3_Impl<16>(y, n, lpcCoeffs, qlpShift); break;
	}
}

VD_CPU_TARGET("pclmul,ssse3")
VDNOINLINE uint16 ATFLACUpdateCRC16_PCMUL(uint16 crc16v, const void *buf, size_t n) {
	const uint8 *__restrict p = (const uint8 *)buf;

	__m128i endianSwap = _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);

	const auto updateSmall = [=](__m128i crc16, const uint8 *__restrict p, size_t n) VD_CPU_TARGET_LAMBDA("pclmul,ssse3") -> __m128i {
		alignas(16) char buf[16] {};
		memcpy(buf + 16 - n, p, n);

		__m128i v = _mm_shuffle_epi8(_mm_load_si128((const __m128i *)buf), endianSwap);

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
