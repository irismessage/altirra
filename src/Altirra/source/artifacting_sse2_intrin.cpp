//	Altirra - Atari 800/800XL/5200 emulator
//	PAL artifacting acceleration - SSE2 intrinsics
//	Copyright (C) 2009-2011 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <intrin.h>
#include <emmintrin.h>
#include "artifacting.h"

void ATArtifactBlend_SSE2(uint32 *dst, const uint32 *src, uint32 n) {
	      __m128i *VDRESTRICT dst16 = (      __m128i *)dst;
	const __m128i *VDRESTRICT src16 = (const __m128i *)src;

	uint32 n2 = n >> 2;

	while(n2--) {
		*dst16 = _mm_avg_epu8(*dst16, *src16);
		++dst16;
		++src16;
	}
}

void ATArtifactBlendExchange_SSE2(uint32 *dst, uint32 *blendDst, uint32 n) {
	__m128i *VDRESTRICT blendDst16 = (__m128i *)blendDst;
	__m128i *VDRESTRICT dst16      = (__m128i *)dst;

	uint32 n2 = n >> 2;

	while(n2--) {
		const __m128i x = *dst16;
		const __m128i y = *blendDst16;

		*blendDst16++ = x;
		*dst16++ = _mm_avg_epu8(x, y);
	}
}

template<bool T_ExtendedRange, typename T_BlendSrc>
void ATArtifactBlendMayExchangeLinear_SSE2(uint32 *dst, T_BlendSrc *blendDst, uint32 n) {
	__m128i *VDRESTRICT blendDst16 = (__m128i *)blendDst;
	__m128i *VDRESTRICT dst16      = (__m128i *)dst;

	__m128i rmask = _mm_set1_epi32(0x00FF0000);
	__m128i gmask = _mm_set1_epi32(0x0000FF00);
	__m128i bmask = _mm_set1_epi32(0x000000FF);
	__m128 half = _mm_set1_ps(0.5f);
	__m128 rround = _mm_set1_ps(0x8000);
	__m128 ground = _mm_set1_ps(0x80);
	__m128 tiny = _mm_set1_ps(1e-8f);
	[[maybe_unused]] __m128i erBias = _mm_set1_epi8(0x40);

	uint32 n2 = n >> 2;

	while(n2--) {
		__m128i dc = *dst16;
		const __m128i sc = *blendDst16;

		// for extended range, remove the 64/255 bias and blend; this will be at half range, but
		// this is OK as our blending math is associative with scaling
		if constexpr (T_ExtendedRange) {
			dc = _mm_subs_epu8(dc, erBias);
		}

		// if we're doing a blend-and-exchange, update the blend source
		if constexpr(!std::is_const_v<T_BlendSrc>) {
			*blendDst16 = dc;
		}

		++blendDst16;

		__m128 sr = _mm_cvtepi32_ps(_mm_and_si128(sc, rmask));
		__m128 sg = _mm_cvtepi32_ps(_mm_and_si128(sc, gmask));
		__m128 sb = _mm_cvtepi32_ps(_mm_and_si128(sc, bmask));
		__m128 dr = _mm_cvtepi32_ps(_mm_and_si128(dc, rmask));
		__m128 dg = _mm_cvtepi32_ps(_mm_and_si128(dc, gmask));
		__m128 db = _mm_cvtepi32_ps(_mm_and_si128(dc, bmask));

		__m128 fr2 = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(sr, sr), _mm_mul_ps(dr, dr)), half);
		__m128 fg2 = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(sg, sg), _mm_mul_ps(dg, dg)), half);
		__m128 fb2 = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(sb, sb), _mm_mul_ps(db, db)), half);

		__m128 fr = _mm_mul_ps(_mm_rsqrt_ps(_mm_max_ps(fr2, tiny)), fr2);
		__m128 fg = _mm_mul_ps(_mm_rsqrt_ps(_mm_max_ps(fg2, tiny)), fg2);
		__m128 fb = _mm_mul_ps(_mm_rsqrt_ps(_mm_max_ps(fb2, tiny)), fb2);

		fr = _mm_min_ps(fr, _mm_set1_ps(255.0f * 65536.0f));
		fg = _mm_min_ps(fg, _mm_set1_ps(255.0f * 256.0f));
		fb = _mm_min_ps(fb, _mm_set1_ps(255.0f));

		__m128i ir = _mm_and_si128(_mm_cvttps_epi32(_mm_add_ps(fr, rround)), rmask);
		__m128i ig = _mm_and_si128(_mm_cvttps_epi32(_mm_add_ps(fg, ground)), gmask);
		__m128i ib = _mm_cvtps_epi32(fb);

		if constexpr (T_ExtendedRange)
			*dst16++ = _mm_adds_epu8(_mm_or_si128(ir, _mm_or_si128(ig, ib)), _mm_set1_epi8(0x40));
		else
			*dst16++ = _mm_or_si128(ir, _mm_or_si128(ig, ib));
	}
}

