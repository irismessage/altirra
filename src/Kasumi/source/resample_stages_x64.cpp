//	VirtualDub - Video processing and capture application
//	Graphics support library
//	Copyright (C) 1998-2018 Avery Lee
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
#include <intrin.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/Kasumi/resample_kernels.h>
#include "resample_stages_x64.h"

extern "C" long VDCDECL vdasm_resize_table_col_SSE2(uint32 *out, const uint32 *const*in_table, const int *filter, int filter_width, uint32 w);
extern "C" long VDCDECL vdasm_resize_table_row_SSE2(uint32 *out, const uint32 *in, const int *filter, int filter_width, uint32 w, long accum, long frac);

VDResamplerSeparableTableRowStageSSE2::VDResamplerSeparableTableRowStageSSE2(const IVDResamplerFilter& filter)
	: VDResamplerRowStageSeparableTable32(filter)
{
	VDResamplerSwizzleTable(mFilterBank.data(), (uint32)mFilterBank.size() >> 1);
}

void VDResamplerSeparableTableRowStageSSE2::Process(void *dst, const void *src, uint32 w, uint32 u, uint32 dudx) {
	vdasm_resize_table_row_SSE2((uint32 *)dst, (const uint32 *)src, (const int *)mFilterBank.data(), (int)mFilterBank.size() >> 8, w, u, dudx);
}

VDResamplerSeparableTableColStageSSE2::VDResamplerSeparableTableColStageSSE2(const IVDResamplerFilter& filter)
	: VDResamplerColStageSeparableTable32(filter)
{
	VDResamplerSwizzleTable(mFilterBank.data(), (uint32)mFilterBank.size() >> 1);
}

void VDResamplerSeparableTableColStageSSE2::Process(void *dst, const void *const *src, uint32 w, sint32 phase) {
	const unsigned filtSize = (unsigned)mFilterBank.size() >> 8;

	vdasm_resize_table_col_SSE2((uint32*)dst, (const uint32 *const *)src, (const int *)mFilterBank.data() + filtSize*((phase >> 8) & 0xff), filtSize, w);
}

///////////////////////////////////////////////////////////////////////////

namespace {
	bool FilterHasNoOvershoot(const sint32 *filter, size_t n) {
		while(n--) {
			sint32 v = *filter++;

			if (v < 0 || v > 0x4000)
				return false;
		}

		return true;
	}
}

VDResamplerSeparableTableRowStage8SSE2::VDResamplerSeparableTableRowStage8SSE2(const IVDResamplerFilter& filter)
	: VDResamplerRowStageSeparableTable32(filter)
{
	mbUseFastLerp = false;
	if (CPUGetEnabledExtensions() & CPUF_SUPPORTS_SSSE3)
		mbUseFastLerp = FilterHasNoOvershoot(mFilterBank.data(), mFilterBank.size());
}

