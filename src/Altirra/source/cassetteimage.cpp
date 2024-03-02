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
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/math.h>
#include <vd2/system/refcount.h>
#include <vd2/Riza/audioformat.h>
#include "cassetteimage.h"
#include "uiprogress.h"
#include "debuggerlog.h"

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

	extern "C" __declspec(align(16)) const sint16 kernel[32][8] = {
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

	uint64 resample16x2(sint16 *d, const sint16 *s, uint32 count, uint64 accum, sint64 inc) {
		do {
			const sint16 *s2 = s + (uint32)(accum >> 32)*2;
			const sint16 *f = kernel[(uint32)accum >> 27];

			accum += inc;

			uint32 l= (sint32)s2[ 0]*(sint32)f[0]
					+ (sint32)s2[ 2]*(sint32)f[1]
					+ (sint32)s2[ 4]*(sint32)f[2]
					+ (sint32)s2[ 6]*(sint32)f[3]
					+ (sint32)s2[ 8]*(sint32)f[4]
					+ (sint32)s2[10]*(sint32)f[5]
					+ (sint32)s2[12]*(sint32)f[6]
					+ (sint32)s2[14]*(sint32)f[7]
					+ 0x20002000;

			uint32 r= (sint32)s2[ 1]*(sint32)f[0]
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

			if (l >= 0x10000)
				l = ~l >> 31;
			if (r >= 0x10000)
				r = ~r >> 31;

			d[0] = (sint16)(l - 0x8000);
			d[1] = (sint16)(r - 0x8000);
			d += 2;
		} while(--count);

		return accum;
	}
}

///////////////////////////////////////////////////////////////////////////

class ATCassetteDecoderFSK {
public:
	ATCassetteDecoderFSK();

	void Reset();
	bool Advance(float x);

protected:
	float mHistory[64];
	float mHistorySum3[64];
	float mHistorySum4[64];
	int mIndex;
};

ATCassetteDecoderFSK::ATCassetteDecoderFSK() {
	Reset();
}

void ATCassetteDecoderFSK::Reset() {
	mIndex = 0;
	memset(mHistory, 0, sizeof mHistory);
	memset(mHistorySum3, 0, sizeof mHistorySum3);
	memset(mHistorySum4, 0, sizeof mHistorySum4);
}

