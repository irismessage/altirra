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
	std::fill(mpHashTable, mpHashTable + 256, (HashLink *)NULL);
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
	mLastHistoryCounter = mpCPU->GetHistoryCounter();
	mTotalSamples = 0;
	mTotalCycles = 0;
	mbAdjustStackNext = false;
	mLastS = mpCPU->GetS();
	mCurrentFrameAddress = mpCPU->GetPC();

	if (mode == kATProfileMode_BasicLines)
		mpUpdateEvent = mpSlowScheduler->AddEvent(2, this, 2);
	else
		mpUpdateEvent = mpSlowScheduler->AddEvent(32, this, 1);

	std::fill(mStackTable, mStackTable+256, -1);
}

void ATCPUProfiler::End() {
	Update();

	if (mpUpdateEvent) {
		mpSlowScheduler->RemoveEvent(mpUpdateEvent);
		mpUpdateEvent = NULL;
	}

	mTotalCycles = mpCallbacks->CPUGetCycle() - mStartCycleTime;
}

void ATCPUProfiler::GetSession(ATProfileSession& session) {
	Update();
	session.mRecords.clear();

	session.mTotalCycles = 0;
	session.mTotalInsns = 0;

	for(int i=0; i<256; ++i) {
		for(HashLink *hl = mpHashTable[i]; hl; hl = hl->mpNext) {
			session.mRecords.push_back(hl->mRecord);
			session.mTotalCycles += hl->mRecord.mCycles;
			session.mTotalInsns += hl->mRecord.mInsns;
		}
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
		uint32 cycles = hentn->mCycle - hentp->mCycle;
		uint32 addr = hentp->mPC;
		bool isCall = false;

		if (mProfileMode == kATProfileMode_BasicLines) {
			addr = lineNo;
		} else if (mProfileMode == kATProfileMode_Insns) {
			if (hentp->mP & AT6502::kFlagI)
				addr += 0x10000;
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
				uint32 newFrameMode = mCurrentFrameAddress & 0x30000;
				if (hentp->mbNMI) {
					newFrameMode = 0x30000;
				} else if (hentp->mbIRQ) {
					newFrameMode = 0x20000;
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
					do {
						mStackTable[mLastS] = -1;
					} while(--mLastS != hentp->mS);

					mStackTable[mLastS] = mCurrentFrameAddress;
					mCurrentFrameAddress = hentp->mPC | newFrameMode;
					isCall = true;
				} else {
					mCurrentFrameAddress = hentp->mPC | newFrameMode;
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
			hl = new HashLink;
			hl->mpNext = hh;
			hl->mRecord.mAddress = addr;
			hl->mRecord.mCycles = 0;
			hl->mRecord.mInsns = 0;
			hl->mRecord.mCalls = 0;
			mpHashTable[hc] = hl;
		}

		hl->mRecord.mCycles += cycles;
		++hl->mRecord.mInsns;

		if (isCall)
			++hl->mRecord.mCalls;

		hentp = hentn;
	}

	mTotalSamples += count;
}

void ATCPUProfiler::ClearSamples() {
	for(int i=0; i<256; ++i) {
		HashLink *hl = mpHashTable[i];

		if (hl) {
			do {
				HashLink *hn = hl->mpNext;

				delete hl;

				hl = hn;
			} while(hl);

			mpHashTable[i] = NULL;
		}
	}
}
