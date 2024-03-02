//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2019 Avery Lee
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

#ifndef f_AT_ATCORE_INTERNAL_CHECKSUM_H
#define f_AT_ATCORE_INTERNAL_CHECKSUM_H

#include <vd2/system/vdtypes.h>

void ATChecksumUpdateSHA256_Reference(ATChecksumStateSHA256& VDRESTRICT state, const void *src, size_t numBlocks);

#if VD_CPU_X86 || VD_CPU_X64
void ATChecksumUpdateSHA256_Scalar(ATChecksumStateSHA256& VDRESTRICT state, const void *src, size_t numBlocks);

template<bool useSSSE3>
void ATChecksumUpdateSHA256_SSE2(ATChecksumStateSHA256& VDRESTRICT state, const void *src, size_t numBlocks);

void ATChecksumUpdateSHA256_SHA(ATChecksumStateSHA256& VDRESTRICT state, const void *src, size_t numBlocks);
void ATChecksumUpdateSHA256_Optimized(ATChecksumStateSHA256& VDRESTRICT state, const void *src, size_t numBlocks);
#elif VD_CPU_ARM64
void ATChecksumUpdateSHA256_Optimized(ATChecksumStateSHA256& VDRESTRICT state, const void* src, size_t numBlocks);
#endif

namespace nsATChecksum {
	alignas(16) inline constexpr uint32 K[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
		0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
		0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
		0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
		0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
		0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
		0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
		0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
		0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
	};
}

#endif	// f_AT_ATCORE_INTERNAL_CHECKSUM_H
