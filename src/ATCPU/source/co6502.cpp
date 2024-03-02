//	Altirra - Atari 800/800XL/5200 emulator
//	Coprocessor library - 6502/65C02 emulator
//	Copyright (C) 2009-2019 Avery Lee
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
#include <vd2/system/binary.h>
#include <at/atcore/logging.h>
#include <at/atcpu/breakpoints.h>
#include <at/atcpu/co6502.h>
#include <at/atcpu/execstate.h>
#include <at/atcpu/history.h>
#include <at/atcpu/memorymap.h>
#include <at/atcpu/states6502.h>

#define VDDEBUG_CPUTRACE(...) ((void)0)
//#define VDDEBUG_CPUTRACE(...) VDDEBUG(__VA_ARGS__)

ATLogChannel g_ATLCCPUTrace(false, false, "CPUTRACE", "CPU trace cache activity");

///////////////////////////////////////////////////////////////////////////////

struct ATCPUStateLenTable6502 {
	constexpr ATCPUStateLenTable6502();

	uint8 len[256];
};

constexpr ATCPUStateLenTable6502::ATCPUStateLenTable6502()
	: len()
{
	using namespace ATCPUStates6502;

	for(uint8& c : len)
		c = 1;

	len[kStateTraceStartInsn] = 6;
	len[kStateTraceStartInsnWithHistory] = 8;
	len[kStateTraceFastJcc] = 4;
	len[kStateTraceJcc] = 3;
}

constexpr ATCPUStateLenTable6502 kATCPUStateLen6502;

///////////////////////////////////////////////////////////////////////////////

class ATCoProcTraceCache {
	ATCoProcTraceCache(const ATCoProcTraceCache&) = delete;
	ATCoProcTraceCache& operator=(const ATCoProcTraceCache&) = delete;
public:
	static ATCoProcTraceCache *Create(uint32 heapSize, uint32 alignment) vdnoexcept_false;
	void Destroy();

	const uint8 *GetHeapBase() const { return mpHeapStart; }
	uint32 GetFreeSpace() const { return mpHeapLimit - mpHeapNext; }

	bool IsInTraceCache(const void *p) const {
		return ((uintptr)p - (uintptr)mpHeapStart) <= (uintptr)(mpHeapLimit - mpHeapStart);
	}

	uint32 GetExceptionData(const uint8 *p) const {
		return p[mExceptionOffset];
	}

	uint32 GetTraceOffset(const void *p) const {
		return (uint32)((const uint8 *)p - mpHeapStart) + 1;
	}

	const uint8 *GetTrace(uint32 traceOffset) const {
		return &mpHeapStart[traceOffset] - 1;
	}

	bool Clear();
	uint8 *GetWritePointer();
	uint8 *GetExceptionWritePointer();
	uint8 *GetWriteLimit() { return mpHeapTop; }
	void Advance(uint32 size);

	struct HashSegment {
		uint32 mTraceOffset[256];
	};

	HashSegment& GetSegment(uint32 id);
	uint32 AllocateSegment();

private:
	ATCoProcTraceCache(uint8 *heap, uint32 heapSize, uint32 alignment);
	~ATCoProcTraceCache();

	uint8 *mpHeapStart = nullptr;
	uint8 *mpHeapNext = nullptr;
	uint8 *mpHeapTop = nullptr;
	uint8 *mpHeapLimit = nullptr;
	uint32 mAlignment = 0;
	uint32 mSegmentCount = 0;
	uint32 mExceptionOffset = 0;
};

ATCoProcTraceCache *ATCoProcTraceCache::Create(uint32 heapSize, uint32 alignment) vdnoexcept_false {
	heapSize /= 2;

	VDASSERT(alignment && !(alignment & (alignment - 1)));
	heapSize = (heapSize + alignment - 1) & ~(alignment - 1);

	void *p = malloc(sizeof(ATCoProcTraceCache) + heapSize * 2 + alignment - 1);

	if (!p)
		throw std::bad_alloc();

	uint8 *heap = (uint8 *)(((uintptr)p + sizeof(ATCoProcTraceCache) + alignment - 1) & ~(uintptr)(alignment - 1));

	ATCoProcTraceCache *tc;
	try {
		tc = new(p) ATCoProcTraceCache(heap, heapSize, alignment);
	} catch(...) {
		free(p);
		throw;
	}

	return tc;
}

