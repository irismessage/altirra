//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2011 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/math.h>
#include <vd2/system/refcount.h>
#include <vd2/Riza/audioformat.h>
#include <at/atcore/progress.h>
#include <at/atio/cassetteblock.h>
#include <at/atio/cassettedecoder.h>
#include "audiosource.h"
#include "cassetteimage.h"
#include "debuggerlog.h"
#include <emmintrin.h>

#ifdef _DEBUG
	#define AT_CASSETTE_VALIDATE() Validate()
#else
	#define AT_CASSETTE_VALIDATE() ((void)0)
#endif

using namespace nsVDWinFormats;

ATDebuggerLogChannel g_ATLCCasImage(false, false, "CASIMAGE", "Cassette image processing");

///////////////////////////////////////////////////////////////////////////

namespace {
	void ReadMono8(sint16 *dst, IVDRandomAccessStream& src, uint32 count) {
		uint8 buf[1024];

		while(count) {
			uint32 tc = count > 1024 ? 1024 : count;
			count -= tc;

			src.Read(buf, tc);

			for(uint32 i=0; i<tc; ++i) {
				dst[0] = dst[1] = (buf[i] << 8) - 0x8000;
				dst += 2;
			}
		}
	}

	void ReadMono16(sint16 *dst, IVDRandomAccessStream& src, uint32 count) {
		sint16 buf[1024];

		while(count) {
			uint32 tc = count > 1024 ? 1024 : count;
			count -= tc;

			src.Read(buf, tc*2);

			for(uint32 i=0; i<tc; ++i) {
				dst[0] = dst[1] = buf[i];
				dst += 2;
			}
		}
	}

	void ReadStereo8(sint16 *dst, IVDRandomAccessStream& src, uint32 count) {
		uint8 buf[1024][2];

		while(count) {
			uint32 tc = count > 1024 ? 1024 : count;
			count -= tc;

			src.Read(buf, tc*2);

			for(uint32 i=0; i<tc; ++i) {
				dst[0] = (buf[i][0] << 8) - 0x8000;
				dst[1] = (buf[i][1] << 8) - 0x8000;
				dst += 2;
			}
		}
	}

	void ReadStereo16(sint16 *dst, IVDRandomAccessStream& src, uint32 count) {
		src.Read(dst, count*4);
	}

	extern "C" VDALIGN(16) const sint16 kernel[32][8] = {
		{+0x0000,+0x0000,+0x0000,+0x4000,+0x0000,+0x0000,+0x0000,+0x0000 },
		{-0x000a,+0x0052,-0x0179,+0x3fe2,+0x019f,-0x005b,+0x000c,+0x0000 },
		{-0x0013,+0x009c,-0x02cc,+0x3f86,+0x0362,-0x00c0,+0x001a,+0x0000 },
		{-0x001a,+0x00dc,-0x03f9,+0x3eef,+0x054a,-0x012c,+0x002b,+0x0000 },
		{-0x001f,+0x0113,-0x0500,+0x3e1d,+0x0753,-0x01a0,+0x003d,+0x0000 },
		{-0x0023,+0x0141,-0x05e1,+0x3d12,+0x097c,-0x021a,+0x0050,-0x0001 },
		{-0x0026,+0x0166,-0x069e,+0x3bd0,+0x0bc4,-0x029a,+0x0066,-0x0001 },
		{-0x0027,+0x0182,-0x0738,+0x3a5a,+0x0e27,-0x031f,+0x007d,-0x0002 },
		{-0x0028,+0x0197,-0x07b0,+0x38b2,+0x10a2,-0x03a7,+0x0096,-0x0003 },
		{-0x0027,+0x01a5,-0x0807,+0x36dc,+0x1333,-0x0430,+0x00af,-0x0005 },
		{-0x0026,+0x01ab,-0x083f,+0x34db,+0x15d5,-0x04ba,+0x00ca,-0x0007 },
		{-0x0024,+0x01ac,-0x085b,+0x32b3,+0x1886,-0x0541,+0x00e5,-0x0008 },
		{-0x0022,+0x01a6,-0x085d,+0x3068,+0x1b40,-0x05c6,+0x0101,-0x000b },
		{-0x001f,+0x019c,-0x0846,+0x2dfe,+0x1e00,-0x0644,+0x011c,-0x000d },
		{-0x001c,+0x018e,-0x0819,+0x2b7a,+0x20c1,-0x06bb,+0x0136,-0x0010 },
		{-0x0019,+0x017c,-0x07d9,+0x28e1,+0x2380,-0x0727,+0x014f,-0x0013 },
		{-0x0016,+0x0167,-0x0788,+0x2637,+0x2637,-0x0788,+0x0167,-0x0016 },
		{-0x0013,+0x014f,-0x0727,+0x2380,+0x28e1,-0x07d9,+0x017c,-0x0019 },
		{-0x0010,+0x0136,-0x06bb,+0x20c1,+0x2b7a,-0x0819,+0x018e,-0x001c },
		{-0x000d,+0x011c,-0x0644,+0x1e00,+0x2dfe,-0x0846,+0x019c,-0x001f },
		{-0x000b,+0x0101,-0x05c6,+0x1b40,+0x3068,-0x085d,+0x01a6,-0x0022 },
		{-0x0008,+0x00e5,-0x0541,+0x1886,+0x32b3,-0x085b,+0x01ac,-0x0024 },
		{-0x0007,+0x00ca,-0x04ba,+0x15d5,+0x34db,-0x083f,+0x01ab,-0x0026 },
		{-0x0005,+0x00af,-0x0430,+0x1333,+0x36dc,-0x0807,+0x01a5,-0x0027 },
		{-0x0003,+0x0096,-0x03a7,+0x10a2,+0x38b2,-0x07b0,+0x0197,-0x0028 },
		{-0x0002,+0x007d,-0x031f,+0x0e27,+0x3a5a,-0x0738,+0x0182,-0x0027 },
		{-0x0001,+0x0066,-0x029a,+0x0bc4,+0x3bd0,-0x069e,+0x0166,-0x0026 },
		{-0x0001,+0x0050,-0x021a,+0x097c,+0x3d12,-0x05e1,+0x0141,-0x0023 },
		{+0x0000,+0x003d,-0x01a0,+0x0753,+0x3e1d,-0x0500,+0x0113,-0x001f },
		{+0x0000,+0x002b,-0x012c,+0x054a,+0x3eef,-0x03f9,+0x00dc,-0x001a },
		{+0x0000,+0x001a,-0x00c0,+0x0362,+0x3f86,-0x02cc,+0x009c,-0x0013 },
		{+0x0000,+0x000c,-0x005b,+0x019f,+0x3fe2,-0x0179,+0x0052,-0x000a },
	};

	uint64 resample16x2_scalar(sint16 *d, const sint16 *s, uint32 count, uint64 accum, sint64 inc) {
		do {
			const sint16 *s2 = s + (uint32)(accum >> 32)*2;
			const sint16 *f = kernel[(uint32)accum >> 27];

			accum += inc;

			sint32 l= (sint32)s2[ 0]*(sint32)f[0]
					+ (sint32)s2[ 2]*(sint32)f[1]
					+ (sint32)s2[ 4]*(sint32)f[2]
					+ (sint32)s2[ 6]*(sint32)f[3]
					+ (sint32)s2[ 8]*(sint32)f[4]
					+ (sint32)s2[10]*(sint32)f[5]
					+ (sint32)s2[12]*(sint32)f[6]
					+ (sint32)s2[14]*(sint32)f[7]
					+ 0x20002000;

			sint32 r= (sint32)s2[ 1]*(sint32)f[0]
					+ (sint32)s2[ 3]*(sint32)f[1]
					+ (sint32)s2[ 5]*(sint32)f[2]
					+ (sint32)s2[ 7]*(sint32)f[3]
					+ (sint32)s2[ 9]*(sint32)f[4]
					+ (sint32)s2[11]*(sint32)f[5]
					+ (sint32)s2[13]*(sint32)f[6]
					+ (sint32)s2[15]*(sint32)f[7]
					+ 0x20002000;

			l >>= 14;
			r >>= 14;

			if ((uint32)l >= 0x10000)
				l = ~l >> 31;
			if ((uint32)r >= 0x10000)
				r = ~r >> 31;

			d[0] = (sint16)(l - 0x8000);
			d[1] = (sint16)(r - 0x8000);
			d += 2;
		} while(--count);

		return accum;
	}

