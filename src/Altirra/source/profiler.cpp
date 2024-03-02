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

#include "stdafx.h"
#include "cpu.h"
#include "cpumemory.h"
#include "console.h"
#include "profiler.h"
#include "scheduler.h"

ATCPUProfiler::ATCPUProfiler()
	: mpCPU(NULL)
	, mpMemory(NULL)
	, mpFastScheduler(NULL)
	, mpSlowScheduler(NULL)
{
	memset(mpHashTable, 0, sizeof mpHashTable);
	memset(mpCGHashTable, 0, sizeof mpCGHashTable);
}

ATCPUProfiler::~ATCPUProfiler() {
	ClearSamples();
}

void ATCPUProfiler::Init(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, ATCPUEmulatorCallbacks *callbacks, ATScheduler *scheduler, ATScheduler *slowScheduler) {
	mpCPU = cpu;
	mpMemory = mem;
	mpCallbacks = callbacks;
	mpFastScheduler = scheduler;
	mpSlowScheduler = slowScheduler;
}

void ATCPUProfiler::Start(ATProfileMode mode) {
	ClearSamples();

	mProfileMode = mode;
	mStartCycleTime = mpCallbacks->CPUGetCycle();
	mStartUnhaltedCycleTime = mpCallbacks->CPUGetUnhaltedCycle();
	mLastHistoryCounter = mpCPU->GetHistoryCounter();
	mTotalSamples = 0;
	mTotalCycles = 0;
	mTotalUnhaltedCycles = 0;
	mbAdjustStackNext = false;
	mLastS = mpCPU->GetS();
	mCurrentFrameAddress = mpCPU->GetInsnPC();

	if (mode == kATProfileMode_BasicLines)
		mpUpdateEvent = mpSlowScheduler->AddEvent(2, this, 2);
	else
		mpUpdateEvent = mpSlowScheduler->AddEvent(32, this, 1);

	if (mode == kATProfileMode_CallGraph) {
		mSession.mCallGraphRecords.resize(4);

		for(int i=0; i<4; ++i) {
			ATProfileCallGraphRecord& rmain = mSession.mCallGraphRecords[i];
			memset(&rmain, 0, sizeof rmain);
		}

		mSession.mCallGraphRecords[1].mAddress = 0x2000000;
		mSession.mCallGraphRecords[2].mAddress = 0x3000000;
		mSession.mCallGraphRecords[3].mAddress = 0x4000000;

		mCurrentContext = 0;
	}

	std::fill(mStackTable, mStackTable+256, -1);
}

void ATCPUProfiler::End() {
	Update();

	if (mpUpdateEvent) {
		mpSlowScheduler->RemoveEvent(mpUpdateEvent);
		mpUpdateEvent = NULL;
	}

	mTotalCycles = mpCallbacks->CPUGetCycle() - mStartCycleTime;
	mTotalUnhaltedCycles = mpCallbacks->CPUGetUnhaltedCycle() - mStartUnhaltedCycleTime;

	if (mProfileMode == kATProfileMode_CallGraph) {
		for(uint32 i = mSession.mCallGraphRecords.size(); i; --i) {
			ATProfileCallGraphRecord& cgr = mSession.mCallGraphRecords[i - 1];
			cgr.mInclusiveCycles += cgr.mCycles;
			cgr.mInclusiveUnhaltedCycles += cgr.mUnhaltedCycles;
			cgr.mInclusiveInsns += cgr.mInsns;

			if (cgr.mParent) {
				ATProfileCallGraphRecord& cgp = mSession.mCallGraphRecords[cgr.mParent];
				cgp.mInclusiveCycles += cgr.mInclusiveCycles;
				cgp.mInclusiveUnhaltedCycles += cgr.mInclusiveUnhaltedCycles;
				cgp.mInclusiveInsns += cgr.mInclusiveInsns;
			}
		}
	}
}

void ATCPUProfiler::GetSession(ATProfileSession& session) {
	Update();
	session.mRecords.clear();
	session.mCallGraphRecords.clear();

	session.mTotalCycles = mTotalCycles;
	session.mTotalUnhaltedCycles = mTotalUnhaltedCycles;
	session.mTotalInsns = mTotalSamples;

	if (mProfileMode == kATProfileMode_CallGraph) {
		session.mCallGraphRecords = mSession.mCallGraphRecords;
	}

	for(int i=0; i<256; ++i) {
		for(HashLink *hl = mpHashTable[i]; hl; hl = hl->mpNext)
			session.mRecords.push_back(hl->mRecord);
	}
}

void ATCPUProfiler::OnScheduledEvent(uint32 id) {
	if (id == 1) {
		mpUpdateEvent = mpSlowScheduler->AddEvent(32, this, 1);

		Update();
	} else if (id == 2) {
		mpUpdateEvent = mpSlowScheduler->AddEvent(2, this, 1);

		Update();
	}
}

