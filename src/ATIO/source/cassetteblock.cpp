//	Altirra - Atari 800/800XL/5200 emulator
//	I/O library - cassette storage block types
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

#include <stdafx.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/error.h>
#include <vd2/system/int128.h>
#include <vd2/system/math.h>
#include <vd2/system/vdstl.h>
#include <at/atio/cassetteblock.h>
#include <at/atio/cassetteimage.h>

namespace {
	constexpr uint32 kATCyclesPerSyncSample = 28;
	constexpr uint32 kAudioSamplesPerSyncSampleInt = kATCyclesPerSyncSample / kATCassetteCyclesPerAudioSample;
	constexpr uint32 kAudioSamplesPerSyncSampleFrac = kATCyclesPerSyncSample % kATCassetteCyclesPerAudioSample;
}

uint8 *ATCassetteGetAudioPhaseTable() {
	static uint8 sPhaseTable[1024];

	if (sPhaseTable[0])
		return sPhaseTable;

	for(int i=0; i<1024; ++i) {
		float t = (float)i * (nsVDMath::kfTwoPi / 1024.0f);
		sPhaseTable[i] = VDClampedRoundFixedToUint8Fast(0.5f + 0.25f * sinf(t));
	}

	return sPhaseTable;
}

///////////////////////////////////////////////////////////////////////////////

bool ATCassetteImageBlock::GetBit(uint32 pos, bool bypassFSK) const {
	return true;
}

uint32 ATCassetteImageBlock::GetBitSum(uint32 pos, uint32 n, bool bypassFSK) const {
	return n;
}

ATCassetteImageBlock::FindBitResult ATCassetteImageBlock::FindBit(uint32 pos, uint32 limit, bool polarity, bool bypassFSK) const {
	if (polarity)
		return { pos, true };
	else
		return { limit, false };
}

void ATCassetteImageBlock::GetTransitionCounts(uint32 pos, uint32 n, bool lastPolarity, bool bypassFSK, uint32& xcount, uint32& mcount) const {
	xcount = n && !lastPolarity ? 1 : 0;
	mcount = n;
}

uint32 ATCassetteImageBlock::AccumulateAudio(float *&dst, uint32& posSample, uint32& posCycle, uint32 n, float volume) const {
	dst += n;

	posCycle += n * kATCyclesPerSyncSample;
	posSample += posCycle / kATCassetteCyclesPerAudioSample;
	posCycle %= kATCassetteCyclesPerAudioSample;

	return n;
}

///////////////////////////////////////////////////////////////////////////////

void ATCassetteImageBlockRawData::AddFSKPulseSamples(bool polarity, uint32 samples) {
	if (!samples)
		return;

	if (~mDataLength < samples)
		throw MyError("Tape too long (exceeds 2^32 samples)");

	// compute new bitfield length and extend
	uint32 newLength = (mDataLength + samples + 31) >> 5;

	mDataFSK.resize(newLength, 0);
	mDataRaw.resize(newLength, 0);

	// if we're writing mark bits, set bits in the FSK bitfield
	if (polarity) {
		SetBits(true, mDataLength, samples, true);
	}

	// set bits in the raw bitfield
	static constexpr const uint64 kIncSpaceTone = (uint64)(0.5 + 5326.7 / kATCassetteDataSampleRate * 4294967296.0);
	static constexpr const uint64 kIncMarkTone = (uint64)(0.5 + 3995.0 / kATCassetteDataSampleRate * 4294967296.0);

	const uint64 phaseInc = polarity ? kIncMarkTone : kIncSpaceTone;
	uint32 *p = &mDataRaw[mDataLength >> 5];
	uint32 bit = UINT32_C(1) << (mDataLength & 31);

	for(uint32 i=0; i<samples; ++i) {
		mFSKPhaseAccum += phaseInc;

		if (mFSKPhaseAccum & 0x80000000U)
			*p |= bit;

		bit += bit;
		if (!bit) {
			bit = 1;
			++p;
		}
	}


	mDataLength += samples;
}

