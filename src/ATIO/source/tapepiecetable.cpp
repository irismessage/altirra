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

#include <stdafx.h>
#include <vd2/system/vdstl_vectorview.h>
#include <at/atio/tapepiecetable.h>

#ifdef _DEBUG
	#define AT_TAPE_PIECE_TABLE_VALIDATE() Validate()
#else
	#define AT_TAPE_PIECE_TABLE_VALIDATE() ((void)0)
#endif

ATTapePieceList::ATTapePieceList(const ATTapePieceList& src)
	: mSpans(src.mSpans)
{
	for(const ATTapeImageSpan& span : mSpans) {
		if (span.mpImageBlock)
			span.mpImageBlock->AddRef();
	}
}

vdnothrow ATTapePieceList::ATTapePieceList(ATTapePieceList&& src) vdnoexcept {
	operator=(std::move(src));
}

ATTapePieceList::~ATTapePieceList() {
	while(!mSpans.empty()) {
		ATCassetteImageBlock *p = mSpans.back().mpImageBlock;

		if (p)
			p->Release();

		mSpans.pop_back();
	}
}

ATTapePieceList& ATTapePieceList::operator=(const ATTapePieceList& src) {
	if (&src != this) {
		for(const ATTapeImageSpan& span : mSpans) {
			if (span.mpImageBlock)
				span.mpImageBlock->Release();
		}

		mSpans.clear();
		mSpans = src.mSpans;
		mLength = src.mLength;
		mSpanCount = src.mSpanCount;

		for(const ATTapeImageSpan& span : mSpans) {
			if (span.mpImageBlock)
				span.mpImageBlock->AddRef();
		}		
	}

	return *this;
}

vdnothrow ATTapePieceList& ATTapePieceList::operator=(ATTapePieceList&& src) vdnoexcept {
	mSpans.swap(src.mSpans);
	std::swap(mLength, src.mLength);
	std::swap(mSpanCount, src.mSpanCount);

	return *this;
}

////////////////////////////////////////////////////////////////////////////////

ATTapePieceTable::ATTapePieceTable() {
	Clear();
}

uint32 ATTapePieceTable::FindSpan(uint32 pos) const {
	if (!pos)
		return 0;

	uint32 i = 0;
	uint32 j = mSpanCount;

	if (mCachedSpanIndex >= j)
		mCachedSpanIndex = j;

	if (pos < mSpans[mCachedSpanIndex].mStart)
		j = mCachedSpanIndex;
	else if (mCachedSpanIndex < j && pos >= mSpans[mCachedSpanIndex + 1].mStart)
		i = mCachedSpanIndex + 1;
	else
		return mCachedSpanIndex;

	while(i < j) {
		uint32 mid = (i + j + 1) >> 1;
		const ATTapeImageSpan *p = &mSpans[mid];

		if (pos < p->mStart)
			j = mid - 1;
		else
			i = mid;
	}

	mCachedSpanIndex = i;
	return i;
}

ATCassetteRegionInfo ATTapePieceTable::GetRegionInfo(uint32 pos) const {
	if (pos >= mLength)
		return ATCassetteRegionInfo { ATCassetteRegionType::Mark, pos, 1 };

	uint32 blockIdx = FindSpan(pos);

	const ATTapeImageSpan& blockInfo = mSpans[blockIdx];

	ATCassetteRegionInfo regionInfo;
	regionInfo.mRegionStart = blockInfo.mStart;
	regionInfo.mRegionLen = mSpans[blockIdx + 1].mStart - blockInfo.mStart;

	switch(blockInfo.mpImageBlock->GetBlockType()) {
		case kATCassetteImageBlockType_Blank:
		default:
			regionInfo.mRegionType = ATCassetteRegionType::Mark;
			break;

		case kATCassetteImageBlockType_Std:
			regionInfo.mRegionType = ATCassetteRegionType::DecodedData;
			break;

		case kATCassetteImageBlockType_FSK:
			regionInfo.mRegionType = ATCassetteRegionType::Raw;
			break;
	}

	return regionInfo;
}