	uint64 resample16x2_SSE2(sint16 *d, const sint16 *s, uint32 count, uint64 accum, sint64 inc) {
		__m128i round = _mm_set1_epi32(0x2000);

		do {
			const sint16 *s2 = s + (uint32)(accum >> 32)*2;
			const sint16 *f = kernel[(uint32)accum >> 27];
			const __m128i coeff16 = *(const __m128i *)f;

			accum += inc;

			__m128i x0 = _mm_loadu_si128((__m128i *)s2);
			__m128i x1 = _mm_loadu_si128((__m128i *)s2 + 1);

			__m128i y0 = _mm_shufflehi_epi16(_mm_shufflelo_epi16(x0, 0xd8), 0xd8);
			__m128i y1 = _mm_shufflehi_epi16(_mm_shufflelo_epi16(x1, 0xd8), 0xd8);

			__m128i z0 = _mm_madd_epi16(y0, _mm_shuffle_epi32(coeff16, 0x50));
			__m128i z1 = _mm_madd_epi16(y1, _mm_shuffle_epi32(coeff16, 0xfa));

			__m128i a = _mm_add_epi32(z0, z1);
			__m128i b = _mm_add_epi32(a, _mm_shuffle_epi32(a, 0xee));
			__m128i r = _mm_srai_epi32(_mm_add_epi32(b, round), 14);

			__m128i result = _mm_packs_epi32(r, r);

			*(int *)d = _mm_cvtsi128_si32(result);
			d += 2;
		} while(--count);

		return accum;
	}

	uint64 resample16x2(sint16 *d, const sint16 *s, uint32 count, uint64 accum, sint64 inc) {
		if (SSE2_enabled)
			return resample16x2_SSE2(d, s, count, accum, inc);
		else
			return resample16x2_scalar(d, s, count, accum, inc);
	}

	void minMax16x2_SSE2(const sint16 * VDRESTRICT src, uint32 n, sint32& minvL, sint32& maxvL, sint32& minvR, sint32& maxvR) {
		// We do unaligned loads from this array, so it's important that we
		// avoid data cache unit (DCU) split penalties on older CPUs.
		static const __declspec(align(64)) uint64 window_table[6] = {
			0, 0, (uint64)0 - 1, (uint64)0 - 1, 0, 0
		};

		const __m128i * VDRESTRICT src128 = (const __m128i *)((uintptr)src & ~(uintptr)15);
		const __m128i * VDRESTRICT srcend128 = (const __m128i *)((uintptr)(src + n*2) & ~(uintptr)15);
		const ptrdiff_t leftOffset = (ptrdiff_t)((uintptr)src & 15);
		const __m128i leftMask = _mm_loadu_si128((const __m128i *)((const char *)window_table + 16 - leftOffset));
		const ptrdiff_t rightOffset = (ptrdiff_t)((uintptr)(src + n * 2) & 15);
		const __m128i rightMask = _mm_loadu_si128((const __m128i *)((const char *)window_table + 32 - rightOffset));

		__m128i minAcc = _mm_insert_epi16(_mm_cvtsi32_si128(minvL), minvR, 1);
		__m128i maxAcc = _mm_insert_epi16(_mm_cvtsi32_si128(maxvL), maxvR, 1);

		if (src128 != srcend128) {
			__m128i vleft = _mm_and_si128(*src128++, leftMask);
			minAcc = _mm_min_epi16(minAcc, vleft);
			maxAcc = _mm_max_epi16(maxAcc, vleft);

			while(src128 != srcend128) {
				__m128i vmid = *src128++;

				minAcc = _mm_min_epi16(minAcc, vmid);
				maxAcc = _mm_max_epi16(maxAcc, vmid);
			}

			if (rightOffset) {
				__m128i vright = _mm_and_si128(*src128, rightMask);
				minAcc = _mm_min_epi16(minAcc, vright);
				maxAcc = _mm_max_epi16(maxAcc, vright);
			}
		} else {
			__m128i v = _mm_and_si128(src128[0], _mm_and_si128(leftMask, rightMask));

			minAcc = _mm_min_epi16(minAcc, v);
			maxAcc = _mm_max_epi16(maxAcc, v);
		}

		// four four accumulators
		minAcc = _mm_min_epi16(minAcc, _mm_shuffle_epi32(minAcc, 0xEE));
		maxAcc = _mm_max_epi16(maxAcc, _mm_shuffle_epi32(maxAcc, 0xEE));
		minAcc = _mm_min_epi16(minAcc, _mm_shuffle_epi32(minAcc, 0x55));
		maxAcc = _mm_max_epi16(maxAcc, _mm_shuffle_epi32(maxAcc, 0x55));

		minvL = (sint16)_mm_extract_epi16(minAcc, 0);
		minvR = (sint16)_mm_extract_epi16(minAcc, 1);
		maxvL = (sint16)_mm_extract_epi16(maxAcc, 0);
		maxvR = (sint16)_mm_extract_epi16(maxAcc, 1);
	}

	void minMax16x2_scalar(const sint16 * VDRESTRICT src, uint32 n, sint32& minvL, sint32& maxvL, sint32& minvR, sint32& maxvR) {
		for(uint32 i=0; i<n; ++i) {
			sint32 vL = src[0];
			sint32 vR = src[1];
			src += 2;

			minvL = std::min(minvL, vL);
			maxvL = std::max(maxvL, vL);
			minvR = std::min(minvR, vR);
			maxvR = std::max(maxvR, vR);
		}
	}

	void minMax16x2(const sint16 * VDRESTRICT src, uint32 n, sint32& minvL, sint32& maxvL, sint32& minvR, sint32& maxvR) {
		if (SSE2_enabled) {
			minMax16x2_SSE2(src, n, minvL, maxvL, minvR, maxvR);
		} else
			minMax16x2_scalar(src, n, minvL, maxvL, minvR, maxvR);
	}
}

///////////////////////////////////////////////////////////////////////////////

class ATCassetteTooLongException : public MyError {
public:
	ATCassetteTooLongException() : MyError("Tape too long (exceeds 30 hours).") {}
};

///////////////////////////////////////////////////////////////////////////////

class ATCassetteImage final : public vdrefcounted<IATCassetteImage> {
public:
	ATCassetteImage();
	~ATCassetteImage();

	uint32 GetDataLength() const override { return mDataLength; }
	uint32 GetAudioLength() const override { return mAudioLength; }

	bool GetBit(uint32 pos, uint32 averagingPeriod, uint32 threshold, bool prevBit) const;
	void ReadPeakMap(float t0, float dt, uint32 n, float *data, float *audio) override;
	void AccumulateAudio(float *&dstLeft, float *&dstRight, uint32& posSample, uint32& posCycle, uint32 n) const override;

	uint32 GetWriteCursor() const override;
	void SetWriteCursor(uint32 writePos) override;
	void WriteBlankData(uint32 len) override;
	void WriteStdData(uint8 byte, uint32 cyclesPerHalfBit) override;
	void WriteFSKPulse(bool polarity, uint32 samples) override;

	void InitNew();
	void Load(IVDRandomAccessStream& file, bool loadImageAsData);
	void SaveCAS(IVDRandomAccessStream& file);

protected:
	struct SortedBlock {
		uint32	mStart;
		uint32	mOffset;
		ATCassetteImageBlock *mpImageBlock;
	};

	uint32 GetBitSum(uint32 pos, uint32 averagingPeriod) const;
	int GetSortedDataBlock(uint32 pos) const;

	uint32 SplitBlock(uint32 startBlockIdx, uint32 splitPt);

	void Validate();

