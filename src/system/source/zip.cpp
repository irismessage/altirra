//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
//	Beginning with 1.6.0, the VirtualDub system library is licensed
//	differently than the remainder of VirtualDub.  This particular file is
//	thus licensed as follows (the "zlib" license):
//
//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.

#include <stdafx.h>
#include <numeric>
#include <vd2/system/vdtypes.h>
#include <vd2/system/zip.h>
#include <vd2/system/binary.h>
#include <vd2/system/date.h>
#include <vd2/system/error.h>
#include <vd2/system/function.h>

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
#include <vd2/system/cpuaccel.h>
#include <intrin.h>
#elif defined(VD_CPU_ARM64)
#include <vd2/system/cpuaccel.h>
#include <intrin.h>
#endif

//#define VDDEBUG_DEFLATE VDDEBUG2
#define VDDEBUG_DEFLATE(...) ((void)0)

//#define VDDEBUG_INFLATE VDDEBUG2
#define VDDEBUG_INFLATE(...) ((void)0)

namespace nsVDDeflate {
	// The tables below are mostly standard Deflate, except with extensions for
	// enhanced Deflate (a.k.a. Deflate64(tm)). The differences are two additional
	// distance codes to extend the reference window to 64K and changing the last
	// length code from fixed 258 to explicit 16-bit + 3.
	//
	// The best documentation for this is from a version of PKWARE's APPNOTE.TXT
	// document, annotated and extended by the Info-Zip folks. The original location
	// of this doc seems to be offline, but as of 2/2023 it was retrieved from:
	//
	// https://github.com/zlib-ng/minizip-ng/blob/master/doc/zip/appnote.iz.txt

	const unsigned len_tbl[32]={
		3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,
		131,163,195,227,258
	};

	const unsigned len_tbl64[32]={
		3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,
		131,163,195,227,3
	};

	const unsigned len_pack_tbl[32]={
		3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,
		131,163,195,227,258,~(unsigned)0
	};

	const unsigned char hclen_tbl[]={
		16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
	};

	const unsigned char len_bits_tbl[32]={
		0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
	};

	const unsigned char len_bits_tbl64[32]={
		0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,16
	};

	const unsigned char dist_bits_tbl[]={
		0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13,14,14
	};

	const unsigned dist_tbl[]={
		1,2,3,4,	// +0 bits
		5,7,		// +1 bits
		9,13,		// +2 bits
		17,25,		// +3 bits
		33,49,		// +4 bits
		65,97,		// +5 bits
		129,193,	// +6 bits
		257,385,	// +7 bits
		513,769,	// +8 bits
		1025,1537,	// +9 bits
		2049,3073,	// +10 bits
		4097,6145,	// +11 bits
		8193,12289,	// +12 bits
		16385,24577,// +13 bits
		32769,49153,// +14 bits (Enhanced Deflate only)
		65537
	};
};

////////////////////////////////////////////////////////////////////////////////

VDDeflateDecompressionException::VDDeflateDecompressionException()
	: MyError("Decompression error while reading Deflate-compressed data.")
{
}

void VDDeflateBitReaderL2Buffer::Init(IVDStream& src, uint64 readLimit) {
	mpSrc = &src;
	mReadLimitRemaining = readLimit;
	mReadValidEnd = kHeaderSize;
}

VDDeflateBitReaderL2Buffer::ValidRange VDDeflateBitReaderL2Buffer::Refill(const void *consumed) {
	// determine how many bytes we have left to preserve
	uint32 validStart = mReadValidEnd;

	if (consumed) {
		ptrdiff_t consumedPos = (const uint8 *)consumed - mReadBuffer;
		if (consumedPos < 0)
			throw VDDeflateDecompressionException();

		// We may get a refill at the very end of the L2 buffer with less than
		// 4/8 bytes. In that case, we must waive the EOF check and allow the
		// buffer to "refill" so the bitstream reader can continue to pull
		// up to 56 bits into its accumulator. We're fine as long as those bits
		// aren't actually used, and if they are, we'll catch it at the end
		if ((size_t)consumedPos < validStart)
			validStart = (uint32)consumedPos;
	}

	// copy down tail
	uint32 tailLen = mReadValidEnd - validStart;
	if (tailLen > kHeaderSize)
		throw VDDeflateDecompressionException();

	if (tailLen)
		memcpy(&mReadBuffer[kHeaderSize - tailLen], &mReadBuffer[validStart], tailLen);

	validStart = kHeaderSize - tailLen;

	mReadValidEnd = kHeaderSize;

	// read in as much new data as we can up to the read limit
	if (mReadLimitRemaining) {
		uint32 toRead = kBufferSize;
		if (toRead > mReadLimitRemaining)
			toRead = (uint32)mReadLimitRemaining;

		mReadLimitRemaining -= toRead;

		mpSrc->Read(mReadBuffer + kHeaderSize, toRead);
		mReadValidEnd += toRead;
	}

	// Return the new range. Note that we always return the full buffer
	// to allow for read-ahead, with read-beyond-EOF being checked on
	// the next refill or at CheckEOF().
	return { &mReadBuffer[validStart], std::end(mReadBuffer) };
}

void VDDeflateBitReaderL2Buffer::CheckEOF(const void *consumed) {
	if (consumed > &mReadBuffer[mReadValidEnd])
		throw VDDeflateDecompressionException();
}

///////////////////////////////////////////////////////////////////////////

void VDDeflateBitReader::CheckEOF() {
	mpBuffer->CheckEOF(mpSrc - (mBitsLeft >> 3));
}

VDNOINLINE void VDDeflateBitReader::RefillBuffer() {
	VDASSERT(mpSrc <= mpSrcLimit + sizeof(mBitAccum));

	CheckEOF();

	auto validRange = mpBuffer->Refill(mpSrc);

	mpSrc = validRange.mpStart;
	mpSrcLimit = validRange.mpEnd - sizeof(mBitAccum);
}

void VDDeflateBitReader::readbytes(void *dst, size_t len) {
	if (!len)
		return;

	// if the bit accumulator is not byte aligned, do it the slow way
	// (uncommon)
	uint8 *VDRESTRICT dst2 = (uint8 *)dst;
	if (mBitsLeft & 7) {
		while(len-- > 0)
			*dst2++ = getbits(8);
		return;
	}

	// consume any whole bytes left in the accumulator
	while(mBitsLeft >= 8) {
		*dst2++ = (uint8)mBitAccum;
		mBitAccum >>= 8;
		mBitsLeft -= 8;

		if (!--len)
			return;
	}

	// In rare cases, the bit accumulator may have one more byte than is
	// tracked. Normally this is OK as that byte is correct and will align
	// with a later refill, but we are bypassing the normal refill flow
	// here and so we should make sure the accumulator is zeroed.
	mBitAccum = 0;

	// consume bytes directly
	while(len) {
		size_t tc = mpSrcLimit - mpSrc;
		if (!tc) {
			RefillBuffer();
			continue;
		}

		if (tc > len)
			tc = len;

		memcpy(dst2, mpSrc, tc);
		mpSrc += tc;
		dst2 += tc;
		len -= tc;
	}
}

///////////////////////////////////////////////////////////////////////////

void VDCRCTable::Init(uint32 crc) {
	InitConst(crc);
}

uint32 VDCRCTable::Process(uint32 crc, const void *src0, size_t count) const {
	const uint8 *src = (const uint8 *)src0;

	// This code is from the PNG spec.
	while(count--)
		crc = mTable[(uint8)crc ^ *src++] ^ (crc >> 8);

	return crc;
}

constexpr VDCRCTable::VDCRCTable(uint32 crc, int)
	: mTable{}
{
	InitConst(crc);
}

// This is broken out weirdly so we can share code between the constexpr and
// non-constexpr paths without forcing the non-constexpr path to do a useless
// runtime array pre-initialization, imposed by constexpr safety reqs.
constexpr void VDCRCTable::InitConst(uint32 crc) {
	for(int i=0; i<256; ++i) {
		unsigned v = i;
		for(int j=0; j<8; ++j)
			v = (v>>1) ^ (crc & -(sint32)(v&1));

		mTable[i] = v;
	}
}

constexpr VDCRCTable VDCRCTable::CRC32(VDCRCTable::kCRC32, 0);

