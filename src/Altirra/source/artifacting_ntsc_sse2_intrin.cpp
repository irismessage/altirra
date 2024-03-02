//	Altirra - Atari 800/800XL/5200 emulator
//	PAL artifacting acceleration - x86 SSE2
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

#if defined(VD_COMPILER_MSVC) && defined(VD_CPU_AMD64)

void ATArtifactNTSCAccum_SSE2(void *rout, const void *table, const void *src, uint32 count) {
	__m128i acc0 = _mm_setzero_si128();
	__m128i acc1 = acc0;
	__m128i acc2 = acc0;
	__m128i fast_impulse0;
	__m128i fast_impulse1;
	__m128i fast_impulse2;

	const __m128i * VDRESTRICT table2 = (const __m128i *)table + 8;
	char * VDRESTRICT dst = (char *)rout;
	const uint8 *VDRESTRICT src8 = (const uint8 *)src;

	count >>= 2;

	uint32 cur0123, next0123;

	do {
		cur0123 = *(const uint32 *)src8;

		if (_rotl(cur0123, 8) == cur0123)
			goto fast_path;

slow_path:
		{
			const __m128i * VDRESTRICT impulse0 = table2 + ((size_t)src8[0] << 4);
			acc0 = _mm_add_epi16(acc0, impulse0[-8]);
			acc1 = _mm_add_epi16(acc1, impulse0[-7]);
			acc2 = impulse0[-6];

			const __m128i * VDRESTRICT impulse1 = table2 + ((size_t)src8[1] << 4);
			acc0 = _mm_add_epi16(acc0, impulse1[-4]);
			acc1 = _mm_add_epi16(acc1, impulse1[-3]);
			acc2 = _mm_add_epi16(acc2, impulse1[-2]);

			const __m128i * VDRESTRICT impulse2 = table2 + ((size_t)src8[2] << 4);
			acc0 = _mm_add_epi16(acc0, impulse2[0]);
			acc1 = _mm_add_epi16(acc1, impulse2[1]);
			acc2 = _mm_add_epi16(acc2, impulse2[2]);

			const __m128i * VDRESTRICT impulse3 = table2 + ((size_t)src8[3] << 4);
			acc0 = _mm_add_epi16(acc0, impulse3[4]);
			acc1 = _mm_add_epi16(acc1, impulse3[5]);
			acc2 = _mm_add_epi16(acc2, impulse3[6]);
		}

		acc0 = _mm_srai_epi16(acc0, 5);
		acc0 = _mm_packus_epi16(acc0, acc0);
		_mm_storel_epi64((__m128i *)dst, acc0);
		dst += 8;

		acc0 = acc1;
		acc1 = acc2;

		src8 += 4;
	} while (--count);

xit:
	acc0 = _mm_srai_epi16(acc0, 5);
	acc1 = _mm_srai_epi16(acc1, 5);
	acc0 = _mm_packus_epi16(acc0, acc0);
	acc1 = _mm_packus_epi16(acc1, acc1);
	_mm_storel_epi64((__m128i *)dst, acc0);
	dst += 8;
	_mm_storel_epi64((__m128i *)dst, acc1);
	return;

fast_path_reload:
	cur0123 = next0123;
	if (cur0123 != _rotl(cur0123, 8))
		goto slow_path;

fast_path:
	{
		const __m128i *fast_impulse = table2 + ((size_t)(uint8)cur0123 << 2);

		fast_impulse0 = fast_impulse[0x1800 - 8];
		fast_impulse1 = fast_impulse[0x1800 - 7];
		fast_impulse2 = fast_impulse[0x1800 - 6];
	}

	for(int i=0; i<3; ++i) {
		src8 += 4;

		acc0 = _mm_add_epi16(acc0, fast_impulse0);
		acc1 = _mm_add_epi16(acc1, fast_impulse1);

		acc0 = _mm_srai_epi16(acc0, 5);
		acc0 = _mm_packus_epi16(acc0, acc0);
		_mm_storel_epi64((__m128i *)dst, acc0);
		dst += 8;

		acc0 = acc1;
		acc1 = fast_impulse2;

		if (!--count)
			goto xit;

		next0123 = *(const uint32 *)src8;

		if (cur0123 != next0123)
			goto fast_path_reload;
	} 

	// beyond this point, we can copy the previous four pixels indefinitely
	const __m128i repeat_pixels = _mm_loadl_epi64((const __m128i *)(dst - 8));
	for(;;) {
		_mm_storel_epi64((__m128i *)dst, repeat_pixels);
		dst += 8;
		src8 += 4;

		if (!--count)
			goto xit;

		next0123 = *(const uint32 *)src8;

		if (cur0123 != next0123)
			goto fast_path_reload;
	}
}

