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

///////////////////////////////////////////////////////////////////////////
// Deviations from spec imposed for robustness/security:
//
// - Decoded float32s are clamped to +/-63 exponent (matches reference
//   decoder).
// - Codebooks are limited to 1M total values.
// - Residue classbooks may not have zero dimension, due to integer
//   division during decode.
// - Floor 0 rate and bark map size cannot be zero, due to floating point
//   division during decode.
//
///////////////////////////////////////////////////////////////////////////


#include "stdafx.h"
#include <numbers>
#include <numeric>
#include <vd2/system/math.h>
#include <vd2/system/zip.h>
#include <at/atio/vorbismisc.h>
#include <at/atio/vorbisbitreader.h>
#include <at/atio/vorbisdecoder.h>

#ifdef VD_CPU_ARM64
#include <arm_neon.h>
#endif

///////////////////////////////////////////////////////////////////////////

void ATVorbisCodeBook::BuildQuickTable() {
	memset(mQuickTable, 0xFF, sizeof mQuickTable);
	mbQuickOnly = true;

	if (mLengths.size() > 65534) {
		mbQuickOnly = false;
	} else {
		for(const auto& len : mLengths) {
			if (len > kQuickBits) {
				mbQuickOnly = false;
				break;
			}
		}
	}

	size_t n = std::min<size_t>(mLengths.size(), 65534);

	if (n == 1) {
		// single-code codebook
		for(auto& qe : mQuickTable) {
			qe[0] = 0;
			qe[1] = 1;
		}
	} else {
		for(size_t i = 0; i < n; ++i) {
			uint32_t len = mLengths[i];

			if (len <= kQuickBits) {
				uint32_t val = Rev32(mCodewords[i]);

				uint32_t step = 1U << len;

				while(val < kQuickTableSize) {
					mQuickTable[val][0] = (uint16_t)mValues[i];
					mQuickTable[val][1] = len;

					val += step;
				}
			}
		}
	}
}

ATVorbisCodeBook::SlowResult ATVorbisCodeBook::DecodeIndexSlow(uint32_t v) const {
	v = Rev32(v);

	const uint32_t i = (uint32_t)(std::upper_bound(mCodewords.begin(), mCodewords.end(), v) - mCodewords.begin()) - 1;

	return { mValues[i], mLengths[i] };
}

///////////////////////////////////////////////////////////////////////////

ATVorbisBitDecoderBase::PacketData ATVorbisBitDecoderBase::RefillContinuePacket(const uint8_t *used) {
	return static_cast<ATVorbisDecoder *>(this)->ContinuePacket(used);
}

void ATVorbisDecoder::DisableCRCChecking() {
	mbCRCCheckingDisabled = true;
}

void ATVorbisDecoder::Init(vdfunction<size_t(void *, size_t)> readfn) {
	mpReadFn = std::move(readfn);
}

void ATVorbisDecoder::ReadHeaders() {
	ReadIdHeader();
	ReadCommentHeader();
	ReadSetupHeader();
}

uint32_t ATVorbisDecoder::ReadInterleavedSamplesS16(int16_t *dst, uint32_t numSamples) {
	if (numSamples > mOutputSamplesLeft)
		numSamples = mOutputSamplesLeft;

	mOutputSamplesLeft -= numSamples;

	if (mNumChannels == 2)
		ATVorbisConvertF32ToS16x2(dst, &mResidueVectors[0][mOutputSampleOffset], &mResidueVectors[1][mOutputSampleOffset], numSamples);
	else
		ATVorbisConvertF32ToS16(dst, &mResidueVectors[0][mOutputSampleOffset], numSamples);

	mOutputSampleOffset += numSamples;

	return numSamples;
}

uint32_t ATVorbisDecoder::ReadInterleavedSamplesStereoS16(int16_t *dst, uint32_t numSamples) {
	if (mNumChannels == 2)
		return ReadInterleavedSamplesS16(dst, numSamples);

	if (numSamples > mOutputSamplesLeft)
		numSamples = mOutputSamplesLeft;

	const float *src = &mResidueVectors[0][mOutputSampleOffset];
	mOutputSamplesLeft -= numSamples;
	mOutputSampleOffset += numSamples;

	ATVorbisConvertF32ToS16Rep2(dst, src, numSamples);

	return numSamples;
}

void ATVorbisDecoder::ConsumeSamples(uint32_t n) {
	if (n <= mOutputSamplesLeft)
		mOutputSamplesLeft -= n;
	else
		mOutputSamplesLeft = 0;
}

void ATVorbisDecoder::ReadIdHeader() {
	uint8_t idHeader[30];

	ReadCompletePacket(idHeader, 30);

	if (memcmp(idHeader, "\x01vorbis", 7))
		throw ATVorbisException("Identification header packet not found");

	const uint32_t vorbisVersion = idHeader[7]
		+ ((uint32_t)idHeader[8] << 8)
		+ ((uint32_t)idHeader[9] << 16)
		+ ((uint32_t)idHeader[10] << 24);

	if (vorbisVersion != 0)
		throw ATVorbisException("Unsupported Vorbis version");

	uint32_t audioChannels = idHeader[11];
	if (!audioChannels)
		throw ATVorbisException("Invalid ID header: audio channel count is zero");

	if (audioChannels > 2)
		throw ATVorbisException("Unsupported channel count (>2 channels)");

	uint32_t audioSampleRate = idHeader[12]
		+ ((uint32_t)idHeader[13] << 8)
		+ ((uint32_t)idHeader[14] << 16)
		+ ((uint32_t)idHeader[15] << 24);
	if (!audioSampleRate)
		throw ATVorbisException("Invalid ID header: audio sampling rate is zero");

	mBlockSizeBits[0] = idHeader[28] & 15;
	mBlockSizeBits[1] = idHeader[28] >> 4;

	// valid block sizes are 64-8192, and short block size must be <= long block size
	if (mBlockSizeBits[0] < 6 || mBlockSizeBits[0] > 13 ||
		mBlockSizeBits[1] < 6 || mBlockSizeBits[1] > 13 ||
		mBlockSizeBits[0] > mBlockSizeBits[1])
	{
		throw ATVorbisException("Invalid ID header: invalid block sizes");
	}

	// check framing bit
	if (!(idHeader[29] & 1))
		throw ATVorbisException("Identification header framing error");

	// all good
	mNumChannels = audioChannels;
	mSampleRate = audioSampleRate;

	// Initialize IMDCTs.
	//
	// Note that the Vorbis block size corresponds to the full 2N output of the
	// IMDCT, so we must halve it when initializing our IMDCT.
	mIMDCT[0].Reserve(mFFTAllocator, size_t(1) << (mBlockSizeBits[0] - 1), true);
	mIMDCT[1].Reserve(mFFTAllocator, size_t(1) << (mBlockSizeBits[1] - 1), true);
	mFFTAllocator.Finalize();
	mIMDCT[0].Bind(mFFTAllocator);
	mIMDCT[1].Bind(mFFTAllocator);

	// compute windows
	for (uint32_t windowIdx = 0; windowIdx < 2; ++windowIdx) {
		const uint32_t n = 1U << mBlockSizeBits[windowIdx];
		const uint32_t n2 = n >> 1;

		float *dst = mHalfWindows[windowIdx];
		float pi_n = std::numbers::pi_v<float> / (float)n;
		float pi_2 = std::numbers::pi_v<float> * 0.5f;
		for (uint32_t i = 0; i < n2; ++i) {
			float xf = ((float)i + 0.5f) * pi_n;
			float w1 = sinf(xf);

			dst[i] = sinf(pi_2 * (w1 * w1));
		}
	}
}

void ATVorbisDecoder::ReadCommentHeader() {
	BeginPacket();
	DiscardRemainingPacket();
}