bool ATCassetteDecoderFSK::Advance(float x) {
	int base = mIndex++ & 31;
	int write = (base + 31) & 31;

	mHistory[write] = mHistory[write + 32] = x;

	// 3995Hz filter has an 8 cycle period.
	// 5327Hz filter has a 6 cycle period.

#if 0
	const float sin0_12 = 0.0f;
	const float sin1_12 = 0.5f;
	const float sin2_12 = 0.86602540378443864676372317075294f;
	const float sin3_12 = 1.0f;
	const float sin4_12 = 0.86602540378443864676372317075294f;
	const float sin5_12 = 0.5f;
	const float sin6_12 = 0.0f;
	const float sin7_12 = -0.5f;
	const float sin8_12 = -0.86602540378443864676372317075294f;
	const float sin9_12 = -1.0f;
	const float sin10_12 = -0.86602540378443864676372317075294f;
	const float sin11_12 = -0.5f;

	const float sin0_8 = 0.0f;
	const float sin1_8 = 0.707107f;
	const float sin2_8 = 1.0f;
	const float sin3_8 = 0.707107f;
	const float sin4_8 = 0.0f;
	const float sin5_8 = -0.707107f;
	const float sin6_8 = -1.0f;
	const float sin7_8 = -0.707107f;

	const float *xs = &mHistory[base];
	float x0 = xs[0] + xs[ 8] + xs[16];
	float x1 = xs[1] + xs[ 9] + xs[17];
	float x2 = xs[2] + xs[10] + xs[18];
	float x3 = xs[3] + xs[11] + xs[19];
	float x4 = xs[4] + xs[12] + xs[20];
	float x5 = xs[5] + xs[13] + xs[21];
	float x6 = xs[6] + xs[14] + xs[22];
	float x7 = xs[7] + xs[15] + xs[23];

	float zero_s = x0*sin0_8 + x1*sin1_8 + x2*sin2_8 + x3*sin3_8 + x4*sin4_8 + x5*sin5_8 + x6*sin6_8 + x7*sin7_8;
	float zero_c = x0*sin2_8 + x1*sin3_8 + x2*sin4_8 + x3*sin5_8 + x4*sin6_8 + x5*sin7_8 + x6*sin0_8 + x7*sin1_8;

	float y1 = xs[0] + xs[ 6] + xs[12] + xs[18];
	float y2 = xs[1] + xs[ 7] + xs[13] + xs[19];
	float y3 = xs[2] + xs[ 8] + xs[14] + xs[20];
	float y4 = xs[3] + xs[ 9] + xs[15] + xs[21];
	float y5 = xs[4] + xs[10] + xs[16] + xs[22];
	float y6 = xs[5] + xs[11] + xs[17] + xs[23];

	float one_s = y1*sin0_12 + y2*sin2_12 + y3*sin4_12 + y4*sin6_12 + y5*sin8_12 + y6*sin10_12;
	float one_c = y1*sin3_12 + y2*sin5_12 + y3*sin7_12 + y4*sin9_12 + y5*sin11_12 + y6*sin1_12;
#else
	const float *xs = &mHistory[base + 8];
	mHistorySum3[write] = mHistorySum3[write + 32] = xs[7] + xs[15] + xs[23];
	mHistorySum4[write] = mHistorySum4[write + 32] = xs[5] + xs[11] + xs[17] + xs[23];

	const float *s3 = &mHistorySum3[base + 32 - 8];
	const float *s4 = &mHistorySum4[base + 32 - 6];

	const float x0 = s3[0];
	const float x1 = s3[1];
	const float x2 = s3[2];
	const float x3 = s3[3];
	const float x4 = s3[4];
	const float x5 = s3[5];
	const float x6 = s3[6];
	const float x7 = s3[7];

	const float sqrt2 = 0.707107f;
	float x1mx5 = x1 - x5;
	float x3mx7 = x3 - x7;
	float zero_s = (x2 - x6) + (x1mx5 + x3mx7)*sqrt2;
	float zero_c = (x0 - x4) + (x1mx5 - x3mx7)*sqrt2;

	const float y1 = s4[0];
	const float y2 = s4[1];
	const float y3 = s4[2];
	const float y4 = s4[3];
	const float y5 = s4[4];
	const float y6 = s4[5];

	const float half = 0.5f;
	const float sin2_12 = 0.86602540378443864676372317075294f;

	float y2my5 = y2 - y5;
	float y3my6 = y3 - y6;
	float one_s = (y2my5 + y3my6)*sin2_12;
	float one_c = (y1 - y4) + (y2my5 - y3my6)*half;
#endif

	float one = (one_s * one_s + one_c * one_c) * ((8.0f / 6.0f) * (8.0f / 6.0f));
	float zero = (zero_s * zero_s + zero_c * zero_c);

	return one > zero;
}

///////////////////////////////////////////////////////////////////////////////

class ATCassetteDecoderDirect {
public:
	ATCassetteDecoderDirect();

	void Reset();
	bool Advance(float x);

protected:
	float mZeroLevel;
	float mOneLevel;
	bool mbCurrentState;
};

ATCassetteDecoderDirect::ATCassetteDecoderDirect() {
	Reset();
}

void ATCassetteDecoderDirect::Reset() {
	mZeroLevel = 0.0f;
	mOneLevel = 1.0f;
}

bool ATCassetteDecoderDirect::Advance(float x) {
	float range = mOneLevel - mZeroLevel;
	float oneThird = mZeroLevel + range * (1.0f / 3.0f);
	float twoThirds = mZeroLevel + range * (2.0f / 3.0f);

	if (x < oneThird) {
		mbCurrentState = false;

		mZeroLevel += (x - mZeroLevel) * 0.95f;
	} else if (x > twoThirds) {
		mbCurrentState = true;

		mOneLevel += (x - mOneLevel) * 0.95f;
	}

	return mbCurrentState;
}

///////////////////////////////////////////////////////////////////////////////

class ATCassetteImageBlock {
public:
	virtual ~ATCassetteImageBlock() {}

	virtual uint32 GetBitSum(uint32 pos, uint32 n) const;
	virtual uint32 AccumulateAudio(float *&dstLeft, float *&dstRight, uint32& posSample, uint32& posCycle, uint32 n) const;
};

uint32 ATCassetteImageBlock::GetBitSum(uint32 pos, uint32 n) const {
	return n;
}

uint32 ATCassetteImageBlock::AccumulateAudio(float *&dstLeft, float *&dstRight, uint32& posSample, uint32& posCycle, uint32 n) const {
	return n;
}

///////////////////////////////////////////////////////////////////////////////