void ATCoProcTraceCache::Destroy() {
	ATCoProcTraceCache *p = this;

	p->~ATCoProcTraceCache();

	free(p);
}

ATCoProcTraceCache::ATCoProcTraceCache(uint8 *heap, uint32 heapSize, uint32 alignment) {
	mpHeapStart = heap;
	mpHeapLimit = heap + heapSize;
	mpHeapNext = mpHeapLimit;		// to force Clear() to do something
	mAlignment = alignment;
	mExceptionOffset = heapSize;

	Clear();
}

ATCoProcTraceCache::~ATCoProcTraceCache() {
}

bool ATCoProcTraceCache::Clear() {
	if (mpHeapNext == mpHeapStart && mpHeapTop == mpHeapLimit)
		return false;

	mpHeapNext = mpHeapStart;
	mpHeapTop = mpHeapLimit;
	mSegmentCount = 0;

	return true;
}

uint8 *ATCoProcTraceCache::GetWritePointer() {
	return mpHeapNext;
}

uint8 *ATCoProcTraceCache::GetExceptionWritePointer() {
	return mpHeapNext + mExceptionOffset;
}

void ATCoProcTraceCache::Advance(uint32 size) {
	VDASSERT((size_t)(mpHeapLimit - mpHeapNext) >= size);

	size += mAlignment - 1;
	size &= ~(mAlignment - 1);

	VDASSERT((size_t)(mpHeapLimit - mpHeapNext) >= size);

	mpHeapNext += size;
}

ATCoProcTraceCache::HashSegment& ATCoProcTraceCache::GetSegment(uint32 id) {
	VDASSERT((uint32)(id - 2) < mSegmentCount);

	return *(HashSegment *)(mpHeapLimit - sizeof(HashSegment) * (id - 1));
}

uint32 ATCoProcTraceCache::AllocateSegment() {
	if (mpHeapTop - mpHeapNext < sizeof(HashSegment))
		return 0;

	mpHeapTop -= sizeof(HashSegment);

	memset(mpHeapTop, 0, sizeof(HashSegment));

	++mSegmentCount;
	return mSegmentCount + 1;
}

///////////////////////////////////////////////////////////////////////////////

#define ATCP_MEMORY_CONTEXT	\
	[[maybe_unused]] uint16 tmpaddr;		\
	[[maybe_unused]] uintptr tmpbase;

#define ATCP_READ_BYTE(addr) (tmpaddr = (addr), tmpbase = mReadMap[(uint8)(tmpaddr >> 8)], (tmpbase & 1 ? ReadByteSlow(tmpbase, tmpaddr) : *(uint8 *)(tmpbase + tmpaddr)))

const uint8 ATCoProc6502::kInitialState = ATCPUStates6502::kStateReadOpcode;
const uint8 ATCoProc6502::kInitialStateNoBreak = ATCPUStates6502::kStateReadOpcodeNoBreak;

ATCoProc6502::ATCoProc6502(bool isC02, bool enableTraceCache)
	: mbIs65C02(isC02)
{
	if (!isC02 && enableTraceCache)
		mpTraceCache = ATCoProcTraceCache::Create(262144, 64);

	ATCPUDecoderGenerator6502 gen;
	gen.RebuildTables(mDecoderTables, false, false, false, isC02, mpTraceCache != nullptr);
}

ATCoProc6502::~ATCoProc6502() {
	if (mpTraceCache)
		mpTraceCache->Destroy();
}

void ATCoProc6502::SetHistoryBuffer(ATCPUHistoryEntry buffer[131072]) {
	bool historyWasOn = (mpHistory != nullptr);
	bool historyNowOn = (buffer != nullptr);

	mpHistory = buffer;

	if (historyWasOn != historyNowOn) {
		mbHistoryChangePending = true;

		for(uint32 i = 0, n = mDecoderTables.mDecodeHeapLimit; i < n; ) {
			uint8& op = mDecoderTables.mDecodeHeap[i];
			i += kATCPUStateLen6502.len[op];

			if (op == ATCPUStates6502::kStateReadOpcode || op == ATCPUStates6502::kStateReadOpcodeNoBreak)
				op = ATCPUStates6502::kStateRegenerateDecodeTables;
			else if (op == ATCPUStates6502::kStateAddToHistory)
				op = ATCPUStates6502::kStateNop;
		}
	}
}