void ATVorbisDecoder::ReadSetupHeader() {
	BeginPacket();

	uint8_t packetSig[7];

	ReadFromPacket(packetSig, 7);

	if (memcmp(packetSig, "\x05vorbis", 7))
		throw ATVorbisException("Setup header packet not found");

	ATVorbisBitReader reader(*this);

	const auto ilog = [](uint32_t v) -> uint32_t {
		uint32_t lv = 0;
		while(v) {
			v >>= 1;
			++lv;
		}

		return lv;
	};

	// read codebooks
	uint32_t numCodebooks = reader(8) + 1;
	mNumCodeBooks = numCodebooks;

	mCodeBooks.resize(numCodebooks);

	for(uint32_t cb = 0; cb < numCodebooks; ++cb) {
		uint32_t cbSig = reader(24);
		if (cbSig != 0x564342)
			throw ATVorbisException("Invalid codebook sync");

		ATVorbisCodeBook& codebook = mCodeBooks[cb];

		const uint32_t cbDim = reader(16);
		const uint32_t cbEnt = reader(24);

		// block codebooks with no values; this would not pass Huffman tree validation anyway
		if (!cbEnt)
			throw ATVorbisException("Invalid empty codebook");

		// limit codebook size to 1M total output values
		if ((uint64_t)cbDim * cbEnt >= UINT64_C(0x100000))
			throw ATVorbisException("Codebook too large");

		bool ordered = reader(1) != 0;

		codebook.mDimension = cbDim;

		vdfastvector<uint32_t> lengths(cbEnt, 0);

		if (!ordered) {
			bool sparse = reader(1) != 0;

			if (sparse) {
				for(uint32_t i = 0; i < cbEnt; ++i) {
					if (reader(1)) {
						// used entry
						lengths[i] = reader(5) + 1;
					}
				}
			} else {
				for(uint32_t i = 0; i < cbEnt; ++i)
					lengths[i] = reader(5) + 1;
			}
		} else {
			uint32_t cbIdx = 0;
			uint32_t cbLen = reader(5) + 1;

			do {
				uint32_t runLen = reader(ilog(cbEnt - cbIdx));

				if (runLen > cbEnt - cbIdx)
					throw ATVorbisException("Invalid codebook");

				for(uint32_t i=0; i<runLen; ++i)
					lengths[cbIdx++] = cbLen;
				++cbLen;
			} while(cbIdx < cbEnt);
		}

		if (cbEnt > 1) {
			uint32_t nextCode[32] {};
			uint32_t i = 0;

			while (i < cbEnt && !lengths[i])
				++i;

			if (i >= cbEnt)
				throw ATVorbisException("Invalid empty codebook");

			// split tree down to first code
			for(uint32_t j = 0; j < lengths[i]; ++j)
				nextCode[j] = UINT32_C(0x80000000) >> j;

			vdvector<std::pair<uint32_t, uint32_t>> codewords;
			codewords.emplace_back(0, i);
			++i;

			// process remaining codes
			for(; i < cbEnt; ++i) {
				uint32_t len = lengths[i];

				if (len) {
					// switch to zero-based
					--len;

					// traverse upward to find first available code
					uint32_t len2 = len;
					while(!nextCode[len2]) {
						if (!len2)
							throw ATVorbisException("Invalid over-full codebook");

						--len2;
					}

					// allocate code
					const uint32_t code = nextCode[len2];
					nextCode[len2] = 0;

					codewords.emplace_back(code, i);

					// fill out split branches down to desired length
					while(len2 < len) {
						++len2;
						nextCode[len2] = code + (UINT32_C(0x80000000) >> len2);
					}
				}
			}

			std::sort(codewords.begin(), codewords.end());

			size_t n = codewords.size();
			codebook.mCodewords.resize(n);
			codebook.mValues.resize(n);
			codebook.mLengths.resize(n);

			for (size_t i = 0; i < n; ++i) {
				codebook.mCodewords[i] = codewords[i].first;
				codebook.mValues[i] = codewords[i].second;
				codebook.mLengths[i] = lengths[codewords[i].second];
			}

			if (std::any_of(std::begin(nextCode), std::end(nextCode), [](uint32_t v) { return v != 0; }))
				throw ATVorbisException("Incomplete or overflowed codebook");
		} else {
			if (lengths[0] != 1)
				throw ATVorbisException("Invalid single-code codebook");

			codebook.mCodewords.resize(1, 0);
			codebook.mValues.resize(1, 0);
			codebook.mLengths.resize(1, 1);
		}

		const uint32_t cbLookupType = reader(4);

		// If the codebook has a VQ representation, enforce that the dimension cannot be zero
		// as it would blow up VQ processing.
		//
		// For a scalar codebook, it's more complex. Codebooks used as residue classbooks use
		// the dimension for integer unpacking, and therefore dim=0 there would be invalid.
		// Other scalar codebooks do not use the dimension, however. We take the compatible
		// approach and validate dimension only when used for a residue classbook.

		if ((cbLookupType == 1 || cbLookupType == 2) && !codebook.mDimension)
			throw ATVorbisException("Invalid zero dimension on VQ-encoded codebook");

		switch(cbLookupType) {
			case 0:
				// Lookup type 0: No VQ representation
				break;

			case 1:
				// Lookup type 1: VQ lattice
				{
					const float minv = reader.ReadFloat();
					const float delv = reader.ReadFloat();
					const uint32_t valBits = reader(4) + 1;
					const bool seqp = reader(1) != 0;

					// compute total number of multiplicand values; this is determined by lookup1_values()
					// in the spec, which requires the highest value for which value^dim <= codebook entry
					// count
					uint32_t numMult = 0;
					for(;;) {
						++numMult;

						uint32_t t = 1;
						for(uint32_t i = 0; i < cbDim; ++i)
							t *= numMult;

						if (t > cbEnt) {
							// It is possible for this to end up being 0, but that can only happen if
							// the number of entries is 0, in which case we will never attempt to divide
							// by it.
							--numMult;
							break;
						}
					}

					// read multiplicands
					vdfastvector<float> multiplicands(numMult);
					for(uint32_t i=0; i<numMult; ++i)
						multiplicands[i] = (float)reader(valBits) * delv + minv;

					// preinit value array
					codebook.mVQValues.resize(cbDim * cbEnt);

					// decode each entry
					float *dst = codebook.mVQValues.data();
					for(uint32_t ent = 0; ent < cbEnt; ++ent) {
						float last = 0;
						uint32_t idx = ent;

						for(uint32_t dim = 0; dim < cbDim; ++dim) {
							// unpack subindex for dimension from multiplicatively-packed index
							const uint32_t subIdx = idx % numMult;
							idx /= numMult;

							// decode value
							const float v = multiplicands[subIdx] + last;
							*dst++ = v;

							if (seqp)
								last = v;
						}
					}
				}
				break;

			case 2:
				// Lookup type 1: VQ explicit
				{
					const float minv = reader.ReadFloat();
					const float delv = reader.ReadFloat();
					const uint32_t valBits = reader(4) + 1;
					const bool seqp = reader(1) != 0;

					// preinit value array
					const uint32_t cbVals = cbEnt * cbDim;
					codebook.mVQValues.resize(cbVals);

					// unpack multiplicands for each entry
					float *dst = codebook.mVQValues.data();
					for(uint32_t ent = 0; ent < cbEnt; ++ent) {
						float last = 0;

						for(uint32_t dim = 0; dim < cbDim; ++dim) {
							// decode value
							const float v = (float)reader(valBits) * delv + minv + last;
							*dst++ = v;

							if (seqp)
								last = v;
						}
					}
				}
				break;

			default:
				throw ATVorbisException("Unsupported codebook lookup type");
		}

		codebook.BuildQuickTable();
	}

	// read time domain transforms
	const uint32_t tdtCount = reader(6) + 1;
	for(uint32_t i=0; i<tdtCount; ++i) {
		uint32_t tdtDesc = reader(16);

		if (tdtDesc)
			throw ATVorbisException("Invalid time domain transform");
	}

	if (reader.CheckEop())
		throw ATVorbisException("End of packet during header decode");

	const uint32_t floorCount = reader(6) + 1;
	mFloors.resize(floorCount);

	for(FloorInfo& floor : mFloors) {
		uint16_t floorType = reader(16);

		if (floorType >= 2)
			throw ATVorbisException("Unsupported floor type");

		floor.mbFloorType1 = (floorType != 0);

		if (floorType == 0) {
			auto& floor0 = floor.mType0;

			floor0.mOrder = reader(8);
			floor0.mRate = reader(16);
			floor0.mBarkMapSize = reader(16);
			floor0.mAmplitudeBits = reader(6);
			floor0.mAmplitudeOffset = reader(8);
			floor0.mNumBooks = reader(4) + 1;
			floor0.mNumBookBits = ilog(floor0.mNumBooks);

			// order-2 is used as upper limit for product, so it cannot be <2
			if (floor0.mOrder < 2)
				throw ATVorbisException("Invalid order value in floor 0 definition");

			// decode divides by this, it cannot be zero
			if (floor0.mBarkMapSize == 0)
				throw ATVorbisException("Invalid bark map size in floor 0 definition");

			// decode divides by bark(rate/2), so it also cannot be zero
			if (floor0.mRate == 0)
				throw ATVorbisException("Invalid rate in floor 0 definition");

			for(uint32_t j=0; j<floor0.mNumBooks; ++j) {
				uint8_t codeBookId = reader(8);

				if (codeBookId >= mNumCodeBooks)
					throw ATVorbisException("Invalid code book ID in floor 0 definition");

				floor0.mBooks[j] = codeBookId;
			}
		} else {
			auto& floor1 = floor.mType1;

			// read floor1 partition count
			uint32_t numParts = reader(5);
			floor1.mNumPartitions = numParts;

			// read floor1 partition classes
			uint32_t numPartClasses = 0;
			for(uint32_t j=0; j<numParts; ++j) {
				uint32_t partClass = reader(4);

				floor1.mPartClass[j] = partClass;

				if (numPartClasses <= partClass)
					numPartClasses = partClass + 1;
			}

			for(uint32_t j=0; j<numPartClasses; ++j) {
				uint32_t partClassDim = reader(3) + 1;
				floor1.mClassDims[j] = partClassDim;

				uint32_t partClassSubClassBits = reader(2);
				floor1.mClassSubclassBits[j] = partClassSubClassBits;

				if (partClassSubClassBits) {
					uint8_t mcb = reader(8);

					if (mcb >= mNumCodeBooks)
						throw ATVorbisException("Invalid master code book referenced by partition");

					floor1.mClassMasterBooks[j] = mcb;
				}

				uint32_t numSubClassBooks = 1 << partClassSubClassBits;
				for(uint32_t k=0; k<numSubClassBooks; ++k) {
					uint8_t sbcb = reader(8);

					if (sbcb > mNumCodeBooks)
						throw ATVorbisException("Invalid code book referenced by partition sub class");

					floor1.mSubclassBooks[j][k] = sbcb;
				}
			}

			uint8_t multiplier = reader(2) + 1;
			floor1.mMultiplier = multiplier;
			floor1.mNumValues = 2;

			uint32_t rangeBits = reader(4);

			if (rangeBits) {
				floor1.mFloorX[0] = 0;
				floor1.mFloorX[1] = 1 << rangeBits;

				uint32_t n = 2;

				for(uint32_t j=0; j<numParts; ++j) {
					uint32_t partClass = floor1.mPartClass[j];
					uint32_t partClassDim = floor1.mClassDims[partClass];

					for(uint32_t k=0; k<partClassDim; ++k) {
						if (n == std::size(floor1.mFloorX))
							throw ATVorbisException("floor_x vector overflow");

						floor1.mFloorX[n++] = (uint16_t)reader(rangeBits);
					}
				}

				floor1.mNumValues = n;

				// precompute sort order
				for(uint32_t j = 0; j < n; ++j)
					floor1.mSortOrder[j] = (uint8_t)j;

				std::sort(floor1.mSortOrder, floor1.mSortOrder + n,
					[&floorX = floor1.mFloorX](uint32_t x, uint32_t y) {
						return floorX[x] < floorX[y];
					}
				);

				// scan the floor X list in sorted order and reject the floor definition if
				// there are duplicate points; this is prohibited by [7.2.2. header decode].
				for(uint32_t j = 0; j < n - 1; ++j) {
					if (floor1.mFloorX[floor1.mSortOrder[j]] == floor1.mFloorX[floor1.mSortOrder[j+1]])
						throw ATVorbisException("Invalid duplicate X values in floor 1 definition");
				}

				// precompute position table for each offset
				uint8_t posTable[sizeof(floor1.mFloorX) / sizeof(floor1.mFloorX[0])];
				for(uint32_t j = 0; j < n; ++j)
					posTable[floor1.mSortOrder[j]] = j;

				// precompute low/high neighbor offsets
				for(uint32_t j = 2; j < n; ++j) {
					uint32_t pos = posTable[j];

					uint32_t hiPos = pos;
					uint32_t loPos = pos;

					while(loPos > 0 && floor1.mSortOrder[loPos] >= j)
						--loPos;

					while(hiPos < n-1 && floor1.mSortOrder[hiPos] >= j)
						++hiPos;

					floor1.mNeighbors[j][0] = floor1.mSortOrder[loPos];
					floor1.mNeighbors[j][1] = floor1.mSortOrder[hiPos];
				}
			}
		}
	}

	if (reader.CheckEop())
		throw ATVorbisException("End of packet during header floor decode");

	const uint32_t residues = reader(6) + 1;

	mResidues.resize(residues);

	for(ResidueInfo& residue : mResidues) {
		const uint32_t residueType = reader(16);

		if (residueType >= 3)
			throw ATVorbisException("Unsupported residue type");

		residue.mResidueType = residueType;

		const uint32_t residueBegin = reader(24);
		const uint32_t residueEnd = reader(24);
		const uint32_t residuePartitionSize = reader(24) + 1;
		const uint32_t residueClassifications = reader(6) + 1;
		const uint8_t residueClassBook = reader(8);

		// pre-validate that classbook is valid
		if (residueClassBook >= mNumCodeBooks)
			throw ATVorbisException("Invalid class book referenced by residue");

		// [8.6.1. header decode] -- if classifications ^ classbook.dimensions > classbook.entries,
		// the stream is undecodable. Note that the classbook is not used in VQ context.
		// validate that the classbook dimension is not zero, as we'll be using it for
		// integer unpacking.
		ATVorbisCodeBook& classbook = mCodeBooks[residueClassBook];
		if (classbook.mDimension == 0)
			throw ATVorbisException("Invalid dimension on residue classbook");

		const size_t numClassbookEntries = classbook.mValues.size();
		uint32_t numNeededEntries = 1;
		if (residueClassifications > 1) {
			// Note that Vorbis allows a very high encoded dimension (64K-1), so a simple loop
			// is inadvisable -- we do squaring here. The classification count limit is 64 and
			// the classbook entry limit is 2^24-1, so we can just say in 32-bit here.
		
			uint32_t power = classbook.mDimension;
			uint32_t multiplier = residueClassifications;
			bool overflow = false;
			for(;;) {
				if (power & 1) {
					// inputs to this multiply both fit within 32-bit, but the result is
					// 64-bit to make overflow detection easier
					uint64_t newNumNeededEntries = (uint64_t)numNeededEntries * multiplier;

					if (newNumNeededEntries > numClassbookEntries) {
						overflow = true;
						break;
					}

					numNeededEntries = (uint32_t)newNumNeededEntries;
				}

				power >>= 1;
				if (!power)
					break;

				// power still has bits set, so if the multiplier would overflow on squaring,
				// we're guaranteed to overflow the result
				if (multiplier > 0xFFFFU) {
					overflow = true;
					break;
				}	

				multiplier *= multiplier;
			}

			if (overflow)
				throw ATVorbisException("Invalid residue definition: classification structure exceeds classbook entries");
		}

		residue.mNumClassifications = residueClassifications;
		residue.mPartitionSize = residuePartitionSize;
		residue.mResidueBegin = residueBegin;
		residue.mResidueEnd = residueEnd;
		residue.mClassBook = residueClassBook;

		// residueClassifications has range 1..64 (6 bits + 1)
		uint8_t residueCascade[64] {};

		for(uint32_t j=0; j<residueClassifications; ++j) {
			uint32_t highBits = 0;
			uint32_t lowBits = reader(3);
			
			if (reader(1))
				highBits = reader(5);

			residueCascade[j] = highBits*8 + lowBits;
		}

		// pre-clear codebook array to unused
		for(auto& v1 : residue.mBooks) {
			for(auto& v2 : v1)
				v2 = -1;
		}

		for(uint32_t j=0; j<residueClassifications; ++j) {
			for(uint32_t k=0; k<8; ++k) {
				if (residueCascade[j] & (1 << k)) {
					uint8_t cbk = reader(8);

					if (cbk >= mNumCodeBooks)
						throw ATVorbisException("Invalid code book referenced by residue");

					// set codebook to be used for classification in encoding pass
					residue.mBooks[j][k] = cbk;
				}
			}
		}
	}

	if (reader.CheckEop())
		throw ATVorbisException("End of packet during header residue decode");

	const uint32_t mappingCount = reader(6) + 1;
	mNumMappings = (uint8_t)mappingCount;

	mMappings.resize(mNumMappings);

	for(MappingInfo& mapping : mMappings) {
		if (reader(16))
			throw ATVorbisException("Unsupported mapping type");

		uint32_t numSubMaps = 1;
		if (reader(1))
			numSubMaps = reader(4) + 1;

		mapping.mNumSubMaps = numSubMaps;

		// read coupling info
		uint32_t mappingCouplingSteps = 0;
		if (reader(1)) {
			mappingCouplingSteps = reader(8) + 1;

			uint32_t mapLen = ilog(mNumChannels - 1);
			for(uint32_t j=0; j<mappingCouplingSteps; ++j) {
				uint32_t magnitudeCh = reader(mapLen);
				uint32_t angleCh = reader(mapLen);

				if (magnitudeCh >= mNumChannels || angleCh >= mNumChannels ||
					magnitudeCh == angleCh)
				{
					throw ATVorbisException("Invalid coupling step");
				}

				mapping.mCouplingSteps[j].mMagnitudeChannel = magnitudeCh;
				mapping.mCouplingSteps[j].mAngleChannel = angleCh;
			}
		}

		mapping.mNumCouplingSteps = mappingCouplingSteps;

		// verify 2-bit reserved field
		if (reader(2))
			throw ATVorbisException("Non-zero mapping reserved field");

		// read channel multiplex
		if (numSubMaps > 1) {
			for(uint32_t j=0; j<mNumChannels; ++j) {
				uint32_t submapIdx = reader(4);
				mapping.mChannelMux[j] = submapIdx;

				if (submapIdx >= numSubMaps)
					throw ATVorbisException("Invalid submap index in channel multiplexing");

				++mapping.mSubmaps[submapIdx].mNumChannels;
			}
		} else {
			mapping.mSubmaps[0].mNumChannels = mNumChannels;

			for(uint32_t j=0; j<mNumChannels; ++j)
				mapping.mChannelMux[j] = 0;
		}

		// read submap encoding info
		for(uint32_t j=0; j<numSubMaps; ++j) {
			// discard unused time configuration placeholder
			(void)reader(8);

			uint8_t floorNumber = reader(8);
			if (floorNumber >= floorCount)
				throw ATVorbisException("Invalid floor referenced by submap");

			uint8_t residueNumber = reader(8);
			if (residueNumber >= residues)
				throw ATVorbisException("Invalid residue referenced by submap");

			mapping.mSubmaps[j].mFloorEncoding = floorNumber;
			mapping.mSubmaps[j].mResidueEncoding = residueNumber;
		}
	}

	if (reader.CheckEop())
		throw ATVorbisException("End of packet during header mapping decode");

	// modes
	mNumModes = reader(6) + 1;
	mNumModeBits = ilog(mNumModes - 1);

	mModes.resize(mNumModes);
	for(ModeInfo& mode : mModes) {
		mode.mbBlockFlag = reader(1) != 0;

		const uint16_t windowType = reader(16);
		if (windowType != 0)
			throw ATVorbisException("Unsupported window type in header");

		const uint16_t transformType = reader(16);
		if (transformType != 0)
			throw ATVorbisException("Unsupported transform type in header");

		mode.mMapping = reader(8);

		if (mode.mMapping >= mappingCount)
			throw ATVorbisException("Invalid mapping selected in mode");
	}

	if (!reader(1))
		throw ATVorbisException("Framing error on setup packet");
}

