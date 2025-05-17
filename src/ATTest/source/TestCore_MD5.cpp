//	Altirra - Atari 800/800XL/5200 emulator
//	Test module
//	Copyright (C) 2009-2021 Avery Lee
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
#include <at/atcore/md5.h>
#include "test.h"

constexpr ATMD5Digest operator""_atmd5(const char *s, size_t n) {
	if (n < 32)
		throw;

	ATMD5Digest digest;
	for(int i = 0; i < 16; ++i) {
		char c = s[i*2+0];
		char d = s[i*2+1];
		uint8 v = 0;

		if (c >= '0' && c <= '9')
			v = (uint8)(c - '0');
		else if (c >= 'A' && c <= 'F')
			v = (uint8)(c - 'A') + 10;
		else if (c >= 'a' && c <= 'f')
			v = (uint8)(c - 'a') + 10;

		v <<= 4;

		if (d >= '0' && d <= '9')
			v += (uint8)(d - '0');
		else if (d >= 'A' && d <= 'F')
			v += (uint8)(d - 'A') + 10;
		else if (d >= 'a' && d <= 'f')
			v += (uint8)(d - 'a') + 10;

		digest.digest[i] = v;
	}

	return digest;
}

DEFINE_TEST(Core_MD5) {
	struct MD5HexDigest {
		MD5HexDigest(const ATMD5Digest& digest) {
			for(int i=0; i<16; ++i) {
				uint8 c = digest.digest[i] >> 4;
				uint8 d = digest.digest[i] & 15;
				buf[i*2+0] = c >= 10 ? 'A' + (c - 10) : '0' + c;
				buf[i*2+1] = d >= 10 ? 'A' + (d - 10) : '0' + d;
			}

			buf[32] = 0;
		}

		const char *c_str() const { return buf; }

		char buf[33];
	};

	ATMD5Digest digest;

	// run RFC 1321 test suite
	digest = ATComputeMD5("", 0);
	AT_TEST_TRACEF("%s  empty", MD5HexDigest(digest).c_str());
	TEST_ASSERT(digest == "D41D8CD98F00B204E9800998ECF8427E"_atmd5);

	digest = ATComputeMD5("a", 1);
	AT_TEST_TRACEF("%s  a", MD5HexDigest(digest).c_str());
	TEST_ASSERT(digest == "0CC175B9C0F1B6A831C399E269772661"_atmd5);

	digest = ATComputeMD5("abc", 3);
	AT_TEST_TRACEF("%s  abc", MD5HexDigest(digest).c_str());
	TEST_ASSERT(digest == "900150983cd24fb0d6963f7d28e17f72"_atmd5);

	digest = ATComputeMD5("message digest", 14);
	AT_TEST_TRACEF("%s  message digest", MD5HexDigest(digest).c_str());
	TEST_ASSERT(digest == "F96B697D7CB7938D525A2F31AAF161D0"_atmd5);

	digest = ATComputeMD5("abcdefghijklmnopqrstuvwxyz", 26);
	AT_TEST_TRACEF("%s  a..z", MD5HexDigest(digest).c_str());
	TEST_ASSERT(digest == "C3FCD3D76192E4007DFB496CCA67E13B"_atmd5);

	digest = ATComputeMD5("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 62);
	AT_TEST_TRACEF("%s  A..Za..z0..9", MD5HexDigest(digest).c_str());
	TEST_ASSERT(digest == "D174AB98D277D9F5A5611C2C9F419D9F"_atmd5);

	digest = ATComputeMD5("12345678901234567890123456789012345678901234567890123456789012345678901234567890", 80);
	AT_TEST_TRACEF("%s  (1..0){8}", MD5HexDigest(digest).c_str());
	TEST_ASSERT(digest == "57EDF4A22BE3C955AC49DA2E2107B67A"_atmd5);

	return 0;
}
