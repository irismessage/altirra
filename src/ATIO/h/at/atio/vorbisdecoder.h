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

#ifndef f_AT_ATIO_VORBISDECODER_H
#define f_AT_ATIO_VORBISDECODER_H

#include <vd2/system/vdstl.h>
#include <at/atcore/fft.h>
#include <at/atio/vorbisbitreader.h>

struct ATVorbisCodeBook;

struct ATVorbisCodeBook {
	static constexpr uint32_t kQuickBits = 10;
	static constexpr uint32_t kQuickTableSize = 1U << kQuickBits;
	static constexpr uint32_t kQuickMask = kQuickTableSize - 1;

	uint32_t mDimension;
	bool mbQuickOnly;

	uint16_t mQuickTable[kQuickTableSize][2];

	vdfastvector<uint32_t> mCodewords;
	vdfastvector<uint32_t> mValues;
	vdfastvector<uint32_t> mLengths;

	// vector values
	vdfastvector<float> mVQValues;

	void BuildQuickTable();

	template<bool T_QuickOnly = false>
	VDFORCEINLINE uint32_t DecodeIndexFast(ATVorbisBitReader& reader) const {
		uint32_t v = reader.Peek32();
		const auto *quickEnt = mQuickTable[v & kQuickMask];
		uint32_t value = quickEnt[0], length = quickEnt[1];

		if constexpr (!T_QuickOnly) {
			if (value == 0xFFFF) {
				auto slow = DecodeIndexSlow(v);

				value = slow.mValue;
				length = slow.mLength;
			}
		}

		reader.Consume(length);

		return value;
	}
	
	uint32_t DecodeIndex(ATVorbisBitReader& reader) const {
		return DecodeIndexFast(reader);
	}

	struct SlowResult {
		uint32_t mValue;
		uint32_t mLength;
	};

	VDNOINLINE SlowResult DecodeIndexSlow(uint32_t peek32) const;

	const float *DecodeVQ(ATVorbisBitReader& reader) const {
		return &mVQValues[DecodeIndex(reader) * mDimension];
	}

	template<size_t T_Dim, bool T_QuickOnly>
	VDFORCEINLINE const float *DecodeVQDim(ATVorbisBitReader& reader) const {
		return &mVQValues[DecodeIndexFast<T_QuickOnly>(reader) * T_Dim];
	}

	// Check if the codebook is valid for VQ decoding.
	//
	// The specification says that a stream is undecodable if a scalar codebook is used in
	// VQ context, but doesn't strongly define 'using'; as such, we currently assume the
	// pessimistic interpretation that it is OK to reference a non-VQ codebook for a VQ
	// context as long as it isn't actually used. This means we have to check during packet
	// decoding whether the codebook is VQ capable. Fortunately, it's a cheap check.
	bool IsValidForVQ() const { return !mVQValues.empty(); }

	static uint32_t Rev32(uint32_t v) {
		v = _byteswap_ulong(v);

		v = ((v & 0x55555555) << 1) + ((v & 0xAAAAAAAA) >> 1);
		v = ((v & 0x33333333) << 2) + ((v & 0xCCCCCCCC) >> 2);
		v = ((v & 0x0F0F0F0F) << 4) + ((v & 0xF0F0F0F0) >> 4);

		return v;
	}
};

class ATVorbisDecoder : public ATVorbisBitDecoderBase {
public:
	void DisableCRCChecking() ;
	void Init(vdfunction<size_t(void *, size_t)> readfn);
	void ReadHeaders();

	uint32_t GetSampleRate() const {
		return mSampleRate;
	}

	uint64_t GetSampleCount() const {
		return mSampleCounter;
	}

	uint32_t GetChannelCount() const {
		return mNumChannels;
	}

	uint32_t GetAvailableSamples() const {
		return mOutputSamplesLeft;
	}

	const float *GetRawSamples(uint32_t ch) const {
		return mResidueVectors[ch] + mOutputSampleOffset;
	}

	uint32_t ReadInterleavedSamplesS16(int16_t *dst, uint32_t numSamples);
	uint32_t ReadInterleavedSamplesStereoS16(int16_t *dst, uint32_t numSamples);
	void ConsumeSamples(uint32_t n);

	void ReadIdHeader();
	void ReadCommentHeader();
	void ReadSetupHeader();

	bool ReadAudioPacket();

	void ReadCompletePacket(void *dst, uint32_t len);
	bool BeginPacket();
	void ReadFromPacket(void *dst, uint32_t len);
	
	PacketData ContinuePacket(const uint8_t *used);

	void DiscardRemainingPacket();
	bool ReadPage();

private:
	// Vorbis I supports up to 256 channels, but we only support up to stereo as we have
	// no use for >2 channel streams.
	static constexpr uint32_t kMaxChannels = 2;