class ATCassetteImageDataBlockFSK : public ATCassetteImageBlock {
public:
	virtual uint32 GetBitSum(uint32 pos, uint32 n) const;

	uint32 mDataLength;
	vdfastvector<uint32> mData;
};

uint32 ATCassetteImageDataBlockFSK::GetBitSum(uint32 pos, uint32 n) const {
	const uint32 pos2 = mDataLength - pos < n ? mDataLength - 1 : pos + n - 1;

	// 0xFFFFFFFF
	// 0xFFFFFFFE
	// 0xFFFFFFFC
	const uint32 firstWordMask = 0xFFFFFFFFU >> (pos & 31);
	const uint32 lastWordMask = 0xFFFFFFFFU << (~pos2 & 31);

	const uint32 idx1 = pos >> 5;
	const uint32 idx2 = pos2 >> 5;

	if (idx1 == idx2) {
		return VDCountBits(mData[idx1] & firstWordMask & lastWordMask);
	} else {
		uint32 sum = VDCountBits(mData[idx1] & firstWordMask);

		for(uint32 i = idx1 + 1; i < idx2; ++i)
			sum += VDCountBits(mData[i]);

		sum += VDCountBits(mData[idx2] & lastWordMask);
		return sum;
	}
}

///////////////////////////////////////////////////////////////////////////////

class ATCassetteImageAudioBlockRaw : public ATCassetteImageBlock {
public:
	uint32 AccumulateAudio(float *&dstLeft, float *&dstRight, uint32& posSample, uint32& posCycle, uint32 n) const;

	vdfastvector<uint8> mAudio;
	uint32 mAudioLength;
};

uint32 ATCassetteImageAudioBlockRaw::AccumulateAudio(float *&dstLeft, float *&dstRight, uint32& posSample, uint32& posCycle, uint32 n) const {
	for(uint32 i = 0; i < n; ++i) {
		const float v = (float)((int)mAudio[posSample] - 0x80) * (1.0f / 8.0f) * (float)kATCyclesPerSyncSample;

		posSample += kAudioSamplesPerSyncSampleInt;
		posCycle += kAudioSamplesPerSyncSampleFrac;

		if (posCycle >= kClockCyclesPerAudioSample) {
			posCycle -= kClockCyclesPerAudioSample;
			++posSample;
		}

		if (posSample >= mAudioLength)
			return i;

		*dstLeft++ += v;

		if (dstRight)
			*dstRight++ += v;
	}

	return n;
}

///////////////////////////////////////////////////////////////////////////////

class ATCassetteImage : public vdrefcounted<IATCassetteImage> {
public:
	ATCassetteImage();
	~ATCassetteImage();

	uint32 GetDataLength() const { return mDataLength; }
	uint32 GetAudioLength() const { return mAudioLength; }
	bool GetBit(uint32 pos, uint32 averagingPeriod, uint32 threshold, bool prevBit) const;
	void AccumulateAudio(float *&dstLeft, float *&dstRight, uint32& posSample, uint32& posCycle, uint32 n) const;

	void Load(IVDRandomAccessStream& file, bool loadImageAsData);

protected:
	struct SortedBlock {
		uint32	mStart;
		ATCassetteImageBlock *mpImageBlock;
	};

	const SortedBlock *GetSortedDataBlock(uint32 pos) const;

	void ParseWAVE(IVDRandomAccessStream& file);
	void ParseCAS(IVDRandomAccessStream& file);
	void ConvertDataToAudio();

	uint32 mDataLength;
	uint32 mAudioLength;

	mutable const SortedBlock *mpCachedDataBlock;
	uint32 mDataBlockCount;
	uint32 mAudioBlockCount;

	typedef vdfastvector<SortedBlock> SortedBlocks;
	SortedBlocks mDataBlocks;
	SortedBlocks mAudioBlocks;

	typedef vdfastvector<ATCassetteImageBlock *> ImageBlocks;
	ImageBlocks mImageBlocks;
};

ATCassetteImage::ATCassetteImage()
	: mDataLength(0)
	, mAudioLength(0)
	, mDataBlockCount(0)
	, mAudioBlockCount(0)
{
}

ATCassetteImage::~ATCassetteImage() {
	while(!mImageBlocks.empty()) {
		ATCassetteImageBlock *p = mImageBlocks.back();

		if (p)
			delete p;

		mImageBlocks.pop_back();
	}
}