void ATArtifactBlendLinear_SSE2(uint32 *dst, const uint32 *src, uint32 n, bool extendedRange) {
	if (extendedRange)
		ATArtifactBlendMayExchangeLinear_SSE2<true>(dst, src, n);
	else
		ATArtifactBlendMayExchangeLinear_SSE2<false>(dst, src, n);
}

void ATArtifactBlendExchangeLinear_SSE2(uint32 *dst, uint32 *blendDst, uint32 n, bool extendedRange) {
	if (extendedRange)
		ATArtifactBlendMayExchangeLinear_SSE2<true>(dst, blendDst, n);
	else
		ATArtifactBlendMayExchangeLinear_SSE2<false>(dst, blendDst, n);
}

template<bool T_BlendCopy, typename T_BlendSrc>
void ATArtifactBlendMayExchangeMonoPersistence_SSE2(uint32 *dst, T_BlendSrc *blendDst, const uint32 *palette, float factor, float factor2, float limit, uint32 n) {
	__m128 *VDRESTRICT blendDst16 = (__m128 *)blendDst;
	__m128i *VDRESTRICT dst16      = (__m128i *)dst;
	const uint32 *VDRESTRICT palette2 = palette;

	__m128i bmask = _mm_set1_epi32(0x000000FF);
	__m128 tiny = _mm_set1_ps(1e-10f);
	__m128 zero = _mm_setzero_ps();
	__m128 paletteScale = _mm_set1_ps(1023);
	__m128 vf1 = _mm_set1_ps(factor);
	__m128 vf2 = _mm_set1_ps(factor2);
	__m128 vlimit = _mm_set1_ps(limit);
	__m128 vscale = _mm_set1_ps(1.0f / 255.0f);
	__m128 vpedestal = _mm_set1_ps(1.0f);

	uint32 n2 = n >> 2;
	while(n2--) {
		__m128i dc = *dst16;

		// load source and convert 8-bit fixed point to float
		__m128 v = _mm_mul_ps(_mm_cvtepi32_ps(_mm_and_si128(dc, bmask)), vscale);

		// convert gamma 2.0 to linear
		v = _mm_mul_ps(v, v);

		// apply decay blend
		__m128 phosphorEnergy;
		__m128 emission;
		if constexpr (T_BlendCopy) {
			phosphorEnergy = v;
			emission = v;
		} else {
			phosphorEnergy = _mm_min_ps(_mm_max_ps(_mm_sub_ps(*blendDst16, vpedestal), zero), vlimit);

			phosphorEnergy = _mm_add_ps(phosphorEnergy, v);
			emission = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(vf2, phosphorEnergy), vf1), phosphorEnergy);
			phosphorEnergy = _mm_sub_ps(phosphorEnergy, emission);
		}

		// if we're doing a blend-and-exchange, update the blend source
		if constexpr(!std::is_const_v<T_BlendSrc>) {
			*blendDst16 = _mm_add_ps(phosphorEnergy, vpedestal);
		}

		++blendDst16;

		// compute approximate square root
		emission = _mm_max_ps(emission, tiny);
		emission = _mm_mul_ps(emission, _mm_rsqrt_ps(emission));

		// scale to 0-1023
		emission = _mm_min_ps(_mm_mul_ps(emission, paletteScale), paletteScale);

		// convert to integer
		__m128i palIdx = _mm_cvtps_epi32(emission);

		// do palette lookup and write pixels
		((uint32 *)dst16)[0] = palette2[_mm_extract_epi16(palIdx, 0)];
		((uint32 *)dst16)[1] = palette2[_mm_extract_epi16(palIdx, 2)];
		((uint32 *)dst16)[2] = palette2[_mm_extract_epi16(palIdx, 4)];
		((uint32 *)dst16)[3] = palette2[_mm_extract_epi16(palIdx, 6)];
		++dst16;
	}
}

void ATArtifactBlendCopyMonoPersistence_SSE2(uint32 *dst, uint32 *blendDst, const uint32 *palette, float factor, float factor2, float limit, uint32 n) {
	ATArtifactBlendMayExchangeMonoPersistence_SSE2<true>(dst, blendDst, palette, factor, factor2, limit, n);
}

void ATArtifactBlendMonoPersistence_SSE2(uint32 *dst, const uint32 *src, const uint32 *palette, float factor, float factor2, float limit, uint32 n) {
	ATArtifactBlendMayExchangeMonoPersistence_SSE2<false>(dst, src, palette, factor, factor2, limit, n);
}

void ATArtifactBlendExchangeMonoPersistence_SSE2(uint32 *dst, uint32 *blendDst, const uint32 *palette, float factor, float factor2, float limit, uint32 n) {
	ATArtifactBlendMayExchangeMonoPersistence_SSE2<false>(dst, blendDst, palette, factor, factor2, limit, n);
}