void ATCPUProfiler::Update() {
	if (mProfileMode == kATProfileMode_CallGraph) {
		UpdateCallGraph();
		return;
	}

	uint32 nextHistoryCounter = mpCPU->GetHistoryCounter();
	uint32 counter = mLastHistoryCounter;
	uint32 count = (nextHistoryCounter - mLastHistoryCounter) & (mpCPU->GetHistoryLength() - 1);
	mLastHistoryCounter = nextHistoryCounter;

	if (!count)
		return;

	if (!mTotalSamples) {
		if (!--count)
			return;
	}

	uint32 lineNo = 0;

	if (mProfileMode == kATProfileMode_BasicLines) {
		uint32 lineAddr = (uint32)mpMemory->DebugReadByte(0x8A) + ((uint32)mpMemory->DebugReadByte(0x8B) << 8);
		lineNo = (uint32)mpMemory->DebugReadByte(lineAddr) + ((uint32)mpMemory->DebugReadByte(lineAddr + 1) << 8);
	}

	const ATCPUHistoryEntry *hentp = &mpCPU->GetHistory(count);
	for(uint32 i=count; i; --i) {
		const ATCPUHistoryEntry *hentn = &mpCPU->GetHistory(i - 1);
		uint32 cycles = (uint16)(hentn->mCycle - hentp->mCycle);
		uint32 unhaltedCycles = (uint16)(hentn->mUnhaltedCycle - hentp->mUnhaltedCycle);
		uint32 extpc = hentp->mPC + (hentp->mK << 16);
		uint32 addr = extpc;
		bool isCall = false;

		if (mProfileMode == kATProfileMode_BasicLines) {
			addr = lineNo;
		} else if (mProfileMode == kATProfileMode_Insns) {
			if (hentp->mP & AT6502::kFlagI)
				addr += 0x1000000;
		} else if (mProfileMode == kATProfileMode_Functions) {
			bool adjustStack = mbAdjustStackNext || hentp->mbIRQ || hentp->mbNMI;
			mbAdjustStackNext = false;

			uint8 opcode = hentp->mOpcode[0];
			switch(opcode) {
				case 0x20:		// JSR
				case 0x60:		// RTS
				case 0x40:		// RTI
				case 0x6C:		// JMP (abs)
					mbAdjustStackNext = true;
					break;
			}

			if (adjustStack) {
				uint32 newFrameMode = mCurrentFrameAddress & 0x7000000;
				if (hentp->mbNMI) {
					if ((hentp->mTimestamp & 0xfff00) < (248 << 8))
						newFrameMode = 0x3000000;
					else
						newFrameMode = 0x4000000;
				} else if (hentp->mbIRQ) {
					newFrameMode = 0x2000000;
				} else if (!(hentp->mP & AT6502::kFlagI)) {
					newFrameMode = 0;
				}

				sint8 sdir = hentp->mS - mLastS;
				if (sdir > 0) {
					// pop
					do {
						sint32 prevFrame = mStackTable[mLastS];

						if (prevFrame >= 0) {
							mCurrentFrameAddress = prevFrame;
							mStackTable[mLastS] = -1;
						}
					} while(++mLastS != hentp->mS);
				} else if (sdir < 0) {
					// push
					while(--mLastS != hentp->mS) {
						mStackTable[mLastS] = -1;
					}

					mStackTable[mLastS] = mCurrentFrameAddress;
					mCurrentFrameAddress = extpc | newFrameMode;
					isCall = true;
				} else {
					mCurrentFrameAddress = extpc | newFrameMode;
					isCall = true;
				}
			}

			addr = mCurrentFrameAddress;
		}

		uint32 hc = addr & 0xFF;
		HashLink *hh = mpHashTable[hc];
		HashLink *hl = hh;

		for(; hl; hl = hl->mpNext) {
			if (hl->mRecord.mAddress == addr)
				break;
		}

		if (!hl) {
			hl = mHashLinkAllocator.Allocate<HashLink>();
			hl->mpNext = hh;
			hl->mRecord.mAddress = addr;
			hl->mRecord.mCycles = 0;
			hl->mRecord.mUnhaltedCycles = 0;
			hl->mRecord.mInsns = 0;
			hl->mRecord.mModeBits = (hentp->mP >> 4) & 3;
			hl->mRecord.mEmulationMode = hentp->mbEmulation;
			hl->mRecord.mCalls = 0;
			mpHashTable[hc] = hl;
		}

		hl->mRecord.mCycles += cycles;
		hl->mRecord.mUnhaltedCycles += unhaltedCycles;
		++hl->mRecord.mInsns;

		if (isCall)
			++hl->mRecord.mCalls;

		hentp = hentn;
	}

	mTotalSamples += count;
}