	void ParseWAVE(IVDRandomAccessStream& file);
	void ParseCAS(IVDRandomAccessStream& file, bool loadDataAsAudio);
	void ConvertDataToPeaks();
	void RefreshPeaksFromData(uint32 startSample, uint32 endSample);

	uint32 mDataLength = 0;
	uint32 mAudioLength = 0;

	mutable int mCachedDataBlockIndex = 0;
	uint32 mDataBlockCount = 0;
	uint32 mAudioBlockCount = 0;

	uint32 mWriteCursor = 0;
	sint32 mCurrentWriteBlockIndex = -1;

	typedef vdfastvector<SortedBlock> SortedBlocks;
	SortedBlocks mDataBlocks;
	SortedBlocks mAudioBlocks;

	typedef vdfastvector<ATCassetteImageBlock *> ImageBlocks;
	ImageBlocks mImageBlocks;

	vdfastvector<uint8> mPeakMaps[2];

	uint32 mPeakDirtyStart = (uint32)0 - 1;
	uint32 mPeakDirtyEnd = 0;

	static const int kDataSamplesPerPeakSample;
	static const float kPeakSamplesPerSecond;
	static const float kSecondsPerPeakSample;
};

const int ATCassetteImage::kDataSamplesPerPeakSample = 512;
const float ATCassetteImage::kPeakSamplesPerSecond = kATCassetteDataSampleRate / (float)kDataSamplesPerPeakSample;
const float ATCassetteImage::kSecondsPerPeakSample = (float)kDataSamplesPerPeakSample / kATCassetteDataSampleRate;

ATCassetteImage::ATCassetteImage() {
}

ATCassetteImage::~ATCassetteImage() {
	while(!mImageBlocks.empty()) {
		ATCassetteImageBlock *p = mImageBlocks.back();

		if (p)
			delete p;

		mImageBlocks.pop_back();
	}
}

uint32 ATCassetteImage::GetBitSum(uint32 pos, uint32 len) const {
	if (pos >= mDataLength)
		return len;

	int idx = GetSortedDataBlock(pos);
	const auto *p = &mDataBlocks[idx];
	if (!p->mpImageBlock)
		return len;

	uint32 sum = 0;
	uint32 offset = pos - p->mStart;
	for(auto *p = &mDataBlocks[idx]; p->mpImageBlock; ++p) {
		uint32 sectionLen = p[1].mStart - pos;

		if (sectionLen > len)
			sectionLen = len;

		sum += p->mpImageBlock->GetBitSum(offset + p->mOffset, sectionLen);

		len-= sectionLen;
		if (!len)
			break;

		offset = 0;
		pos += sectionLen;
	}

	return sum;
}

bool ATCassetteImage::GetBit(uint32 pos, uint32 averagingPeriod, uint32 threshold, bool prevBit) const {
	uint32 sum = GetBitSum(pos, averagingPeriod);

	if (sum < threshold)
		return false;
	else if (sum > averagingPeriod - threshold)
		return true;
	else
		return prevBit;
}

void ATCassetteImage::ReadPeakMap(float t0, float dt, uint32 n, float *data, float *audio) {
	if (mPeakDirtyEnd > mPeakDirtyStart) {
		RefreshPeaksFromData(mPeakDirtyStart, mPeakDirtyEnd);
		mPeakDirtyStart = 0;
		--mPeakDirtyStart;	// intentional underflow to -1
		mPeakDirtyEnd = 0;
	}

	if (mPeakMaps[0].empty()) {
		memset(data, 0, sizeof(float)*n*2);
		memset(audio, 0, sizeof(float)*n*2);
		return;
	}

	const size_t m = mPeakMaps[0].size() >> 1;

	float x0 = t0 * kPeakSamplesPerSecond;
	float dx = dt * kPeakSamplesPerSecond;

	const auto *peakMap0 = mPeakMaps[0].data();
	const auto *peakMap1 = mPeakMaps[1].empty() ? peakMap0 : mPeakMaps[1].data();

	while(n--) {
		float x1 = x0 + dx;
		sint32 ix0 = VDCeilToInt(x0 - 0.5f);
		sint32 ix1 = VDCeilToInt(x1 - 0.5f);
		x0 = x1;

		if ((uint32)ix0 >= m)
			ix0 = (ix0 < 0) ? 0 : (sint32)(m-1);

		if ((uint32)ix1 >= m)
			ix1 = (sint32)m;

		ix0 *= 2;
		ix1 *= 2;

		uint8 minR = peakMap0[ix0];
		uint8 maxR = peakMap0[ix0+1];
		uint8 minL = peakMap1[ix0];
		uint8 maxL = peakMap1[ix0+1];

		for(sint32 ix = ix0 + 2; ix < ix1; ix += 2) {
			const uint8 vMinR = peakMap0[ix];
			const uint8 vMaxR = peakMap0[ix+1];
			const uint8 vMinL = peakMap1[ix];
			const uint8 vMaxL = peakMap1[ix+1];

			if (minR > vMinR) minR = vMinR;
			if (maxR < vMaxR) maxR = vMaxR;
			if (minL > vMinL) minL = vMinL;
			if (maxL < vMaxL) maxL = vMaxL;
		}

		*data++  = ((float)minR - 128.0f) / 127.0f;
		*data++  = ((float)maxR - 128.0f) / 127.0f;
		*audio++ = ((float)minL - 128.0f) / 127.0f;
		*audio++ = ((float)maxL - 128.0f) / 127.0f;
	}
}

void ATCassetteImage::AccumulateAudio(float *&dstLeft, float *&dstRight, uint32& posSample, uint32& posCycle, uint32 n) const {
	if (!n)
		return;

	uint32 i = 0;
	uint32 j = mAudioBlockCount;

	const SortedBlock *p;
	for(;;) {
		uint32 mid = (i + j) >> 1;
		p = &mAudioBlocks[mid];

		if (i + 1 >= j) {
			if (!p->mpImageBlock)
				return;

			break;
		}

		if (posSample < p->mStart)
			j = mid;
		else
			i = mid;
	}
	
	const uint32 posSample0 = posSample;
	const uint32 posCycle0 = posCycle;

	VDASSERT(posSample >= p->mStart);
	VDASSERT(!p->mpImageBlock || posSample <= p[1].mStart);

	while(n && p->mpImageBlock) {
		const uint32 audioSampleLimit = ((p[1].mStart - posSample) * kATCassetteCyclesPerAudioSample - posCycle + kATCyclesPerSyncSample - 1) / kATCyclesPerSyncSample;

		posSample -= p->mStart;
		posSample += p->mOffset;
		VDASSERT(posSample < UINT32_C(0x80000000));

		// check if we need to clip
		uint32 tc = n;
		if (tc > audioSampleLimit)
			tc = audioSampleLimit;

		n -= p->mpImageBlock->AccumulateAudio(dstLeft, dstRight, posSample, posCycle, tc);

		posSample -= p->mOffset;
		posSample += p->mStart;
		++p;

		VDASSERT(!n || posSample >= p->mStart);
	}
}

uint32 ATCassetteImage::GetWriteCursor() const {
	return mWriteCursor;
}

void ATCassetteImage::SetWriteCursor(uint32 pos) {
	if (pos > kATCassetteDataLimit)
		pos = kATCassetteDataLimit;

	mWriteCursor = pos;
	mCurrentWriteBlockIndex = -1;
}

