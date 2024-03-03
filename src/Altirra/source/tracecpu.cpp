//	Altirra - Atari 800/800XL/5200 emulator
//	Execution trace data structures - CPU history tracing
//	Copyright (C) 2009-2017 Avery Lee
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

#include "stdafx.h"
#include <vd2/system/bitmath.h>
#include <vd2/system/math.h>
#include "tracecpu.h"

#define AT_PROFILE_CPUTRACE 0

struct ATTraceChannelCPUHistory::StaticProfiling {
#if AT_PROFILE_CPUTRACE
	static inline uint32 sMaskChangeCount[64] {};
	static inline uint32 sBlockCount = 0;
#endif

	static void Start();
	static void AddBlock();
	static void AddFirst(uint32 mask);
	static void Add(uint32 mask);
	static void Stop();
};

void ATTraceChannelCPUHistory::StaticProfiling::Start() {
#if AT_PROFILE_CPUTRACE
	for(auto& v : sMaskChangeCount)
		v = 0;
	
	sBlockCount = 0;
#endif
}

void ATTraceChannelCPUHistory::StaticProfiling::AddBlock() {
#if AT_PROFILE_CPUTRACE
	++sBlockCount;
#endif
}

void ATTraceChannelCPUHistory::StaticProfiling::AddFirst(uint32 mask) {
#if AT_PROFILE_CPUTRACE
	for(int i=0; i<32; ++i) {
		if (mask & (1 << i))
			++sMaskChangeCount[i];
	}
#endif
}

void ATTraceChannelCPUHistory::StaticProfiling::Add(uint32 mask) {
#if AT_PROFILE_CPUTRACE
	for(int i=0; i<32; ++i) {
		if (mask & (1 << i))
			++sMaskChangeCount[i + 32];
	}
#endif
}

void ATTraceChannelCPUHistory::StaticProfiling::Stop() {
#if AT_PROFILE_CPUTRACE
	static constexpr const char *kDesc[] = {
		"cycle",
		"cycle",
		"cycle",
		"cycle",
		"unhalted cycle",
		"unhalted cycle",
		"unhalted cycle",
		"unhalted cycle",
		"EA",
		"EA",
		"EA",
		"EA",
		"A",
		"X",
		"Y",
		"S",
		"PC",
		"PC",
		"P",
		"flags",
		"insn",
		"insn",
		"insn",
		"insn",
		"global PC base",
		"global PC base",
		"global PC base",
		"global PC base",
		"-",
		"-",
		"-",
		"-",
	};

	static_assert(std::size(kDesc) == 32);

	if (!sBlockCount)
		return;

	VDDEBUG2("=== CPUTrace profiling results ===\n");

	for(int i=0; i<32; ++i) {
		VDDEBUG2("%2d: %11u (%6.2f%%) | %11u (%6.2f%%) | %s\n"
			, i
			, sMaskChangeCount[i]
			, (float)sMaskChangeCount[i] / (float)sBlockCount * 100.0f
			, sMaskChangeCount[i + 32]
			, (float)sMaskChangeCount[i + 32] / ((float)sBlockCount * (float)(kBlockSize - 1)) * 100.0f
			, kDesc[i]
		);
	}

	VDDEBUG2("=== end ===\n");
#endif
}

ATTraceChannelCPUHistory::ATTraceChannelCPUHistory(uint64 tickOffset, double tickScale, const wchar_t *name, ATDebugDisasmMode disasmMode, uint32 subCycles, ATTraceMemoryTracker *memTracker, bool enableAsync) {
	mTickOffset = tickOffset;
	mTickScale = tickScale;
	mName = name;
	mDisasmMode = disasmMode;
	mSubCycles = subCycles;
	mpMemTracker = memTracker;
	mbAsyncEnabled = enableAsync;

	std::fill(std::begin(mUnpackedBlockIds), std::end(mUnpackedBlockIds), UINT32_MAX);

	for(uint32 i=0; i<vdcountof(mPseudoLRU); ++i)
		mPseudoLRU[i] = i;
}

