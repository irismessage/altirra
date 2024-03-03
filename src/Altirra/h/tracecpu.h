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

#ifndef f_AT_TRACECPU_H
#define f_AT_TRACECPU_H

#include <future>
#include <vd2/system/linearalloc.h>
#include <vd2/system/thread.h>
#include <at/atcpu/history.h>
#include <at/atdebugger/target.h>
#include "trace.h"

class ATTraceChannelCPUHistory final : public vdrefcounted<IATTraceChannel> {
public:
	static const uint32 kTypeID = 'tcch';

	ATTraceChannelCPUHistory(uint64 tickOffset, double tickScale, const wchar_t *name, ATDebugDisasmMode disasmMode, uint32 subCycles, ATTraceMemoryTracker *memTracker, bool enableAsync);

	ATDebugDisasmMode GetDisasmMode() const { return mDisasmMode; }
	uint32 GetSubCycles() const { return mSubCycles; }
	
	void BeginEvents();
	void AddEvent(uint64 tick, const ATCPUHistoryEntry& he);
	void EndEvents();

	void *AsInterface(uint32 iid) override;

	const wchar_t *GetName() const override;
	double GetDuration() const override;
	bool IsEmpty() const override;
	void StartIteration(double startTime, double endTime, double eventThreshold) override;
	bool GetNextEvent(ATTraceEvent& ev) override final;

	double GetSecondsPerTick() const { return mTickScale; }
	void StartHistoryIteration(double startTime, sint32 eventOffset);
	uint32 ReadHistoryEvents(const ATCPUHistoryEntry **ppEvents, uint32 offset, uint32 n);
	uint32 FindEvent(double t);
	double GetEventTime(uint32 index);
	uint64 GetTraceSize() const { return mTraceSize; }
	uint32 GetEventCount() const { return mEventCount; }

	// Return the start time of the trace, in history cycle counter time. This is used
	// to rebias the mCycle value of history entries to compute trace time offset. It is
	// not necessarily the time of the first insn entry as that may start after the
	// trace starts by a few cycles. The history base cycle corresponds to trace time 0 (modulo
	// 2^32 wrapping).
	uint32 GetHistoryBaseCycle() const { return (uint32)mTickOffset; }

	const ATCPUTimestampDecoder& GetTimestampDecoder() const;
	void SetTimestampDecoder(const ATCPUTimestampDecoder& dec);

private:
	static constexpr uint32 kBlockSizeBits = 6;
	static constexpr uint32 kBlockSize = 1 << kBlockSizeBits;
	static constexpr uint32 kUnpackedSlots = 8;

	struct StaticProfiling;

	struct EventBlock {
		// starting time of block
		double mTime;

		// pointer to packed data, or null if block is a tail block
		const void *mpPackedData;
	};

	void PackBlockAsync(uint32 blockIdx);
	void FinalizePackChunk();
	void ProcessPendingChunks();
	bool ProcessNextPendingChunk();

	const ATCPUHistoryEntry *UnpackBlock(uint32 id);

	// List of all event blocks. This includes tail blocks, which are added to
	// this list as soon as they are opened -- this means that all events are
	// represented regardless of whether they are pending or being filled.
	vdfastvector<EventBlock> mEventBlocks;

	// List of all packed blocks and whether they have been unpacked to a slot.
	// This list only has entries for packed blocks and does not include pending
	// or partial tail blocks.
	vdfastvector<uint8> mUnpackMap;

	// Tail blocks are blocks that are either incomplete or pending asynchronous
	// compression. They are available for event reads. Tail blocks are queued
	// up to 3 deep, with the last block being the one that is being filled out.
	//
	// Tail/head block management is only updated from the main thread.
	//
	// Note that tail blocks are rather small (64 entries), so they need to be
	// batched for async packing to be worthwhile. At ~400K instructions per
	// second real time with 64 insns/block (2K), packing one block at a time
	// would result in 6,250 tasks/sec with excessive overhead. We batch 256
	// blocks together into 512K chunks to give the worker threads enough to
	// work on at a time.
	//
	static constexpr uint32 kNumTailBlocks = 1024;
	static constexpr uint32 kTailBlockMask = kNumTailBlocks - 1;
	static constexpr uint32 kAsyncCount = 4;
	static constexpr uint32 kAsyncChunkBlockCount = kNumTailBlocks / kAsyncCount;
	static constexpr uint32 kAsyncChunkBlockMask = kAsyncChunkBlockCount - 1;
	static constexpr uint32 kAsyncChunkMask = kAsyncCount - 1;

	uint32 mTailOffset = 0;
	uint32 mTailBlockTailNo = 0;
	uint32 mTailBlockHeadNo = 0;
	bool mbAsyncPending[kAsyncCount] {};
	std::future<void> mAsyncFutures[kAsyncCount];

	// next chunk index to retire - [0, kAsyncCount)
	uint32 mAsyncChunkTail = 0;
	bool mbAsyncEnabled = false;

	uint8 mLRUClock = 0;
	uint32 mEventCount = 0;
	uint64 mTraceSize = 0;

	uint32 mIterPos = 0;
	double mTickScale = 0;
	uint64 mTickOffset = 0;
	ATDebugDisasmMode mDisasmMode = {};
	uint32 mSubCycles = 0;
	VDStringW mName;

	ATCPUTimestampDecoder mTimestampDecoder;

	uint32 mPseudoLRU[kUnpackedSlots] = {};
	uint32 mUnpackedBlockIds[kUnpackedSlots] = {};
	ATCPUHistoryEntry mUnpackedBlocks[kUnpackedSlots][kBlockSize] = {};
	ATCPUHistoryEntry mTailBlock[kNumTailBlocks][kBlockSize] = {};

	ATTraceMemoryTracker *mpMemTracker = nullptr;

	VDCriticalSection mMutex;
	uint32 mTailBlockPackedSizes[kNumTailBlocks] {};
	const void *mpTailBlockPackedPtrs[kNumTailBlocks] {};
	VDLinearAllocator mBlockAllocator { 512*1024 - 128 };

	static constexpr bool kCompressionEnabled = true;
};

#endif
