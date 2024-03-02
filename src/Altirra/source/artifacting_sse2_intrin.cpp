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

void ATArtifactNTSCFinal_SSE2(void *dst0, const void *srcr0, const void *srcg0, const void *srcb0, uint32 count) {
	const __m128i *VDRESTRICT srcr = (const __m128i *)srcr0;
	const __m128i *VDRESTRICT srcg = (const __m128i *)srcg0;
	const __m128i *VDRESTRICT srcb = (const __m128i *)srcb0;
	__m128i *VDRESTRICT dst = (__m128i *)dst0;
	uint32 n4 = count >> 2;

	{
		const __m128i red = _mm_srai_epi16(*srcr++, 5);
		const __m128i grn = _mm_srai_epi16(*srcg++, 5);
		const __m128i blu = _mm_srai_epi16(*srcb++, 5);

		const __m128i r8 = _mm_packus_epi16(red, red);
		const __m128i g8 = _mm_packus_epi16(grn, grn);
		const __m128i b8 = _mm_packus_epi16(blu, blu);

		const __m128i rb8 = _mm_unpacklo_epi8(b8, r8);
		const __m128i gg8 = _mm_unpacklo_epi8(g8, g8);

		*dst++ = _mm_unpackhi_epi8(rb8, gg8);
	}
	--n4;

	do {
		const __m128i red = _mm_srai_epi16(*srcr++, 5);
		const __m128i grn = _mm_srai_epi16(*srcg++, 5);
		const __m128i blu = _mm_srai_epi16(*srcb++, 5);

		const __m128i r8 = _mm_packus_epi16(red, red);
		const __m128i g8 = _mm_packus_epi16(grn, grn);
		const __m128i b8 = _mm_packus_epi16(blu, blu);

		const __m128i rb8 = _mm_unpacklo_epi8(b8, r8);
		const __m128i gg8 = _mm_unpacklo_epi8(g8, g8);

		*dst++ = _mm_unpacklo_epi8(rb8, gg8);
		*dst++ = _mm_unpackhi_epi8(rb8, gg8);
	} while(--n4);
}