bool ATCassetteImage::GetBit(uint32 pos, uint32 averagingPeriod, uint32 threshold, bool prevBit) const {
	if (pos >= mDataLength)
		return true;

	uint32 pos1 = pos;
	uint32 halfAveragingPeriod = averagingPeriod >> 1;

	if (pos1 > halfAveragingPeriod)
		pos1 -= halfAveragingPeriod;
	else
		pos1 = 0;

	uint32 pos2 = pos1;
	if (mDataLength - pos2 > averagingPeriod)
		pos2 += averagingPeriod;
	else
		pos2 = mDataLength - 1;

	const SortedBlock *p = GetSortedDataBlock(pos);
	if (!p)
		return true;

	const uint32 sum = p->mpImageBlock->GetBitSum(pos - p->mStart, averagingPeriod);

	if (sum < threshold)
		return false;
	else if (sum > averagingPeriod - threshold)
		return true;
	else
		return prevBit;
}

void ATCassetteImage::AccumulateAudio(float *&dstLeft, float *&dstRight, uint32& posSample, uint32& posCycle, uint32 n) const {
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

	while(n && p->mpImageBlock) {
		posSample -= p->mStart;
		n -= p->mpImageBlock->AccumulateAudio(dstLeft, dstRight, posSample, posCycle, n);
		posSample += p->mStart;

		++p;
	}
}

void ATCassetteImage::Load(IVDRandomAccessStream& file, bool loadDataAsAudio) {
	uint32 basehdr;
	if (file.ReadData(&basehdr, 4) != 4)
		basehdr = 0;

	file.Seek(0);

	uint32 baseid = VDFromLE32(basehdr);
	if (baseid == VDMAKEFOURCC('R', 'I', 'F', 'F'))
		ParseWAVE(file);
	else if (baseid == VDMAKEFOURCC('F', 'U', 'J', 'I'))
		ParseCAS(file);
	else
		throw MyError("%ls is not in a recognizable Atari cassette format.", file.GetNameForError());

	mpCachedDataBlock = &mDataBlocks.front();

	if (mAudioBlocks.empty() && loadDataAsAudio)
		ConvertDataToAudio();
}

const ATCassetteImage::SortedBlock *ATCassetteImage::GetSortedDataBlock(uint32 pos) const {
	uint32 i = 0;
	uint32 j = mDataBlockCount;

	if (pos < mpCachedDataBlock[0].mStart)
		j = (uint32)(mpCachedDataBlock - mDataBlocks.data());
	else if (pos >= mpCachedDataBlock[1].mStart)
		i = (uint32)((mpCachedDataBlock + 1) - mDataBlocks.data());
	else
		return mpCachedDataBlock;

	for(;;) {
		uint32 mid = (i + j) >> 1;
		const SortedBlock *p = &mDataBlocks[mid];

		if (i + 1 >= j) {
			if (!p->mpImageBlock)
				return NULL;

			mpCachedDataBlock = p;
			return p;
		}

		if (pos < p->mStart)
			j = mid;
		else
			i = mid;
	}
}