void ATTraceChannelCPUHistory::BeginEvents() {
	StaticProfiling::Start();
}

void ATTraceChannelCPUHistory::AddEvent(uint64 tick, const ATCPUHistoryEntry& he) {
	if (mTailOffset >= kBlockSize) {
		mTailOffset = 0;
		++mTailBlockHeadNo;
		mTailBlockHeadNo &= kTailBlockMask;

		if (!(mTailBlockHeadNo & kAsyncChunkBlockMask)) {
			// This chunk should have been empty before we started filling it. Otherwise,
			// we're in trouble, we've overwritten a chunk in progress.
			const uint32 openingAsyncChunkIndex = (mTailBlockHeadNo & kTailBlockMask) / kAsyncChunkBlockCount;
			const uint32 closingAsyncChunkIndex = (openingAsyncChunkIndex - 1) & kAsyncChunkMask;
			VDASSERT(!mbAsyncPending[closingAsyncChunkIndex]);

			mAsyncFutures[closingAsyncChunkIndex] = std::async(
				mbAsyncEnabled ? std::launch::async | std::launch::deferred : std::launch::deferred,
				[this, startNo = closingAsyncChunkIndex * kAsyncChunkBlockCount] {
					for(int32 i = 0; i < kAsyncChunkBlockCount; ++i)
						PackBlockAsync((startNo + i) & kTailBlockMask);
				}
			);

			mbAsyncPending[closingAsyncChunkIndex] = true;

			// Ensure that the new chunk we're about to open is clear.
			if (mbAsyncPending[openingAsyncChunkIndex]) {
				VDASSERT(mAsyncChunkTail == openingAsyncChunkIndex);
				ProcessNextPendingChunk();
				VDASSERT(!mbAsyncPending[openingAsyncChunkIndex]);
			}
		}
	}

	// check if we are opening a new block
	if (mTailOffset == 0)
		mEventBlocks.emplace_back((tick - mTickOffset) * mTickScale, nullptr);

	mTailBlock[mTailBlockHeadNo & kTailBlockMask][mTailOffset++] = he;
	++mEventCount;
}

void ATTraceChannelCPUHistory::EndEvents() {
	ProcessPendingChunks();

	StaticProfiling::Stop();
}

void *ATTraceChannelCPUHistory::AsInterface(uint32 iid) {
	if (iid == ATTraceChannelCPUHistory::kTypeID)
		return this;

	return nullptr;
}

const wchar_t *ATTraceChannelCPUHistory::GetName() const {
	return mName.c_str();
}

double ATTraceChannelCPUHistory::GetDuration() const {
	// The tail offset is only wrapped before adding a new event, so if the tail
	// offset is 0, we never had any events.
	if (!mTailOffset)
		return 0;
	
	// Since we had at least one event, there is always a tail block
	return mEventBlocks.back().mTime + (double)(sint32)(mTailBlock[mTailBlockHeadNo][mTailOffset - 1].mCycle - mTailBlock[mTailBlockHeadNo][0].mCycle) * mTickScale;
}

bool ATTraceChannelCPUHistory::IsEmpty() const {
	return mTailOffset == 0;
}

void ATTraceChannelCPUHistory::StartIteration(double startTime, double endTime, double eventThreshold) {
}

bool ATTraceChannelCPUHistory::GetNextEvent(ATTraceEvent& ev) {
	return false;
}

