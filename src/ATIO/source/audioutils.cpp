//	Altirra	- Atari	800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//
//	This program is	free software; you can redistribute	it and/or modify
//	it under the terms of the GNU General Public License as	published by
//	the	Free Software Foundation; either version 2 of the License, or
//	(at	your option) any later version.
//
//	This program is	distributed	in the hope	that it	will be	useful,
//	but	WITHOUT	ANY	WARRANTY; without even the implied warranty	of
//	MERCHANTABILITY	or FITNESS FOR A PARTICULAR	PURPOSE.  See the
//	GNU	General	Public License for more	details.
//
//	You	should have	received a copy	of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <at/atio/audioutils.h>
#include <at/attest/test.h>

extern const constinit VDCxArray<float,	256> kATDecodeModifiedALawTable	= VDCxArray<float, 256>::from_index(ATDecodeModifiedALaw);

#ifdef AT_TESTS_ENABLED

AT_DEFINE_TEST(IO_ALaw)	{
	// test monotonicity, symmetry, and range
	float last = 0;
	for(int	i=0; i<32768; ++i) {
		float x = (float)i / 16384.0f;
		
		uint8 pv = ATEncodeModifiedALaw(x);
		uint8 nv = ATEncodeModifiedALaw(-x);
		
		// non-zero values should be negatives of each other
		if (pv != 0x80 && nv != 0x80 && (pv	^ nv) != 0xFF) {
			AT_TEST_ASSERTF(false, "FAIL %d (%g -> %02X %02X)", i, x, pv, nv);
			return 5;
		}
		
		float py = kATDecodeModifiedALawTable.v[pv];
		float ny = kATDecodeModifiedALawTable.v[nv];

		AT_TEST_ASSERT(py >= 0.0f && py <= 1.0f);
		AT_TEST_ASSERT(ny >= -1.0f && ny <= 0.0f);
		
		if (i && py != -ny) {
			AT_TEST_ASSERTF(false, "FAIL %d", i);
			return 5;
		}
		
		if (i && py < last) {
			AT_TEST_ASSERTF(false, "FAIL %d", i);
			return 5;
		}
		
		last = py;
	}
	
	// test round-tripping
	for(int	i=0; i<256;	++i) {
		float y = kATDecodeModifiedALawTable.v[i];
		uint8 x = ATEncodeModifiedALaw(y);
		
		if (i != x && i != 0x7F && i != 0x80 && x != 0x7F && x != 0x80) {
			AT_TEST_ASSERTF(false, "FAIL %02X -> %g -> %02X", i, y, x);
			return 5;
		} 
	}
	
	// test underflow
	for(int	i=0; i<1024; ++i) {
		float x = (float)i / (4095.0f * 1024.0f);
		
		uint8 pv = ATEncodeModifiedALaw(x);
		uint8 nv = ATEncodeModifiedALaw(-x);
		
		if (pv != 0x80 || nv != (i ? 0x7F :	0x80)) {
			AT_TEST_ASSERTF(false, "FAIL %d", i);
			return 5;
		}
	}
	
	// test overflow
	for(int	i=0; i<1024; ++i) {
		float x = 1.0f + (float)i / 256.0f;
		
		uint8	pv = ATEncodeModifiedALaw(x);
		uint8	nv = ATEncodeModifiedALaw(-x);
		
		if (pv != 0xFF || nv != 0x00) {
			AT_TEST_ASSERTF(false, "FAIL %d", i);
			return 5;
		}
	}
	
	return 0;
}

#endif