void ATCPUProfiler::UpdateCallGraph() {
	uint32 nextHistoryCounter = mpCPU->GetHistoryCounter();
	uint32 counter = mLastHistoryCounter;
	uint32 count = (nextHistoryCounter - mLastHistoryCounter) & (mpCPU->GetHistoryLength() - 1);
	mLastHistoryCounter = nextHistoryCounter;

	if (!count)
		return;

	if (!mTotalSamples) {
		if (!--count)
			return;
	}

	uint32 lineNo = 0;

	const ATCPUHistoryEntry *hentp = &mpCPU->GetHistory(count);
	for(uint32 i=count; i; --i) {
		const ATCPUHistoryEntry *hentn = &mpCPU->GetHistory(i - 1);
		uint32 cycles = (uint16)(hentn->mCycle - hentp->mCycle);
		uint32 unhaltedCycles = (uint16)(hentn->mUnhaltedCycle - hentp->mUnhaltedCycle);

		bool adjustStack = mbAdjustStackNext || hentp->mbIRQ || hentp->mbNMI;
		mbAdjustStackNext = false;

		uint8 opcode = hentp->mOpcode[0];
		switch(opcode) {
			case 0x20:		// JSR
			case 0x60:		// RTS
			case 0x40:		// RTI
			case 0x6C:		// JMP (abs)
				mbAdjustStackNext = true;
				break;
		}

		if (adjustStack) {
			sint8 sdir = hentp->mS - mLastS;
			if (sdir > 0) {
				// pop
				do {
					sint32 prevContext = mStackTable[mLastS];

					if (prevContext >= 0) {
						mCurrentContext = prevContext;
						mStackTable[mLastS] = -1;
					}
				} while(++mLastS != hentp->mS);
			} else {
				if (sdir < 0) {
					// push
					while(--mLastS != hentp->mS) {
						mStackTable[mLastS] = -1;
					}

					mStackTable[mLastS] = mCurrentContext;
				}

				if (hentp->mbNMI) {
					if ((hentp->mTimestamp & 0xfff00) < (248 << 8))
						mCurrentContext = 2;
					else
						mCurrentContext = 3;
				} else if (hentp->mbIRQ)
					mCurrentContext = 1;

				const uint32 newScope = hentp->mPC + (hentp->mK << 16);
				const uint32 hc = newScope & 0xFF;
				CallGraphHashLink *hh = mpCGHashTable[hc];
				CallGraphHashLink *hl = hh;

				for(; hl; hl = hl->mpNext) {
					if (hl->mScope == newScope && hl->mParentContext == mCurrentContext)
						break;
				}

				if (!hl) {
					hl = mHashLinkAllocator.Allocate<CallGraphHashLink>();
					hl->mpNext = hh;
					hl->mContext = (uint32)mSession.mCallGraphRecords.size();
					hl->mParentContext = mCurrentContext;
					hl->mScope = newScope;
					mpCGHashTable[hc] = hl;

					ATProfileCallGraphRecord& cgr = mSession.mCallGraphRecords.push_back();
					memset(&cgr, 0, sizeof cgr);
					cgr.mAddress = newScope;
					cgr.mParent = mCurrentContext;
				}

				mCurrentContext = hl->mContext;
				++mSession.mCallGraphRecords[mCurrentContext].mCalls;
			}
		}

		mSession.mCallGraphRecords[mCurrentContext].mCycles += cycles;
		++mSession.mCallGraphRecords[mCurrentContext].mInsns;

		uint32 addr = hentp->mPC + (hentp->mK << 16);
		uint32 hc = addr & 0xFF;
		HashLink *hh = mpHashTable[hc];
		HashLink *hl = hh;

		for(; hl; hl = hl->mpNext) {
			if (hl->mRecord.mAddress == addr)
				break;
		}

		if (!hl) {
			hl = mHashLinkAllocator.Allocate<HashLink>();
			hl->mpNext = hh;
			hl->mRecord.mAddress = addr;
			hl->mRecord.mCycles = 0;
			hl->mRecord.mUnhaltedCycles = 0;
			hl->mRecord.mInsns = 0;
			hl->mRecord.mCalls = 0;
			hl->mRecord.mModeBits = (hentp->mP >> 4) & 3;
			hl->mRecord.mEmulationMode = hentp->mbEmulation;
			mpHashTable[hc] = hl;
		}

		hl->mRecord.mCycles += cycles;
		hl->mRecord.mUnhaltedCycles += unhaltedCycles;
		++hl->mRecord.mInsns;

		hentp = hentn;
	}

	mTotalSamples += count;
}

void ATCPUProfiler::ClearSamples() {
	memset(mpHashTable, 0, sizeof mpHashTable);
	memset(mpCGHashTable, 0, sizeof mpCGHashTable);

	mHashLinkAllocator.Clear();

	mSession.mRecords.clear();
	mSession.mCallGraphRecords.clear();
}