bool ATVorbisDecoder::ReadAudioPacket() {
	if (!BeginPacket())
		return false;

	ATVorbisBitReader reader(*this);
	if (reader(1))
		throw ATVorbisException("Invalid audio packet");

	uint8_t modeIdx = reader(mNumModeBits);
	if (modeIdx >= mNumModes)
		throw ATVorbisException("Invalid mode selected in audio packet");

	const ModeInfo& modeInfo = mModes[modeIdx];
	const MappingInfo& mapping = mMappings[modeInfo.mMapping];

	// check for long window -- note that we may not actually have two different window sizes
	const uint32_t blockSizeBits = mBlockSizeBits[modeInfo.mbBlockFlag];
	const uint32_t blockSize = 1 << blockSizeBits;
	const uint32_t halfBlockSize = blockSize >> 1;

	if (modeInfo.mbBlockFlag) {
		// Long window -- read hybrid window flags. We don't actually use these flags
		// right now as we instead store the previous block state.
		[[maybe_unused]] const bool prevWindow = reader(1) == 0;
		[[maybe_unused]] const bool nextWindow = reader(1) == 0;
	}

	// Check for end-of-packet -- if we get one at this point, discard the packet
	if (reader.CheckEop())
		return true;

	// Set the portion of the residue buffers to use. The offset is set so that the
	// 1/4 point of the block, or the 1/2 point of the residue vector, is always in
	// the middle of the residue buffer.
	mResidueVectorOffset = 2048 - (halfBlockSize >> 1);

	// decode floor curves
	uint32_t invalidFloorCurves = DecodeFloorCurves(reader, mapping, halfBlockSize);

	// check if we had end-of-packet during floor curves -- this causes all residues
	// to be zeroed, which we do by just setting all curves to not decode
	if (reader.CheckEop())
		invalidFloorCurves = ~UINT32_C(0);

	// Nonzero vector propagation
	//
	// Check all channel couplings to see if there is a conflict. If both channels
	// in the coupling decode or don't decode, that's fine, but if only one of them
	// decodes, force the other to do so as well.
	uint32_t channelVectorsToNotDecode = invalidFloorCurves;

	for(uint32_t couplingStepIdx = 0; couplingStepIdx < mapping.mNumCouplingSteps; ++couplingStepIdx) {
		const CouplingStep& couplingStep = mapping.mCouplingSteps[couplingStepIdx];
		const uint32_t magnitudeChBit = UINT32_C(1) << couplingStep.mMagnitudeChannel;
		const uint32_t angleChBit = UINT32_C(1) << couplingStep.mAngleChannel;
		const uint32_t bothChBits = magnitudeChBit | angleChBit;

		if ((channelVectorsToNotDecode & bothChBits) != bothChBits)
			channelVectorsToNotDecode &= ~bothChBits;
	}

	// Decode residue vectors
	DecodeResidueVectors(reader, mapping, halfBlockSize, channelVectorsToNotDecode);

	// inverse channel coupling of residue vectors
	DecoupleChannels(mapping, halfBlockSize);
	
	// IMDCT + overlap-add
	//
	// We compute IMDCT and overlap-add with the previous block to produce the final
	// output. This is done in-place within the residue vector with a bit of tricky
	// layout to avoid copying.
	//
	// This is conceptually what we want to do:
	//  ________________                                 _______________+       .
	//  |               \___                         ___/               |       .
	//	|               .   \___                 ___/   .               |       .
    //  |               .       \___         ___/       .               |       .
    //  |               .           \___ ___/           .               |       .
    //  |               .            ___X___            .               |       .
    //  |               .        ___/   .   \___        .               |       .
    //  |               .    ___/       .       \___    .               |       .
    //  |               .___/           .           \___.               |       .
    //  + - - - - - - - + - - - - - - - + - - - - - - - + - - - - - - - +       .
	//                  |<------------ 1/4 ----- current block --------3/4
	// 1/4 ----- previous block ------ 3/4 ------------>|
	//
	// The 3/4 point of the previous block aligns with the 1/4 point of the
	// current block. The IMDCT's output is half redundant, however, so it would
	// be wasteful to generate the full output; instead, we only generate two
	// quarters and save off only one quarter. The other half of each block is
	// then reconstructed by symmetry during the overlap-add process.
	//
	// The tricky part is handling hybrid block overlaps where the prev and cur
	// blocks have different block sizes. In that case, the overlap area is the
	// determined by the short block size, and extra data is copied from the
	// longer block. The returned data is always from the halfway point of the
	// previous block to the current block, which always ends up being the
	// overlap window plus the extra data from the longer block.
	//
	// To accommodate this, the residue vector is arranged so that its halfway
	// point is in the middle of the residue buffer, and after an in-place
	// IMDCT, centers the third and second quarter blocks. The overlap buffer
	// is also similarly aligned in only holding Q3:
	//
	//	+ - - - - - - - - - - - - -+----+----+- - - - - - - - - - - - - +
	//                             | q3 | q2 | (short)
	//  |           +--------------+----+----+--------------+           | (residue/output buffer)
	//              |        q3         |        q2         | (long)
	//  + - - - - - +-------------------+-------------------+ - - - - - +
	//  |           |      prev q3      | (overlap buffer)
	//  + - - - - - + - - - - - - - - - +
	//
	// This is arranged so that the overlap-add process runs on the larger area
	// of the two block sizes, reconstructs q1 = -reverse(q2) and
	// prev_q4 = reverse(prev_q3), blends the two, and moves q3 into prev_q3 for
	// the next block's overlap-add operation. In the mixed block size case,
	// an additional copy is done to move either the unused part of prev_q3 into
	// the q3 area for long -> short, or the unused q3 -> prev_q3 to short -> long.
	// In the latter case, the extra part of q2 unused by the overlap-add operation
	// is already in the correct place for output.

	const uint32_t prevHalfBlockSize = (1U << mBlockSizeBits[mbLongPrevWindow ? 1 : 0]) >> 1;
	const bool overlapLongWindow = mbLongPrevWindow && modeInfo.mbBlockFlag;

	mbLongPrevWindow = modeInfo.mbBlockFlag;

	for(uint32_t ch = 0; ch < mNumChannels; ++ch) {
		auto& residueVec = mResidueVectors[ch];
		float *const residueVecMid = residueVec + 2048;
		const uint32_t n = halfBlockSize;
		const uint32_t n2 = halfBlockSize / 2;

		// Render the floor curve onto the residue vector.
		//
		// If the floor curve wasn't rendered, then it is zero and the resulting vector will also
		// be zero. This is independent from the do-not-decode status -- a channel may have been forced
		// to decode residue by coupling, but its floor vector will still be zero. In that case, we
		// can skip both the floor curve rendering and the IMDCT.
		if (invalidFloorCurves & (1 << ch)) {
			memset(residueVec + mResidueVectorOffset, 0, sizeof(float) * n);
		} else {
			RenderFloorCurve(mapping, ch, n);

			// Transform to time domain through the IMDCT. This gives us only half the output, the
			// third and second quarters; the first is the negated reverse of the second and the
			// fourth is the reverse of the third.
			mIMDCT[modeInfo.mbBlockFlag ? 1 : 0].Transform(residueVec + mResidueVectorOffset);
		}

		float *const prevEnd = std::end(mOverlapVector[ch]);

		// Check if this is the first block; if so, we only need to transform and store its
		// Q3, as no data will be returned. We've already produced Q2 as well which will be
		// discarded, but the cost is insignificant.
		if (!mbDropFirstBlock) {
			// Check if we are transitioning from a long block to a short block. In that case,
			// the previous block will have extra data that won't be used in the overlap-add
			// and needs to be copied into the output buffer. The data to be copied will be
			// in Q3 which is the format we use in the overlap buffer, so no reflection is
			// required.
			if (prevHalfBlockSize > halfBlockSize) {
				memcpy(
					residueVecMid - (prevHalfBlockSize >> 1),
					prevEnd - (prevHalfBlockSize >> 1),
					((prevHalfBlockSize - halfBlockSize) >> 1) * sizeof(float)
				);
			}

			// Overlap-add
			//
			// The 3/4 point of the previous window always aligns with the 1/4 point of the
			// current window, with the overlap window being determined by the smaller of the
			// two blocks.
			//
			const float *leftWindow = mHalfWindows[overlapLongWindow ? 1 : 0];
			const uint32_t quarterWindowSize = std::min(halfBlockSize, prevHalfBlockSize) >> 1;

			ATVorbisOverlapAdd(
				residueVecMid - quarterWindowSize,
				prevEnd - quarterWindowSize,
				leftWindow,
				quarterWindowSize);

			// Check if the current block size is longer than the previous one -- if so, we must
			// copy out the unused data in the third quarter that wasn't already copied by the
			// overlap routine. The extra data in the second quarter is already in the correct
			// place in the residue/output buffer.
			if (halfBlockSize > prevHalfBlockSize) {
				uint32_t postCopyLen = (halfBlockSize - prevHalfBlockSize) >> 1;

				memcpy(prevEnd - n2,
					residueVecMid - n2,
					postCopyLen * sizeof(float));
			}
		} else {
			// store q3 of current block for overlap with the next block
			memcpy(prevEnd - n2, residueVecMid - n2, sizeof(float) * n2);
		}
	}

	if (mbDropFirstBlock) {
		mOutputSamplesLeft = 0;
		mbDropFirstBlock = false;
	} else {
		mOutputSampleOffset = 2048 - (prevHalfBlockSize >> 1);
		mOutputSamplesLeft = (halfBlockSize + prevHalfBlockSize) >> 1;
	}

	mSampleCounter += mOutputSamplesLeft;

	// done

	DiscardRemainingPacket();
	return true;
}

