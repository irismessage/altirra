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

#ifndef f_AT_TRACEFILEENCODING_H
#define f_AT_TRACEFILEENCODING_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/refcount.h>
#include <at/atcore/savestate.h>
#include <at/atcore/snapshotimpl.h>
#include <at/atcpu/history.h>

class ATSaveStateMemoryBuffer;
struct ATCPUHistoryEntry;
class ATSavedTraceCPUChannelDetail;

class IATTraceFmtCodec : public IVDRefUnknown {
protected:
	~IATTraceFmtCodec() = default;

public:
	static constexpr uint32 kTypeID = "IATTraceFmtCodec"_vdtypeid;

	virtual void Validate(uint32 rowSize) const = 0;
	virtual void Encode(ATSaveStateMemoryBuffer& dst, const uint8 *src, uint32 rowSize, size_t rowCount) const = 0;
	virtual void Decode(const ATSaveStateMemoryBuffer& src, uint8 *dst, uint32 rowSize, size_t rowCount) const = 0;
};

class ATSavedTraceCodecNull final : public ATSnapExchangeObject<ATSavedTraceCodecNull, "ATSavedTraceCodecNull", IATTraceFmtCodec> {
public:
	template<ATExchanger T>
	void Exchange(T& ex) {
	}

	void Validate(uint32 rowSize) const override {}
	void Encode(ATSaveStateMemoryBuffer& dst, const uint8 *src, uint32 rowSize, size_t rowCount) const override;
	void Decode(const ATSaveStateMemoryBuffer& src, uint8 *dst, uint32 rowSize, size_t rowCount) const override;
};

class ATSavedTraceCodecSparse final : public ATSnapExchangeObject<ATSavedTraceCodecSparse, "ATSavedTraceCodecSparse", IATTraceFmtCodec> {
public:
	ATSavedTraceCodecSparse();

	template<ATExchanger T>
	void Exchange(T& ex) {
	}

	void Validate(uint32 rowSize) const override;
	void Encode(ATSaveStateMemoryBuffer& dst, const uint8 *src, uint32 rowSize, size_t rowCount) const override;
	void Decode(const ATSaveStateMemoryBuffer& src, uint8 *dst, uint32 rowSize, size_t rowCount) const override;

private:
	void Decode_Scalar(const ATSaveStateMemoryBuffer& src, uint8 *dst, uint32 rowSize, size_t rowCount) const;

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	void Decode_SSSE3(const ATSaveStateMemoryBuffer& src, uint8 *dst, uint32 rowSize, size_t rowCount) const;
	void Decode_SSE41_POPCNT_24(const ATSaveStateMemoryBuffer& buf, uint8 *dst, size_t rowCount) const;

	alignas(16) uint8 mBitCountTab[256];
	alignas(16) uint8 mShuffleTab[256][8];
#elif defined(VD_CPU_ARM64)
	void Decode_NEON(const ATSaveStateMemoryBuffer& src, uint8 *dst, uint32 rowSize, size_t rowCount) const;

	alignas(16) uint8 mBitCountTab[256];
	alignas(16) uint8 mShuffleTab[256][8];
#endif
};

////////////////////////////////////////////////////////////////////////////////

struct ATTraceFmtRowMask {
	vdfastvector<uint32> mMask;

	ATTraceFmtRowMask() = default;
	ATTraceFmtRowMask(uint32 rowSize) : mMask((rowSize + 31) >> 5, 0) {}

	void MarkCount(uint32 first, uint32 n) {
		[[maybe_unused]] size_t len = mMask.size();
		VDASSERT(first < len*32 && len*32 - first >= n && n > 0);

		uint32 last = first + n - 1;
		uint32 firstMask = ~UINT32_C(0) << (first & 31);
		uint32 lastMask = ~((~UINT32_C(0) << (last & 31)) << 1);
		uint32 firstIdx = first >> 5;
		uint32 lastIdx = last >> 5;

		if (firstIdx == lastIdx) {
			mMask[firstIdx] |= firstMask & lastMask;
		} else {
			mMask[firstIdx] |= firstMask;

			for(uint32 i = firstIdx + 1; i < lastIdx; ++i)
				mMask[i] = ~UINT32_C(0);

			mMask[lastIdx] |= lastMask;
		}
	}