void ATTapePieceTable::Clear() {
	ATTapePieceList newPieceList;

	newPieceList.mSpans.emplace_back(ATTapeImageSpan { 0, 0, kATCassetteImageBlockType_End, nullptr });

	static_cast<ATTapePieceList *>(this)->operator=(std::move(newPieceList));
}

bool ATTapePieceTable::WriteBlankData(ATCassetteWriteCursor& cursor, uint32 len0, bool insert) {
	// check if write would go beyond end and clamp
	bool truncated = false;

	if (cursor.mPosition > kATCassetteDataLimit) {
		cursor.mPosition = kATCassetteDataLimit;
		truncated = true;
	}

	uint32 len = std::min<uint32>(len0, insert ? kATCassetteDataLimit - mLength : kATCassetteDataLimit - cursor.mPosition);

	// if write cursor is beyond end, extend start to current end (we can't use
	// WriteClampAndExtend, because that calls us!)
	if (cursor.mPosition > mLength) {
		const uint32 extendLen = cursor.mPosition - mLength;

		cursor.mPosition = mLength;
		len += extendLen;
	}

	if (!len)
		return len0 == len;

	AT_TAPE_PIECE_TABLE_VALIDATE();

	// update cached block index
	UpdateWriteCachedSpan(cursor);

	// check if we are exactly between blocks
	if (mSpans[cursor.mCachedBlockIndex].mStart == cursor.mPosition) {
		// yes -- if the previous block is not a blank block, add a new one
		if (!cursor.mCachedBlockIndex || mSpans[cursor.mCachedBlockIndex - 1].mBlockType != kATCassetteImageBlockType_Blank) {
			InsertNewBlock<ATCassetteImageBlockBlank>(cursor.mCachedBlockIndex);
			++cursor.mCachedBlockIndex;
		}
	} else {
		// no -- if the current block is not a blank block, split and add a new one
		if (mSpans[cursor.mCachedBlockIndex].mBlockType != kATCassetteImageBlockType_Blank) {
			cursor.mCachedBlockIndex = SplitSpan(cursor.mCachedBlockIndex, cursor.mPosition);

			InsertNewBlock<ATCassetteImageBlockBlank>(cursor.mCachedBlockIndex);
		}

		++cursor.mCachedBlockIndex;
	}

	VDASSERT(cursor.mCachedBlockIndex > 0);
	VDASSERT(mSpans[cursor.mCachedBlockIndex - 1].mBlockType == kATCassetteImageBlockType_Blank);

	// advance write cursor
	cursor.mPosition += len;

	if (insert) {
		ShiftSpansRight(cursor.mCachedBlockIndex, len);
	} else {
		// truncate valid overlapping blocks
		TrimSpans(cursor.mCachedBlockIndex, cursor.mPosition);
	}

	// check if we have following blocks that are also blank -- if so, it is a waste
	// to have back-to-back blank blocks, and we should just coalesce them
	MergeBlankSpans(cursor.mCachedBlockIndex);

	// check if we actually crossed over into the next block -- if not, fall back so cached block is correct
	if (cursor.mPosition < mSpans[cursor.mCachedBlockIndex].mStart)
		--cursor.mCachedBlockIndex;

	AT_TAPE_PIECE_TABLE_VALIDATE();

	return len0 == len;
}