void ATCassetteImage::ParseWAVE(IVDRandomAccessStream& file) {
	WaveFormatEx wf = {0};
	sint64 limit = file.Length();
	sint64 datapos = -1;
	uint32 datalen = 0;

	ATUIProgress progress;

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
	uint64	resampStep = VDRoundToInt64(wf.mSamplesPerSec / kAudioFrequency * 4294967296.0f);

	sint16	inputBuffer[512][2] = {0};
	uint32	inputBufferLevel = 3;

	sint16	outputBuffer[4096][2] = {0};
	uint32	outputBufferIdx = 0;
	uint32	outputBufferLevel = 0;

	uint32	inputSamplesLeft = datalen / wf.mBlockAlign;

	file.Seek(datapos);

	bool outputBit = false;
	bool lastBit = false;
	bool dataPhase = false;
	int bitTimer = 0;
	uint8 bitCounter = 0;
	int bitAccum = 0;

	mImageBlocks.resize(2, (ATCassetteImageBlock *)NULL);

	ATCassetteImageDataBlockFSK *pDataBlock = new ATCassetteImageDataBlockFSK;
	mImageBlocks[0] = pDataBlock;

	ATCassetteImageAudioBlockRaw *pAudioBlock = new ATCassetteImageAudioBlockRaw;
	mImageBlocks[1] = pAudioBlock;

	progress.InitF((uint32)((uint64)datalen >> 10), L"Processed %uK / %uK", L"Processing raw waveform");
	sint64 progressValue = 0;

	uint32 outAccum = 0;
	uint32 outAccumBits = 0;
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

		if (++bitTimer >= kAudioSamplesPerDataBit) {
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

	if (outAccumBits)
		pDataBlock->mData.push_back(outAccum << (32 - outAccumBits));

	mDataLength = pDataBlock->mDataLength = ((uint32)pDataBlock->mData.size() << 5) + outAccumBits;
	mAudioLength = pAudioBlock->mAudioLength = pAudioBlock->mAudio.size();

	mDataBlocks.resize(2);
	mDataBlocks[0].mStart = 0;
	mDataBlocks[0].mpImageBlock = pDataBlock;
	mDataBlocks[1].mStart = mDataLength;
	mDataBlocks[1].mpImageBlock = NULL;
	mDataBlockCount = 1;

	mAudioBlocks.resize(2);
	mAudioBlocks[0].mStart = 0;
	mAudioBlocks[0].mpImageBlock = pAudioBlock;
	mAudioBlocks[1].mStart = mAudioLength;
	mAudioBlocks[1].mpImageBlock = NULL;
	mAudioBlockCount = 1;
}

namespace {
	class ATCassetteEncoder {
	public:
		typedef vdfastvector<uint32> Bitstream;

		ATCassetteEncoder(Bitstream& bs);

		void SetBaudRate(uint32 baud);
		void EncodeTone(bool isMark, float seconds);
		void EncodeByte(uint8 c);
		void EncodeBytes(const uint8 *p, uint32 len);

		uint32 Flush();

	protected:
		Bitstream& mBitstream;
		uint32	mSamplesPerBitF12;
		uint32	mSampleAccumF12;
		uint8	mAccumCounter;
		uint32	mAccum;
	};

	ATCassetteEncoder::ATCassetteEncoder(Bitstream& bs)
		: mBitstream(bs)
		, mSamplesPerBitF12(0)
		, mSampleAccumF12(0)
		, mAccumCounter(0)
		, mAccum(0)
	{
		SetBaudRate(600);
	}

	void ATCassetteEncoder::SetBaudRate(uint32 baud) {
		// [samples/bit] = [samples/second] / [bits/second]
		mSamplesPerBitF12 = VDRoundToInt(kDataFrequency * 4096.0f / (float)baud);
	}

	void ATCassetteEncoder::EncodeTone(bool isMark, float seconds) {
		sint32 delaySamples = VDRoundToInt32(seconds * kDataFrequency);

		const uint32 addend = isMark ? 1 : 0;
		while(delaySamples > 0) {
			mAccum += mAccum;
			mAccum += addend;

			mAccumCounter += 0x08;
			if (!mAccumCounter)
				mBitstream.push_back(mAccum);

			--delaySamples;
		}
	}

	void ATCassetteEncoder::EncodeByte(uint8 c) {
		EncodeBytes(&c, 1);
	}

	void ATCassetteEncoder::EncodeBytes(const uint8 *p, uint32 len) {
		while(len--) {
			// data bytes consist of a space, followed by bits from LSB to MSB, followed by a mark
			uint32 bits = ((uint32)*p++ << 1) | 0x200;

			// encode 10 bits starting from LSB
			for(uint32 i=0; i<10; ++i) {
				// compute cycle delta for this bit
				mSampleAccumF12 += mSamplesPerBitF12;
				uint32 bitCount = mSampleAccumF12 >> 12;
				mSampleAccumF12 &= 0xfff;

				// encode a bit
				const uint32 addend = (bits & 1);
				bits >>= 1;

				while(bitCount--) {
					mAccum += mAccum;
					mAccum += addend;

					mAccumCounter += 0x08;
					if (!mAccumCounter)
						mBitstream.push_back(mAccum);
				}
			}
		}
	}

	uint32 ATCassetteEncoder::Flush() {
		uint32 extraBits = mAccumCounter >> 3;
		uint32 outputCount = ((uint32)mBitstream.size() << 5) + extraBits;

		if (extraBits) {
			mAccum <<= (32 - extraBits);
			mBitstream.push_back(mAccum);
		}

		return outputCount;
	}
}

void ATCassetteImage::ParseCAS(IVDRandomAccessStream& file) {
	mImageBlocks.push_back(NULL);
	ATCassetteImageDataBlockFSK *pDataBlock = new ATCassetteImageDataBlockFSK;
	mImageBlocks[0] = pDataBlock;

	ATCassetteEncoder enc(pDataBlock->mData);
	uint32 baudRate = 600;
	uint32 currentCycle = 0;
	uint8 buf[128];

	ATUIProgress progress;
	progress.InitF((uint32)((uint64)file.Length() >> 10), L"Processing %uK of %uK", L"Processing CAS file");

	// insert 10 second mark tone (normally 20s)
	enc.EncodeTone(true, 10.0f);

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

				enc.SetBaudRate(baudRate);
				break;

			case VDMAKEFOURCC('d', 'a', 't', 'a'):{
				// encode inter-record gap

				//VDDEBUG("Starting IRG at position %u\n", (unsigned)mBitstream.size());
				//VDDEBUG("Starting data at position %u\n", (unsigned)mBitstream.size());

				const sint32 gapms = hdr.aux1 + ((uint32)hdr.aux2 << 8);

				if (g_ATLCCasImage.IsEnabled()) {
					float pos = (float)pDataBlock->mData.size() * 32.0f / (float)kDataFrequency;
					int mins = (int)(pos / 60.0f);
					float secs = pos - (float)mins * 60.0f;

					g_ATLCCasImage("Data block @ %3d:%06.3f: %ums gap, %u data bytes @ %u baud\n", mins, secs, gapms, len, baudRate);
				}

				enc.EncodeTone(true, (float)gapms / 1000.0f);

				// encode data bytes
				while(len > 0) {
					uint32 tc = sizeof(buf);
					if (tc > len)
						tc = len;

					file.Read(buf, tc);
					enc.EncodeBytes(buf, tc);
					len -= tc;
				}
				break;
			}
		}

		file.Seek(file.Pos() + len);
	}

	// add two second footer
	enc.EncodeTone(true, 2.0f);

	// set up data blocks
	pDataBlock->mDataLength = enc.Flush();
	mDataLength = pDataBlock->mDataLength;
	mDataBlockCount = 1;
	mDataBlocks.resize(2);
	mDataBlocks[0].mStart = 0;
	mDataBlocks[0].mpImageBlock = pDataBlock;
	mDataBlocks[1].mStart = mDataLength;
	mDataBlocks[1].mpImageBlock = NULL;
}