	void Merge(const ATTraceFmtRowMask& other) {
		VDASSERT(mMask.size() == other.mMask.size());

		auto it1 = mMask.begin(), itEnd = mMask.end();
		auto it2 = other.mMask.cbegin();

		for(; it1 != itEnd; ++it1, ++it2) {
			*it1 |= *it2;
		}
	}

	bool Overlaps(const ATTraceFmtRowMask& other) const {
		VDASSERT(mMask.size() == other.mMask.size());
		auto it1 = mMask.cbegin(), itEnd = mMask.cend(), it2 = other.mMask.cbegin();

		for(; it1 != itEnd; ++it1, ++it2) {
			if (*it1 & *it2)
				return true;
		}

		return false;
	}
};

struct ATTraceFmtAccessMask {
	ATTraceFmtRowMask mReadMask;
	ATTraceFmtRowMask mWriteMask;
	uint32 mRowSize;

	ATTraceFmtAccessMask() = default;
	ATTraceFmtAccessMask(size_t rowSize)
		: mReadMask(rowSize)
		, mWriteMask(rowSize)
		, mRowSize(rowSize)
	{
	}

	void MarkRead(uint32 first, uint32 n);
	void MarkWrite(uint32 first, uint32 n);
	void MarkReadWrite(uint32 first, uint32 n);

	void Merge(const ATTraceFmtAccessMask& other);

	void Validate(uint32 first, uint32 n);

	bool CanSwapWith(const ATTraceFmtAccessMask& other) {
		// A can swap with B if:
		//	- A doesn't read anything B writes
		//	- B doesn't read anything A writes
		//	- A and B don't have overlapping write ranges.
		//
		// However, A and B can have overlapping read ranges.

		return !mReadMask.Overlaps(other.mWriteMask)
			&& !mWriteMask.Overlaps(other.mReadMask)
			&& !mWriteMask.Overlaps(other.mWriteMask);
	}
};

class IATTraceFmtPredictor : public IVDRefUnknown {
protected:
	~IATTraceFmtPredictor() = default;

public:
	static constexpr uint32 kTypeID = "IATTraceFmtPredictor"_vdtypeid;

	virtual void Validate(ATTraceFmtAccessMask& accessMask) const = 0;
	virtual IATTraceFmtPredictor *Clone() const = 0;
	virtual void Reset() = 0;
	virtual void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) = 0;
	virtual void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) = 0;
};

template<typename T, ATSerializationStaticName T_Name>
class ATSavedTracePredictorT : public ATSnapExchangeObject<T, T_Name, IATTraceFmtPredictor> {
public:
	IATTraceFmtPredictor *Clone() const override final {
		return new T(*static_cast<const T*>(this));
	}
};

class ATSavedTracePredictorXOR final : public ATSavedTracePredictorT<ATSavedTracePredictorXOR, "ATSavedTracePredictorXOR"> {
public:
	ATSavedTracePredictorXOR() = default;
	ATSavedTracePredictorXOR(uint32 offset, uint32 size);

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("offset", &mXorOffset);
		ex.Transfer("size", &mXorSize);
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mXorOffset = 0;
	uint32 mXorSize = 0;
	uint8 mPredBuf[32] {};
};

class ATSavedTracePredictorEA final : public ATSavedTracePredictorT<ATSavedTracePredictorEA, "ATSavedTracePredictorEA"> {
public:
	ATSavedTracePredictorEA() = default;
	ATSavedTracePredictorEA(uint32 offset);

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("offset", &mOffset);
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mOffset = 0;
	uint32 mPrev = 0;
};

