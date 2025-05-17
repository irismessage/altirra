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

#include <stdafx.h>
#include <bit>
#include <vd2/system/cpuaccel.h>
#include <at/atcore/savestate.h>
#include <at/atcore/snapshotimpl.h>
#include <at/atcpu/history.h>
#include <vd2/system/binary.h>

#include "tracefileencoding.h"
#include "tracefileformat.h"

#if VD_CPU_ARM64
#include <arm_neon.h>
#endif

//#define AT_PROFILE_TRACE_CODEC_STATISTICS

void ATSavedTraceCodecNull::Encode(ATSaveStateMemoryBuffer& dst, const uint8 *src, uint32 rowSize, size_t rowCount) const {
	dst.GetWriteBuffer().assign(src, src + rowSize * rowCount);
}

void ATSavedTraceCodecNull::Decode(const ATSaveStateMemoryBuffer& src, uint8 *dst, uint32 rowSize, size_t rowCount) const {
	const size_t tableSize = rowSize * rowCount;

	if (src.GetReadBuffer().size() != tableSize)
		throw ATInvalidSaveStateException();

	memcpy(dst, src.GetReadBuffer().data(), tableSize);
}

ATSavedTraceCodecSparse::ATSavedTraceCodecSparse() {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64) || defined(VD_CPU_ARM64)
	for(int i=0; i<256; ++i) {
		uint32 v = i;

		v -= (v & 0xAA) >> 1;
		v = (v & 0x33) + ((v & 0xCC) >> 2);

		mBitCountTab[i] = (uint8)(((v * 0x11) >> 4) & 15);
	}

	for(int i=0; i<256; ++i) {
		uint32 mask = i;

		auto& shuffleMask = mShuffleTab[i];
		uint8 srcIndex = 0;
		for(int j=0; j<8; ++j) {
			if (mask & (1 << j))
				shuffleMask[j] = srcIndex++;
			else
				shuffleMask[j] = 0x80;
		}
	}
#endif
}

void ATSavedTraceCodecSparse::Validate(uint32 rowSize) const {
	if (rowSize > 32)
		throw ATInvalidSaveStateException();
}

void ATSavedTraceCodecSparse::Encode(ATSaveStateMemoryBuffer& dst, const uint8 *src, uint32 rowSize, size_t rowCount) const {
	if (rowSize > 32)
		throw MyError("Cannot encode trace stripe: unsupported row geometry.");

	vdfastvector<uint8> codecData;
	uint8 codecbuf[36] {};
				
#ifdef AT_PROFILE_TRACE_CODEC_STATISTICS
	uint32 stat[32] {};
#endif

	for(uint32 i = 0; i < rowCount; ++i) {
		const uint8 *VDRESTRICT rowSrc = &src[i * rowSize];

		uint32 mask = 0;
		uint8 *dst = codecbuf + 4;

		for(uint32 j=0; j<rowSize; ++j) {
			if (rowSrc[j]) {
				*dst++ = rowSrc[j];
				mask |= (1 << j);
#ifdef AT_PROFILE_TRACE_CODEC_STATISTICS
				++stat[j];
#endif
			}
		}

		if (rowSize <= 8) {
			codecbuf[3] = (uint8)mask;
			codecData.insert(codecData.end(), codecbuf+3, dst);
		} else if (rowSize <= 16) {
			VDWriteUnalignedLEU16(codecbuf+2, (uint16)mask);
			codecData.insert(codecData.end(), codecbuf+2, dst);
		} else if (rowSize <= 24) {
			VDWriteUnalignedLEU32(codecbuf, mask << 8);
			codecData.insert(codecData.end(), codecbuf+1, dst);
		} else {
			VDWriteUnalignedLEU32(codecbuf, mask);
			codecData.insert(codecData.end(), codecbuf, dst);
		}
	}

#ifdef AT_PROFILE_TRACE_CODEC_STATISTICS
	int stat2[32];

	for(int i=0; i<32; ++i)
		stat2[i] = (stat[i] * 200 / 524288 + 1) / 2;

	VDDEBUG2("Change states: %3u %3u %3u %3u | %3u %3u %3u %3u | %3u %3u %3u %3u | %3u %3u %3u %3u | %3u %3u %3u %3u | %3u %3u %3u %3u | %3u %3u %3u %3u\n"
		, stat2[ 0], stat2[ 1], stat2[ 2], stat2[ 3]
		, stat2[ 4], stat2[ 5], stat2[ 6], stat2[ 7]
		, stat2[ 8], stat2[ 9], stat2[10], stat2[11]
		, stat2[12], stat2[13], stat2[14], stat2[15]
		, stat2[16], stat2[17], stat2[18], stat2[19]
		, stat2[20], stat2[21], stat2[22], stat2[23]
		, stat2[24], stat2[25], stat2[26], stat2[27]
	);
#endif

	dst.GetWriteBuffer().assign(codecData.begin(), codecData.end());
}

void ATSavedTraceCodecSparse::Decode(const ATSaveStateMemoryBuffer& buf, uint8 *dst, uint32 rowSize, size_t rowCount) const {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	if (rowSize == 24 && VDCheckAllExtensionsEnabled(CPUF_SUPPORTS_SSE41 | VDCPUF_SUPPORTS_POPCNT))
		return Decode_SSE41_POPCNT_24(buf, dst, rowCount);
	else if (CPUGetEnabledExtensions() & CPUF_SUPPORTS_SSSE3) {
		return Decode_SSSE3(buf, dst, rowSize, rowCount);
	}

	return Decode_Scalar(buf, dst, rowSize, rowCount);
#elif defined(VD_CPU_ARM64)
	return Decode_NEON(buf, dst, rowSize, rowCount);
#else
	return Decode_Scalar(buf, dst, rowSize, rowCount);
#endif
}

void ATSavedTraceCodecSparse::Decode_Scalar(const ATSaveStateMemoryBuffer& buf, uint8 *dst, uint32 rowSize, size_t rowCount) const {
	uint8 tailBuf[256 + 64] {};

	memset(dst, 0, rowSize * rowCount);

	const auto& readBuffer = buf.GetReadBuffer();
	const size_t srcLen = readBuffer.size();
	const uint8 *VDRESTRICT src = readBuffer.data();
	const uint8 *srcEnd = src + srcLen;
	const uint8 *srcSafe = srcEnd - std::min<size_t>(srcLen, 256 + 32);
	for(uint32 row = 0; row < rowCount; ++row) {
		uint8 *VDRESTRICT decodeDst = &dst[rowSize*row];

		if (src >= srcSafe) [[unlikely]] {
			if (src >= srcEnd)
				throw ATInvalidSaveStateException();

			const size_t tailLen = srcEnd - src;
			memcpy(tailBuf, src, tailLen);
			src = tailBuf;
			srcEnd = srcSafe = src + tailLen;
		}

		uint32 mask = 0;

		if (rowSize <= 8)
			mask = *src++;
		else if (rowSize <= 16) {
			mask = VDReadUnalignedLEU16(src);
			src += 2;
		} else if (rowSize <= 24) {
			mask = VDReadUnalignedLEU32(src) & 0xFFFFFF;
			src += 3;
		} else {
			mask = VDReadUnalignedLEU32(src);
			src += 4;
		}

		for(uint32 i=0; i<rowSize; ++i) {
			if (mask & (1 << i))
				decodeDst[i] = *src++;
		}
	}

	if (src != srcEnd)
		throw ATInvalidSaveStateException();
}

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
VD_CPU_TARGET("ssse3")
void ATSavedTraceCodecSparse::Decode_SSSE3(const ATSaveStateMemoryBuffer& buf, uint8 *dst, uint32 rowSize, size_t rowCount) const {
	uint8 tailBuf[256 + 64];

	const uint32 maskByteCount = (rowSize + 7) >> 3;

	const auto& readBuffer = buf.GetReadBuffer();
	const size_t srcLen = readBuffer.size();
	const uint8 *VDRESTRICT src = readBuffer.data();
	const uint8 *srcEnd = src + srcLen;
	const uint8 *srcSafe = srcEnd - std::min<size_t>(srcLen, 256 + 32);
	for(uint32 row = 0; row < rowCount; ++row) {
		uint8 *VDRESTRICT decodeDst = &dst[rowSize*row];

		if (src >= srcSafe) [[unlikely]] {
			if (src >= srcEnd)
				throw ATInvalidSaveStateException();

			const size_t tailLen = srcEnd - src;
			memcpy(tailBuf, src, tailLen);
			src = tailBuf;
			srcEnd = srcSafe = src + tailLen;
		}

		const uint8 *VDRESTRICT maskSrc = src;
		src += maskByteCount;

		uint32 mc = maskByteCount;
		do {
			const uint8 mask = *maskSrc++;

			__m128i vsrc = _mm_loadl_epi64((const __m128i *)src);
			__m128i vdst = _mm_shuffle_epi8(vsrc, _mm_loadl_epi64((const __m128i *)mShuffleTab[mask]));
			_mm_storel_epi64((__m128i *)decodeDst, vdst);
			decodeDst += 8;

			src += mBitCountTab[mask];
		} while(--mc);
	}

	if (src != srcEnd)
		throw ATInvalidSaveStateException();
}

