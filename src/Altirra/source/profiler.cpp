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

#include <stdafx.h>
#include <at/atcore/scheduler.h>
#include "cpu.h"
#include "cpumemory.h"
#include "console.h"
#include "profiler.h"

ATProfileSession::~ATProfileSession() {
	for(auto *p : mpFrames)
		delete p;
}

ATProfileSession& ATProfileSession::operator=(ATProfileSession&& src) {
	if (!mpFrames.empty()) {
		for(auto *p : mpFrames)
			delete p;

		mpFrames.clear();
	}

	mpFrames = std::move(src.mpFrames);
	mContexts = std::move(src.mContexts);
	return *this;
}

///////////////////////////////////////////////////////////////////////////

void ATProfileComputeInclusiveStats(
	ATProfileCallGraphInclusiveRecord *dst,
	const ATProfileCallGraphRecord *src,
	const ATProfileCallGraphContext *contexts,
	size_t n)
{
	for(size_t i = n; i; --i) {
		const auto& exRecord = src[i - 1];
		auto& inRecord = dst[i - 1];
		inRecord.mInclusiveCycles += exRecord.mCycles;
		inRecord.mInclusiveUnhaltedCycles += exRecord.mUnhaltedCycles;
		inRecord.mInclusiveInsns += exRecord.mInsns;

		if (i > 4) {
			auto& parentInRecord = dst[contexts[i - 1].mParent];

			parentInRecord.mInclusiveCycles += inRecord.mInclusiveCycles;
			parentInRecord.mInclusiveUnhaltedCycles += inRecord.mInclusiveUnhaltedCycles;
			parentInRecord.mInclusiveInsns += inRecord.mInclusiveInsns;
		}
	}
}

///////////////////////////////////////////////////////////////////////////

ATCPUProfiler::ATCPUProfiler()
	: mpTSDProvider(nullptr)
	, mpCPU(nullptr)
	, mpMemory(nullptr)
	, mpCallbacks(nullptr)
	, mpFastScheduler(nullptr)
	, mpSlowScheduler(nullptr)
	, mpUpdateEvent(nullptr)
	, mHashLinkAllocator(262144 - 128)
	, mCGHashLinkAllocator(262144 - 128)
{
	memset(mpHashTable, 0, sizeof mpHashTable);
	memset(mpBlockHashTable, 0, sizeof mpBlockHashTable);
	memset(mpCGHashTable, 0, sizeof mpCGHashTable);
}

ATCPUProfiler::~ATCPUProfiler() {
	ClearContexts();
}

void ATCPUProfiler::SetBoundaryRule(ATProfileBoundaryRule rule, uint32 param, uint32 param2) {
	mBoundaryRule = rule;
	mBoundaryParam = param;
	mBoundaryParam2 = param2;
}

void ATCPUProfiler::Init(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, ATCPUEmulatorCallbacks *callbacks, ATScheduler *scheduler, ATScheduler *slowScheduler, IATCPUTimestampDecoderProvider *tsdprovider) {
	mpTSDProvider = tsdprovider;
	mpCPU = cpu;
	mpMemory = mem;
	mpCallbacks = callbacks;
	mpFastScheduler = scheduler;
	mpSlowScheduler = slowScheduler;
}

void ATCPUProfiler::Start(ATProfileMode mode, ATProfileCounterMode c1, ATProfileCounterMode c2) {
	mProfileMode = mode;
	mLastHistoryCounter = mpCPU->GetHistoryCounter();
	mbAdjustStackNext = false;
	mbKeepNextFrame = true;
	mLastS = mpCPU->GetS();
	mCurrentFrameAddress = 0xFFFFFF;
	mCurrentContext = 0;
	mTotalContexts = 0;

	mbCountersEnabled = c1 || c2;
	mCounterModes[0] = c1;
	mCounterModes[1] = c2;

	if (mode == kATProfileMode_BasicLines)
		mpUpdateEvent = mpSlowScheduler->AddEvent(2, this, 2);
	else
		mpUpdateEvent = mpSlowScheduler->AddEvent(32, this, 1);

	std::fill(mStackTable, mStackTable+256, -1);

	ClearContexts();

	if (mode == kATProfileMode_CallGraph) {
		mTotalContexts = 4;

		mSession.mContexts.resize(4);
		mSession.mContexts[0] = ATProfileCallGraphContext { 0, 0 };
		mSession.mContexts[1] = ATProfileCallGraphContext { 0, 0x2000000 };
		mSession.mContexts[2] = ATProfileCallGraphContext { 0, 0x3000000 };
		mSession.mContexts[3] = ATProfileCallGraphContext { 0, 0x4000000 };
	} else {
		mSession.mContexts.clear();
	}

	OpenFrame();

	auto&& tsd = mpTSDProvider->GetTimestampDecoder();
	mNextFrameTime = tsd.GetFrameStartTime(mpCallbacks->CPUGetCycle() - 248*114) + 248*114 + tsd.mCyclesPerFrame;
}

