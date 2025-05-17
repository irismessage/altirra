//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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

#ifndef f_AT_ATIO_AUDIOUTILS_H
#define f_AT_ATIO_AUDIOUTILS_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/constexpr.h>
#include <bit>

////////////////////////////////////////////////////////////////////////////////
// Modified A-law
//
// We use A-law encoding here with two modifications:
//
// - Negative values are reversed so as to make the encoding monotonic via a
//   biased encoding instead of a signed-magnitude encoding. This allows encoded
//   values to be compared directly with unsigned integer comparisons.
//
// - The rounding/median bit is suppressed for the encoding nearest to zero, so
//   as to provide an actual zero encoding.
//
inline uint8 ATEncodeModifiedALaw(float x) {
	// A-law is defined in terms of a 13-bit signed magnitude fixed point
	// linear encoding. This means that we need to apply a slight rescaling
	// before doing a floating-point hack to extract exponent/mantissa.
	//
	// The math below is arranged to allow for a constant fold with a previous
	// multiply.
	constexpr float kScale = 2.0f * 2047.0f / 2048.0f;

	float v = std::min<float>(fabsf(x * kScale), kScale);

	// Force a fake denormal by applying a bias if the value is too small to
	// implement the linear section. This also handles actual denormals.
	uint8 bias = 0;

	if (v < 2.0f / 128.0f) {
		v += 2.0f / 128.0f;
		bias = 0x10;
	}

	// convert to float and extract bit pattern
	const uint32 ix = std::bit_cast<uint32>(v);

	// extract relevant exponent/mantissa bits
	return (((ix >> 19) & 0x7F) - bias) ^ (x < 0 ? 0x7F : 0x80);
}

constexpr float ATDecodeModifiedALaw(uint8 x) {
	// mask off sign bit and reverse negative encodings
	uint8_t ix = x & 0x80 ? x ^ 0x80 : x ^ 0x7F;

	// special-case zero
	if (ix == 0)
		return 0;

	// shift up exponent and mantissa
	uint32 ival = (uint32)ix << 19;

	// add exponent bias and median bit
	ival += 0x3B840000;

	// check if we need to handle the denormal case
	float bias = 0;

	if (ix < 0x10) {
		ival += 0x00800000;
		bias = 1.0f / 128.0f;
	}

	// convert through FP unit, merge sign, and rescale magnitude from [0,4095]/4096
	// to [0,4096]/4096
	return (std::bit_cast<float>(ival) - bias) * (x & 0x80 ? 4096.0f/4095.0f : -4096.0f/4095.0f);
}

extern const VDCxArray<float, 256> kATDecodeModifiedALawTable;

#endif

