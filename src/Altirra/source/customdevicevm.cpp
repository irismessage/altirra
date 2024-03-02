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
#include "customdevicevm.h"

extern const ATCDVMTypeInfo kATCDVMTypeVoid { ATCDVMTypeClass::Void };
extern const ATCDVMTypeInfo kATCDVMTypeInt { ATCDVMTypeClass::Int };
extern const ATCDVMTypeInfo kATCDVMTypeString { ATCDVMTypeClass::String };
extern const ATCDVMTypeInfo kATCDVMTypeFunctionPtr { ATCDVMTypeClass::FunctionPointer, 0 };

uint32 ATCDVMGetOpcodeLength(ATCDVMOpcode opcode) {
	switch(opcode) {
		case ATCDVMOpcode::IVLoad:
		case ATCDVMOpcode::IVStore:
		case ATCDVMOpcode::ILLoad:
		case ATCDVMOpcode::ILStore:
		case ATCDVMOpcode::ISLoad:
		case ATCDVMOpcode::ITLoad:
		case ATCDVMOpcode::IntConst8:
			return 2;

		case ATCDVMOpcode::IntConst:
		case ATCDVMOpcode::Ljz:
		case ATCDVMOpcode::Ljnz:
		case ATCDVMOpcode::Ljmp:
			return 5;

		case ATCDVMOpcode::MethodCallVoid:
		case ATCDVMOpcode::MethodCallInt:
		case ATCDVMOpcode::StaticMethodCallVoid:
		case ATCDVMOpcode::StaticMethodCallInt:
		case ATCDVMOpcode::FunctionCallVoid:
		case ATCDVMOpcode::FunctionCallInt:
			return 3;

		case ATCDVMOpcode::Jz:
		case ATCDVMOpcode::Jnz:
		case ATCDVMOpcode::Jmp:
			return 2;

		default:
			return 1;
	}
}

void ATCDVMDomain::Clear() {
	mAllocator.Clear();
	mGlobalVariables.clear();
	mSpecialVariables.clear();
	mGlobalObjects.clear();
	mFunctions.clear();
	mThreads.clear();
	mNumThreadVariables = 0;
}

///////////////////////////////////////////////////////////////////////////

void ATCDVMThreadWaitQueue::Reset() {
	mThreadList.clear();
}

void ATCDVMThreadWaitQueue::Suspend(ATCDVMThread& thread) {
	thread.mpSuspendQueue = this;
	mThreadList.push_back(&thread);

	thread.Suspend();
}

ATCDVMThread *ATCDVMThreadWaitQueue::GetNext() const {
	return mThreadList.empty() ? nullptr : mThreadList.front();
}

void ATCDVMThreadWaitQueue::ResumeVoid() {
	if (!mThreadList.empty()) {
		ATCDVMThread *thread = mThreadList.front();
		mThreadList.pop_front();

		VDASSERT(thread->mpSuspendQueue == this);
		thread->mpSuspendQueue = nullptr;
		thread->Resume();
	}
}

void ATCDVMThreadWaitQueue::ResumeInt(sint32 v) {
	if (!mThreadList.empty()) {
		ATCDVMThread *thread = mThreadList.front();
		mThreadList.pop_front();

		VDASSERT(thread->mpSuspendQueue == this);
		thread->mpSuspendQueue = nullptr;
		thread->SetResumeInt(v);
		thread->Resume();
	}
}

///////////////////////////////////////////////////////////////////////////

void ATCDVMThread::Init(ATCDVMDomain& domain) {
	mpDomain = &domain;
	mThreadIndex = (uint32)domain.mThreads.size();
	domain.mThreads.push_back(this);
	mThreadVariables.clear();
	mThreadVariables.resize(domain.mNumThreadVariables, 0);

	mbSuspended = false;
	mpSuspendQueue = nullptr;
	mStackFrames.clear();
}

void ATCDVMThread::Reset() {
	Abort();
}

void ATCDVMThread::StartVoid(const ATCDVMFunction& function) {
	if (mbSuspended)
		Abort();

	mStackFrames.push_back(StackFrame { &function, nullptr, 0, 0 });
}

bool ATCDVMThread::RunVoid(const ATCDVMFunction& function) {
	StartVoid(function);

	return Run();
}

sint32 ATCDVMThread::RunInt(const ATCDVMFunction& function) {
	RunVoid(function);

	return mArgStack[0];
}