void ATVorbisDecoder::ReadCompletePacket(void *dst, uint32_t len) {
	BeginPacket();
	ReadFromPacket(dst, len);
}

bool ATVorbisDecoder::BeginPacket() {
	uint32_t packetLen = 0;

	mbPacketEopReturned = false;
	mbPacketClosed = false;

	for(;;) {
		while(mNextSegmentIndex >= mNumSegments) {
			if (packetLen)
				goto have_packet;

			if (!ReadPage()) {
				mbPacketClosed = true;
				return false;
			}
		}

		uint8_t segmentLen = mPageHeaderAndSegmentTable[kPageHeaderLen + mNextSegmentIndex++];
		packetLen += segmentLen;

		if (segmentLen < 255) {
			// last segment in the packet has been reached, so mark end of packet
			mbPacketClosed = true;
			break;
		}
	}

have_packet:
	mPacketLenLeft = packetLen;
	mpPacketSrc = mpNextSegment;
	mpNextSegment += packetLen;

	return true;
}

void ATVorbisDecoder::ReadFromPacket(void *dst, uint32_t len) {
	while(len) {
		uint32_t tc = len < mPacketLenLeft ? len : mPacketLenLeft;

		if (tc) {
			memcpy(dst, mpPacketSrc, tc);
			mpPacketSrc += tc;
			mPacketLenLeft -= tc;
			dst = (char *)dst + tc;
			len -= tc;
		} else {
			if (mbPacketClosed || !BeginPacket())
				throw ATVorbisException("Unexpected end of packet encountered");
		}
	}
}

