//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
#include <vd2/system/binary.h>
#include <at/atvm/vm.h>

extern const ATVMTypeInfo kATVMTypeVoid { ATVMTypeClass::Void };
extern const ATVMTypeInfo kATVMTypeInt { ATVMTypeClass::Int };
extern const ATVMTypeInfo kATVMTypeString { ATVMTypeClass::String };
extern const ATVMTypeInfo kATVMTypeFunctionPtr { ATVMTypeClass::FunctionPointer, 0 };

uint32 ATVMGetOpcodeLength(ATVMOpcode opcode) {
	switch(opcode) {
		case ATVMOpcode::IVLoad:
		case ATVMOpcode::IVStore:
		case ATVMOpcode::ILLoad:
		case ATVMOpcode::ILStore:
		case ATVMOpcode::ISLoad:
		case ATVMOpcode::ITLoad:
		case ATVMOpcode::IntConst8:
			return 2;

		case ATVMOpcode::IntConst:
		case ATVMOpcode::Ljz:
		case ATVMOpcode::Ljnz:
		case ATVMOpcode::Ljmp:
			return 5;

		case ATVMOpcode::MethodCallVoid:
		case ATVMOpcode::MethodCallInt:
		case ATVMOpcode::StaticMethodCallVoid:
		case ATVMOpcode::StaticMethodCallInt:
		case ATVMOpcode::FunctionCallVoid:
		case ATVMOpcode::FunctionCallInt:
			return 3;

		case ATVMOpcode::Jz:
		case ATVMOpcode::Jnz:
		case ATVMOpcode::Jmp:
			return 2;

		default:
			return 1;
	}
}

void ATVMDomain::Clear() {
	mAllocator.Clear();
	mGlobalVariables.clear();
	mSpecialVariables.clear();
	mGlobalObjects.clear();
	mFunctions.clear();
	mThreads.clear();
	mNumThreadVariables = 0;
}

///////////////////////////////////////////////////////////////////////////

void ATVMThreadWaitQueue::Reset() {
	mThreadList.clear();
}

void ATVMThreadWaitQueue::Suspend(ATVMThread& thread) {
	// We don't assert that the thread is non-suspended so as to allow carrying over
	// a suspension into another wait queue.

	thread.mpSuspendQueue = this;
	thread.mpSuspendAbortFn = nullptr;
	mThreadList.push_back(&thread);

	thread.Suspend();
}

ATVMThread *ATVMThreadWaitQueue::GetNext() const {
	return mThreadList.empty() ? nullptr : mThreadList.front();
}

ATVMThread *ATVMThreadWaitQueue::Pop() {
	if (mThreadList.empty())
		return nullptr;

	ATVMThread *thread = mThreadList.front();
	mThreadList.pop_front();

	VDASSERT(thread->mpSuspendQueue == this);
	thread->mpSuspendQueue = nullptr;

	return thread;
}

void ATVMThreadWaitQueue::ResumeVoid() {
	if (!mThreadList.empty()) {
		ATVMThread *thread = mThreadList.front();
		mThreadList.pop_front();

		VDASSERT(thread->mpSuspendQueue == this);
		thread->mpSuspendQueue = nullptr;
		thread->Resume();
	}
}

void ATVMThreadWaitQueue::ResumeInt(sint32 v) {
	if (!mThreadList.empty()) {
		ATVMThread *thread = mThreadList.front();
		mThreadList.pop_front();

		VDASSERT(thread->mpSuspendQueue == this);
		thread->mpSuspendQueue = nullptr;
		thread->SetResumeInt(v);
		thread->Resume();
	}
}

void ATVMThreadWaitQueue::TransferNext(ATVMThreadWaitQueue& dest) {
	if (!mThreadList.empty()) {
		mThreadList.back()->mpSuspendQueue = &dest;
		dest.mThreadList.splice(dest.mThreadList.end(), mThreadList, std::prev(mThreadList.end()));
	}
}

void ATVMThreadWaitQueue::TransferAll(ATVMThreadWaitQueue& dest) {
	for(ATVMThread *thread : mThreadList)
		thread->mpSuspendQueue = &dest;

	dest.mThreadList.splice(dest.mThreadList.end(), mThreadList);
}

///////////////////////////////////////////////////////////////////////////

void ATVMThread::Init(ATVMDomain& domain) {
	mpDomain = &domain;
	mThreadIndex = (uint32)domain.mThreads.size();
	domain.mThreads.push_back(this);
	mThreadVariables.clear();
	mThreadVariables.resize(domain.mNumThreadVariables, 0);

	mbSuspended = false;
	mpSuspendQueue = nullptr;
	mStackFrames.clear();
}

void ATVMThread::Reset() {
	Abort();
}

void ATVMThread::StartVoid(const ATVMFunction& function) {
	if (mbSuspended)
		Abort();

	mStackFrames.push_back(StackFrame { &function, nullptr, 0, 0 });
}

bool ATVMThread::RunVoid(const ATVMFunction& function) {
	StartVoid(function);

	return Run();
}

sint32 ATVMThread::RunInt(const ATVMFunction& function) {
	RunVoid(function);

	return mArgStack[0];
}