void ATTraceChannelCPUHistory::StartHistoryIteration(double startTime, sint32 eventOffset) {
	sint64 pos = 0;

	if (startTime > 0 && mEventCount) {
		double timeOffset;
		const ATCPUHistoryEntry *he;
		size_t numEvents;
		uint32 blockId;

		auto itBlockStart = mEventBlocks.begin();
		auto itBlock = std::lower_bound(itBlockStart, mEventBlocks.end(), startTime,
			[](const EventBlock& block, double t) { return block.mTime < t; });

		blockId = (uint32)(itBlock - itBlockStart);

		if (!blockId) {
			// time is before first block
			mIterPos = 0;
			return;
		}

		--blockId;
		he = UnpackBlock(blockId);
		timeOffset = mEventBlocks[blockId].mTime;

		if (blockId + 1 == mEventBlocks.size())
			numEvents = mTailOffset;
		else
			numEvents = kBlockSize;

		// search events within block
		sint32 tickOffset = (sint32)((startTime - timeOffset) / mTickScale + 0.5);

		auto it = std::lower_bound(he, he + numEvents, tickOffset,
			[baseTick = he[0].mCycle](const ATCPUHistoryEntry& ev, sint32 offset) {
				return (sint32)(ev.mCycle - baseTick) < offset;
			}
		);

		pos = (uint32)(it - he) + (blockId << kBlockSizeBits);
	}

	pos += eventOffset;

	if (pos < 0)
		mIterPos = 0;
	else
		mIterPos = (uint32)std::min<uint64>(mEventCount, pos);
}

uint32 ATTraceChannelCPUHistory::ReadHistoryEvents(const ATCPUHistoryEntry **ppEvents, uint32 offset, uint32 n) {
	uint32 fetchPos = VDClampToUint32((sint64)mIterPos + offset);

	if (fetchPos >= mEventCount)
		return 0;

	n = std::min<uint32>(n, mEventCount - fetchPos);

	if (ppEvents) {
		uint32 left = n;

		while(left) {
			uint32 span = std::min<uint32>(left, kBlockSize - (fetchPos & (kBlockSize - 1)));
			left -= span;

			const ATCPUHistoryEntry *he = UnpackBlock(fetchPos >> kBlockSizeBits) + (fetchPos & (kBlockSize - 1));
			fetchPos += span;

			for(uint32 i=0; i<span; ++i)
				*ppEvents++ = he++;
		}
	}

	return n;
}

uint32 ATTraceChannelCPUHistory::FindEvent(double t) {
	if (!mEventCount)
		return 0;

	double timeOffset;
	const ATCPUHistoryEntry *he;
	size_t numEvents;
	uint32 blockId;

	const uint32 startBlockId = mIterPos >> kBlockSizeBits;

	auto itBlockStart = mEventBlocks.begin() + startBlockId;
	auto itBlock = std::upper_bound(itBlockStart, mEventBlocks.end(), t,
		[](double t, const EventBlock& block) { return t < block.mTime; });

	blockId = (uint32)(itBlock - itBlockStart);

	if (!blockId) {
		// time is before first block, so return start of iteration range
		return 0;
	}

	blockId += startBlockId - 1;
	he = UnpackBlock(blockId);
	timeOffset = mEventBlocks[blockId].mTime;
	numEvents = (blockId + 1 >= mEventBlocks.size()) ? mTailOffset : kBlockSize;

	// search events within block
	sint32 tickOffset = (sint32)((t - timeOffset) / mTickScale + 0.5);

	auto it = std::upper_bound(he, he + numEvents, tickOffset,
		[baseTick = he[0].mCycle](sint32 offset, const ATCPUHistoryEntry& ev) {
			return offset < (sint32)(ev.mCycle - baseTick);
		}
	);

	uint32 pos = (uint32)(it - he) + (blockId << kBlockSizeBits);

	if (pos) --pos;

	return pos < mIterPos ? 0 : pos - mIterPos;
}

double ATTraceChannelCPUHistory::GetEventTime(uint32 index) {
	if (index >= mEventCount - mIterPos)
		return GetDuration();

	index += mIterPos;

	const uint32 blockId = index >> kBlockSizeBits;
	const double blockTime = mEventBlocks[blockId].mTime;
	const ATCPUHistoryEntry *he = UnpackBlock(blockId);

	return (double)(sint32)(he[index & (kBlockSize - 1)].mCycle - he[0].mCycle) * mTickScale + blockTime;
}

const ATCPUTimestampDecoder& ATTraceChannelCPUHistory::GetTimestampDecoder() const {
	return mTimestampDecoder;
}

