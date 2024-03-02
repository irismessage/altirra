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
#include <vd2/system/binary.h>
#include <at/atcore/checksum.h>
#include <at/atcore/internal/checksum.h>

uint64 ATComputeOffsetChecksum(uint64 offset) {
	uint8 buf[8];

	VDWriteUnalignedLEU64(buf, offset);

	return ATComputeBlockChecksum(kATBaseChecksum, buf, 8);
}

uint64 ATComputeBlockChecksum(uint64 hash, const void *src, size_t len) {
	const uint64 kFNV1Prime = 1099511628211;

	const uint8 *src8 = (const uint8 *)src;

	while(len--)
		hash = (hash * kFNV1Prime) ^ *src8++;

	return hash;
}

uint64 ATComputeZeroBlockChecksum(uint64 hash, size_t len) {
	const uint64 kFNV1Prime = 1099511628211;
	const uint64 kFNV1Offset = 14695981039346656037;
	uint64 multiplier = kFNV1Prime;

	for(;;) {
		if (len & 1)
			hash *= multiplier;

		len >>= 1;
		if (!len)
			break;

		multiplier *= multiplier;
	}

	return hash;
}

bool operator==(const ATChecksumSHA256& x, const ATChecksumSHA256& y) {
	return memcmp(x.mDigest, y.mDigest, 32) == 0;
}

bool operator!=(const ATChecksumSHA256& x, const ATChecksumSHA256& y) {
	return memcmp(x.mDigest, y.mDigest, 32) != 0;
}

void ATChecksumUpdateSHA256_Reference(ATChecksumStateSHA256& VDRESTRICT state, const void *src, size_t numBlocks) {
	using namespace nsATChecksum;

	uint32 W[64];

	const auto BSIG0 = [=](uint32 v) {
		return ((v << (32 - 2)) | (v >> 2))
			^ ((v << (32 - 13)) | (v >> 13))
			^ ((v << (32 - 22)) | (v >> 22));
	};

	const auto BSIG1 = [=](uint32 v) {
		return ((v << (32 - 6)) | (v >> 6))
			^ ((v << (32 - 11)) | (v >> 11))
			^ ((v << (32 - 25)) | (v >> 25));
	};

	const auto SSIG0 = [=](uint32 v) {
		return ((v << (32 - 7)) | (v >> 7))
			^ ((v << (32 - 18)) | (v >> 18))
			^ (v >> 3);
	};

	const auto SSIG1 = [=](uint32 v) {
		return ((v << (32 - 17)) | (v >> 17))
			^ ((v << (32 - 19)) | (v >> 19))
			^ (v >> 10);
	};

	const auto CH = [](uint32 x, uint32 y, uint32 z) {
		return (x & y) ^ (~x & z);
	};

	const auto MAJ = [](uint32 x, uint32 y, uint32 z) {
		return (x & y) ^ (x & z) ^ (y & z);
	};

	const char *VDRESTRICT src2 = (const char *)src;

	while(numBlocks--) {
		memcpy(W, src2, 64);
		src2 += 64;

		for(uint32 i = 0; i < 16; ++i)
			W[i] = VDFromBE32(W[i]);

		for(uint32 i = 16; i < 64; ++i)
			W[i] = SSIG1(W[i-2]) + W[i-7] + SSIG0(W[i-15]) + W[i-16];

		ATChecksumStateSHA256 state2(state);

		for(uint32 i = 0; i < 64; ++i) {
			uint32 T1 = state2.H[7] + BSIG1(state2.H[4]) + CH(state2.H[4], state2.H[5], state2.H[6]) + K[i] + W[i];
			uint32 T2 = BSIG0(state2.H[0]) + MAJ(state2.H[0], state2.H[1], state2.H[2]);

			state2.H[7] = state2.H[6];
			state2.H[6] = state2.H[5];
			state2.H[5] = state2.H[4];
			state2.H[4] = state2.H[3] + T1;
			state2.H[3] = state2.H[2];
			state2.H[2] = state2.H[1];
			state2.H[1] = state2.H[0];
			state2.H[0] = T1 + T2;
		}

		state.H[0] += state2.H[0];
		state.H[1] += state2.H[1];
		state.H[2] += state2.H[2];
		state.H[3] += state2.H[3];
		state.H[4] += state2.H[4];
		state.H[5] += state2.H[5];
		state.H[6] += state2.H[6];
		state.H[7] += state2.H[7];
	}
}

