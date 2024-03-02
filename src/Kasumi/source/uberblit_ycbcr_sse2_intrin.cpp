//	VirtualDub - Video processing and capture application
//	Graphics support library
//	Copyright (C) 1998-2019 Avery Lee
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
#include <intrin.h>
#include "uberblit_ycbcr_sse2_intrin.h"

void VDPixmapGenRGB32ToYCbCr709_SSE2::Init(IVDPixmapGen *src, uint32 srcindex) {
	InitSource(src, srcindex);
}

void VDPixmapGenRGB32ToYCbCr709_SSE2::Start() {
	StartWindow(mWidth, 3);
}

const void *VDPixmapGenRGB32ToYCbCr709_SSE2::GetRow(sint32 y, uint32 index) {
	return (const uint8 *)VDPixmapGenWindowBasedOneSource::GetRow(y, index) + mWindowPitch * index;
}

uint32 VDPixmapGenRGB32ToYCbCr709_SSE2::GetType(uint32 output) const {
	return (mpSrc->GetType(mSrcIndex) & ~(kVDPixType_Mask | kVDPixSpace_Mask)) | kVDPixType_8 | kVDPixSpace_YCC_709;
}

void VDPixmapGenRGB32ToYCbCr709_SSE2::Compute(void *dst0, sint32 y) {
	__m128i rb_to_y  = _mm_set1_epi32(( 5983 << 16) + 2032);
	__m128i rb_to_cb = _mm_set1_epi32((-3298 << 16) + 14392);
	__m128i rb_to_cr = _mm_set1_epi32((14392 << 16) - 1320 + 65536);
	__m128i ag_to_y  = _mm_set1_epi32(20127);
	__m128i ag_to_cb = _mm_set1_epi32(-11094 + 65536);
	__m128i ag_to_cr = _mm_set1_epi32(-13073 + 65536);
	__m128i y_bias = _mm_set1_epi32(0x084000);
	__m128i c_bias = _mm_set1_epi32(0x404000);
	__m128i rb_mask = _mm_set1_epi32(0x00FF00FF);

	uint8 *VDRESTRICT dstCr = (uint8 *)dst0;
	uint8 *VDRESTRICT dstY = dstCr + mWindowPitch;
	uint8 *VDRESTRICT dstCb = dstY + mWindowPitch;
	const uint8 *VDRESTRICT srcRGB = (const uint8 *)mpSrc->GetRow(y, mSrcIndex);
	
	const sint32 w = mWidth;

	for(sint32 i = w >> 2; i; --i) {
		__m128i c = _mm_loadu_si128((const __m128i *)srcRGB);
		srcRGB += 16;

		__m128i rb = _mm_and_si128(c, rb_mask);
		__m128i ag = _mm_srli_epi16(c, 8);

		__m128i  y32 = _mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(rb, rb_to_y ), _mm_madd_epi16(ag, ag_to_y )), y_bias);
		__m128i cb32 = _mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(rb, rb_to_cb), _mm_madd_epi16(ag, ag_to_cb)), c_bias);
		__m128i cr32 = _mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(rb, rb_to_cr), _mm_madd_epi16(ag, ag_to_cr)), c_bias);

		y32 = _mm_srai_epi32(y32, 15);
		cb32 = _mm_srai_epi32(cb32, 15);
		cr32 = _mm_srai_epi32(cr32, 15);

		__m128i  y16 = _mm_packs_epi32( y32,  y32);
		__m128i cb16 = _mm_packs_epi32(cb32, cb32);
		__m128i cr16 = _mm_packs_epi32(cr32, cr32);

		_mm_storeu_si32(dstY,  _mm_packus_epi16( y16,  y16));
		_mm_storeu_si32(dstCb, _mm_packus_epi16(cb16, cb16));
		_mm_storeu_si32(dstCr, _mm_packus_epi16(cr16, cr16));

		dstY += 4;
		dstCb += 4;
		dstCr += 4;
	}

	for(sint32 i = w & 3; i; --i) {
		__m128i c = _mm_loadu_si32(srcRGB);
		srcRGB += 4;

		__m128i rb = _mm_and_si128(c, rb_mask);
		__m128i ag = _mm_srli_epi16(c, 8);

		__m128i  y32 = _mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(rb, rb_to_y ), _mm_madd_epi16(ag, ag_to_y )), y_bias);
		__m128i cb32 = _mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(rb, rb_to_cb), _mm_madd_epi16(ag, ag_to_cb)), c_bias);
		__m128i cr32 = _mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(rb, rb_to_cr), _mm_madd_epi16(ag, ag_to_cr)), c_bias);

		y32 = _mm_srai_epi32(y32, 15);
		cb32 = _mm_srai_epi32(cb32, 15);
		cr32 = _mm_srai_epi32(cr32, 15);

		*dstY ++ = (uint8)_mm_cvtsi128_si32(_mm_packus_epi16(y32, y32));
		*dstCb++ = (uint8)_mm_cvtsi128_si32(_mm_packus_epi16(cb32, cb32));
		*dstCr++ = (uint8)_mm_cvtsi128_si32(_mm_packus_epi16(cr32, cr32));
	}
}
#endif
