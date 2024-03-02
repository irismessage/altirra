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

#ifndef f_AT_ATIO_AUDIOREADERFLAC_H
#define f_AT_ATIO_AUDIOREADERFLAC_H

#include <span>
#include <at/atcore/md5.h>
#include <at/atio/audioreader.h>

class ATAudioReaderFLAC final : public IATAudioReader {
public:
	ATAudioReaderFLAC(IVDRandomAccessStream& stream);

	void SetVerifyEnabled(bool enabled);

	void Init();

	uint64 GetDataSize() const override;
	uint64 GetDataPos() const override;
	uint64 GetFrameCount() const override;

	ATAudioReadFormatInfo GetFormatInfo() const override;
	uint32 ReadStereo16(sint16 *dst, uint32 n) override;

private:
	struct CRC8Table;
	struct CRC16Table;
	struct CRC16x2Table;
	struct CRCTables;

	struct BitPos {
		sint32 mBitPos = 0;
		const unsigned char *mpSrcEnd = nullptr;
	};

	class BitReader : public BitPos {
	public:
		BitReader(ATAudioReaderFLAC& parent) : mParent(&parent) {}

		VDFORCEINLINE uint32 GetBits(uint32 n);
		VDFORCEINLINE sint32 GetBitsSigned(uint32 n);

		template<bool T_HaveLzcnt>
		VDFORCEINLINE uint32 GetUnaryValue();

		VDFORCEINLINE void Refill();

		ATAudioReaderFLAC *mParent;
	};

	void ParseHeader();
	bool ParseFrame();
	void ParseSubFrame(BitReader& __restrict bitReader, std::span<sint32> buffer, uint32 frameBitsPerSample);

	void DecodeLeftDiff(std::span<sint32> ch0, std::span<sint32> ch1);
	void DecodeDiffRight(std::span<sint32> ch0, std::span<sint32> ch1);
	void DecodeMidSide(std::span<sint32> ch0, std::span<sint32> ch1);

	void ParseConstantSubFrame(BitReader& __restrict bitReader, std::span<sint32> buffer, uint32 frameBitsPerSample);
	void ParseVerbatimSubFrame(BitReader& __restrict bitReader, std::span<sint32> buffer, uint32 frameBitsPerSample);
	void ParseFixedSubFrame(BitReader& __restrict bitReader, std::span<sint32> buffer, uint32 frameBitsPerSample, uint32 order);
	void ParseLPCSubFrame(BitReader& __restrict bitReader, std::span<sint32> buffer, uint32 frameBitsPerSample, uint32 order);

	template<int Order>
	void ReconstructLPC(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift);

	template<int Order>
	void ReconstructLPC_Narrow_SSE2(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift);

	template<int Order>
	void ReconstructLPC_Narrow_SSSE3(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift);

	void ParseResiduals(BitReader& __restrict bitReader, std::span<sint32> buffer, uint32 order);

	bool TryRead(void *buf, uint32 bytes);
	void Read(void *buf, uint32 bytes);
	uint32 ReadRaw(void *buf, uint32 bytes);

	VDNOINLINE BitPos Refill(BitPos bitPos);
	VDNOINLINE void VerifyBlock();
	VDNOINLINE void VerifyEnd();

	void UpdateCRC16(const uint8 *p, size_t n);
	VDNOINLINE void UpdateCRC16_Scalar(const uint8 *p, size_t n);
	VDNOINLINE void UpdateCRC16_PCMUL(const uint8 *p, size_t n);
	bool FinalizeCRC16();

	IVDRandomAccessStream& mStream;
	ATAudioReadFormatInfo mFormatInfo {};

	uint32 mPos = 0;
	uint32 mLimit = 0;
	uint64 mBasePos = 0;
	uint32 mCRC16BasePos = 0;
	uint16 mCRC16 = 0;
	bool mbCRC16Enabled = false;
	bool mbVerify = false;
	bool mbStreamEnded = false;

	uint32 mStreamInfoSampleRate = 0;
	uint32 mStreamInfoChannels = 0;
	uint32 mStreamInfoBitsPerSample = 0;
	uint32 mStreamInfoBlockSize = 0;
	uint64 mSampleCount = 0;
	uint64 mDataLen = 0;

	uint32 mCurBlockSize = 0;
	uint32 mCurBlockPos = 0;
	std::vector<sint32> mBlocks;

	uint8 mMD5[16] {};

	ATMD5Engine mMD5Engine;

	// +16 bytes at the beginning for an overlap tail for bit reading, which we preserve when
	// scrolling.
	// +8 bytes to make bitreading more efficient.
	static constexpr uint32 kBlockSize = 65536;
	alignas(16) uint8 mBuf[kBlockSize + 16 + 8] {};
};

#endif