bool ATTapePieceTable::WriteStdData(ATCassetteWriteCursor& cursor, const uint8 *data, uint32 numBytes, uint32 baudRate, bool insert) {
	WriteClampAndExtend(cursor);

	if (!numBytes)
		return true;

	// check if we would go beyond end (with suitable buffer)
	if (cursor.mPosition >= kATCassetteDataLimit)
		return false;

	if (insert) {
		if (kATCassetteDataLimit - mLength < kATCassetteDataWriteByteBuffer)
			return false;
	} else {
		if (cursor.mPosition >= kATCassetteDataLimit || kATCassetteDataLimit - cursor.mPosition < kATCassetteDataWriteByteBuffer)
			return false;
	}

	AT_TAPE_PIECE_TABLE_VALIDATE();

	UpdateWriteCachedSpan(cursor);

	// split if we are in the middle of a block, even if it is compatible
	if (cursor.mPosition != mSpans[cursor.mCachedBlockIndex].mStart)
		cursor.mCachedBlockIndex = SplitSpan(cursor.mCachedBlockIndex, cursor.mPosition);

	// check if we have a previous block that is compatible and that we're
	// appending precisely at the end of, regardless of block trimming in the
	// block list
	bool needInsert = true;

	if (cursor.mCachedBlockIndex) {
		const auto& prevBlock = mSpans[cursor.mCachedBlockIndex - 1];

		// check that prev block is a std block
		if (prevBlock.mBlockType == kATCassetteImageBlockType_Std) {
			auto& prevStdBlock = *static_cast<ATCassetteImageDataBlockStd *>(prevBlock.mpImageBlock);

			// check that baud rate is compatible
			if (prevStdBlock.GetBaudRate() == baudRate) {
				// check that we are appending to the end of the original, untrimmed block
				if (prevStdBlock.GetDataSampleCount() == cursor.mPosition - prevBlock.mStart)
					needInsert = false;
			}
		}
	}

	if (needInsert) {
		InsertNewBlock<ATCassetteImageDataBlockStd>(cursor.mCachedBlockIndex).Init(baudRate);
		++cursor.mCachedBlockIndex;
	}

	// add the new data
	const auto& blockToExtend = mSpans[cursor.mCachedBlockIndex - 1];
	auto& stdBlockToExtend = blockToExtend.GetBlockAsChecked<ATCassetteImageDataBlockStd>();

	stdBlockToExtend.AddData(data, numBytes);

	// advance write cursor
	const uint32 newWriteCursor = blockToExtend.mStart + (stdBlockToExtend.GetDataSampleCount() - blockToExtend.mOffset);
	const uint32 delta = newWriteCursor - cursor.mPosition;
	VDASSERT(cursor.mPosition <= newWriteCursor);

	cursor.mPosition = newWriteCursor;

	if (insert) {
		ShiftSpansRight(cursor.mCachedBlockIndex, delta);
	} else {
		// trim or remove any blocks we overwrote (guaranteed unless we're at the end)
		TrimSpans(cursor.mCachedBlockIndex, cursor.mPosition);
	}

	AT_TAPE_PIECE_TABLE_VALIDATE();

	return true;
}

uint32 ATTapePieceTable::EstimateWriteStdData(ATCassetteWriteCursor& cursor, uint32 numBytes, uint32 baudRate) const {
	if (!numBytes)
		return 0;

	if (cursor.mPosition > mLength)
		return ATCassetteImageDataBlockStd::EstimateNewBlockLen(numBytes, baudRate);

	UpdateWriteCachedSpan(cursor);

	// split if we are in the middle of a block, even if it is compatible
	if (cursor.mPosition != mSpans[cursor.mCachedBlockIndex].mStart)
		return ATCassetteImageDataBlockStd::EstimateNewBlockLen(numBytes, baudRate);

	// check if we have a previous block that is compatible and that we're
	// appending precisely at the end of, regardless of block trimming in the
	// block list
	bool needInsert = true;

	if (cursor.mCachedBlockIndex) {
		const auto& prevBlock = mSpans[cursor.mCachedBlockIndex - 1];

		// check that prev block is a std block
		if (prevBlock.mBlockType == kATCassetteImageBlockType_Std) {
			auto& prevStdBlock = *static_cast<ATCassetteImageDataBlockStd *>(prevBlock.mpImageBlock);

			// check that baud rate is compatible
			if (prevStdBlock.GetBaudRate() == baudRate) {
				// check that we are appending to the end of the original, untrimmed block
				if (prevStdBlock.GetDataSampleCount() == cursor.mPosition - prevBlock.mStart)
					needInsert = false;
			}
		}
	}

	if (needInsert)
		return ATCassetteImageDataBlockStd::EstimateNewBlockLen(numBytes, baudRate);

	// add the new data
	const auto& blockToExtend = mSpans[cursor.mCachedBlockIndex - 1];
	const auto& stdBlockToExtend = blockToExtend.GetBlockAsChecked<ATCassetteImageDataBlockStd>();

	return stdBlockToExtend.EstimateAddData(numBytes);
}