void ATCPUProfiler::BeginFrame() {
	Update();
	AdvanceFrame(true);
}

void ATCPUProfiler::EndFrame() {
	Update();
	AdvanceFrame(false);
}

void ATCPUProfiler::End() {
	Update();

	if (mpUpdateEvent) {
		mpSlowScheduler->RemoveEvent(mpUpdateEvent);
		mpUpdateEvent = NULL;
	}

	CloseFrame();

	ClearContexts();
}

void ATCPUProfiler::GetSession(ATProfileSession& session) {
	session = std::move(mSession);
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

	const auto& tsdecoder = mpTSDProvider->GetTimestampDecoder();

	const ATCPUHistoryEntry *hentp = &mpCPU->GetHistory(count);
	uint32 pos = count;
	while(pos) {
		uint32 leftInSegment = ScanForFrameBoundary(pos);

		mTotalSamples += leftInSegment;

		while(leftInSegment--) {
			const ATCPUHistoryEntry *hentn = &mpCPU->GetHistory(--pos);
			uint32 cycles = (uint16)(hentn->mCycle - hentp->mCycle);
			uint32 unhaltedCycles = (uint16)(hentn->mUnhaltedCycle - hentp->mUnhaltedCycle);
			uint32 extpc = hentp->mPC + (hentp->mK << 16);
			uint32 addr = extpc;
			bool isCall = false;
			bool useFrameAddr = false;
			uint32 frameAddr;

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
						if (tsdecoder.IsInterruptPositionVBI(hentp->mCycle))
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

				frameAddr = mCurrentFrameAddress;
				useFrameAddr = true;
			} else if (mProfileMode == kATProfileMode_BasicBlock) {
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

				if ((mCurrentFrameAddress & 0xFFFFFF) == 0xFFFFFF) {
					mCurrentFrameAddress &= extpc | 0xFF000000;
					isCall = true;
				}

				if (adjustStack) {
					uint32 newFrameMode = mCurrentFrameAddress & 0x7000000;
					if (hentp->mbNMI) {
						if (tsdecoder.IsInterruptPositionVBI(hentp->mCycle))
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

				frameAddr = mCurrentFrameAddress;
				useFrameAddr = true;

				switch(opcode) {
					case 0x20:		// JSR
					case 0x60:		// RTS
					case 0x40:		// RTI
					case 0x4C:		// JMP
					case 0x6C:		// JMP (abs)
					case 0x10:		// Bcc
					case 0x30:
					case 0x50:
					case 0x70:
					case 0x90:
					case 0xB0:
					case 0xD0:
					case 0xF0:
						mCurrentFrameAddress |= 0xFFFFFF;
						break;
				}
			}

			if (useFrameAddr) {
				uint32 hc = frameAddr & 0xFF;
				HashLink *hh = mpBlockHashTable[hc];
				HashLink *hl = hh;

				for(; hl; hl = hl->mpNext) {
					if (hl->mRecord.mAddress == frameAddr)
						break;
				}

				if (!hl) {
					hl = mHashLinkAllocator.Allocate<HashLink>();
					hl->mpNext = hh;
					hl->mRecord.mAddress = frameAddr;
					hl->mRecord.mCycles = 0;
					hl->mRecord.mUnhaltedCycles = 0;
					hl->mRecord.mInsns = 0;
					hl->mRecord.mModeBits = (hentp->mP >> 4) & 3;
					hl->mRecord.mEmulationMode = hentp->mbEmulation;
					hl->mRecord.mCalls = 0;
					memset(hl->mRecord.mCounters, 0, sizeof hl->mRecord.mCounters);
					mpBlockHashTable[hc] = hl;
				}

				hl->mRecord.mCycles += cycles;
				hl->mRecord.mUnhaltedCycles += unhaltedCycles;
				++hl->mRecord.mInsns;

				if (isCall)
					++hl->mRecord.mCalls;
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
				memset(hl->mRecord.mCounters, 0, sizeof hl->mRecord.mCounters);
				mpHashTable[hc] = hl;
			}

			hl->mRecord.mCycles += cycles;
			hl->mRecord.mUnhaltedCycles += unhaltedCycles;
			++hl->mRecord.mInsns;

			if (mbCountersEnabled)
				UpdateCounters(hl->mRecord.mCounters, *hentp);

			hentp = hentn;
		}
	}
}

void ATCPUProfiler::UpdateCallGraph() {
	uint32 nextHistoryCounter = mpCPU->GetHistoryCounter();
	uint32 count = (nextHistoryCounter - mLastHistoryCounter) & (mpCPU->GetHistoryLength() - 1);
	mLastHistoryCounter = nextHistoryCounter;

	if (!count)
		return;

	if (!mTotalSamples) {
		if (!--count)
			return;
	}

	const auto& tsdecoder = mpTSDProvider->GetTimestampDecoder();

	const ATCPUHistoryEntry *hentp = &mpCPU->GetHistory(count);
	uint32 pos = count;
	while(pos) {
		uint32 leftInSegment = ScanForFrameBoundary(pos);

		mTotalSamples += leftInSegment;

		while(leftInSegment--) {
			const ATCPUHistoryEntry *hentn = &mpCPU->GetHistory(--pos);
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
						if (tsdecoder.IsInterruptPositionVBI(hentp->mCycle))
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
						hl = mCGHashLinkAllocator.Allocate<CallGraphHashLink>();
						hl->mpNext = hh;
						hl->mContext = (uint32)mpCurrentFrame->mCallGraphRecords.size();
						hl->mParentContext = mCurrentContext;
						hl->mScope = newScope;
						mpCGHashTable[hc] = hl;

						ATProfileCallGraphRecord& cgr = mpCurrentFrame->mCallGraphRecords.push_back();
						memset(&cgr, 0, sizeof cgr);

						mSession.mContexts.push_back(ATProfileCallGraphContext { (uint32)mCurrentContext, newScope });

						++mTotalContexts;
					}

					mCurrentContext = hl->mContext;
					++mpCurrentFrame->mCallGraphRecords[mCurrentContext].mCalls;
				}
			}

			auto& cgRecord = mpCurrentFrame->mCallGraphRecords[mCurrentContext];
			cgRecord.mCycles += cycles;
			cgRecord.mUnhaltedCycles += unhaltedCycles;
			++cgRecord.mInsns;

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
				memset(hl->mRecord.mCounters, 0, sizeof hl->mRecord.mCounters);
				mpHashTable[hc] = hl;
			}

			hl->mRecord.mCycles += cycles;
			hl->mRecord.mUnhaltedCycles += unhaltedCycles;
			++hl->mRecord.mInsns;

			hentp = hentn;
		}
	}
}