void ATCassetteImageBlockRawData::AddDirectPulseSamples(bool polarity, uint32 samples) {
	if (~mDataLength < samples)
		throw MyError("Tape too long (exceeds 2^32 samples)");

	// compute new bitfield length and extend
	uint32 newLength = (mDataLength + samples + 31) >> 5;

	mDataFSK.resize(newLength, 0);
	mDataRaw.resize(newLength, 0);

	// if we're writing mark bits, set bits in the direct bitfield
	if (polarity)
		SetBits(false, mDataLength, samples, true);

	// for now, just set the FSK data as mark tone
	static constexpr const uint64 kIncMarkTone = (uint64)(0.5 + 3995.0 / kATCassetteDataSampleRate * 4294967296.0);

	SetBits(true, mDataLength, samples, true);
	mFSKPhaseAccum += kIncMarkTone * samples;

	mDataLength += samples;
}

void ATCassetteImageBlockRawData::ExtractPulses(vdfastvector<uint32>& pulses, uint32 sampleOffset, uint32 maxSamples, bool bypassFSK) const {
	bool lastPolarity = false;
	uint32 pulseLen = 0;

	uint32 n = mDataLength;
	if (sampleOffset >= n)
		return;

	maxSamples = std::min<uint32>(maxSamples, n - sampleOffset);
	if (!maxSamples)
		return;

	const uint32 *VDRESTRICT bitfield = bypassFSK ? mDataRaw.data() : mDataFSK.data();
	const uint32 sampleLimit = sampleOffset + maxSamples;
	for(uint32 i=sampleOffset; i<sampleLimit; ++i) {
		const bool polarity = ((bitfield[i >> 5] << (i & 31)) & 0x80000000) != 0;

		if (lastPolarity != polarity) {
			lastPolarity = polarity;

			pulses.push_back(pulseLen);
			pulseLen = 0;
		}

		++pulseLen;
	}

	if (pulseLen)
		pulses.push_back(pulseLen);
}

bool ATCassetteImageBlockRawData::GetBit(uint32 pos, bool bypassFSK) const {
	VDASSERT(pos < mDataLength);

	const uint32 *bitfield = bypassFSK ? mDataRaw.data() : mDataFSK.data();
	return (bitfield[pos >> 5] & (1 << (~pos & 31))) != 0;
}

uint32 ATCassetteImageBlockRawData::GetBitSum(uint32 pos, uint32 n, bool bypassFSK) const {
	VDASSERT(pos <= mDataLength);

	if (mDataLength == 0)
		return 0;

	const uint32 pos2 = mDataLength - pos < n ? mDataLength - 1 : pos + n - 1;

	// 0xFFFFFFFF
	// 0xFFFFFFFE
	// 0xFFFFFFFC
	const uint32 firstWordMask = 0xFFFFFFFFU >> (pos & 31);
	const uint32 lastWordMask = 0xFFFFFFFFU << (~pos2 & 31);

	const uint32 idx1 = pos >> 5;
	const uint32 idx2 = pos2 >> 5;

	const auto& dataSource = bypassFSK ? mDataRaw : mDataFSK;

	if (idx1 == idx2) {
		return VDCountBits(dataSource[idx1] & firstWordMask & lastWordMask);
	} else {
		uint32 sum = VDCountBits(dataSource[idx1] & firstWordMask);

		for(uint32 i = idx1 + 1; i < idx2; ++i)
			sum += VDCountBits(dataSource[i]);

		sum += VDCountBits(dataSource[idx2] & lastWordMask);
		return sum;
	}
}

ATCassetteImageBlockRawData::FindBitResult ATCassetteImageBlockRawData::FindBit(uint32 pos, uint32 limit, bool polarity, bool bypassFSK) const {
	// We want to search for 1 bits, so if we are searching for 0s then we should invert
	// the bitfield.
	const uint32 polarityMask = polarity ? 0 : ~UINT32_C(0);
	
	const uint32 firstWordMask = 0xFFFFFFFFU >> (pos & 31);
	uint32 idx = pos >> 5;

	const auto& dataSource = bypassFSK ? mDataRaw : mDataFSK;

	if (uint32 firstWord = (dataSource[idx] ^ polarityMask) & firstWordMask)
		return { (idx << 5) + 31 - VDFindHighestSetBitFast(firstWord), true };

	const uint32 idxLimit = std::min<uint32>((uint32)dataSource.size(), (limit + 31) >> 5);

	while(++idx < idxLimit) {
		pos += 32;

		if (const uint32 word = dataSource[idx] ^ polarityMask)
			return { (idx << 5) + 31 - VDFindHighestSetBitFast(word), true };
	}

	return { (idx << 5), false };
}