///////////////////////////////////////////////////////////////////////////

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
VD_CPU_TARGET("pclmul")
uint32 VDCRC32Update_CLMUL(uint32 crc, const void *src, size_t len) {
	// This algorithm is based on Intel's white paper, Fast CRC Computation
	// Using PCLMULQDQ Instruction. There are a bunch of subtleties in computing
	// it, however.
	//
	// The CRC we're computing is the zlib/PNG/Ethernet CRC, commonly given as
	// the bit pattern 0xEDB88320 after shr1. The CRC is computed with both
	// initial and final values inverted, with the precise calculation being
	// (in GF(2)):
	//
	//	CRC32 = (0xFFFFFFFF*x^(8*len) + msg*x^32) mod P'
	//
	// where:
	//	P' = full polynomial (0x1DB710641)
	//	len = message length in bytes
	//	msg = message, with the first byte being the most significant byte,
	//	      and bit 0 being the most significant bit (reversed!).
	//
	// This is also equivalent to the four first or most significant bytes of
	// the message being inverted -- including inverting some bytes of the CRC
	// area if the message is shorter than 4 bytes.
	//
	// Additionally, this computation is bit-reflected, with multiplying by x^1
	// resulting in a _right_ shift. This means that XMM register math here is
	// also bit reversed, with the highest bit holding x^0 and the lowest bit
	// holding x^127. As in the Intel paper, the constants here have been
	// shifted left one to left-align the 127-bit result from PCLMULQDQ. The
	// polynomial 0x1DB710641 is in bit-reversed order; in natural arithmetic
	// bit order, the equivalent is P = 0x104C11DB7.
	//
	// Not particularly clear in the Intel paper is that the constants are
	// right-aligned instead of left-aligned so as to avoid needing a 65-bit
	// constant, which means that they have an implicit x^32 factor. To combat
	// this, the pregenerated constants are multiplied by x^32 less before the
	// modulus. For instance, to multiply by (x^96 mod P), the constant used
	// is actually (x^64 mod P')*x^32. Taking the bit reversal and extra shl1
	// into account, this becomes: bitrev((x^64 mod P')*x^32)<<1.
	//
	// There are two additional complications here not covered by the Intel
	// paper. First, the directions in the paper to handle a final partial
	// xmmword seem incorrect or at least misworded, as it isn't valid to just
	// zero-pad the final xmmword as it's in the least significant bits.
	// Instead, we multiply by special x^(8*n) and x^(8*n+64) folding constants
	// to make room. The tricky part is due to the implicit x^32 in the constant
	// constant, we actually need negative powers for n=1..3. Fortunately x^8
	// is invertible mod P and we use that to encode the necessary constants.
	//
	// The final issue is that for simplicity, we emulate a standard interface
	// that takes and returns the intermediate uninverted CRC32. In order to
	// cleanly XOR in new message data in x^0..127 positions, we need the
	// incoming CRC32 to be in the fractional x^-32..-1 bits:
	//
	//		msg = '01 02 03' -> '80 40 C0' bit reversed
	//		byte 1: 0x00.FFFFFFFF * x^8 + 0x80 = 0x7F.FFFFFF
	//		byte 2: 0x7F.FFFFFF * x^8 + 0x40 = 0x7FBF.FFFF
	//		byte 3: 0x7FBF.FFFF * x^8 + 0xC0 = 0x7FBF3F.FF
	//		final CRC32 = ~bitrev(0x7FBF3F.FF * x^32 mod P')
	//		            = ~bitrev(0x47FEC255)
	//
	// To make this work, we multiply the incoming CRC by x^-32.
	

	alignas(16) static constexpr uint64 kByteShifts[] {
		UINT64_C(0x0B66B1FA6),	// [ 0] = bitrev((x^-32 mod P')<<32) << 1
		UINT64_C(0x03F036DC2),	// [ 1] = bitrev((x^-24 mod P')<<32) << 1
		UINT64_C(0x1AE24A6B0),	// [ 2] = bitrev((x^-16 mod P')<<32) << 1
		UINT64_C(0x0CACF972A),	// [ 3] = bitrev((x^ -8 mod P')<<32) << 1
		UINT64_C(0x100000000),	// [ 4] = bitrev((x^  0 mod P')<<32) << 1
		UINT64_C(0x001000000),	// [ 5] = bitrev((x^  8 mod P')<<32) << 1
		UINT64_C(0x000010000),	// [ 6] = bitrev((x^ 16 mod P')<<32) << 1
		UINT64_C(0x000000100),	// [ 7] = bitrev((x^ 24 mod P')<<32) << 1
		UINT64_C(0x1db710640),	// [ 8] = bitrev((x^ 32 mod P')<<32) << 1
		UINT64_C(0x077073096),	// [ 9] = bitrev((x^ 40 mod P')<<32) << 1
		UINT64_C(0x1c26a3700),	// [10] = bitrev((x^ 48 mod P')<<32) << 1
		UINT64_C(0x1dab36c76),	// [11] = bitrev((x^ 56 mod P')<<32) << 1
		UINT64_C(0x163cd6124),	// [12] = bitrev((x^ 64 mod P')<<32) << 1
		UINT64_C(0x03d6029b0),	// [13] = bitrev((x^ 72 mod P')<<32) << 1
		UINT64_C(0x1102dd5e4),	// [14] = bitrev((x^ 80 mod P')<<32) << 1
		UINT64_C(0x0a6770bb4),	// [15] = bitrev((x^ 88 mod P')<<32) << 1
		UINT64_C(0x0ccaa009e),	// [16] = bitrev((x^ 96 mod P')<<32) << 1
		UINT64_C(0x1cc0a1202),	// [17] = bitrev((x^104 mod P')<<32) << 1
		UINT64_C(0x0efc26b3e),	// [18] = bitrev((x^112 mod P')<<32) << 1
		UINT64_C(0x0c18edfc0),	// [19] = bitrev((x^120 mod P')<<32) << 1
		UINT64_C(0x140d44a2e),	// [20] = bitrev((x^128 mod P')<<32) << 1
		UINT64_C(0x106e7dfc4),	// [21] = bitrev((x^136 mod P')<<32) << 1
		UINT64_C(0x09d0fe176),	// [22] = bitrev((x^144 mod P')<<32) << 1
		UINT64_C(0x0b9fbdbe8),	// [23] = bitrev((x^152 mod P')<<32) << 1
		UINT64_C(0x1751997d0),	// [24] = bitrev((x^160 mod P')<<32) << 1
	};

	const __m128i vfold128 = _mm_set_epi64x(kByteShifts[16], kByteShifts[24]);

	// Multiply running CRC by (x^-32 mod P') so the next 128 bits are weighted as
	// x^0..127. The CRC32 comes in at the lowest lane, which is scaled by x^32
	// in bit-reflected space -- so we need another x^-32 factor to combat that.
	static constexpr uint64 xn64_modP_shl32 = 0x6CA226EA;
	
	__m128i vcrc = _mm_clmulepi64_si128(_mm_cvtsi32_si128(crc), _mm_set_epi64x(0, xn64_modP_shl32), 0x00);

	// do 512-bit chunks
	if (len >= 64) {
		__m128i vcrc0 = _mm_setzero_si128();
		__m128i vcrc1 = vcrc0;
		__m128i vcrc2 = vcrc0;
		__m128i vcrc3 = vcrc;

		static constexpr uint64 x480_modP_shl32 = UINT64_C(0x1c6e41596);
		static constexpr uint64 x544_modP_shl32 = UINT64_C(0x154442bd4);
		__m128i vfold512 = _mm_set_epi64x(x480_modP_shl32, x544_modP_shl32);
		while(len >= 64) {
			vcrc0 = _mm_xor_si128(
				_mm_clmulepi64_si128(vcrc0, vfold512, 0x11),
				_mm_xor_si128(
					_mm_clmulepi64_si128(vcrc0, vfold512, 0x00),
					_mm_loadu_si128((const __m128i *)src + 0)
				)
			);

			vcrc1 = _mm_xor_si128(
				_mm_clmulepi64_si128(vcrc1, vfold512, 0x11),
				_mm_xor_si128(
					_mm_clmulepi64_si128(vcrc1, vfold512, 0x00),
					_mm_loadu_si128((const __m128i *)src + 1)
				)
			);

			vcrc2 = _mm_xor_si128(
				_mm_clmulepi64_si128(vcrc2, vfold512, 0x11),
				_mm_xor_si128(
					_mm_clmulepi64_si128(vcrc2, vfold512, 0x00),
					_mm_loadu_si128((const __m128i *)src + 2)
				)
			);

			vcrc3 = _mm_xor_si128(
				_mm_clmulepi64_si128(vcrc3, vfold512, 0x11),
				_mm_xor_si128(
					_mm_clmulepi64_si128(vcrc3, vfold512, 0x00),
					_mm_loadu_si128((const __m128i *)src + 3)
				)
			);

			src = (const char *)src + 64;
			len -= 64;
		}

		// fold down from 512 to 128
		static constexpr uint64 x224_modP_shl32 = UINT64_C(0x15A546366);
		static constexpr uint64 x288_modP_shl32 = UINT64_C(0x0F1DA05AA);
		static constexpr uint64 x352_modP_shl32 = UINT64_C(0x174359406);
		static constexpr uint64 x416_modP_shl32 = UINT64_C(0x03DB1ECDC);

		const __m128i vfold256 = _mm_set_epi64x(x224_modP_shl32, x288_modP_shl32);
		const __m128i vfold384 = _mm_set_epi64x(x352_modP_shl32, x416_modP_shl32);

		vcrc = vcrc3;
		vcrc = _mm_xor_si128(vcrc,
			_mm_xor_si128(
				_mm_clmulepi64_si128(vcrc2, vfold128, 0x00),
				_mm_clmulepi64_si128(vcrc2, vfold128, 0x11)
			)
		);
		vcrc = _mm_xor_si128(vcrc,
			_mm_xor_si128(
				_mm_clmulepi64_si128(vcrc1, vfold256, 0x00),
				_mm_clmulepi64_si128(vcrc1, vfold256, 0x11)
			)
		);
		vcrc = _mm_xor_si128(vcrc,
			_mm_xor_si128(
				_mm_clmulepi64_si128(vcrc0, vfold384, 0x00),
				_mm_clmulepi64_si128(vcrc0, vfold384, 0x11)
			)
		);
	}

	// do 128-bit chunks
	while(len >= 16) {
		// multiply current CRC chunks by (x^96 mod P')<<32 and (x^192 mod P')<<32
		__m128i foldedLo = _mm_clmulepi64_si128(vcrc, vfold128, 0x11);
		__m128i foldedHi = _mm_clmulepi64_si128(vcrc, vfold128, 0x00);

		// load next 128 bits
		__m128i va = _mm_loadu_si128((const __m128i *)src);
		src = (const char *)src + 16;

		// fold existing CRC and add in another 128 bits of the message
		vcrc = _mm_xor_si128(_mm_xor_si128(va, foldedLo), foldedHi);

		len -= 16;
	}

	// handle leftover bits
	if (len) {
		// multiply running CRC by x^8..120 mod P
		__m128i vfoldnlo = _mm_loadl_epi64((const __m128i *)&kByteShifts[len]);
		__m128i vfoldnhi = _mm_loadl_epi64((const __m128i *)&kByteShifts[len + 8]);

		__m128i foldedLo = _mm_clmulepi64_si128(vcrc, vfoldnlo, 0x01);
		__m128i foldedHi = _mm_clmulepi64_si128(vcrc, vfoldnhi, 0x00);

		// fold and add in remaining message bytes
		alignas(16) uint8 buf[16] {};
		memcpy(buf + (16 - len), src, len);

		__m128i va = _mm_loadu_si128((const __m128i *)buf);
		vcrc = _mm_xor_si128(_mm_xor_si128(va, foldedLo), foldedHi);
	}

	// At this point, we have a full 128-bit CRC bit-reversed in vcrc.
	// First, fold down the high 64 bits to produce a 96-bit intermediate,
	// right-aligned. This also multiplies the low 64 bits by x^32 to make
	// room for the final CRC.
	//
	// 127                                                      0 (reg order)
	//  +-------------+-------------+-------------+-------------+
	//  |      CRC low 64 bits      |      CRC high 64 bits     | vcrc
	//  +-------------+-------------+-------------+-------------+
	//          |                                 x
	//          |                                 +-------------+
	//          |                                 | x^96 mod P' |
	//          |                                 +-------------+
	//          |                                 =
	//          |     +-------------+-------------+-------------+
	//          |     |             CRC high product            |
	//          |     +-------------+-------------+-------------+
	//          |                                 +
	//          |                   +-------------+-------------+
	//          +------------------>|      CRC low 64 bits      |
	//                              +-------------+-------------+
	//                                            =
	// + - - - - - - -+-------------+-------------+-------------+
	// .      0       |               96-bit CRC                |
	// + - - - - - - -+-------------+-------------+-------------+

	vcrc = _mm_xor_si128(
		_mm_srli_si128(vcrc, 8),
		_mm_clmulepi64_si128(vcrc, _mm_load_si128((const __m128i *)&kByteShifts[16]), 0x00)
	);

	// Fold the high 32-bits. In the bit-reversed register order, this is:
	// vcrc[95:32] ^ vcrc[31:0]*((x^64 mod P') << 31). This leaves us a 64-bit CRC
	// in the low 64 bits.
	//
	// 127                                                      0 (reg order)
	// + - - - - - - -+-------------+-------------+-------------+
	// .      0       |      CRC low 64 bits      | CRC high 32b| vcrc
	// + - - - - - - -+-------------+-------------+-------------+
	//                      |                            x
	//                      |                     +-------------+
	//                      |                     | x^64 mod P '|
	//                      |                     +-------------+
	//                      |                     =
	//                      |       +-------------+-------------+
	//                      |       |      CRC high product     |
	//                      |       +-------------+-------------+
	//                      |                     +
	//                      |       +-------------+-------------+
	//                      +------>|      CRC low 64 bits      |
	//                              +-------------+-------------+
	//                                            =
	//                              +-------------+-------------+
	//                              |         64-bit CRC        |
	//                              +-------------+-------------+

	vcrc = _mm_xor_si128(
		_mm_shuffle_epi32(vcrc, 0b0'11'11'10'01),
		_mm_clmulepi64_si128(
			_mm_castps_si128(_mm_move_ss(_mm_setzero_ps(), _mm_castsi128_ps(vcrc))),
			_mm_load_si128((const __m128i *)&kByteShifts[12]),
			0x00
		)
	);

	// At this point, we now have a bit-reversed 64-bit CRC value in the low
	// 64 bits and need to do a Barrett reduction.
	//
	static constexpr uint64 x64divP = 0X1F7011641;
	const uint64 P = 0x1DB710640;
	const __m128i redConsts = _mm_set_epi64x(P, x64divP);

	//	T1(x) = floor(R(x)/x^32)*floor(x^64/P(x))
	__m128i t1 =
		_mm_clmulepi64_si128(
			_mm_castps_si128(_mm_move_ss(_mm_setzero_ps(), _mm_castsi128_ps(vcrc))),
			redConsts,
			0x00
		);

	//	T2(x) = floor(T1(x)/x^32)*P(x)
	__m128i t2 =
		_mm_clmulepi64_si128(
			_mm_castps_si128(_mm_move_ss(_mm_setzero_ps(), _mm_castsi128_ps(t1))),
			redConsts,
			0x10
		);

	//	C(x) = R(x) ^ loword(T2(x) mod x^32)
	vcrc = _mm_xor_si128(vcrc, t2);

	// extract running CRC32 in low 32-bits (in lane 1 in register).
	return _mm_cvtsi128_si32(_mm_shuffle_epi32(vcrc, 0x55));
}
#endif