void ATArtifactNTSCAccumTwin_SSE2(void *rout, const void *table, const void *src, uint32 count) {
	__m128i acc0 = _mm_setzero_si128();
	__m128i acc1 = acc0;
	__m128i acc2 = acc0;

	__m128i fast_impulse0;
	__m128i fast_impulse1;
	__m128i fast_impulse2;

	char *VDRESTRICT dst = (char *)rout;
	const __m128i *VDRESTRICT impulse_table = (const __m128i *)table;
	const uint8 *VDRESTRICT src8 = (const uint8 *)src;

	count >>= 2;

	uint8 c0, c2;
	uint32 c0123;

	do {
		c0 = src8[0];
		c2 = src8[2];
		src8 += 4;

		if (c0 == c2)
			goto fast_path;

slow_path:
		{
			const __m128i * VDRESTRICT impulse0 = impulse_table + ((size_t)c0 << 3);
			const __m128i * VDRESTRICT impulse1 = impulse_table + ((size_t)c2 << 3);

			acc0 = _mm_add_epi16(acc0, impulse0[0]);
			acc1 = _mm_add_epi16(acc1, impulse0[1]);
			acc2 = impulse0[2];

			acc0 = _mm_add_epi16(acc0, impulse1[4]);
			acc1 = _mm_add_epi16(acc1, impulse1[5]);
			acc2 = _mm_add_epi16(acc2, impulse1[6]);
		}

		acc0 = _mm_srai_epi16(acc0, 5);
		acc0 = _mm_packus_epi16(acc0, acc0);
		_mm_storel_epi64((__m128i *)dst, acc0);
		dst += 8;

		acc0 = acc1;
		acc1 = acc2;
	} while(--count);

xit:
	acc0 = _mm_srai_epi16(acc0, 5);
	acc1 = _mm_srai_epi16(acc1, 5);
	acc0 = _mm_packus_epi16(acc0, acc0);
	acc1 = _mm_packus_epi16(acc1, acc1);
	_mm_storel_epi64((__m128i *)dst, acc0);
	dst += 8;
	_mm_storel_epi64((__m128i *)dst, acc1);
	return;

fast_path_reload:
	c0 = src8[0];
	c2 = src8[2];
	src8 += 4;

	if (c0 != c2)
		goto slow_path;

fast_path:
	c0123 = *(const uint32 *)(src8 - 4);

	{
		const __m128i * VDRESTRICT fast_impulse = impulse_table + ((size_t)c0 << 2);
		fast_impulse0 = fast_impulse[0x800];
		fast_impulse1 = fast_impulse[0x801];
		fast_impulse2 = fast_impulse[0x802];
	}

	for(int i=0; i<3; ++i) {
		acc0 = _mm_add_epi16(acc0, fast_impulse0);
		acc1 = _mm_add_epi16(acc1, fast_impulse1);

		acc0 = _mm_srai_epi16(acc0, 5);
		acc0 = _mm_packus_epi16(acc0, acc0);
		_mm_storel_epi64((__m128i *)dst, acc0);
		dst += 8;

		acc0 = acc1;
		acc1 = fast_impulse2;

		if (!--count)
			goto xit;

		if (*(const uint32 *)src8 != c0123)
			goto fast_path_reload;

		src8 += 4;
	}

	// beyond this point, we can copy the previous four pixels indefinitely
	const __m128i repeat_pixels = _mm_loadl_epi64((const __m128i *)(dst - 8));
	for(;;) {
		_mm_storel_epi64((__m128i *)dst, repeat_pixels);
		dst += 8;

		if (!--count)
			goto xit;

		if (*(const uint32 *)src8 != c0123)
			goto fast_path_reload;

		src8 += 4;
	}
}

#endif