void ATCoProc6502::GetExecState(ATCPUExecState& state) const {
	state.m6502.mPC = mInsnPC;
	state.m6502.mA = mA;
	state.m6502.mX = mX;
	state.m6502.mY = mY;
	state.m6502.mS = mS;
	state.m6502.mP = mP;
	state.m6502.mAH = 0;
	state.m6502.mXH = 0;
	state.m6502.mYH = 0;
	state.m6502.mSH = 0;
	state.m6502.mB = 0;
	state.m6502.mK = 0;
	state.m6502.mDP = 0;
	state.m6502.mbEmulationFlag = true;

	switch(*mpNextState) {
		case ATCPUStates6502::kStateReadOpcode:
		case ATCPUStates6502::kStateReadOpcodeNoBreak:
		case ATCPUStates6502::kStateTraceStartInsn:
		case ATCPUStates6502::kStateTraceStartInsnWithHistory:
			state.m6502.mbAtInsnStep = true;
			break;

		default:
			state.m6502.mbAtInsnStep = false;
			break;
	}
}

void ATCoProc6502::SetExecState(const ATCPUExecState& state) {
	const ATCPUExecState6502& state6502 = state.m6502;

	if (mInsnPC != state6502.mPC) {
		Jump(state6502.mPC);
	}

	mExtraFlags = {};
	mA = state6502.mA;
	mX = state6502.mX;
	mY = state6502.mY;
	mS = state6502.mS;
	mP = state6502.mP | 0x30;
}

void ATCoProc6502::Jump(uint16 addr) {
	mPC = addr;
	mInsnPC = addr;

	mpNextState = &kInitialStateNoBreak;
}

void ATCoProc6502::SetBreakpointMap(const bool bpMap[65536], IATCPUBreakpointHandler *bpHandler) {
	bool wasEnabled = (mpBreakpointMap != nullptr);
	bool nowEnabled = (bpMap != nullptr);

	mpBreakpointMap = bpMap;
	mpBreakpointHandler = bpHandler;

	if (wasEnabled != nowEnabled) {
		ExitTrace();
		ClearTraceCache();

		if (nowEnabled) {
			for(uint32 i = 0, n = mDecoderTables.mDecodeHeapLimit; i < n;) {
				uint8& op = mDecoderTables.mDecodeHeap[i];
				i += kATCPUStateLen6502.len[op];

				if (op == ATCPUStates6502::kStateReadOpcodeNoBreak)
					op = ATCPUStates6502::kStateReadOpcode;
			}
		} else {
			for(uint32 i = 0, n = mDecoderTables.mDecodeHeapLimit; i < n;) {
				uint8& op = mDecoderTables.mDecodeHeap[i];
				i += kATCPUStateLen6502.len[op];

				if (op == ATCPUStates6502::kStateReadOpcode)
					op = ATCPUStates6502::kStateReadOpcodeNoBreak;
			}

			if (mpNextState == &kInitialState)
				mpNextState = &kInitialStateNoBreak;
		}
	}
}

void ATCoProc6502::OnBreakpointsChanged(const uint16 *pc) {
	if (!mpBreakpointMap || !mpTraceCache) 
		return;

	if (pc) {
		// Check if the PC is in the trace cache and avoid invalidating the trace
		// cache if that instruction has not yet been traced. It is guaranteed that
		// the starting offset will be set if any trace contains that instruction,
		// as traces populate trace offsets for all insns not already covered.
		//
		// Note that this will not invalidate if the PC is within a traced insn but
		// not at the insn start. Our BPs do not fire in this situation, only when
		// the insn PC matches.

		const uint16 addr = *pc;

		uint32 traceSegmentId = mTraceMap[addr >> 8];

		if (traceSegmentId < 2)
			return;

		const auto& traceSegment = mpTraceCache->GetSegment(traceSegmentId);
		if (!traceSegment.mTraceOffset[addr & 0xFF])
			return;
	}

	ExitTrace();
	ClearTraceCache();
}

void ATCoProc6502::ColdReset() {
	mA = 0;
	mP = 0x30;
	mX = 0;
	mY = 0;
	mS = 0xFF;
	mPC = 0;

	WarmReset();
}