VD_CPU_TARGET("sse4.1,popcnt")
void ATSavedTraceCodecSparse::Decode_SSE41_POPCNT_24(const ATSaveStateMemoryBuffer& buf, uint8 *dst, size_t rowCount) const {
	uint8 tailBuf[64 + 32] {};

	const auto& readBuffer = buf.GetReadBuffer();
	const size_t srcLen = readBuffer.size();
	const uint8 *VDRESTRICT src = readBuffer.data();
	const uint8 *srcEnd = src + srcLen;
	const uint8 *srcSafe = srcEnd - std::min<size_t>(srcLen, 64);
	uint8 *VDRESTRICT decodeDst = dst;

	for(uint32 row = 0; row < rowCount; ++row) {

		if (src >= srcSafe) [[unlikely]] {
			if (src >= srcEnd)
				throw ATInvalidSaveStateException();

			const size_t tailLen = srcEnd - src;
			memcpy(tailBuf, src, tailLen);
			src = tailBuf;
			srcEnd = srcSafe = src + tailLen;
		}

		const uint8 mask0 = *src++;
		const uint8 mask1 = *src++;
		const uint8 mask2 = *src++;

		__m128i vsrc0 = _mm_loadl_epi64((const __m128i *)src);
		src += (unsigned)_mm_popcnt_u32(mask0);
		__m128i vsrc1 = _mm_loadl_epi64((const __m128i *)src);
		src += (unsigned)_mm_popcnt_u32(mask1);
		__m128i vsrc2 = _mm_loadl_epi64((const __m128i *)src);
		src += (unsigned)_mm_popcnt_u32(mask2);

		_mm_storel_epi64((__m128i *)decodeDst, _mm_shuffle_epi8(vsrc0, _mm_loadl_epi64((const __m128i *)mShuffleTab[mask0])));
		decodeDst += 8;
		_mm_storel_epi64((__m128i *)decodeDst, _mm_shuffle_epi8(vsrc1, _mm_loadl_epi64((const __m128i *)mShuffleTab[mask1])));
		decodeDst += 8;
		_mm_storel_epi64((__m128i *)decodeDst, _mm_shuffle_epi8(vsrc2, _mm_loadl_epi64((const __m128i *)mShuffleTab[mask2])));
		decodeDst += 8;
	}

	if (src != srcEnd)
		throw ATInvalidSaveStateException();
}
#endif

#if defined(VD_CPU_ARM64)
void ATSavedTraceCodecSparse::Decode_NEON(const ATSaveStateMemoryBuffer& buf, uint8 *dst, uint32 rowSize, size_t rowCount) const {
	uint8 srcTail[64] {};

	const uint32 maskByteCount = (rowSize + 7) >> 3;

	const auto& readBuffer = buf.GetReadBuffer();
	const uint8 *src = readBuffer.data();
	const uint8 *srcEnd = src + readBuffer.size();
	for(uint32 row = 0; row < rowCount; ++row) {
		uint8 *VDRESTRICT decodeDst = &dst[rowSize*row];
		const uint8 *VDRESTRICT decodeSrc = src;

		if (srcEnd - src < 64) {
			memcpy(srcTail, src, srcEnd - src);
			decodeSrc = srcTail;
		}

		const uint8 *VDRESTRICT decodeSrc2 = decodeSrc;
		const uint8 *VDRESTRICT maskSrc = decodeSrc2;

		decodeSrc2 += maskByteCount;

		uint32 mc = maskByteCount;
		do {
			const uint8 mask = *maskSrc++;

			uint8x8_t vsrc = vld1_u8(decodeSrc2);
			uint8x8_t vdst = vtbl1_u8(vsrc, vld1_u8(mShuffleTab[mask]));
			vst1_u8(decodeDst, vdst);
			decodeDst += 8;

			decodeSrc2 += mBitCountTab[mask];
		} while(--mc);

		const size_t srcUsed = decodeSrc2 - decodeSrc;
		if (srcEnd - src < (ptrdiff_t)srcUsed)
			throw ATInvalidSaveStateException();

		src += srcUsed;
	}

	if (src != srcEnd)
		throw ATInvalidSaveStateException();
}
#endif

////////////////////////////////////////////////////////////////////////////////

void ATTraceFmtAccessMask::MarkRead(uint32 first, uint32 n) {
	Validate(first, n);
	mReadMask.MarkCount(first, n);
}

void ATTraceFmtAccessMask::MarkWrite(uint32 first, uint32 n) {
	Validate(first, n);
	mWriteMask.MarkCount(first, n);
}

void ATTraceFmtAccessMask::MarkReadWrite(uint32 first, uint32 n) {
	Validate(first, n);
	mReadMask.MarkCount(first, n);
	mWriteMask.MarkCount(first, n);
}

void ATTraceFmtAccessMask::Merge(const ATTraceFmtAccessMask& other) {
	VDASSERT(mRowSize == other.mRowSize);

	mReadMask.Merge(other.mReadMask);
	mWriteMask.Merge(other.mWriteMask);
}

void ATTraceFmtAccessMask::Validate(uint32 first, uint32 n) {
	if (first >= mRowSize && mRowSize - first < n)
		throw ATInvalidSaveStateException();
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorXOR::ATSavedTracePredictorXOR(uint32 offset, uint32 size)
	: mXorOffset(offset)
	, mXorSize(size)
{
}

void ATSavedTracePredictorXOR::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mXorOffset, mXorSize);
}

void ATSavedTracePredictorXOR::Reset() {
	memset(mPredBuf, 0, sizeof mPredBuf);
}

void ATSavedTracePredictorXOR::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	dst += mXorOffset;

	const uint32 xorSize = mXorSize;
	uint8 *VDRESTRICT dst2 = dst;
	while(rowCount--) {
		for(uint32 i=0; i<xorSize; ++i) {
			uint8 c = dst2[i];
			dst2[i] = c ^ mPredBuf[i];
			mPredBuf[i] = c;
		}

		dst2 += rowSize;
	}
}