void ATCassetteImage::WriteBlankData(uint32 len) {
	if (!len)
		return;

	// check if write would go beyond end and clamp
	if (mWriteCursor >= kATCassetteDataLimit)
		return;

	if (len > kATCassetteDataLimit - mWriteCursor)
		len = kATCassetteDataLimit - mWriteCursor;

	// if write cursor is beyond end, extend start to current end
	if (mWriteCursor > mDataLength) {
		const uint32 extendLen = mWriteCursor - mDataLength;

		mWriteCursor = mDataLength;
		len += extendLen;
	}

	AT_CASSETTE_VALIDATE();

	// check if we have a prev block
	if (mCurrentWriteBlockIndex < 0) {
		if (mWriteCursor)
			mCurrentWriteBlockIndex = (sint32)GetSortedDataBlock(mWriteCursor - 1);
		else
			mCurrentWriteBlockIndex = 0;
	}

	// check if the current write block is compatible
	if (!mDataBlocks[mCurrentWriteBlockIndex].mpImageBlock || mDataBlocks[mCurrentWriteBlockIndex].mpImageBlock->GetBlockType() != kATCassetteImageBlockType_Blank) {
		// split it and insert a new blank block 
		mCurrentWriteBlockIndex = SplitBlock(mCurrentWriteBlockIndex, mWriteCursor);

		if (!mDataBlocks[mCurrentWriteBlockIndex].mpImageBlock || mDataBlocks[mCurrentWriteBlockIndex].mpImageBlock->GetBlockType() != kATCassetteImageBlockType_Blank) {
			auto *emptyBlock = new ATCassetteImageBlockBlank;
			auto emptyBlockHolder = vdmakeautoptr(emptyBlock);

			mImageBlocks.push_back(emptyBlock);
			emptyBlockHolder.release();

			// insert null block at start
			mDataBlocks.insert(mDataBlocks.begin() + mCurrentWriteBlockIndex, SortedBlock { mWriteCursor, 0, emptyBlock });
			++mDataBlockCount;
		}
	}

	AT_CASSETTE_VALIDATE();

	VDASSERT(mDataBlocks[mCurrentWriteBlockIndex].mpImageBlock->GetBlockType() == kATCassetteImageBlockType_Blank);

	// advance write cursor
	if (mPeakDirtyStart > mWriteCursor)
		mPeakDirtyStart = mWriteCursor;

	mWriteCursor += len;

	if (mPeakDirtyEnd < mWriteCursor)
		mPeakDirtyEnd = mWriteCursor;

	// truncate valid overlapping blocks
	const uint32 nextIndex = mCurrentWriteBlockIndex + 1;
	for(;;) {
		auto& nextBlock = mDataBlocks[nextIndex];

		// stop if the next block no longer overlaps the write range
		if (nextBlock.mStart >= mWriteCursor)
			break;

		// stop if the next block is the sentinel
		if (!nextBlock.mpImageBlock) {
			// extend the end of tape and stop
			nextBlock.mStart = mWriteCursor;
			mDataLength = mWriteCursor;
			break;
		}

		// check if the next block is also blank -- if so, we can trivially merge with it
		if (nextBlock.mpImageBlock->GetBlockType() != kATCassetteImageBlockType_Blank) {
			// check if the next block is entirely contained in the write
			// range
			auto& nextNextBlock = mDataBlocks[nextIndex + 1];

			if (nextNextBlock.mStart > mWriteCursor) {
				// no -- check if it is compatible
				if (nextBlock.mpImageBlock->GetBlockType() != kATCassetteImageBlockType_Blank) {
					// no -- truncate and stop
					const uint32 truncOffset = nextNextBlock.mStart - mWriteCursor;

					nextBlock.mOffset += truncOffset;
					nextBlock.mStart = mWriteCursor;
					break;
				}
			}

			// yes -- fall through to delete the existing block (merge)
		}

		// yes -- delete the next block and continue
		mDataBlocks.erase(mDataBlocks.begin() + nextIndex);
		--mDataBlockCount;
	}

	AT_CASSETTE_VALIDATE();
}

void ATCassetteImage::WriteStdData(uint8 byte, uint32 baudRate) {
	if (!baudRate)
		return;

	// if write cursor is beyond end, insert an intermediate blank area
	if (mWriteCursor > mDataLength) {
		const uint32 extendLen = mWriteCursor - mDataLength;

		WriteBlankData(extendLen);
	}

	// check if we would go beyond end (with suitable buffer)
	if (mWriteCursor >= kATCassetteDataLimit || kATCassetteDataLimit - mWriteCursor < kATCassetteDataWriteByteBuffer)
		return;

	AT_CASSETTE_VALIDATE();

	// check if we have a prev block
	if (mCurrentWriteBlockIndex < 0) {
		if (mWriteCursor) {
			mCurrentWriteBlockIndex = (sint32)GetSortedDataBlock(mWriteCursor - 1);

			auto& prevBlock = mDataBlocks[mCurrentWriteBlockIndex];

			// split the previous block, even if it's a standard data block
			++mCurrentWriteBlockIndex;

			auto *emptyBlock = new ATCassetteImageDataBlockStd;
			auto emptyBlockHolder = vdmakeautoptr(emptyBlock);
			
			emptyBlock->Init(baudRate);

			mImageBlocks.push_back(emptyBlock);
			emptyBlockHolder.release();

			const SortedBlock newBlocks[] = {
				{ mWriteCursor, 0, emptyBlock },
				{ mWriteCursor, mWriteCursor - prevBlock.mStart, prevBlock.mpImageBlock}
			};

			mDataBlocks.insert(mDataBlocks.begin() + mCurrentWriteBlockIndex,
				std::begin(newBlocks), std::end(newBlocks));

			mDataBlockCount += 2;
		} else {
			// insert new block at start
			mCurrentWriteBlockIndex = 0;

			auto *emptyBlock = new ATCassetteImageDataBlockStd;
			auto emptyBlockHolder = vdmakeautoptr(emptyBlock);

			emptyBlock->Init(baudRate);

			mImageBlocks.push_back(emptyBlock);
			emptyBlockHolder.release();

			mDataBlocks.insert(mDataBlocks.begin(),
				SortedBlock { 0, 0, emptyBlock } );

			++mDataBlockCount;
		}
	} else {
		// we have an existing block -- check if the baud rate is compatible
		auto *curBlock = mDataBlocks[mCurrentWriteBlockIndex].mpImageBlock;

		if (curBlock->GetBlockType() != kATCassetteImageBlockType_Std
			|| static_cast<ATCassetteImageDataBlockStd *>(curBlock)->GetBaudRate() != baudRate)
		{
			// not compatible -- split the existing block if necessary, then insert a new block
			mCurrentWriteBlockIndex = SplitBlock(mCurrentWriteBlockIndex, mWriteCursor);

			VDASSERT(mDataBlocks[mCurrentWriteBlockIndex].mStart == mWriteCursor);

			auto *emptyBlock = new ATCassetteImageDataBlockStd;
			auto emptyBlockHolder = vdmakeautoptr(emptyBlock);

			emptyBlock->Init(baudRate);

			mImageBlocks.push_back(emptyBlock);
			emptyBlockHolder.release();

			mDataBlocks.insert(mDataBlocks.begin() + mCurrentWriteBlockIndex,
				SortedBlock { mWriteCursor, 0, emptyBlock } );

			++mDataBlockCount;
		}
	}

	// add the new data
	auto *dataBlockPtr = mDataBlocks[mCurrentWriteBlockIndex].mpImageBlock;
	VDASSERT(dataBlockPtr->GetBlockType() == kATCassetteImageBlockType_Std);

	auto *dataBlock = static_cast<ATCassetteImageDataBlockStd *>(dataBlockPtr);

	VDASSERT(mDataBlocks[mCurrentWriteBlockIndex].mOffset == 0);
	VDASSERT(dataBlock->GetBlockType() == kATCassetteImageBlockType_Std);
	dataBlock->AddData(&byte, 1);

	// advance write cursor
	const uint32 newWriteCursor = mDataBlocks[mCurrentWriteBlockIndex].mStart + dataBlock->GetDataSampleCount();
	VDASSERT(mWriteCursor <= newWriteCursor);

	if (mPeakDirtyStart > mWriteCursor)
		mPeakDirtyStart = mWriteCursor;

	if (mPeakDirtyEnd < newWriteCursor)
		mPeakDirtyEnd = newWriteCursor;

	mWriteCursor = newWriteCursor;

	// truncate valid overlapping blocks
	const uint32 nextIndex = mCurrentWriteBlockIndex + 1;
	for(;;) {
		auto& nextBlock = mDataBlocks[nextIndex];

		// stop if the next block no longer overlaps the write range
		if (nextBlock.mStart >= mWriteCursor)
			break;

		// stop if the next block is the sentinel
		if (!nextBlock.mpImageBlock) {
			// extend end of tape and stop
			nextBlock.mStart = mWriteCursor;
			mDataLength = mWriteCursor;
			break;
		}

		// check if the next block is entirely contained in the write
		// range
		auto& nextNextBlock = mDataBlocks[nextIndex + 1];

		if (nextNextBlock.mStart > mWriteCursor) {
			// no -- truncate and stop, even if it is also a data block
			const uint32 truncOffset = nextNextBlock.mStart - mWriteCursor;

			nextBlock.mOffset += truncOffset;
			nextBlock.mStart = mWriteCursor;
			break;
		}

		// yes -- delete the next block and continue
		mDataBlocks.erase(mDataBlocks.begin() + nextIndex);
		--mDataBlockCount;
	}

	AT_CASSETTE_VALIDATE();
}

