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

#include <stdafx.h>

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
#include <intrin.h>

void VDPixmapResolve4x_SSE2(void *dst, ptrdiff_t dstpitch, const void *src, ptrdiff_t srcpitch, uint32 w, uint32 h) {
	__m128i mask = _mm_set1_epi16(0x00ff);
	__m128i alphamask = _mm_set_epi16(-1, 0, 0 ,0, -1, 0, 0, 0);

	do {
		uint32 *__restrict dst2 = (uint32 *)dst;
		const char *__restrict src2 = (const char *)src;

		for(uint32 x = 0; x < w; ++x) {
			__m128i sumrb = _mm_setzero_si128();
			__m128i sumag = _mm_setzero_si128();

			const char *__restrict src3 = src2;
			for(uint32 y2 = 0; y2 < 4; ++y2) {
				__m128i c = _mm_loadu_si128((const __m128i *)src3);
				src3 += srcpitch;

				c = _mm_shufflelo_epi16(c, _MM_SHUFFLE(3, 1, 2, 0));
				c = _mm_shufflehi_epi16(c, _MM_SHUFFLE(3, 1, 2, 0));

				__m128i pxrb = _mm_and_si128(c, mask);
				__m128i pxag = _mm_srli_epi16(c, 8);

				sumrb = _mm_add_epi32(sumrb, _mm_madd_epi16(pxrb, pxrb));
				sumag = _mm_add_epi32(sumag, _mm_madd_epi16(pxag, _mm_or_si128(pxag, alphamask)));
			}

			// sumrb = r1b1r0b0
			// sumag = a1g1a0g0

			__m128i sum = _mm_add_epi32(_mm_unpacklo_epi32(sumrb, sumag), _mm_unpackhi_epi32(sumrb, sumag));
			__m128 avgf = _mm_mul_ps(_mm_cvtepi32_ps(sum), _mm_set_ps(-1.0f / 16.0f, 1.0f / 16.0f, 1.0f / 16.0f, 1.0f / 16.0f));
			__m128 avgf2 = _mm_sqrt_ps(avgf);

			avgf2 = _mm_shuffle_ps(avgf2, _mm_shuffle_ps(avgf, avgf2, _MM_SHUFFLE(3, 2, 3, 2)), _MM_SHUFFLE(1, 2, 1, 0));
			__m128i d = _mm_cvtps_epi32(avgf2);

			d = _mm_packs_epi32(d, d);
			d = _mm_packus_epi16(d, d);

			_mm_storeu_si32(dst2 + x, d);

			src2 += 16;
		}

		dst = (char *)dst + dstpitch;
		src = (const char *)src + srcpitch * 4;
	} while(--h);
}
#endif