#if defined(VD_CPU_ARM64)
uint32 VDCRC32Update_ARM64_CRC32(uint32 crc, const void *src, size_t len) {
	// The ARM64 version is stupidly simpler than the x64 version because ARM64
	// has a native instruction to calculate the Ethernet CRC. It has all
	// scalar sizes (8/16/32/64-bit) and works on a running CRC32
	// without the final inversion. It does require the optional CRC32 ISA
	// extension, which is common, but technically we still need to do a runtime
	// check.

	const auto partialUpdate = [](uint32 crc, const void *src, size_t len) -> uint32 {
		if (len & 4) {
			crc = __crc32w(crc, VDReadUnalignedU32(src));
			src = (const char *)src + 4;
		}

		if (len & 2) {
			crc = __crc32h(crc, VDReadUnalignedU16(src));
			src = (const char *)src + 2;
		}

		if (len & 1)
			crc = __crc32b(crc, *(const uint8 *)src);

		return crc;
	};

	// check for pre-alignment
	size_t alignLen = ((size_t)0 - len) & 7;
	if (alignLen) {
		if (alignLen > len)
			alignLen = len;

		crc = partialUpdate(crc, src, alignLen);
		src = (const char *)src + alignLen;
		len -= alignLen;
	}

	// do large blocks
	if (len >= 64) {
		size_t numLargeBlocks = len >> 6;
		do {
			crc = __crc32d(crc, *((uint64 *)src + 0));
			crc = __crc32d(crc, *((uint64 *)src + 1));
			crc = __crc32d(crc, *((uint64 *)src + 2));
			crc = __crc32d(crc, *((uint64 *)src + 3));
			crc = __crc32d(crc, *((uint64 *)src + 4));
			crc = __crc32d(crc, *((uint64 *)src + 5));
			crc = __crc32d(crc, *((uint64 *)src + 6));
			crc = __crc32d(crc, *((uint64 *)src + 7));
			src = (const char *)src + 64;
		} while(--numLargeBlocks);

		len &= 63;
	}

	// do small blocks
	while(len >= 8) {
		crc = __crc32d(crc, *(uint64 *)src);
		src = (const char *)src + 8;
		len -= 8;
	}

	// do tail
	return partialUpdate(crc, src, len);
}
#endif

VDCRCChecker::VDCRCChecker(const VDCRCTable& table)
	: mValue(0xFFFFFFFF), mpTable(&table)
{
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	if (mpTable == &VDCRCTable::CRC32) {
		if (CPUGetEnabledExtensions() & CPUF_SUPPORTS_CLMUL)
			mpTable = nullptr;
	}
#elif defined(VD_CPU_ARM64)
	if (mpTable == &VDCRCTable::CRC32) {
		if (CPUGetEnabledExtensions() & VDCPUF_SUPPORTS_CRC32)
			mpTable = nullptr;
	}
#endif
}
void VDCRCChecker::Process(const void *src, sint32 count) {
	if (count <= 0)
		return;

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	if (!mpTable) {
		mValue = VDCRC32Update_CLMUL(mValue, src, count);
		return;
	}
#elif defined(VD_CPU_ARM64)
	if (!mpTable) {
		mValue = VDCRC32Update_ARM64_CRC32(mValue, src, count);
		return;
	}
#endif

	mValue = mpTable->Process(mValue, src, count);
}

struct VDHuffmanHistoSorterData {
	VDHuffmanHistoSorterData(const int pHisto[288]) {
		for(int i=0; i<288; ++i) {
			mHisto[i] = (pHisto[i] << 9) + 287 - i;
		}
	}

	int mHisto[288];
};

///////////////////////////////////////////////////////////////////////////

class VDDeflateHuffmanTable {
public:
	VDDeflateHuffmanTable();

	void Init();

	inline void Tally(int c) {
		++mHistogram[c];
	}

	inline void Tally(int c, int count) {
		mHistogram[c] += count;
	}

	void BuildCode(int depth_limit = 15);
	void BuildEncodingTable(uint16 *p, int *l, int limit);
	void BuildStaticLengthEncodingTable(uint16 *p, int *l);
	void BuildStaticDistanceEncodingTable(uint16 *p, int *l);

	uint32 GetCodeCount(int limit) const;
	uint32 GetOutputSize() const;
	uint32 GetStaticOutputSize() const;

	const uint16 *GetDHTSegment() { return mDHT; }
	int GetDHTSegmentLen() const { return mDHTLength; }

private:
	int mHistogram[288];
	int mHistogram2[288];
	uint16 mDHT[288+16];
	int mDHTLength;
};

VDDeflateHuffmanTable::VDDeflateHuffmanTable() {
	Init();
}

void VDDeflateHuffmanTable::Init() {
	std::fill(mHistogram, mHistogram+288, 0);
}

void VDDeflateHuffmanTable::BuildCode(int depth_limit) {
	int i;
	int nonzero_codes = 0;

	for(i=0; i<288; ++i) {
		mDHT[i+16] = i;
		if (mHistogram[i])
			++nonzero_codes;
		mHistogram2[i] = mHistogram[i];
	}

	// Codes are stored in the second half of the DHT segment in decreasing
	// order of frequency.
	std::sort(&mDHT[16], &mDHT[16+288], [&](int f1, int f2) { return mHistogram[f1] > mHistogram[f2]; });
	mDHTLength = 16 + nonzero_codes;

	// Sort histogram in increasing order.

	std::sort(mHistogram, mHistogram+288);

	int *A = mHistogram+288 - nonzero_codes;

	// Begin merging process (from "In-place calculation of minimum redundancy codes" by A. Moffat and J. Katajainen)
	//
	// There are three merging possibilities:
	//
	// 1) Leaf node with leaf node.
	// 2) Leaf node with internal node.
	// 3) Internal node with internal node.

	int leaf = 2;					// Next, smallest unattached leaf node.
	int internal = 0;				// Next, smallest unattached internal node.

	// Merging always creates one internal node and eliminates one node from
	// the total, so we will always be doing N-1 merges.

	A[0] += A[1];		// First merge is always two leaf nodes.
	for(int next=1; next<nonzero_codes-1; ++next) {		// 'next' is the value that receives the next unattached internal node.
		int a, b;

		// Pick first node.
		if (leaf < nonzero_codes && A[leaf] <= A[internal]) {
			A[next] = a=A[leaf++];			// begin new internal node with P of smallest leaf node
		} else {
			A[next] = a=A[internal];		// begin new internal node with P of smallest internal node
			A[internal++] = next;					// hook smallest internal node as child of new node
		}

		// Pick second node.
		if (internal >= next || (leaf < nonzero_codes && A[leaf] <= A[internal])) {
			A[next] += b=A[leaf++];			// complete new internal node with P of smallest leaf node
		} else {
			A[next] += b=A[internal];		// complete new internal node with P of smallest internal node
			A[internal++] = next;					// hook smallest internal node as child of new node
		}
	}

	// At this point, we have a binary tree composed entirely of pointers to
	// parents, partially sorted such that children are always before their
	// parents in the array.  Traverse the array backwards, replacing each
	// node with its depth in the tree.

	A[nonzero_codes-2] = 0;		// root has height 0 (0 bits)
	for(i = nonzero_codes-3; i>=0; --i)
		A[i] = A[A[i]]+1;		// child height is 1+height(parent).

	// Compute canonical tree bit depths for first part of DHT segment.
	// For each internal node at depth N, add two counts at depth N+1
	// and subtract one count at depth N.  Essentially, we are splitting
	// as we go.  We traverse backwards to ensure that no counts will drop
	// below zero at any time.

	std::fill(mDHT, mDHT+16, 0);

	int overallocation = 0;

	mDHT[0] = 2;		// 2 codes at depth 1 (1 bit)
	for(i = nonzero_codes-3; i>=0; --i) {
		int depth = A[i];

		// The optimal Huffman tree for N nodes can have a depth of N-1,
		// but we have to constrain ourselves at depth 15.  We simply
		// pile up counts at depth 15.  This causes us to overallocate the
		// codespace, but we will compensate for that later.

		if (depth >= depth_limit) {
			++mDHT[depth_limit-1];
		} else {
			--mDHT[depth-1];
			++mDHT[depth];
			++mDHT[depth];
		}
	}

	// Remove the extra code point.
	for(i=15; i>=0; --i) {
		if (mDHT[i])
			overallocation += mDHT[i] * (0x8000 >> i);
	}
	overallocation -= 0x10000;

	// We may have overallocated the codespace if we were forced to shorten
	// some codewords.

	if (overallocation > 0) {
		// Codespace is overallocated.  Begin lengthening codes from bit depth
		// 15 down until we are under the limit.

		i = depth_limit-2;
		while(overallocation > 0) {
			if (mDHT[i]) {
				--mDHT[i];
				++mDHT[i+1];
				overallocation -= 0x4000 >> i;
				if (i < depth_limit-2)
					++i;
			} else
				--i;
		}

		// We may be undercommitted at this point.  Raise codes from bit depth
		// 1 up until we are at the desired limit.

		int underallocation = -overallocation;

		i = 1;
		while(underallocation > 0) {
			if (mDHT[i] && (0x8000>>i) <= underallocation) {
				underallocation -= (0x8000>>i);
				--mDHT[i];
				--i;
				++mDHT[i];
			} else {
				++i;
			}
		}
	}
}

uint32 VDDeflateHuffmanTable::GetOutputSize() const {
	const uint16 *pCodes = mDHT+16;

	uint32 size = 0;

	for(int len=0; len<16; ++len) {
		int count = mDHT[len];

		uint32 points = 0;
		while(count--) {
			int code = *pCodes++;

			points += mHistogram2[code];
		}

		size += points * (len + 1);
	}

	return size;
}

uint32 VDDeflateHuffmanTable::GetCodeCount(int limit) const {
	return std::accumulate(mHistogram2, mHistogram2+limit, 0);
}

uint32 VDDeflateHuffmanTable::GetStaticOutputSize() const {
	uint32 sum7 = 0;
	uint32 sum8 = 0;
	uint32 sum9 = 0;
	sum8 = std::accumulate(mHistogram2+  0, mHistogram2+144, sum8);
	sum9 = std::accumulate(mHistogram2+144, mHistogram2+256, sum9);
	sum7 = std::accumulate(mHistogram2+256, mHistogram2+280, sum7);
	sum8 = std::accumulate(mHistogram2+280, mHistogram2+288, sum8);

	return 7*sum7 + 8*sum8 + 9*sum9;
}

static unsigned revword15(unsigned x) {
	unsigned y = 0;
	for(int i=0; i<15; ++i) {
		y = y + y + (x&1);
		x >>= 1;
	}
	return y;
}

void VDDeflateHuffmanTable::BuildEncodingTable(uint16 *p, int *l, int limit) {
	const uint16 *pCodes = mDHT+16;

	uint16 total = 0;
	uint16 inc = 0x4000;

	for(int len=0; len<16; ++len) {
		int count = mDHT[len];

		while(count--) {
			int code = *pCodes++;

			l[code] = len+1;
		}

		for(int k=0; k<limit; ++k) {
			if (l[k] == len+1) {
				p[k] = revword15(total) << (16 - (len+1));
				total += inc;
			}
		}
		inc >>= 1;
	}
}

void VDDeflateHuffmanTable::BuildStaticLengthEncodingTable(uint16 *p, int *l) {
	memset(mDHT, 0, sizeof(mDHT[0])*16);
	mDHT[6] = 24;
	mDHT[7] = 152;
	mDHT[8] = 112;

	uint16 *dst = mDHT + 16;
	for(int i=256; i<280; ++i)
		*dst++ = i;
	for(int i=0; i<144; ++i)
		*dst++ = i;
	for(int i=280; i<288; ++i)
		*dst++ = i;
	for(int i=144; i<256; ++i)
		*dst++ = i;

	BuildEncodingTable(p, l, 288);
}

void VDDeflateHuffmanTable::BuildStaticDistanceEncodingTable(uint16 *p, int *l) {
	memset(mDHT, 0, sizeof(mDHT[0])*16);
	mDHT[4] = 32;

	for(int i=0; i<32; ++i)
		mDHT[i+16] = i;

	BuildEncodingTable(p, l, 32);
}

class VDDeflateEncoder {
	VDDeflateEncoder(const VDDeflateEncoder&) = delete;
	VDDeflateEncoder& operator=(const VDDeflateEncoder&) = delete;
public:
	VDDeflateEncoder() = default;

	void SetCompressionLevel(VDDeflateCompressionLevel level);

	void Init(bool quick, vdfunction<void(const void *, uint32)> preProcessFn, vdfunction<void(const void *, uint32)> writeFn);
	void Write(const void *src, size_t len);
	void ForceNewBlock();
	void Finish();

protected:
	void EndBlock(bool term);
	void Compress(bool flush);

	template<VDDeflateCompressionLevel T_CompressionLevel>
	void Compress2(bool flush);

	void VDFORCEINLINE PutBits(uint32 encoding, int enclen);
	void FlushBits();
	void FlushOutput();
	uint32 Flush(int n, int ndists, bool term, bool test);

	uint32	mAccum;
	int		mAccBits;
	uint32	mHistoryPos;
	uint32	mHistoryTail;
	uint32	mHistoryBase;
	uint32	mHistoryBlockStart;
	uint32	mLenExtraBits;
	uint32	mPendingLen;
	uint8	*mpLen;
	uint16	*mpCode;
	uint16	*mpDist;