void ATCPUProfiler::AdvanceFrame(bool enableCollection) {
	AdvanceFrame(mpCallbacks->CPUGetCycle(), mpCallbacks->CPUGetUnhaltedCycle(), enableCollection);
}

void ATCPUProfiler::AdvanceFrame(uint32 cycle, uint32 unhaltedCycle, bool enableCollection) {
	CloseFrame(cycle, unhaltedCycle, mbKeepNextFrame);
	OpenFrame(cycle, unhaltedCycle);
	mbKeepNextFrame = enableCollection;
}

void ATCPUProfiler::OpenFrame() {
	OpenFrame(mpCallbacks->CPUGetCycle(), mpCallbacks->CPUGetUnhaltedCycle());
}

void ATCPUProfiler::OpenFrame(uint32 cycle, uint32 unhaltedCycle) {
	mTotalSamples = 0;
	mStartCycleTime = cycle;
	mStartUnhaltedCycleTime = unhaltedCycle;

	mSession.mpFrames.push_back(nullptr);
	mpCurrentFrame = new ATProfileFrame;
	mSession.mpFrames.back() = mpCurrentFrame;

	if (mProfileMode == kATProfileMode_CallGraph) {
		mpCurrentFrame->mCallGraphRecords.resize(4);

		for(int i=0; i<4; ++i) {
			ATProfileCallGraphRecord& rmain = mpCurrentFrame->mCallGraphRecords[i];
			memset(&rmain, 0, sizeof rmain);
		}
	}

	mpCurrentFrame->mCallGraphRecords.resize(mTotalContexts, ATProfileCallGraphRecord());
}