void ATCassetteImage::WriteFSKPulse(bool polarity, uint32 samples) {
	// if write cursor is beyond end, insert an intermediate blank area
	if (mWriteCursor > mDataLength) {
		const uint32 extendLen = mWriteCursor - mDataLength;

		WriteBlankData(extendLen);
	}

	// check if we have a prev block
	if (mCurrentWriteBlockIndex < 0) {
		if (mWriteCursor) {
			mCurrentWriteBlockIndex = (sint32)GetSortedDataBlock(mWriteCursor - 1);

			auto& prevBlock = mDataBlocks[mCurrentWriteBlockIndex];

			// split the previous block, even if it's an FSK data block
			++mCurrentWriteBlockIndex;

			auto *emptyBlock = new ATCassetteImageBlockDataFSK;
			auto emptyBlockHolder = vdmakeautoptr(emptyBlock);

			mImageBlocks.push_back(emptyBlock);
			emptyBlockHolder.release();

			const SortedBlock newBlocks[] = {
				{ mWriteCursor, 0, emptyBlock },
				{ mWriteCursor, mWriteCursor - prevBlock.mStart, prevBlock.mpImageBlock}
			};

			mDataBlocks.insert(mDataBlocks.begin() + mCurrentWriteBlockIndex,
				std::begin(newBlocks), std::end(newBlocks));

			mDataBlockCount += 2;
		} else {
			// insert new block at start
			mCurrentWriteBlockIndex = 0;

			auto *emptyBlock = new ATCassetteImageBlockDataFSK;
			auto emptyBlockHolder = vdmakeautoptr(emptyBlock);

			mImageBlocks.push_back(emptyBlock);
			emptyBlockHolder.release();

			mDataBlocks.insert(mDataBlocks.begin(),
				SortedBlock { 0, 0, emptyBlock } );

			++mDataBlockCount;
		}
	}

	// add the new data
	auto *fskBlock = static_cast<ATCassetteImageBlockDataFSK *>(mDataBlocks[mCurrentWriteBlockIndex].mpImageBlock);

	VDASSERT(mDataBlocks[mCurrentWriteBlockIndex].mOffset == 0);
	VDASSERT(fskBlock->GetBlockType() == kATCassetteImageBlockType_FSK);
	fskBlock->AddPulseSamples(polarity, samples);

	// advance write cursor
	mWriteCursor = mDataBlocks[mCurrentWriteBlockIndex].mStart + fskBlock->GetDataSampleCount();

	// truncate valid overlapping blocks
	const uint32 nextIndex = mCurrentWriteBlockIndex + 1;
	for(;;) {
		auto& nextBlock = mDataBlocks[nextIndex];

		// stop if the next block no longer overlaps the write range
		if (nextBlock.mStart >= mWriteCursor)
			break;

		// stop if the next block is the sentinel
		if (!nextBlock.mpImageBlock) {
			// extend end of tape and stop
			nextBlock.mStart = mWriteCursor;
			mDataLength = mWriteCursor;
			break;
		}

		// check if the next block is entirely contained in the write
		// range
		auto& nextNextBlock = mDataBlocks[nextIndex + 1];

		if (nextNextBlock.mStart < mWriteCursor) {
			// no -- check if it is compatible
			if (nextBlock.mpImageBlock->GetBlockType() != kATCassetteImageBlockType_Blank) {
				// no -- truncate and stop
				const uint32 truncOffset = nextNextBlock.mStart - mWriteCursor;

				nextBlock.mOffset += truncOffset;
				nextBlock.mStart = mWriteCursor;
				break;
			}

			// yes -- fall through to delete the existing block (merge)
		}

		// yes -- delete the next block and continue
		mDataBlocks.erase(mDataBlocks.begin() + nextIndex);
		--mDataBlockCount;
	}
}

void ATCassetteImage::InitNew() {
	mCachedDataBlockIndex = 0;
	mWriteCursor = 0;
	mCurrentWriteBlockIndex = -1;
	mDataLength = 0;
	mAudioLength = 0;

	mDataBlocks.assign( { SortedBlock { 0, 0, nullptr } });
	mDataBlockCount = 0;

	mAudioBlocks.assign({ SortedBlock { 0, 0, nullptr } });
	mAudioBlockCount = 1;
}

void ATCassetteImage::Load(IVDRandomAccessStream& file, bool loadDataAsAudio) {
	uint32 basehdr;
	if (file.ReadData(&basehdr, 4) != 4)
		basehdr = 0;

	file.Seek(0);

	mCachedDataBlockIndex = 0;
	mWriteCursor = 0;
	mCurrentWriteBlockIndex = -1;

	uint32 baseid = VDFromLE32(basehdr);
	if (baseid == VDMAKEFOURCC('R', 'I', 'F', 'F'))
		ParseWAVE(file);
	else if (baseid == VDMAKEFOURCC('F', 'U', 'J', 'I'))
		ParseCAS(file, loadDataAsAudio);
	else
		throw MyError("%ls is not in a recognizable Atari cassette format.", file.GetNameForError());
}