class ATSavedTracePredictorPC final : public ATSavedTracePredictorT<ATSavedTracePredictorPC, "ATSavedTracePredictorPC"> {
public:
	ATSavedTracePredictorPC() = default;
	ATSavedTracePredictorPC(uint32 offset);

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("offset", &mOffset);
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mOffset = 0;
	uint16 mPrevPC = 0;

	uint16 mPredBuf[64*1024];
};

class ATSavedTracePredictorInsn final : public ATSavedTracePredictorT<ATSavedTracePredictorInsn, "ATSavedTracePredictorInsn"> {
public:
	ATSavedTracePredictorInsn() = default;
	ATSavedTracePredictorInsn(uint32 insnOffset, uint32 insnSize, uint32 pcOffset, uint32 flagsBitOffset);

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("pc_offset", &mPCOffset);
		ex.Transfer("insn_offset", &mInsnOffset);
		ex.Transfer("insn_size", &mInsnSize);
		ex.Transfer("flags_bit_offset", &mFlagsBitOffset);

		if constexpr(ex.IsReader) {
			if (mInsnSize == 0 || mInsnSize > 8)
				throw ATInvalidSaveStateException();

			if (8 - (mFlagsBitOffset * 7) < mInsnSize)
				throw ATInvalidSaveStateException();
		}
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	void Decode4(uint8 *dst, uint32 rowSize, size_t rowCount);
	void DecodeN(uint8 *dst, uint32 rowSize, size_t rowCount);

	uint32 mPCOffset = 0;
	uint32 mInsnOffset = 0;
	uint8 mInsnSize = 0;
	uint32 mFlagsBitOffset = 0;
	uint32 mFlagsOffset = 0;
	uint8 mFlagsShift = 0;

	uint8 mPrevFlags = 0;

	uint8 mPredBuf[64*1024];
};

class ATSavedTracePredictorHorizDelta16 final : public ATSavedTracePredictorT<ATSavedTracePredictorHorizDelta16, "ATSavedTracePredictorHorizDelta16"> {
public:
	ATSavedTracePredictorHorizDelta16() = default;
	ATSavedTracePredictorHorizDelta16(uint32 dstOffset, uint32 predOffset);

	uint32 GetDstOffset() const { return mDstOffset; }
	uint32 GetPredOffset() const { return mPredOffset; }

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("offset", &mDstOffset);
		ex.Transfer("pred_offset", &mPredOffset);
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mDstOffset = 0;
	uint32 mPredOffset = 0;
};

class ATSavedTracePredictorHorizDelta32 final : public ATSavedTracePredictorT<ATSavedTracePredictorHorizDelta32, "ATSavedTracePredictorHorizDelta32"> {
public:
	ATSavedTracePredictorHorizDelta32() = default;
	ATSavedTracePredictorHorizDelta32(uint32 dstOffset, uint32 predOffset);

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("offset", &mDstOffset);
		ex.Transfer("pred_offset", &mPredOffset);
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mDstOffset = 0;
	uint32 mPredOffset = 0;
};

class ATSavedTracePredictorVertDelta16 final : public ATSavedTracePredictorT<ATSavedTracePredictorVertDelta16, "ATSavedTracePredictorVertDelta16"> {
public:
	ATSavedTracePredictorVertDelta16() = default;
	ATSavedTracePredictorVertDelta16(uint32 offset, uint32 bias);

	uint32 GetOffset() const { return mOffset; }
	sint16 GetBias() const { return mBias; }

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("offset", &mOffset);
		ex.Transfer("bias", &mBias);
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mOffset = 0;
	sint16 mBias = 0;
	uint16 mPrev = 0;
};

class ATSavedTraceMergedPredictor : public vdrefcounted<IATTraceFmtPredictor> {
public:
	void *AsInterface(uint32 iid) override;
};

template<typename T>
class ATSavedTraceMergedPredictorT : public ATSavedTraceMergedPredictor {
public:
	IATTraceFmtPredictor *Clone() const override final {
		return new T(*static_cast<const T*>(this));
	}
};

