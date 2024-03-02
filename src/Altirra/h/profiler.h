//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2010 Avery Lee
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

#ifndef f_AT_PROFILER_H
#define f_AT_PROFILER_H

#include <vd2/system/linearalloc.h>
#include <at/atcore/scheduler.h>

class ATCPUEmulator;
class ATCPUEmulatorMemory;
class ATCPUEmulatorCallbacks;
class IATCPUTimestampDecoderProvider;

struct ATProfileRecord {
	uint32 mAddress;
	uint32 mCalls;
	uint32 mInsns : 29;
	uint32 mModeBits : 2;
	uint32 mEmulationMode : 1;
	uint32 mCycles;
	uint32 mUnhaltedCycles;
	uint32 mCounters[2];
};

struct ATProfileCallGraphRecord {
	uint32	mInsns;
	uint32	mCycles;
	uint32	mUnhaltedCycles;
	uint32	mCalls;
};

struct ATProfileCallGraphInclusiveRecord {
	uint32	mInclusiveCycles;
	uint32	mInclusiveUnhaltedCycles;
	uint32	mInclusiveInsns;
};

struct ATProfileCallGraphContext {
	uint32	mParent;
	uint32	mAddress;
};

void ATProfileComputeInclusiveStats(ATProfileCallGraphInclusiveRecord *dst, const ATProfileCallGraphRecord *src, const ATProfileCallGraphContext *contexts, size_t n);

struct ATProfileFrame {
	typedef vdfastvector<ATProfileRecord> Records;
	Records mRecords;
	Records mBlockRecords;

	typedef vdfastvector<ATProfileCallGraphRecord> CallGraphRecords;
	CallGraphRecords mCallGraphRecords;

	uint32	mTotalCycles;
	uint32	mTotalUnhaltedCycles;
	uint32	mTotalInsns;
};

class ATProfileSession {
	ATProfileSession(const ATProfileSession&) = delete;
	ATProfileSession& operator=(const ATProfileSession&) = delete;
public:
	ATProfileSession() = default;
	~ATProfileSession();

	ATProfileSession& operator=(ATProfileSession&& src);

	vdfastvector<ATProfileFrame *> mpFrames;

	typedef vdfastvector<ATProfileCallGraphContext> CGContexts;
	CGContexts mContexts;
};

enum ATProfileMode {
	kATProfileMode_Insns,
	kATProfileMode_Functions,
	kATProfileMode_CallGraph,
	kATProfileMode_BasicBlock,
	kATProfileMode_BasicLines,
	kATProfileModeCount
};

enum ATProfileCounterMode {
	kATProfileCounterMode_None,
	kATProfileCounterMode_BranchTaken,
	kATProfileCounterMode_BranchNotTaken,
	kATProfileCounterMode_PageCrossing,
	kATProfileCounterMode_RedundantOp,
};

enum ATProfileBoundaryRule {
	kATProfileBoundaryRule_None,
	kATProfileBoundaryRule_VBlank,
	kATProfileBoundaryRule_PCAddress,
	kATProfileBoundaryRule_PCAddressFunction
};

class ATCPUProfiler final : public IATSchedulerCallback {
	ATCPUProfiler(const ATCPUProfiler&) = delete;
	ATCPUProfiler& operator=(const ATCPUProfiler&) = delete;
public:
	ATCPUProfiler();
	~ATCPUProfiler();

	bool IsRunning() const { return mpUpdateEvent != NULL; }

	void SetBoundaryRule(ATProfileBoundaryRule rule, uint32 param, uint32 param2);

	void Init(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, ATCPUEmulatorCallbacks *callbacks, ATScheduler *scheduler, ATScheduler *slowScheduler, IATCPUTimestampDecoderProvider *tsdprovider);
	void Start(ATProfileMode mode, ATProfileCounterMode c1, ATProfileCounterMode c2);
	void BeginFrame();
	void EndFrame();
	void End();

	void GetSession(ATProfileSession& session);

protected:
	void OnScheduledEvent(uint32 id);
	void Update();
	void UpdateCallGraph();
	void AdvanceFrame(bool enableCollection);
	void AdvanceFrame(uint32 cycle, uint32 unhaltedCycle, bool enableCollection);
	void OpenFrame();
	void OpenFrame(uint32 cycle, uint32 unhaltedCycle);
	void CloseFrame();
	void CloseFrame(uint32 cycle, uint32 unhaltedCycle, bool keepFrame);
	void ClearContexts();
	void UpdateCounters(uint32 *p, const ATCPUHistoryEntry& he);
	uint32 ScanForFrameBoundary(uint32 n);

	IATCPUTimestampDecoderProvider *mpTSDProvider;
	ATCPUEmulator *mpCPU;
	ATCPUEmulatorMemory *mpMemory;
	ATCPUEmulatorCallbacks *mpCallbacks;
	ATScheduler *mpFastScheduler;
	ATScheduler *mpSlowScheduler;
	ATEvent *mpUpdateEvent;

	ATProfileMode mProfileMode;
	uint32 mStartCycleTime;
	uint32 mStartUnhaltedCycleTime;
	uint32 mNextFrameTime;
	uint32 mFramePeriod;
	uint32 mLastHistoryCounter;
	uint32 mTotalSamples;
	uint32 mTotalContexts;
	bool mbAdjustStackNext;
	bool mbCountersEnabled;
	bool mbKeepNextFrame;
	ATProfileCounterMode mCounterModes[2];
	uint8 mLastS;
	sint32	mCurrentFrameAddress;
	sint32	mCurrentContext;

	ATProfileBoundaryRule mBoundaryRule = kATProfileBoundaryRule_None;
	uint32	mBoundaryParam = 0;
	uint32	mBoundaryParam2 = 0;

	struct HashLink {
		HashLink *mpNext;
		ATProfileRecord mRecord;
	};

	struct CallGraphHashLink {
		CallGraphHashLink *mpNext;
		uint32	mParentContext;
		uint32	mScope;
		uint32	mContext;
	};

	VDLinearAllocator mHashLinkAllocator;
	VDLinearAllocator mCGHashLinkAllocator;
	ATProfileSession mSession;
	ATProfileFrame *mpCurrentFrame = nullptr;

	struct AddressHashTable {
		HashLink *mpBuckets[256];
	};

 	HashLink *mpHashTable[256];
 	HashLink *mpBlockHashTable[256];
	CallGraphHashLink *mpCGHashTable[256];

	sint32	mStackTable[256];
};

#endif	// f_AT_PROFILER_H