void ATCassetteImage::SaveCAS(IVDRandomAccessStream& file) {
	VDBufferedWriteStream ws(&file, 65536);

	// write header
	const uint32 kFUJI = VDMAKEFOURCC('F', 'U', 'J', 'I');
	const uint32 fujiSize = 0;

	ws.Write(&kFUJI, 4);
	ws.Write(&fujiSize, 4);

	// iterate down the blocks
	uint32 pos = 0;
	uint32 lastBaudRate = 0;

	vdfastvector<uint32> pulses32;
	vdfastvector<uint16> pulses16;

	for(const SortedBlock& blk : mDataBlocks) {
		if (!blk.mpImageBlock)
			continue;

		switch(blk.mpImageBlock->GetBlockType()) {
			case kATCassetteImageBlockType_Blank:
			case kATCassetteImageBlockType_RawAudio:
				// skip these block types
				break;

			case kATCassetteImageBlockType_FSK:
				{
					const auto& fsk = *static_cast<ATCassetteImageBlockDataFSK *>(blk.mpImageBlock);
					uint32 gapMS = 0;

					if (blk.mStart > pos)
						gapMS = VDRoundToInt32((double)(blk.mStart - pos) * 1000.0 / kATCassetteDataSampleRate);

					uint8 header[8] = {
						(uint8)'f',
						(uint8)'s',
						(uint8)'k',
						(uint8)' ',
						0,
						0,
						0xFF,
						0xFF,
					};

					while(gapMS > 65535) {
						ws.Write(header, 8);
						gapMS -= 65535;
					}

					// convert raw data to pulse widths
					pulses32.clear();
					fsk.ExtractPulses(pulses32);

					// resample pulse widths from data sample rate to 100us
					// and convert pulses to 16-bit
					pulses16.clear();
					pulses16.reserve(pulses32.size());

					uint32 pulsePosSrcRate = 0;
					uint32 pulsePosDstRate = 0;
					for(uint32 pulseLen : pulses32) {
						uint32 pulsePosSrcRate2 = pulsePosSrcRate + pulseLen;
						uint32 pulsePosDstRate2 = (uint32)(0.5 + (double)pulsePosSrcRate2 * (10000.0 / (double)kATCassetteDataSampleRate));

						VDASSERT(pulsePosDstRate2 >= pulsePosDstRate);
						uint32 pulseLenDstRate = pulsePosDstRate2 - pulsePosDstRate;

						while(pulseLenDstRate > 0xFFFF) {
							pulseLenDstRate -= 0xFFFF;

							pulses16.push_back(0xFFFF);
							pulses16.push_back(0);
						}

						pulses16.push_back((uint16)pulseLenDstRate);

						pulsePosSrcRate = pulsePosSrcRate2;
						pulsePosDstRate = pulsePosDstRate2;
					}

					// stream out the pulses
					const uint16 *pulseSrc = pulses16.data();
					uint32 pulsesLeft = (uint32)pulses16.size();

					while(pulsesLeft) {
						// We can write a maximum of 65535 bytes, or 32767 pulses. However,
						// that's annoying because it repeats the 0 bit pulse, so we only
						// write 32766 pulses instead.
						uint32 pulsesToWrite = std::min<uint32>(pulsesLeft, 0x7FFE);

						VDWriteUnalignedLEU16(header + 4, pulsesToWrite * 2);
						VDWriteUnalignedLEU16(header + 6, gapMS);

						ws.Write(header, 8);
						ws.Write(pulseSrc, pulsesToWrite * 2);

						pulseSrc += pulsesToWrite;
						pulsesLeft -= pulsesToWrite;
						gapMS = 0;
					}

					pos = blk.mStart + fsk.GetDataSampleCount();
				}
				break;

			case kATCassetteImageBlockType_Std:
				{
					const auto& std = *static_cast<ATCassetteImageDataBlockStd *>(blk.mpImageBlock);
					const uint32 baudRate = std.GetBaudRate();

					if (lastBaudRate != baudRate) {
						lastBaudRate = baudRate;

						uint8 baudRateHeader[8] = {
							(uint8)'b',
							(uint8)'a',
							(uint8)'u',
							(uint8)'d',
						};

						VDWriteUnalignedLEU16(baudRateHeader + 6, (uint16)baudRate);

						ws.Write(baudRateHeader, 8);
					}

					uint32 gapMS = 0;

					if (blk.mStart > pos)
						gapMS = VDRoundToInt32((double)(blk.mStart - pos) * 1000.0 / kATCassetteDataSampleRate);

					uint8 header[8] = {
						(uint8)'d',
						(uint8)'a',
						(uint8)'t',
						(uint8)'a',
						0,
						0,
						0xFF,
						0xFF,
					};

					while(gapMS > 65535) {
						gapMS -= 65535;
						ws.Write(header, 8);
					}

					uint32 dataLen = std.GetDataLen();
					const uint8 *data = std.GetData();

					while(dataLen > 0) {
						uint32 tc = dataLen > 65535 ? 65535 : dataLen;

						VDWriteUnalignedLEU16(header + 4, tc);
						VDWriteUnalignedLEU16(header + 6, gapMS);

						ws.Write(header, 8);
						ws.Write(data, tc);

						dataLen -= tc;
						data += tc;

						gapMS = 0;
					}

					pos = blk.mStart + std.GetDataSampleCount();
				}
				break;
		}
	}

	ws.Flush();
}

int ATCassetteImage::GetSortedDataBlock(uint32 pos) const {
	uint32 i = 0;
	uint32 j = mDataBlockCount;

	if (pos < mDataBlocks[mCachedDataBlockIndex].mStart)
		j = mCachedDataBlockIndex;
	else if (pos >= mDataBlocks[mCachedDataBlockIndex + 1].mStart)
		i = mCachedDataBlockIndex + 1;
	else
		return mCachedDataBlockIndex;

	for(;;) {
		uint32 mid = (i + j) >> 1;
		const SortedBlock *p = &mDataBlocks[mid];

		if (i + 1 >= j) {
			if (p->mpImageBlock)
				mCachedDataBlockIndex = mid;

			return mid;
		}

		if (pos < p->mStart)
			j = mid;
		else
			i = mid;
	}
}

uint32 ATCassetteImage::SplitBlock(uint32 startBlockIdx, uint32 splitPt) {
	VDASSERT(mDataBlocks.size() == mDataBlockCount + 1);
	VDASSERT(startBlockIdx <= mDataBlockCount);

	const uint32 pos1 = mDataBlocks[startBlockIdx].mStart;

	VDASSERT(splitPt >= pos1);

	if (splitPt == pos1)
		return startBlockIdx;

	const uint32 pos2 = mDataBlocks[startBlockIdx + 1].mStart;
	VDASSERT(splitPt <= pos2);

	if (splitPt != pos2) {
		VDASSERT(startBlockIdx < mDataBlockCount);

		SortedBlock newBlock = mDataBlocks[startBlockIdx];

		newBlock.mStart = splitPt;
		newBlock.mOffset = splitPt - pos1;

		mDataBlocks.insert(mDataBlocks.begin() + startBlockIdx + 1, newBlock);
		++mDataBlockCount;

		AT_CASSETTE_VALIDATE();
	}
	
	return startBlockIdx + 1;
}

void ATCassetteImage::Validate() {
	VDASSERT(mDataBlocks.size() == mDataBlockCount + 1);
	VDASSERT(!mDataBlocks.back().mpImageBlock);
	VDASSERT(mDataLength == mDataBlocks.back().mStart);

	VDASSERT(std::is_sorted(mDataBlocks.begin(), mDataBlocks.end(),
		[](const SortedBlock& a, const SortedBlock& b) { return a.mStart < b.mStart; }));
}