void ATTraceChannelCPUHistory::SetTimestampDecoder(const ATCPUTimestampDecoder& dec) {
	mTimestampDecoder = dec;
}

void ATTraceChannelCPUHistory::PackBlockAsync(uint32 blockIdx) {
	void *p;

	uint32 sizeDelta;

	auto& srcBlock = mTailBlock[blockIdx];

	if (kCompressionEnabled) {
		uint32 lastCycle = 0;
		uint32 lastUnhaltedCycle = 0;

		{
			ATCPUHistoryEntry *VDRESTRICT he = &srcBlock[0];

			for(uint32 i = 0; i < kBlockSize; ++i, ++he) {

				he->mCycle -= he->mUnhaltedCycle;

				uint32 cycle = he->mCycle;
				uint32 unhaltedCycle = he->mUnhaltedCycle;

				he->mCycle -= lastCycle;
				he->mUnhaltedCycle -= lastUnhaltedCycle;

				lastCycle = cycle;
				lastUnhaltedCycle = unhaltedCycle;
			}
		}

		static_assert(sizeof(ATCPUHistoryEntry) == 32, "struct layout problem");

		uint8 packBuffer[36 * kBlockSize];
		uint8 *VDRESTRICT dst = packBuffer;
		const uint8 *VDRESTRICT src = (const uint8 *)mTailBlock[blockIdx];
		uint8 checkBuffer[32] = {};
		uint8 insnBuffer[16] = {};
		uint8 eaPred[3] = {};

		for(uint32 i=0; i<kBlockSize; ++i) {
			uint32 deltaMask = 0;

			uint8 *maskPtr = dst;
			dst += 4;

			for(uint32 j=0; j<3; ++j)
				checkBuffer[20+j] = insnBuffer[(src[16] + j) & 15];

			if (src[11] & 0x80) {
				checkBuffer[8] = 0xFF;
				checkBuffer[9] = 0xFF;
				checkBuffer[10] = 0xFF;
			} else {
				checkBuffer[8] = eaPred[0];
				checkBuffer[9] = eaPred[1];
				checkBuffer[10] = eaPred[2];
			}

			for(uint32 j=0; j<32; ++j) {
				if (src[j] != checkBuffer[j]) {
					checkBuffer[j] = src[j];
					*dst++ = src[j];

					deltaMask |= 1 << j;
				}
			}

			for(uint32 j=0; j<3; ++j)
				insnBuffer[(src[16] + j) & 15] = checkBuffer[20+j];

			if (!(src[11] & 0x80)) {
				eaPred[0] = checkBuffer[8];
				eaPred[1] = checkBuffer[9];
				eaPred[2] = checkBuffer[10];
			}

			src += 32;
	
#if AT_PROFILE_CPUTRACE
			if (i)
				StaticProfiling::Add(deltaMask);
			else
				StaticProfiling::AddFirst(deltaMask);
#endif

			memcpy(maskPtr, &deltaMask, 4);
		}

		const uint32 packedSize = (uint32)(dst - packBuffer);

		vdsynchronized(mMutex) {
			p = mBlockAllocator.Allocate(packedSize, 1);
		}

		memcpy(p, packBuffer, packedSize);
		
		sizeDelta = packedSize;

#if AT_PROFILE_CPUTRACE
		StaticProfiling::AddBlock();
#endif
	} else {
		vdsynchronized(mMutex) {
			p = mBlockAllocator.Allocate(sizeof srcBlock);
		}

		memcpy(p, srcBlock, sizeof srcBlock);

		sizeDelta = sizeof srcBlock;
	}

	vdsynchronized(mMutex) {
		mTailBlockPackedSizes[blockIdx] = sizeDelta;
		mpTailBlockPackedPtrs[blockIdx] = p;
	}
}

