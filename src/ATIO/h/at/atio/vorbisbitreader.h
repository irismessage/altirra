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

#ifndef AT_ATIO_VORBISMISC_H
#define AT_ATIO_VORBISMISC_H

#include <vd2/system/binary.h>
#include "vorbismisc.h"

class ATVorbisBitDecoderBase {
public:
	struct PacketData {
		uint8_t *mpSrc;
		uint32_t mLen;

		// true if an end of packet is being returned
		bool mbEop;

		// byte offset from start at which end of packet occurs
		uint32_t mEopOffset;
	};

	PacketData RefillContinuePacket(const uint8_t *used);
};

///////////////////////////////////////////////////////////////////////////

// Ogg bitstream reader.
//
// Ogg consumes bits LSB first. A complication is that Vorbis codebooks
// are allowed to consume up to 32 bits; currently we force a 64-bit
// accumulator to allow for this, which imposes a penalty on 32-bit
// decoding.
//
class ATVorbisBitReader {
public:
	ATVorbisBitReader(ATVorbisBitDecoderBase& parent)
		: mParent(parent)
	{
		RefillSlow();
		Refill();
	}
	
	ATVorbisBitReader(const ATVorbisBitReader&) = default;

	// Peek at the next 32 bits in the bitstream without consuming any bits.
	VDFORCEINLINE uint32_t Peek32() const {
		return (uint32_t)mBitAccum;
	}

	// Consume bits in the bitstream and then refill the accumulator.
	void Consume(uint32_t bits) {
		mBitAccum >>= bits;
		mBitCount -= bits;

		Refill();
	}

	// Extract + consume a single bit and then refill.
	bool operator()() {
		const bool b = (mBitAccum & 1) != 0;
		mBitAccum >>= 1;
		--mBitCount;

		Refill();
		return b;
	}

	// Extract + consume a fixed number of bits (1-32) and then refill.
	uint32_t operator()(uint32_t bits) {
		const uint32_t v = (uint32_t)(mBitAccum & ((UINT64_C(1) << bits) - 1));

		mBitAccum >>= bits;
		mBitCount -= bits;

		Refill();

		return v;
	}

	// Extract + consume a fixed number of bits (1-64) and then refill.
	uint64_t ReadVar64(uint32_t bits) {
		const uint64_t v = mBitAccum & ((UINT64_C(1) << bits) - 1);

		if (bits >= 32) {
			bits &= 31;

			Consume(32);
		}

		mBitAccum >>= bits;
		mBitCount -= bits;

		Refill();

		return v;
	}

	// Read Vorbis-encoded floats. These use a different representation from IEEE floats.
	float ReadFloat();

	// Check for end of packet condition.
	bool CheckEop() const {
		// The bytes in the accumulator are folded into the EoP threshold for this to work.
		// This is simple as by design we _always_ have 7 bytes in the accumulator
		// (bitCount always 56-63).
		//
		// Note that we only want to flag EoP when the bitstream read has gone _past_ the
		// EoP.
		return mpSrc > mpSrcEopThreshold;
	}

	ATVorbisBitReader& operator=(const ATVorbisBitReader& other) {
		mBitAccum = other.mBitAccum;
		mBitCount = other.mBitCount;
		mpSrc = other.mpSrc;
		mpSrcLimit = other.mpSrcLimit;
		mpSrcEopThreshold = other.mpSrcEopThreshold;
		return *this;
	}

private:
	VDFORCEINLINE void Refill() {
		mBitAccum |= VDReadUnalignedLEU64(mpSrc) << mBitCount;
		mpSrc += (63 - mBitCount) >> 3;
		mBitCount |= 56;

		if (mpSrc >= mpSrcLimit) [[unlikely]] {
			auto x = *this;
			x.RefillSlow();
			*this = x;
		}
	}

	void RefillSlow();

	uint64_t mBitAccum = 0;
	uint32_t mBitCount = 0;

	const uint8_t *mpSrc = nullptr;
	const uint8_t *mpSrcLimit = nullptr;
	const uint8_t *mpSrcEopThreshold = nullptr;
	ATVorbisBitDecoderBase& mParent;
};

#endif