void ATCassetteImage::ParseWAVE(IVDRandomAccessStream& file) {
	WaveFormatEx wf = {0};
	sint64 limit = file.Length();
	sint64 datapos = -1;
	uint32 datalen = 0;

	ATProgress progress;

	for(;;) {
		uint32 hdr[2];

		if (file.Pos() >= limit)
			break;

		if (8 != file.ReadData(hdr, 8))
			break;

		uint32 fcc = hdr[0];
		uint32 len = VDFromLE32(hdr[1]);

		switch(fcc) {
		case VDMAKEFOURCC('R', 'I', 'F', 'F'):
			limit = file.Pos() + len;
			if (len < 4)
				throw MyError("'%ls' is an invalid WAV file.", file.GetNameForError());

			file.Read(hdr, 4);
			if (hdr[0] != VDMAKEFOURCC('W', 'A', 'V', 'E'))
				throw MyError("'%ls' is not a WAV file.", file.GetNameForError());

			len = 0;
			break;

		case VDMAKEFOURCC('f', 'm', 't', ' '):
			{
				uint32 toread = std::min<uint32>(sizeof(wf), len);

				file.Read(&wf, toread);
				len -= toread;

				// validate format
				if (wf.mFormatTag != kWAVE_FORMAT_PCM
					|| (wf.mBitsPerSample != 8 && wf.mBitsPerSample != 16)
					|| (wf.mChannels != 1 && wf.mChannels != 2)
					|| (wf.mBlockAlign != wf.mBitsPerSample * wf.mChannels / 8)
					|| wf.mSamplesPerSec < 8000)
				{
					throw MyError("'%ls' uses an unsupported WAV format.", file.GetNameForError());
				}
			}
			break;

		case VDMAKEFOURCC('d', 'a', 't', 'a'):
			datapos = file.Pos();
			datalen = len;
			break;
		}

		if (len)
			file.Seek(file.Pos() + len + (len & 1));
	}

	if (!wf.mBlockAlign || datapos < 0)
		throw MyError("'%ls' is not a valid WAV file.", file.GetNameForError());

	// These are hard-coded into the 410 hardware.
	ATCassetteDecoderFSK	decoder;
//	ATCassetteDecoderDirect	decoder;

	uint64	resampAccum = 0;
	uint64	resampStep = VDRoundToInt64(wf.mSamplesPerSec / kATCassetteImageAudioRate * 4294967296.0f);

	sint16	inputBuffer[512][2] = {0};
	uint32	inputBufferLevel = 3;

	sint16	outputBuffer[4096][2] = {0};
	uint32	outputBufferIdx = 0;
	uint32	outputBufferLevel = 0;

	uint32	inputSamplesLeft = datalen / wf.mBlockAlign;

	file.Seek(datapos);

	int bitTimer = 0;
	int bitAccum = 0;

	mImageBlocks.resize(2, (ATCassetteImageBlock *)NULL);

	ATCassetteImageBlockDataFSK *pDataBlock = new ATCassetteImageBlockDataFSK;
	mImageBlocks[0] = pDataBlock;

	ATCassetteImageBlockRawAudio *pAudioBlock = new ATCassetteImageBlockRawAudio;
	mImageBlocks[1] = pAudioBlock;

	progress.InitF((uint32)((uint64)datalen >> 10), L"Processed %uK / %uK", L"Processing raw waveform");

	uint32 outAccum = 0;
	uint32 outAccumBits = 0;
	sint32 peakValAccumL0 = 0;
	sint32 peakValAccumL1 = 0;
	sint32 peakValAccumR0 = 0;
	sint32 peakValAccumR1 = 0;
	uint32 peakMapAccum = 0;
	uint32 peakMapAccumInc = (uint32)VDRoundToInt64((double)kPeakSamplesPerSecond * 4294967296.0 / (double)wf.mSamplesPerSec);

	uint32 peakLeft = 0;
	uint32 peakTotal = 0;

	for(;;) {
		if (outputBufferIdx >= outputBufferLevel) {
			uint32 toRead = 512 - inputBufferLevel;
			if (toRead > inputSamplesLeft)
				toRead = inputSamplesLeft;

			inputSamplesLeft -= toRead;

			if (wf.mBlockAlign == 1) {
				ReadMono8(inputBuffer[inputBufferLevel], file, toRead);
			} else if (wf.mBlockAlign == 2) {
				if (wf.mChannels == 1)
					ReadMono16(inputBuffer[inputBufferLevel], file, toRead);
				else
					ReadStereo8(inputBuffer[inputBufferLevel], file, toRead);
			} else if (wf.mBlockAlign == 4) {
				ReadStereo16(inputBuffer[inputBufferLevel], file, toRead);
			}
	
			progress.Update((uint32)((uint64)(file.Pos() - datapos) >> 10));

			uint32 peakSamples = 0;
			while(peakSamples < toRead) {
				if (peakLeft == 0) {
					if (peakTotal) {
						const float scale = 1.0f / 32767.0f * 127.0f / 255.0f;

						const uint8 vR0 = VDClampedRoundFixedToUint8Fast(peakValAccumR0 * scale + 128.0f / 255.0f);
						const uint8 vR1 = VDClampedRoundFixedToUint8Fast(peakValAccumR1 * scale + 128.0f / 255.0f);
						mPeakMaps[0].push_back(vR0);
						mPeakMaps[0].push_back(vR1);
						peakValAccumR0 = 0;
						peakValAccumR1 = 0;

						if (wf.mChannels > 1) {
							const uint8 vL0 = VDClampedRoundFixedToUint8Fast(peakValAccumL0 * scale + 128.0f / 255.0f);
							const uint8 vL1 = VDClampedRoundFixedToUint8Fast(peakValAccumL1 * scale + 128.0f / 255.0f);
							mPeakMaps[1].push_back(vL0);
							mPeakMaps[1].push_back(vL1);
							peakValAccumL0 = 0;
							peakValAccumL1 = 0;
						}
					}

					peakTotal = (UINT32_C(0xFFFFFFFF) - peakMapAccum) / peakMapAccumInc + 1;
					peakLeft = peakTotal;
					peakMapAccum += peakMapAccumInc * peakTotal;
				}

				// accumulate peak map samples
				uint32 toPeakScan = toRead - peakSamples;
				if (toPeakScan > peakLeft)
					toPeakScan = peakLeft;

				minMax16x2(&inputBuffer[peakSamples][0], toPeakScan, peakValAccumL0, peakValAccumL1, peakValAccumR0, peakValAccumR1);

				peakLeft -= toPeakScan;
				peakSamples += toPeakScan;
			}

			inputBufferLevel += toRead;

			// resampAccum + resampStep*(count - 1) < ((inputBufferLevel - 7) << 32)
			// count <= (((inputBufferLevel - 7) << 32) - resampAccum) / resampStep
			sint32 resampCount = (sint32)((sint64)(((uint64)(inputBufferLevel - 7) << 32) - resampAccum) / resampStep);

			if (!resampCount)
				break;

			resampAccum = resample16x2(outputBuffer[0], inputBuffer[0], resampCount, resampAccum, resampStep);

			uint32 shift = (uint32)(resampAccum >> 32);
			if (shift) {
				memmove(inputBuffer[0], inputBuffer[shift], (inputBufferLevel - shift)*sizeof(inputBuffer[0]));
				inputBufferLevel -= shift;
				resampAccum -= (uint64)shift << 32;
			}

			outputBufferIdx = 0;
			outputBufferLevel = resampCount;
		}

		int ix = outputBuffer[outputBufferIdx][1];

		float x = (float)ix;
		const bool outputBit = decoder.Advance(x);

		bitAccum += outputBit;

		if (++bitTimer >= kATCassetteAudioSamplesPerDataSample) {
			bitTimer = 0;

			outAccum += outAccum;

			if (bitAccum >= 4)
				++outAccum;

			bitAccum = 0;

			if (++outAccumBits >= 32) {
				outAccumBits = 0;
				
				pDataBlock->mData.push_back(outAccum);
			}
		}

		pAudioBlock->mAudio.push_back((outputBuffer[outputBufferIdx][0] >> 8) + 0x80);

		++outputBufferIdx;
	}

	mDataLength = pDataBlock->mDataLength = ((uint32)pDataBlock->mData.size() << 5) + outAccumBits;
	mAudioLength = pAudioBlock->mAudioLength = (uint32)pAudioBlock->mAudio.size();

	if (outAccumBits)
		pDataBlock->mData.push_back(outAccum << (32 - outAccumBits));

	mDataBlocks.resize(2);
	mDataBlocks[0].mStart = 0;
	mDataBlocks[0].mOffset = 0;
	mDataBlocks[0].mpImageBlock = pDataBlock;
	mDataBlocks[1].mStart = mDataLength;
	mDataBlocks[1].mOffset = 0;
	mDataBlocks[1].mpImageBlock = NULL;
	mDataBlockCount = 1;

	mAudioBlocks.resize(2);
	mAudioBlocks[0].mStart = 0;
	mAudioBlocks[0].mOffset = 0;
	mAudioBlocks[0].mpImageBlock = pAudioBlock;
	mAudioBlocks[1].mStart = mAudioLength;
	mAudioBlocks[1].mOffset = 0;
	mAudioBlocks[1].mpImageBlock = NULL;
	mAudioBlockCount = 1;
}