void ATSavedTracePredictorXOR::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	dst += mXorOffset;

	const uint32 xorSize = mXorSize;

	const auto optXor = [](auto nval, uint8 *VDRESTRICT dst2, uint32 rowSize, size_t rowCount, uint8 *predBuf) {
		constexpr auto n = nval;
		uint8 localBuf[n];

		memcpy(localBuf, predBuf, n);

		while(rowCount--) {
			for(uint32 i=0; i<n; ++i) {
				uint8 v = localBuf[i] ^ dst2[i];
				localBuf[i] = v;
				dst2[i] = v;
			}

			dst2 += rowSize;
		}

		memcpy(predBuf, localBuf, n);
	};
	
	switch(xorSize) {
		case 2:
			optXor(std::integral_constant<uint32, 2>(), dst, rowSize, rowCount, mPredBuf);
			break;

		default:
			{
				uint8 *VDRESTRICT dst2 = dst;
				while(rowCount--) {
					for(uint32 i=0; i<xorSize; ++i) {
						uint8 v = mPredBuf[i] ^ dst2[i];
						mPredBuf[i] = v;
						dst2[i] = v;
					}

					dst2 += rowSize;
				}
			}
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorEA::ATSavedTracePredictorEA(uint32 offset)
	: mOffset(offset)
{
}

void ATSavedTracePredictorEA::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mOffset, 4);
}

void ATSavedTracePredictorEA::Reset() {
	mPrev = 0;
}

void ATSavedTracePredictorEA::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	dst += mOffset;

	uint8 *VDRESTRICT dst2 = dst;
	uint32 prev = mPrev;
	while(rowCount--) {
		uint32 v = VDReadUnalignedLEU32(dst2);
		uint32 delta = v ^ prev;

		// -1 is common as it means no effective address, so we modify the encoding
		// to make the lower three bytes don't care in that case.
		if (v >= 0xFF000000)
			delta &= 0xFF000000;

		prev ^= delta;

		VDWriteUnalignedLEU32(dst2, delta);

		dst2 += rowSize;
	}

	mPrev = prev;
}

void ATSavedTracePredictorEA::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	dst += mOffset;

	uint8 *VDRESTRICT dst2 = dst;
	uint32 prev = mPrev;

	while(rowCount--) {
		uint32 delta = VDReadUnalignedLEU32(dst2);
		prev ^= delta;

		uint32 v = prev;
		if (v >= 0xFF000000)
			v = 0xFFFFFFFF;

		VDWriteUnalignedLEU32(dst2, v);
		dst2 += rowSize;
	}

	mPrev = prev;
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorPC::ATSavedTracePredictorPC(uint32 offset)
	: mOffset(offset)
{
}

void ATSavedTracePredictorPC::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mOffset, 2);
}

void ATSavedTracePredictorPC::Reset() {
	memset(mPredBuf, 0, sizeof mPredBuf);
	mPrevPC = 0;
}

void ATSavedTracePredictorPC::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	dst += mOffset;

	uint8 *VDRESTRICT dst2 = dst;
	uint16 prevPC = mPrevPC;
	while(rowCount--) {
		uint16 pc = VDReadUnalignedLEU16(dst2);
		uint16 pcDelta = pc - (prevPC + 1);
		uint16& pred = mPredBuf[prevPC];
		
		uint16 encDelta = pcDelta - pred;
		pred = pcDelta;

		if (encDelta & 0x8000)
			encDelta ^= 0x7FFF;

		encDelta = std::rotl<uint16_t>(encDelta, 1);

		VDWriteUnalignedLEU16(dst2, encDelta);

		prevPC = pc;
		dst2 += rowSize;
	}

	mPrevPC = prevPC;
}

void ATSavedTracePredictorPC::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mOffset;
	uint16 pc = mPrevPC;
	while(rowCount--) {
		uint16 encDelta = VDReadUnalignedLEU16(dst2);

		encDelta = std::rotr<uint16_t>(encDelta, 1);

		if (encDelta & 0x8000)
			encDelta ^= 0x7FFF;

		uint16& pred = mPredBuf[pc];
		uint16 pcDelta = pred + encDelta;
		pred = pcDelta;

		pc += pcDelta + 1;

		VDWriteUnalignedLEU16(dst2, pc);
		dst2 += rowSize;
	}

	mPrevPC = pc;
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorInsn::ATSavedTracePredictorInsn(uint32 insnOffset, uint32 insnSize, uint32 pcOffset, uint32 flagsBitOffset)
	: mPCOffset(pcOffset)
	, mInsnOffset(insnOffset)
	, mInsnSize(insnSize)
	, mFlagsBitOffset(flagsBitOffset)
{
}

void ATSavedTracePredictorInsn::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkRead(mPCOffset, 2);
	accessMask.MarkReadWrite(mInsnOffset, mInsnSize);

	// flags already validated to be within a byte in the serializer
	accessMask.MarkReadWrite(mFlagsBitOffset >> 3, 1);

	// check for overlaps
	const uint32 flagsByteOffset = mFlagsBitOffset >> 3;

	if ((uint32)(flagsByteOffset - mPCOffset) < 2)
		throw ATInvalidSaveStateException();

	if ((uint32)(flagsByteOffset - mInsnOffset) < mInsnSize)
		throw ATInvalidSaveStateException();

	if (mPCOffset < mInsnOffset + mInsnSize && mInsnOffset < mPCOffset + 2)
		throw ATInvalidSaveStateException();
}

void ATSavedTracePredictorInsn::Reset() {
	mFlagsOffset = mFlagsBitOffset >> 3;
	mFlagsShift = mFlagsBitOffset & 7;

	memset(mPredBuf, 0, sizeof mPredBuf);
	mPrevFlags = 0;
}

void ATSavedTracePredictorInsn::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	const uint8 *VDRESTRICT dstPC = dst + mPCOffset;
	uint8 *VDRESTRICT dstInsn = dst + mInsnOffset;
	uint8 *VDRESTRICT dstFlags = dst + mFlagsOffset;

	while(rowCount--) {
		const uint16 pc = VDReadUnalignedLEU16(dstPC);

		// predict opcode based on PC
		uint8 flags = 0;

		for(uint32 i=0; i<mInsnSize; ++i) {
			uint8& pred = mPredBuf[(pc + i) & 0xFFFF];
			uint8 c = dstInsn[i];
			uint8 delta = c ^ pred;

			if (delta) {
				if (!c)
					flags |= 1 << i;
				else {
					pred = c;
					dstInsn[i] = delta;
				}
			} else {
				if (!c && (mPrevFlags & (1 << i)))
					flags |= 1 << i;

				dstInsn[i] = 0;
			}
		}
		
		mPrevFlags = flags;
		dstFlags[0] |= flags << mFlagsShift;

		dstPC += rowSize;
		dstInsn += rowSize;
		dstFlags += rowSize;
	}
}

void ATSavedTracePredictorInsn::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	if (mInsnSize == 4)
		return Decode4(dst, rowSize, rowCount);
	else
		return DecodeN(dst, rowSize, rowCount);
}

void ATSavedTracePredictorInsn::Decode4(uint8 *dst, uint32 rowSize, size_t rowCount) {
	const uint8 *VDRESTRICT dstPC = dst + mPCOffset;
	uint8 *VDRESTRICT dstInsn = dst + mInsnOffset;
	const uint8 *VDRESTRICT dstFlags = dst + mFlagsOffset;

	while(rowCount--) {
		const uint16 pc = VDReadUnalignedLEU16(dstPC);
		const uint8 flags = dstFlags[0] >> mFlagsShift;

		if (flags != 15) {
			if (pc <= 0xFFFC) {
				for(uint32 i=0; i<4; ++i) {
					uint8 c = dstInsn[i];
					uint8& VDRESTRICT pred = mPredBuf[pc + i];

					if (!(flags & (1 << i))) {
						c ^= pred;
						pred = c;
						dstInsn[i] = c;
					}
				}
			} else [[unlikely]] {
				for(uint32 i=0; i<4; ++i) {
					uint8 c = dstInsn[i];
					uint8& VDRESTRICT pred = mPredBuf[(pc + i) & 0xFFFF];

					if (!(flags & (1 << i))) {
						c ^= pred;
						pred = c;
						dstInsn[i] = c;
					}
				}
			}
		}

		dstPC += rowSize;
		dstInsn += rowSize;
		dstFlags += rowSize;
	}
}

