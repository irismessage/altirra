//	Altirra - Atari 800/800XL/5200 emulator
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <at/atcore/checksum.h>
#include <at/atcore/randomization.h>

ATRandomizationSeeds g_ATRandomizationSeeds;

void ATSetRandomizationSeeds(uint32 masterSeed) {
	static_assert(std::is_standard_layout_v<ATRandomizationSeeds>);

	// assert and fix up on lockup state
	if (!masterSeed) {
		VDFAIL("Randomization seed should not be zero.");
		masterSeed = 1;
	}

	// RNG quality is not too important here as we aren't doing anything critical
	// like crypto, but we do have something to watch for: we shouldn't be using
	// a fast PRNG to sequence the initial seeds, as if the downstream code uses
	// the same PRNG, it'll cause overlapping subsequences between systems.
	// Therefore, we use crypto-strength SHA256 to generate the initial seeds
	// so the fast PRNGs are decoupled.

	ATChecksumStateSHA256 state {};
	uint8 dummy[64] {};

	memcpy(&state, &masterSeed, sizeof(uint32));

	size_t len = sizeof(ATRandomizationSeeds);
	char *dst = (char *)&g_ATRandomizationSeeds;

	for(;;) {
		// always do at least one update before using bits
		ATChecksumUpdateSHA256(state, dummy, 1);

		// remove zero entries as fast PRNGs don't like them, and figure
		// out how much entropy we actually have
		uint32 nonZeroEntropy[8];
		uint32 validCount = 0;
		for(int i=0; i<8; ++i) {
			if (state.H[i])
				nonZeroEntropy[validCount++] = state.H[i];
		}

		if (!validCount)
			continue;

		uint32 entropyLen = validCount * sizeof(nonZeroEntropy[0]);

		if (len < entropyLen) {
			memcpy(dst, nonZeroEntropy, len);
			break;
		}

		memcpy(dst, nonZeroEntropy, entropyLen);
		dst += entropyLen;
		len -= entropyLen;
	}
}