void VDResamplerSeparableTableRowStage8SSE2::Init(const VDResamplerAxis& axis, uint32 srcw) {
	mSrcWidth = srcw;

	if (axis.dx_dualclip) {
		// We don't need the row kernels in the dual clip case as the engine will call the
		// standard Process() instead. This is only for the case where the source is narrower
		// than the filter kernel, which is not worth optimizing.
		return;
	}

	const uint32 ksize = (int)mFilterBank.size() >> 8;
	const uint32 kquads = (ksize + 3) >> 2;
	const uint32 ksize4 = kquads * 4;

	if (srcw < ksize4)
		mTempBuffer.resize(ksize4, 0);
	
	mRowFilters.resize((kquads * 4 + 4) * axis.dx);

	sint16 *rowFilter = mRowFilters.data();
	memset(rowFilter, 0, mRowFilters.size() * sizeof(mRowFilters[0]));

	uint32 xstart = 0;

	// 2-tap is a special case that we can optimize. For it we encode groups of 4 destination pixels
	// with solely 2-tap filters, instead of the padded 4*N-tap filters we use for wider sizes. Any
	// remaining pixels at the end are done conventionally. We can also simplify some checks for
	// this case as 1-pixel sources are special cased as a memset(), so we'll always be able to fit
	// the 2-tap kernel within the source.
	//
	// A fast group of 4 pixels takes 32 bytes, while regular encoding for a 2-tap filter takes
	// 16 bytes, or 64 bytes for 4 pixels. This means that the fast group takes less space, and we
	// don't need to resize the row filter buffer as long as we're OK with a bit of unused space.
	//
	// When SSSE3 is available, the filter has no overshoot, and we are interpolating, we can use
	// an even faster path where we only expand to 16-bit instead of 32-bit for intermediate values
	// and use shuffling instead of gathering. In this case, the fast groups are 8 pixels wide.

	mNumFastGroups = 0;

	// We can use SSSE3 fast lerp as long as the gather doesn't have to pull more than 16 consecutive
	// samples. This works for any step factor of 2.0 or below.
	if (axis.dudx > 0x20000)
		mbUseFastLerp = false;

	if (ksize == 2) {
		if (mbUseFastLerp) {
			uint32 fastGroups = axis.dx >> 3;

			mFastLerpOffsets.resize(fastGroups);

			for(uint32 i = 0; i < fastGroups; ++i) {
				alignas(16) uint16 offsets[8];

				for(uint32 j = 0; j < 8; ++j) {
					sint32 u = axis.u + axis.dudx * (i*8 + j);
					sint32 rawSrcOffset = u >> 16;
					sint32 srcOffset = rawSrcOffset;

					const sint32 *VDRESTRICT filter = &mFilterBank[ksize * ((u >> 8) & 0xFF)];

					if (srcOffset > (sint32)srcw - 2) {
						srcOffset = (sint32)srcw - 2;
						rowFilter[8] = -0x8000;
					} else {
						uint8 f0 = (uint8)(0 - ((filter[0] + 64) >> 7));
						uint8 f1 = (uint8)(0x80 - f0);
						rowFilter[8] = (sint16)(uint16)(((uint32)f1 << 8) + f0);
					}
		
					if (srcOffset < 0) {
						srcOffset = 0;
						rowFilter[8] = 0x0080;
					}

					offsets[j] = srcOffset;

					++rowFilter;
				}

				mFastLerpOffsets[i] = offsets[0];

				__m128i absOffsets = _mm_load_si128((const __m128i *)offsets);
				__m128i relOffsets = _mm_sub_epi16(absOffsets, _mm_shuffle_epi32(_mm_shufflelo_epi16(absOffsets, 0), 0));
				__m128i shuffles = _mm_packus_epi16(relOffsets, relOffsets);

				shuffles = _mm_add_epi8(_mm_unpacklo_epi8(shuffles, shuffles), _mm_set1_epi16(0x0100));

				*(__m128i *)&rowFilter[-8] = shuffles;

				rowFilter += 8;
			}

			mNumFastGroups = fastGroups;

			xstart = fastGroups << 3;
		} else {
			uint32 fastGroups = axis.dx >> 2;

			for(uint32 i = 0; i < fastGroups; ++i) {
				for(uint32 j = 0; j < 4; ++j) {
					sint32 u = axis.u + axis.dudx * (i*4 + j);
					sint32 rawSrcOffset = u >> 16;
					sint32 srcOffset = rawSrcOffset;

					if (srcOffset > (sint32)srcw - 2)
						srcOffset = (sint32)srcw - 2;
		
					if (srcOffset < 0)
						srcOffset = 0;

					rowFilter[0] = srcOffset;

					const sint32 *VDRESTRICT filter = &mFilterBank[ksize * ((u >> 8) & 0xFF)];
					for(uint32 k = 0; k < 2; ++k) {
						sint32 tapSrcOffset = rawSrcOffset + k;

						if (tapSrcOffset < 0)
							tapSrcOffset = 0;
						if (tapSrcOffset >= (sint32)srcw)
							tapSrcOffset = (sint32)srcw - 1;

						rowFilter[8 + tapSrcOffset - srcOffset] += filter[k];
					}

					rowFilter += 2;
				}

				rowFilter += 8;
			}

			mNumFastGroups = fastGroups;

			xstart = fastGroups << 2;
		}
	}

	for(sint32 x = xstart; x < axis.dx; ++x) {
		sint32 u = axis.u + axis.dudx * x;
		sint32 rawSrcOffset = u >> 16;
		sint32 srcOffset = rawSrcOffset;

		// We need both these clamps in the case where the raw kernel fits within the source, but
		// the 4-expanded kernel doesn't. In that case, we push the kernel as far left as it can.
		// The source is copied and padded in Process() to prevent read overruns.
		if (srcOffset > (sint32)srcw - (sint32)ksize4)
			srcOffset = (sint32)srcw - (sint32)ksize4;
		
		if (srcOffset < 0)
			srcOffset = 0;

		const sint32 *VDRESTRICT filter = &mFilterBank[ksize * ((u >> 8) & 0xFF)];

		*rowFilter++ = srcOffset;
		*rowFilter++ = 0;
		*rowFilter++ = 0;
		*rowFilter++ = 0;

		for(uint32 i = 0; i < ksize; ++i) {
			sint32 tapSrcOffset = rawSrcOffset + i;

			if (tapSrcOffset < 0)
				tapSrcOffset = 0;
			if (tapSrcOffset >= (sint32)srcw)
				tapSrcOffset = (sint32)srcw - 1;

			rowFilter[tapSrcOffset - srcOffset] += filter[i];
		}

		rowFilter += kquads*4;
	}
}