void ATCassetteImageBlockRawData::GetTransitionCounts(uint32 pos, uint32 n, bool lastPolarity, bool bypassFSK, uint32& xcount, uint32& mcount) const {
	const uint32 pos1 = pos;
	const uint32 pos2 = pos + n - 1;
	const uint32 firstWordMask = 0xFFFFFFFFU >> (pos1 & 31);
	const uint32 lastWordMask = 0xFFFFFFFFU << (~pos2 & 31);
	const uint32 idx1 = pos1 >> 5;
	const uint32 idx2 = pos2 >> 5;

	uint32 idx = idx1;
	uint32 prevWord = lastPolarity ? ~UINT32_C(0) : 0;
	const auto& dataSource = bypassFSK ? mDataRaw : mDataFSK;

	uint32 v = (dataSource[idx] & firstWordMask) + (prevWord & ~firstWordMask);
	uint32 delta = v ^ ((v >> 1) + (prevWord << 31));
	prevWord = v;

	v &= firstWordMask;
	delta &= firstWordMask;

	uint32 msum = 0;
	uint32 xsum = 0;
	while(idx != idx2) {
		msum += VDCountBits(v);
		xsum += VDCountBits(delta);
		v = dataSource[++idx];
		delta = v ^ ((v >> 1) + (prevWord << 31));
		prevWord = v;
	}

	// do last word (which may also be the first word), omitting delta bits beyond last mask
	msum += VDCountBits(v & lastWordMask);
	xsum += VDCountBits(delta & lastWordMask);

	xcount = xsum;
	mcount = msum;
}

void ATCassetteImageBlockRawData::SetBits(bool fsk, uint32 startPos, uint32 n, bool polarity) {
	const uint32 pos1 = startPos;
	const uint32 pos2 = startPos + n - 1;
	const uint32 firstWordMask = 0xFFFFFFFFU >> (pos1 & 31);
	const uint32 lastWordMask = 0xFFFFFFFFU << (~pos2 & 31);
	const uint32 idx1 = pos1 >> 5;
	const uint32 idx2 = pos2 >> 5;

	VDASSERT(idx1 < mDataFSK.size());
	VDASSERT(idx2 < mDataFSK.size());

	auto& data = fsk ? mDataFSK : mDataRaw;
	if (idx1 == idx2) {
		data[idx1] |= firstWordMask & lastWordMask;
	} else {
		data[idx1] |= firstWordMask;

		for(uint32 i = idx1+1; i < idx2; ++i)
			data[i] = (uint32)0xFFFFFFFF;

		data[idx2] |= lastWordMask;
	}
}

///////////////////////////////////////////////////////////////////////////////

void ATCassetteImageBlockRawAudio::GetMinMax(uint32 offset, uint32 len, uint8& minVal, uint8& maxVal) const {
	uint8 minAccum = 255;
	uint8 maxAccum = 0;

	const uint8 *p = &mAudio[offset];

	while(len--) {
		uint8 v = *p++;

		if (minAccum > v)
			minAccum = v;

		if (maxAccum < v)
			maxAccum = v;
	}

	minVal = minAccum;
	maxVal = maxAccum;
}

uint32 ATCassetteImageBlockRawAudio::AccumulateAudio(float *&dst, uint32& posSample, uint32& posCycle, uint32 n, float volume) const {
	for(uint32 i = 0; i < n; ++i) {
		if (posSample >= mAudioLength)
			return i;

		const float v = (float)((int)mAudio[posSample] - 0x80) * volume;

		posSample += kAudioSamplesPerSyncSampleInt;
		posCycle += kAudioSamplesPerSyncSampleFrac;

		if (posCycle >= kATCassetteCyclesPerAudioSample) {
			posCycle -= kATCassetteCyclesPerAudioSample;
			++posSample;
		}

		*dst++ += v;
	}

	return n;
}

///////////////////////////////////////////////////////////////////////////////

ATCassetteImageDataBlockStd::ATCassetteImageDataBlockStd() {
	mPhaseSums.push_back(0);
}

uint32 ATCassetteImageDataBlockStd::EstimateNewBlockLen(uint32 bytes, uint32 baudRate) {
	if (!baudRate)
		return 0;

	uint64 dataSamplesPerByteF32 = (uint64)(kATCassetteDataSampleRate * 10.0 * 4294967296.0 / (double)baudRate + 0.5);

	return (uint32)((bytes * dataSamplesPerByteF32) >> 32);
}