void ATCoProc6502::WarmReset() {
	ATCP_MEMORY_CONTEXT;

	mPC = ATCP_READ_BYTE(0xFFFC);
	mPC += ((uint32)ATCP_READ_BYTE(0xFFFD) << 8);

	// clear D flag
	mP &= 0xF7;

	// set MX and E flags
	mP |= 0x30;

	mpNextState = mpBreakpointMap ? &kInitialState : &kInitialStateNoBreak;
	mInsnPC = mPC;

	if (mbHistoryChangePending)
		RegenerateDecodeTables();
}

void ATCoProc6502::InvalidateTraceCache() {
	ExitTrace();
	ClearTraceCache();
}

uint8 ATCoProc6502::DebugReadByteSlow(uintptr base, uint32 addr) {
	auto node = (ATCoProcReadMemNode *)(base - 1);

	return node->mpDebugRead(addr, node->mpThis);
}

uint8 ATCoProc6502::ReadByteSlow(uintptr base, uint32 addr) {
	auto node = (ATCoProcReadMemNode *)(base - 1);

	return node->mpRead(addr, node->mpThis);
}

void ATCoProc6502::WriteByteSlow(uintptr base, uint32 addr, uint8 value) {
	auto node = (ATCoProcWriteMemNode *)(base - 1);

	node->mpWrite(addr, value, node->mpThis);
}

void ATCoProc6502::DoExtra() {
	if (mExtraFlags & kExtraFlag_SetV) {
		mP |= 0x40;
	}

	mExtraFlags = {};
}

bool ATCoProc6502::CheckBreakpoint() {
	if (mpBreakpointHandler->CheckBreakpoint(mPC)) {
		mpNextState = &kInitialStateNoBreak;
		mInsnPC = mPC;
		return true;
	}

	return false;
}

const uint8 *ATCoProc6502::RegenerateDecodeTables() {
	mbHistoryChangePending = false;

	ATCPUDecoderGenerator6502 gen;
	gen.RebuildTables(mDecoderTables, false, mpHistory != nullptr, mpBreakpointMap != nullptr, mbIs65C02, mpTraceCache != nullptr);

	return mpBreakpointMap ? &kInitialState : &kInitialStateNoBreak;
}

void ATCoProc6502::TryEnterTrace(uint32 tracePageId) {
	uint32 traceOffset = 0;

	if (tracePageId > 1)
		traceOffset = mpTraceCache->GetSegment(tracePageId).mTraceOffset[mPC & 0xFF];

	if (!traceOffset)
		traceOffset = CreateTrace(mPC);

	if (traceOffset)
		mpNextState = mpTraceCache->GetTrace(traceOffset);
}

void ATCoProc6502::ExitTrace() {
	if (mpTraceCache && mpTraceCache->IsInTraceCache(mpNextState)) {
		uint8 exdata = mpTraceCache->GetExceptionData(mpNextState);

		g_ATLCCPUTrace("Exiting trace: PC=$%04X [$%04X], Opcode=$%02X, exception data=$%02X\n", mInsnPC, mPC, mOpcode, exdata);

		if (exdata == 0xFF) {
			mpNextState = mpBreakpointMap ? &kInitialState : &kInitialStateNoBreak;
		} else {
			// The PC is always advanced to the end of the instruction by traces. We must adjust it
			// when exiting the trace based on the PC offset data.
			mPC -= (exdata >> 4);

			mpNextState = &mDecoderTables.mDecodeHeap[mDecoderTables.mInsnPtrs[mOpcode] + (exdata & 0x0F)];
		}
	}
}

void ATCoProc6502::ClearTraceCache() {
	if (!mpTraceCache)
		return;
	
	VDASSERT(!mpTraceCache->IsInTraceCache(mpNextState));
	if (!mpTraceCache->Clear())
		return;

	g_ATLCCPUTrace("Flushing trace cache\n");
	for(uint32& id : mTraceMap) {
		if (id)
			id = 1;
	}
}