ATVorbisDecoder::PacketData ATVorbisDecoder::ContinuePacket(const uint8_t *used) {
	uint8_t *const prefixEnd = &mPageData[kPageDataPrefixLen];

	// If we already returned EoP once, we already gave the bitreader a full buffer of
	// dummy data -- which means that no unused data is going to be valid either.
	if (mbPacketEopReturned) {
		return PacketData {
			.mpSrc = prefixEnd,
			.mLen = kPageDataMainLen,
			.mbEop = true,
			.mEopOffset = 0
		};
	}

	// check for any leftover data and move it into the prefix buffer
	uint32_t prefixLen = 0;

	if (used) {
		prefixLen = (uint32_t)(mpPacketSrc - used);
		if (prefixLen > kPageDataPrefixLen)
			throw ATVorbisException("Overflow during packet switching");

		// note that this may actually move in _both_ directions, if we stopped within
		// the prefix buffer (!)
		memmove(prefixEnd - prefixLen, used, prefixLen);
	}


	for(;;) {
		// check if we have packet data left
		if (mPacketLenLeft) {
			// see if it is enough to fit into the prefix buffer -- if so, we need to accumulate
			// into it to handle the case where pages are too small to individually read through the
			// bit reader
			if (mPacketLenLeft + prefixLen <= kPageDataPrefixLen) {
				// yes -- shift down any current prefix data
				if (prefixLen) {
					memmove(
						prefixEnd - prefixLen - mPacketLenLeft,
						prefixEnd - prefixLen,
						prefixLen
					);
				}

				// move remaining packet data into the prefix buffer
				memmove(prefixEnd - mPacketLenLeft, mpPacketSrc, mPacketLenLeft);
				prefixLen += mPacketLenLeft;

				// clear remaining packet data length so we have no packet data left
				mPacketLenLeft = 0;
			} else {
				// check if we have any prefix data
				if (prefixLen) {
					// shift down the packet data to abut against the prefix, as it must be contiguous with
					// prefix data
					if (mpPacketSrc != prefixEnd) {
						memmove(prefixEnd, mpPacketSrc, mPacketLenLeft);
						mpPacketSrc = prefixEnd;
					}
				}

				// at this point we are guaranteed to have at least the max prefix bytes
				// available, so the bitreader can run
				break;
			}
		}

		// we have no packet data left -- go get a new packet
		if (mbPacketClosed || !BeginPacket()) {
			// end of packet reached
			mbPacketEopReturned = true;

			// return whatever data we have left along with dummy data to end of the
			// buffer, and set the EoP offset to the end of the prefix
			return PacketData {
				.mpSrc = prefixEnd - prefixLen,
				.mLen = prefixLen + kPageDataMainLen,
				.mbEop = true,
				.mEopOffset = prefixLen
			};
		}
	}

	PacketData pd {
		.mpSrc = mpPacketSrc - prefixLen,
		.mLen = mPacketLenLeft + prefixLen,
		.mbEop = false,
		.mEopOffset = 0
	};

	mpPacketSrc += mPacketLenLeft;
	mPacketLenLeft = 0;

	return pd;
}

void ATVorbisDecoder::DiscardRemainingPacket() {
	while(!mbPacketClosed)
		(void)ContinuePacket(nullptr);
}