class ATSavedTracePredictorHVDelta16x2 final : public ATSavedTraceMergedPredictorT<ATSavedTracePredictorHVDelta16x2> {
public:
	ATSavedTracePredictorHVDelta16x2(uint32 offset, uint32 bias);

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mOffset = 0;
	sint16 mBias = 0;
	uint16 mPrev1 = 0;
	uint16 mPrev2 = 0;
};

class ATSavedTracePredictorVertDelta32 final : public ATSavedTracePredictorT<ATSavedTracePredictorVertDelta32, "ATSavedTracePredictorVertDelta32"> {
public:
	ATSavedTracePredictorVertDelta32() = default;
	ATSavedTracePredictorVertDelta32(uint32 offset, uint32 bias);

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("offset", &mOffset);
		ex.Transfer("bias", &mBias);
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mOffset = 0;
	sint32 mBias = 0;
	uint32 mPrev = 0;
};

class ATSavedTracePredictorVertDelta8 final : public ATSavedTracePredictorT<ATSavedTracePredictorVertDelta8, "ATSavedTracePredictorVertDelta8"> {
public:
	ATSavedTracePredictorVertDelta8() = default;
	ATSavedTracePredictorVertDelta8(uint32 offset, uint32 count);

	uint32 GetOffset() const { return mOffset; }
	uint32 GetCount() const { return mCount; }

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("offset", &mOffset);
		ex.Transfer("count", &mCount);

		if constexpr(ex.IsReader) {
			if (!mCount)
				throw ATInvalidSaveStateException();
		}
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mOffset = 0;
	uint32 mCount = 0;
	uint8 mPredBuf[32] {};
};

class ATSavedTracePredictorDelta32TablePrev8 final : public ATSavedTracePredictorT<ATSavedTracePredictorDelta32TablePrev8, "ATSavedTracePredictorDelta32TablePrev8"> {
public:
	ATSavedTracePredictorDelta32TablePrev8() = default;
	ATSavedTracePredictorDelta32TablePrev8(uint32 valueOffset, uint32 opcodeOffset);

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("offset", &mValueOffset);
		ex.Transfer("op_offset", &mOpcodeOffset);
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mValueOffset = 0;
	uint32 mOpcodeOffset = 0;
	uint8 mPrevOp = 0;
	uint32 mPredBuf[256] {};
};

class ATSavedTracePredictorDelta16TablePrev8 final : public ATSavedTracePredictorT<ATSavedTracePredictorDelta16TablePrev8, "ATSavedTracePredictorDelta16TablePrev8"> {
public:
	ATSavedTracePredictorDelta16TablePrev8() = default;
	ATSavedTracePredictorDelta16TablePrev8(uint32 valueOffset, uint32 opcodeOffset);

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("offset", &mValueOffset);
		ex.Transfer("op_offset", &mOpcodeOffset);
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mValueOffset = 0;
	uint32 mOpcodeOffset = 0;
	uint8 mPrevOp = 0;
	uint16 mPredBuf[256] {};
};

class ATSavedTracePredictorXor32Table8 final : public ATSavedTracePredictorT<ATSavedTracePredictorXor32Table8, "ATSavedTracePredictorXor32Table8"> {
public:
	ATSavedTracePredictorXor32Table8() = default;
	ATSavedTracePredictorXor32Table8(uint32 valueOffset, uint32 opcodeOffset);

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("offset", &mValueOffset);
		ex.Transfer("pred_offset", &mPredOffset);
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mValueOffset = 0;
	uint32 mPredOffset = 0;
	uint32 mPredBuf[256] {};
};

class ATSavedTracePredictorXor32TablePrev16 final : public ATSavedTracePredictorT<ATSavedTracePredictorXor32TablePrev16, "ATSavedTracePredictorXor32TablePrev16"> {
public:
	ATSavedTracePredictorXor32TablePrev16() = default;
	ATSavedTracePredictorXor32TablePrev16(uint32 valueOffset, uint32 pcOffset);