void ATCPUProfiler::CloseFrame() {
	CloseFrame(mpCallbacks->CPUGetCycle(), mpCallbacks->CPUGetUnhaltedCycle(), true);
}

void ATCPUProfiler::CloseFrame(uint32 cycle, uint32 unhaltedCycle, bool keepFrame) {
	ATProfileFrame& frame = *mpCurrentFrame;

	frame.mTotalCycles = cycle - mStartCycleTime;
	frame.mTotalUnhaltedCycles = unhaltedCycle - mStartUnhaltedCycleTime;
	frame.mTotalInsns = mTotalSamples;

	for(const HashLink *hl : mpHashTable) {
		for(; hl; hl = hl->mpNext)
			frame.mRecords.push_back(hl->mRecord);
	}

	for(const HashLink *hl : mpBlockHashTable) {
		for(; hl; hl = hl->mpNext)
			frame.mBlockRecords.push_back(hl->mRecord);
	}

	if (!keepFrame) {
		mSession.mpFrames.pop_back();
		delete mpCurrentFrame;
	}

	mpCurrentFrame = nullptr;

	memset(mpHashTable, 0, sizeof mpHashTable);
	memset(mpBlockHashTable, 0, sizeof mpBlockHashTable);

	mHashLinkAllocator.Clear();
}

void ATCPUProfiler::ClearContexts() {
	memset(mpCGHashTable, 0, sizeof mpCGHashTable);

	mCGHashLinkAllocator.Clear();
}

void ATCPUProfiler::UpdateCounters(uint32 *p, const ATCPUHistoryEntry& he) {
	static const struct BranchOpInfo {
		uint8 mFlagsAnd;
		uint8 mFlagsXor;
	} kBranchOps[8]={
		{ AT6502::kFlagN, AT6502::kFlagN	},		// BPL
		{ AT6502::kFlagN, 0					},		// BMI
		{ AT6502::kFlagV, AT6502::kFlagV	},		// BVC
		{ AT6502::kFlagV, 0					},		// BVS
		{ AT6502::kFlagC, AT6502::kFlagC	},		// BCC
		{ AT6502::kFlagC, 0					},		// BCS
		{ AT6502::kFlagZ, AT6502::kFlagZ	},		// BNE
		{ AT6502::kFlagZ, 0					},		// BEQ
	};

	for(int i=0; i<vdcountof(mCounterModes); ++i) {
		const uint8 opcode = he.mOpcode[0];

		switch(mCounterModes[i]) {
			case kATProfileCounterMode_BranchTaken:
				if ((opcode & 0x1F) == 0x10) {
					const BranchOpInfo& info = kBranchOps[opcode >> 5];

					if ((he.mP & info.mFlagsAnd) ^ info.mFlagsXor)
						++p[i];
				}
				break;

			case kATProfileCounterMode_BranchNotTaken:
				if ((opcode & 0x1F) == 0x10) {
					const BranchOpInfo& info = kBranchOps[opcode >> 5];

					if (!((he.mP & info.mFlagsAnd) ^ info.mFlagsXor))
						++p[i];
				}
				break;

			case kATProfileCounterMode_PageCrossing:
				{
					switch(opcode) {
						case 0x10:	// BPL rel
						case 0x30:	// BMI rel
						case 0x50:	// BVC
						case 0x70:	// BVS
						case 0x90:	// BCC rel8
						case 0xB0:	// BCS rel8
						case 0xD0:	// BNE rel8
						case 0xF0:	// BEQ rel8
							if (((he.mPC + 2) ^ ((he.mPC + 2) + (sint32)(sint8)he.mOpcode[1])) & 0xFF00) {
								const auto& branchOpInfo = kBranchOps[opcode >> 5];

								if ((he.mP & branchOpInfo.mFlagsAnd) ^ branchOpInfo.mFlagsXor)
									++p[i];
							}
							break;

						case 0x1D:	// ORA abs,X
						case 0x3D:	// AND abs,X
						case 0x5D:	// EOR abs,X
						case 0x7D:	// ADC abs,X
						case 0xBC:	// LDY abs,X
						case 0xBD:	// LDA abs,X
						case 0xDD:	// CMP abs,X
						case 0xFD:	// SBC abs,X
							if ((uint32)he.mOpcode[1] + he.mX >= 0x100)
								++p[i];
							break;

						case 0x11:	// ORA (zp),Y
						case 0x31:	// AND (zp),Y
						case 0x51:	// EOR (zp),Y
						case 0x71:	// ADC (zp),Y
						case 0xB1:	// LDA (zp),Y
						case 0xD1:	// CMP (zp),Y
						case 0xF1:	// SBC (zp),Y
							if ((he.mEA & 0xFF) < he.mY)
								++p[i];
							break;

						case 0x19:	// ORA abs,Y
						case 0x39:	// AND abs,Y
						case 0x59:	// EOR abs,Y
						case 0x79:	// ADC abs,Y
						case 0xB9:	// LDA abs,Y
						case 0xBE:	// LDX abs,Y
						case 0xD9:	// CMP abs,Y
						case 0xF9:	// SBC abs,Y
							if ((uint32)he.mOpcode[1] + he.mY >= 0x100)
								++p[i];
							break;
					}
				}
				break;

			case kATProfileCounterMode_RedundantOp:
				switch(opcode) {
					case 0x18:	// CLC
						if (!(he.mP & AT6502::kFlagC))
							++p[i];
						break;
					case 0x38:	// SEC
						if (he.mP & AT6502::kFlagC)
							++p[i];
						break;
					case 0x58:	// CLI
						if (!(he.mP & AT6502::kFlagI))
							++p[i];
						break;
					case 0x78:	// SEI
						if (he.mP & AT6502::kFlagI)
							++p[i];
						break;
					case 0xB8:	// CLV
						if (!(he.mP & AT6502::kFlagV))
							++p[i];
						break;
					case 0xD8:	// CLD
						if (!(he.mP & AT6502::kFlagD))
							++p[i];
						break;
					case 0xF8:	// SED
						if (he.mP & AT6502::kFlagD)
							++p[i];
						break;

					case 0xA9:	// LDA #imm
						if (he.mOpcode[1] == he.mA)
							++p[i];
						break;

					case 0xA2:	// LDX #imm
						if (he.mOpcode[1] == he.mX)
							++p[i];
						break;

					case 0xA0:	// LDY #imm
						if (he.mOpcode[1] == he.mY)
							++p[i];
						break;

				}
				break;
		}
	}
}