	uint32	mWindowLimit;
	uint32	mPreprocessPos = 0;
	VDDeflateCompressionLevel mCompressionLevel = VDDeflateCompressionLevel::Best;

	vdfunction<void(const void *, uint32)> mpPreProcessFn;
	vdfunction<void(const void *, uint32)> mpOutputFn;
	uint32	mOutputLevel = 0;
	uint8	mOutputBuf[4096 + 4];

	// Block coding tables
	uint16	mCodeEnc[288];
	int		mCodeLen[288];

	uint16	mDistEnc[32];
	int		mDistLen[32];

	uint8	mHistoryBuffer[65536+6];
	sint32	mHashNext[32768];
	sint32	mHashTable[65536];
	uint8	mLenBuf[32769];
	uint16	mCodeBuf[32769];
	uint16	mDistBuf[32769];
};

void VDDeflateEncoder::SetCompressionLevel(VDDeflateCompressionLevel level) {
	mCompressionLevel = level;
}

void VDDeflateEncoder::Init(bool quick, vdfunction<void(const void *, uint32)> preProcessFn, vdfunction<void(const void *, uint32)> writeFn) {
	std::fill(mHashNext, mHashNext+32768, -0x20000);
	std::fill(mHashTable, mHashTable+65536, -0x20000);

	mWindowLimit = quick ? 1024 : 32768;

	mpLen = mLenBuf;
	mpCode = mCodeBuf;
	mpDist = mDistBuf;
	mHistoryPos = 0;
	mHistoryTail = 0;
	mHistoryBase = 0;
	mHistoryBlockStart = 0;
	mLenExtraBits = 0;
	mPendingLen = 0;
	mAccum = 0;
	mAccBits = 0;

	mpOutputFn = std::move(writeFn);
	mpPreProcessFn = std::move(preProcessFn);

	mOutputBuf[0] = 0x78;		// 32K window, Deflate
	mOutputBuf[1] = 0xDA;		// maximum compression, no dictionary, check offset = 0x1A
	mOutputLevel = 0;
}

void VDDeflateEncoder::Write(const void *src, size_t len) {
	while(len > 0) {
		uint32 tc = sizeof mHistoryBuffer - mHistoryTail;

		if (!tc) {
			Compress(false);
			continue;
		}

		if ((size_t)tc > len)
			tc = (uint32)len;

		memcpy(mHistoryBuffer + mHistoryTail, src, tc);

		mHistoryTail += tc;
		src = (const char *)src + tc;
		len -= tc;
	}
}

void VDDeflateEncoder::ForceNewBlock() {
	Compress(false);
	EndBlock(false);
}

#define HASH(pos) ((((uint32)hist[(pos)  ] << 8) + ((uint32)hist[(pos)+1] << 4) + ((uint32)hist[(pos)+2] << 0)) & 0xffff)