void ATTraceChannelCPUHistory::FinalizePackChunk() {
	const uint32 blockIdx = mTailBlockTailNo & kTailBlockMask;
	uint32 totalSize = 0;

	vdsynchronized(mMutex) {
		for(uint32 i = 0; i < kAsyncChunkBlockCount; ++i) {
			totalSize += mTailBlockPackedSizes[blockIdx + i];

			const void *p = mpTailBlockPackedPtrs[blockIdx + i];
			VDASSERT(p);

			mEventBlocks[mTailBlockTailNo + i].mpPackedData = p;
		}
	}

	mTraceSize += totalSize;

	if (mpMemTracker)
		mpMemTracker->AddSize(totalSize);

	for(uint32 i = 0; i < kAsyncChunkBlockCount; ++i)
		mUnpackMap.push_back(0);

	mTailBlockTailNo += kAsyncChunkBlockCount;
}

void ATTraceChannelCPUHistory::ProcessPendingChunks() {
	// we must be careful to retire chunks in order
	while(ProcessNextPendingChunk())
		;
}

bool ATTraceChannelCPUHistory::ProcessNextPendingChunk() {
	if (!mbAsyncPending[mAsyncChunkTail])
		return false;

	mbAsyncPending[mAsyncChunkTail] = false;

	mAsyncFutures[mAsyncChunkTail].get();

	FinalizePackChunk();

	++mAsyncChunkTail;
	mAsyncChunkTail &= kAsyncChunkMask;
	return true;
}

const ATCPUHistoryEntry *ATTraceChannelCPUHistory::UnpackBlock(uint32 id) {
	// if the block ID is beyond the unpacked block map, it must be in
	// the tail blocks
	if (id >= mUnpackMap.size())
		return mTailBlock[id & kTailBlockMask];

	// check if we already have it unpacked
	uint8 slot = mUnpackMap[id];
	if (mUnpackedBlockIds[slot] == id) {
		// We must mark this slot as recently used in order to ensure that
		// a consecutive number of unpack requests smaller than the window
		// size is guaranteed to get that many unconflicting slots.
		mPseudoLRU[slot] = mLRUClock++;

		return mUnpackedBlocks[slot];
	}

	// find oldest slot
	uint32 oldestAge = 0;
	slot = 0;

	for(uint32 i=0; i<kUnpackedSlots; ++i) {
		const uint32 age = mLRUClock - mPseudoLRU[i];

		if (age > oldestAge) {
			oldestAge = age;
			slot = i;
		}
	}

	mPseudoLRU[slot] = mLRUClock++;

	// unpack into next slot
	mUnpackMap[id] = slot;
	mUnpackedBlockIds[slot] = id;

	ATCPUHistoryEntry *slotData = mUnpackedBlocks[slot];

	if (kCompressionEnabled) {
		uint8 unpackBuffer[32] = {};
		uint8 insnBuffer[16] = {};
		const uint8 *src = (const uint8 *)mEventBlocks[id].mpPackedData;
		uint32 cycle = 0;
		uint32 unhaltedCycle = 0;

		for(uint32 i=0; i<kBlockSize; ++i) {
			uint32 deltaMask;
			memcpy(&deltaMask, src, 4);
			src += 4;

			for(uint32 j=0; j<32; ++j) {
				if (deltaMask & (1 << j))
					unpackBuffer[j] = *src++;
			}

			const uint8 pcOffset = unpackBuffer[16];

			for(uint32 j=0; j<3; ++j) {
				if (!(deltaMask & (1 << (20 + j))))
					unpackBuffer[20+j] = insnBuffer[(pcOffset + j) & 15];
			}

			for(uint32 j=0; j<3; ++j)
				insnBuffer[(pcOffset + j) & 15] = unpackBuffer[20+j];

			memcpy(&slotData[i], unpackBuffer, 32);

			cycle = slotData[i].mCycle += cycle;
			unhaltedCycle = slotData[i].mUnhaltedCycle += unhaltedCycle;
			slotData[i].mCycle += unhaltedCycle;

			if (slotData[i].mEA & 0x80000000)
				slotData[i].mEA = 0xFFFFFFFF;
		}

	} else {
		memcpy(slotData, mEventBlocks[id].mpPackedData, sizeof(ATCPUHistoryEntry)*kBlockSize);
	}

	return slotData;
}