uint32 ATCPUProfiler::ScanForFrameBoundary(uint32 n) {
	switch(mBoundaryRule) {
		case kATProfileBoundaryRule_None:
		default:
			break;

		case kATProfileBoundaryRule_VBlank:
			{
				const auto& tsdecoder = mpTSDProvider->GetTimestampDecoder();
				for(uint32 i=0; i<n; ++i) {
					const ATCPUHistoryEntry *hent = &mpCPU->GetHistory(n - i);

					if (hent->mCycle - mNextFrameTime < (1U << 31)) {
						if (i)
							return i;

						AdvanceFrame(hent->mCycle, hent->mUnhaltedCycle, true);

						mNextFrameTime = tsdecoder.GetFrameStartTime(hent->mCycle - 248*114) + 248*114 + tsdecoder.mCyclesPerFrame;
					}
				}
			}
			break;

		case kATProfileBoundaryRule_PCAddress:
			for(uint32 i=0; i<n; ++i) {
				const ATCPUHistoryEntry *hent = &mpCPU->GetHistory(n - i);

				if (hent->mPC == mBoundaryParam) {
					if (i)
						return i;

					AdvanceFrame(hent->mCycle, hent->mUnhaltedCycle, true);
				} else if (hent->mPC == mBoundaryParam2) {
					if (i)
						return i;

					AdvanceFrame(hent->mCycle, hent->mUnhaltedCycle, false);
				}
			}
			break;

		case kATProfileBoundaryRule_PCAddressFunction:
			for(uint32 i=0; i<n; ++i) {
				const ATCPUHistoryEntry *hent = &mpCPU->GetHistory(n - i);

				if (hent->mPC == mBoundaryParam) {
					if (i)
						return i;

					AdvanceFrame(hent->mCycle, hent->mUnhaltedCycle, true);
					mBoundaryParam2 = hent->mS;
				} else if (((hent->mS - mBoundaryParam2 - 1) & 0xFF) < 0x0F) {
					if (i)
						return i;

					AdvanceFrame(hent->mCycle, hent->mUnhaltedCycle, false);
				}
			}
			break;
	}

	return n;
}
