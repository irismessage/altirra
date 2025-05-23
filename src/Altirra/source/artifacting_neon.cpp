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
#include <arm_neon.h>

template<typename T>
void ATArtifactBlendMayExchange_NEON(uint32 *dst, T *blendDst, uint32 n) {
	using blendType = std::conditional_t<std::is_const_v<T>, const uint8x16_t, uint8x16_t>;

	blendType *VDRESTRICT blendDst16 = (blendType *)blendDst;
	uint8x16_t *VDRESTRICT dst16      = (uint8x16_t *)dst;

	uint32 n2 = n >> 2;

	while(n2--) {
		const uint8x16_t x = *dst16;
		const uint8x16_t y = *blendDst16;

		if constexpr(!std::is_const_v<T>) {
			*blendDst16 = x;
		}

		++blendDst16;

		*dst16++ = vrhaddq_u8(x, y);
	}
}

void ATArtifactBlend_NEON(uint32 *dst, const uint32 *src, uint32 n) {
	ATArtifactBlendMayExchange_NEON(dst, src, n);
}

void ATArtifactBlendExchange_NEON(uint32 *dst, uint32 *blendDst, uint32 n) {
	ATArtifactBlendMayExchange_NEON(dst, blendDst, n);
}

template<bool T_ExtendedRange, typename T>
void ATArtifactBlendMayExchangeLinear_NEON(uint32 *dst, T *blendDst, uint32 n) {
	using blendType = std::conditional_t<std::is_const_v<T>, const uint8x16_t, uint8x16_t>;

	blendType *VDRESTRICT blendDst16 = (blendType *)blendDst;
	uint8x16_t *VDRESTRICT dst16      = (uint8x16_t *)dst;

	uint32 n2 = n >> 2;

	float32x4_t tiny = vmovq_n_f32(1e-8f);

	while(n2--) {
		uint8x16_t x = *dst16;
		const uint8x16_t y = *blendDst16;

		if constexpr(T_ExtendedRange) {
			x = vqsubq_u8(x, vmovq_n_u8(0x40));
		}

		if constexpr(!std::is_const_v<T>) {
			*blendDst16 = x;
		}

		++blendDst16;

		const uint8x8_t xl = vget_low_u8(x);
		const uint8x8_t yl = vget_low_u8(y);
		const uint16x8_t w1 = vrhaddq_u16(vmull_u8(xl, xl), vmull_u8(yl, yl));
		const uint16x8_t w2 = vrhaddq_u16(vmull_high_u8(x, x), vmull_high_u8(y, y));
		const uint32x4_t d1 = vmovl_u16(vget_low_u16(w1));
		const uint32x4_t d2 = vmovl_high_u16(w1);
		const uint32x4_t d3 = vmovl_u16(vget_low_u16(w2));
		const uint32x4_t d4 = vmovl_high_u16(w2);
		const float32x4_t f1 = vcvtq_f32_u32(d1);
		const float32x4_t f2 = vcvtq_f32_u32(d2);
		const float32x4_t f3 = vcvtq_f32_u32(d3);
		const float32x4_t f4 = vcvtq_f32_u32(d4);
		const uint32x4_t r1 = vcvtnq_u32_f32(vmulq_f32(f1, vrsqrteq_f32(vmaxq_f32(f1, tiny))));
		const uint32x4_t r2 = vcvtnq_u32_f32(vmulq_f32(f2, vrsqrteq_f32(vmaxq_f32(f2, tiny))));
		const uint32x4_t r3 = vcvtnq_u32_f32(vmulq_f32(f3, vrsqrteq_f32(vmaxq_f32(f3, tiny))));
		const uint32x4_t r4 = vcvtnq_u32_f32(vmulq_f32(f4, vrsqrteq_f32(vmaxq_f32(f4, tiny))));
		const uint8x16_t rgb8 = vcombine_u8(vmovn_u16(vcombine_u16(vmovn_u32(r1), vmovn_u32(r2))), vmovn_u16(vcombine_u16(vmovn_u32(r3), vmovn_u32(r4))));
		
		if constexpr(T_ExtendedRange)
			*dst16++ = vqaddq_u8(rgb8, vmovq_n_u8(0x40));
		else
			*dst16++ = rgb8;
	}
}

void ATArtifactBlendLinear_NEON(uint32 *dst, const uint32 *src, uint32 n, bool extendedRange) {
	if (extendedRange)
		ATArtifactBlendMayExchangeLinear_NEON<true>(dst, src, n);
	else
		ATArtifactBlendMayExchangeLinear_NEON<false>(dst, src, n);
}

void ATArtifactBlendExchangeLinear_NEON(uint32 *dst, uint32 *blendDst, uint32 n, bool extendedRange) {
	if (extendedRange)
		ATArtifactBlendMayExchangeLinear_NEON<true>(dst, blendDst, n);
	else
		ATArtifactBlendMayExchangeLinear_NEON<false>(dst, blendDst, n);
}

