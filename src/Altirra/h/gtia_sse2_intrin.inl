//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2015 Avery Lee
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

#ifndef f_GTIA_SSE2_INTRIN_INL
#define f_GTIA_SSE2_INTRIN_INL

#include <intrin.h>

void atasm_update_playfield_160_sse2(void *dst0, const uint8 *src, uint32 n) {
	// We do unaligned loads from this array, so it's important that we
	// avoid data cache unit (DCU) split penalties on older CPUs. Minimum
	// for SSSE3 is Core 2, so we can assume at least 64 byte cache lines.
	const __declspec(align(64)) uint64 window_table[6] = {
		0, 0, (uint64)0 - 1, (uint64)0 - 1, 0, 0
	};

	const __declspec(align(16)) uint64 lowbit_mask[2] = { 0x0f0f0f0f0f0f0f0f, 0x0f0f0f0f0f0f0f0f };

	// load and preshuffle color table
	const __m128i pfMask = *(const __m128i *)lowbit_mask;

	if (!n)
		return;

	char *dst = (char *)dst0;
	const uint8 *srcEnd = src + n;
		
	// check if we have a starting source offset and remove it
	uintptr startOffset = (uintptr)src & 15;

	dst -= startOffset * 2;
	src -= startOffset;

	// check if we have overlapping start and stop masks
	if (!(((uintptr)src ^ (uintptr)srcEnd) & ~(uintptr)15)) {
		ptrdiff_t startingMaskOffset = 16 - startOffset;
		ptrdiff_t endingMaskOffset = 32 - ((uintptr)srcEnd & 15);

		__m128i mask1 = _mm_loadl_epi64((const __m128i *)((const uint8 *)window_table + startingMaskOffset));
		__m128i mask2 = _mm_loadl_epi64((const __m128i *)((const uint8 *)window_table + startingMaskOffset + 8));
		__m128i mask3 = _mm_loadl_epi64((const __m128i *)((const uint8 *)window_table + endingMaskOffset));
		__m128i mask4 = _mm_loadl_epi64((const __m128i *)((const uint8 *)window_table + endingMaskOffset + 8));

		mask1 = _mm_and_si128(mask1, mask3);
		mask2 = _mm_and_si128(mask2, mask4);

		mask1 = _mm_unpacklo_epi8(mask1, mask1);
		mask2 = _mm_unpacklo_epi8(mask2, mask2);

		const __m128i anxData = *(const __m128i *)src;
		src += 16;

		const __m128i evenColorCodes = _mm_and_si128(_mm_srli_epi32(anxData, 4), pfMask);
		const __m128i  oddColorCodes = _mm_and_si128(               anxData    , pfMask);

		_mm_maskmoveu_si128(_mm_unpacklo_epi8(evenColorCodes, oddColorCodes), mask1, dst);
		_mm_maskmoveu_si128(_mm_unpackhi_epi8(evenColorCodes, oddColorCodes), mask2, dst+16);
	} else {
		// process initial oword
		if (startOffset) {
			ptrdiff_t startingMaskOffset = 16 - startOffset;

			__m128i mask1 = _mm_loadl_epi64((const __m128i *)((const uint8 *)window_table + startingMaskOffset));
			__m128i mask2 = _mm_loadl_epi64((const __m128i *)((const uint8 *)window_table + startingMaskOffset + 8));

			mask1 = _mm_unpacklo_epi8(mask1, mask1);
			mask2 = _mm_unpacklo_epi8(mask2, mask2);

			const __m128i anxData = *(const __m128i *)src;
			src += 16;

			const __m128i evenColorCodes = _mm_and_si128(_mm_srli_epi32(anxData, 4), pfMask);
			const __m128i  oddColorCodes = _mm_and_si128(               anxData    , pfMask);

			_mm_maskmoveu_si128(_mm_unpacklo_epi8(evenColorCodes, oddColorCodes), mask1, dst);
			_mm_maskmoveu_si128(_mm_unpackhi_epi8(evenColorCodes, oddColorCodes), mask2, dst+16);
			dst += 32;
		}

		// process main owords
		ptrdiff_t byteCounter = srcEnd - src - 16;

		while(byteCounter >= 0) {
			const __m128i anxData = *(const __m128i *)src;
			src += 16;

			const __m128i evenColorCodes = _mm_and_si128(_mm_srli_epi32(anxData, 4), pfMask);
			const __m128i  oddColorCodes = _mm_and_si128(               anxData    , pfMask);

			// double-up and write
			*(__m128i *)(dst + 0)  = _mm_unpacklo_epi8(evenColorCodes, oddColorCodes);
			*(__m128i *)(dst + 16) = _mm_unpackhi_epi8(evenColorCodes, oddColorCodes);
			dst += 32;

			byteCounter -= 16;
		}

		// process final oword
		byteCounter &= 15;
		if (byteCounter) {
			ptrdiff_t endingMaskOffset = 32 - byteCounter;

			__m128i mask1 = _mm_loadl_epi64((const __m128i *)((const uint8 *)window_table + endingMaskOffset));
			__m128i mask2 = _mm_loadl_epi64((const __m128i *)((const uint8 *)window_table + endingMaskOffset + 8));

			mask1 = _mm_unpacklo_epi8(mask1, mask1);
			mask2 = _mm_unpacklo_epi8(mask2, mask2);

			const __m128i anxData = *(const __m128i *)src;
			const __m128i evenColorCodes = _mm_and_si128(_mm_srli_epi32(anxData, 4), pfMask);
			const __m128i  oddColorCodes = _mm_and_si128(               anxData    , pfMask);

			_mm_maskmoveu_si128(_mm_unpacklo_epi8(evenColorCodes, oddColorCodes), mask1, dst);
			_mm_maskmoveu_si128(_mm_unpackhi_epi8(evenColorCodes, oddColorCodes), mask2, dst+16);
		}
	}

	// flush store queue
	_mm_sfence();
}

#endif