void VDResamplerSeparableTableRowStage8SSE2::Process(void *dst, const void *src, uint32 w) {
	// get one degenerate case out of the way
	if (mSrcWidth == 1) {
		memset(dst, *(const uint8 *)src, w);
		return;
	}

	// if the source is narrower than the 4-padded filter kernel, copy the source to a temp buffer
	// so we can safely read over to an integral number of quads
	if (!mTempBuffer.empty()) {
		memcpy(mTempBuffer.data(), src, mSrcWidth);
		src = mTempBuffer.data();
	}

	const sint16 *VDRESTRICT rowFilter = mRowFilters.data();
	const uint32 ksize = mFilterBank.size() >> 8;
	const uint32 kquads = (ksize + 3) >> 2;
	uint8 *VDRESTRICT dst8 = (uint8 *)dst;

#if 0		// reference code
	while(w--) {
		const uint8 *VDRESTRICT src2 = (const uint8 *)src + (uint16)rowFilter[0];

		rowFilter += 4;

		sint32 accum = 0x2000;
		for(uint32 i=0; i<kquads; ++i) {
			accum += rowFilter[0] * (sint32)src2[0] + rowFilter[1] * (sint32)src2[1] + rowFilter[2] * (sint32)src2[2] + rowFilter[3] * (sint32)src2[3];
			rowFilter += 4;
			src2 += 4;
		}

		accum >>= 14;

		if (accum < 0)
			accum = 0;
		if (accum > 255)
			accum = 255;

		*dst8++ = (uint8)accum;
	}
#else
	const __m128i round = _mm_set_epi32(0, 0, 0, 0x2000);
	const __m128i zero = _mm_setzero_si128();
	const __m128i n255 = _mm_set1_epi32(255);

	uint32 fastGroups = mNumFastGroups;
	if (fastGroups) {
		if (mbUseFastLerp) {
			const __m128i round8 = _mm_set1_epi16(0x40);
			const uint16 *VDRESTRICT offsets = mFastLerpOffsets.data();

			do {
				__m128i srcVector = _mm_loadu_si128((const __m128i *)((const uint8 *)src + (*offsets++)));
				__m128i gatherVector = _mm_shuffle_epi8(srcVector, *(const __m128i *)rowFilter);
				__m128i accum = _mm_maddubs_epi16(gatherVector, *(const __m128i *)(rowFilter + 8));

				accum = _mm_srai_epi16(_mm_sub_epi16(round8, accum), 7);
				_mm_storeu_si64(dst8, _mm_packus_epi16(accum, accum));

				rowFilter += 16;
				dst8 += 8;
			} while(--fastGroups);

			w &= 7;
		} else {
			const __m128i round4 = _mm_set_epi32(0x2000, 0x2000, 0x2000, 0x2000);

			do {
				__m128i srcVector = _mm_undefined_si128();

				srcVector = _mm_insert_epi16(srcVector, *(const uint16 *)((const uint8 *)src + *(const uint32 *)(rowFilter + 0)), 0);
				srcVector = _mm_insert_epi16(srcVector, *(const uint16 *)((const uint8 *)src + *(const uint32 *)(rowFilter + 2)), 1);
				srcVector = _mm_insert_epi16(srcVector, *(const uint16 *)((const uint8 *)src + *(const uint32 *)(rowFilter + 4)), 2);
				srcVector = _mm_insert_epi16(srcVector, *(const uint16 *)((const uint8 *)src + *(const uint32 *)(rowFilter + 6)), 3);

				__m128i accum = _mm_madd_epi16(_mm_unpacklo_epi8(srcVector, zero), *(const __m128i *)(rowFilter + 8));

				accum = _mm_srai_epi32(_mm_add_epi32(accum, round4), 14);

				accum = _mm_packs_epi32(accum, accum);
				_mm_storeu_si32(dst8, _mm_packus_epi16(accum, accum));

				rowFilter += 16;
				dst8 += 4;
			} while(--fastGroups);

			w &= 3;
		}
	}

	if (kquads == 1) {
		while(w--) {
			const uint8 *VDRESTRICT src2 = (const uint8 *)src + (uint16)rowFilter[0];
			__m128i accum = _mm_madd_epi16(_mm_unpacklo_epi8(_mm_loadu_si32(src2), zero), _mm_loadu_si64(rowFilter + 4));

			accum = _mm_add_epi32(accum, _mm_shuffle_epi32(accum, 0x55));
			accum = _mm_add_epi32(accum, round);
			accum = _mm_srai_epi32(accum, 14);

			accum = _mm_max_epi16(accum, zero);
			accum = _mm_min_epi16(accum, n255);

			*dst8++ = (uint8)_mm_cvtsi128_si32(accum);
			rowFilter += 8;
		}
	} else {
		while(w--) {
			const uint8 *VDRESTRICT src2 = (const uint8 *)src + (uint16)rowFilter[0];

			rowFilter += 4;

			__m128i accum = round;
			uint32 i = kquads;
			do {
				accum = _mm_add_epi32(accum, _mm_madd_epi16(_mm_unpacklo_epi8(_mm_loadu_si32(src2), zero), _mm_loadu_si64(rowFilter)));
				rowFilter += 4;
				src2 += 4;
			} while(--i);

			accum = _mm_add_epi32(accum, _mm_shuffle_epi32(accum, 0x55));
			accum = _mm_srai_epi32(accum, 14);

			accum = _mm_max_epi16(accum, zero);
			accum = _mm_min_epi16(accum, n255);

			*dst8++ = (uint8)_mm_cvtsi128_si32(accum);
		}
	}
#endif
}