void ATChecksumUpdateSHA256(ATChecksumStateSHA256& VDRESTRICT state, const void *src, size_t numBlocks) {
#if VD_CPU_X86 || VD_CPU_X64 || VD_CPU_ARM64
	ATChecksumUpdateSHA256_Optimized(state, src, numBlocks);
#else
	ATChecksumUpdateSHA256_Reference(state, src, numBlocks);
#endif
}

ATChecksumEngineSHA256::ATChecksumEngineSHA256() {
	mState.H[0] = 0x6a09e667;
	mState.H[1] = 0xbb67ae85;
	mState.H[2] = 0x3c6ef372;
	mState.H[3] = 0xa54ff53a;
	mState.H[4] = 0x510e527f;
	mState.H[5] = 0x9b05688c;
	mState.H[6] = 0x1f83d9ab;
	mState.H[7] = 0x5be0cd19;

	mFragmentLen = 0;
	mTotalBytes = 0;
}

void ATChecksumEngineSHA256::Process(const void *src, size_t len) {
	if (!len)
		return;

	mTotalBytes += len;

	// if we have an existing fragment, try to complete it
	if (mFragmentLen) {
		size_t tc = 64 - mFragmentLen;

		if (tc > len)
			tc = len;

		memcpy(mFragmentBuffer + mFragmentLen, src, tc);
		src = (const char *)src + tc;
		len -= tc;

		mFragmentLen += tc;
		if (mFragmentLen >= 64) {
			mFragmentLen = 0;

			ATChecksumUpdateSHA256(mState, mFragmentBuffer, 1);
		}

		if (!len)
			return;
	}

	// hash full blocks directly from source
	size_t numFullBlocks = len >> 6;

	if (numFullBlocks) {
		ATChecksumUpdateSHA256(mState, src, numFullBlocks);
		src = (const char *)src + (numFullBlocks << 6);
		len &= 63;
	}

	// copy remaining data into fragment buffer
	if (len) {
		memcpy(mFragmentBuffer, src, len);
		mFragmentLen = len;
	}
}

ATChecksumSHA256 ATChecksumEngineSHA256::Finalize() {
	// The fragment buffer can't be full, or we would have flushed it. Append the sentinel bit
	// now.
	mFragmentBuffer[mFragmentLen++] = 0x80;

	// If the fragment buffer can't hold the message length, flush it.
	if (mFragmentLen > 56) {
		memset(mFragmentBuffer + mFragmentLen, 0, 64 - mFragmentLen);
		ATChecksumUpdateSHA256(mState, mFragmentBuffer, 1);
		mFragmentLen = 0;
	}

	// Zero-pad up to the end and then add the message length, and hash.
	memset(mFragmentBuffer + mFragmentLen, 0, 56 - mFragmentLen);
	VDWriteUnalignedBEU64(&mFragmentBuffer[56], mTotalBytes * 8);

	ATChecksumUpdateSHA256(mState, mFragmentBuffer, 1);

	// Byte-swap the state back to canonical digest form and return it.
	ATChecksumSHA256 checksum;

	for(int i=0; i<8; ++i) {
		VDWriteUnalignedBEU32(&checksum.mDigest[4*i], mState.H[i]);
	}

	return checksum;
}

ATChecksumSHA256 ATComputeChecksumSHA256(const void *src, size_t len) {
	ATChecksumEngineSHA256 engine;
	engine.Process(src, len);
	return engine.Finalize();
}
