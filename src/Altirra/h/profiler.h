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
#include "scheduler.h"

class ATCPUEmulator;
class ATCPUEmulatorMemory;
class ATCPUEmulatorCallbacks;

struct ATProfileRecord {
	uint32 mAddress;
	uint32 mCalls;
	uint32 mInsns : 29;
	uint32 mModeBits : 2;
	uint32 mEmulationMode : 1;
	uint32 mCycles;
	uint32 mUnhaltedCycles;
};

struct ATProfileCallGraphRecord {
	uint32	mParent;
	uint32	mAddress;
	uint32	mInsns;
	uint32	mCycles;
	uint32	mUnhaltedCycles;
	uint32	mCalls;
	uint32	mInclusiveCycles;
	uint32	mInclusiveUnhaltedCycles;
	uint32	mInclusiveInsns;
};

struct ATProfileSession {
	typedef vdfastvector<ATProfileRecord> Records;
	Records mRecords;

	typedef vdfastvector<ATProfileCallGraphRecord> CallGraphRecords;
	CallGraphRecords mCallGraphRecords;

	uint32	mTotalCycles;
	uint32	mTotalUnhaltedCycles;
	uint32	mTotalInsns;
};

enum ATProfileMode {
	kATProfileMode_Insns,
	kATProfileMode_Functions,
	kATProfileMode_CallGraph,
	kATProfileMode_BasicLines,
	kATProfileModeCount
};

class ATCPUProfiler : public IATSchedulerCallback {
	ATCPUProfiler(const ATCPUProfiler&);
	ATCPUProfiler& operator=(const ATCPUProfiler&);
public:
	ATCPUProfiler();
	~ATCPUProfiler();

	bool IsRunning() const { return mpUpdateEvent != NULL; }

	void Init(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, ATCPUEmulatorCallbacks *callbacks, ATScheduler *scheduler, ATScheduler *slowScheduler);
	void Start(ATProfileMode mode);
	void End();

	void GetSession(ATProfileSession& session);

protected:
	void OnScheduledEvent(uint32 id);
	void Update();
	void UpdateCallGraph();
	void ClearSamples();

	ATCPUEmulator *mpCPU;
	ATCPUEmulatorMemory *mpMemory;
	ATCPUEmulatorCallbacks *mpCallbacks;
	ATScheduler *mpFastScheduler;
	ATScheduler *mpSlowScheduler;
	ATEvent *mpUpdateEvent;

	ATProfileMode mProfileMode;
	uint32 mStartCycleTime;
	uint32 mStartUnhaltedCycleTime;
	uint32 mLastHistoryCounter;
	uint32 mTotalSamples;
	uint32 mTotalCycles;
	uint32 mTotalUnhaltedCycles;
	bool mbAdjustStackNext;
	uint8 mLastS;
	sint32	mCurrentFrameAddress;
	sint32	mCurrentContext;

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
	ATProfileSession mSession;

	HashLink *mpHashTable[256];
	CallGraphHashLink *mpCGHashTable[256];

	sint32	mStackTable[256];
};

#endif	// f_AT_PROFILER_H
