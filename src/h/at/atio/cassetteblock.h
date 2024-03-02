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

#ifndef f_AT_ATIO_CASSETTEBLOCK_H
#define f_AT_ATIO_CASSETTEBLOCK_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/refcount.h>

enum ATCassetteImageBlockType : uint8 {
	kATCassetteImageBlockType_End,
	kATCassetteImageBlockType_Blank,
	kATCassetteImageBlockType_Std,
	kATCassetteImageBlockType_FSK,
	kATCassetteImageBlockType_RawAudio
};

// Base class for all in-memory cassette image blocks.
class ATCassetteImageBlock : public vdrefcount {
public:
	virtual ATCassetteImageBlockType GetBlockType() const = 0;

	// Retrieve a bit at the given block-local offset. The position must be within
	// the block.
	virtual bool GetBit(uint32 pos, bool bypassFSK) const;

	// Retrieve sum of bits starting at the given block-local offset. The
	// count must be >0 and the range must fit within the block.
	virtual uint32 GetBitSum(uint32 pos, uint32 n, bool bypassFSK) const;

	struct FindBitResult {
		uint32 mPos;
		bool mFound;
	};

	virtual FindBitResult FindBit(uint32 pos, uint32 limit, bool polarity, bool bypassFSK) const;
	
	virtual void GetTransitionCounts(uint32 pos, uint32 n, bool lastPolarity, bool bypassFSK, uint32& xcount, uint32& mcount) const;

	// Retrieve audio sync samples.
	//
	// dst: Auto-incremented pointer to output buffer.
	// posSample: Auto-incremented integer audio sample counter.
	// posCycle: Auto-updated fractional audio sample counter.
	// n: Number of sync samples requested.
	//
	// Returns number of sync samples produced.
	//
	// The audio samples in the block are resampled to the sync mixer sample
	// rate, with (posSample,posCycle) as the fractional audio sample position.
	// The resulting sync mixer samples are added into the left and right
	// channel buffers. The right channel buffer, if provided, receives the
	// same audio as the left buffer. The buffer must be initialized on entry
	// such that the buffers need not be touched if there is no audio.
	//
	virtual uint32 AccumulateAudio(float *&dst, uint32& posSample, uint32& posCycle, uint32 n, float volume) const;
};

template<ATCassetteImageBlockType T_BlockType>
class ATCassetteImageBlockT : public ATCassetteImageBlock {
public:
	static constexpr ATCassetteImageBlockType kBlockType = T_BlockType;

	ATCassetteImageBlockType GetBlockType() const override final {
		return T_BlockType;
	}
};

// Cassette image block type for standard framed bytes with 8-bits of data stored
// in FSK encoding.
class ATCassetteImageDataBlockStd final : public ATCassetteImageBlockT<kATCassetteImageBlockType_Std> {
public:
	ATCassetteImageDataBlockStd();

	static uint32 EstimateNewBlockLen(uint32 bytes, uint32 baudRate);

	void Init(uint32 baudRate);

	void AddData(const uint8 *data, uint32 len);
	uint32 EstimateAddData(uint32 len) const;

	const uint8 *GetData() const;
	const uint32 GetDataLen() const;

	uint32 GetBaudRate() const { return mBaudRate; }
	uint32 GetDataSampleCount() const;
	uint64 GetDataSampleCount64() const;

	bool GetBit(uint32 pos, bool bypassFSK) const override;
	uint32 GetBitSum(uint32 pos, uint32 n, bool bypassFSK) const override;

	FindBitResult FindBit(uint32 pos, uint32 limit, bool polarity, bool bypassFSK) const override;

	void GetTransitionCounts(uint32 pos, uint32 n, bool lastPolarity, bool bypassFSK, uint32& xcount, uint32& mcount) const;

	// Note that volume here differs from the cassette image as it applies to signed samples, not normalized
	// samples.
	uint32 AccumulateAudio(float *&dst, uint32& posSample, uint32& posCycle, uint32 n, float volume) const override;

private:
	uint64 mDataSamplesPerByteF32 = 0;
	uint64 mBytesPerDataSampleF32 = 0;
	uint64 mBytesPerCycleF32 = 0;
	uint32 mBitsPerSyncSampleF32 = 0;

	uint32 mPhaseAddedPerOneBitLo = 0;
	uint32 mPhaseAddedPerOneBitHi = 0;

	uint32 mBaudRate = 0;

	vdfastvector<uint8> mData;

	// Partial sum of '1' bits prior to start of current byte, mod 24. Why 24?
	// Well, we use this array to determine the phase shift caused by the
	// distribution of '0' and '1' bits. A one bit advances the phase by 1/24th
	// more than a zero. This means that we don't care about multiples of 24.
	// However, we have to do an explicit mod since 256 mod 24 is nonzero.
	vdfastvector<uint8> mPhaseSums;
};

/// Cassette image block for raw data.
class ATCassetteImageBlockRawData final : public ATCassetteImageBlockT<kATCassetteImageBlockType_FSK> {
public:
	uint32 GetDataSampleCount() const { return mDataLength; }

	void AddFSKPulseSamples(bool polarity, uint32 samples);
	void AddDirectPulseSamples(bool polarity, uint32 samples);

	// Extract pairs of 0/1 pulse lengths.
	void ExtractPulses(vdfastvector<uint32>& pulses, bool bypassFSK) const;

	bool GetBit(uint32 pos, bool bypassFSK) const override;
	uint32 GetBitSum(uint32 pos, uint32 n, bool bypassFSK) const override;

	FindBitResult FindBit(uint32 pos, uint32 limit, bool polarity, bool bypassFSK) const override;

	void GetTransitionCounts(uint32 pos, uint32 n, bool lastPolarity, bool bypassFSK, uint32& xcount, uint32& mcount) const override;

	void SetBits(bool fsk, uint32 startPos, uint32 n, bool polarity);

	uint32 mDataLength = 0;
	vdfastvector<uint32> mDataRaw {};		// Storage is MSB first.
	vdfastvector<uint32> mDataFSK {};		// Storage is MSB first.

	uint64 mFSKPhaseAccum = 0;
};

/// Cassette image block type for raw audio data only.
class ATCassetteImageBlockRawAudio final : public ATCassetteImageBlockT<kATCassetteImageBlockType_RawAudio> {
public:
	void GetMinMax(uint32 offset, uint32 len, uint8& minVal, uint8& maxVal) const;

	uint32 AccumulateAudio(float *&dst, uint32& posSample, uint32& posCycle, uint32 n, float volume) const override;

	vdfastvector<uint8> mAudio;
	uint32 mAudioLength;
};

/// Cassette image block type for blank tape.
class ATCassetteImageBlockBlank final : public ATCassetteImageBlockT<kATCassetteImageBlockType_Blank> {
public:
	uint32 AccumulateAudio(float *&dst, uint32& posSample, uint32& posCycle, uint32 n, float volume) const override;
};


#endif