void VDDeflateEncoder::EndBlock(bool term) {
	if (mpCode > mCodeBuf) {
		if (mPendingLen) {
			const uint8 *hist = mHistoryBuffer - mHistoryBase;
			int bestlen = mPendingLen - 1;
			mPendingLen = 0;

			while(bestlen-- > 0) {
				int hval = HASH(mHistoryPos);
				mHashNext[mHistoryPos & 0x7fff] = mHashTable[hval];
				mHashTable[hval] = mHistoryPos;
				++mHistoryPos;
			}
		}

		*mpCode++ = 256;
		Flush((int)(mpCode - mCodeBuf), (int)(mpDist - mDistBuf), term, false);
		mpCode = mCodeBuf;
		mpDist = mDistBuf;
		mpLen = mLenBuf;
		mHistoryBlockStart = mHistoryPos;
		mLenExtraBits = 0;
	} else if (term) {
		// We have no data pending, but need to emit the terminator -- this generally
		// only occurs if the stream is empty. Emit an empty block. Our best bet
		// is a static block, as it avoids the overhead of the length bytes and
		// byte alignment. The encoding is %1 for final block, %01 for static coding,
		// and %0000000 for EOB.
		PutBits(0b0'0000000'01'1U << (32-10), 10);
	}
}


void VDDeflateEncoder::Compress(bool flush) {
	switch(mCompressionLevel) {
		case VDDeflateCompressionLevel::Best:
			return Compress2<VDDeflateCompressionLevel::Best>(flush);

		case VDDeflateCompressionLevel::Quick:
			return Compress2<VDDeflateCompressionLevel::Quick>(flush);
	}
}

template<VDDeflateCompressionLevel T_CompressionLevel>
void VDDeflateEncoder::Compress2(bool flush) {
	using namespace nsVDDeflate;

	uint8	*lenptr = mpLen;
	uint16	*codeptr = mpCode;
	uint16	*distptr = mpDist;

	const uint8 *hist = mHistoryBuffer - mHistoryBase;

	uint32 pos = mHistoryPos;
	const uint32 len = mHistoryBase + mHistoryTail;
	const uint32 maxpos = flush ? len : len > 258+3 ? len - (258+3) : 0;		// +6 is for the 3-byte hash.

	if (mPreprocessPos < len) {
		mpPreProcessFn(mHistoryBuffer + (mPreprocessPos - mHistoryBase), len - mPreprocessPos);
		mPreprocessPos = len;
	}

	while(pos < maxpos) {
		if (codeptr >= mCodeBuf + 32768) {
			mpCode = codeptr;
			mpDist = distptr;
			mpLen = lenptr;
			mHistoryPos = pos;
			EndBlock(false);
			pos = mHistoryPos;
			codeptr = mpCode;
			distptr = mpDist;
			lenptr = mpLen;

			// Note that it's possible for the EndBlock() to have flushed out a pending
			// run and pushed us all the way to maxpos.
			VDASSERT(pos <= mHistoryBase + mHistoryTail);
			continue;
		}

		uint8 c = hist[pos];
		uint32 hcode = HASH(pos);

		sint32 hpos = mHashTable[hcode];
		uint32 limit = 258;
		if (limit > len-pos)
			limit = len-pos;

		sint32 hlimit = pos - mWindowLimit;		// note that our initial hash table values are low enough to avoid colliding with this.
		if (hlimit < 0)
			hlimit = 0;

		uint32 minmatch = mPendingLen > 3 ? mPendingLen : 3;
		uint32 bestlen = minmatch - 1;
		uint32 bestoffset = 0;

		if (hpos >= hlimit && limit >= minmatch) {
			sint32 hstart = hpos;
			const unsigned char *s2 = hist + pos;
			const uint16 matchWord1 = *(const uint16 *)s2;
			const uint8 matchWord2 = *(const uint8 *)(s2 + 2);
			uint32 hoffset = 0;

			[[maybe_unused]] uint32 patience = 16;

			do {
				const unsigned char *s1 = hist + hpos - hoffset;

				VDDEBUG_DEFLATE("testing %u %u (%02X%02X%02X %02X%02X%02X %02X %02X)\n", hpos, bestlen
					, hist[hpos]
					, hist[hpos+1]
					, hist[hpos+2]
					, s2[hoffset]
					, s2[hoffset+1]
					, s2[hoffset+2]
					, HASH(hpos)
					, HASH(pos + hoffset)
				);

				if (s1[bestlen] == s2[bestlen] && *(const uint16 *)s1 == matchWord1 && s1[2] == matchWord2) {
					uint32 mlen = 3;
					while(mlen < limit && s1[mlen] == s2[mlen])
						++mlen;

					// Check for a suboptimal match.
					//
					// The Deflate format requires additional raw bits for distance as the distance
					// increases. This means that it is not cost-effective to encode long distance
					// matches with too short of a length. The breakpoints are as follows:
					//
					//		Distance	Extra bits
					//			5			1
					//			9			2
					//			17			3
					//			33			4
					//			65			5
					//			129			6
					//			257			7
					//			513			8
					//			1025		9
					//			2049		10
					//			4097		11
					//			8193		12
					//			16385		13
					//
					// At least 1 bit is also needed for the distance encoding.
					//
					// This is also true for lengths, but with a given set of Huffman trees it is never
					// advantageous to code a shorter length. (It may be more efficient if it concentrates
					// the Huffman tree nodes, but we don't have visibility of that at this point.)
					//
					// If we assume we have a mostly full literal tree, any literals will cost 8-9 bits.
					// Therefore, we can apply a cost penalty of 1 byte for >=513.

					uint32 offset = (uint32)(s2 - s1);
					uint32 penalty = (offset >= 513) ? 1 : 0;

					if (mlen > bestlen + penalty) {
						bestoffset = offset;
						bestlen = mlen;

						if (mlen >= limit)
							break;

						if (mlen > 3) {
							// hop hash chains!
#if 1
							const uint32 diff = (mlen - 3) - hoffset;

							hlimit += diff;
							hpos += diff;
							if (hpos == pos)
								hpos = hstart;
							else
								hpos = mHashNext[hpos & 0x7fff];

							hoffset = mlen - 3;
#else
							const uint32 diff = (mlen - 2) - hoffset;

							hlimit += diff;
							hoffset = mlen - 2;
							hpos = mHashTable[HASH(pos + hoffset)];
#endif

						} else {
							hoffset = 1;
							++hlimit;

							hpos = mHashTable[HASH(pos + 1)];
						}
						continue;
					}
				}

				if constexpr (T_CompressionLevel == VDDeflateCompressionLevel::Quick) {
					if (!--patience)
						break;
				}

				hpos = mHashNext[hpos & 0x7fff];
			} while(hpos >= hlimit);
		}

		if (bestoffset) {
			// check for an illegal match
			VDASSERT((uint32)(bestoffset-1) < 32768U);
			VDASSERT(bestlen < 259);
			VDASSERT(!memcmp(hist+pos, hist+pos-bestoffset, bestlen));
			VDASSERT(pos >= bestoffset);
			VDASSERT(pos+bestlen <= len);
			VDASSERT(pos-bestoffset >= mHistoryBase);

			unsigned lcode = 0;
			while(bestlen >= len_pack_tbl[lcode+1])
				++lcode;

			*codeptr++ = lcode + 257;
			*distptr++ = bestoffset;
			*lenptr++ = bestlen - 3;
			mLenExtraBits += len_bits_tbl[lcode];

			VDDEBUG_DEFLATE("%u match: (%u, %u)\n", pos, bestoffset, bestlen);
		} else {
			VDDEBUG_DEFLATE("%u literal %02X\n", pos, c);
			*codeptr++ = c;
			bestlen = 1;
		}

		// Lazy matching.
		//
		//	prev	current		compare		action
		//	======================================
		//	lit		lit						append
		//	lit		match					stash
		//	match	lit						retire
		//	match	match		shorter		retire
		//	match	match		longer		obsolete
		VDASSERT(pos+bestlen <= mHistoryBase + mHistoryTail);

		if (!mPendingLen) {
			// no pending match -- make the new match pending if we have one
			if (bestlen > 1) {
				mPendingLen = bestlen;
				bestlen = 1;
			}
		} else {
			// 
			if (bestlen > mPendingLen) {
				// new match is better than the pending match -- truncate the previous
				// match in favor of the new one
				codeptr[-2] = hist[pos - 1];
				distptr[-2] = distptr[-1];
				--distptr;
				lenptr[-2] = lenptr[-1];
				--lenptr;
				mPendingLen = bestlen;
				bestlen = 1;
			} else {
				// pending match is better -- keep that and discard the one we just found
				--codeptr;
				if (bestlen > 1) {
					--distptr;
					--lenptr;
				}

				bestlen = mPendingLen - 1;
				mPendingLen = 0;
			}
		}

		VDASSERT(pos+bestlen <= mHistoryBase + mHistoryTail);

		if (bestlen > 0) {
			mHashNext[pos & 0x7fff] = mHashTable[hcode];
			mHashTable[hcode] = pos;
			++pos;

			while(--bestlen) {
				uint32 hcode = HASH(pos);
				mHashNext[pos & 0x7fff] = mHashTable[hcode];
				mHashTable[hcode] = pos;
				++pos;
			}
		}
	}

	// shift down by 32K
	if (pos - mHistoryBase >= 49152) {
		uint32 delta = (pos - 32768) - mHistoryBase;
		memmove(mHistoryBuffer, mHistoryBuffer + delta, mHistoryTail - delta);
		mHistoryBase += delta;
		mHistoryTail -= delta;
	}

	mHistoryPos = pos;
	mpLen = lenptr;
	mpCode = codeptr;
	mpDist = distptr;
}

void VDDeflateEncoder::Finish() {
	while(mHistoryPos != mHistoryBase + mHistoryTail)
		Compress(true);

	// we may get here with no codes in the unique case of an empty stream
	EndBlock(true);

	FlushBits();
	FlushOutput();
}

void VDFORCEINLINE VDDeflateEncoder::PutBits(uint32 encoding, int enclen) {
	mAccum >>= enclen;
	mAccum += encoding;
	mAccBits += enclen;
	VDASSERT(mAccBits >= -16 && mAccBits < 32);

	if (mAccBits >= 16) {
		mAccBits -= 16;

		if (vdcountof(mOutputBuf) - mOutputLevel < 2) {
			mpOutputFn(mOutputBuf, mOutputLevel);
			mOutputLevel = 0;
		}

		mOutputBuf[mOutputLevel++] = mAccum >> (16-mAccBits);
		mOutputBuf[mOutputLevel++] = mAccum >> (24-mAccBits);
	}		
}

void VDDeflateEncoder::FlushBits() {
	if (vdcountof(mOutputBuf) - mOutputLevel < 4)
		FlushOutput();

	while(mAccBits > 0) {
		mOutputBuf[mOutputLevel++] = (uint8)(mAccum >> (32-mAccBits));
		mAccBits -= 8;
	}
}

void VDDeflateEncoder::FlushOutput() {
	if (mOutputLevel) {
		mpOutputFn(mOutputBuf, mOutputLevel);
		mOutputLevel = 0;
	}
}

uint32 VDDeflateEncoder::Flush(int n, int ndists, bool term, bool test) {
	using namespace nsVDDeflate;

	const uint16 *codes = mCodeBuf;
	const uint8 *lens = mLenBuf;
	const uint16 *dists = mDistBuf;

	VDDeflateHuffmanTable htcodes, htdists, htlens;
	int i;

	memset(mCodeLen, 0, sizeof mCodeLen);
	memset(mDistLen, 0, sizeof mDistLen);

	for(i=0; i<n; ++i)
		htcodes.Tally(codes[i]);

	htcodes.BuildCode(15);

	for(i=0; i<ndists; ++i) {
		int c=0;
		while(dists[i] >= dist_tbl[c+1])
			++c;

		htdists.Tally(c);
	}

	htdists.BuildCode(15);

	int totalcodes = 286;
	int totaldists = 30;
	int totallens = totalcodes + totaldists;

	htcodes.BuildEncodingTable(mCodeEnc, mCodeLen, 288);
	htdists.BuildEncodingTable(mDistEnc, mDistLen, 32);

	// RLE the length table
	uint8 lenbuf[286+30+1];
	uint8 *lendst = lenbuf;
	uint8 rlebuf[286+30+1];
	uint8 *rledst = rlebuf;

	for(i=0; i<totalcodes; ++i)
		*lendst++ = mCodeLen[i];

	for(i=0; i<totaldists; ++i)
		*lendst++ = mDistLen[i];

	*lendst = 255;		// avoid match

	int last = -1;
	uint32 treeExtraBits = 0;
	i=0;
	while(i<totallens) {
		if (!lenbuf[i] && !lenbuf[i+1] && !lenbuf[i+2]) {
			int j;
			for(j=3; j<138 && !lenbuf[i+j]; ++j)
				;
			if (j < 11) {
				*rledst++ = 17;
				*rledst++ = j-3;
				treeExtraBits += 3;
			} else {
				*rledst++ = 18;
				*rledst++ = j-11;
				treeExtraBits += 7;
			}
			htlens.Tally(rledst[-2]);
			i += j;
			last = 0;
		} else if (lenbuf[i] == last && lenbuf[i+1] == last && lenbuf[i+2] == last) {
			int j;
			for(j=3; j<6 && lenbuf[i+j] == last; ++j)
				;
			*rledst++ = 16;
			htlens.Tally(16);
			*rledst++ = j-3;
			treeExtraBits += 2;
			i += j;
		} else {
			htlens.Tally(*rledst++ = lenbuf[i++]);
			last = lenbuf[i-1];
		}
	}

	htlens.BuildCode(7);

	// compute bits for dynamic encoding
	uint32 blockSize = mHistoryPos - mHistoryBlockStart;
	uint32 alignBits = -(mAccBits+3) & 7;
	uint32 dynamicBlockBits = htcodes.GetOutputSize() + htdists.GetOutputSize() + mLenExtraBits + htlens.GetOutputSize() + 14 + 19*3 + treeExtraBits;
	uint32 staticBlockBits = htcodes.GetStaticOutputSize() + htdists.GetCodeCount(32)*5 + mLenExtraBits;
	uint32 storeBlockBits = blockSize*8 + 32 + alignBits;

	if (storeBlockBits < dynamicBlockBits && storeBlockBits < staticBlockBits) {
		if (test)
			return storeBlockBits;

		PutBits((term ? 0x20000000 : 0) + (0 << 30), 3);

		// align to byte boundary
		PutBits(0, alignBits);

		// write block size
		PutBits((blockSize << 16) & 0xffff0000, 16);
		PutBits((~blockSize << 16) & 0xffff0000, 16);

		// write the block.
		FlushBits();
		FlushOutput();

		const uint8 *base = &mHistoryBuffer[mHistoryBlockStart - mHistoryBase];
		if (blockSize)
			mpOutputFn(base, blockSize);
	} else {
		if (dynamicBlockBits < staticBlockBits) {
			if (test)
				return dynamicBlockBits;

			PutBits((term ? 0x20000000 : 0) + (2 << 30), 3);

			PutBits((totalcodes - 257) << 27, 5);	// code count - 257
			PutBits((totaldists - 1) << 27, 5);	// dist count - 1
			PutBits(0xf0000000, 4);	// ltbl count - 4

			uint16 hlenc[19];
			int hllen[19]={0};
			htlens.BuildEncodingTable(hlenc, hllen, 19);

			for(i=0; i<19; ++i) {
				int k = hclen_tbl[i];

				PutBits(hllen[k] << 29, 3);
			}

			uint8 *rlesrc = rlebuf;
			while(rlesrc < rledst) {
				uint8 c = *rlesrc++;
				PutBits((uint32)hlenc[c] << 16, hllen[c]);

				if (c == 16)
					PutBits((uint32)*rlesrc++ << 30, 2);
				else if (c == 17)
					PutBits((uint32)*rlesrc++ << 29, 3);
				else if (c == 18)
					PutBits((uint32)*rlesrc++ << 25, 7);
			}
		} else {
			if (test)
				return staticBlockBits;

			PutBits((term ? 0x20000000 : 0) + (1 << 30), 3);

			memset(mCodeLen, 0, sizeof(mCodeLen));
			memset(mDistLen, 0, sizeof(mDistLen));
			htcodes.BuildStaticLengthEncodingTable(mCodeEnc, mCodeLen);
			htdists.BuildStaticDistanceEncodingTable(mDistEnc, mDistLen);
		}

		for(i=0; i<n; ++i) {
			unsigned code = *codes++;
			unsigned clen = mCodeLen[code];

			PutBits((uint32)mCodeEnc[code] << 16, clen);

			if (code >= 257) {
				unsigned extralenbits = len_bits_tbl[code-257];
				unsigned len = *lens++ + 3;

				VDASSERT(len >= len_pack_tbl[code-257]);
				VDASSERT(len < len_pack_tbl[code-256]);

				if (extralenbits)
					PutBits((len - len_tbl[code-257]) << (32 - extralenbits), extralenbits);

				unsigned dist = *dists++;
				int dcode=0;
				while(dist >= dist_tbl[dcode+1])
					++dcode;

				PutBits((uint32)mDistEnc[dcode] << 16, mDistLen[dcode]);

				unsigned extradistbits = dist_bits_tbl[dcode];

				if (extradistbits)
					PutBits((dist - dist_tbl[dcode]) << (32 - extradistbits), extradistbits);
			}
		}
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////

template<bool T_Enhanced>
void VDInflateStream<T_Enhanced>::Init(IVDStream *pSrc, uint64 limit, bool bStored) {
	mL2Buffer.Init(*pSrc, limit);
	mBits.init(mL2Buffer);
	mBlockType = kNoBlock;
	mReadPt = mWritePt = mBufferLevel = 0;
	mStoredBytesLeft = 0;
	mbNoMoreBlocks = false;

	if (bStored) {
		mStoredBytesLeft = (uint32)limit;
		mbNoMoreBlocks = true;
		mBlockType = kStoredBlock;
	}
}

template<bool T_Enhanced>
VDInflateStream<T_Enhanced>::~VDInflateStream() {
}

template<bool T_Enhanced>
void VDInflateStream<T_Enhanced>::VerifyCRC() const {
	if (mbCRCEnabled && CRC() != mExpectedCRC)
		throw MyError("Read error on compressed data (CRC error).");
}

template<bool T_Enhanced>
const wchar_t *VDInflateStream<T_Enhanced>::GetNameForError() {
	return mL2Buffer.GetSource().GetNameForError();
}

template<bool T_Enhanced>
sint64 VDInflateStream<T_Enhanced>::Pos() {
	return mPos;
}

template<bool T_Enhanced>
void VDInflateStream<T_Enhanced>::Read(void *buffer, sint32 bytes) {
	if (bytes != ReadData(buffer, bytes))
		throw VDDeflateDecompressionException();
}

template<bool T_Enhanced>
sint32 VDInflateStream<T_Enhanced>::ReadData(void *dst0, sint32 bytes) {
	sint32 actual = 0;

	uint8 *dst = (uint8 *)dst0;

	while(bytes > 0) {
		if (mBufferLevel > 0) {
			unsigned tc = std::min<unsigned>(mBufferLevel, bytes);
			unsigned bp = kBufferSize - mReadPt;

			if (bp < tc) {
				memcpy(dst, mBuffer+mReadPt, bp);
				memcpy(dst+bp, mBuffer, tc-bp);
				mReadPt = tc-bp;
			} else {
				memcpy(dst, mBuffer+mReadPt, tc);
				mReadPt += tc;
			}
			mBufferLevel -= tc;
			dst += tc;
			bytes -= tc;
			actual += tc;
		} else {
			uint32 origWritePt = mWritePt;
			uint32 origBufferLevel = mBufferLevel;

			if (!Inflate())
				break;

			if (mbCRCEnabled && mBufferLevel != origBufferLevel) {
				if (mWritePt <= origWritePt) {
					mCRCChecker.Process(mBuffer+origWritePt, 65536 - origWritePt);
					mCRCChecker.Process(mBuffer, mWritePt);
				} else {
					mCRCChecker.Process(mBuffer+origWritePt, mWritePt - origWritePt);
				}
			}
		}
	}

	mPos += actual;
	return actual;
}

template<bool T_Enhanced>
void VDInflateStream<T_Enhanced>::Write(const void *buffer, sint32 bytes) {
	throw MyError("Zip streams are read-only.");
}

template<bool T_Enhanced>
bool VDInflateStream<T_Enhanced>::Inflate() {
	if (mBlockType == kNoBlock) {
		if (mbNoMoreBlocks) {
			mBits.CheckEOF();
			return false;
		}

		ParseBlockHeader();
	}

	if (mBlockType == kStoredBlock) {
		while(mBufferLevel < kBufferSize) {
			if (mStoredBytesLeft <= 0) {
				mBlockType = kNoBlock;
				break;
			}
			uint32 tc = std::min<uint32>(kBufferSize - mWritePt, std::min<uint32>(kBufferSize - mBufferLevel, mStoredBytesLeft));

			mBits.readbytes(mBuffer + mWritePt, tc);

			mWritePt = (mWritePt + tc) & kBufferMask;
			mStoredBytesLeft -= tc;
			mBufferLevel += tc;
		}
	} else
		InflateBlock();

	return true;
}

template<bool T_Enhanced>
VDNOINLINE void VDInflateStream<T_Enhanced>::InflateBlock() {
	using namespace nsVDDeflate;

	size_t writePt = mWritePt;
	uint32 bufferLevel = mBufferLevel;

	uint8 *VDRESTRICT buffer = &mBuffer[0];
	auto bitReader = mBits;

	// We must always have enough space in the buffer to accommodate a full
	// run. For Deflate, this is 258 octets, but for enhanced Deflate, it's
	// a full 64K. We also need a little bit more space so we can overrun
	// with vector copies.
	while(bufferLevel < 65024) {
		// Max bit sequences we can encounter:
		//
		//	Literal[1..15]
		//	CopyLen[1..15] + CopyLenExtra[0..5] + Dist[1..15] + DistExtra[0..13] (Deflate)
		//	CopyLen[1..15] + CopyLenExtra[0..16] + Dist[1..15] + DistExtra[0..14] (Deflate64)
		//
		// Refill guarantees 24 bits for 32-bit and 56 bits for 64-bit. For
		// 32-bit, we can read CopyLenExtra without refilling for Deflate only.
		// For 64-bit, we can read the entire sequence (49 bits max) for Deflate,
		// but need one refill for Deflate64.

		uint32 codeWindow = bitReader.Peek32();
		const auto *VDRESTRICT quickCode = mCodeQuickDecode[codeWindow & kQuickCodeMask];
		uint32 code = quickCode[0];
		uint32 bits = quickCode[1];
		if (code >= kQuickCodes) {
			code = mCodeDecode[codeWindow & code];
			bits = code & 15;
			code >>= 4;
		}

		bitReader.Consume(bits);

		if (code >= 256) {
			if (code == 256) [[unlikely]] {
				mBlockType = kNoBlock;
				break;
			}

			code -= 257;

			unsigned len;
			if constexpr (T_Enhanced) {
				if constexpr (VDDeflateBitReader::kUsing64)
					len = len_tbl64[code] + bitReader.GetBitsUnchecked(len_bits_tbl64[code]);
				else
					len = len_tbl64[code] + bitReader.getbits(len_bits_tbl64[code]);
			} else {
				// We have at least 24 bits guaranteed available from the peek above, of
				// which at least 15 bits at most have been used, giving 9 left. The max
				// bits we consume here is 5. This is not safe for Deflate64 where the
				// last code can grab 16 bits.
				len = len_tbl[code] + bitReader.GetBitsUnchecked(len_bits_tbl[code]);
			}

			if (len < 3)	// can happen with a bad static block
				throw VDDeflateDecompressionException();

			uint32 distWindow;
			
			if constexpr (VDDeflateBitReader::kUsing64 && !T_Enhanced)
				distWindow = bitReader.PeekUnchecked32();
			else
				distWindow = bitReader.Peek32();

			const auto *VDRESTRICT distQuickCode = mDistQuickDecode[distWindow & kQuickCodeMask];
			uint32 dcode = distQuickCode[0];
			uint32 dbits = distQuickCode[1];
			if (dcode >= kQuickCodes) {
				dcode = mDistDecode[distWindow & dcode];
				dbits = mCodeLengths[dcode + 288];
			}

			bitReader.Consume(dbits);

			uint32 dist = dist_tbl[dcode];
			
			if constexpr (VDDeflateBitReader::kUsing64)
				dist += bitReader.GetBitsUnchecked(dist_bits_tbl[dcode]);
			else
				dist += bitReader.getbits(dist_bits_tbl[dcode]);

			VDDEBUG_INFLATE("copy (%u, %u)\n", dist, len);

			size_t copySrcOffset = (writePt - dist) & kBufferMask;

			bufferLevel += len;

			// NOTE: This can be a self-replicating copy.  It must be ascending and it must
			//		 be by bytes.
			if (((writePt > copySrcOffset ? writePt : copySrcOffset) + len) > kBufferSize) [[unlikely]] {
				// wrapped copy
				do {
					buffer[writePt] = buffer[copySrcOffset];
					++writePt;
					writePt &= kBufferMask;

					++copySrcOffset;
					copySrcOffset &= kBufferMask;
				} while(--len);
			} else {
				// unwrapped copy
				writePt += len;

				uint8 *copyDstEnd = &buffer[writePt];	// must use unwrapped value if exactly at EOB
				const uint8 *copySrcEnd = &buffer[copySrcOffset + len];
				ptrdiff_t copyOffset = -(ptrdiff_t)len;

				// check if we have a repeating copy
				if (dist >= len) {
					// Non-repeating -- copy vecs at a time. We use a larger buffer than the window
					// (64K > 32K or 128K > 64K) and don't allow the buffer to completely fill up,
					// so it is OK to overrun a bit.
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
					do {
						_mm_storeu_si128(
							(__m128i *)&copyDstEnd[copyOffset],
							_mm_loadu_si128((const __m128i *)&copySrcEnd[copyOffset])
						);

						copyOffset += 16;
					} while(copyOffset < 0);
#elif defined(VD_CPU_ARM64)
					do {
						vst1q_u8(&copyDstEnd[copyOffset], vld1q_u8(&copySrcEnd[copyOffset]));

						copyOffset += 16;
					} while(copyOffset < 0);
#else
#error Unaligned access not implemented
#endif
				} else {
					// Repeating -- must copy a byte at a time
					do {
						copyDstEnd[copyOffset] = copySrcEnd[copyOffset];
					} while(++copyOffset);
				}

				writePt &= kBufferMask;
			}
		} else {
			VDDEBUG_INFLATE("literal %u\n", code);
			buffer[writePt++] = code;
			writePt &= kBufferMask;
			++bufferLevel;
		}
	}

	mBits = bitReader;
	mBufferLevel = bufferLevel;
	mWritePt = (uint32)writePt;
}

namespace {
	static unsigned revword8(unsigned x) {
		x = (unsigned char )((x << 4) + (x >> 4));
		x = ((x << 2) & 0xcc) + ((x >> 2) & 0x33);
		return ((x << 1) & 0xaa) + ((x >> 1) & 0x55);
	}

	template<int T_Bits>
	constexpr unsigned revword(uint32 x) {
		if constexpr (T_Bits == 9) {
			const uint32 x4 = x & 0x10;

			x =    ((x << 5) & 0b0'1111'0'0000) + ((x >> 5) & 0b0'0000'0'1111);
			x =    ((x << 2) & 0b0'1100'0'1100) + ((x >> 2) & 0b0'0011'0'0011);
			return ((x << 1) & 0b0'1010'0'1010) + ((x >> 1) & 0b0'0101'0'0101) + x4;
		} else if constexpr (T_Bits == 10) {
			x =    ((x << 5) & 0b0'11111'00000) + ((x >> 5) & 0b0'00000'11111);

			const uint32 xmid = x & 0b0'00100'00100;
			x =    ((x << 3) & 0b0'11000'11000) + ((x >> 3) & 0b0'00011'00011);
			return ((x << 1) & 0b0'10010'10010) + ((x >> 1) & 0b0'01001'01001) + xmid;
		} else if constexpr (T_Bits == 11) {
			const uint32 xmid1 = x & 0b0'00000'1'00000;

			x =    ((x << 6) & 0b0'11111'0'00000) + ((x >> 6) & 0b0'00000'0'11111);

			const uint32 xmid2 = xmid1 + (x & 0b0'00100'0'00100);
			x =    ((x << 3) & 0b0'11000'0'11000) + ((x >> 3) & 0b0'00011'0'00011);
			return ((x << 1) & 0b0'10010'0'10010) + ((x >> 1) & 0b0'01001'0'01001) + xmid2;
		} else if constexpr (T_Bits == 12) {
			x =    ((x << 6) & 0b0'111111'000000) + ((x >> 6) & 0b0'000000'111111);
			x =    ((x << 3) & 0b0'111000'111000) + ((x >> 3) & 0b0'000111'000111);
			return ((x << 2) & 0b0'100100'100100) + ((x >> 2) & 0b0'001001'001001) + (x & 0b0'010010'010010);
		} else {
			return sizeof(int[-T_Bits]);
		}
	}

	static_assert(revword<10>(0b0'0000000001) == 0b0'1000000000);
	static_assert(revword<10>(0b0'0000000010) == 0b0'0100000000);
	static_assert(revword<10>(0b0'0000000100) == 0b0'0010000000);
	static_assert(revword<10>(0b0'0000001000) == 0b0'0001000000);
	static_assert(revword<10>(0b0'0000010000) == 0b0'0000100000);
	static_assert(revword<10>(0b0'0000100000) == 0b0'0000010000);
	static_assert(revword<10>(0b0'0001000000) == 0b0'0000001000);
	static_assert(revword<10>(0b0'0010000000) == 0b0'0000000100);
	static_assert(revword<10>(0b0'0100000000) == 0b0'0000000010);
	static_assert(revword<10>(0b0'1000000000) == 0b0'0000000001);

	static unsigned revword15(unsigned x) {
		x = ((x << 8) & 0xff00) + ((x >> 8) & 0x00ff);
		x = ((x << 4) & 0xf0f0) + ((x >> 4) & 0x0f0f);
		x = ((x << 2) & 0xcccc) + ((x >> 2) & 0x3333);
		return (x & 0x5555) + ((x >> 2) & 0x2aaa);
	}

	static bool InflateExpandTable256(unsigned char *dst, unsigned char *lens, unsigned codes) {
		unsigned	k;
		unsigned	ki;
		unsigned	base=0;

		for(unsigned i=1; i<16; ++i) {
			ki = 1<<i;

			for(unsigned j=0; j<codes; ++j) {
				if (lens[j] == i) {
					for(k=base; k<0x100; k+=ki)
						dst[k] = j;

					base = revword8((revword8(base)+(0x100 >> i)) & 0xff);
				}
			}
		}

		return !base;
	}

//#define VD_INFLATE_PROFILE_TREE_32K

	template<uint32 T_QuickBits, typename T_Quick, typename T_Full>
	static bool InflateExpandTable32K(T_Full *dst, T_Quick (*quickDst)[2], uint8 *lens, uint32 codes) {
#ifdef VD_INFLATE_PROFILE_TREE_32K
		uint32 quickCodeSpaceUsed = 0;
#endif

		constexpr uint32 kQuickCodes = 1 << T_QuickBits;
		constexpr uint32 kQuickCodeMask = kQuickCodes - 1;

		uint32 codesUsed = 0;
		uint32 codeSpaceUsed = 0;
		uint32 lastCode = 0;
		uint32 maxLen = 0;

		for(uint32 i=0; i<codes; ++i) {
			uint8 len = lens[i];
			if (len) {
				lastCode = i;
				++codesUsed;
				codeSpaceUsed += 0x8000 >> len;
				if (maxLen < len)
					maxLen = len;

#ifdef VD_INFLATE_PROFILE_TREE_32K
				if (len <= T_QuickBits)
					quickCodeSpaceUsed += kQuickCodes >> len;
#endif
			}
		}

#ifdef VD_INFLATE_PROFILE_TREE_32K
		VDDEBUG2("%d/32768 (%.2f%%) within %d-bit | maxlen %d/15 for %d codes\n"
			, quickCodeSpaceUsed << 6
			, (float)quickCodeSpaceUsed / 32768.0f * 100.0f
			, T_QuickBits
			, maxLen
			, codes);
#endif

		// check for tree completeness
		if (codeSpaceUsed != 0x8000) {
			// Two special cases:
			//
			// 1) If there is exactly one distance code, then it must be coded
			//    as an incomplete tree with a single 1-bit code.
			//
			// 2) If there are no distance codes, then it is coded as a single
			//    code of length zero.
			//
			if (codesUsed == 1 && codeSpaceUsed == 0x4000) {
				// we don't need the full table, the quick table suffices
				for(int i=0; i<kQuickCodes; ++i) {
					quickDst[i][0] = T_Quick(lastCode);
					quickDst[i][1] = 1;
				}

				return true;
			} else if (codeSpaceUsed == 0) {
				for(int i=0; i<kQuickCodes; ++i) {
					quickDst[i][0] = T_Quick(0);
					quickDst[i][1] = 1;
				}

				return true;
			}

			// invalid incomplete tree
			throw VDDeflateDecompressionException();
		}

		// populate quick (9-bit) table
		constexpr bool canEncodeQuickMask = std::numeric_limits<T_Quick>().max() >= 0xFFFF;
		constexpr bool canEncodeFullLen = std::numeric_limits<T_Full>().max() >= 0xFFFF;

		for(int i = 0; i < kQuickCodes; ++i) {
			if constexpr (canEncodeQuickMask)
				quickDst[i][0] = ~(~T_Quick(0) >> 1);
			else
				quickDst[i][0] = ~T_Quick(0);

			quickDst[i][1] = 0;
		}

		uint32 quickBase = 0;
		for(int len = 1; len <= T_QuickBits; ++len) {
			uint32 ki = 1 << len;

			for(unsigned j = 0; j < codes; ++j) {
				if (lens[j] == len) {
					for(uint32 code = quickBase; code < kQuickCodes; code += ki) {
						quickDst[code][0] = j;
						quickDst[code][1] = len;
					}

					quickBase = revword<T_QuickBits>(revword<T_QuickBits>(quickBase)+(kQuickCodes >> len));
				}
			}
		}

		// populate full (15-bit) table if any codes longer than 9 bit exist
		if (maxLen > T_QuickBits) {
			unsigned base = quickBase;

			// we only need 10 bit codes and beyond
			for(int len = T_QuickBits + 1; len < 16; ++len) {
				uint32 ki = 1 << len;

				for(unsigned j=0; j<codes; ++j) {
					if (lens[j] == len) {
						if constexpr (canEncodeQuickMask)
							quickDst[base & kQuickCodeMask][0] = (quickDst[base & kQuickCodeMask][0] & 0x7FFF) | ((1U << len) - 1);

						T_Full encoding(j);

						if constexpr (canEncodeFullLen)
							encoding = T_Full((encoding << 4) + len);

						for(uint32 code = base; code < 0x8000; code += ki)
							dst[code] = encoding;

						base = revword15(revword15(base)+(0x8000 >> len));
					}
				}
			}
		}

		return true;
	}
}

template<bool T_Enhanced>
void VDInflateStream<T_Enhanced>::ParseBlockHeader() {
	unsigned char ltbl_lengths[20];
	unsigned char ltbl_decode[256];

	if (mBits.getbit())
		mbNoMoreBlocks = true;

	unsigned type = mBits.getbits(2);

	switch(type) {
	case 0:		// stored
		{
			mBits.align();

			mStoredBytesLeft = mBits.getbits(16);

			const uint32 invCount = mBits.getbits(16);

			if ((uint16)~invCount != mStoredBytesLeft)
				throw VDDeflateDecompressionException();

			mBlockType = kStoredBlock;
		}
		break;
	case 1:		// static trees
		{
			int i;

			for(i=0; i<144; ++i) mCodeLengths[i] = 8;
			for(   ; i<256; ++i) mCodeLengths[i] = 9;
			for(   ; i<280; ++i) mCodeLengths[i] = 7;
			for(   ; i<288; ++i) mCodeLengths[i] = 8;
			for(i=0; i< 32; ++i) mCodeLengths[i+288] = 5;

			if (!InflateExpandTable32K<kQuickBits>(mCodeDecode, mCodeQuickDecode, mCodeLengths, 288))
				throw VDDeflateDecompressionException();

			if (!InflateExpandTable32K<kQuickBits>(mDistDecode, mDistQuickDecode, mCodeLengths+288, 32))
				throw VDDeflateDecompressionException();

			mBlockType = kDeflatedBlock;
		}
		break;
	case 2:		// dynamic trees
		{
			const unsigned	code_count	= mBits.getbits(5) + 257;
			const unsigned	dist_count	= mBits.getbits(5) + 1;
			const unsigned	total_count	= code_count + dist_count;
			const unsigned	ltbl_count	= mBits.getbits(4) + 4;

			// decompress length table tree
			memset(ltbl_lengths, 0, sizeof ltbl_lengths);

			static const unsigned char hclen_tbl[]={
				16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
			};

			for(unsigned i=0; i<ltbl_count; ++i) {
				ltbl_lengths[hclen_tbl[i]] = mBits.getbits(3);
			}

			if (!InflateExpandTable256(ltbl_decode, ltbl_lengths, 20))
				throw VDDeflateDecompressionException();

			// decompress length table

			unsigned j=0;
			unsigned last = 0;
			while(j < total_count) {
				unsigned k = ltbl_decode[0xff & mBits.Peek32()];
				unsigned run = 1;

				mBits.Consume(ltbl_lengths[k]);

				switch(k) {
				case 16:	// last run of 3-6
					run = mBits.getbits(2) + 3;
					break;
				case 17:	// zero run of 3-10
					run = mBits.getbits(3) + 3;
					last = 0;
					break;
				case 18:	// zero run of 11-138
					run = mBits.getbits(7) + 11;
					last = 0;
					break;
				default:
					last = k;
				}

				if (run+j > total_count)
					throw VDDeflateDecompressionException();

				do {
					mCodeLengths[j++] = last;
				} while(--run);
			}

			memmove(mCodeLengths + 288, mCodeLengths + code_count, dist_count);

			if (!InflateExpandTable32K<kQuickBits>(mCodeDecode, mCodeQuickDecode, mCodeLengths, code_count))
				throw VDDeflateDecompressionException();

			if (!InflateExpandTable32K<kQuickBits>(mDistDecode, mDistQuickDecode, mCodeLengths+288, dist_count))
				throw VDDeflateDecompressionException();

			mBlockType = kDeflatedBlock;
		}
		break;

	default:
		throw VDDeflateDecompressionException();
	}
}

template class VDInflateStream<false>;
template class VDInflateStream<true>;

///////////////////////////////////////////////////////////////////////////

#pragma pack(push, 2)

namespace {
	enum {
		kZipMethodStore		= 0,
		kZipMethodDeflate	= 8,
		kZipMethodEnhancedDeflate	= 9
	};

	struct ZipFileHeader {
		enum { kSignature = 0x04034b50 };
		uint32		signature;
		uint16		version_required;
		uint16		flags;
		uint16		method;
		uint16		mod_time;
		uint16		mod_date;
		uint32		crc32;
		uint32		compressed_size;
		uint32		uncompressed_size;
		uint16		filename_len;
		uint16		extrafield_len;
	};

	struct ZipDataDescriptor {
		uint32		crc32;
		uint32		compressed_size;
		uint32		uncompressed_size;
	};

	struct ZipFileEntry {
		enum { kSignature = 0x02014b50 };
		uint32		signature;
		uint16		version_create;
		uint16		version_required;
		uint16		flags;
		uint16		method;
		uint16		mod_time;
		uint16		mod_date;
		uint32		crc32;
		uint32		compressed_size;
		uint32		uncompressed_size;
		uint16		filename_len;
		uint16		extrafield_len;
		uint16		comment_len;
		uint16		diskno;
		uint16		internal_attrib;
		uint32		external_attrib;
		uint32		reloff_localhdr;
	};

	struct ZipCentralDir {
		enum { kSignature = 0x06054b50 };

		uint32		signature;
		uint16		diskno;
		uint16		diskno_dir;
		uint16		dirents;
		uint16		dirents_total;
		uint32		dirsize;
		uint32		diroffset;
		uint16		comment_len;
	};
}

#pragma pack(pop)

VDZipArchive::VDZipArchive() {
}

VDZipArchive::~VDZipArchive() {
}

void VDZipArchive::Init(IVDRandomAccessStream *pSrc) {
	mpStream = pSrc;

	// First, see if the central directory is at the end (common case).
	const sint64 streamLen = mpStream->Length();

	if (streamLen < sizeof(ZipCentralDir)) {
		if (streamLen == 0)
			throw MyError("The .zip file is empty.");
		else
			throw MyError("The file is too short to be a .zip archive.");
	}

	mpStream->Seek(streamLen - sizeof(ZipCentralDir));

	ZipCentralDir cdirhdr;

	mpStream->Read(&cdirhdr, sizeof cdirhdr);
	if (cdirhdr.signature != ZipCentralDir::kSignature) {
		// Okay, the central directory isn't at the end. Read the last 64K of the file
		// and see if we can spot it. 
		uint32 buflen = 65536 + sizeof(ZipCentralDir);

		if ((sint64)buflen > streamLen)
			buflen = (uint32)streamLen;

		vdfastvector<uint8> buf(buflen);
		const uint8 *bufp = buf.data();

		const sint64 bufOffset = streamLen - buflen;
		mpStream->Seek(bufOffset);
		mpStream->Read(buf.data(), buflen);

		// Search for valid end-of-central-dir signature.
		const uint32 kNativeEndSig = VDFromLE32(ZipCentralDir::kSignature);
		const uint32 kNativeStartSig = VDFromLE32(ZipFileEntry::kSignature);

		for(uint32 i=0; i<buflen-4; ++i) {
			if (VDReadUnalignedU32(bufp + i) == kNativeEndSig) {
				const uint32 diroffset = VDReadUnalignedLEU32(bufp + i + offsetof(ZipCentralDir, diroffset));
				const uint32 dirsize = VDReadUnalignedLEU32(bufp + i + offsetof(ZipCentralDir, dirsize));

				if (diroffset + dirsize == bufOffset + i) {
					uint32 testsig;
					mpStream->Seek(diroffset);
					mpStream->Read(&testsig, 4);

					if (testsig == kNativeStartSig) {
						memcpy(&cdirhdr, bufp + i, sizeof(ZipCentralDir));
						goto found_directory;
					}
				}
			}
		}

		throw MyError("Zip file has missing or bad central directory");
	}

found_directory:
	mDirectory.resize(cdirhdr.dirents_total);

	mpStream->Seek(cdirhdr.diroffset);

	for(int i=0; i<cdirhdr.dirents_total; ++i) {
		FileInfoInternal& fii = mDirectory[i];
		ZipFileEntry ent;

		mpStream->Read(&ent, sizeof ent);
		if (ent.signature != ZipFileEntry::kSignature)
			throw MyError("Zip directory is bad");

		fii.mbSupported = false;
		fii.mbPacked = false;
		fii.mbEnhancedDeflate = false;
		switch(ent.method) {
			case kZipMethodStore:
				fii.mbSupported = true;
				break;
				
			case kZipMethodDeflate:
				fii.mbSupported = true;
				fii.mbPacked = true;
				break;

			case kZipMethodEnhancedDeflate:
				fii.mbSupported = true;
				fii.mbPacked = true;
				fii.mbEnhancedDeflate = true;
				break;

			default:
				break;
		}

		fii.mDataStart			= ent.reloff_localhdr;
		fii.mCompressedSize		= ent.compressed_size;
		fii.mUncompressedSize	= ent.uncompressed_size;
		fii.mCRC32				= ent.crc32;
		fii.mFileName.resize(ent.filename_len);

		mpStream->Read(&*fii.mFileName.begin(), ent.filename_len);
		
		mpStream->Seek(mpStream->Pos() + ent.extrafield_len + ent.comment_len);
	}
}

sint32 VDZipArchive::GetFileCount() {
	return mDirectory.size();
}

const VDZipArchive::FileInfo& VDZipArchive::GetFileInfo(sint32 idx) const {
	VDASSERT((size_t)idx < mDirectory.size());
	return mDirectory[idx];
}

sint32 VDZipArchive::FindFile(const char *name, bool caseSensitive) const {
	sint32 n = (sint32)mDirectory.size();

	for(sint32 i=0; i<n; ++i) {
		const auto& fi = mDirectory[i];

		if (caseSensitive) {
			if (fi.mFileName != name)
				continue;
		} else {
			if (fi.mFileName.comparei(name) != 0)
				continue;
		}

		return i;
	}

	return -1;
}

IVDStream *VDZipArchive::OpenRawStream(sint32 idx) {
	const FileInfoInternal& fi = mDirectory[idx];

	mpStream->Seek(fi.mDataStart);

	ZipFileHeader hdr;
	mpStream->Read(&hdr, sizeof hdr);

	if (hdr.signature != ZipFileHeader::kSignature)
		throw MyError("Bad header for file in zip archive");

	mpStream->Seek(fi.mDataStart + sizeof(hdr) + hdr.filename_len + hdr.extrafield_len);

	return mpStream;
}

IVDInflateStream *VDZipArchive::OpenDecodedStream(sint32 idx, bool allowLarge) {
	const FileInfo& info = GetFileInfo(idx);

	if (!info.mbSupported)
		throw MyError("Unsupported compression method in zip archive for file: %s.", info.mFileName.c_str());

	if ((!allowLarge && info.mUncompressedSize > 384 * 1024 * 1024) || info.mCompressedSize > 0x7FFFFFFF || info.mUncompressedSize > 0x7FFFFFFF)
		throw MyError("Zip file item is too large (%llu bytes).", (unsigned long long)info.mUncompressedSize);

	IVDStream& innerStream = *OpenRawStream(idx);

	vdautoptr<IVDInflateStream> zs;

	if (info.mbEnhancedDeflate)
		zs = new VDZipStream<true>(&innerStream, info.mCompressedSize, false);
	else
		zs = new VDZipStream<false>(&innerStream, info.mCompressedSize, !info.mbPacked);

	zs->EnableCRC();
	zs->SetExpectedCRC(info.mCRC32);
	return zs.release();
}

bool VDZipArchive::ReadRawStream(sint32 idx, vdfastvector<uint8>& buf, bool allowLarge) {
	const FileInfoInternal& fi = mDirectory[idx];

	if ((!allowLarge && fi.mUncompressedSize > 384 * 1024 * 1024) || fi.mCompressedSize > 0x7FFFFFFF || fi.mUncompressedSize > 0x7FFFFFFF)
		throw MyError("Zip file item is too large (%llu bytes).", (unsigned long long)fi.mUncompressedSize);

	buf.resize(fi.mCompressedSize);

	if (fi.mCompressedSize) {
		IVDStream *src = OpenRawStream(idx);
		src->Read(buf.data(), fi.mCompressedSize);
	}

	return !fi.mbPacked;
}

void VDZipArchive::DecompressStream(sint32 idx, vdfastvector<uint8>& buf) const {
	const FileInfo& info = GetFileInfo(idx);

	if (!info.mbPacked)
		return;

	VDMemoryStream ms(buf.data(), buf.size());

	vdautoptr<IVDInflateStream> zs;
	if (info.mbEnhancedDeflate)
		zs = new VDZipStream<true>(&ms, info.mCompressedSize, false);
	else
		zs = new VDZipStream<false>(&ms, info.mCompressedSize, false);

	vdfastvector<uint8> decompBuf(info.mUncompressedSize);
	zs->Read(decompBuf.data(), info.mUncompressedSize);

	buf.swap(decompBuf);
}

///////////////////////////////////////////////////////////////////////////

void VDGUnzipStream::Init(IVDStream *pSrc, uint64 limit) {
	// See RFC1952 for a description of the gzip header format.
	uint8 hdr[10];

	uint32 gzipContainerBytes = 10 + 8;	// header + footer
	pSrc->Read(hdr, 10);
	if (hdr[0] != 0x1f || hdr[1] != 0x8b)
		throw MyError("Source stream is not in gzip format.");

	if (hdr[2] != 0x08)
		throw MyError("Gzip stream uses an unsupported compression method.");

	enum {
		FLG_FTEXT		= 0x01,
		FLG_FHCRC		= 0x02,
		FLG_FEXTRA		= 0x04,
		FLG_FNAME		= 0x08,
		FLG_FCOMMENT	= 0x10
	};

	const uint8 flg = hdr[3];

	if (flg & FLG_FEXTRA) {
		uint8 xlendat[2];
		pSrc->Read(xlendat, 2);

		uint32 xlen = VDReadUnalignedLEU16(xlendat);
		uint8 buf[256];

		gzipContainerBytes += xlen + 2;

		while(xlen) {
			uint32 tc = xlen > 256 ? 256 : xlen;
			pSrc->Read(buf, tc);
			xlen -= tc;
		}
	}

	if (flg & FLG_FNAME) {
		// ugh
		uint8 c;
		for(;;) {
			pSrc->Read(&c, 1);
			++gzipContainerBytes;

			if (!c)
				break;

			mFilename += c;
		} 
	}

	if (flg & FLG_FCOMMENT) {
		// ugh
		uint8 c;
		do {
			pSrc->Read(&c, 1);
			++gzipContainerBytes;
		} while(c);
	}

	if (flg & FLG_FHCRC) {
		uint16 crc16;

		pSrc->Read(&crc16, 2);
		gzipContainerBytes += 2;
	}

	if (gzipContainerBytes > limit)
		throw MyError("The gzip compressed data is invalid.");

	limit -= gzipContainerBytes;

	VDInflateStream::Init(pSrc, limit, false);
}

///////////////////////////////////////////////////////////////////////////

VDDeflateStream::VDDeflateStream(IVDStream& dest)
	: mDestStream(dest)
	, mCRCChecker(VDCRCTable::CRC32)
{
	Reset();
}

VDDeflateStream::~VDDeflateStream() {
	delete mpEncoder;
}

void VDDeflateStream::SetCompressionLevel(VDDeflateCompressionLevel level) {
	mCompressionLevel = level;

	if (mpEncoder)
		mpEncoder->SetCompressionLevel(level);
}

void VDDeflateStream::Reset() {
	mPos = 0;
	
	mCRCChecker.Init();

	delete mpEncoder;
	mpEncoder = nullptr;
	mpEncoder = new VDDeflateEncoder;
	mpEncoder->SetCompressionLevel(mCompressionLevel);
	mpEncoder->Init(false,
		[this](const void *p, uint32 n) { PreProcessInput(p, n); },
		[this](const void *p, uint32 n) { WriteOutput(p, n); }
	);
}

void VDDeflateStream::Finalize() {
	mpEncoder->Finish();
}

const wchar_t *VDDeflateStream::GetNameForError() {
	return mDestStream.GetNameForError();
}

void VDDeflateStream::Read(void *buffer, sint32 bytes) {
	throw MyError("Deflate streams are write-only.");
}

sint32 VDDeflateStream::ReadData(void *buffer, sint32 bytes) {
	throw MyError("Deflate streams are write-only.");
}

void VDDeflateStream::Write(const void *buffer, sint32 bytes) {
	if (bytes <= 0)
		return;

	mPos += bytes;
	mpEncoder->Write(buffer, (uint32)bytes);
}

void VDDeflateStream::PreProcessInput(const void *p, uint32 n) {
	mCRCChecker.Process(p, n);
}

void VDDeflateStream::WriteOutput(const void *p, uint32 n) {
	mDestStream.Write(p, n);
}

///////////////////////////////////////////////////////////////////////////

class VDZipArchiveWriter final : public IVDZipArchiveWriter {
public:
	VDZipArchiveWriter(IVDStream& dest);
	~VDZipArchiveWriter();

	VDDeflateStream& BeginFile(const wchar_t *path, VDDeflateCompressionLevel compressionLevel);
	void EndFile();

	void Finalize();

private:
	struct DirEnt {
		VDStringA mPath;
		sint64 mPos;
		sint64 mCompressedSize;
		sint64 mUncompressedSize;
		uint32 mCRC32;
		uint16 mFlags;
		uint8 mMethod;
	};

	IVDStream& mDestStream;
	sint64 mFileStart;
	sint64 mFileEnd;
	uint16 mFileDate;
	uint16 mFileTime;

	vdvector<DirEnt> mDirectory;

	// large -- put at end
	VDDeflateStream mDeflateStream;
};

VDZipArchiveWriter::VDZipArchiveWriter(IVDStream& dest)
	: mDestStream(dest)
	, mDeflateStream(dest)
{
	// Currently, we use a single timestamp from the beginning of the archive creation for
	// all files within the archive. The local date and time need to be encoded to MS-DOS
	// format for the basic Zip headers.

	const VDExpandedDate localDate = VDGetLocalDate(VDGetCurrentDate());

	mFileDate = (((localDate.mYear - 1980) & 127) << 9)
			+ (localDate.mMonth << 5)
			+ localDate.mDay;

	mFileTime = (localDate.mHour << 11)
		+ (localDate.mMinute << 5)
		+ (localDate.mSecond >> 1);
}

VDZipArchiveWriter::~VDZipArchiveWriter() {
}

VDDeflateStream& VDZipArchiveWriter::BeginFile(const wchar_t *path, VDDeflateCompressionLevel compressionLevel) {
	DirEnt& de = mDirectory.emplace_back();
	de.mPos = mDestStream.Pos();

	// Normalize the zip path by removing leading slashes, repeated slashes, and
	// converting backslashes to forward slashes.
	const VDStringA& rawPath = VDTextWToU8(VDStringSpanW(path));
	char last = '/';
	char extCheck = 0;
	for(char c : rawPath) {
		if (c == '\\')
			c = '/';

		if (c != last || last != '/')
			de.mPath.push_back(c);

		last = c;
		extCheck |= c;
	}

	//de.mMethod = 0;				// stored
	de.mMethod = 8;				// Deflate

	// bit 3: local header size/CRC32 are not filled out, use data descriptor
	de.mFlags = 0x0008;

	// bits 8-9: compression level (Deflate only)
	switch(compressionLevel) {
		case VDDeflateCompressionLevel::Quick:
			// specify Fast
			de.mFlags |= 0x04;
			break;

		case VDDeflateCompressionLevel::Best:
			// specify Maximum
			de.mFlags |= 0x02;
			break;
	}

	// bit 11: language encoding flag (EFS) - use UTF-8
	//
	// To avoid tempting fate, this bit is only set if we have non-ASCII characters in
	// the path. Info-Zip unzip versions through 6.00 unfortunately do not handle this
	// properly, either using OEM CP437 or ISO 8859-1 decoding for the filename, making
	// it a bit of a lost cause. Windows 10 File Explorer and 7-Zip 16 do handle UTF-8
	// filenames.

	if (extCheck & (char)0x80)
		de.mFlags |= 0x800;

	ZipFileHeader zhdr {};
	zhdr.signature = zhdr.kSignature;
	zhdr.version_required = 20;
	zhdr.flags = de.mFlags;
	zhdr.method = de.mMethod;
	zhdr.mod_time = mFileTime;
	zhdr.mod_date = mFileDate;
	zhdr.crc32 = 0;
	zhdr.compressed_size = 0;
	zhdr.uncompressed_size = 0;
	zhdr.filename_len = de.mPath.size();
	zhdr.extrafield_len = 0;

	mDestStream.Write(&zhdr, sizeof zhdr);
	mDestStream.Write(de.mPath.data(), de.mPath.size());

	mFileStart = mDestStream.Pos();

	mDeflateStream.SetCompressionLevel(compressionLevel);
	mDeflateStream.Reset();

	return mDeflateStream;
}

void VDZipArchiveWriter::EndFile() {
	mDeflateStream.Finalize();
	
	mFileEnd = mDestStream.Pos();

	DirEnt& de = mDirectory.back();
	de.mCompressedSize = mFileEnd - mFileStart;
	de.mUncompressedSize = mDeflateStream.Pos();
	de.mCRC32 = mDeflateStream.GetCRC();

	// write data descriptor
	ZipDataDescriptor zdesc {};
	zdesc.crc32 = de.mCRC32;
	zdesc.compressed_size =  de.mCompressedSize > 0xFFFFFFFF ? 0xFFFFFFFF : (uint32)de.mCompressedSize;
	zdesc.uncompressed_size =  de.mUncompressedSize > 0xFFFFFFFF ? 0xFFFFFFFF : (uint32)de.mUncompressedSize;

	mDestStream.Write(&zdesc, sizeof zdesc);
}

void VDZipArchiveWriter::Finalize() {
	ZipFileEntry zfe;

	const sint64 dirStartPos = mDestStream.Pos();

	for(const DirEnt& de : mDirectory) {
		zfe = {};
		zfe.signature = zfe.kSignature;
		zfe.version_create = 20;
		zfe.version_required = 20;	// pkzip 2.0 compatible
		zfe.flags = de.mFlags;
		zfe.method = de.mMethod;
		zfe.mod_time = mFileTime;
		zfe.mod_date = mFileDate;
		zfe.crc32 = de.mCRC32;
		zfe.compressed_size = de.mCompressedSize;
		zfe.uncompressed_size = de.mUncompressedSize;
		zfe.filename_len = de.mPath.size();
		zfe.extrafield_len = 0;
		zfe.comment_len = 0;
		zfe.diskno = 0;
		zfe.internal_attrib = 0;	// binary data
		zfe.external_attrib = 0;
		zfe.reloff_localhdr = de.mPos != (uint32)de.mPos ? 0xFFFFFFFF : (uint32)de.mPos;

		mDestStream.Write(&zfe, sizeof zfe);
		mDestStream.Write(de.mPath.data(), de.mPath.size());
	}
	
	const sint64 dirEndPos = mDestStream.Pos();

	ZipCentralDir zdir {};
	zdir.signature = zdir.kSignature;
	zdir.diskno = 0;
	zdir.diskno_dir = 0;
	zdir.dirents = mDirectory.size();
	zdir.dirents_total = mDirectory.size();
	zdir.dirsize = (uint32)(dirEndPos - dirStartPos);
	zdir.diroffset = (uint32)dirStartPos;
	zdir.comment_len = 0;

	mDestStream.Write(&zdir, sizeof zdir);
}

IVDZipArchiveWriter *VDCreateZipArchiveWriter(IVDStream& stream) {
	return new VDZipArchiveWriter(stream);
}