bool ATTapePieceTable::WritePulses(ATCassetteWriteCursor& cursor, const Pulse *pulses, uint32 numPulses, uint32 samples0, bool insert, bool fsk) {
	// if write cursor is beyond end, insert an intermediate blank area
	WriteClampAndExtend(cursor);

	if (!samples0)
		return true;

	// truncate if the whole length won't fit
	uint32 samples = std::min<uint32>(samples0, insert ? kATCassetteDataLimit - mLength : kATCassetteDataLimit - cursor.mPosition);

	if (!samples)
		return false;

	// check if we need to split the current block
	UpdateWriteCachedSpan(cursor);

	// if we are not immediately at the end of an FSK block, split it -- this allows us to keep
	// FSK blocks as immutable for undo purposes
	bool needSplit = true;

	if (cursor.mCachedBlockIndex) {
		const ATTapeImageSpan& prevBlock = mSpans[cursor.mCachedBlockIndex - 1];
		const ATTapeImageSpan& nextBlock = mSpans[cursor.mCachedBlockIndex];

		if (prevBlock.mBlockType == kATCassetteImageBlockType_FSK &&
			nextBlock.mStart == cursor.mPosition &&
			(nextBlock.mStart - prevBlock.mStart) + prevBlock.mOffset == prevBlock.GetBlockAsChecked<ATCassetteImageBlockRawData>().GetDataSampleCount())
		{
			needSplit = false;
		}
	}

	if (needSplit) {
		cursor.mCachedBlockIndex = SplitSpan(cursor.mCachedBlockIndex, cursor.mPosition);
		InsertNewBlock<ATCassetteImageBlockRawData>(cursor.mCachedBlockIndex);
		++cursor.mCachedBlockIndex;
	}

	// we are now guaranteed to be after an FSK block we can directly append to -- add
	// the new data
	auto& rawBlock = mSpans[cursor.mCachedBlockIndex - 1].GetBlockAsChecked<ATCassetteImageBlockRawData>();

	uint32 samplesLeft = samples;
	for(uint32 i = 0; i < numPulses; ++i) {
		const Pulse& pulse = pulses[i];
		const uint32 pulseLen = std::min(pulse.mSamples, samplesLeft);

		if (fsk)
			rawBlock.AddFSKPulseSamples(pulse.mbPolarity, pulseLen);
		else
			rawBlock.AddDirectPulseSamples(pulse.mbPolarity, pulseLen);

		samplesLeft -= pulseLen;
		if (!samplesLeft)
			break;
	}

	// advance write cursor
	cursor.mPosition += samples;

	// truncate or shift valid overlapping blocks
	if (insert)
		ShiftSpansRight(cursor.mCachedBlockIndex, samples);
	else
		TrimSpans(cursor.mCachedBlockIndex, cursor.mPosition);

	AT_TAPE_PIECE_TABLE_VALIDATE();
	return samples == samples0;
}

uint32 ATTapePieceTable::InsertRange(uint32 start, const ATTapePieceList& clip) {
	ATCassetteWriteCursor cursor { start };
	const uint32 clipLength = clip.mSpans.back().mStart;

	WriteBlankData(cursor, clipLength, true);
	ReplaceRange(start, clip);

	return start + clipLength;
}