void ATCassetteImageDataBlockStd::Init(uint32 baudRate) {
	mDataSamplesPerByteF32 = (uint64)(kATCassetteDataSampleRate * 10.0 * 4294967296.0 / (double)baudRate + 0.5);
	mBytesPerDataSampleF32 = (uint64)((double)baudRate / (kATCassetteDataSampleRate * 10.0) * 4294967296.0 + 0.5);
	mBytesPerCycleF32 = (uint64)(baudRate * 429496729.6 / (7159090.0f / 4.0f) + 0.5);
	mBitsPerSyncSampleF32 = (uint32)(mBytesPerCycleF32 * 10 * kATCyclesPerSyncSample);

	// The sound wave advances by 1/12th of a cycle for every sync sample. 
	const float kSyncRate = 7159090.0f / 4.0f / (float)kATCyclesPerSyncSample;
	uint64 phaseDelta = (uint64)(4294967296.0 * (1.0/12.0 - 1.0/16.0) * (kSyncRate / baudRate));

	mPhaseAddedPerOneBitLo = (uint32)phaseDelta;
	mPhaseAddedPerOneBitHi = (uint32)(phaseDelta >> 32);

	mBaudRate = baudRate;
}

void ATCassetteImageDataBlockStd::AddData(const uint8 *data, uint32 len) {
	mData.insert(mData.end(), data, data + len);

	uint8 phase = mPhaseSums.back();
	auto startPhaseSumPos = mPhaseSums.size();

	mPhaseSums.resize(startPhaseSumPos + len);

	uint8 *dst = &mPhaseSums[startPhaseSumPos];
	while(len--) {
		// Count data bits plus the stop bit.
		phase += VDCountBits8(*data++) + 1;

		if (phase >= 24)
			phase = 0;

		*dst++ = phase;
	}
}

uint32 ATCassetteImageDataBlockStd::EstimateAddData(uint32 len) const {
	if (!len)
		return 0;

	uint32 dataLen = (uint32)mData.size();

	const uint32 samples0 = (uint32)((dataLen * mDataSamplesPerByteF32) >> 32);
	const uint32 samples1 = (uint32)(((dataLen + len) * mDataSamplesPerByteF32) >> 32);

	return samples1 - samples0;
}

const uint8 *ATCassetteImageDataBlockStd::GetData() const {
	return mData.data();
}

const uint32 ATCassetteImageDataBlockStd::GetDataLen() const {
	return (uint32)mData.size();
}

uint32 ATCassetteImageDataBlockStd::GetDataSampleCount() const {
	return (uint32)((mData.size() * mDataSamplesPerByteF32) >> 32);
}

uint64 ATCassetteImageDataBlockStd::GetDataSampleCount64() const {
	return (uint64)((vduint128(mData.size()) * vduint128(mDataSamplesPerByteF32)) >> 32);
}

bool ATCassetteImageDataBlockStd::GetBit(uint32 pos, bool bypassFSK) const {
	uint64 bytePosF32 = mBytesPerDataSampleF32 * pos;

	const uint32 limit = (uint32)mData.size();
	const uint32 byteIndex = (uint32)(bytePosF32 >> 32);

	if (byteIndex >= limit)
		return true;

	const uint32 byte = (uint32)mData[byteIndex] * 2 + 0x200;
	const uint32 bit = (uint32)(((uint64)(uint32)bytePosF32 * 10) >> 32);

	return ((byte >> bit) & 1) != 0;
}

uint32 ATCassetteImageDataBlockStd::GetBitSum(uint32 pos, uint32 n, bool bypassFSK) const {
	uint32 sum = 0;

	// space - lsb -> msb - mark
	const uint32 limit = (uint32)mData.size();
	uint64 bytePosF32 = mBytesPerDataSampleF32 * pos;

	while(n--) {
		const uint32 byteIndex = (uint32)(bytePosF32 >> 32);

		if (byteIndex >= limit)
			break;

		const uint32 byte = (uint32)mData[byteIndex] * 2 + 0x200;
		const uint32 bit = (uint32)(((uint64)(uint32)bytePosF32 * 10) >> 32);

		sum += (byte >> bit) & 1;

		bytePosF32 += mBytesPerDataSampleF32;
	}

	return sum;
}