void ATSavedTracePredictorInsn::DecodeN(uint8 *dst, uint32 rowSize, size_t rowCount) {
	const uint8 *VDRESTRICT dstPC = dst + mPCOffset;
	uint8 *VDRESTRICT dstInsn = dst + mInsnOffset;
	const uint8 *VDRESTRICT dstFlags = dst + mFlagsOffset;

	while(rowCount--) {
		const uint16 pc = VDReadUnalignedLEU16(dstPC);
		const uint8 flags = dstFlags[0] >> mFlagsShift;

		for(uint32 i=0; i<mInsnSize; ++i) {
			uint8 c = dstInsn[i];
			uint8& VDRESTRICT pred = mPredBuf[(pc + i) & 0xFFFF];

			if (!(flags & (1 << i))) {
				c ^= pred;
				pred = c;
				dstInsn[i] = c;
			}
		}

		dstPC += rowSize;
		dstInsn += rowSize;
		dstFlags += rowSize;
	}
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorHorizDelta16::ATSavedTracePredictorHorizDelta16(uint32 dstOffset, uint32 predOffset)
	: mDstOffset(dstOffset)
	, mPredOffset(predOffset)
{
}

void ATSavedTracePredictorHorizDelta16::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mDstOffset, 2);
	accessMask.MarkRead(mPredOffset, 2);

	if (mDstOffset < mPredOffset + 2 && mPredOffset < mDstOffset + 2)
		throw ATInvalidSaveStateException();
}

void ATSavedTracePredictorHorizDelta16::Reset() {
}

void ATSavedTracePredictorHorizDelta16::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mDstOffset;
	const uint8 *VDRESTRICT pred = dst + mPredOffset;

	while(rowCount--) {
		VDWriteUnalignedLEU16(dst2, VDReadUnalignedLEU16(dst2) - VDReadUnalignedLEU16(pred));

		dst2 += rowSize;
		pred += rowSize;
	}
}

void ATSavedTracePredictorHorizDelta16::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mDstOffset;
	const uint8 *VDRESTRICT pred = dst + mPredOffset;

	while(rowCount--) {
		VDWriteUnalignedLEU16(dst2, VDReadUnalignedLEU16(dst2) + VDReadUnalignedLEU16(pred));

		dst2 += rowSize;
		pred += rowSize;
	}
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorHorizDelta32::ATSavedTracePredictorHorizDelta32(uint32 dstOffset, uint32 predOffset)
	: mDstOffset(dstOffset)
	, mPredOffset(predOffset)
{
}

void ATSavedTracePredictorHorizDelta32::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mDstOffset, 4);
	accessMask.MarkRead(mPredOffset, 4);

	if (mDstOffset < mPredOffset + 4 && mPredOffset < mDstOffset + 4)
		throw ATInvalidSaveStateException();
}

void ATSavedTracePredictorHorizDelta32::Reset() {
}

void ATSavedTracePredictorHorizDelta32::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mDstOffset;
	const uint8 *VDRESTRICT pred = dst + mPredOffset;

	while(rowCount--) {
		VDWriteUnalignedLEU32(dst2, VDReadUnalignedLEU32(dst2) - VDReadUnalignedLEU32(pred));

		dst2 += rowSize;
		pred += rowSize;
	}
}

void ATSavedTracePredictorHorizDelta32::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mDstOffset;
	const uint8 *VDRESTRICT pred = dst + mPredOffset;

	while(rowCount--) {
		VDWriteUnalignedLEU32(dst2, VDReadUnalignedLEU32(dst2) + VDReadUnalignedLEU32(pred));

		dst2 += rowSize;
		pred += rowSize;
	}
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorVertDelta16::ATSavedTracePredictorVertDelta16(uint32 offset, uint32 bias)
	: mOffset(offset)
	, mBias((sint32)bias)
{
}

void ATSavedTracePredictorVertDelta16::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mOffset, 2);
}

void ATSavedTracePredictorVertDelta16::Reset() {
	mPrev = 0;
}

void ATSavedTracePredictorVertDelta16::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mOffset;

	uint32 prev = mPrev;
	while(rowCount--) {
		uint32 v = VDReadUnalignedLEU16(dst2);
		uint32 delta = v - (prev + (uint16)mBias);
		prev = v;

		VDWriteUnalignedLEU16(dst2, delta);

		dst2 += rowSize;
	}

	mPrev = prev;
}

void ATSavedTracePredictorVertDelta16::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mOffset;

	uint32 v = mPrev;
	while(rowCount--) {
		uint32 delta = VDReadUnalignedLEU16(dst2);

		v += delta + (uint16)mBias;
		VDWriteUnalignedLEU16(dst2, v);

		dst2 += rowSize;
	}

	mPrev = v;
}

////////////////////////////////////////////////////////////////////////////////

void *ATSavedTraceMergedPredictor::AsInterface(uint32 iid) {
	if (iid == IATTraceFmtPredictor::kTypeID)
		return static_cast<IATTraceFmtPredictor *>(this);

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorHVDelta16x2::ATSavedTracePredictorHVDelta16x2(uint32 offset, uint32 bias)
	: mOffset(offset)
	, mBias((sint32)bias)
{
}

void ATSavedTracePredictorHVDelta16x2::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mOffset, 4);
}

void ATSavedTracePredictorHVDelta16x2::Reset() {
	mPrev1 = 0;
	mPrev2 = 0;
}

void ATSavedTracePredictorHVDelta16x2::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mOffset;
	
	uint16 prev1 = mPrev1;
	uint16 prev2 = mPrev2;

	const uint16 bias = (uint16)mBias;
	while(rowCount--) {
		uint16 v1 = VDReadUnalignedLEU16(dst2 + 0);
		uint16 v2 = VDReadUnalignedLEU16(dst2 + 2);

		// vertical prediction
		uint16 delta1 = v1 - (prev1 + bias);
		uint16 delta2 = v2 - (prev2 + bias);
		prev1 = v1;
		prev2 = v2;

		// horizontal prediction
		VDWriteUnalignedLEU16(dst2 + 0, delta1 - delta2);
		VDWriteUnalignedLEU16(dst2 + 2, delta2);

		prev1 = v1;
		prev2 = v2;

		dst2 += rowSize;
	}

	mPrev1 = prev1;
	mPrev2 = prev2;
}

void ATSavedTracePredictorHVDelta16x2::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mOffset;

	uint16 prev1 = mPrev1;
	uint16 prev2 = mPrev2;

	const uint16 bias = (uint16)mBias;
	while(rowCount--) {
		uint16 v1 = VDReadUnalignedLEU16(dst2 + 0);
		uint16 v2 = VDReadUnalignedLEU16(dst2 + 2);

		// horizontal prediction
		v1 += v2;

		// vertical prediction
		prev1 += v1 + bias;
		prev2 += v2 + bias;

		VDWriteUnalignedLEU16(dst2 + 0, prev1);
		VDWriteUnalignedLEU16(dst2 + 2, prev2);

		dst2 += rowSize;
	}

	mPrev1 = prev1;
	mPrev2 = prev2;
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorVertDelta32::ATSavedTracePredictorVertDelta32(uint32 offset, uint32 bias)
	: mOffset(offset)
	, mBias((sint32)bias)
{
}

void ATSavedTracePredictorVertDelta32::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mOffset, 4);
}

void ATSavedTracePredictorVertDelta32::Reset() {
	mPrev = 0;
}

void ATSavedTracePredictorVertDelta32::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mOffset;

	while(rowCount--) {
		uint32 v = VDReadUnalignedLEU32(dst2);
		uint32 delta = v - (mPrev + (uint32)mBias);
		mPrev = v;

		VDWriteUnalignedLEU32(dst2, delta);

		dst2 += rowSize;
	}
}

