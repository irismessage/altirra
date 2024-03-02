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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <at/atcore/md5.h>

void ATMD5Engine::Update(const void *data, size_t len) {
	uint8 buf[64];

	mLen += len;

	if (mSubBufferLength) {
		size_t tc = 64 - mSubBufferLength;
		if (tc > len) {
			memcpy(mSubBuffer + mSubBufferLength, data, len);
			mSubBufferLength += len;
			return;
		}

		memcpy(buf, mSubBuffer, 64);
		memcpy(buf + mSubBufferLength, data, tc);

		UpdateBlocks(buf, 1);

		len -= tc;
		data = (const char *)data + tc;
	}

	size_t blocks = len >> 6;
	if (blocks) {
		UpdateBlocks(data, blocks);
		data = (const char *)data + (blocks << 6);
	}

	mSubBufferLength = len & 63;
	if (mSubBufferLength)
		memcpy(mSubBuffer, data, mSubBufferLength);
}

void ATMD5Engine::UpdateBlocks(const void *data, size_t blocks) {
	// This is a by-the-book implementation of MD5 as described in RFC 1321,
	// section 3. There are no special tricks here, as we only need this for
	// algorithm verification (FLAC, in particular, as it stores an MD5
	// digest in the stream).

	static constexpr uint32 T[64] = {
		0xD76AA478, 0xE8C7B756, 0x242070DB, 0xC1BDCEEE, 0xF57C0FAF, 0x4787C62A, 0xA8304613, 0xFD469501,
		0x698098D8, 0x8B44F7AF, 0xFFFF5BB1, 0x895CD7BE, 0x6B901122, 0xFD987193, 0xA679438E, 0x49B40821,
		0xF61E2562, 0xC040B340, 0x265E5A51, 0xE9B6C7AA, 0xD62F105D, 0x02441453, 0xD8A1E681, 0xE7D3FBC8,
		0x21E1CDE6, 0xC33707D6, 0xF4D50D87, 0x455A14ED, 0xA9E3E905, 0xFCEFA3F8, 0x676F02D9, 0x8D2A4C8A,
		0xFFFA3942, 0x8771F681, 0x6D9D6122, 0xFDE5380C, 0xA4BEEA44, 0x4BDECFA9, 0xF6BB4B60, 0xBEBFBC70,
		0x289B7EC6, 0xEAA127FA, 0xD4EF3085, 0x04881D05, 0xD9D4D039, 0xE6DB99E5, 0x1FA27CF8, 0xC4AC5665,
		0xF4292244, 0x432AFF97, 0xAB9423A7, 0xFC93A039, 0x655B59C3, 0x8F0CCC92, 0xFFEFF47D, 0x85845DD1,
		0x6FA87E4F, 0xFE2CE6E0, 0xA3014314, 0x4E0811A1, 0xF7537E82, 0xBD3AF235, 0x2AD7D2BB, 0xEB86D391
	};

	uint32 X[16];
	uint32 A = mA;
	uint32 B = mB;
	uint32 C = mC;
	uint32 D = mD;

	do {
		memcpy(X, data, 64);
		data = (const char *)data + 64;

		uint32 AA = A;
		uint32 BB = B;
		uint32 CC = C;
		uint32 DD = D;

#define ROT(x, s) ((x << s) + (x >> (32 - s)))

#define ABCD(k,s,i) A += OP(B,C,D) + X[k] + T[i-1]; A = B + ROT(A, s)
#define DABC(k,s,i) D += OP(A,B,C) + X[k] + T[i-1]; D = A + ROT(D, s)
#define CDAB(k,s,i) C += OP(D,A,B) + X[k] + T[i-1]; C = D + ROT(C, s)
#define BCDA(k,s,i) B += OP(C,D,A) + X[k] + T[i-1]; B = C + ROT(B, s)

#define OP(x,y,z) ((x & y) + (~x & z))
		ABCD( 0, 7, 1); DABC( 1,12, 2); CDAB( 2,17, 3); BCDA( 3,22, 4);
		ABCD( 4, 7, 5); DABC( 5,12, 6); CDAB( 6,17, 7); BCDA( 7,22, 8);
		ABCD( 8, 7, 9); DABC( 9,12,10); CDAB(10,17,11); BCDA(11,22,12);
		ABCD(12, 7,13); DABC(13,12,14); CDAB(14,17,15); BCDA(15,22,16);
#undef OP

#define OP(x,y,z) ((x & z) + (y & ~z))
		ABCD( 1, 5,17); DABC( 6, 9,18); CDAB(11,14,19); BCDA( 0,20,20);
		ABCD( 5, 5,21); DABC(10, 9,22); CDAB(15,14,23); BCDA( 4,20,24);
		ABCD( 9, 5,25); DABC(14, 9,26); CDAB( 3,14,27); BCDA( 8,20,28);
		ABCD(13, 5,29); DABC( 2, 9,30); CDAB( 7,14,31); BCDA(12,20,32);
#undef OP

#define OP(x,y,z) (x ^ y ^ z)
		ABCD( 5, 4,33); DABC( 8,11,34); CDAB(11,16,35); BCDA(14,23,36);
		ABCD( 1, 4,37); DABC( 4,11,38); CDAB( 7,16,39); BCDA(10,23,40);
		ABCD(13, 4,41); DABC( 0,11,42); CDAB( 3,16,43); BCDA( 6,23,44);
		ABCD( 9, 4,45); DABC(12,11,46); CDAB(15,16,47); BCDA( 2,23,48);
#undef OP

#define OP(x,y,z) (y ^ (x | ~z))
		ABCD( 0, 6,49); DABC( 7,10,50); CDAB(14,15,51); BCDA( 5,21,52);
		ABCD(12, 6,53); DABC( 3,10,54); CDAB(10,15,55); BCDA( 1,21,56);
		ABCD( 8, 6,57); DABC(15,10,58); CDAB( 6,15,59); BCDA(13,21,60);
		ABCD( 4, 6,61); DABC(11,10,62); CDAB( 2,15,63); BCDA( 9,21,64);
#undef OP

#undef BCDA
#undef CDAB
#undef DABC
#undef ABCD
#undef ROT

		A += AA;
		B += BB;
		C += CC;
		D += DD;
	} while(--blocks);

	mA = A;
	mB = B;
	mC = C;
	mD = D;
}

ATMD5Digest ATMD5Engine::Finalize() {
	uint8 buf[72] {};
	uint32 padBase = ((uint32)mLen + 8) & 63;

	buf[padBase] = 0x80;
	VDWriteUnalignedLEU64(&buf[64], mLen * 8);

	Update(buf + padBase, 72 - padBase);

	ATMD5Digest digest;
	VDWriteUnalignedLEU32(&digest.digest[0], mA);
	VDWriteUnalignedLEU32(&digest.digest[4], mB);
	VDWriteUnalignedLEU32(&digest.digest[8], mC);
	VDWriteUnalignedLEU32(&digest.digest[12], mD);

	return digest;
}

ATMD5Digest ATComputeMD5(const void *data, size_t len) {
	ATMD5Engine engine;

	engine.Update(data, len);
	return engine.Finalize();
}
