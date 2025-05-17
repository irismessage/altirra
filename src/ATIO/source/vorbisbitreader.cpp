//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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

#include "stdafx.h"
#include <at/atio/vorbisbitreader.h>

// Read Vorbis-encoded floats. These use a different representation from IEEE floats.
// There is no implicit bit, and there are no denormal, infinity, or NaN encodings.
//
// The range is a bit of a problem. Vorbis has a 10-bit biased exponent with a range
// of 2^-788 to 2^235. Additionally, the mantissa is a 21-bit integer instead of a
// fraction, which adds another 2^20 compared to IEEE floats. The negative range is
// not an issue, but the positive range is as it is twice that of an IEEE float.
//
// Somewhat disturbingly, the specification does not specify a valid or required
// range and it is undocumented thatthe reference implementation clamps the decoded
// exponent to +/-63. We grudglingly follow here.
//
// This is not a performance-critical path as floats are only decoded in header
// decode when reading in VQ-enabled codebooks.
//
float ATVorbisBitReader::ReadFloat() {
	const uint32_t v = (*this)(32);
	const uint32_t mantissa = v & 0x1fffff;
	const float sign = v & 0x80000000U ? -1.0f : 1.0f;
	const int exponent = std::clamp((int)((v & 0x7fe00000) >> 21) - 788, -63, 63);

	return sign * ldexpf((float)mantissa, exponent);
}

void ATVorbisBitReader::RefillSlow() {
	auto pdata = mParent.RefillContinuePacket(mpSrc);

	mpSrc = pdata.mpSrc;
	mpSrcEopThreshold = pdata.mpSrc + pdata.mLen;
	mpSrcLimit = pdata.mpSrc + pdata.mLen - 12;

	if (pdata.mbEop) {
		// The bit accumulator always contains 7 whole bytes by design, which
		// we must take into account here.
		mpSrcEopThreshold = pdata.mpSrc + pdata.mEopOffset + 7;
	}
}