void VDResamplerSeparableTableRowStage8SSE2::Process(void *dst0, const void *src0, uint32 w, uint32 u, uint32 dudx) {
	// if we're hitting this path, it must be for dualclip, so perf is not significant
	uint8 *VDRESTRICT dst = (uint8 *)dst0;
	const uint8 *src = (const uint8 *)src0;
	const unsigned ksize = (int)mFilterBank.size() >> 8;
	const sint32 *filterBase = mFilterBank.data();

	do {
		const uint8 *VDRESTRICT src2 = src + (u>>16);
		const sint32 *VDRESTRICT filter = filterBase + ksize*((u>>8)&0xff);
		u += dudx;

		int accum = 0x2000;
		for(unsigned i = ksize; i; --i) {
			uint8 p = *src2++;
			sint32 coeff = *filter++;

			accum += (sint32)p * coeff;
		}

		accum >>= 14;

		if (accum < 0)
			accum = 0;

		if (accum > 255)
			accum = 255;

		*dst++ = (uint8)accum;
	} while(--w);
}

VDResamplerSeparableTableColStage8SSE2::VDResamplerSeparableTableColStage8SSE2(const IVDResamplerFilter& filter)
	: VDResamplerColStageSeparableTable32(filter)
{
	mbUseFastLerp = false;
	if (CPUGetEnabledExtensions() & CPUF_SUPPORTS_SSSE3)
		mbUseFastLerp = FilterHasNoOvershoot(mFilterBank.data(), mFilterBank.size());

	VDResamplerSwizzleTable(mFilterBank.data(), (uint32)mFilterBank.size() >> 1);
}