ATCassetteImageDataBlockStd::FindBitResult ATCassetteImageDataBlockStd::FindBit(uint32 pos, uint32 limit, bool polarity, bool bypassFSK) const {
	// Because std blocks consist of back-to-back bytes using standard framing, FindBit()
	// can be simplified: every byte has a 0 bit for the start bit and a 1 bit for the stop
	// bit, so we never have to go farther than 10 bits to find the desired polarity, and
	// there is never a need to involve more than one source byte. We simply ignore the
	// supplied limit, as we are allowed to return hits past limit.

	const uint64 bytePosF32 = mBytesPerDataSampleF32 * pos;
	const uint32 byteIndex = (uint32)(bytePosF32 >> 32);
	const uint32 bitIndex = (uint32)(((uint64)(uint32)bytePosF32 * 10) >> 32);
	uint32 bitStream = ((uint32)mData[byteIndex] * 2 + 0x200) >> bitIndex;

	// invert bitstream if necessary so that we are always searching for 1 bits
	if (!polarity)
		bitStream = ~bitStream;

	// check if we are already starting in a bit with the desired polarity
	if (bitStream & 1)
		return { pos, true };

	// At this point, we are always going to advance by at least one source bit, so find
	// the matching bit and then back-convert to sample position. Because we always start
	// somewhere within 10 bits of start-data(8)-stop, we will always stop somewhere within
	// those 10 bits or the start bit of the next byte.
	uint32 foundBitIndex = bitIndex + VDFindLowestSetBitFast(bitStream);
	uint64 foundBytePosF32 = ((uint64)byteIndex << 32) + ((uint64)foundBitIndex << 32) / 10;
	uint32 foundSamplePos = (foundBytePosF32 - 1) / mBytesPerDataSampleF32 + 1;

	return { foundSamplePos, (foundBytePosF32 >> 32) < mData.size() };
}

void ATCassetteImageDataBlockStd::GetTransitionCounts(uint32 pos, uint32 n, bool lastPolarity, bool bypassFSK, uint32& xcount, uint32& mcount) const {
	uint32 msum = 0;
	uint32 xsum = 0;

	// space - lsb -> msb - mark
	const uint32 limit = (uint32)mData.size();
	uint64 bytePosF32 = mBytesPerDataSampleF32 * pos;

	uint32 pbitval = lastPolarity ? 1 : 0;
	while(n--) {
		const uint32 byteIndex = (uint32)(bytePosF32 >> 32);

		if (byteIndex >= limit)
			break;

		const uint32 byte = (uint32)mData[byteIndex] * 2 + 0x200;
		const uint32 bit = (uint32)(((uint64)(uint32)bytePosF32 * 10) >> 32);
		uint32 bitval = (byte >> bit) & 1;

		msum += bitval;
		xsum += bitval ^ pbitval;
		pbitval = bitval;

		bytePosF32 += mBytesPerDataSampleF32;
	}

	xcount = xsum;
	mcount = msum;
}