void ATCassetteImage::ParseCAS(IVDRandomAccessStream& file, bool loadDataAsAudio) {
	uint32 baudRate = 600;
	uint8 buf[128];

	ATProgress progress;
	progress.InitF((uint32)((uint64)file.Length() >> 10), L"Processing %uK of %uK", L"Processing CAS file");

	mDataLength = 0;

	vdfastvector<ATCassetteImageDataBlockStd *> dataBlocks;

	bool lastIsBlank = false;
	bool lastIsFSK = false;
	bool lastIsData = false;
	uint32 lastBaudRate = 0;

	auto addGap = [&,this](float seconds) {
		const uint32 samples = (uint32)(kATCassetteDataSampleRate * seconds);

		if (!samples)
			return;

		if (!lastIsBlank) {
			lastIsBlank = true;
			lastIsFSK = false;
			lastIsData = false;

			mImageBlocks.push_back(new ATCassetteImageBlockBlank);
			mDataBlocks.push_back({ mDataLength, 0, mImageBlocks.back() });
		}

		mDataLength += samples;
	};

	// enforce at least a 10 second mark tone at beginning of tape
	float minGap = 10.0f;

	for(;;) {
		progress.Update((uint32)((uint64)file.Pos() >> 10));

		struct {
			uint32 id;
			uint16 len;
			uint8 aux1;
			uint8 aux2;
		} hdr;

		if (file.ReadData(&hdr, 8) != 8)
			break;

		uint32 len = VDFromLE16(hdr.len);

		switch(hdr.id) {
			case VDMAKEFOURCC('F', 'U', 'J', 'I'):
				break;

			case VDMAKEFOURCC('b', 'a', 'u', 'd'):
				baudRate = hdr.aux1 + ((uint32)hdr.aux2 << 8);

				if (!baudRate)
					throw MyError("The cassette image contains an invalid baud rate in the data block at offset %lld.", file.Pos() - 8);

				break;

			case VDMAKEFOURCC('d', 'a', 't', 'a'):{
				// encode inter-record gap
				const sint32 gapms = hdr.aux1 + ((uint32)hdr.aux2 << 8);

				if (g_ATLCCasImage.IsEnabled()) {
					float pos = (float)mDataLength / (float)kATCassetteDataSampleRate;
					int mins = (int)(pos / 60.0f);
					float secs = pos - (float)mins * 60.0f;

					g_ATLCCasImage("Data block @ %3d:%06.3f: %ums gap, %u data bytes @ %u baud\n", mins, secs, gapms, len, baudRate);
				}

				addGap(std::max<float>(minGap, (float)gapms / 1000.0f));
				minGap = 0;

				// encode data bytes
				if (len > 0) {
					ATCassetteImageDataBlockStd *dataBlock = nullptr;
					
					if (lastIsData && lastBaudRate == baudRate)
						dataBlock = static_cast<ATCassetteImageDataBlockStd *>(mDataBlocks.back().mpImageBlock);

					if (!dataBlock) {
						dataBlock = new ATCassetteImageDataBlockStd;
						mImageBlocks.push_back(dataBlock);
						mDataBlocks.push_back({ mDataLength, 0, dataBlock });
						dataBlocks.push_back(dataBlock);
						dataBlock->Init(baudRate);

						lastBaudRate = baudRate;
						lastIsBlank = false;
						lastIsData = true;
						lastIsFSK = false;
					}

					while(len > 0) {
						uint32 tc = sizeof(buf);
						if (tc > len)
							tc = len;

						file.Read(buf, tc);

						dataBlock->AddData(buf, tc);

						len -= tc;
					}

					uint64 newDataLength = mDataBlocks.back().mStart + dataBlock->GetDataSampleCount64();

					if (newDataLength > kATCassetteDataLimit)
						throw ATCassetteTooLongException();

					mDataLength = (uint32)newDataLength;
				}
				break;
			}

			case VDMAKEFOURCC('f', 's', 'k', ' '):{
				// length must be even or chunk is malformed
				if (len & 1)
					throw MyError("Broken FSK chunk found at offset %lld.", file.Pos() - 8);

				const sint32 gapms = hdr.aux1 + ((uint32)hdr.aux2 << 8);

				if (g_ATLCCasImage.IsEnabled()) {
					float pos = (float)mDataLength / (float)kATCassetteDataSampleRate;
					int mins = (int)(pos / 60.0f);
					float secs = pos - (float)mins * 60.0f;

					g_ATLCCasImage("FSK block @ %3d:%06.3f: %ums gap, %u data bytes @ %u baud\n", mins, secs, gapms, len, baudRate);
				}

				addGap(std::max<float>(minGap, (float)gapms / 1000.0f));
				minGap = 0;

				// encode FSK bits
				if (len > 0) {
					ATCassetteImageBlockDataFSK *fskBlock = nullptr;
					
					if (lastIsFSK)
						fskBlock = static_cast<ATCassetteImageBlockDataFSK *>(mDataBlocks.back().mpImageBlock);

					if (!fskBlock) {
						fskBlock = new ATCassetteImageBlockDataFSK;
						mImageBlocks.push_back(fskBlock);
						mDataBlocks.push_back({ mDataLength, 0, fskBlock });

						lastIsBlank = false;
						lastIsData = false;
						lastIsFSK = true;
					}

					// FSK blocks can easily exceed the data limit even within a single block, so we
					// monitor the length on the fly. Note that we might already have gone over a little
					// bit due to addGap(); we just ignore that, knowing that it'll be caught later.
					const uint32 maxBlockLen = mDataLength < kATCassetteDataLimit ? kATCassetteDataLimit - mDataLength : 0;
					bool polarity = false;

					while(len > 0) {
						uint16 rawPulseWidth;
						file.Read(&rawPulseWidth, 2);

						fskBlock->AddPulse(polarity, VDFromLE16(rawPulseWidth));

						if (fskBlock->GetDataSampleCount() > maxBlockLen)
							throw ATCassetteTooLongException();

						polarity = !polarity;
						len -= 2;
					}

					mDataLength = mDataBlocks.back().mStart + fskBlock->GetDataSampleCount();
				}

				break;
			}

			case VDMAKEFOURCC('p', 'w', 'm', 'c'):{
				break;
			}

			case VDMAKEFOURCC('p', 'w', 'm', 'd'):
			case VDMAKEFOURCC('p', 'w', 'm', 'l'):
				throw MyError("Cannot load tape: turbo encoded (PWM) data exists in image.");
		}

		file.Seek(file.Pos() + len);
	}

	// add two second footer
	addGap(2.0f);

	// final size check
	if (mDataLength > kATCassetteDataLimit)
		throw ATCassetteTooLongException();

	// set up data blocks
	mDataBlockCount = (uint32)mDataBlocks.size();
	mDataBlocks.push_back( { mDataLength, 0, nullptr } );

	ConvertDataToPeaks();

	// set up audio blocks
	if (loadDataAsAudio) {
		mAudioBlocks = mDataBlocks;

		for(auto& block : mAudioBlocks)
			block.mStart *= kATCassetteAudioSamplesPerDataSample;

		mAudioBlockCount = mDataBlockCount;
		mAudioLength = mDataLength * kATCassetteAudioSamplesPerDataSample;

		mPeakMaps[1] = mPeakMaps[0];
	}
}

void ATCassetteImage::ConvertDataToPeaks() {
	RefreshPeaksFromData(0, mDataLength);
}

void ATCassetteImage::RefreshPeaksFromData(uint32 startSample, uint32 endSample) {
	if (endSample > mDataLength)
		endSample = mDataLength;

	if (endSample <= startSample)
		return;

	uint32 firstPeak = startSample / kDataSamplesPerPeakSample;
	uint32 lastPeak = (endSample - 1) / kDataSamplesPerPeakSample;

	uint32 reqAlloc = (lastPeak + 1) * 2;
	if (mPeakMaps[0].size() < reqAlloc)
		mPeakMaps[0].resize(reqAlloc, 0x80);
	
	for(uint32 peak = firstPeak; peak <= lastPeak; ++peak) {
		uint32 blockStart = peak * kDataSamplesPerPeakSample;
		uint32 blockLen = std::min<uint32>(kDataSamplesPerPeakSample, mDataLength - blockStart);

		int delta = 127 - (GetBitSum(blockStart, blockLen) * 127) / blockLen;

		mPeakMaps[0][peak*2+0] = 128 - delta;
		mPeakMaps[0][peak*2+1] = 128 + delta;
	}
}

///////////////////////////////////////////////////////////////////////////

void ATCreateNewCassetteImage(IATCassetteImage **ppImage) {
	vdrefptr<ATCassetteImage> pImage(new ATCassetteImage);
	pImage->InitNew();
	*ppImage = pImage.release();
}

void ATLoadCassetteImage(IVDRandomAccessStream& file, bool loadAudioAsData, IATCassetteImage **ppImage) {
	vdrefptr<ATCassetteImage> pImage(new ATCassetteImage);

	pImage->Load(file, loadAudioAsData);

	*ppImage = pImage.release();
}

void ATSaveCassetteImageCAS(IVDRandomAccessStream& file, IATCassetteImage *image) {
	static_cast<ATCassetteImage *>(image)->SaveCAS(file);
}
