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

#ifndef f_AT_ATIO_TAPEPIECETABLE_H
#define f_AT_ATIO_TAPEPIECETABLE_H

#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <at/atio/cassetteimage.h>
#include <at/atio/cassetteblock.h>

// The audio and data tracks are managed by an array of sorted refcounted
// block ranges that effectively form a piece table. Blocks may be trimmed
// at either end so that only a span of the block is referenced; this allows
// blocks to be partially overwritten in the list without requiring
// the blocks themselves to be modified. A sentinel is placed at the end to
// bracket the last block.
//
// The data within the image blocks is immutable. For efficiency, the blocks
// may be appended to, but the existing data is never overwritten. This
// simplifies the overwrite logic and makes undo reasonably implementable.
// The same block may be referenced in multiple disjoint spans, which is why
// they are refcounted.
//
struct ATTapeImageSpan {
	// Starting position of block in the piece table. Blocks must be
	// strictly ascending in position.
	uint32	mStart;

	// Offset within the block of the span that is actually referenced.
	// Must be within the actual size of the block.
	uint32	mOffset;

	ATCassetteImageBlockType mBlockType;
	ATCassetteImageBlock *mpImageBlock;

	static constexpr ATTapeImageSpan CreateEnd(uint32 pos) {
		return { pos, 0, kATCassetteImageBlockType_End, nullptr };
	}

	template<typename T>
	T& GetBlockAsChecked() const {
		VDASSERT(mBlockType == T::kBlockType);
		VDASSERT(mpImageBlock->GetBlockType() == T::kBlockType);

		return *static_cast<T *>(mpImageBlock);
	}

	bool InMiddleOfNonEndSpan(uint32 pos) const {
		return pos > mStart && mpImageBlock;
	}

	bool CanFuseWithNext(const ATTapeImageSpan& nextSpan) const {
		if (mBlockType == kATCassetteImageBlockType_Blank && nextSpan.mBlockType == kATCassetteImageBlockType_Blank)
			return true;

		return mpImageBlock == nextSpan.mpImageBlock && nextSpan.mOffset == mOffset + (nextSpan.mStart - mStart);
	}
};

////////////////////////////////////////////////////////////////////////////////
struct ATTapePieceList {
	vdfastvector<ATTapeImageSpan> mSpans;
	uint32 mSpanCount = 0;
	uint32 mLength = 0;

	ATTapePieceList() = default;
	ATTapePieceList(const ATTapePieceList&);
	vdnothrow ATTapePieceList(ATTapePieceList&&) vdnoexcept;
	~ATTapePieceList();

	ATTapePieceList& operator=(const ATTapePieceList&);
	vdnothrow ATTapePieceList& operator=(ATTapePieceList&&) vdnoexcept;
};

////////////////////////////////////////////////////////////////////////////////
class ATTapePieceTable final : public ATTapePieceList {
public:
	ATTapePieceTable();

	uint32 FindSpan(uint32 pos) const;
	ATCassetteRegionInfo GetRegionInfo(uint32 pos) const;

	void Clear();

	bool WriteBlankData(ATCassetteWriteCursor& cursor, uint32 len, bool insert);
	bool WriteStdData(ATCassetteWriteCursor& cursor, const uint8 *data, uint32 numBytes, uint32 baudRate, bool insert);
	uint32 EstimateWriteStdData(ATCassetteWriteCursor& cursor, uint32 numBytes, uint32 baudRate) const;

	struct Pulse {
		bool mbPolarity;
		uint32 mSamples;
	};

	bool WritePulses(ATCassetteWriteCursor& cursor, const Pulse *pulses, uint32 numPulses, uint32 numSamples, bool insert, bool fsk);

	uint32 InsertRange(uint32 start, const ATTapePieceList& clip);
	uint32 ReplaceRange(uint32 start, const ATTapePieceList& clip0);
	void DeleteRange(uint32 start, uint32 end);
	void CopyRange(uint32 start, uint32 len, ATTapePieceList& clip);

private:
	void Validate() const;
	uint32 SplitSpan(uint32 startBlockIdx, uint32 splitPt);

	ATCassetteImageBlock& InsertNewBlock(uint32 startBlockIdx, ATCassetteImageBlock *imageBlock);

	template<typename T>
	T& InsertNewBlock(uint32 startBlockIdx) {
		return static_cast<T&>(InsertNewBlock(startBlockIdx, new T));
	}

	void TrimSpans(uint32 startBlockIdx, uint32 pos);
	bool FuseSpans(uint32 blockIdx);
	void ShiftSpansRight(uint32 startBlockIdx, uint32 delta);
	void MergeBlankSpans(uint32 blockIdx);
	void WriteClampAndExtend(ATCassetteWriteCursor& cursor);
	void UpdateWriteCachedSpan(ATCassetteWriteCursor& cursor) const;

	mutable uint32 mCachedSpanIndex = 0;
};

#endif