namespace {
	template<unsigned T_Rows>
	void FilterColumns_SSE2(void *dst0, const uint8 *const *src, const sint16 *filter, uint32 n) {
		uint8 *VDRESTRICT dst = (uint8 *)dst0;

		const uint8 *rows[T_Rows];
		for(unsigned i = 0; i < T_Rows; ++i)
			rows[i] = src[i];

		__m128i rowFilters[T_Rows];
		for(unsigned i = 0; i < T_Rows; ++i)
			rowFilters[i] = _mm_shuffle_epi32(_mm_loadu_si32(&filter[i*2]), 0);


		const __m128i zero = _mm_setzero_si128();
		const __m128i round = _mm_set1_epi32(0x2000);
		uint32 xoffset = 0;

		if constexpr(T_Rows == 2) {
			uint32 n2 = n >> 1;
			n &= 1;

			uint32 xoffsetLimit1 = n2 * 8;
			while(xoffset < xoffsetLimit1) {
				// load and interleave source samples from the two rows
				__m128i x = _mm_unpacklo_epi8(_mm_loadu_si64(rows[0] + xoffset), _mm_loadu_si64(rows[1] + xoffset));

				// filter two sets of four pairs of pixels
				__m128i accum1 = _mm_madd_epi16(_mm_unpacklo_epi8(x, zero), rowFilters[0]);
				__m128i accum2 = _mm_madd_epi16(_mm_unpackhi_epi8(x, zero), rowFilters[0]);

				accum1 = _mm_add_epi32(accum1, round);
				accum2 = _mm_add_epi32(accum2, round);

				accum1 = _mm_srai_epi32(accum1, 14);
				accum2 = _mm_srai_epi32(accum2, 14);

				accum1 = _mm_packs_epi32(accum1, accum2);
				accum1 = _mm_packus_epi16(accum1, accum1);

				_mm_storeu_si64(dst + xoffset, accum1);

				xoffset += 8;
			}
		}

		uint32 xoffsetLimit2 = xoffset + n*4;
		while(xoffset < xoffsetLimit2) {
			__m128i accum = round;

			// this section is critical and must be unrolled
			if constexpr(T_Rows >= 2)
				accum = _mm_add_epi32(accum, _mm_madd_epi16(_mm_unpacklo_epi8(_mm_unpacklo_epi8(_mm_loadu_si32(rows[0] + xoffset), _mm_loadu_si32(rows[1] + xoffset)), zero), rowFilters[0]));

			if constexpr(T_Rows >= 4)
				accum = _mm_add_epi32(accum, _mm_madd_epi16(_mm_unpacklo_epi8(_mm_unpacklo_epi8(_mm_loadu_si32(rows[2] + xoffset), _mm_loadu_si32(rows[3] + xoffset)), zero), rowFilters[1]));

			if constexpr(T_Rows >= 6)
				accum = _mm_add_epi32(accum, _mm_madd_epi16(_mm_unpacklo_epi8(_mm_unpacklo_epi8(_mm_loadu_si32(rows[4] + xoffset), _mm_loadu_si32(rows[5] + xoffset)), zero), rowFilters[2]));

			if constexpr(T_Rows >= 8)
				accum = _mm_add_epi32(accum, _mm_madd_epi16(_mm_unpacklo_epi8(_mm_unpacklo_epi8(_mm_loadu_si32(rows[6] + xoffset), _mm_loadu_si32(rows[7] + xoffset)), zero), rowFilters[3]));

			accum = _mm_srai_epi32(accum, 14);
			accum = _mm_packs_epi32(accum, accum);
			accum = _mm_packus_epi16(accum, accum);

			_mm_storeu_si32(dst + xoffset, accum);

			xoffset += 4;
		}
	}