void ATCassetteImage::ConvertDataToAudio() {
	vdblock<uint8> phaseTable(1024);

	for(int i=0; i<1024; ++i) {
		float t = (float)i * (nsVDMath::kfTwoPi / 1024.0f);
		phaseTable[i] = VDClampedRoundFixedToUint8Fast(0.5f + 0.25f * sinf(t));
	}

	uint32 dataLength = mDataLength;

	if (dataLength < 1)
		return;

	const uint32 kPhaseIncOne = (uint32)(0.5 + 5327.0f / kAudioFrequency * 1024.0f);
	const uint32 kPhaseIncZero = (uint32)(0.5 + 3995.0f / kAudioFrequency * 1024.0f);

	mImageBlocks.push_back(NULL);
	ATCassetteImageAudioBlockRaw *pAudioBlock = new ATCassetteImageAudioBlockRaw;
	mImageBlocks.back() = pAudioBlock;

	mAudioLength = dataLength * kAudioSamplesPerDataBit;
	pAudioBlock->mAudio.resize(mAudioLength);
	pAudioBlock->mAudioLength = mAudioLength;

	SortedBlock& sb1 = mAudioBlocks.push_back();
	sb1.mStart = 0;
	sb1.mpImageBlock = pAudioBlock;
	SortedBlock& sb2 = mAudioBlocks.push_back();
	sb2.mStart = mAudioLength;
	sb2.mpImageBlock = pAudioBlock;

	mAudioBlockCount = 1;

	uint8 *dst = pAudioBlock->mAudio.data();

	uint32 phaseAccum = 0;
	for(uint32 i=0; i<dataLength; ++i) {
		const bool bit = GetBit(i, 1, 1, false);
		const uint32 phaseInc = bit ? kPhaseIncOne : kPhaseIncZero;

		for(uint32 j=0; j<kAudioSamplesPerDataBit; ++j) {
			dst[j] = phaseTable[phaseAccum & 1023];
			phaseAccum += phaseInc;
		}

		dst += kAudioSamplesPerDataBit;
	}
}

///////////////////////////////////////////////////////////////////////////

void ATLoadCassetteImage(IVDRandomAccessStream& file, bool loadAudioAsData, IATCassetteImage **ppImage) {
	vdrefptr<ATCassetteImage> pImage(new ATCassetteImage);

	pImage->Load(file, loadAudioAsData);

	*ppImage = pImage.release();
}