void ATCDVMThread::SetResumeInt(sint32 v) {
	VDASSERT(mbSuspended && !mArgStack.empty() && !mStackFrames.empty());

	if (mbSuspended)
		mArgStack[mStackFrames.back().mSP - 1] = v;
}

bool ATCDVMThread::Resume() {
	VDASSERT(mbSuspended && !mpSuspendQueue);

	mbSuspended = false;
	return mStackFrames.empty() || Run();
}

void ATCDVMThread::Abort() {
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

bool ATCDVMThread::Run() {
	auto *prev = mpDomain->mpActiveThread;
	mpDomain->mpActiveThread = this;

	uint32 loopSafetyCounter = 10000;

	// these cannot be restrict as they are modified by method calls
	auto *vars = mpDomain->mGlobalVariables.data();
	auto *objs = mpDomain->mGlobalObjects.data();
	auto *svars = mpDomain->mSpecialVariables.data();

	for(;;) {
		// resume current function for top frame on the stack
		StackFrame& VDRESTRICT frame = mStackFrames.back();
		const uint8 *VDRESTRICT pc = frame.mpPC;
		const ATCDVMFunction *VDRESTRICT function = frame.mpFunction;

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
			ATCDVMOpcode opcode = (ATCDVMOpcode)*pc++;

			switch(opcode) {
				case ATCDVMOpcode::Nop:			break;
				case ATCDVMOpcode::Pop:			--sp; break;
				case ATCDVMOpcode::Dup:			*sp = sp[-1]; ++sp; break;
				case ATCDVMOpcode::IVLoad:		*sp++ = vars[*pc++]; break;
				case ATCDVMOpcode::IVStore:		vars[*pc++] = *--sp; break;
				case ATCDVMOpcode::ILLoad:		*sp++ = bp[*pc++]; break;
				case ATCDVMOpcode::ILStore:		bp[*pc++] = *--sp; break;
				case ATCDVMOpcode::ISLoad:		*sp++ = svars[*pc++]; break;
				case ATCDVMOpcode::ITLoad:		*sp++ = mThreadVariables[*pc++]; break;
				case ATCDVMOpcode::IntConst:	*sp++ = VDReadUnalignedLES32(pc); pc += 4; break;
				case ATCDVMOpcode::IntConst8:	*sp++ = (sint8)*pc++; break;
				case ATCDVMOpcode::IntAdd:		sp[-2] = (sint32)((uint32)sp[-2] + (uint32)sp[-1]); --sp; break;
				case ATCDVMOpcode::IntSub:		sp[-2] = (sint32)((uint32)sp[-2] - (uint32)sp[-1]); --sp; break;
				case ATCDVMOpcode::IntMul:		sp[-2] *= sp[-1]; --sp; break;

				case ATCDVMOpcode::IntDiv:
					if (!sp[-1])
						sp[-2] = 0;
					else if (sp[-1] == -1)
						sp[-2] = -sp[-2];
					else
						sp[-2] /= sp[-1];
					--sp;
					break;

				case ATCDVMOpcode::IntMod:
					if (sp[-1] >= -1 && sp[-1] <= 1)
						sp[-2] = 0;
					else
						sp[-2] %= sp[-1];
					--sp;
					break;

				case ATCDVMOpcode::IntAnd:		sp[-2] &= sp[-1]; --sp; break;
				case ATCDVMOpcode::IntOr:		sp[-2] |= sp[-1]; --sp; break;
				case ATCDVMOpcode::IntXor:		sp[-2] ^= sp[-1]; --sp; break;
				case ATCDVMOpcode::IntAsr:		sp[-2] >>= sp[-1] & 31; --sp; break;
				case ATCDVMOpcode::IntAsl:		sp[-2] <<= sp[-1] & 31; --sp; break;
				case ATCDVMOpcode::Not:			sp[-1] = !sp[-1]; break;
				case ATCDVMOpcode::And:			sp[-2] = sp[-2] && sp[-1]; --sp; break;
				case ATCDVMOpcode::Or:			sp[-2] = sp[-2] || sp[-1]; --sp; break;
				case ATCDVMOpcode::IntLt:		sp[-2] = sp[-2] <  sp[-1] ? 1 : 0; --sp; break;
				case ATCDVMOpcode::IntLe:		sp[-2] = sp[-2] <= sp[-1] ? 1 : 0; --sp; break;
				case ATCDVMOpcode::IntGt:		sp[-2] = sp[-2] >  sp[-1] ? 1 : 0; --sp; break;
				case ATCDVMOpcode::IntGe:		sp[-2] = sp[-2] >= sp[-1] ? 1 : 0; --sp; break;
				case ATCDVMOpcode::IntEq:		sp[-2] = sp[-2] == sp[-1] ? 1 : 0; --sp; break;
				case ATCDVMOpcode::IntNe:		sp[-2] = sp[-2] != sp[-1] ? 1 : 0; --sp; break;
				case ATCDVMOpcode::IntNeg:		sp[-1] = -sp[-1]; break;
				case ATCDVMOpcode::IntNot:		sp[-1] = ~sp[-1]; break;
				case ATCDVMOpcode::Jz:			++pc; if (!*--sp) pc += (sint8)pc[-1]; break;
				case ATCDVMOpcode::Jnz:			++pc; if (*--sp) pc += (sint8)pc[-1]; break;
				case ATCDVMOpcode::Jmp:			pc += (sint8)*pc + 4; break;
				case ATCDVMOpcode::Ljz:			pc += 4; if (!*--sp) pc += VDReadUnalignedLES32(pc - 4); break;
				case ATCDVMOpcode::Ljnz:		pc += 4; if (*--sp) pc += VDReadUnalignedLES32(pc - 4); break;
				case ATCDVMOpcode::Ljmp:		pc += VDReadUnalignedLES32(pc) + 4; break;
				case ATCDVMOpcode::LoopChk:
					if (--loopSafetyCounter <= 0) {
						return true;
					}
					break;

				case ATCDVMOpcode::MethodCallVoid:
					pc += 2;
					sp -= pc[-2] + 1;
					((ATCDVMExternalVoidMethod)function->mpMethodTable[pc[-1]])(*mpDomain, sp);
					if (mbSuspended) {
						frame.mpPC = pc;
						frame.mSP = (uint32)(sp - sp0);
						goto exit;
					}
					break;

				case ATCDVMOpcode::MethodCallInt:
					pc += 2;
					sp -= pc[-2];
					sp[-1] = ((ATCDVMExternalIntMethod)function->mpMethodTable[pc[-1]])(*mpDomain, sp - 1);
					if (mbSuspended) {
						frame.mpPC = pc;
						frame.mSP = (uint32)(sp - sp0);
						goto exit;
					}
					break;

				case ATCDVMOpcode::StaticMethodCallVoid:
					pc += 2;
					sp -= pc[-2];
					((ATCDVMExternalVoidMethod)function->mpMethodTable[pc[-1]])(*mpDomain, sp);
					if (mbSuspended) {
						frame.mpPC = pc;
						frame.mSP = (uint32)(sp - sp0);
						goto exit;
					}
					break;

				case ATCDVMOpcode::StaticMethodCallInt:
					pc += 2;
					sp -= pc[-2] - 1;
					sp[-1] = ((ATCDVMExternalIntMethod)function->mpMethodTable[pc[-1]])(*mpDomain, sp - 1);
					if (mbSuspended) {
						frame.mpPC = pc;
						frame.mSP = (uint32)(sp - sp0);
						goto exit;
					}
					break;

				case ATCDVMOpcode::FunctionCallVoid:
				case ATCDVMOpcode::FunctionCallInt:
					{
						pc += 2;
						sp -= pc[-2];

						const ATCDVMFunction *childFunc = mpDomain->mFunctions[pc[-1]];

						frame.mpPC = pc;
						frame.mSP = (uint32)(sp - sp0);

						// if there is a return value, reserve space for it now in the current frame
						// (this will be populated by the called function and may overlap the first
						// argument)
						if (opcode == ATCDVMOpcode::FunctionCallInt)
							++frame.mSP;
						
						if (mStackFrames.size() < 100) {
							const uint32 newSP = (uint32)(sp - mArgStack.data());
							mStackFrames.push_back(StackFrame { childFunc, nullptr, newSP, newSP });
						} else if (opcode == ATCDVMOpcode::FunctionCallInt)
							*sp = 0;
					}
					goto update_frame;

				case ATCDVMOpcode::ReturnInt:
					*bp = sp[-1];
					[[fallthrough]];
				case ATCDVMOpcode::ReturnVoid:
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
