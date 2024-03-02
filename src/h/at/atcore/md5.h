//	Altirra - Atari 800/800XL/5200 emulator
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_MD5_H
#define f_AT_ATCORE_MD5_H

#include <string.h>
#include <vd2/system/vdtypes.h>

struct ATMD5Digest {
	uint8 digest[16];

	bool operator==(const ATMD5Digest& other) const {
		return memcmp(digest, other.digest, 16) == 0;
	}

	bool operator!=(const ATMD5Digest& other) const {
		return memcmp(digest, other.digest, 16) != 0;
	}
};

class ATMD5Engine {
public:
	void Update(const void *data, size_t len);
	void UpdateBlocks(const void *data, size_t blocks);
	ATMD5Digest Finalize();

private:
	uint32 mA = 0x67452301;
	uint32 mB = 0xEFCDAB89;
	uint32 mC = 0x98BADCFE;
	uint32 mD = 0x10325476;
	uint64 mLen = 0;
	uint8 mSubBufferLength = 0;
	uint8 mSubBuffer[64];
};

ATMD5Digest ATComputeMD5(const void *data, size_t len);

#endif