uint32 ATTapePieceTable::ReplaceRange(uint32 start, const ATTapePieceList& clip) {
	ATCassetteWriteCursor cursor { start };
	
	WriteClampAndExtend(cursor);

	if (start >= kATCassetteDataLimit)
		return start;

	if (clip.mSpans.size() <= 1)
		return start;

	AT_TAPE_PIECE_TABLE_VALIDATE();

	UpdateWriteCachedSpan(cursor);

	const uint32 insertIdx = SplitSpan(cursor.mCachedBlockIndex, start);
	VDASSERT(!insertIdx || mSpans[insertIdx - 1].mStart < start);

	const uint32 len = std::min<uint32>(clip.mSpans.back().mStart, kATCassetteDataLimit - start);

	auto it = std::upper_bound(clip.mSpans.begin(), clip.mSpans.end() - 1,
		ATTapeImageSpan { len - 1 },
		[](const ATTapeImageSpan& x, const ATTapeImageSpan& y) { return x.mStart < y.mStart; });

	uint32 numBlocks = (uint32)(it - clip.mSpans.begin());

	mSpans.insert(mSpans.begin() + insertIdx, clip.mSpans.begin(), it);

	for(ATTapeImageSpan& span : vdvector_view(mSpans.data() + insertIdx, numBlocks)) {
		span.mStart += start;
		span.mpImageBlock->AddRef();
	}

	mSpanCount += numBlocks;

	// remove or trim overlapping blocks
	TrimSpans(insertIdx + numBlocks, start + len);

	// fuse at beginning and end -- note that this may become a 3-way fuse for a single span
	// insert (A-B-C -> A+B-C -> A+B+C)
	FuseSpans(insertIdx + numBlocks);
	FuseSpans(insertIdx);
	AT_TAPE_PIECE_TABLE_VALIDATE();

	return start + len;
}

void ATTapePieceTable::DeleteRange(uint32 start, uint32 end) {
	end = std::min(end, mLength);

	if (start >= end)
		return;

	uint32 startBlock = SplitSpan(FindSpan(start), start);

	TrimSpans(startBlock, end);

	auto it = mSpans.begin() + startBlock;
	const uint32 len = end - start;

	for(;;) {
		VDASSERT(it->mStart >= end);
		it->mStart -= len;

		if (!it->mpImageBlock)
			break;

		++it;
	}

	mLength -= len;

	MergeBlankSpans(startBlock);

	AT_TAPE_PIECE_TABLE_VALIDATE();
}

void ATTapePieceTable::CopyRange(uint32 start, uint32 len, ATTapePieceList& clip) {
	if (start >= mLength || !len) {
		clip.mSpans.push_back(ATTapeImageSpan::CreateEnd(0));
		return;
	}

	len = std::min<uint32>(len, mLength - start);

	uint32 startSpanIdx = FindSpan(start);
	uint32 endSpanIdx = FindSpan(start + len);

	VDASSERT(startSpanIdx <= endSpanIdx);

	clip.mSpans.assign(mSpans.begin() + startSpanIdx, mSpans.begin() + endSpanIdx + (mSpans[endSpanIdx].InMiddleOfNonEndSpan(start + len) ? 1 : 0));

	for(ATTapeImageSpan& span : clip.mSpans) {
		if (span.mStart < start) {
			span.mOffset += start - span.mStart;
			span.mStart = start;
		}

		span.mStart -= start;

		if (span.mpImageBlock)
			span.mpImageBlock->AddRef();
	}

	clip.mSpans.push_back(ATTapeImageSpan::CreateEnd(len));
	clip.mSpanCount = clip.mSpans.size() - 1;
	clip.mLength = len;
}