void ATSavedTracePredictorVertDelta32::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mOffset;

	uint32 v = mPrev;
	while(rowCount--) {
		uint32 delta = VDReadUnalignedLEU32(dst2);

		v += delta + (uint32)mBias;
		VDWriteUnalignedLEU32(dst2, v);

		dst2 += rowSize;
	}

	mPrev = v;
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorVertDelta8::ATSavedTracePredictorVertDelta8(uint32 offset, uint32 count)
	: mOffset(offset)
	, mCount(count)
{
}

void ATSavedTracePredictorVertDelta8::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mOffset, mCount);
}

void ATSavedTracePredictorVertDelta8::Reset() {
	memset(mPredBuf, 0, sizeof mPredBuf);
}

void ATSavedTracePredictorVertDelta8::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mOffset;

	while(rowCount--) {
		for(uint32 i=0; i<mCount; ++i) {
			uint8 v = dst2[i];
			dst2[i] = v - mPredBuf[i];
			mPredBuf[i] = v;
		}

		dst2 += rowSize;
	}
}

void ATSavedTracePredictorVertDelta8::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mOffset;

	const auto optPred = [this](auto nval, uint8 *VDRESTRICT dst3, size_t rowCount, size_t rowSize) {
		constexpr auto n = nval;
		uint8 pred[n];

		memcpy(pred, mPredBuf, n);

		while(rowCount--) {
			for(uint32 i=0; i<n; ++i) {
				uint8 c = pred[i] + dst3[i];
				pred[i] = c;
				dst3[i] = c;
			}

			dst3 += rowSize;
		}

		memcpy(mPredBuf, pred, n);
	};

	switch(mCount) {
		case 4:
			optPred(std::integral_constant<uint32, 4>(), dst2, rowCount, rowSize);
			break;

		default:
			while(rowCount--) {
				for(uint32 i=0; i<mCount; ++i) {
					uint8 c = mPredBuf[i] + dst2[i];
					mPredBuf[i] = c;
					dst2[i] = c;
				}

				dst2 += rowSize;
			}
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorDelta32TablePrev8::ATSavedTracePredictorDelta32TablePrev8(uint32 valueOffset, uint32 opcodeOffset)
	: mValueOffset(valueOffset)
	, mOpcodeOffset(opcodeOffset)
{
}

void ATSavedTracePredictorDelta32TablePrev8::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mValueOffset, 4);
	accessMask.MarkRead(mOpcodeOffset, 1);

	if ((uint32)(mOpcodeOffset - mValueOffset) < 4)
		throw ATInvalidSaveStateException();
}

void ATSavedTracePredictorDelta32TablePrev8::Reset() {
	memset(mPredBuf, 0, sizeof mPredBuf);
	mPrevOp = 0;
}

void ATSavedTracePredictorDelta32TablePrev8::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mValueOffset;
	const uint8 *VDRESTRICT opcode = dst + mOpcodeOffset;

	uint8 prevOp = mPrevOp;
	while(rowCount--) {
		uint32 v = VDReadUnalignedLEU32(dst2);

		VDWriteUnalignedLEU32(dst2, v - mPredBuf[prevOp]);
		mPredBuf[prevOp] = v;

		prevOp = *opcode;

		dst2 += rowSize;
		opcode += rowSize;
	}
	mPrevOp = prevOp;
}

void ATSavedTracePredictorDelta32TablePrev8::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mValueOffset;
	const uint8 *VDRESTRICT opcode = dst + mOpcodeOffset;

	uint8 prevOp = mPrevOp;
	while(rowCount--) {
		uint32 delta = VDReadUnalignedLEU32(dst2);
		uint32 v = mPredBuf[prevOp] + delta;

		mPredBuf[prevOp] = v;
		VDWriteUnalignedLEU32(dst2, v);

		prevOp = *opcode;

		dst2 += rowSize;
		opcode += rowSize;
	}
	mPrevOp = prevOp;
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorDelta16TablePrev8::ATSavedTracePredictorDelta16TablePrev8(uint32 valueOffset, uint32 opcodeOffset)
	: mValueOffset(valueOffset)
	, mOpcodeOffset(opcodeOffset)
{
}

void ATSavedTracePredictorDelta16TablePrev8::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mValueOffset, 2);
	accessMask.MarkRead(mOpcodeOffset, 1);

	if ((uint32)(mOpcodeOffset - mValueOffset) < 2)
		throw ATInvalidSaveStateException();
}

void ATSavedTracePredictorDelta16TablePrev8::Reset() {
	memset(mPredBuf, 0, sizeof mPredBuf);
	mPrevOp = 0;
}

void ATSavedTracePredictorDelta16TablePrev8::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mValueOffset;
	const uint8 *VDRESTRICT opcode = dst + mOpcodeOffset;

	uint8 prevOp = mPrevOp;
	while(rowCount--) {
		uint16 v = VDReadUnalignedLEU16(dst2);

		VDWriteUnalignedLEU16(dst2, v - mPredBuf[prevOp]);
		mPredBuf[prevOp] = v;

		prevOp = *opcode;

		dst2 += rowSize;
		opcode += rowSize;
	}
	mPrevOp = prevOp;
}

void ATSavedTracePredictorDelta16TablePrev8::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mValueOffset;
	const uint8 *VDRESTRICT opcode = dst + mOpcodeOffset;

	uint8 prevOp = mPrevOp;
	while(rowCount--) {
		uint16 delta = VDReadUnalignedLEU16(dst2);
		uint32 v = mPredBuf[prevOp] + delta;

		mPredBuf[prevOp] = v;
		VDWriteUnalignedLEU16(dst2, v);

		prevOp = *opcode;

		dst2 += rowSize;
		opcode += rowSize;
	}
	mPrevOp = prevOp;
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorXor32Table8::ATSavedTracePredictorXor32Table8(uint32 valueOffset, uint32 predOffset)
	: mValueOffset(valueOffset)
	, mPredOffset(predOffset)
{
}

void ATSavedTracePredictorXor32Table8::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mValueOffset, 4);
	accessMask.MarkRead(mPredOffset, 1);

	if ((uint32)(mPredOffset - mValueOffset) < 4)
		throw ATInvalidSaveStateException();
}

void ATSavedTracePredictorXor32Table8::Reset() {
	memset(mPredBuf, 0, sizeof mPredBuf);
}

void ATSavedTracePredictorXor32Table8::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mValueOffset;
	const uint8 *VDRESTRICT pred = dst + mPredOffset;

	while(rowCount--) {
		uint32 v = VDReadUnalignedLEU32(dst2);
		uint8 p = *pred;

		VDWriteUnalignedLEU32(dst2, v ^ mPredBuf[p]);
		mPredBuf[p] = v;

		dst2 += rowSize;
		pred += rowSize;
	}
}

void ATSavedTracePredictorXor32Table8::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mValueOffset;
	const uint8 *VDRESTRICT pred = dst + mPredOffset;

	while(rowCount--) {
		uint32 delta = VDReadUnalignedLEU32(dst2);
		uint8 p = *pred;

		uint32 v = delta ^ mPredBuf[p];
		mPredBuf[p] = v;
		VDWriteUnalignedLEU32(dst2, v);

		dst2 += rowSize;
		pred += rowSize;
	}
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorXor32TablePrev16::ATSavedTracePredictorXor32TablePrev16(uint32 valueOffset, uint32 pcOffset)
	: mValueOffset(valueOffset)
	, mPCOffset(pcOffset)
{
}

void ATSavedTracePredictorXor32TablePrev16::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mValueOffset, 4);
	accessMask.MarkRead(mPCOffset, 2);

	if (mValueOffset < mPCOffset + 2 && mPCOffset < mValueOffset + 4)
		throw ATInvalidSaveStateException();
}

