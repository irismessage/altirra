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

#if VD_CPU_ARM64
#include <arm_neon.h>

void VDPixmapResolve4x_NEON(void *dst, ptrdiff_t dstpitch, const void *src, ptrdiff_t srcpitch, uint32 w, uint32 h) {
	uint8x8_t alphamask = vcreate_u8(0xFFFF000000000000ULL);

	static constexpr float scale[4] = { 1.0f / 16.0f, 1.0f / 16.0f, 1.0f / 16.0f, 1.0f / (16.0f * 255.0f) };
	float32x4_t vscale = vld1q_f32(scale);

	do {
		uint32 *__restrict dst2 = (uint32 *)dst;
		const uint8 *__restrict src2 = (const uint8 *)src;

		for(uint32 x = 0; x < w; ++x) {
			uint32x4_t sumsq = vmovq_n_u32(0);

			const uint8 *__restrict src3 = src2;
			src2 += 16;

			for(int y2=0; y2<4; ++y2) {
				uint8x16_t c = vld1q_u8(src3);
				src3 += srcpitch;

				uint8x8x2_t c0 = vzip_u8(vget_low_u8(c), vget_high_u8(c));

				uint8x8x2_t c1;
				c1.val[0] = vorr_u8(c0.val[0], alphamask);
				c1.val[1] = vorr_u8(c0.val[1], alphamask);

				uint16x8_t sq0 = vmull_u8(c0.val[0], c1.val[0]);
				uint16x8_t sq1 = vmull_u8(c0.val[1], c1.val[1]);

				sumsq = vpadalq_u16(sumsq, sq0);
				sumsq = vpadalq_u16(sumsq, sq1);
			}

			// compute average of squares
			float32x4_t sum = vmulq_f32(vcvtq_f32_u32(sumsq), vscale);

			// take sqrt of RGB, leave A
			sum = vcopyq_laneq_f32(vsqrtq_f32(sum), 3, sum, 3);

			// pack from u32x4 to u8x4
			uint32x4_t r32 = vcvtnq_u32_f32(sum);
			uint16x4_t r16 = vqmovn_u32(r32);
			uint8x8_t r8 = vqmovn_u16(vcombine_u16(r16, r16));

			dst2[x] = vget_lane_u32(vreinterpret_u32_u8(r8), 0);
		}

		dst = (char *)dst + dstpitch;
		src = (const char *)src + srcpitch * 4;
	} while(--h);
}

#endif