bool ATVorbisDecoder::ReadPage() {
	if (mbEndOfStream)
		return false;

	if (27 != mpReadFn(mPageHeaderAndSegmentTable, 27)) {
		mbEndOfStream = true;
		return false;
	}

	if (memcmp(mPageHeaderAndSegmentTable, "OggS", 4))
		throw ATVorbisException("Invalid page signature");

	if (mPageHeaderAndSegmentTable[4] != 0)
		throw ATVorbisException("Invalid page version");

	[[maybe_unused]] const uint64_t pos = (uint64_t)mPageHeaderAndSegmentTable[6]
		+ ((uint64_t)mPageHeaderAndSegmentTable[7] << 8)
		+ ((uint64_t)mPageHeaderAndSegmentTable[8] << 16)
		+ ((uint64_t)mPageHeaderAndSegmentTable[9] << 24)
		+ ((uint64_t)mPageHeaderAndSegmentTable[10] << 32)
		+ ((uint64_t)mPageHeaderAndSegmentTable[11] << 40)
		+ ((uint64_t)mPageHeaderAndSegmentTable[12] << 48)
		+ ((uint64_t)mPageHeaderAndSegmentTable[13] << 56);

	[[maybe_unused]] const uint32_t serial = (uint32_t)mPageHeaderAndSegmentTable[14]
		+ ((uint32_t)mPageHeaderAndSegmentTable[15] << 8)
		+ ((uint32_t)mPageHeaderAndSegmentTable[16] << 16)
		+ ((uint32_t)mPageHeaderAndSegmentTable[17] << 24);

	[[maybe_unused]] const uint32_t seq = (uint32_t)mPageHeaderAndSegmentTable[18]
		+ ((uint32_t)mPageHeaderAndSegmentTable[19] << 8)
		+ ((uint32_t)mPageHeaderAndSegmentTable[20] << 16)
		+ ((uint32_t)mPageHeaderAndSegmentTable[21] << 24);

	const uint32_t checksum = (uint32_t)mPageHeaderAndSegmentTable[22]
		+ ((uint32_t)mPageHeaderAndSegmentTable[23] << 8)
		+ ((uint32_t)mPageHeaderAndSegmentTable[24] << 16)
		+ ((uint32_t)mPageHeaderAndSegmentTable[25] << 24);

	mPageHeaderAndSegmentTable[22] = 0;
	mPageHeaderAndSegmentTable[23] = 0;
	mPageHeaderAndSegmentTable[24] = 0;
	mPageHeaderAndSegmentTable[25] = 0;

	const uint32_t numSegments = mPageHeaderAndSegmentTable[26];

	uint32_t dataLen = 0;

	if (numSegments) {
		if (numSegments != mpReadFn(mPageHeaderAndSegmentTable + kPageHeaderLen, numSegments))
			throw ATVorbisException("Error reading page segment table");

		dataLen = std::accumulate(mPageHeaderAndSegmentTable + kPageHeaderLen, mPageHeaderAndSegmentTable + kPageHeaderLen + numSegments, UINT32_C(0));

		if (dataLen) {
			if (dataLen != mpReadFn(mPageData + kPageDataPrefixLen, dataLen))
				throw ATVorbisException("Error reading page data");
		}
	}

	mpNextSegment = &mPageData[kPageDataPrefixLen];
	mNextSegmentIndex = 0;
	mNumSegments = numSegments;

	if (!mbCRCCheckingDisabled) {
		const uint32 cchk = ATVorbisComputeCRC(mPageHeaderAndSegmentTable, 27 + numSegments, mPageData + kPageDataPrefixLen, dataLen);

		if (cchk != checksum)
			throw ATVorbisException("Checksum error");
	}

	return true;
}

VDNOINLINE uint32_t ATVorbisDecoder::DecodeFloorCurves(ATVorbisBitReader& reader, const MappingInfo& mapping, uint32_t halfBlockSize) {
	uint32_t invalidFloorCurves = 0;

	for(uint32_t ch=0; ch<mNumChannels; ++ch) {
		uint32_t submapIdx = mapping.mChannelMux[ch];
		uint32_t floorIdx = mapping.mSubmaps[submapIdx].mFloorEncoding;
		FloorInfo& floor = mFloors[floorIdx];
		
		bool used = false;
		if (!floor.mbFloorType1)
			used = DecodeFloorCurve0(reader, mapping, floor, ch, halfBlockSize);
		else
			used = DecodeFloorCurve1(reader, mapping, floor, ch, halfBlockSize);

		if (!used) {
			invalidFloorCurves |= UINT32_C(1) << ch;
		}
	}

	return invalidFloorCurves;
}

void ATVorbisDecoder::RenderFloorCurve(const MappingInfo& mapping, uint32_t ch, uint32_t n) {
	uint32_t submapIdx = mapping.mChannelMux[ch];
	uint32_t floorIdx = mapping.mSubmaps[submapIdx].mFloorEncoding;
	FloorInfo& floor = mFloors[floorIdx];
	DecodedFloorInfo& decodedFloor = mDecodedFloors[ch];
		
	if (!floor.mbFloorType1)
		RenderFloorCurve0(floor.mType0, decodedFloor.mType0, mResidueVectors[ch] + mResidueVectorOffset, n);
	else
		RenderFloorCurve1(floor.mType1, decodedFloor.mType1, mResidueVectors[ch] + mResidueVectorOffset, n);
}

// [6.2.2 packet decode]
bool ATVorbisDecoder::DecodeFloorCurve0(ATVorbisBitReader& reader, const MappingInfo& mapping, FloorInfo& floor, uint32_t ch, uint32_t halfBlockSize) {
	auto& floor0 = floor.mType0;
	auto& decodedFloor0 = mDecodedFloors[ch].mType0;

	uint64_t amplitude = reader.ReadVar64(floor0.mAmplitudeBits);
	if (!amplitude || reader.CheckEop())
		return false;

	const float amplitudeF = (float)amplitude;

	uint8_t codeBookIndex = reader(floor0.mNumBookBits);
	if (codeBookIndex >= floor0.mNumBooks)
		throw ATVorbisException("Invalid codebook index during floor 0 decode");

	ATVorbisCodeBook& codeBook = mCodeBooks[floor0.mBooks[codeBookIndex]];
	if (!codeBook.IsValidForVQ())
		throw ATVorbisException("Codebook specified for floor 0 decode is not valid in VQ context");

	uint32_t numCoeffsRead = 0;
	float last = 0;

	while(numCoeffsRead < floor0.mOrder) {
		const float *vals = codeBook.DecodeVQ(reader);

		for(uint32_t i = 0; i < codeBook.mDimension; ++i) {
			decodedFloor0.mCosCoeffs[numCoeffsRead] = cosf(last + vals[i]);

			// coeffs may run over with dim>1 -- this is allowed and ignored
			if (++numCoeffsRead >= floor0.mOrder)
				break;
		}

		last += vals[codeBook.mDimension - 1];
	}

	// end of packet during floor 0 decode results in the packet having zeroed output
	if (reader.CheckEop())
		return false;

	decodedFloor0.mLinearFloorScale = 0.11512925f
		* amplitudeF
		* (float)floor0.mAmplitudeOffset / (float)((uint64_t(1) << floor0.mAmplitudeBits) - 1)
		* ldexpf(1.0f, -(floor0.mOrder >> 1));	// factored out 2x per product loop iteration

	decodedFloor0.mLinearFloorOffset = -0.11512925f * (float)floor0.mAmplitudeOffset;
	return true;
}

void ATVorbisDecoder::RenderFloorCurve0(const Floor0Info& floor0, const DecodedFloor0Info& decodedFloor0, float *dst, uint32_t n) {
	// synthesize floor curve [6.2.3. curve computation]
	constexpr auto bark = [](float x) {
		return 13.1f * atanf(0.00074f * x) + 2.24f * atanf(0.0000000185f * (x*x)) + 0.0001f*x;
	};

	const auto map_i =
	[
		argScale = (float)floor0.mRate * 0.5f / (float)n,
		valScale = (float)floor0.mBarkMapSize / bark(0.5f * (float)floor0.mRate),
		bark,
		map_floor = (float)floor0.mBarkMapSize - 1.0f
	](uint32_t i) {
		return std::min<float>(floorf(bark((float)i * argScale) * valScale), map_floor);
	};

	// header decode ensures size > 0 and order >= 2
	const float w_scale = std::numbers::pi_v<float> / (float)floor0.mBarkMapSize;
	const uint32_t numTerms = floor0.mOrder >> 1;

	uint32_t i = 0;
	float map_last = map_i(0);

	const float linearFloorScale = decodedFloor0.mLinearFloorScale;
	const float linearFloorOffset = decodedFloor0.mLinearFloorOffset;

	while(i < n) {
		float w = map_last * w_scale;
		const float cos_w = cosf(w);
		const float cos2_w = cos_w * cos_w;

		float p, q;
		if (floor0.mOrder & 1) {
			p = 1.0f - cos2_w;
			q = 0.25f;

			for(uint32_t i = 0; i < numTerms; ++i) {
				float pdiff = decodedFloor0.mCosCoeffs[2*i+1] - cos_w;
				float qdiff = decodedFloor0.mCosCoeffs[2*i] - cos_w;
				p *= pdiff * pdiff;
				q *= qdiff * qdiff;
			}

			float qdiff2 = decodedFloor0.mCosCoeffs[2*numTerms] - cos_w;
			q *= 4.0f * (qdiff2 * qdiff2);
		} else {
			p = (1.0f - cos_w) * 0.5f;
			q = (1.0f + cos_w) * 0.5f;

			for(uint32_t i = 0; i < numTerms; ++i) {
				float pdiff = decodedFloor0.mCosCoeffs[2*i+1] - cos_w;
				float qdiff = decodedFloor0.mCosCoeffs[2*i] - cos_w;
				p *= pdiff * pdiff;
				q *= qdiff * qdiff;
			}
		}

		float linearFloorValue = expf(linearFloorScale / sqrtf(p + q) + linearFloorOffset);

		for(;;) {
			dst[i] *= linearFloorValue;
			if (++i >= n)
				break;

			// This could be precomputed, but floor 0 is so rare it's not worth it.
			float map_next = map_i(i);
			if (map_next != map_last) {
				map_last = map_next;
				break;
			}
		}
	}
}