uint32 ATCoProc6502::CreateTrace(uint16 pc) {
	using namespace ATCPUStates6502;

	uint32 tracePageId;

	if (mpBreakpointMap && mpBreakpointMap[pc])
		return 0;

	for(uint32 pass = 0; ; ++pass) {
		tracePageId = mTraceMap[pc >> 8];

		if (tracePageId < 2) {
			tracePageId = mpTraceCache->AllocateSegment();
			if (!tracePageId) {
				ExitTrace();
				ClearTraceCache();

				tracePageId = mpTraceCache->AllocateSegment();
				if (!tracePageId) {
					VDFAIL("Should have been able to allocate segment from cleared trace cache.");
					return 0;
				}
			}

			mTraceMap[pc >> 8] = tracePageId;
		}

		const uint32 traceLeft = mpTraceCache->GetFreeSpace();

		if (traceLeft >= 1536) {
			break;
		}

		if (pass) {
			VDFAIL("Should have been able to allocate trace on second pass.");
			return 0;
		}

		ExitTrace();
		ClearTraceCache();
	}

	const uint16 pc0 = pc;
	uint8 *dst0 = mpTraceCache->GetWritePointer();
	uint8 *dst = dst0;
	uint8 *ex0 = mpTraceCache->GetExceptionWritePointer();
	uint8 *ex = ex0;
	uint32 insnsTraced = 0;
	const uint8 *src0 = nullptr;

	g_ATLCCPUTrace("Creating trace: PC=%04X -> %p\n", pc, dst0);
	VDDEBUG_CPUTRACE("Creating trace: PC=%04X -> %p\n", pc, dst0);

	while(mpTraceCache->GetWriteLimit() - dst >= 64) {
		// check if PC is in a traceable page
		uint32 currentTracePageId = mTraceMap[pc >> 8];
		if (!currentTracePageId)
			break;

		// check if PC has a breakpoint on it
		if (mpBreakpointMap && mpBreakpointMap[pc])
			break;

		// check if we need to allocate a segment
		if (currentTracePageId == 1) {
			// page is traceable but doesn't have a segment allocated;
			// see if we can allocate one
			if (mpTraceCache->GetWriteLimit() - dst < 1536) {
				// no -- end the trace
				break;
			}

			currentTracePageId = mpTraceCache->AllocateSegment();
			mTraceMap[pc >> 8] = currentTracePageId;
		}

		// check if PC is in an existing trace and we have emitted enough
		// to make it worthwhile to jump
		auto& segment = mpTraceCache->GetSegment(currentTracePageId);

		if (const uint32 existingTraceOffset = segment.mTraceOffset[pc & 0xFF]) {
			if (existingTraceOffset && insnsTraced >= 8) {
				if (((uintptr)dst & 63) > 60) {
					*dst++ = kStateTraceBridge;
					dst = (uint8 *)(((uintptr)dst + 63) & ~(uintptr)63);
				}

				*dst++ = kStateTraceUJump;
				VDWriteUnalignedLES32(dst, (existingTraceOffset - 1) - (dst - mpTraceCache->GetHeapBase()));
				dst += 4;

				for(uint32 i = (uint32)((dst - dst0) - (ex - ex0)); i; --i)
					*ex++ = 0xFF;

				goto trace_redirected;
			}
		}

		// record instruction location in case we have to backtrack
		uint16 insnPC = pc;
		uint8 *insnPreStart = dst;
		uint8 *insnExPreStart = ex;

		// trace this instruction
		struct InsnHeader {
			uint8 mState = 0;
			uint8 mLen = 0;
			uint16 mAddr = 0;
			uint8 mData = 0;
			uint8 mOpcode[3] {};
		} insnHeader;

		VDASSERT((dst - dst0) == (ex - ex0));

		if (mpHistory) {
			if (((uintptr)dst & 63) > 64 - 8) {
				*dst++ = kStateTraceBridge;
				dst = (uint8 *)(((uintptr)dst + 63) & ~(uintptr)63);
			}

			insnHeader.mState = kStateTraceStartInsnWithHistory;
		} else {
			if (((uintptr)dst & 63) > 64 - 6) {
				*dst++ = kStateTraceBridge;
				dst = (uint8 *)(((uintptr)dst + 63) & ~(uintptr)63);
			}

			insnHeader.mState = kStateTraceStartInsn;
		}
		
		uint8 *insnStart = dst;

		const uint32 insnStartLen = insnHeader.mState == kStateTraceStartInsnWithHistory ? 8 : 6;
		dst += insnStartLen;

		for(size_t i = (dst - dst0) - (ex - ex0); i; --i)
			*ex++ = 0xFF;

		uint8 *insnPrefetch = dst;
		uint8 *exPrefetch = ex;

		uint8 state = kStateReadOpcode;
		const uint8 *nextInsnState = nullptr;
		bool endOfTrace = false;
		bool tracePCAfterInsn = false;
		
		for(;;) {
			// check if this is a state where we need to read an insn byte
			uint8 insnByte;

			switch(state) {
				case kStateReadOpcode:
				case kStateReadOpcodeNoBreak:
				case kStateReadDummyOpcode:
				case kStateReadImm:
				case kStateReadAddrL:
				case kStateReadAddrH:
				case kStateReadAddrHX:
				case kStateReadAddrHY:
				case kStateReadAddrHX_SHY:
				case kStateReadAddrHY_SHA:
				case kStateReadAddrHY_SHX:
					// yes -- check if page is traceable
					if (!mTraceMap[pc >> 8]) {
						// oops -- instruction crosses into untraceable page, abort
						// this insn and truncate the trace
						VDDEBUG_CPUTRACE("Tracing: Ending at PC=$%04X due to untraceable page\n", pc);
						dst = insnPreStart;
						ex = insnExPreStart;
						goto truncate_trace;
					}

					// read byte
					{
						ATCP_MEMORY_CONTEXT
						insnByte = ATCP_READ_BYTE(pc);

						VDDEBUG_CPUTRACE("Tracing: PC=$%04X opcode[%u]=$%02X dst=%p\n", pc, insnHeader.mLen, insnByte, dst);

						++pc;

						VDASSERT(insnHeader.mLen < 3);

						if (insnHeader.mLen)
							*dst++ = kStateTraceContInsn1;

						insnHeader.mOpcode[insnHeader.mLen++] = insnByte;
					}
					break;

				default:
					break;
			}

			// process the state
			const uint8 exdata = nextInsnState ? (uint8)((nextInsnState - src0) - 1) : 0;

			switch(state) {
				case kStateReadOpcode:
				case kStateReadOpcodeNoBreak:
				case kStateReadDummyOpcode:
					nextInsnState = &mDecoderTables.mDecodeHeap[mDecoderTables.mInsnPtrs[insnByte]];
					src0 = nextInsnState;
					break;

				case kStateReadImm:
					insnHeader.mData = insnByte;
					break;

				case kStateReadAddrL:
					insnHeader.mAddr = insnByte;
					break;

				case kStateReadAddrH:
					insnHeader.mAddr += (uint16)insnByte << 8;
					break;
						
				case kStateReadAddrHX:
					insnHeader.mAddr += (uint16)insnByte << 8;
					*dst++ = kStateTraceAddrAddX;
					break;

				case kStateReadAddrHY:
					insnHeader.mAddr += (uint16)insnByte << 8;
					*dst++ = kStateTraceAddrAddY;
					break;

				case kStateReadAddrHX_SHY:
					insnHeader.mAddr += (uint16)insnByte << 8;
					*dst++ = kStateTraceAddrHX_SHY;
					break;

				case kStateReadAddrHY_SHA:
					insnHeader.mAddr += (uint16)insnByte << 8;
					*dst++ = kStateTraceAddrHY_SHA;
					break;

				case kStateReadAddrHY_SHX:
					insnHeader.mAddr += (uint16)insnByte << 8;
					*dst++ = kStateTraceAddrHY_SHX;
					break;

				case kStateJs:
				case kStateJns:
				case kStateJc:
				case kStateJnc:
				case kStateJz:
				case kStateJnz:
				case kStateJo:
				case kStateJno:
					VDASSERT(*nextInsnState == kStateJccFalseRead);
					++nextInsnState;

					insnHeader.mAddr = (uint16)(pc + (sint8)insnHeader.mOpcode[1]);

					*dst++ = kStateTraceFastJcc;

					switch(state) {
						case kStateJs:	*dst++ = 0x00; *dst++ = 0x80; break;
						case kStateJns:	*dst++ = 0x80; *dst++ = 0x80; break;
						case kStateJc:	*dst++ = 0x00; *dst++ = 0x01; break;
						case kStateJnc:	*dst++ = 0x01; *dst++ = 0x01; break;
						case kStateJz:	*dst++ = 0x00; *dst++ = 0x02; break;
						case kStateJnz:	*dst++ = 0x02; *dst++ = 0x02; break;
						case kStateJo:	*dst++ = 0x00; *dst++ = 0x40; break;
						case kStateJno:	*dst++ = 0x40; *dst++ = 0x40; break;
						default:
							VDNEVERHERE;
					}

					// fill in exception data for jcc -- normal path below will fill it in
					// at offset+1 for branch ops
					*ex++ = exdata;
					*ex++ = exdata;
					*ex++ = exdata;
					*ex++ = exdata;

					// check for page crossing
					if ((pc ^ insnHeader.mAddr) & 0xFF00) {
						*dst++ = 7;
						*dst++ = kStateTraceContInsn2;

					} else {
						*dst++ = 6;
					}
					*dst++ = kStateTraceContInsn1;
					*dst++ = kStateTraceAddrToPC;
					*dst++ = mpBreakpointMap ? kStateReadOpcode : kStateReadOpcodeNoBreak;
					break;

				case kStateAddrToPC:
					*dst++ = kStateTraceAddrToPC;
					break;

				case kStateAddToHistory:
					// we can just drop this opcode as history is already added in the initial
					// insn opcode when enabled
					break;

				case kStatePopPCH:
				case kStatePopPCHP1:
					*dst++ = state;
					tracePCAfterInsn = true;
					break;

				default:
					// all other states we can use as-is
					*dst++ = state;
					break;
			}

			// update exception data
			const uint8 *expos = ex0 + (dst - dst0);

			while(ex < expos)
				*ex++ = exdata;

			if (state == kStateAddrToPC
				|| state == kStateTraceAddrToPC)
			{
				endOfTrace = true;
				break;
			}

			// fetch next uop
			state = *nextInsnState++;

			// check for end of insn
			if (state == kStateReadOpcode
				|| state == kStateReadOpcodeNoBreak
				|| state == kStateRegenerateDecodeTables
				|| state == kStateNMIOrIRQVecToPC
				|| state == kStateNMIVecToPC
				)
				break;
		}

		if (tracePCAfterInsn) {
			*dst++ = kStateTracePC;
			*ex++ = 0xFF;
			endOfTrace = true;
		}

		// rewrite the continuation uops
		--insnHeader.mLen;

		VDASSERT((ex - ex0) == (dst - dst0));
		VDASSERT((exPrefetch - ex0) == (insnPrefetch - dst0));

		switch(insnHeader.mLen) {
			case 0:		// 1 byte insn
				break;

			case 1:		// 2 byte insn
				exPrefetch[0] |= 0x10;
				break;
			case 2:		// 3 byte insn
				VDASSERT(insnPrefetch[0] == kStateTraceContInsn1);
				insnPrefetch[0] = kStateTraceContInsn2;
				exPrefetch[0] |= 0x20;
				exPrefetch[1] |= 0x10;
				break;

			default:
				VDFAIL("Unexpected prefetch length.");
				break;
		}

		// populate the insn uop
		memcpy(insnStart, &insnHeader, insnHeader.mState == kStateTraceStartInsnWithHistory ? 8 : 6);

		// record insn start in the trace hash map
		segment.mTraceOffset[insnPC & 0xFF] = (uint32)(insnStart - mpTraceCache->GetHeapBase()) + 1;

		// check if the trace ended due to unconditional control flow
		if (endOfTrace)
			goto trace_redirected;

		if (++insnsTraced >= 64)
			break;
	}

truncate_trace:
	VDASSERT((ex - ex0) == (dst - dst0));
	*ex++ = 0xFF;
	*dst++ = kStateTracePC;

trace_redirected:
	VDASSERT((ex - ex0) == (dst - dst0));
	*ex++ = 0xFF;
	*dst++ = mpBreakpointMap ? kStateReadOpcode : kStateReadOpcodeNoBreak;

	const uint32 traceLen = (uint32)(dst - dst0);
	mpTraceCache->Advance(traceLen);

	VDDEBUG_CPUTRACE("Ending trace -- $%04X bytes.\n", traceLen);

	return mpTraceCache->GetTraceOffset(dst0);
}