void ATSavedTracePredictorXor32TablePrev16::Reset() {
	memset(mPredBuf, 0, sizeof mPredBuf);
	mPrevPC = 0;
}

void ATSavedTracePredictorXor32TablePrev16::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT valPtr = dst + mValueOffset;
	const uint8 *VDRESTRICT pcPtr = dst + mPCOffset;
	
	uint16 prevPC = mPrevPC;
	while(rowCount--) {
		uint32 v = VDReadUnalignedU32(valPtr);

		VDWriteUnalignedU32(valPtr, v ^ mPredBuf[prevPC]);
		mPredBuf[prevPC] = v;

		prevPC = VDReadUnalignedLEU16(pcPtr);

		valPtr += rowSize;
		pcPtr += rowSize;
	}
	mPrevPC = prevPC;
}

void ATSavedTracePredictorXor32TablePrev16::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT valPtr = dst + mValueOffset;
	const uint8 *VDRESTRICT pcPtr = dst + mPCOffset;

	uint16 prevPC = mPrevPC;
	while(rowCount--) {
		const uint32 delta = VDReadUnalignedU32(valPtr);
		uint32 v = mPredBuf[prevPC] ^ delta;

		mPredBuf[prevPC] = v;
		VDWriteUnalignedU32(valPtr, v);
		
		prevPC = VDReadUnalignedLEU16(pcPtr);

		valPtr += rowSize;
		pcPtr += rowSize;
	}
	mPrevPC = prevPC;
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorXor32VertDeltaTablePrev16::ATSavedTracePredictorXor32VertDeltaTablePrev16(uint32 valueOffset, uint32 pcOffset)
	: mValueOffset(valueOffset)
	, mPCOffset(pcOffset)
{
}

void ATSavedTracePredictorXor32VertDeltaTablePrev16::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mValueOffset, 4);
	accessMask.MarkRead(mPCOffset, 2);

	if (mPCOffset < mValueOffset + 4 && mValueOffset < mPCOffset + 2)
		throw ATInvalidSaveStateException();
}

void ATSavedTracePredictorXor32VertDeltaTablePrev16::Reset() {
	memset(mPredBuf, 0, sizeof mPredBuf);
	mPrevPC = 0;
	mVPred = 0;
}

void ATSavedTracePredictorXor32VertDeltaTablePrev16::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mValueOffset;
	const uint8 *VDRESTRICT pcPtr = dst + mPCOffset;

	uint32 vpred = mVPred;
	uint16 prevPC = mPrevPC;

	while(rowCount--) {
		uint32 v = VDReadUnalignedU32(dst2);

		uint32 delta = ((v | 0x80808080) - (vpred & 0x7F7F7F7F)) ^ (~(vpred ^ v) & 0x80808080);
		vpred = v;

		VDWriteUnalignedU32(dst2, delta ^ mPredBuf[prevPC]);
		mPredBuf[prevPC] = delta;

		prevPC = VDReadUnalignedLEU16(pcPtr);

		dst2 += rowSize;
		pcPtr += rowSize;
	}

	mPrevPC = prevPC;
	mVPred = vpred;
}

void ATSavedTracePredictorXor32VertDeltaTablePrev16::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mValueOffset;
	const uint8 *VDRESTRICT pcPtr = dst + mPCOffset;

	uint32 vpred = mVPred;
	uint16 prevPC = mPrevPC;

	while(rowCount--) {
		uint32 delta = VDReadUnalignedLEU32(dst2);

		uint32 v = delta ^ mPredBuf[prevPC];
		mPredBuf[prevPC] = v;
		prevPC = VDReadUnalignedLEU16(pcPtr);

		vpred = ((vpred & 0x7F7F7F7F) + (v & 0x7F7F7F7F)) ^ ((vpred ^ v) & 0x80808080);

		VDWriteUnalignedLEU32(dst2, vpred);

		dst2 += rowSize;
		pcPtr += rowSize;
	}

	mPrevPC = prevPC;
	mVPred = vpred;
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorSignMag16::ATSavedTracePredictorSignMag16(uint32 offset)
	: mOffset(offset)
{
}

bool ATSavedTracePredictorSignMag16::TryMerge(const ATSavedTracePredictorSignMag16& other) {
	if (mCount + other.mCount > 2)
		return false;

	if (mOffset == other.mOffset + other.mCount * 2) {
		mOffset = other.mOffset;
		mCount += other.mCount;
		return true;
	}

	if (mOffset + mCount * 2 == other.mOffset) {
		mCount += other.mCount;
		return true;
	}

	return false;
}

void ATSavedTracePredictorSignMag16::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mOffset, mCount * 2);
}

void ATSavedTracePredictorSignMag16::Reset() {
}

void ATSavedTracePredictorSignMag16::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	switch(mCount) {
		case 1: EncodeT<1>(dst, rowSize, rowCount); break;
		case 2: EncodeT<2>(dst, rowSize, rowCount); break;
	}
}

void ATSavedTracePredictorSignMag16::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	switch(mCount) {
		case 1: DecodeT<1>(dst, rowSize, rowCount); break;
		case 2: DecodeT<2>(dst, rowSize, rowCount); break;
	}
}

template<size_t N>
void ATSavedTracePredictorSignMag16::EncodeT(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mOffset;

	while(rowCount--) {
		for(size_t i = 0; i < N; ++i) {
			uint16 delta = VDReadUnalignedLEU16(dst2 + 2*i);

			if (delta & 0x8000)
				delta ^= 0x7FFF;

			delta = std::rotl<uint16_t>(delta, 1);

			VDWriteUnalignedLEU16(dst2 + 2*i, delta);
		}

		dst2 += rowSize;
	}
}

template<size_t N>
void ATSavedTracePredictorSignMag16::DecodeT(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mOffset;

	while(rowCount--) {
		for(size_t i = 0; i < N; ++i) {
			uint16 delta = VDReadUnalignedLEU16(dst2 + 2*i);

			delta = std::rotr<uint16_t>(delta, 1);

			if (delta & 0x8000)
				delta ^= 0x7FFF;

			VDWriteUnalignedLEU16(dst2 + 2*i, delta);
		}

		dst2 += rowSize;
	}
}

////////////////////////////////////////////////////////////////////////////////

ATSavedTracePredictorSignMag32::ATSavedTracePredictorSignMag32(uint32 offset)
	: mOffset(offset)
{
}

void ATSavedTracePredictorSignMag32::Validate(ATTraceFmtAccessMask& accessMask) const {
	accessMask.MarkReadWrite(mOffset, 4);
}

void ATSavedTracePredictorSignMag32::Reset() {
}

void ATSavedTracePredictorSignMag32::Encode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mOffset;

	while(rowCount--) {
		uint32 delta = VDReadUnalignedLEU32(dst2);

		if (delta & 0x80000000)
			delta ^= 0x7FFFFFFF;

		delta = std::rotl<uint32_t>(delta, 1);

		VDWriteUnalignedLEU32(dst2, delta);

		dst2 += rowSize;
	}
}

void ATSavedTracePredictorSignMag32::Decode(uint8 *dst, uint32 rowSize, size_t rowCount) {
	uint8 *VDRESTRICT dst2 = dst + mOffset;

	while(rowCount--) {
		uint32 delta = VDReadUnalignedLEU32(dst2);

		delta = std::rotr<uint32_t>(delta, 1);

		if (delta & 0x80000000)
			delta ^= 0x7FFFFFFF;

		VDWriteUnalignedLEU32(dst2, delta);

		dst2 += rowSize;
	}
}

////////////////////////////////////////////////////////////////////////////////