void ATTapePieceTable::Validate() const {
	VDASSERT(mSpans.size() == mSpanCount + 1);
	VDASSERT(mSpans.front().mStart == 0);
	VDASSERT(!mSpans.back().mpImageBlock);
	VDASSERT(mLength == mSpans.back().mStart);

	uint32 pos = 0;
	uint8 lastType = 255;

	for(const ATTapeImageSpan& block : mSpans) {
		VDASSERT(block.mStart >= pos);
		VDASSERT(block.mStart <= kATCassetteDataLimit);

		pos = block.mStart + 1;

		// we should not have back-to-back blanks (harmless, but inefficient)
		VDASSERT(lastType != kATCassetteImageBlockType_Blank || block.mBlockType != kATCassetteImageBlockType_Blank);
		lastType = (uint8)block.mBlockType;

		VDASSERT(block.mOffset < UINT32_C(0x80000000));

		if (block.mpImageBlock) {
			VDASSERT(block.mpImageBlock->GetBlockType() == block.mBlockType);

			switch(block.mBlockType) {
				case kATCassetteImageBlockType_FSK:
					VDASSERT(((&block)[1].mStart - block.mStart) + block.mOffset <= block.GetBlockAsChecked<ATCassetteImageBlockRawData>().GetDataSampleCount());
					break;

				default:
					break;
			}
		} else {
			VDASSERT(block.mBlockType == kATCassetteImageBlockType_End);
			VDASSERT(&block == &mSpans.back());
		}
	}

	VDASSERT(mLength != kATCassetteDataLimit);
}

uint32 ATTapePieceTable::SplitSpan(uint32 startBlockIdx, uint32 splitPt) {
	VDASSERT(mSpans.size() == mSpanCount + 1);
	VDASSERT(startBlockIdx <= mSpanCount);

	const uint32 pos1 = mSpans[startBlockIdx].mStart;

	VDASSERT(splitPt >= pos1);

	if (splitPt == pos1)
		return startBlockIdx;

	const uint32 pos2 = mSpans[startBlockIdx + 1].mStart;
	VDASSERT(splitPt <= pos2);

	if (splitPt != pos2) {
		VDASSERT(startBlockIdx < mSpanCount);

		ATTapeImageSpan newBlock = mSpans[startBlockIdx];

		newBlock.mStart = splitPt;
		newBlock.mOffset += splitPt - pos1;

		mSpans.insert(mSpans.begin() + startBlockIdx + 1, newBlock);
		++mSpanCount;

		newBlock.mpImageBlock->AddRef();

		// We cannot validate here as we may have created back-to-back blanks.
		//AT_CASSETTE_VALIDATE();
	}
	
	return startBlockIdx + 1;
}

ATCassetteImageBlock& ATTapePieceTable::InsertNewBlock(uint32 startBlockIdx, ATCassetteImageBlock *imageBlock) {
	VDASSERT(startBlockIdx < mSpans.size());

	vdrefptr imageBlockPtr(imageBlock);

	auto it = mSpans.begin() + startBlockIdx;
	ATTapeImageSpan sb {
		.mStart = it->mStart,
		.mOffset = 0,
		.mBlockType = imageBlock->GetBlockType(),
		.mpImageBlock = imageBlock
	};

	mSpans.insert(it, sb);
	imageBlockPtr.release();

	++mSpanCount;

	// We can't validate here because we temporarily have an illegal zero-size
	// block.
	//AT_CASSETTE_VALIDATE();

	return *imageBlock;
}