template<bool T_BlendCopy, typename T_BlendSrc>
void ATArtifactBlendMayExchangeMonoPersistence_NEON(uint32 *dst0, T_BlendSrc *blendDst0, const uint32 *palette, float factor, float factor2, float limit, uint32 n) {
	T_BlendSrc *VDRESTRICT blendDst = blendDst0;
	uint32 *VDRESTRICT dst = dst0;
	const uint32 *VDRESTRICT palette2 = palette;

	uint32x4_t bmask = vmovq_n_u32(0x000000FF);
	float32x4_t tiny = vmovq_n_f32(1e-10f);
	float32x4_t zero = vmovq_n_f32(0);
	float32x4_t paletteScale = vmovq_n_f32(1023);
	float32x4_t vf1 = vmovq_n_f32(factor);
	float32x4_t vf2 = vmovq_n_f32(factor2);
	float32x4_t vlimit = vmovq_n_f32(limit);
	float32x4_t vscale = vmovq_n_f32(1.0f / 255.0f);
	float32x4_t vpedestal = vmovq_n_f32(1.0f);

	uint32 n2 = n >> 2;
	while(n2--) {
		uint32x4_t dc = vld1q_u32(dst);

		// load source and convert 8-bit fixed point to float
		float32x4_t v = vmulq_f32(vcvtq_f32_u32(vandq_u32(dc, bmask)), vscale);

		// convert gamma 2.0 to linear
		v = vmulq_f32(v, v);

		// apply decay blend
		float32x4_t phosphorEnergy;
		float32x4_t emission;
		if constexpr (T_BlendCopy) {
			phosphorEnergy = v;
			emission = v;
		} else {
			phosphorEnergy = vminq_f32(vmaxq_f32(vsubq_f32(vreinterpretq_f32_u32(vld1q_u32(blendDst)), vpedestal), zero), vlimit);

			phosphorEnergy = vaddq_f32(phosphorEnergy, v);
			emission = vmulq_f32(vfmaq_f32(vf2, phosphorEnergy, vf1), phosphorEnergy);
			phosphorEnergy = vsubq_f32(phosphorEnergy, emission);
		}

		// if we're doing a blend-and-exchange, update the blend source
		if constexpr(!std::is_const_v<T_BlendSrc>) {
			vst1q_u32(blendDst, vreinterpretq_u32_f32(vaddq_f32(phosphorEnergy, vpedestal)));
		}

		blendDst += 4;

		// compute approximate square root
		emission = vmaxq_f32(emission, tiny);
		emission = vmulq_f32(emission, vrsqrteq_f32(emission));

		// scale to 0-1023
		emission = vminq_f32(vmulq_f32(emission, paletteScale), paletteScale);

		// convert to integer
		uint16x8_t palIdx = vreinterpretq_u16_u32(vcvtnq_u32_f32(emission));

		// do palette lookup and write pixels
		dst[0] = palette2[vgetq_lane_u16(palIdx, 0)];
		dst[1] = palette2[vgetq_lane_u16(palIdx, 2)];
		dst[2] = palette2[vgetq_lane_u16(palIdx, 4)];
		dst[3] = palette2[vgetq_lane_u16(palIdx, 6)];
		dst += 4;
	}
}

void ATArtifactBlendCopyMonoPersistence_NEON(uint32 *dst, uint32 *blendDst, const uint32 *palette, float factor, float factor2, float limit, uint32 n) {
	ATArtifactBlendMayExchangeMonoPersistence_NEON<true>(dst, blendDst, palette, factor, factor2, limit, n);
}

void ATArtifactBlendMonoPersistence_NEON(uint32 *dst, const uint32 *src, const uint32 *palette, float factor, float factor2, float limit, uint32 n) {
	ATArtifactBlendMayExchangeMonoPersistence_NEON<false>(dst, src, palette, factor, factor2, limit, n);
}

void ATArtifactBlendExchangeMonoPersistence_NEON(uint32 *dst, uint32 *blendDst, const uint32 *palette, float factor, float factor2, float limit, uint32 n) {
	ATArtifactBlendMayExchangeMonoPersistence_NEON<false>(dst, blendDst, palette, factor, factor2, limit, n);
}

void ATArtifactBlendScanlines_NEON(uint32 *dst0, const uint32 *src10, const uint32 *src20, uint32 n, float intensity) {
	uint8x16_t *VDRESTRICT dst = (uint8x16_t *)dst0;
	const uint8x16_t *VDRESTRICT src1 = (const uint8x16_t *)src10;
	const uint8x16_t *VDRESTRICT src2 = (const uint8x16_t *)src20;
	const uint32 n4 = n >> 2;
	const uint8x16_t zero = vmovq_n_u8(0);

	const uint8x8_t scale = vdup_lane_u8(vreinterpret_u8_u32(vcvtn_u32_f32(vmov_n_f32(intensity * 128.0f))), 0);

	for(uint32 i=0; i<n4; ++i) {
		const uint8x16_t prev = *src1++;
		const uint8x16_t next = *src2++;
		uint8x16_t r = vrhaddq_u8(prev, next);

		uint8x8_t rlo = vrshrn_n_u16(vmull_u8(vget_low_u8(r), scale), 7);
		uint8x8_t rhi = vrshrn_n_u16(vmull_u8(vget_high_u8(r), scale), 7);

		*dst++ = vcombine_u8(rlo, rhi);
	}
}
