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

#include "scheduler.h"

class ATCPUEmulator;
class ATCPUEmulatorMemory;
class ATCPUEmulatorCallbacks;

struct ATProfileRecord {
	uint32 mAddress;
	uint32 mCalls;
	uint32 mInsns;
	uint32 mCycles;
};

struct ATProfileSession {
	typedef vdfastvector<ATProfileRecord> Records;
	Records mRecords;

	uint32	mTotalCycles;
	uint32	mTotalInsns;
};

enum ATProfileMode {
	kATProfileMode_Insns,
	kATProfileMode_Functions,
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
	void ClearSamples();

	ATCPUEmulator *mpCPU;
	ATCPUEmulatorMemory *mpMemory;
	ATCPUEmulatorCallbacks *mpCallbacks;
	ATScheduler *mpFastScheduler;
	ATScheduler *mpSlowScheduler;
	ATEvent *mpUpdateEvent;

	ATProfileMode mProfileMode;
	uint32 mStartCycleTime;
	uint32 mLastHistoryCounter;
	uint32 mTotalSamples;
	uint32 mTotalCycles;
	bool mbAdjustStackNext;
	uint8 mLastS;
	sint32	mCurrentFrameAddress;

	struct HashLink {
		HashLink *mpNext;
		ATProfileRecord mRecord;
	};

	HashLink *mpHashTable[256];

	sint32	mStackTable[256];
};

#endif	// f_AT_PROFILER_H