bool ATVorbisDecoder::DecodeFloorCurve1(ATVorbisBitReader& reader, const MappingInfo& mapping, FloorInfo& floor, uint32_t ch, uint32_t halfBlockSize) {
	auto& floor1 = floor.mType1;
	auto& decodedFloor1 = mDecodedFloors[ch].mType1;
	const bool nonzero = reader();

	if (!nonzero || reader.CheckEop())
		return false;

	static constexpr uint32_t kRangeTable[4] { 256, 128, 86, 64 };
	static constexpr uint8_t kRangeBits[4] { 8, 7, 7, 6 };
	const uint32_t range = kRangeTable[floor.mType1.mMultiplier - 1];
	const uint32_t rangeBits = kRangeBits[floor.mType1.mMultiplier - 1];

	// read floor 0/1
	int32_t rawY[65];
	rawY[0] = (int32_t)std::min<uint32_t>(reader(rangeBits), range - 1);
	rawY[1] = (int32_t)std::min<uint32_t>(reader(rangeBits), range - 1);

	uint32_t n = 2;
	for(uint32_t partIdx = 0; partIdx < floor1.mNumPartitions; ++partIdx) {
		const uint32_t partClassIdx = floor1.mPartClass[partIdx];
		const uint32_t partClassDim = floor1.mClassDims[partClassIdx];
		const uint32_t subClassBits = floor1.mClassSubclassBits[partClassIdx];
		const uint32_t subClassMask = (1 << subClassBits) - 1;

		uint32_t cval = 0;
		if (subClassBits) {
			cval = mCodeBooks[floor1.mClassMasterBooks[partClassIdx]].DecodeIndex(reader);
		}

		// this shouldn't happen due to header decode having the same check
		if (n + partClassDim > std::size(decodedFloor1.mFloorY))
			throw ATVorbisException("Floor_Y vector overflow");

		for(uint32_t i=0; i<partClassDim; ++i) {
			uint8_t subClassBook = floor1.mSubclassBooks[partClassIdx][cval & subClassMask];
			cval >>= subClassBits;

			if (subClassBook) {
				rawY[n] = (int32_t)mCodeBooks[subClassBook - 1].DecodeIndex(reader);
			} else
				rawY[n] = 0;

			++n;
		}
	}

	// end of packet during curve decode causes zeroed output on all channels
	if (reader.CheckEop())
		return false;

	// raw X/Y points are now decoded -- now we need to interpolate them
	std::ranges::fill(decodedFloor1.mbStep2Flags, false);

	decodedFloor1.mbStep2Flags[0] = true;
	decodedFloor1.mbStep2Flags[1] = true;

	decodedFloor1.mFloorY[0] = (uint8_t)rawY[0];
	decodedFloor1.mFloorY[1] = (uint8_t)rawY[1];

	for(uint32_t i = 2; i < n; ++i) {
		const uint32_t loIdx = floor1.mNeighbors[i][0];
		const uint32_t hiIdx = floor1.mNeighbors[i][1];

		// predict by linear interpolation (render_point in spec)
		const int32_t x = floor1.mFloorX[i];
		const int32_t x0 = floor1.mFloorX[loIdx];
		const int32_t x1 = floor1.mFloorX[hiIdx];
		const int32_t y0 = decodedFloor1.mFloorY[loIdx];
		const int32_t y1 = decodedFloor1.mFloorY[hiIdx];
		const int32_t adx = x1 - x0;
		const int32_t dy = y1 - y0;
		const int32_t ady = abs(dy);
		const int32_t err = ady * (x - x0);
		const int32_t off = err / adx;
		const int32_t predicted = dy < 0 ? y0 - off : y0 + off;

		const int32_t val = rawY[i];
		const int32_t highroom = range - predicted;
		const int32_t lowroom = predicted;
		const int32_t room = highroom < lowroom ? highroom*2 : lowroom*2;

		int32_t finalY = predicted;
		if (val) {
			decodedFloor1.mbStep2Flags[loIdx] = true;
			decodedFloor1.mbStep2Flags[hiIdx] = true;
			decodedFloor1.mbStep2Flags[i] = true;

			if (val >= room) {
				if (highroom > lowroom)
					finalY = predicted + val - lowroom;
				else
					finalY = predicted - val + highroom - 1;
			} else {
				if (val & 1) {
					finalY = predicted - (val + 1) / 2;
				} else {
					finalY = predicted + (val / 2);
				}
			}
		}

		// clamp final Y points per recommendation at end of [7.2.4. curve computation].
		decodedFloor1.mFloorY[i] = (uint8_t)std::clamp<int32_t>(finalY, 0, range - 1);
	}

	return true;
}

void ATVorbisDecoder::RenderFloorCurve1(const Floor1Info& floor1, const DecodedFloor1Info& decodedFloor1, float *dst, uint32_t n) {

	int32_t hx = 0;
	int32_t lx = 0;
	int32_t ly = decodedFloor1.mFloorY[floor1.mSortOrder[0]] * floor1.mMultiplier;
	int32_t hy = 0;

	for(uint32_t i = 1; i < floor1.mNumValues; ++i) {
		const uint32_t idx = floor1.mSortOrder[i];

		if (decodedFloor1.mbStep2Flags[idx]) {
			// Floor Y values are clamped during decoding to within [0, range), where range is
			// defined as 256/multiplier. This means that all line endpoints are guaranteed to
			// lie within 0..255. To be safe, though, we clamp the table lookups anyway.
			hy = decodedFloor1.mFloorY[idx] * floor1.mMultiplier;
			hx = floor1.mFloorX[idx];
			ATVorbisRenderFloorLine(lx, ly, hx, hy, dst, n);
			lx = hx;
			ly = hy;
		}
	}

	if (hx < (int32_t)n) {
		const float v = g_ATVorbisInverseDbTable[hy];

		do {
			dst[hx] *= v;
		} while(++hx < (int32_t)n);
	}
}

VDNOINLINE void ATVorbisDecoder::DecodeResidueVectors(ATVorbisBitReader& reader, const MappingInfo& mapping, uint32_t halfBlockSize, uint32_t channelVectorsToNotDecode) {
	// Clear residue channel vectors
	for(uint32_t ch = 0; ch < mNumChannels; ++ch) {
		memset(mResidueVectors[ch] + mResidueVectorOffset, 0, sizeof(float) * halfBlockSize);
	}

	// Residues are decoded with channels in submap order, not channel order.
	for(uint32_t submapIdx = 0; submapIdx < mapping.mNumSubMaps; ++submapIdx) {
		const Submap& submap = mapping.mSubmaps[submapIdx];
		const uint8_t residueIdx = submap.mResidueEncoding;
		const ResidueInfo& residue = mResidues[residueIdx];

		// calculate residue range to decode
		uint32_t residueStart = residue.mResidueBegin;
		uint32_t residueEnd = residue.mResidueEnd;
		uint32_t residueSize = halfBlockSize;

		if (residue.mResidueType == 2)
			residueSize *= submap.mNumChannels;

		if (residueEnd > residueSize)
			residueEnd = residueSize;

		// compute number of partitions (yes, this must truncate per spec)
		const uint32_t residueLen = residueEnd > residueStart ? residueEnd - residueStart : 0;
		const uint32_t partitionCount = residueLen / residue.mPartitionSize;	// partition size > 0 by encoding

		if (!partitionCount)
			continue;

		// select the classbook
		const ATVorbisCodeBook& classbook = mCodeBooks[residue.mClassBook];

		// collect channels used by this submap and assign residue buffers
		uint32_t chInSubmap[kMaxChannels] {};
		float *subChVec[kMaxChannels] {};
		uint32_t numVecs = 0;
		uint32_t numVecsToNotDecode = 0;

		for(uint32_t ch = 0; ch < mNumChannels; ++ch) {
			if (mapping.mChannelMux[ch] == submapIdx) {
				chInSubmap[numVecs] = ch;
				subChVec[numVecs] = &mResidueVectors[ch][(residue.mResidueType == 2 ? 0 : residueStart) + mResidueVectorOffset];

				if (channelVectorsToNotDecode & (UINT32_C(1) << ch)) {
					if (residue.mResidueType != 2)
						continue;

					++numVecsToNotDecode;
				}

				++numVecs;
			}
		}

		if (numVecs == 0)
			continue;

		// for residue type 2, we must decode if any vectors are set to decode, but if all vectors are
		// not decided, then we skip
		if (numVecsToNotDecode == numVecs)
			continue;

		// Resize partition classification buffers -- we round the parition count up to the next multiple
		// of the classbook dimension since the last decode is allowed to run over.
		//
		// Note that dimension 0 is blocked in header decode.
		for(uint32_t vecIdx = 0; vecIdx < (residue.mResidueType == 2 ? 1 : numVecs); ++vecIdx)
			mResiduePartitionClasses[vecIdx].resize(((partitionCount + classbook.mDimension - 1) / classbook.mDimension) * classbook.mDimension);

		if (residue.mResidueType == 2)
			memset(mResidueDecodingBuffer, 0, sizeof(float)*numVecs*halfBlockSize); 

		// begin coding passes (up to 8; determined by residue cascade)
		for(uint32_t codingPassIdx = 0; codingPassIdx < 8; ++codingPassIdx) {
			uint32_t partitionIdx = 0;
			uint32_t partitionClassIdx = 0;

			// iterate over each partition in the coding pass
			while(partitionIdx < partitionCount) {
				// if this is the first pass, we need to decode the classifications for each partition; these
				// are then reused for subsequent encoding passes
				if (codingPassIdx == 0) {
					// decode classifications
					for(uint32_t vecIdx = 0; vecIdx < numVecs; ++vecIdx) {
						uint32_t classv = classbook.DecodeIndex(reader);

						for(uint32_t dim = classbook.mDimension; dim; --dim) {
							mResiduePartitionClasses[vecIdx][partitionClassIdx + (dim - 1)] = classv % residue.mNumClassifications;
							classv /= residue.mNumClassifications;
						}

						if (residue.mResidueType == 2)
							break;
					}

					partitionClassIdx += classbook.mDimension;
				}

				for(uint32_t dim = 0; dim < classbook.mDimension && partitionIdx < partitionCount; ++dim, ++partitionIdx) {
					if (residue.mResidueType == 2) {
						// residue type 2 - channel-interleaved row-major interleaved VQ coding

						const auto codeBookIndex = residue.mBooks[mResiduePartitionClasses[0][partitionIdx]][codingPassIdx];
						if (codeBookIndex < 0)
							continue;

						const ATVorbisCodeBook& vecCodeBook = mCodeBooks[codeBookIndex];
						if (!vecCodeBook.IsValidForVQ())
							throw ATVorbisException("Non-VQ codebook referenced in residue");

						// [8.4. residue 1] guarantees even divisibility, and this is implied for
						// residue 2 by [8.5. residue 2] relating residue 2 to residue 1.
						const uint32_t codeCount = residue.mPartitionSize / vecCodeBook.mDimension;
						if (residue.mPartitionSize % vecCodeBook.mDimension)
							throw ATVorbisException("Residue partition size is not a multiple of the codebook dimension");

						float *dst = mResidueDecodingBuffer + residue.mResidueBegin + partitionIdx * residue.mPartitionSize;

						DecodeResidue1(reader, vecCodeBook, dst, codeCount);
					} else {
						// residue type 0 or 1 -- loop over each vector
						for (uint32_t vecIdx = 0; vecIdx < numVecs; ++vecIdx) {
							const auto codeBookIndex = residue.mBooks[mResiduePartitionClasses[vecIdx][partitionIdx]][codingPassIdx];
							if (codeBookIndex < 0)
								continue;

							const ATVorbisCodeBook& vecCodeBook = mCodeBooks[codeBookIndex];
							if (!vecCodeBook.IsValidForVQ())
								throw ATVorbisException("Non-VQ codebook referenced in residue");

							// [8.3. residue 0] and [8.4. residue 1] guarantee that the partition size
							// is an even multiple of the codebook dimension.
							const uint32_t codeCount = residue.mPartitionSize / vecCodeBook.mDimension;
							if (residue.mPartitionSize % vecCodeBook.mDimension)
								throw ATVorbisException("Residue partition size is not a multiple of the codebook dimension");

							float *dst = subChVec[vecIdx] + partitionIdx * residue.mPartitionSize;
							if (residue.mResidueType == 0) {
								// residue type 0 - column-major interleaved VQ coding
								DecodeResidue0(reader, vecCodeBook, dst, codeCount);
							} else {
								// residue type 1 - row-major interleaved VQ coding
								DecodeResidue1(reader, vecCodeBook, dst, codeCount);
							}
						}
					}
				}
			}
		}

		// deinterleave residue 2
		if (residue.mResidueType == 2)
			ATVorbisDeinterleaveResidue(subChVec, mResidueDecodingBuffer, halfBlockSize, numVecs);
	}
}