	struct MappingInfo;
	struct DecodedFloor0Info;
	struct DecodedFloor1Info;

	struct Floor0Info {
		uint8_t mOrder;				// 0..255; 0-1 blocked during header decode
		uint16_t mRate;				// 0..65535
		uint16_t mBarkMapSize;		// 0..65535; 0 blocked during header decode
		uint8_t mAmplitudeBits;		// 0..63
		uint8_t mAmplitudeOffset;	// 0..255
		uint8_t mNumBooks;			// 1..16
		uint8_t mNumBookBits;		// precomputed ilog(mNumBooks)

		uint8_t mBooks[16];			// 0..255, code book ID validated during header decode
	};

	struct Floor1Info {
		uint8_t mNumPartitions;		// 0..31
		uint8_t mNumValues;			// computed from partition and dimensions
		uint8_t mMultiplier;		// 0..3

		uint8_t mPartClass[32];		// highest value determines number of valid classes

		uint8_t mClassDims[16];			// 1..8
		uint8_t mClassSubclassBits[16];	// 0..3
		uint8_t mClassMasterBooks[16];	// validate on decode; only used/valid if subclass bits non-zero
		uint8_t mSubclassBooks[16][16];	// [partclass][0..2^subClassBits-1]; validated in decode

		uint16_t mFloorX[65];		// validated in header decode for uniqueness (NOT sorted by spec)

		uint16_t mNeighbors[65][2];	// computed in header decode off of floor X values
		uint8_t mSortOrder[65];		// computed in header decode off of floor X values
	};

	struct FloorInfo {
		bool mbFloorType1 = false;

		union {
			Floor0Info mType0;
			Floor1Info mType1;
		};
	};

	struct DecodedFloor0Info {
		float mLinearFloorScale;
		float mLinearFloorOffset;

		float mCosCoeffs[255];
	};

	struct DecodedFloor1Info {
		uint8_t mFloorY[65];
		bool mbStep2Flags[65];
	};

	struct DecodedFloorInfo {
		union {
			DecodedFloor0Info mType0;
			DecodedFloor1Info mType1;
		};
	};

	struct ResidueInfo {
		uint8_t mResidueType = 0;			// 0..2; validated in header decode
		uint8_t mNumClassifications = 0;	// 1..64; multiplier for partition class int packing
		uint8_t mClassBook = 0;				// validated in header decode
		uint32_t mPartitionSize = 0;		// 1..2^24-1
		uint32_t mResidueBegin = 0;			// 0..2^24-1
		uint32_t mResidueEnd = 0;			// 0..2^24-1, exclusive with begin; validated in *packet decode*

		// codebook indices by classification and then encoding pass
		int16_t mBooks[64][8] {};			// validated in header decode; may be -1
	};

	struct ModeInfo {
		bool mbBlockFlag = false;
		uint8_t mMapping = 0;				// validated in header decode
	};

	struct Submap {
		// computed from channel mux
		uint8_t mNumChannels = 0;			// 1..2; validated in header decode

		uint8_t mFloorEncoding = 0;			// validated in header decode
		uint8_t mResidueEncoding = 0;		// validated in header decode
	};

	struct CouplingStep {
		uint8_t mMagnitudeChannel = 0;		// validated in header decode
		uint8_t mAngleChannel = 0;			// validated in header decode
	};

	struct MappingInfo {
		uint8_t mNumSubMaps = 0;			// 1..16 
		uint8_t mNumCouplingSteps = 0;		// 0..256

		CouplingStep mCouplingSteps[256] {};

		// map of submap used by each channel
		uint8_t mChannelMux[kMaxChannels] {};

		Submap mSubmaps[16];
	};

	uint32_t DecodeFloorCurves(ATVorbisBitReader& reader, const MappingInfo& mapping, uint32_t halfBlockSize);
	void RenderFloorCurve(const MappingInfo& mapping, uint32_t ch, uint32_t n);

	bool DecodeFloorCurve0(ATVorbisBitReader& reader, const MappingInfo& mapping, FloorInfo& floor, uint32_t ch, uint32_t halfBlockSize);
	void RenderFloorCurve0(const Floor0Info& floor0, const DecodedFloor0Info& decodedFloor0, float *dst, uint32_t n);

	bool DecodeFloorCurve1(ATVorbisBitReader& reader, const MappingInfo& mapping, FloorInfo& floor, uint32_t ch, uint32_t halfBlockSize);
	void RenderFloorCurve1(const Floor1Info& floor1, const DecodedFloor1Info& decodedFloor1, float *dst, uint32_t n);

	void DecodeResidueVectors(ATVorbisBitReader& reader, const MappingInfo& mapping, uint32_t halfBlockSize, uint32_t channelVectorsToNotDecode);
	void DecodeResidue0(ATVorbisBitReader& reader, const ATVorbisCodeBook& codeBook, float *dst, uint32_t codeCount);