void ATVMThread::SetResumeInt(sint32 v) {
	VDASSERT(mbSuspended && !mArgStack.empty() && !mStackFrames.empty());

	if (mbSuspended)
		mArgStack[mStackFrames.back().mSP - 1] = v;
}

bool ATVMThread::Resume() {
	VDASSERT(mbSuspended && !mpSuspendQueue);

	mbSuspended = false;
	mpSuspendAbortFn = nullptr;
	return mStackFrames.empty() || Run();
}

void ATVMThread::Abort() {
	mbSuspended = false;
	mStackFrames.clear();

	if (mpSuspendQueue) {
		mpSuspendQueue->mThreadList.erase(this);
		mpSuspendQueue = nullptr;
	}

	if (mpSuspendAbortFn) {
		auto fn = mpSuspendAbortFn;
		mpSuspendAbortFn = nullptr;

		(*fn)(*this);
	}
}

bool ATVMThread::Run() {
	auto *prev = mpDomain->mpActiveThread;
	mpDomain->mpActiveThread = this;

	uint32 loopSafetyCounter = 10000;

	// these cannot be restrict as they are modified by method calls
	auto *vars = mpDomain->mGlobalVariables.data();
	auto *svars = mpDomain->mSpecialVariables.data();

	for(;;) {
		// resume current function for top frame on the stack
		StackFrame& VDRESTRICT frame = mStackFrames.back();
		const uint8 *VDRESTRICT pc = frame.mpPC;
		const ATVMFunction *VDRESTRICT function = frame.mpFunction;

		if (!pc) {
			pc = function->mpByteCode;
			frame.mSP = frame.mBP + function->mLocalSlotsRequired;

			uint32 spLimit = frame.mBP + function->mStackSlotsRequired;
			if (mArgStack.size() < spLimit)
				mArgStack.resize(spLimit);

			for(uint32 i = frame.mBP; i != frame.mSP; ++i)
				mArgStack[i] = 0;
		}

		auto *sp0 = mArgStack.data();
		auto *sp = sp0 + frame.mSP;
		auto *bp = sp0 + frame.mBP;
		[[maybe_unused]] auto *spbase = bp + function->mLocalSlotsRequired;

		for(;;) {
			ATVMOpcode opcode = (ATVMOpcode)*pc++;

			switch(opcode) {
				case ATVMOpcode::Nop:			break;
				case ATVMOpcode::Pop:			--sp; break;
				case ATVMOpcode::Dup:			*sp = sp[-1]; ++sp; break;
				case ATVMOpcode::IVLoad:		*sp++ = vars[*pc++]; break;
				case ATVMOpcode::IVStore:		vars[*pc++] = *--sp; break;
				case ATVMOpcode::ILLoad:		*sp++ = bp[*pc++]; break;
				case ATVMOpcode::ILStore:		bp[*pc++] = *--sp; break;
				case ATVMOpcode::ISLoad:		*sp++ = svars[*pc++]; break;
				case ATVMOpcode::ITLoad:		*sp++ = mThreadVariables[*pc++]; break;
				case ATVMOpcode::IntConst:	*sp++ = VDReadUnalignedLES32(pc); pc += 4; break;
				case ATVMOpcode::IntConst8:	*sp++ = (sint8)*pc++; break;
				case ATVMOpcode::IntAdd:		sp[-2] = (sint32)((uint32)sp[-2] + (uint32)sp[-1]); --sp; break;
				case ATVMOpcode::IntSub:		sp[-2] = (sint32)((uint32)sp[-2] - (uint32)sp[-1]); --sp; break;
				case ATVMOpcode::IntMul:		sp[-2] *= sp[-1]; --sp; break;

				case ATVMOpcode::IntDiv:
					if (!sp[-1])
						sp[-2] = 0;
					else if (sp[-1] == -1)
						sp[-2] = -sp[-2];
					else
						sp[-2] /= sp[-1];
					--sp;
					break;

				case ATVMOpcode::IntMod:
					if (sp[-1] >= -1 && sp[-1] <= 1)
						sp[-2] = 0;
					else
						sp[-2] %= sp[-1];
					--sp;
					break;

				case ATVMOpcode::IntAnd:		sp[-2] &= sp[-1]; --sp; break;
				case ATVMOpcode::IntOr:		sp[-2] |= sp[-1]; --sp; break;
				case ATVMOpcode::IntXor:		sp[-2] ^= sp[-1]; --sp; break;
				case ATVMOpcode::IntAsr:		sp[-2] >>= sp[-1] & 31; --sp; break;
				case ATVMOpcode::IntAsl:		sp[-2] <<= sp[-1] & 31; --sp; break;
				case ATVMOpcode::Not:			sp[-1] = !sp[-1]; break;
				case ATVMOpcode::And:			sp[-2] = sp[-2] && sp[-1]; --sp; break;
				case ATVMOpcode::Or:			sp[-2] = sp[-2] || sp[-1]; --sp; break;
				case ATVMOpcode::IntLt:		sp[-2] = sp[-2] <  sp[-1] ? 1 : 0; --sp; break;
				case ATVMOpcode::IntLe:		sp[-2] = sp[-2] <= sp[-1] ? 1 : 0; --sp; break;
				case ATVMOpcode::IntGt:		sp[-2] = sp[-2] >  sp[-1] ? 1 : 0; --sp; break;
				case ATVMOpcode::IntGe:		sp[-2] = sp[-2] >= sp[-1] ? 1 : 0; --sp; break;
				case ATVMOpcode::IntEq:		sp[-2] = sp[-2] == sp[-1] ? 1 : 0; --sp; break;
				case ATVMOpcode::IntNe:		sp[-2] = sp[-2] != sp[-1] ? 1 : 0; --sp; break;
				case ATVMOpcode::IntNeg:		sp[-1] = -sp[-1]; break;
				case ATVMOpcode::IntNot:		sp[-1] = ~sp[-1]; break;
				case ATVMOpcode::Jz:			++pc; if (!*--sp) pc += (sint8)pc[-1]; break;
				case ATVMOpcode::Jnz:			++pc; if (*--sp) pc += (sint8)pc[-1]; break;
				case ATVMOpcode::Jmp:			pc += (sint8)*pc + 4; break;
				case ATVMOpcode::Ljz:			pc += 4; if (!*--sp) pc += VDReadUnalignedLES32(pc - 4); break;
				case ATVMOpcode::Ljnz:		pc += 4; if (*--sp) pc += VDReadUnalignedLES32(pc - 4); break;
				case ATVMOpcode::Ljmp:		pc += VDReadUnalignedLES32(pc) + 4; break;
				case ATVMOpcode::LoopChk:
					if (--loopSafetyCounter <= 0) {
						[[unlikely]]
						if (mpDomain->mInfiniteLoopHandler)
							mpDomain->mInfiniteLoopHandler(frame.mpFunction->mpName);

						mStackFrames.clear();
						mbSuspended = false;
						goto exit;
					}
					break;

				case ATVMOpcode::MethodCallVoid:
					pc += 2;
					sp -= pc[-2] + 1;
					((ATVMExternalVoidMethod)function->mpMethodTable[pc[-1]])(*mpDomain, sp);
					if (mbSuspended) {
						frame.mpPC = pc;
						frame.mSP = (uint32)(sp - sp0);
						goto exit;
					}
					break;

				case ATVMOpcode::MethodCallInt:
					pc += 2;
					sp -= pc[-2];
					sp[-1] = ((ATVMExternalIntMethod)function->mpMethodTable[pc[-1]])(*mpDomain, sp - 1);
					if (mbSuspended) {
						frame.mpPC = pc;
						frame.mSP = (uint32)(sp - sp0);
						goto exit;
					}
					break;

				case ATVMOpcode::StaticMethodCallVoid:
					pc += 2;
					sp -= pc[-2];
					((ATVMExternalVoidMethod)function->mpMethodTable[pc[-1]])(*mpDomain, sp);
					if (mbSuspended) {
						frame.mpPC = pc;
						frame.mSP = (uint32)(sp - sp0);
						goto exit;
					}
					break;

				case ATVMOpcode::StaticMethodCallInt:
					pc += 2;
					sp -= pc[-2] - 1;
					sp[-1] = ((ATVMExternalIntMethod)function->mpMethodTable[pc[-1]])(*mpDomain, sp - 1);
					if (mbSuspended) {
						frame.mpPC = pc;
						frame.mSP = (uint32)(sp - sp0);
						goto exit;
					}
					break;

				case ATVMOpcode::FunctionCallVoid:
				case ATVMOpcode::FunctionCallInt:
					{
						pc += 2;
						sp -= pc[-2];

						const ATVMFunction *childFunc = mpDomain->mFunctions[pc[-1]];

						frame.mpPC = pc;
						frame.mSP = (uint32)(sp - sp0);

						// if there is a return value, reserve space for it now in the current frame
						// (this will be populated by the called function and may overlap the first
						// argument)
						if (opcode == ATVMOpcode::FunctionCallInt)
							++frame.mSP;
						
						if (mStackFrames.size() < 100) {
							const uint32 newSP = (uint32)(sp - mArgStack.data());
							mStackFrames.push_back(StackFrame { childFunc, nullptr, newSP, newSP });
						} else {
							if (opcode == ATVMOpcode::FunctionCallInt)
								*sp = 0;

							if (mpDomain->mInfiniteLoopHandler)
								mpDomain->mInfiniteLoopHandler(frame.mpFunction->mpName);

							mStackFrames.clear();
							mbSuspended = false;
							goto exit;
						}
					}
					goto update_frame;

				case ATVMOpcode::ReturnInt:
					*bp = sp[-1];
					[[fallthrough]];
				case ATVMOpcode::ReturnVoid:
					mStackFrames.pop_back();
					if (mStackFrames.empty())
						goto exit;

					goto update_frame;
			}

			VDASSERT(sp >= spbase);
			VDASSERT((size_t)(pc - function->mpByteCode) < function->mByteCodeLen);
		}

update_frame:
		;
	}

exit:
	mpDomain->mpActiveThread = prev;
	return !mbSuspended;
}