	uint32 GetValueOffset() const { return mValueOffset; }
	uint32 GetPCOffset() const { return mPCOffset; }

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("offset", &mValueOffset);
		ex.Transfer("pc_offset", &mPCOffset);
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mValueOffset = 0;
	uint32 mPCOffset = 0;
	uint16 mPrevPC = 0;
	uint32 mPredBuf[64*1024] {};
};

class ATSavedTracePredictorXor32VertDeltaTablePrev16 final : public ATSavedTraceMergedPredictorT<ATSavedTracePredictorXor32VertDeltaTablePrev16> {
public:
	ATSavedTracePredictorXor32VertDeltaTablePrev16(uint32 valueOffset, uint32 opcodeOffset);

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mValueOffset = 0;
	uint32 mPCOffset = 0;
	uint16 mPrevPC = 0;
	uint32 mVPred = 0;
	uint32 mPredBuf[64*1024] {};
};

class ATSavedTracePredictorSignMag16 final : public ATSavedTracePredictorT<ATSavedTracePredictorSignMag16, "ATSavedTracePredictorSignMag16"> {
public:
	ATSavedTracePredictorSignMag16() = default;
	ATSavedTracePredictorSignMag16(uint32 offset);

	bool TryMerge(const ATSavedTracePredictorSignMag16& other);

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("offset", &mOffset);
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	template<size_t N>
	void EncodeT(uint8 *dst, uint32 rowSize, size_t rowCount);

	template<size_t N>
	void DecodeT(uint8 *dst, uint32 rowSize, size_t rowCount);

	uint32 mOffset = 0;
	uint32 mCount = 1;
};

class ATSavedTracePredictorSignMag32 final : public ATSavedTracePredictorT<ATSavedTracePredictorSignMag32, "ATSavedTracePredictorSignMag32"> {
public:
	ATSavedTracePredictorSignMag32() = default;
	ATSavedTracePredictorSignMag32(uint32 offset);

	template<ATExchanger T_Ex>
	void Exchange(T_Ex& ex) {
		ex.Transfer("offset", &mOffset);
	}

	void Validate(ATTraceFmtAccessMask& accessMask) const override;
	void Reset() override;
	void Encode(uint8 *dst, uint32 rowSize, size_t rowCount) override;
	void Decode(uint8 *dst, uint32 rowSize, size_t rowCount) override;

private:
	uint32 mOffset = 0;
};

////////////////////////////////////////////////////////////////////////////////

class ATSavedTraceCPUHistoryDecoder {
public:
	void Init(const ATSavedTraceCPUChannelDetail& info);

	void Decode(const uint8 *src, ATCPUHistoryEntry *hedst, size_t n);

private:
	void Decode_Scalar(const uint8 *src, ATCPUHistoryEntry *hedst, size_t n);

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	void Decode_SSSE3(const uint8 *src, ATCPUHistoryEntry *hedst, size_t n);
	void Decode_SSSE3_32(const uint8 *src, ATCPUHistoryEntry *hedst, size_t n);
#endif
#if defined(VD_CPU_ARM64)
	void Decode_NEON(const uint8 *src, ATCPUHistoryEntry *hedst, size_t n);
#endif

	uint32 mRowSize = 0;

	vdvector<std::pair<uint32, uint32>> mCopyPairs;
	uint32 mIrqOffset = 0;
	uint32 mNmiOffset = 0;
	uint8 mIrqMask = 0;
	uint8 mNmiMask = 0;
	uint32 mCycleStep = 0;
	uint32 mUnhaltedCycleStep = 0;
	uint32 mLastCycle = 0;
	uint32 mLastUnhaltedCycle = 0;
	uint32 mBaseCycle = 0;
	uint32 mBaseUnhaltedCycle = 0;

	ATCPUHistoryEntry mBaseEntry {};

	vdvector<uint8, vdaligned_alloc<uint8, 16>> mFastCopyMaps;
};

#endif
