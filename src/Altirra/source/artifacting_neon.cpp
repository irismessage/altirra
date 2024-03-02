//	Altirra - Atari 800/800XL/5200 emulator
//	Artifacting acceleration - NEON intrinsics
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <arm64_neon.h>

void ATArtifactBlend_NEON(uint32 *dst, const uint32 *src, uint32 n) {
	      uint8x16_t *VDRESTRICT dst16 = (      uint8x16_t *)dst;
	const uint8x16_t *VDRESTRICT src16 = (const uint8x16_t *)src;
	uint32 n2 = n >> 2;

	while(n2--) {
		vst1q_u8(dst, vrhaddq_u8(vld1q_u8(dst), vld1q_u8(src)));
		++dst16;
		++src16;
	}
}

void ATArtifactBlendExchange_NEON(uint32 *dst, uint32 *blendDst, uint32 n) {
	uint8x16_t *VDRESTRICT blendDst16 = (uint8x16_t *)blendDst;
	uint8x16_t *VDRESTRICT dst16      = (uint8x16_t *)dst;

	uint32 n2 = n >> 2;

	while(n2--) {
		const uint8x16_t x = *dst16;
		const uint8x16_t y = *blendDst16;

		*blendDst16++ = x;
		*dst16++ = vrhaddq_u8(x, y);
	}
}

void ATArtifactBlendScanlines_NEON(uint32 *dst0, const uint32 *src10, const uint32 *src20, uint32 n) {
	uint8x16_t *VDRESTRICT dst = (uint8x16_t *)dst0;
	const uint8x16_t *VDRESTRICT src1 = (const uint8x16_t *)src10;
	const uint8x16_t *VDRESTRICT src2 = (const uint8x16_t *)src20;
	const uint32 n4 = n >> 2;
	const uint8x16_t zero = vmovq_n_u8(0);

	for(uint32 i=0; i<n4; ++i) {
		const uint8x16_t prev = *src1++;
		const uint8x16_t next = *src2++;
		uint8x16_t r = vrhaddq_u8(prev, next);

		*dst++ = vrshrq_n_u8(r, 2);
	}
}