void ATTapePieceTable::TrimSpans(uint32 startBlockIdx, uint32 pos) {
	uint32 blockIdx = startBlockIdx;
	uint32 blocksToRemove = 0;

	for(;;) {
		auto& block = mSpans[blockIdx];

		// stop if the next block no longer overlaps the write range
		if (block.mStart >= pos)
			break;

		// stop if the next block is the sentinel
		if (!block.mpImageBlock) {
			VDASSERT(block.mBlockType == kATCassetteImageBlockType_End);

			// extend end of tape and stop
			block.mStart = pos;
			mLength = pos;
			break;
		}

		// check if the next block is entirely contained in the write
		// range
		auto& block2 = mSpans[blockIdx + 1];

		if (block2.mStart > pos) {
			// no -- trim and stop
			const uint32 truncOffset = pos - block.mStart;

			block.mOffset += truncOffset;
			block.mStart = pos;
			break;
		}

		// entirely contained -- mark the entry for deletion, release the
		// image block, and continue
		block.mpImageBlock->Release();
		block.mpImageBlock = nullptr;
		++blocksToRemove;
		++blockIdx;
	}

	// remove the entries for any blocks we deleted (pointers have already
	// been released)
	if (blocksToRemove) {
		mSpans.erase(mSpans.begin() + startBlockIdx, mSpans.begin() + (startBlockIdx + blocksToRemove));
		mSpanCount -= blocksToRemove;
	}
}

bool ATTapePieceTable::FuseSpans(uint32 blockIdx) {
	if (!blockIdx || blockIdx >= mSpanCount)
		return false;

	const ATTapeImageSpan& prevSpan = mSpans[blockIdx - 1];
	const ATTapeImageSpan& nextSpan = mSpans[blockIdx];

	VDASSERT(prevSpan.mpImageBlock);
	VDASSERT(prevSpan.mStart < nextSpan.mStart);

	if (prevSpan.CanFuseWithNext(nextSpan)) {
		VDASSERT(prevSpan.mBlockType == nextSpan.mBlockType);
		VDASSERT(prevSpan.mBlockType != kATCassetteImageBlockType_End);

		nextSpan.mpImageBlock->Release();
		mSpans.erase(mSpans.begin() + blockIdx);
		--mSpanCount;
		return true;
	}

	return false;
}

void ATTapePieceTable::ShiftSpansRight(uint32 startBlockIdx, uint32 delta) {
	delta = std::min<uint32>(delta, kATCassetteDataLimit - mLength);

	VDASSERT(startBlockIdx <= mSpanCount);
	auto it = mSpans.begin() + startBlockIdx;

	for(;;) {
		it->mStart += delta;
		if (!it->mpImageBlock) {
			VDASSERT(mLength == it->mStart - delta);
			mLength = it->mStart;
			break;
		}

		++it;
	}
}

void ATTapePieceTable::MergeBlankSpans(uint32 blockIdx) {
	if (!blockIdx || mSpans[blockIdx - 1].mBlockType != kATCassetteImageBlockType_Blank)
		return;

	uint32 trimIdxEnd = blockIdx;

	while(mSpans[trimIdxEnd].mBlockType == kATCassetteImageBlockType_Blank)
		++trimIdxEnd;

	if (trimIdxEnd > blockIdx)
		TrimSpans(blockIdx, mSpans[trimIdxEnd].mStart);
}

void ATTapePieceTable::WriteClampAndExtend(ATCassetteWriteCursor& cursor) {
	if (cursor.mPosition > kATCassetteDataLimit)
		cursor.mPosition = kATCassetteDataLimit;

	const uint32 pos = cursor.mPosition;
	if (pos > mLength) {
		cursor.mPosition = mLength;
		WriteBlankData(cursor, pos - mLength, false);
	}
}

void ATTapePieceTable::UpdateWriteCachedSpan(ATCassetteWriteCursor& cursor) const {
	if (cursor.mCachedBlockIndex < mSpanCount) {
		const uint32 blockStart = mSpans[cursor.mCachedBlockIndex].mStart;
		const uint32 blockEnd = mSpans[cursor.mCachedBlockIndex+1].mStart;

		if (cursor.mPosition >= blockStart && cursor.mPosition < blockEnd)
			return;
	}

	cursor.mCachedBlockIndex = FindSpan(cursor.mPosition);
}