	template<uint32_t T_Dim>
	void DecodeResidue0Dim(ATVorbisBitReader& reader, const ATVorbisCodeBook& codeBook, float *dst, uint32_t codeCount);

	void DecodeResidue1(ATVorbisBitReader& reader, const ATVorbisCodeBook& codeBook, float *dst, uint32_t codeCount);

	template<uint32_t T_Dim, bool T_QuickOnly>
	void DecodeResidue1Dim(ATVorbisBitReader& reader, const ATVorbisCodeBook& codeBook, float *dst, uint32_t codeCount);

	void DecoupleChannels(const MappingInfo& mapping, uint32_t halfBlockSize);

	bool mbDropFirstBlock = true;
	bool mbLongPrevWindow = false;
	uint64_t mSampleCounter = 0;

	// When decoding varied block sizes, the residue vectors are aligned
	// so that the block 1/4 point, or the residue 1/2 point, is consistent
	// across all blocks. This allows us to do overlap-and-add in place.
	// mResidueVectorOffset indicates the offset from the start of the
	// residue vector to the portion used.
	//
	// The guaranteed minimum alignment is 16 since the smallest block
	// size allowed in Vorbis I is 64 samples.
	uint32_t mResidueVectorOffset = 0;

	uint32_t mOutputSamplesLeft = 0;
	uint32_t mOutputSampleOffset = 0;

	uint8_t mNumChannels = 1;
	uint32_t mSampleRate = 48000;

	uint32_t mNumCodeBooks = 0;
	uint8_t mNumModes = 0;
	uint8_t mNumModeBits = 0;
	uint8_t mNumMappings = 0;

	// [0] = short, [1] = long
	uint8_t mBlockSizeBits[2] {};

	vdvector<ATVorbisCodeBook> mCodeBooks;
	vdvector<FloorInfo> mFloors;

	vdfastvector<ResidueInfo> mResidues;

	vdfastvector<ModeInfo> mModes;
	vdfastvector<MappingInfo> mMappings;

	uint8_t *mpPacketSrc = nullptr;
	uint32_t mPacketLenLeft = 0;

	// Flag set when a short segment has been reached that indicates end of
	// packet. This blocks continuing to the next segment or page so that
	// end-of-packet processing can be handled properly, while still carrying
	// over to the next page when a packet spans pages.
	bool mbPacketClosed = false;

	// Flag set once we have started returning EoP indications to the bitreader.
	// After this point we start returning dummy data, so on continuations we
	// shouldn't treat any unused data as valid.
	bool mbPacketEopReturned = false;

	// Set when the end of the stream is reached and there are no more pages
	// or packets.
	bool mbEndOfStream = false;

	uint8_t *mpNextSegment = nullptr;
	uint8_t mNextSegmentIndex = 0;
	uint8_t mNumSegments = 0;

	bool mbCRCCheckingDisabled = false;

	vdfastvector<int16_t> mResiduePartitionClasses[kMaxChannels];
	DecodedFloorInfo mDecodedFloors[kMaxChannels];

	// short/long block IMDCTs
	ATIMDCT mIMDCT[2];
	ATFFTAllocator mFFTAllocator;

	vdfunction<size_t(void *, size_t)> mpReadFn;

	static constexpr size_t kPageHeaderLen = 27;
	uint8_t mPageHeaderAndSegmentTable[kPageHeaderLen + 255] {};

	// Max page data size is 255 segments of 255 bytes each.
	// We have an additional 32 prefix bytes to accommodate any data that had
	// to be carried over from the previous page, including up to 8 bytes that
	// the bit reader couldn't consume, and up to 16 bytes for a short page.
	static constexpr uint32_t kPageDataPrefixLen = 32;
	static constexpr uint32_t kPageDataMainLen = 255*255;

	uint8_t mPageData[kPageDataPrefixLen + kPageDataMainLen] {};

	// Temporary buffer only used to decode residue 2, which requires deinterleaving
	// across channels. The residue partition size is effectively unlimited (2^24)
	// and we have to accommodate the possibility that the residue decode starts
	// unaligned from the channels.
	alignas(16) float mResidueDecodingBuffer[kMaxChannels * 4096] {};

	// Used to decode residue, multiply in floor curve, and do IMDCT. Max size
	// is half max block (8K/2 = 4K elements).
	alignas(16) float mResidueVectors[kMaxChannels][4096] {};

	// holds the last quarter of IMDCT output for overlapping with the next
	// block; this is symmetrically reflected to reconstitute the third quarter.
	alignas(16) float mOverlapVector[kMaxChannels][2048] {};

	// left half of window, [0] = short and [1] = long
	alignas(16) float mHalfWindows[2][4096] {};
};

#endif