uint32 ATCassetteImageDataBlockStd::AccumulateAudio(float *&dst, uint32& posSample, uint32& posCycle, uint32 n, float volume) const {
	// The good news is that we have integral number of audio samples per data bit
	// and an integral number of sync mixer samples per audio sample. The bad news
	// is that the phase has to be continuous between bits, so the starting phase
	// for each bit is dependent upon all previous bits. To simplify things, we
	// first assume that the block starts with phase 0, and then we use a precomputed
	// phase array to determine the starting phase for each byte.

	// Calculate machine cycle.
	uint32 cycle = posSample * kATCassetteCyclesPerAudioSample + posCycle;

	// Convert to data byte position (32:32).
	uint64 bytePosF32 = cycle * mBytesPerCycleF32;

	// Compute initial phase.
	uint32 byteIndex = (uint32)(bytePosF32 >> 32);
	uint64 bitPosF32 = ((uint64)(uint32)bytePosF32 * 10);
	uint32 bitIndex = (uint32)(bitPosF32 >> 32);
	
	// We can be a little bit over due to roundoff, in which case we should clamp.
	uint32 oneBits = 0;
	bool currentBit = true;

	if (byteIndex >= mData.size()) {
		oneBits = mPhaseSums.back();

		currentBit = true;
	} else {
		oneBits = mPhaseSums[byteIndex];

		if (bitIndex > 1)
			oneBits += VDCountBits8(mData[byteIndex] & ((1 << bitIndex) - 1));

		currentBit = ((mData[byteIndex] >> bitIndex) & 1) != 0;
	}

	// Zero bits output 16 clocks per cycle, while one bits output 12 clocks per
	// cycle. We accumulate phase at 1/16 per cycle based on bit position alone
	// and then add (1/12-1/16 = 1/48) for every 'one' bit.
	const uint32 kPhasePerSyncSample = 0x10000000;
	const uint32 kPhasePerCycle = 0x10000000 / kATCyclesPerSyncSample;
	uint32 phaseAccum = cycle * kPhasePerCycle + oneBits * mPhaseAddedPerOneBitLo;

	// If we're in a 'one' bit, interpolate phase.
	if (currentBit) {
		const uint32 oneBitFraction = (uint32)bitPosF32;

		phaseAccum += mPhaseAddedPerOneBitHi * oneBitFraction;
		phaseAccum += (uint32)(((uint64)mPhaseAddedPerOneBitLo * oneBitFraction) >> 32);
	}

	const uint8 *VDRESTRICT const phaseTable = ATCassetteGetAudioPhaseTable();
	uint32 bitAccum = (uint32)bitPosF32;
	uint32 actual = 0;

	for(;;) {
		// Write out a sample.
		const float sample = (float)((int)phaseTable[phaseAccum >> 22] - 0x80) * volume;

		*dst++ += sample;
		++actual;

		if (!--n)
			break;

		// Advance.
		uint32 newBitAccum = bitAccum + mBitsPerSyncSampleF32;
		uint32 oneBitTime = 0;

		if (newBitAccum < bitAccum) {
			if (currentBit)
				oneBitTime -= bitAccum;

			if (++bitIndex >= 10) {
				bitIndex = 0;
				++byteIndex;
			}

			if (byteIndex >= mData.size())
				currentBit = true;
			else
				currentBit = (((mData[byteIndex] * 2 + 0x200) >> bitIndex) & 1) != 0;

			if (currentBit)
				oneBitTime += newBitAccum;
		} else {
			if (currentBit)
				oneBitTime = newBitAccum - bitAccum;
		}

		phaseAccum += mPhaseAddedPerOneBitHi * oneBitTime;
		phaseAccum += (uint32)(((uint64)mPhaseAddedPerOneBitLo * oneBitTime) >> 32);

		bitAccum = newBitAccum;

		phaseAccum += kPhasePerSyncSample;
	}

	posCycle += actual * kATCyclesPerSyncSample;
	posSample += posCycle / kATCassetteCyclesPerAudioSample;
	posCycle %= kATCassetteCyclesPerAudioSample;

	return actual;
}

///////////////////////////////////////////////////////////////////////////////

uint32 ATCassetteImageBlockBlank::AccumulateAudio(float *&dst, uint32& posSample, uint32& posCycle, uint32 n, float volume) const {
	// The good news is that we have integral number of audio samples per data bit
	// and an integral number of sync mixer samples per audio sample. The bad news
	// is that the phase has to be continuous between bits, so the starting phase
	// for each bit is dependent upon all previous bits. To simplify things, we
	// first assume that the block starts with phase 0, and then we use a precomputed
	// phase array to determine the starting phase for each byte.

	// Calculate machine cycle.
	uint32 cycle = posSample * kATCassetteCyclesPerAudioSample + posCycle;

	// Zero bits output 16 clocks per cycle, while one bits output 12 clocks per
	// cycle.
	const uint32 kPhasePerSyncSample = 0x15555555;
	const uint32 kPhasePerCycle = kPhasePerSyncSample / kATCyclesPerSyncSample;
	uint32 phaseAccum = cycle * kPhasePerCycle;

	const uint8 *VDRESTRICT const phaseTable = ATCassetteGetAudioPhaseTable();

	for(uint32 i=0; i<n; ++i) {
		// Write out a sample.
		const float sample = (float)((int)phaseTable[phaseAccum >> 22] - 0x80) * volume;

		*dst++ += sample;

		// Advance.
		phaseAccum += kPhasePerSyncSample;
	}

	posCycle += n * kATCyclesPerSyncSample;
	posSample += posCycle / kATCassetteCyclesPerAudioSample;
	posCycle %= kATCassetteCyclesPerAudioSample;

	return n;
}