void ATSavedTraceCPUHistoryDecoder::Init(const ATSavedTraceCPUChannelDetail& info) {
	const uint32 blockCount = info.mRowCount ? (info.mRowCount - 1) / info.mRowGroupSize + 1 : 0;

	// width=0 and width>4096 are already checked during serialization

	// if we didn't have a codec, it's invalid, but it may be an
	// unsupported one
	if (!info.mpCodec)
		throw ATUnsupportedSaveStateException();

	// stripe must have correct block count and all blocks must exist
	if (info.mBlocks.size() != blockCount)
		throw ATInvalidSaveStateException();

	for(const auto& block : info.mBlocks) {
		if (!block)
			throw ATInvalidSaveStateException();
	}

	for(const ATSavedTraceCPUColumnInfo& col : info.mColumns) {
		const uint32 byteOffset = col.mBitOffset >> 3;

		switch(col.mType) {
			case ATSavedTraceCPUColumnType::A:
				mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mA), byteOffset);
				break;

			case ATSavedTraceCPUColumnType::X:
				mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mX), byteOffset);
				break;

			case ATSavedTraceCPUColumnType::Y:
				mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mY), byteOffset);
				break;

			case ATSavedTraceCPUColumnType::S:
				mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mS), byteOffset);
				break;

			case ATSavedTraceCPUColumnType::P:
				mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mP), byteOffset);
				break;

			case ATSavedTraceCPUColumnType::PC:
				if (col.mBitWidth >= 16) {
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mPC), byteOffset);
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mPC) + 1, byteOffset + 1);
				}
				break;

			case ATSavedTraceCPUColumnType::Opcode:
				mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mOpcode) + 0, byteOffset);

				if (col.mBitWidth >= 16)
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mOpcode) + 1, byteOffset + 1);

				if (col.mBitWidth >= 24)
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mOpcode) + 2, byteOffset + 2);

				if (col.mBitWidth >= 32)
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mOpcode) + 3, byteOffset + 3);
				break;

			case ATSavedTraceCPUColumnType::Cycle:
				mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mCycle), byteOffset);
				mCycleStep = 0x100;

				if (col.mBitWidth >= 16) {
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mCycle) + 1, byteOffset + 1);
					mCycleStep = 0x10000;

					if (col.mBitWidth >= 24) {
						mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mCycle) + 2, byteOffset + 2);
						mCycleStep = 0x1000000;

						if (col.mBitWidth >= 32) {
							mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mCycle) + 3, byteOffset + 3);
							mCycleStep = 0;
						}
					}
				}
				break;

			case ATSavedTraceCPUColumnType::UnhaltedCycle:
				mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mUnhaltedCycle), byteOffset);
				mUnhaltedCycleStep = 0x100;

				if (col.mBitWidth >= 16) {
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mUnhaltedCycle) + 1, byteOffset + 1);
					mUnhaltedCycleStep = 0x10000;

					if (col.mBitWidth >= 24) {
						mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mUnhaltedCycle) + 2, byteOffset + 2);
						mUnhaltedCycleStep = 0x1000000;

						if (col.mBitWidth >= 32) {
							mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mUnhaltedCycle) + 3, byteOffset + 3);
							mUnhaltedCycleStep = 0;
						}
					}
				}
				break;

			case ATSavedTraceCPUColumnType::Irq:
				mIrqOffset = byteOffset;
				mIrqMask = 1 << (col.mBitOffset & 7);
				break;

			case ATSavedTraceCPUColumnType::Nmi:
				mNmiOffset = byteOffset;
				mNmiMask = 1 << (col.mBitOffset & 7);
				break;

			case ATSavedTraceCPUColumnType::EffectiveAddress:
				if (col.mBitWidth >= 32) {
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mEA), byteOffset);
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mEA) + 1, byteOffset + 1);
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mEA) + 2, byteOffset + 2);
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mEA) + 3, byteOffset + 3);
				}
				break;

			case ATSavedTraceCPUColumnType::GlobalPCBase:
				if (col.mBitWidth >= 32) {
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mGlobalPCBase), byteOffset);
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mGlobalPCBase) + 1, byteOffset + 1);
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mGlobalPCBase) + 2, byteOffset + 2);
					mCopyPairs.emplace_back(offsetof(ATCPUHistoryEntry, mGlobalPCBase) + 3, byteOffset + 3);
				}
				break;
		}
	}

	const uint32 rowSize = info.mRowSize;

	if (rowSize > 4096)
		throw ATUnsupportedSaveStateException();

	mRowSize = rowSize;

	mBaseEntry = {};
	mBaseEntry.mEA = ~(uint32)0;
	mBaseEntry.mP = 0x30;

	for(const auto& copy : mCopyPairs)
		*((unsigned char *)&mBaseEntry + copy.first) = 0;

#if defined(VD_CPU_X86) || defined(VD_CPU_X64) || defined(VD_CPU_ARM64)
	mFastCopyMaps.resize(((rowSize - 1) / 16 + 1) * 32, 0x80);
	
	for(const auto [dstOffset, srcOffset] : mCopyPairs)
		mFastCopyMaps[(srcOffset >> 4)*32 + dstOffset] = srcOffset & 15;
#endif
}

void ATSavedTraceCPUHistoryDecoder::Decode(const uint8 *src, ATCPUHistoryEntry *hedst, size_t n) {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	if (CPUGetEnabledExtensions() & CPUF_SUPPORTS_SSSE3) {
		if (mFastCopyMaps.size() == 64)
			return Decode_SSSE3_32(src, hedst, n);
		else
			return Decode_SSSE3(src, hedst, n);
	}

	return Decode_Scalar(src, hedst, n);
#elif defined(VD_CPU_ARM64)
	return Decode_NEON(src, hedst, n);
#else
	return Decode_Scalar(src, hedst, n);
#endif
}

void ATSavedTraceCPUHistoryDecoder::Decode_Scalar(const uint8 *src, ATCPUHistoryEntry *hedst, size_t n) {
	const uint32 rowSize = mRowSize;
	uint32 lastCycle = mLastCycle;
	uint32 lastUnhaltedCycle = mLastUnhaltedCycle;
	uint32 baseCycle = mBaseCycle;
	uint32 baseUnhaltedCycle = mBaseUnhaltedCycle;

	for(size_t i=0; i<n; ++i) {
		ATCPUHistoryEntry& VDRESTRICT he = hedst[i];

		he = mBaseEntry;

		for(const auto& copy : mCopyPairs)
			*((unsigned char *)&he + copy.first) = src[copy.second];

		if (mIrqMask)
			he.mbIRQ = (src[mIrqOffset] & mIrqMask) != 0;

		if (mNmiMask)
			he.mbNMI = (src[mNmiOffset] & mNmiMask) != 0;

		if (he.mCycle < lastCycle)
			baseCycle += mCycleStep;

		if (he.mUnhaltedCycle < lastUnhaltedCycle)
			baseUnhaltedCycle += mUnhaltedCycleStep;

		lastCycle = he.mCycle;
		lastUnhaltedCycle = he.mUnhaltedCycle;

		he.mCycle += baseCycle;
		he.mUnhaltedCycle += baseUnhaltedCycle;

		src += rowSize;
	}

	mLastCycle = lastCycle;
	mLastUnhaltedCycle = lastUnhaltedCycle;
	mBaseCycle = baseCycle;
	mBaseUnhaltedCycle = baseUnhaltedCycle;
}

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
VD_CPU_TARGET("ssse3")
void ATSavedTraceCPUHistoryDecoder::Decode_SSSE3(const uint8 *src, ATCPUHistoryEntry *hedst, size_t n) {
	const uint32 rowSize = mRowSize;

	__m128i base1 = _mm_or_si128(
		_mm_set_epi32(0, 0, mBaseUnhaltedCycle, mBaseCycle),
		_mm_loadu_si128((const __m128i *)&mBaseEntry)
	);

	__m128i base2 = _mm_loadu_si128((const __m128i *)&mBaseEntry + 1);
	__m128i lastv1 = _mm_set_epi32(0, 0, mLastUnhaltedCycle, mLastCycle);
	__m128i incs = _mm_set_epi32(0, 0, mUnhaltedCycleStep, mCycleStep);

	size_t numCopyMaps = mFastCopyMaps.size() / 32;
	const __m128i *VDRESTRICT copyMaps = (const __m128i *)mFastCopyMaps.data();

	for(size_t i=0; i<n; ++i) {
		ATCPUHistoryEntry& VDRESTRICT he = hedst[i];

		__m128i v1 = _mm_setzero_si128();
		__m128i v2 = _mm_setzero_si128();

		for(size_t i=0; i<numCopyMaps; ++i) {
			__m128i vsrc = _mm_loadu_si128((const __m128i *)(src + 16*i));

			v1 = _mm_or_si128(v1, _mm_shuffle_epi8(vsrc, copyMaps[2*i+0]));
			v2 = _mm_or_si128(v2, _mm_shuffle_epi8(vsrc, copyMaps[2*i+1]));
		}

		__m128i carry = _mm_cmplt_epi32(v1, lastv1);
		lastv1 = v1;

		base1 = _mm_add_epi32(base1, _mm_and_si128(carry, incs));
		
		_mm_storeu_si128((__m128i *)&he + 0, _mm_or_si128(v1, base1));
		_mm_storeu_si128((__m128i *)&he + 1, _mm_or_si128(v2, base2));

		if (src[mIrqOffset] & mIrqMask)
			he.mbIRQ = true;

		if (src[mNmiOffset] & mNmiMask)
			he.mbNMI = true;

		src += rowSize;
	}

	mLastCycle = _mm_cvtsi128_si32(lastv1);
	mLastUnhaltedCycle = _mm_cvtsi128_si32(_mm_shuffle_epi32(lastv1, 0x55));
	mBaseCycle = _mm_cvtsi128_si32(base1);
	mBaseUnhaltedCycle = _mm_cvtsi128_si32(_mm_shuffle_epi32(base1, 0x55));
}