template<int T_Intensity>
void ATArtifactBlendScanlinesN_SSE2(uint32 *dst0, const uint32 *src10, const uint32 *src20, uint32 n) {
	__m128i *VDRESTRICT dst = (__m128i *)dst0;
	const __m128i *VDRESTRICT src1 = (const __m128i *)src10;
	const __m128i *VDRESTRICT src2 = (const __m128i *)src20;
	const uint32 n4 = n >> 2;
	const __m128i zero = _mm_setzero_si128();

	for(uint32 i=0; i<n4; ++i) {
		if constexpr (T_Intensity == 0) {
			*dst++ = zero;
		} else {
			__m128i r = _mm_avg_epu8(*src1++, *src2++);

			if constexpr (T_Intensity == 1) {
				*dst++ = _mm_avg_epu8(_mm_avg_epu8(_mm_avg_epu8(r, zero), zero), zero);
			} else if constexpr (T_Intensity == 2) {
				*dst++ = _mm_avg_epu8(_mm_avg_epu8(r, zero), zero);
			} else if constexpr (T_Intensity == 3) {
				__m128i r4 = _mm_avg_epu8(r, zero);
				__m128i r2 = _mm_avg_epu8(r4, zero);
				*dst++ = _mm_avg_epu8(r2, r4);
			} else if constexpr (T_Intensity == 4) {
				*dst++ = _mm_avg_epu8(r, zero);
			} else if constexpr (T_Intensity == 5) {
				__m128i r4 = _mm_avg_epu8(r, zero);
				__m128i r6 = _mm_avg_epu8(r4, r);
				*dst++ = _mm_avg_epu8(r6, r4);
			} else if constexpr (T_Intensity == 6) {
				*dst++ = _mm_avg_epu8(r, _mm_avg_epu8(r, zero));
			} else if constexpr (T_Intensity == 7) {
				__m128i r4 = _mm_avg_epu8(r, zero);
				__m128i r6 = _mm_avg_epu8(r4, r);
				*dst++ = _mm_avg_epu8(r, r6);
			} else if constexpr (T_Intensity == 8) {
				*dst++ = r;
			}
		}
	}
}

void ATArtifactBlendScanlines_SSE2(uint32 *dst, const uint32 *src1, const uint32 *src2, uint32 n, float intensity) {
	int intensity8 = (int)(intensity * 8 + 0.5f);
	switch(intensity8) {
		case 1: ATArtifactBlendScanlinesN_SSE2<1>(dst, src1, src2, n); break;
		case 2: ATArtifactBlendScanlinesN_SSE2<2>(dst, src1, src2, n); break;
		case 3: ATArtifactBlendScanlinesN_SSE2<3>(dst, src1, src2, n); break;
		case 4: ATArtifactBlendScanlinesN_SSE2<4>(dst, src1, src2, n); break;
		case 5: ATArtifactBlendScanlinesN_SSE2<5>(dst, src1, src2, n); break;
		case 6: ATArtifactBlendScanlinesN_SSE2<6>(dst, src1, src2, n); break;
		case 7: ATArtifactBlendScanlinesN_SSE2<7>(dst, src1, src2, n); break;

		default:
			if (intensity < 0.5f)
				ATArtifactBlendScanlinesN_SSE2<0>(dst, src1, src2, n);
			else
				ATArtifactBlendScanlinesN_SSE2<8>(dst, src1, src2, n);
			break;
	}
}

void ATArtifactNTSCFinal_SSE2(void *dst0, const void *srcr0, const void *srcg0, const void *srcb0, uint32 count) {
	const __m128i *VDRESTRICT srcr = (const __m128i *)srcr0;
	const __m128i *VDRESTRICT srcg = (const __m128i *)srcg0;
	const __m128i *VDRESTRICT srcb = (const __m128i *)srcb0;
	__m128i *VDRESTRICT dst = (__m128i *)dst0;
	uint32 n8 = count >> 3;

	do {
		const __m128i r8 = *srcr++;
		const __m128i g8 = *srcg++;
		const __m128i b8 = *srcb++;

		const __m128i rb8a = _mm_unpacklo_epi8(b8, r8);
		const __m128i rb8b = _mm_unpackhi_epi8(b8, r8);
		const __m128i gg8a = _mm_unpacklo_epi8(g8, g8);
		const __m128i gg8b = _mm_unpackhi_epi8(g8, g8);

		*dst++ = _mm_unpacklo_epi8(rb8a, gg8a);
		*dst++ = _mm_unpackhi_epi8(rb8a, gg8a);
		*dst++ = _mm_unpacklo_epi8(rb8b, gg8b);
		*dst++ = _mm_unpackhi_epi8(rb8b, gg8b);
	} while(--n8);
}

void ATArtifactingEngine::ArtifactCompressRange_SSE2(uint32 *dst0, uint32 width) {
	__m128i *VDRESTRICT dst = (__m128i *)dst0;
	uint32 n4 = width >> 2;

	__m128i x40b = _mm_set1_epi8(0x40);

	while(n4--) {
		__m128i c = _mm_subs_epu8(*dst, x40b);

		*dst = _mm_adds_epu8(c, c);
		++dst;
	}
}

