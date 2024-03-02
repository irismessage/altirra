//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2016 Avery Lee
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

#ifndef AT_ATCORE_CHECKSUM_H
#define AT_ATCORE_CHECKSUM_H

#include <vd2/system/vdtypes.h>

// FNV-1 offset.
inline const uint64 kATBaseChecksum = UINT64_C(14695981039346656037);

uint64 ATComputeOffsetChecksum(uint64 offset);
uint64 ATComputeBlockChecksum(uint64 hash, const void *src, size_t len);
uint64 ATComputeZeroBlockChecksum(uint64 hash, size_t len);

struct ATChecksumSHA256 {
	uint8 mDigest[32];
};

bool operator==(const ATChecksumSHA256& x, const ATChecksumSHA256& y);
bool operator!=(const ATChecksumSHA256& x, const ATChecksumSHA256& y);

ATChecksumSHA256 ATComputeChecksumSHA256(const void *src, size_t len);

struct ATChecksumStateSHA256 {
	alignas(16) uint32 H[8];
};

void ATChecksumUpdateSHA256(ATChecksumStateSHA256& VDRESTRICT state, const void *src, size_t numBlocks);

struct ATChecksumEngineSHA256 {
	ATChecksumEngineSHA256();

	void Reset();
	void Process(const void *src, size_t len);
	ATChecksumSHA256 Finalize();

	ATChecksumStateSHA256 mState;
	alignas(16) uint8 mFragmentBuffer[64];
	size_t mFragmentLen;
	uint64 mTotalBytes;
};

#endif	// AT_ATCORE_CHECKSUM_H