VD_CPU_TARGET("ssse3")
void ATSavedTraceCPUHistoryDecoder::Decode_SSSE3_32(const uint8 *src, ATCPUHistoryEntry *hedst, size_t n) {
	const uint32 rowSize = mRowSize;

	__m128i base1 = _mm_or_si128(
		_mm_set_epi32(0, 0, mBaseUnhaltedCycle, mBaseCycle),
		_mm_loadu_si128((const __m128i *)&mBaseEntry)
	);

	__m128i base2 = _mm_loadu_si128((const __m128i *)&mBaseEntry + 1);
	__m128i lastv1 = _mm_set_epi32(0, 0, mLastUnhaltedCycle, mLastCycle);
	__m128i incs = _mm_set_epi32(0, 0, mUnhaltedCycleStep, mCycleStep);

	const __m128i *VDRESTRICT copyMaps = (const __m128i *)mFastCopyMaps.data();
	const __m128i copy11 = copyMaps[0];
	const __m128i copy12 = copyMaps[1];
	const __m128i copy21 = copyMaps[2];
	const __m128i copy22 = copyMaps[3];

	for(size_t i=0; i<n; ++i) {
		ATCPUHistoryEntry& VDRESTRICT he = hedst[i];

		__m128i vsrc1 = _mm_loadu_si128((const __m128i *)(src + 0));
		__m128i vsrc2 = _mm_loadu_si128((const __m128i *)(src + 16));

		__m128i v1 = _mm_or_si128(_mm_shuffle_epi8(vsrc1, copy11), _mm_shuffle_epi8(vsrc2, copy21));
		__m128i v2 = _mm_or_si128(_mm_shuffle_epi8(vsrc1, copy12), _mm_shuffle_epi8(vsrc2, copy22));

		__m128i carry = _mm_cmplt_epi32(v1, lastv1);
		lastv1 = v1;

		base1 = _mm_add_epi32(base1, _mm_and_si128(carry, incs));
		
		_mm_storeu_si128((__m128i *)&he + 0, _mm_or_si128(v1, base1));
		_mm_storeu_si128((__m128i *)&he + 1, _mm_or_si128(v2, base2));

		if (src[mIrqOffset] & mIrqMask)
			he.mbIRQ = true;

		if (src[mNmiOffset] & mNmiMask)
			he.mbNMI = true;

		src += rowSize;
	}

	mLastCycle = _mm_cvtsi128_si32(lastv1);
	mLastUnhaltedCycle = _mm_cvtsi128_si32(_mm_shuffle_epi32(lastv1, 0x55));
	mBaseCycle = _mm_cvtsi128_si32(base1);
	mBaseUnhaltedCycle = _mm_cvtsi128_si32(_mm_shuffle_epi32(base1, 0x55));
}
#endif

#if defined(VD_CPU_ARM64)
void ATSavedTraceCPUHistoryDecoder::Decode_NEON(const uint8 *src, ATCPUHistoryEntry *hedst, size_t n) {
	const uint32 rowSize = mRowSize;

	uint8x16_t base1 = vld1q_u8((const uint8_t *)&mBaseEntry);

	base1 = vreinterpretq_u8_u32(vsetq_lane_u32(mBaseCycle, vreinterpretq_u32_u8(base1), 0));
	base1 = vreinterpretq_u8_u32(vsetq_lane_u32(mBaseUnhaltedCycle, vreinterpretq_u32_u8(base1), 1));

	uint8x16_t base2 = vld1q_u8((const uint8_t *)&mBaseEntry + 16);
	uint32x2_t lastv1 = vset_lane_u32(mLastUnhaltedCycle, vdup_n_u32(mLastCycle), 1);
	uint32x2_t incs = vset_lane_u32(mUnhaltedCycleStep, vdup_n_u32(mCycleStep), 1);

	const size_t numCopyMaps = mFastCopyMaps.size() / 32;
	const uint8 *VDRESTRICT copyMaps = mFastCopyMaps.data();

	for(size_t i=0; i<n; ++i) {
		ATCPUHistoryEntry& VDRESTRICT he = hedst[i];

		uint8x16_t v1 = vdupq_n_u8(0);
		uint8x16_t v2 = vdupq_n_u8(0);

		for(size_t i=0; i<numCopyMaps; ++i) {
			uint8x16_t vsrc = vld1q_u8(src + 16*i);

			v1 = vqtbx1q_u8(v1, vsrc, vld1q_u8(&copyMaps[32*i]));
			v2 = vqtbx1q_u8(v2, vsrc, vld1q_u8(&copyMaps[32*i + 16]));
		}

		uint32x2_t carry = vclt_u32(vreinterpret_u32_u8(vget_low_u8(v1)), lastv1);
		lastv1 = vreinterpret_u32_u8(vget_low_u8(v1));

		base1 = vcombine_u8(vadd_u32(vreinterpret_u32_u8(vget_low_u8(base1)), vand_u32(carry, incs)), vget_high_u8(base1));
		
		vst1q_u8((uint8_t *)&he + 0, vorrq_u8(v1, base1));
		vst1q_u8((uint8_t *)&he + 16, vorrq_u8(v2, base2));

		if (src[mIrqOffset] & mIrqMask)
			he.mbIRQ = true;

		if (src[mNmiOffset] & mNmiMask)
			he.mbNMI = true;

		src += rowSize;
	}

	mLastCycle = vget_lane_u32(lastv1, 0);
	mLastUnhaltedCycle = vget_lane_u32(lastv1, 1);
	mBaseCycle = vgetq_lane_u32(vreinterpretq_u32_u8(base1), 0);
	mBaseUnhaltedCycle = vgetq_lane_u32(vreinterpretq_u32_u8(base1), 1);
}
#endif
