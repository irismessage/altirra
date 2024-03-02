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