	void FilterColumnsLerp_SSSE3(void *dst0, const uint8 *const *src, const sint16 *filter, uint32 n) {
		uint8 *VDRESTRICT dst = (uint8 *)dst0;

		const uint8 *VDRESTRICT row0 = src[0];
		const uint8 *VDRESTRICT row1 = src[1];

		// reduce filter from 2.14 to -1.7 -- we use negative so we can get 0..128 range
		uint8 f0 = (uint8)(0 - ((filter[0] + 64) >> 7));
		uint8 f1 = (uint8)(0x80 - f0);
		__m128i rowFilter = _mm_set1_epi16((sint16)(uint16)(((uint32)f1 << 8) + f0));

		const __m128i zero = _mm_setzero_si128();
		const __m128i round = _mm_set1_epi16(0x40);
		uint32 xoffset = 0;

		uint32 n2 = n >> 1;

		uint32 xoffsetLimit1 = n2 * 8;
		while(xoffset < xoffsetLimit1) {
			// load and interleave source samples from the two rows
			__m128i x = _mm_unpacklo_epi8(_mm_loadu_si64(row0 + xoffset), _mm_loadu_si64(row1 + xoffset));

			// filter two sets of four pairs of pixels
			__m128i accum = _mm_sub_epi16(round, _mm_maddubs_epi16(x, rowFilter));

			accum = _mm_srai_epi16(accum, 7);
			accum = _mm_packus_epi16(accum, accum);

			_mm_storeu_si64(dst + xoffset, accum);

			xoffset += 8;
		}

		if (n & 1) {
			__m128i x = _mm_unpacklo_epi8(_mm_loadu_si32(row0 + xoffset), _mm_loadu_si32(row1 + xoffset));

			// filter two sets of four pairs of pixels
			__m128i accum = _mm_sub_epi16(round, _mm_maddubs_epi16(x, rowFilter));

			accum = _mm_srai_epi16(accum, 7);
			accum = _mm_packus_epi16(accum, accum);

			_mm_storeu_si32(dst + xoffset, accum);
		}
	}
}

void VDResamplerSeparableTableColStage8SSE2::Process(void *dst0, const void *const *src0, uint32 w, sint32 phase) {
	uint8 *VDRESTRICT dst = (uint8 *)dst0;
	const uint8 *const *VDRESTRICT src = (const uint8 *const *)src0;
	const unsigned ksize = (unsigned)mFilterBank.size() >> 8;
	const sint16 *VDRESTRICT filter = (const sint16 *)&mFilterBank[((phase>>8)&0xff) * ksize];

	int w4 = w & ~3;

	if (w4) {
		switch(ksize) {
			case 2:
				if (mbUseFastLerp)
					FilterColumnsLerp_SSSE3(dst, src, filter, w >> 2);
				else
					FilterColumns_SSE2<2>(dst, src, filter, w >> 2);
				break;

			case 4:
				FilterColumns_SSE2<4>(dst, src, filter, w >> 2);
				break;

			case 6:
				FilterColumns_SSE2<6>(dst, src, filter, w >> 2);
				break;

			case 8:
				FilterColumns_SSE2<8>(dst, src, filter, w >> 2);
				break;

			default:
				{
					const __m128i zero = _mm_setzero_si128();
					const __m128i round = _mm_set1_epi32(0x2000);

					uint32 xoffset = 0;

					for(int i=0; i<w4; i += 4) {
						__m128i accum = round;

						__assume(ksize > 0);
						for(unsigned j = 0; j < ksize; j += 2) {
							accum = _mm_add_epi32(accum, _mm_madd_epi16(_mm_unpacklo_epi8(_mm_unpacklo_epi8(_mm_loadu_si32(src[j+0] + xoffset), _mm_loadu_si32(src[j+1] + xoffset)), zero), _mm_shuffle_epi32(_mm_loadu_si32(filter + j*2), 0)));
						}

						accum = _mm_srai_epi32(accum, 14);
						accum = _mm_packs_epi32(accum, accum);
						accum = _mm_packus_epi16(accum, accum);

						_mm_storeu_si32(dst + xoffset, accum);

						xoffset += 4;
					}
				}
				break;
		}
	}

	for(uint32 i=w4; i<w; ++i) {
		int b = 0x2000;
		const sint16 *filter2 = filter;
		const uint8 *const *src2 = src;

		for(unsigned j = ksize; j; j -= 2) {
			sint32 p0 = (*src2++)[i];
			sint32 p1 = (*src2++)[i];
			sint32 coeff0 = filter2[0];
			sint32 coeff1 = filter2[1];
			filter2 += 4;

			b += p0*coeff0;
			b += p1*coeff1;
		}

		b >>= 14;

		if ((uint32)b >= 0x00000100)
			b = ~b >> 31;

		dst[i] = (uint8)b;
	}
}