void ATVorbisDecoder::DecodeResidue0(ATVorbisBitReader& reader, const ATVorbisCodeBook& codeBook, float *dst, uint32_t codeCount) {
	// The codebook is pre-validated for VQ by DecodeResidueVectors().

	if (codeBook.mDimension == 1)
		return DecodeResidue0Dim<1>(reader, codeBook, dst, codeCount);

	if (codeBook.mDimension == 2)
		return DecodeResidue0Dim<2>(reader, codeBook, dst, codeCount);

	if (codeBook.mDimension == 3)
		return DecodeResidue0Dim<3>(reader, codeBook, dst, codeCount);

	if (codeBook.mDimension == 4)
		return DecodeResidue0Dim<4>(reader, codeBook, dst, codeCount);

	for (uint32_t i = 0; i < codeCount; ++i) {
		const float *v = codeBook.DecodeVQ(reader);

		for (uint32_t j = 0; j < codeBook.mDimension; ++j)
			dst[i + j * codeCount] += *v++;
	}
}

template<uint32_t T_Dim>
void ATVorbisDecoder::DecodeResidue0Dim(ATVorbisBitReader& reader, const ATVorbisCodeBook& codeBook, float *dst, uint32_t codeCount) {
	static_assert(T_Dim > 0 && T_Dim <= 4);

	for (uint32_t i = 0; i < codeCount; ++i) {
		const float *v = codeBook.DecodeVQ(reader);

		dst[i] += v[0];

		if constexpr (T_Dim >= 2)
			dst[i + 1 * codeCount] += v[1];

		if constexpr (T_Dim >= 3)
			dst[i + 2 * codeCount] += v[2];

		if constexpr (T_Dim >= 4)
			dst[i + 3 * codeCount] += v[3];
	}
}

void ATVorbisDecoder::DecodeResidue1(ATVorbisBitReader& reader, const ATVorbisCodeBook& codeBook, float *dst, uint32_t codeCount) {
	// The codebook is pre-validated for VQ by DecodeResidueVectors().

	if (codeBook.mDimension == 1)
		return DecodeResidue1Dim<1, false>(reader, codeBook, dst, codeCount);

	if (codeBook.mDimension == 2) {
		if (codeBook.mbQuickOnly)
			return DecodeResidue1Dim<2, true>(reader, codeBook, dst, codeCount);
		else
			return DecodeResidue1Dim<2, false>(reader, codeBook, dst, codeCount);
	}

	if (codeBook.mDimension == 3)
		return DecodeResidue1Dim<3, false>(reader, codeBook, dst, codeCount);

	if (codeBook.mDimension == 4) {
		if (codeBook.mbQuickOnly)
			return DecodeResidue1Dim<4, true>(reader, codeBook, dst, codeCount);
		else
			return DecodeResidue1Dim<4, false>(reader, codeBook, dst, codeCount);
	}

	if (codeBook.mDimension == 8) {
		if (codeBook.mbQuickOnly)
			return DecodeResidue1Dim<8, true>(reader, codeBook, dst, codeCount);
		else
			return DecodeResidue1Dim<8, false>(reader, codeBook, dst, codeCount);
	}

	for (uint32_t i = 0; i < codeCount; ++i) {
		const float *v = codeBook.DecodeVQ(reader);

		if (reader.CheckEop())
			return;

		for (uint32_t j = 0; j < codeBook.mDimension; ++j)
			*dst++ += *v++;
	}
}

template<uint32_t T_Dim, bool T_QuickOnly>
void ATVorbisDecoder::DecodeResidue1Dim(ATVorbisBitReader& reader0, const ATVorbisCodeBook& codeBook, float *dst0, uint32_t codeCount) {
	auto reader = reader0;

	float *VDRESTRICT dst = dst0;

	for (uint32_t i = 0; i < codeCount; ++i) {
		const float *VDRESTRICT v = codeBook.DecodeVQDim<T_Dim, T_QuickOnly>(reader);

		if (reader.CheckEop())
			return;

		if constexpr (T_Dim == 2) {
#if defined(VD_CPU_x86) || defined(VD_CPU_X64)
			__m128 vec = _mm_castpd_ps(_mm_load_sd((const double *)v));
			__m128 dvec = _mm_add_ps(_mm_castpd_ps(_mm_load_sd((double *)dst)), vec);
			_mm_store_sd((double *)dst, _mm_castps_pd(dvec));
			dst += 2;
#elif defined(VD_CPU_ARM64)
			vst1_f32(dst, vadd_f32(vld1_f32(dst), vld1_f32(v)));
			dst += 2;
#else
			*dst++ += *v++;
			*dst++ += *v++;
#endif
		} else {
			for (uint32_t j = 0; j < T_Dim; ++j)
				*dst++ += *v++;
		}
	}

	reader0 = reader;
}

VDNOINLINE void ATVorbisDecoder::DecoupleChannels(const MappingInfo& mapping, uint32_t halfBlockSize) {
	for(uint32_t couplingIdxP1 = mapping.mNumCouplingSteps; couplingIdxP1; --couplingIdxP1) {
		const CouplingStep& couplingStep = mapping.mCouplingSteps[couplingIdxP1 - 1];

		float *angles = mResidueVectors[couplingStep.mAngleChannel] + mResidueVectorOffset;
		float *magnitudes = mResidueVectors[couplingStep.mMagnitudeChannel] + mResidueVectorOffset;

		ATVorbisDecoupleChannels(magnitudes, angles, halfBlockSize);
	}
}
