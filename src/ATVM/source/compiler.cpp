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
#include <array>
#include <unordered_set>
#include <vd2/system/binary.h>
#include <vd2/system/hash.h>
#include <at/atvm/compiler.h>
#include <at/atvm/vm.h>

ATVMCompileError::ATVMCompileError(const ATVMDataValue& ref, const char *err)
	: MyError(err)
	, mSrcOffset(ref.mSrcOffset)
{
}

ATVMCompileError ATVMCompileError::Format(const ATVMDataValue& ref, const char *format, ...) {
	ATVMCompileError e;
	e.mSrcOffset = ref.mSrcOffset;

	va_list val;

	va_start(val, format);
	e.vsetf(format, val);
	va_end(val);

	return e;
}

///////////////////////////////////////////////////////////////////////////

ATVMCompiler::ATVMCompiler(ATVMDomain& domain)
	: mpDomain(&domain)
{
	// function pointer type 0 is always void()
	(void)GetFunctionPointerTypeIndex(vdvector_view(&kATVMTypeVoid, 1));

	// add break stack sentinel
	mBreakTargetLabels.push_back(0);
}

void ATVMCompiler::SetBindEventHandler(vdfunction<bool(ATVMCompiler&, const char *, const ATVMScriptFragment&)> bindEventFn) {
	mpBindEvent = std::move(bindEventFn);
}

void ATVMCompiler::SetOptionHandler(vdfunction<bool(ATVMCompiler&, const char *, const ATVMDataValue&)> setOptionFn) {
	mpSetOption = std::move(setOptionFn);
}

bool ATVMCompiler::IsValidVariableName(const char *name) {
	char c = *name;

	if (c != '_' && (c < 'a' || c > 'z') && (c < 'A' || c > 'Z'))
		return false;

	while((c = *++name)) {
		if (c != '_' && (c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && (c < '0' || c > '9'))
			return false;
	}

	return true;
}

bool ATVMCompiler::IsSpecialVariableReferenced(const char *name) const {
	auto it = mSpecialVariableLookup.find_as(name);

	if (it == mSpecialVariableLookup.end())
		return false;
	
	uint32 idx = it->second.mIndex;

	return idx < mSpecialVariablesReferenced.size() && mSpecialVariablesReferenced[idx];
}

void ATVMCompiler::DefineClass(const ATVMObjectClass& objClass, DefineInstanceFn *defineFn) {
	auto r = mClassLookup.insert_as(objClass.mpClassName);

	if (r.second) {
		r.first->second.mTypeInfo = ATVMTypeInfo { ATVMTypeClass::ObjectClass, 0, &objClass };
	} else
		VDASSERT(r.first->second.mTypeInfo.mpObjectClass == &objClass);

	if (defineFn)
		r.first->second.mpDefineFn = std::move(*defineFn);
}

bool ATVMCompiler::DefineObjectVariable(const char *name, ATVMObject *obj, const ATVMObjectClass& objClass) {
	if (!IsValidVariableName(name))
		return ReportErrorF("Invalid variable name '%s'", name);

	auto r = mVariableLookup.insert_as(name);

	if (!r.second)
		return ReportErrorF("Variable '%s' has already been defined", name);

	if (mClassLookup.find_as(name) != mClassLookup.end())
		return ReportErrorF("'%s' cannot be declared as a variable because it is a class name", name);

	mpDomain->mGlobalVariables.push_back((sint32)mpDomain->mGlobalObjects.size());
	mpDomain->mGlobalObjects.push_back(obj);
	r.first->second = TypeInfo { TypeClass::ObjectLValue, mVariableCount++, &objClass };

	DefineClass(objClass);
	return true;
}

bool ATVMCompiler::DefineSpecialObjectVariable(const char *name, ATVMObject *obj, const ATVMObjectClass& objClass) {
	auto r = mSpecialVariableLookup.insert_as(name);
	VDASSERT(r.second);

	mpDomain->mSpecialVariables.push_back((sint32)mpDomain->mGlobalObjects.size());
	mpDomain->mGlobalObjects.push_back(obj);
	r.first->second = TypeInfo { TypeClass::ObjectLValue, (uint8)(mpDomain->mSpecialVariables.size() - 1), &objClass };

	DefineClass(objClass);
	return true;
}

bool ATVMCompiler::DefineIntegerVariable(const char *name) {
	if (!IsValidVariableName(name))
		return ReportErrorF("Invalid variable name '%s'", name);

	if (mClassLookup.find_as(name) != mClassLookup.end())
		return ReportErrorF("'%s' cannot be declared as a variable because it is a class name", name);

	auto r = mVariableLookup.insert_as(name);

	if (!r.second)
		return ReportErrorF("Variable '%s' has already been defined", name);

	mpDomain->mGlobalVariables.push_back(0);
	r.first->second = TypeInfo { TypeClass::IntLValueVariable, mVariableCount++ };
	return true;
}

void ATVMCompiler::DefineSpecialVariable(const char *name) {
	auto r = mSpecialVariableLookup.insert_as(name);
	VDASSERT(r.second);

	mpDomain->mSpecialVariables.push_back(0);
	r.first->second = TypeInfo { TypeClass::IntLValueVariable, (uint8)(mpDomain->mSpecialVariables.size() - 1) };
}

void ATVMCompiler::DefineThreadVariable(const char *name) {
	auto r = mThreadVariableLookup.insert_as(name);
	VDASSERT(r.second);

	++mpDomain->mNumThreadVariables;
	mpDomain->mSpecialVariables.push_back(0);
	r.first->second = TypeInfo { TypeClass::IntLValueVariable, (uint8)(mThreadVariableLookup.size() - 1) };
}

const ATVMTypeInfo *ATVMCompiler::GetVariable(const char *name) const {
	auto it = mVariableLookup.find_as(name);

	if (it == mVariableLookup.end())
		return nullptr;

	return &it->second;
}

const ATVMFunction *ATVMCompiler::DeferCompile(const ATVMTypeInfo& returnType, const ATVMScriptFragment& scriptFragment, ATVMFunctionFlags asyncAllowedMask, ATVMConditionalMask conditionalMask) {
	ATVMFunction *func = mpDomain->mAllocator.Allocate<ATVMFunction>();
	VDStringA name;
	name.sprintf("<anonymous function %u>", (unsigned)mpDomain->mFunctions.size() + 1);
	char *name2 = (char *)mpDomain->mAllocator.Allocate(name.size() + 1);
	memcpy(name2, name.c_str(), name.size() + 1);
	func->mpName = name2;
	mpDomain->mFunctions.push_back(func);
	
	FunctionInfo *fi = AllocTemp<FunctionInfo>();
	mFunctionInfoTable.push_back(fi);

	fi->mAsyncAllowedMask = asyncAllowedMask;
	fi->mDefinitionPos = scriptFragment.mSrcOffset;
	fi->mFunctionIndex = (sint32)mpDomain->mFunctions.size() - 1;

	mDeferredCompiles.emplace_back(
		[returnType, &scriptFragment, asyncAllowedMask, conditionalMask, func, fi, this] {
			mpReturnType = &returnType;

			const auto prevSrc = mpSrc;
			const auto prevSrcEnd = mpSrcEnd;

			InitSource(scriptFragment.mpSrc, scriptFragment.mpSrc + scriptFragment.mSrcOffset, scriptFragment.mSrcLength);

			mpCurrentFunctionInfo = fi;
			bool success = ParseFunction(func, returnType, asyncAllowedMask, conditionalMask);
			mpCurrentFunctionInfo = nullptr;

			if (success && Token() != kTokEnd)
				success = ReportError("Expected end of script");

			mpSrc = prevSrc;
			mpSrcEnd = prevSrcEnd;

			return success;
		}
	);

	return func;
}

bool ATVMCompiler::CompileDeferred() {
	for(const auto& step : mDeferredCompiles) {
		if (!step())
			return false;
	}

	// check for undefined functions
	for(const ATVMFunction *func : mpDomain->mFunctions) {
		if (!func->mpByteCode) {
			if (!func->mpName)
				return ReportError("Internal compiler error: anonymous function was not defined");
			else
				return ReportErrorF("Function '%s' declared but not defined", func->mpName);
		}
	}

	// propagate and validate dependencies
	for(;;) {
		bool changed = false;

		for(const auto [callerIndex, calleeIndex] : mFunctionDependencies) {
			FunctionInfo *callerInfo = mFunctionInfoTable[callerIndex];
			FunctionInfo *calleeInfo = mFunctionInfoTable[calleeIndex];

			// propagate flags upward
			ATVMFunctionFlags missingFlags = ~callerInfo->mFlags & calleeInfo->mFlags & ATVMFunctionFlags::AsyncAll;
			if (missingFlags != ATVMFunctionFlags{}) {
				changed = true;

				if ((~callerInfo->mAsyncAllowedMask & missingFlags) != ATVMFunctionFlags{}) {
					return ReportErrorF("Function %s() can suspend in a mode not allowed by calling function %s()"
						, mpDomain->mFunctions[calleeInfo->mFunctionIndex]->mpName
						, mpDomain->mFunctions[callerInfo->mFunctionIndex]->mpName
					);
				}

				callerInfo->mFlags |= missingFlags;
			}

			// propagate restrictions downward
			ATVMFunctionFlags missingRestrictions = ~callerInfo->mAsyncAllowedMask & calleeInfo->mAsyncAllowedMask;
			if (missingRestrictions != ATVMFunctionFlags{}) {
				changed = true;

				if ((callerInfo->mFlags & missingFlags) != ATVMFunctionFlags{}) {
					return ReportErrorF("Function %s() can suspend in a mode not allowed by calling function %s()"
						, mpDomain->mFunctions[calleeInfo->mFunctionIndex]->mpName
						, mpDomain->mFunctions[callerInfo->mFunctionIndex]->mpName
					);
				}

				callerInfo->mAsyncAllowedMask &= ~missingRestrictions;
			}
		}

		if (!changed)
			break;
	}

	return true;
}

bool ATVMCompiler::CompileFile(const char *src, size_t len) {
	mpSrc0 = src;
	InitSource(src, src, len);

	return ParseFile();
}

const char *ATVMCompiler::GetError() const {
	return mError.empty() ? nullptr : mError.c_str();
}

std::pair<uint32, uint32> ATVMCompiler::GetErrorLinePos() const {
	uint32 lineNo = 1;
	const char *lineStart = mpSrc0;
	const char *errorPos = mpSrc0 + mErrorPos;

	for(const char *s = mpSrc0; s != errorPos; ++s) {
		if (*s == '\n') {
			++lineNo;
			lineStart = s+1;
		}
	}

	return { lineNo, (uint32)(errorPos - lineStart) + 1 };
}

void ATVMCompiler::InitSource(const void *src0, const void *src, size_t len) {
	mpSrc = (const char *)src;
	mpSrcStart = mpSrc;
	mpSrcEnd = mpSrc + len;

	mMethodPtrs.clear();
	mByteCodeBuffer.clear();
	mPushedToken = 0;

	mError.clear();
}

bool ATVMCompiler::ParseFile() {
	for(;;) {
		uint32 tok = Token();
		if (tok == kTokError)
			return false;

		if (tok == kTokEnd)
			break;

		if (tok == kTokFunction) {
			if (!ParseFunction())
				return false;
		} else if (tok == kTokInt || tok == kTokVoid || tok == kTokIdentifier) {
			if (!ParseVariableDefinition(tok))
				return false;
		} else if (tok == kTokEvent) {
			if (!ParseEventBinding())
				return false;
		} else if (tok == kTokOption) {
			if (!ParseOption())
				return false;
		} else
			return ReportError("Function or variable definition expected");
	}

	return true;
}

bool ATVMCompiler::ParseVariableDefinition(uint32 typeTok) {
	if (typeTok == kTokVoid)
		return ReportError("Variables cannot be of type void");

	ClassInfo *classInfo = nullptr;

	if (typeTok == kTokIdentifier) {
		auto itClass = mClassLookup.find_as(mTokIdent);

		if (itClass == mClassLookup.end())
			return ReportErrorF("Unknown type '%.*s'", (int)mTokIdent.size(), mTokIdent.data());

		classInfo = &itClass->second;

		if (!classInfo->mpDefineFn)
			return ReportErrorF("Cannot instantiate instances of class type '%.*s'", (int)mTokIdent.size(), mTokIdent.data());
	}

	for(;;) {
		uint32 tok = Token();

		if (tok != kTokIdentifier)
			return ReportError("Expected variable name");

		VDStringA varName(mTokIdent);
		if (!IsValidVariableName(varName.c_str()))
			return ReportErrorF("Invalid variable name: '%s'", varName.c_str());

		if (mClassLookup.find_as(varName) != mClassLookup.end())
			return ReportErrorF("'%s' cannot be declared as a variable because it is a class name", varName.c_str());

		auto r = mVariableLookup.find_as(varName);

		if (r != mVariableLookup.end())
			return ReportErrorF("Variable '%s' has already been defined", varName.c_str());

		if (classInfo) {
			// check for initializer
			ATVMDataValue initializer {};

			tok = Token();
			if (tok == ':') {
				if (!ParseDataValue(initializer))
					return false;
			} else
				Push(tok);

			try {
				if (!classInfo->mpDefineFn(*this, varName.c_str(), initializer.mType != ATVMDataType::Invalid ? &initializer : nullptr))
					return false;
			} catch(const ATVMCompileError& e) {
				if (e.mSrcOffset != ~UINT32_C(0))
					return ReportError(e.mSrcOffset, e.c_str());
				else
					return ReportError(e.c_str());
			} catch(const MyError& e) {
				return ReportError(e.c_str());
			}
		} else {
			if (!DefineIntegerVariable(varName.c_str()))
				return false;
		}

		tok = Token();
		if (tok == ';')
			break;

		if (tok != ',')
			return ReportError("Expected ';' or ',' after variable name");
	}

	return true;
}

bool ATVMCompiler::ParseEventBinding() {
	if (Token() != kTokStringLiteral)
		return ReportError("Event name expected");

	VDStringA eventName(mTokIdent);

	if (Token() != ':')
		return ReportError("Expected ':' after event name");

	ATVMDataValue value;
	if (!ParseDataValue(value))
		return false;

	if (!value.IsScript())
		return ReportError("Expected inline script");

	if (Token() != ';')
		return ReportError("Expected ';' at end of event binding");

	return mpBindEvent(*this, eventName.c_str(), *value.mpScript);
}

bool ATVMCompiler::ParseOption() {
	if (Token() != kTokStringLiteral)
		return ReportError("Option name expected");

	VDStringA optionName(mTokIdent);

	if (Token() != ':')
		return ReportError("Expected ':' after option name");

	ATVMDataValue value;
	if (!ParseDataValue(value))
		return false;

	if (Token() != ';')
		return ReportError("Expected ';' at end of event binding");

	if (optionName == "debug") {
		// reject if any functions have been compiled or queued
		if (!mDeferredCompiles.empty() || !mpDomain->mFunctions.empty())
			return ReportError("Option 'debug' must be set before any functions are declared");

		if (!value.IsInteger())
			return ReportError("Option 'debug' value must be an integer");

		mbCompileDebugCode = (value.mIntValue != 0);
		return true;
	}

	return mpSetOption(*this, optionName.c_str(), value);
}

bool ATVMCompiler::ParseFunction() {
	uint32 tok = Token();
	ATVMTypeInfo returnType;

	if (tok == kTokVoid)
		returnType = ATVMTypeInfo { ATVMTypeClass::Void };
	else if (tok == kTokInt)
		returnType = ATVMTypeInfo { ATVMTypeClass::Int };
	else
		return ReportError("Return type expected (int or void)");

	mpReturnType = &returnType;

	tok = Token();
	if (tok != kTokIdentifier)
		return ReportError("Function name expected");

	const VDStringA& name(mTokIdent);

	if (mpDomain->mFunctions.size() >= 256)
		return ReportError("Named function count limit exceeded (256 max)");

	if (mVariableLookup.find_as(name.c_str()) != mVariableLookup.end())
		return ReportError("Variable with same name has already been declared");

	auto r = mFunctionLookup.insert_as(name.c_str());
	bool alreadyDefined = false;

	ATVMFunction *func = nullptr;
	FunctionInfo *fi = r.first->second;
	if (!r.second) {
		func = const_cast<ATVMFunction *>(mpDomain->mFunctions[fi->mFunctionIndex]);

		if (func->mpByteCode)
			alreadyDefined = true;

		if (func->mReturnType != returnType)
			return ReportErrorF("Function '%s' previously declared with different return type", name.c_str());
	} else {
		fi = AllocTemp<FunctionInfo>();
		mFunctionInfoTable.push_back(fi);

		fi->mFunctionIndex = mpDomain->mFunctions.size();
		r.first->second = fi;

		// we might compile inline functions, so reserve this slot first
		func = mpDomain->mAllocator.Allocate<ATVMFunction>();
		char *persistentName = (char *)mpDomain->mAllocator.Allocate(name.size() + 1);
		memcpy(persistentName, name.c_str(), mTokIdent.size() + 1);
		func->mpName = persistentName;
		func->mReturnType = returnType;
		mpDomain->mFunctions.push_back(func);
	}

	tok = Token();
	if (tok != '(')
		return ReportError("Expected '('");

	tok = Token();
	if (tok != ')')
		return ReportError("Expected ')'");

	tok = Token();
	if (tok == ';') {
		// declaration
		return true;
	} else {
		// definition
		if (alreadyDefined)
			return ReportErrorF("Function '%s' has already been declared", name.c_str());

		if (tok != '{')
			return ReportError("Expected '{'");

		fi->mDefinitionPos = (sint32)(mpSrc - mpSrc0);

		mpCurrentFunctionInfo = fi;
		bool success = ParseFunction(func, returnType, (ATVMFunctionFlags)~UINT32_C(0), ATVMConditionalMask::None);
		mpCurrentFunctionInfo = nullptr;

		if (!success)
			return false;

		tok = Token();
		if (tok != '}')
			return ReportError("Expected '}' at end of function");

		return success;
	}
}

bool ATVMCompiler::ParseFunction(ATVMFunction *func, const ATVMTypeInfo& returnType, ATVMFunctionFlags asyncAllowedMask, ATVMConditionalMask conditionalMask) {
	bool hasReturn = false;

	mMethodPtrs.clear();
	mByteCodeBuffer.clear();
	mLocalLookup.clear();
	
	mpCurrentFunction = func;
	mAsyncAllowedMask = asyncAllowedMask;
	mConditionalMask = conditionalMask;

	if (mbCompileDebugCode)
		mConditionalMask |= ATVMConditionalMask::DebugEnabled;
	else
		mConditionalMask |= ATVMConditionalMask::NonDebugEnabled;

	func->mLocalSlotsRequired = 0;

	if (!ParseBlock(hasReturn))
		return false;

	if (!hasReturn) {
		if (returnType.mClass == ATVMTypeClass::Void)
			mByteCodeBuffer.push_back((uint8)ATVMOpcode::ReturnVoid);
		else
			return ReportError("No return at end of function");
	}

	if (!mPendingBranchTargets.empty())
		return ReportError("Internal compiler error: Unresolved branch targets");

	if (mBreakTargetLabels.size() != 1)
		return ReportError("Internal compiler error: Break stack invalid after function");

	uint32 bclen = (uint32)mByteCodeBuffer.size();

	uint8 *byteCode = (uint8 *)mpDomain->mAllocator.Allocate(bclen);
	memcpy(byteCode, mByteCodeBuffer.data(), bclen);

	void (**methodTable)() = nullptr;
	
	if (!mMethodPtrs.empty()) {
		methodTable = mpDomain->mAllocator.AllocateArray<void (*)()>(mMethodPtrs.size());
		memcpy(methodTable, mMethodPtrs.data(), mMethodPtrs.size() * sizeof(methodTable[0]));
	}

	func->mReturnType = returnType;
	func->mpByteCode = byteCode;
	func->mByteCodeLen = bclen;
	func->mpMethodTable = methodTable;
	func->mStackSlotsRequired = 0;

	struct StackState {
		sint32 mParent;
		TypeClass mTopType;
		uint16 mDepth;
	};

	struct StackStatePred {
		bool operator()(const StackState& x, const StackState& y) const {
			return x.mParent == y.mParent
				&& x.mTopType == y.mTopType;
		}

		size_t operator()(const StackState& x) const {
			return x.mParent ^ ((uint32)x.mTopType << 16);
		}
	};

	vdfastvector<sint32> ipToStackId(bclen, -1);

	vdfastvector<StackState> stackStateList;
	vdhashmap<StackState, uint32, StackStatePred, StackStatePred> stackStateLookup;

	stackStateList.push_back(StackState{});
	stackStateLookup[StackState{}] = 0;

	struct TraversalTarget {
		sint32 mIP;
		sint32 mStackState;
	};

	vdfastvector<TraversalTarget> traversalStack;

	traversalStack.push_back({0, 0});

	while(!traversalStack.empty()) {
		auto [ip, stackId] = traversalStack.back();
		traversalStack.pop_back();

		for(;;) {
			if (ip < 0 || (uint32)ip >= bclen)
				return ReportError("Internal compiler error: Bytecode validation failed (invalid branch target)");

			if (ipToStackId[ip] < 0)
				ipToStackId[ip] = stackId;
			else {
				if (ipToStackId[ip] != stackId)
					return ReportError("Internal compiler error: Bytecode validation failed (stack mismatch)");

				break;
			}

			ATVMOpcode opcode = (ATVMOpcode)byteCode[ip];
			uint32 popCount = 0;
			TypeClass pushType = TypeClass::Void;

			switch(opcode) {
				case ATVMOpcode::Nop:
				case ATVMOpcode::ReturnVoid:
				case ATVMOpcode::IntNeg:
				case ATVMOpcode::IntNot:
				case ATVMOpcode::Jmp:
				case ATVMOpcode::Ljmp:
				case ATVMOpcode::LoopChk:
				case ATVMOpcode::Not:
					// no stack change
					break;

				case ATVMOpcode::Dup:
					pushType = stackStateList[stackId].mTopType;
					break;

				case ATVMOpcode::IVLoad:
				case ATVMOpcode::ILLoad:
				case ATVMOpcode::ISLoad:
				case ATVMOpcode::ITLoad:
				case ATVMOpcode::IntConst:
				case ATVMOpcode::IntConst8:
					pushType = TypeClass::Int;
					break;

				case ATVMOpcode::MethodCallVoid:
					popCount = byteCode[ip+1] + 1;
					break;

				case ATVMOpcode::MethodCallInt:
					popCount = byteCode[ip+1];
					break;

				case ATVMOpcode::StaticMethodCallVoid:
				case ATVMOpcode::FunctionCallVoid:
					popCount = byteCode[ip+1];
					break;

				case ATVMOpcode::StaticMethodCallInt:
				case ATVMOpcode::FunctionCallInt:
					popCount = byteCode[ip+1];
					pushType = TypeClass::Int;
					break;

				case ATVMOpcode::IntAdd:
				case ATVMOpcode::IntSub:
				case ATVMOpcode::IntMul:
				case ATVMOpcode::IntDiv:
				case ATVMOpcode::IntMod:
				case ATVMOpcode::IntAnd:
				case ATVMOpcode::IntOr:
				case ATVMOpcode::IntXor:
				case ATVMOpcode::IntAsr:
				case ATVMOpcode::IntAsl:
				case ATVMOpcode::IVStore:
				case ATVMOpcode::ILStore:
				case ATVMOpcode::And:
				case ATVMOpcode::Or:
				case ATVMOpcode::IntLt:
				case ATVMOpcode::IntLe:
				case ATVMOpcode::IntGt:
				case ATVMOpcode::IntGe:
				case ATVMOpcode::IntEq:
				case ATVMOpcode::IntNe:
				case ATVMOpcode::Jz:
				case ATVMOpcode::Jnz:
				case ATVMOpcode::Ljz:
				case ATVMOpcode::Ljnz:
				case ATVMOpcode::Pop:
				case ATVMOpcode::ReturnInt:
					popCount = 1;
					break;

				default:
					return ReportError("Bytecode validation failed (unhandled opcode)");
			}

			while(popCount--) {
				if (stackId == 0)
					return ReportError("Bytecode validation failed (stack underflow)");

				stackId = stackStateList[stackId].mParent;
			}

			if (pushType != TypeClass::Void) {
				StackState newStackState { stackId, pushType, (uint16)(stackStateList[stackId].mDepth + 1) };

				auto r = stackStateLookup.insert(newStackState);
				if (r.second) {
					r.first->second = (sint32)stackStateList.size();
					stackStateList.push_back(newStackState);

					if (func->mStackSlotsRequired < newStackState.mDepth)
						func->mStackSlotsRequired = newStackState.mDepth;

				}

				stackId = r.first->second;
			}

			if (opcode == ATVMOpcode::Jnz || opcode == ATVMOpcode::Jz) {
				sint32 branchTarget = ip + (sint8)byteCode[ip + 1] + 2;

				traversalStack.push_back({branchTarget, stackId});
			} else if (opcode == ATVMOpcode::Ljnz || opcode == ATVMOpcode::Ljz) {
				sint32 branchTarget = ip + VDReadUnalignedLES32(&byteCode[ip + 1]) + 5;

				traversalStack.push_back({branchTarget, stackId});
			} else if (opcode == ATVMOpcode::Jmp) {
				ip += (sint8)byteCode[ip + 1] + 2;
				continue;
			} else if (opcode == ATVMOpcode::Ljmp) {
				ip += VDReadUnalignedLES32(&byteCode[ip + 1]) + 5;
				continue;
			} else if (opcode == ATVMOpcode::ReturnInt || opcode == ATVMOpcode::ReturnVoid) {
				break;
			} else if (opcode == ATVMOpcode::ILStore || opcode == ATVMOpcode::ILLoad) {
				if (byteCode[ip + 1] >= func->mLocalSlotsRequired)
					return ReportError("Bytecode validation failed (invalid local index)");
			}

			ip += ATVMGetOpcodeLength(opcode);
		}
	}

	func->mStackSlotsRequired += func->mLocalSlotsRequired;

	mpCurrentFunction = nullptr;

	return true;
}

bool ATVMCompiler::ParseBlock(bool& hasReturn) {
	for(;;) {
		uint32 tok = Token();
		if (tok == kTokError)
			return false;

		if (tok == '}') {
			Push(tok);
			break;
		}

		if (tok == kTokEnd)
			break;

		if (!ParseStatement(tok, hasReturn))
			return false;
	}

	return true;
}

bool ATVMCompiler::ParseStatement(uint32 tok, bool& hasReturn) {
	if (tok == '[') {
		tok = Token();

		bool inv = false;
		if (tok == '!') {
			tok = Token();
			inv = true;
		}

		if (tok != kTokIdentifier)
			return ReportError("Expected attribute name");

		ATVMConditionalMask condition;
		bool checkAllowed = false;

		if (mTokIdent == "debug_read") {
			condition = inv ? ATVMConditionalMask::NonDebugReadEnabled : ATVMConditionalMask::DebugReadEnabled;
			checkAllowed = true;
		} else if (mTokIdent == "debug")
			condition = inv ? ATVMConditionalMask::NonDebugEnabled : ATVMConditionalMask::DebugEnabled;
		else
			return ReportErrorF("Unrecognized attribute '%.*s'", (int)mTokIdent.size(), mTokIdent.size());

		if (Token() != ']')
			return ReportError("Expected ']' after attribute name");

		if (checkAllowed) {
			if (!((uint32)mConditionalMask & (uint32)ATVMConditionalMask::Allowed))
				return ReportError("Conditional attributes not supported in this function");
		}

		if (!((uint32)mConditionalMask & (uint32)condition)) {
			uint32 braceLevel = 0;

			for(;;) {
				tok = Token();

				if (tok == kTokError)
					return false;

				if (tok == kTokEnd)
					return ReportError("Encountered end of file while looking for end of statement");

				if (tok == '{') {
					++braceLevel;
				} else if (tok == '}') {
					--braceLevel;

					if (!braceLevel) {
						tok = Token();

						if (tok != kTokElse) {
							Push(tok);
							break;
						}
					}
				} else if (braceLevel == 0 && tok == ';')
					break;
			}

			return true;
		}

		tok = Token();
	}

	if (tok == kTokIf) {
		if (!ParseIfStatement(hasReturn))
			return false;

		return true;
	} else if (tok == kTokReturn) {
		if (!ParseReturnStatement())
			return false;

		hasReturn = true;
	} else if (tok == kTokLoop) {
		if (!ParseLoopStatement(hasReturn))
			return false;

		return true;
	} else if (tok == kTokDo) {
		if (!ParseDoWhileStatement())
			return false;
	} else if (tok == kTokWhile) {
		if (!ParseWhileStatement())
			return false;

		return true;
	} else if (tok == kTokBreak) {
		if (!ParseBreakStatement())
			return false;

		// Break ends the execution path. It won't have issued a proper return if
		// used at the top level of the function, but we won't have gotten here
		// if that's the case as it's ill-formed outside of a loop.
		hasReturn = true;
	} else if (tok == kTokInt) {
		if (!ParseDeclStatement(tok))
			return false;

		return true;
	} else if (tok == '{') {
		uint32 localLevel = mpCurrentFunction->mLocalSlotsRequired;

		if (!ParseBlock(hasReturn))
			return false;

		tok = Token();
		if (tok != '}')
			return ReportError("Expected '}' at end of block");

		// remove all local variables added within the scope, but do not reuse their
		// stack slots -- we currently avoid scoreboarding so as to not risk values
		// from dead locals appearing as uninitialized data in others, since we don't
		// have a proper dead store elimination pass to optimize
		if (localLevel != mpCurrentFunction->mLocalSlotsRequired) {
			for(auto it = mLocalLookup.begin(); it != mLocalLookup.end(); ) {
				if (it->second.mIndex >= localLevel)
					it = mLocalLookup.erase(it);
				else
					++it;
			}
		}

		return true;
	} else {
		if (!ParseExpressionStatement(tok))
			return false;
	}

	tok = Token();
	if (tok != ';')
		return ReportError("Expected ';' at end of statement");

	return true;
}

bool ATVMCompiler::ParseIfStatement(bool& hasReturn) {
	if (Token() != '(')
		return ReportError("Expected '(' before if condition");

	if (!ParseIntExpression())
		return false;

	if (Token() != ')')
		return ReportError("Expected ')' after if condition");

	mByteCodeBuffer.push_back((uint8)ATVMOpcode::Ljz);
	mByteCodeBuffer.push_back(0);
	mByteCodeBuffer.push_back(0);
	mByteCodeBuffer.push_back(0);
	mByteCodeBuffer.push_back(0);

	uint32 branchAnchor = (uint32)mByteCodeBuffer.size();

	bool trueBranchHasReturn = false;
	if (!ParseStatement(Token(), trueBranchHasReturn))
		return false;

	uint32 tok = Token();
	bool hasElse = false;
	if (tok == kTokElse)
		hasElse = true;
	else
		Push(tok);

	if (hasElse) {
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::Ljmp);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
	}

	uint32 branchDistance = (uint32)mByteCodeBuffer.size() - branchAnchor;
	VDWriteUnalignedLEU32(&mByteCodeBuffer[branchAnchor - 4], branchDistance);

	uint32 elseBranchAnchor = (uint32)mByteCodeBuffer.size();

	if (hasElse) {
		bool falseBranchHasReturn = false;
		if (!ParseStatement(Token(), falseBranchHasReturn))
			return false;

		uint32 elseBranchDistance = (uint32)mByteCodeBuffer.size() - elseBranchAnchor;

		VDWriteUnalignedLEU32(&mByteCodeBuffer[elseBranchAnchor - 4], elseBranchDistance);

		if (trueBranchHasReturn && falseBranchHasReturn)
			hasReturn = true;
	}

	return true;
}

bool ATVMCompiler::ParseReturnStatement() {
	uint32 tok = Token();
	if (tok == ';') {
		if (mpReturnType->mClass != ATVMTypeClass::Void)
			return ReportError("Return value required");

		Push(tok);
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::ReturnVoid);
		return true;
	}

	Push(tok);

	TypeInfo returnType;
	if (!ParseExpression(returnType))
		return false;

	ConvertToRvalue(returnType);

	if (returnType.mClass != mpReturnType->mClass || (returnType.mClass == ATVMTypeClass::Object && returnType.mpObjectClass != mpReturnType->mpObjectClass))
		return ReportError("Return type mismatch");

	if (returnType.mClass == TypeClass::Void)
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::ReturnVoid);
	else if (returnType.mClass == TypeClass::Int)
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::ReturnInt);
	else
		return ReportError("Cannot return expression type");

	return true;
}

bool ATVMCompiler::ParseLoopStatement(bool& hasReturn) {
	const uint32 branchAnchor = (uint32)mByteCodeBuffer.size();

	BeginLoop();

	bool loopBodyHasReturn = false;
	if (!ParseStatement(Token(), loopBodyHasReturn))
		return false;

	mByteCodeBuffer.push_back((uint8)ATVMOpcode::LoopChk);
	JumpToOffset(branchAnchor);

	const bool hadBreak = EndLoop();

	// if there is no break in the loop body and the return type is void,
	// we can mark the path as terminated
	if (!hadBreak && mpReturnType->mClass == ATVMTypeClass::Void)
		hasReturn = true;

	return true;
}

bool ATVMCompiler::ParseDoWhileStatement() {
	BeginLoop();

	const uint32 branchAnchor = (uint32)mByteCodeBuffer.size();

	bool loopBodyHasReturn = false;
	if (!ParseStatement(Token(), loopBodyHasReturn))
		return false;

	if (Token() != kTokWhile)
		return ReportError("Expected 'while' after 'do' and do-while loop body");

	if (Token() != '(')
		return ReportError("Expected '(' after 'while'");

	if (!ParseIntExpression())
		return false;

	if (Token() != ')')
		return ReportError("Expected ')' after while condition");

	JumpToOffset(ATVMOpcode::Ljnz, branchAnchor);

	EndLoop();
	return true;
}

bool ATVMCompiler::ParseWhileStatement() {
	const auto breakLabel = BeginLoop();

	if (Token() != '(')
		return ReportError("Expected '(' after 'while'");

	const uint32 branchAnchor = (uint32)mByteCodeBuffer.size();

	if (!ParseIntExpression())
		return false;

	if (Token() != ')')
		return ReportError("Expected ')' after while condition");

	mByteCodeBuffer.push_back((uint8)ATVMOpcode::Ljz);
	EmitPendingBranchTarget(breakLabel);

	bool loopBodyHasReturn = false;
	if (!ParseStatement(Token(), loopBodyHasReturn))
		return false;

	JumpToOffset(branchAnchor);

	EndLoop();
	return true;
}

bool ATVMCompiler::ParseBreakStatement() {
	mByteCodeBuffer.push_back((uint8)ATVMOpcode::Ljmp);
	return EmitBreakTarget();
}

bool ATVMCompiler::ParseDeclStatement(uint32 typeToken) {
	VDASSERT(typeToken == kTokInt);

	for(;;) {
		uint32 tok = Token();

		if (tok != kTokIdentifier)
			return ReportError("Expected variable name");

		auto ins = mLocalLookup.insert_as(mTokIdent);
		if (!ins.second)
			return ReportErrorF("Local variable '%.*s' already declared", (int)mTokIdent.size(), mTokIdent.data());

		uint32 varIndex = mpCurrentFunction->mLocalSlotsRequired;
		ins.first->second = ATVMTypeInfo { ATVMTypeClass::IntLValueLocal, varIndex };
		++mpCurrentFunction->mLocalSlotsRequired;

		tok = Token();

		// check for initializer
		if (tok == '=') {
			if (!ParseIntExpression())
				return false;

			mByteCodeBuffer.push_back((uint8)ATVMOpcode::ILStore);
			mByteCodeBuffer.push_back((uint8)varIndex);

			tok = Token();
		}

		if (tok == ';')
			break;

		if (tok != ',')
			return ReportError("Expected ';' or ',' after variable name");
	}

	return true;
}

bool ATVMCompiler::ParseExpressionStatement(uint32 tok) {
	Push(tok);

	TypeInfo returnType;
	if (!ParseExpression(returnType))
		return false;

	// discard rvalues, and don't bother doing lvalue conversion as we don't need it on the stack
	if (returnType.mClass == TypeClass::Int || returnType.mClass == TypeClass::Object)
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::Pop);

	return true;
}

// dataValue := dataObject | dataArray | expr
// dataValues = dataValue | dataValue ',' dataValues
// dataArray := '[' dataValues? ']'
// dataObject := '{' dataMembers? '}'
// dataMembers := dataMember | dataMember ',' dataMembers
// dataMember := identifier ':' dataValue

bool ATVMCompiler::ParseDataValue(ATVMDataValue& value) {
	uint32 tok = Token();

	value.mSrcOffset = (uint32)(mpSrc - mpSrc0);

	if (tok == '[') {
		ATVMDataValue *array = nullptr;
		vdfastvector<ATVMDataValue> arrayElements;

		for(;;) {
			tok = Token();
			if (tok == ']')
				break;

			Push(tok);
			arrayElements.push_back();

			if (!ParseDataValue(arrayElements.back()))
				return false;

			tok = Token();
			if (tok == ']')
				break;

			if (tok != ',')
				return ReportError("Expected ',' or ']' after data array element");
		}

		array = mpDomain->mAllocator.AllocateArray<ATVMDataValue>(arrayElements.size());

		std::copy(arrayElements.begin(), arrayElements.end(), array);

		value.mType = ATVMDataType::Array;
		value.mLength = (uint32)arrayElements.size();
		value.mpArrayElements = array;
	} else if (tok == '{') {
		ATVMDataMember *members = nullptr;

		struct ATVMDataMemberHash {
			size_t operator()(const ATVMDataMember& m) const {
				return m.mNameHash;
			}
		};

		struct ATVMDataMemberPred {
			bool operator()(const ATVMDataMember& a, const ATVMDataMember& b) const {
				return a.mNameHash == b.mNameHash && !strcmp(a.mpName, b.mpName);
			}
		};

		std::unordered_set<ATVMDataMember, ATVMDataMemberHash, ATVMDataMemberPred> memberElements;

		for(;;) {
			tok = Token();

			if (tok == '}')
				break;

			if (tok != kTokIdentifier)
				return ReportError("Expected data member name");

			tok = Token();
			if (tok != ':')
				return ReportError("Expected ':' after data member name");

			ATVMDataMember mb {};
			size_t nameLen = mTokIdent.size();
			mb.mNameHash = VDHashString32(mTokIdent.data(), nameLen);
			char *name = (char *)mpDomain->mAllocator.Allocate(nameLen + 1); 
			mb.mpName = name;
			memcpy(name, mTokIdent.data(), nameLen);
			name[nameLen] = 0;

			if (!ParseDataValue(mb.mValue))
				return false;

			if (!memberElements.insert(mb).second)
				return ReportErrorF("Member '%s' has already been defined in this object", name);

			tok = Token();
			if (tok == '}')
				break;

			if (tok != ',')
				return ReportErrorF("Expected ',' or '}' after data object member '%s'", name);
		}

		members = mpDomain->mAllocator.AllocateArray<ATVMDataMember>(memberElements.size());
		std::copy(memberElements.begin(), memberElements.end(), members);

		value.mType = ATVMDataType::DataObject;
		value.mLength = (uint32)memberElements.size();
		value.mpObjectMembers = members;
	} else if (tok == kTokFunction) {
		if (!ParseInlineScript(value))
			return false;
	} else {
		Push(tok);

		ATVMTypeInfo valueType;
		if (!ParseConstantExpression(valueType))
			return false;

		if (valueType.mClass == TypeClass::IntConst) {
			value.mType = ATVMDataType::Int;
			value.mLength = 0;
			value.mIntValue = (sint32)valueType.mIndex;
		} else if (valueType.mClass == TypeClass::StringConst) {
			value.mType = ATVMDataType::String;
			value.mLength = 0;
			value.mpStrValue = static_cast<const ATVMStringObject *>(mpDomain->mGlobalObjects[valueType.mIndex])->c_str();
		} else if (valueType.mClass == TypeClass::ObjectLValue) {
			value.mType = ATVMDataType::RuntimeObject;
			value.mLength = mpDomain->mGlobalVariables[valueType.mIndex];
			value.mpRuntimeObjectClass = valueType.mpObjectClass;
		} else
			return ReportError("Cannot use this type in a data object");
	}

	return true;
}

bool ATVMCompiler::ParseConstantExpression(TypeInfo& returnType) {
	return ParseConstantValue(returnType);
}

bool ATVMCompiler::ParseIntExpression() {
	TypeInfo returnType;
	if (!ParseExpression(returnType))
		return false;

	ConvertToRvalue(returnType);

	if (returnType.mClass != TypeClass::Int)
		return ReportError("Integer expression required");

	return true;
}

bool ATVMCompiler::ParseExpression(TypeInfo& returnType) {
	return ParseAssignmentExpression(returnType);
}

bool ATVMCompiler::ParseAssignmentExpression(TypeInfo& returnType) {
	if (!ParseLogicalOrExpression(returnType))
		return false;

	uint32 tok = Token();

	if (tok != '=') {
		Push(tok);
		return true;
	}

	if (returnType.mClass != TypeClass::IntLValueVariable && returnType.mClass != TypeClass::IntLValueLocal)
		return ReportError("Left side of assignment must be assignable variable");

	TypeInfo arg2Type;
	if (!ParseLogicalOrExpression(arg2Type))
		return false;

	ConvertToRvalue(arg2Type);

	if (arg2Type.mClass != TypeClass::Int)
		return ReportError("Right side of assignment must be integer expression");

	if (returnType.mClass == TypeClass::IntLValueVariable)
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::IVStore);
	else
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::ILStore);

	mByteCodeBuffer.push_back((uint8)returnType.mIndex);

	returnType = TypeInfo { TypeClass::Void };
	return true;
}

bool ATVMCompiler::ParseLogicalOrExpression(TypeInfo& returnType) {
	if (!ParseLogicalAndExpression(returnType))
		return false;

	for(;;) {
		uint32 tok = Token();
		if (tok != kTokLogicalOr) {
			Push(tok);
			return true;
		}
	
		ConvertToRvalue(returnType);

		if (returnType.mClass != TypeClass::Int)
			return ReportError("Logical operator can only be applied to integer arguments");

		mByteCodeBuffer.push_back((uint8)ATVMOpcode::Dup);
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::Ljnz);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);

		const auto branchAnchor = mByteCodeBuffer.size();
	
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::Pop);

		if (!ParseLogicalAndExpression(returnType))
			return false;

		ConvertToRvalue(returnType);

		const uint32 branchDistance = (uint32)(mByteCodeBuffer.size() - branchAnchor);

		VDWriteUnalignedLEU32(&mByteCodeBuffer[branchAnchor - 4], branchDistance);
	}
}

bool ATVMCompiler::ParseLogicalAndExpression(TypeInfo& returnType) {
	if (!ParseEqualityExpression(returnType))
		return false;

	for(;;) {
		uint32 tok = Token();
		if (tok != kTokLogicalAnd) {
			Push(tok);
			return true;
		}
	
		ConvertToRvalue(returnType);

		if (returnType.mClass != TypeClass::Int)
			return ReportError("Logical operator can only be applied to integer arguments");

		mByteCodeBuffer.push_back((uint8)ATVMOpcode::Dup);
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::Ljz);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);

		const auto branchAnchor = mByteCodeBuffer.size();
	
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::Pop);

		if (!ParseEqualityExpression(returnType))
			return false;

		ConvertToRvalue(returnType);

		const uint32 branchDistance = (uint32)(mByteCodeBuffer.size() - branchAnchor);

		VDWriteUnalignedLEU32(&mByteCodeBuffer[branchAnchor - 4], branchDistance);
	}
}

bool ATVMCompiler::ParseEqualityExpression(TypeInfo& returnType) {
	if (!ParseRelationalExpression(returnType))
		return false;

	for(;;) {
		uint32 tok = Token();
		if (tok != kTokEq && tok != kTokNe) {
			Push(tok);
			return true;
		}

		ConvertToRvalue(returnType);

		TypeInfo arg2Type;
		if (!ParseRelationalExpression(arg2Type))
			return false;

		ConvertToRvalue(arg2Type);

		if (returnType.mClass != arg2Type.mClass)
			return ReportError("Equality operator can only be applied to arguments of same type");

		if (tok == kTokEq)
			mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntEq);
		else if (tok == kTokNe)
			mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntNe);
	}
}

bool ATVMCompiler::ParseRelationalExpression(TypeInfo& returnType) {
	if (!ParseBitwiseOrExpression(returnType))
		return false;

	for(;;) {
		uint32 tok = Token();
		if (tok != '<' && tok != '>' && tok != kTokLe && tok != kTokGe) {
			Push(tok);
			return true;
		}

		ConvertToRvalue(returnType);

		TypeInfo arg2Type;
		if (!ParseBitwiseOrExpression(arg2Type))
			return false;

		ConvertToRvalue(arg2Type);

		if (returnType.mClass != TypeClass::Int || arg2Type.mClass != TypeClass::Int)
			return ReportError("Relational operator can only be applied to integer arguments");

		if (tok == '<')
			mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntLt);
		else if (tok == '>')
			mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntGt);
		else if (tok == kTokLe)
			mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntLe);
		else if (tok == kTokGe)
			mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntGe);
	}
}

bool ATVMCompiler::ParseBitwiseOrExpression(TypeInfo& returnType) {
	if (!ParseBitwiseXorExpression(returnType))
		return false;

	for(;;) {
		uint32 tok = Token();
		if (tok != '|') {
			Push(tok);
			return true;
		}

		ConvertToRvalue(returnType);

		TypeInfo arg2Type;
		if (!ParseBitwiseXorExpression(arg2Type))
			return false;

		ConvertToRvalue(arg2Type);

		if (returnType.mClass != TypeClass::Int || arg2Type.mClass != TypeClass::Int)
			return ReportError("Bitwise operator can only be applied to integer arguments");

		mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntOr);
	}
}

bool ATVMCompiler::ParseBitwiseXorExpression(TypeInfo& returnType) {
	if (!ParseBitwiseAndExpression(returnType))
		return false;

	for(;;) {
		uint32 tok = Token();
		if (tok != '^') {
			Push(tok);
			return true;
		}

		ConvertToRvalue(returnType);

		TypeInfo arg2Type;
		if (!ParseBitwiseAndExpression(arg2Type))
			return false;

		ConvertToRvalue(arg2Type);

		if (returnType.mClass != TypeClass::Int || arg2Type.mClass != TypeClass::Int)
			return ReportError("Bitwise operator can only be applied to integer arguments");

		mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntXor);
	}
}

bool ATVMCompiler::ParseBitwiseAndExpression(TypeInfo& returnType) {
	if (!ParseShiftExpression(returnType))
		return false;

	for(;;) {
		uint32 tok = Token();
		if (tok != '&') {
			Push(tok);
			return true;
		}

		ConvertToRvalue(returnType);

		TypeInfo arg2Type;
		if (!ParseShiftExpression(arg2Type))
			return false;

		ConvertToRvalue(arg2Type);

		if (returnType.mClass != TypeClass::Int || arg2Type.mClass != TypeClass::Int)
			return ReportError("Bitwise operator can only be applied to integer arguments");

		mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntAnd);
	}
}

bool ATVMCompiler::ParseShiftExpression(TypeInfo& returnType) {
	if (!ParseAdditiveExpression(returnType))
		return false;

	for(;;) {
		uint32 tok = Token();
		if (tok != kTokLeftShift && tok != kTokRightShift) {
			Push(tok);
			return true;
		}

		ConvertToRvalue(returnType);

		TypeInfo arg2Type;
		if (!ParseAdditiveExpression(arg2Type))
			return false;

		ConvertToRvalue(arg2Type);

		if (returnType.mClass != TypeClass::Int || arg2Type.mClass != TypeClass::Int)
			return ReportError("Additive operator can only be applied to integer arguments");

		if (tok == kTokLeftShift)
			mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntAsl);
		else if (tok == kTokRightShift)
			mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntAsr);
	}
}

bool ATVMCompiler::ParseAdditiveExpression(TypeInfo& returnType) {
	if (!ParseMultiplicativeExpression(returnType))
		return false;

	for(;;) {
		uint32 tok = Token();
		if (tok != '+' && tok != '-') {
			Push(tok);
			return true;
		}

		ConvertToRvalue(returnType);

		TypeInfo arg2Type;
		if (!ParseMultiplicativeExpression(arg2Type))
			return false;

		ConvertToRvalue(arg2Type);

		if (returnType.mClass != TypeClass::Int || arg2Type.mClass != TypeClass::Int)
			return ReportError("Additive operator can only be applied to integer arguments");

		if (tok == '+')
			mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntAdd);
		else if (tok == '-')
			mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntSub);
	}
}

bool ATVMCompiler::ParseMultiplicativeExpression(TypeInfo& returnType) {
	if (!ParseUnaryExpression(returnType))
		return false;

	for(;;) {
		uint32 tok = Token();
		if (tok != '*' && tok != '/' && tok != '%') {
			Push(tok);
			return true;
		}

		ConvertToRvalue(returnType);

		TypeInfo arg2Type;
		if (!ParseUnaryExpression(arg2Type))
			return false;

		ConvertToRvalue(arg2Type);

		if (returnType.mClass != TypeClass::Int || arg2Type.mClass != TypeClass::Int)
			return ReportError("Multiplicative operator can only be applied to integer arguments");

		if (tok == '*')
			mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntMul);
		else if (tok == '/')
			mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntDiv);
		else if (tok == '%')
			mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntMod);
	}
}

bool ATVMCompiler::ParseUnaryExpression(TypeInfo& returnType) {
	uint32 tok = Token();
	if (tok == kTokIncrement || tok == kTokDecrement)
		return ReportError("Preincrement/decrement operators not supported");

	if (tok != '+' && tok != '-' && tok != '~' && tok != '!') {
		Push(tok);
		return ParsePostfixExpression(returnType);
	}

	if (!ParseUnaryExpression(returnType))
		return false;

	ConvertToRvalue(returnType);

	if (returnType.mClass != TypeClass::Int)
		return ReportError("Unary operator can only be applied to integers");

	if (tok == '-')
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntNeg);
	else if (tok == '~')
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntNot);
	else if (tok == '!')
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::Not);

	return true;
}

bool ATVMCompiler::ParsePostfixExpression(TypeInfo& returnType) {
	if (!ParseValue(returnType))
		return false;

	uint32 tok = Token();

	if (tok == kTokIncrement || tok == kTokDecrement)
		return ReportError("Postincrement/decrement operators not supported");

	if (tok != '.') {
		Push(tok);

		if (returnType.mClass == ATVMTypeClass::IntConst)
			ConvertToRvalue(returnType);

		return true;
	}

	ConvertToRvalue(returnType);

	if (returnType.mClass != TypeClass::Object && returnType.mClass != TypeClass::ObjectClass)
		return ReportError("'.' operator can only be used on object");

	tok = Token();
	if (tok != kTokIdentifier)
		return ReportError("Expected method name after '.' operator");

	const ATVMExternalMethod *foundMethod = nullptr;
	for(const ATVMExternalMethod& method : returnType.mpObjectClass->mMethods) {
		if (mTokIdent == method.mpName) {
			foundMethod = &method;
			break;
		}
	}

	if (!foundMethod)
		return ReportErrorF("Class '%s' does not have method called '%.*s'", returnType.mpObjectClass->mpClassName, (int)mTokIdent.size(), mTokIdent.data());

	const bool isStaticCall = (returnType.mClass == TypeClass::ObjectClass);
	if ((foundMethod->mFlags & ATVMFunctionFlags::Static) != ATVMFunctionFlags::None)
	{
		if (!isStaticCall)
			return ReportErrorF("Static method '%.*s' must be called on a class instance", (int)mTokIdent.size(), mTokIdent.data());
	}
	else
	{
		if (isStaticCall)
			return ReportErrorF("Instance method '%.*s' must be called on an object instance", (int)mTokIdent.size(), mTokIdent.data());
	}

	ATVMFunctionFlags funcAsyncMask = foundMethod->mFlags & ATVMFunctionFlags::AsyncAll;

	if (funcAsyncMask != ATVMFunctionFlags::None) {
		if ((~mAsyncAllowedMask & funcAsyncMask) != ATVMFunctionFlags::None) {
			if (mAsyncAllowedMask != ATVMFunctionFlags::None)
				return ReportErrorF("Cannot call '%.*s' as it can suspend in a mode not supported by the current context", (int)mTokIdent.size(), mTokIdent.data());
			else
				return ReportErrorF("Cannot call '%.*s' as it can suspend, which is not supported by the current context", (int)mTokIdent.size(), mTokIdent.data());
		}

		mpCurrentFunctionInfo->mFlags |= funcAsyncMask;
	}

	tok = Token();
	if (tok != '(')
		return ReportError("Expected '(' after method name");

	uint32 argCount = 0;

	tok = Token();
	if (tok != ')') {
		Push(tok);

		for(;;) {
			TypeInfo argType;
			if (!ParseExpression(argType))
				return false;

			ConvertToRvalue(argType);
			++argCount;

			if (argCount <= foundMethod->mNumArgs && *foundMethod->mpTypes[argCount] != argType)
				return ReportErrorF("Argument type mismatch on argument %u", argCount);

			tok = Token();

			if (tok == ')')
				break;

			if (tok != ',')
				return ReportError("Expected ',' or ')' after method argument");
		}
	}

	if (argCount != foundMethod->mNumArgs)
		return ReportErrorF("Method %s.%s() expects %u arguments, %u provided", returnType.mpObjectClass->mpClassName, foundMethod->mpName, foundMethod->mNumArgs, argCount);

	if (mMethodPtrs.size() >= 256)
		return ReportError("External method call limit exceeded");

	if (foundMethod->mpTypes[0]->mClass == ATVMTypeClass::Int) {
		mByteCodeBuffer.push_back(isStaticCall ? (uint8)ATVMOpcode::StaticMethodCallInt : (uint8)ATVMOpcode::MethodCallInt);
		mMethodPtrs.push_back((void(*)())foundMethod->mpIntMethod);
	} else {
		mByteCodeBuffer.push_back(isStaticCall ? (uint8)ATVMOpcode::StaticMethodCallVoid : (uint8)ATVMOpcode::MethodCallVoid);
		mMethodPtrs.push_back((void(*)())foundMethod->mpVoidMethod);
	}

	mByteCodeBuffer.push_back((uint8)argCount);
	mByteCodeBuffer.push_back((uint8)(mMethodPtrs.size() - 1));

	returnType = *foundMethod->mpTypes[0];
	return true;
}

bool ATVMCompiler::ParseValue(TypeInfo& returnType) {
	uint32 tok = Token();
	if (tok == kTokIdentifier) {
		if (auto it = mLocalLookup.find_as(mTokIdent); it != mLocalLookup.end()) {
			// local variable reference
			returnType = it->second;
		} else if (auto it = mVariableLookup.find_as(mTokIdent); it != mVariableLookup.end()) {
			// variable reference
			returnType = it->second;
		} else if (auto it = mClassLookup.find_as(mTokIdent); it != mClassLookup.end()) {
			// class reference
			returnType = it->second.mTypeInfo;
		} else if (auto it = mFunctionLookup.find_as(mTokIdent); it != mFunctionLookup.end()) {
			// function reference -- check if it is a call
			const FunctionInfo& fi = *it->second;
			const ATVMFunction& func = *mpDomain->mFunctions[fi.mFunctionIndex];

			tok = Token();
			if (tok != '(') {
				// no function call, we are returning a function pointer
				Push(tok);

				LoadConst(fi.mFunctionIndex);

				returnType = ATVMTypeInfo { ATVMTypeClass::FunctionPointer, GetFunctionPointerTypeIndex(vdvector_view(&func.mReturnType, 1)) };
				return true;
			}

			// we have a function call -- check for arguments (currently we don't support any)
			tok = Token();
			if (tok != ')')
				return ReportError("Expected ')' after function name");

			ATVMFunctionFlags funcAsyncMask = fi.mFlags & ATVMFunctionFlags::AsyncAll;

			if (funcAsyncMask != ATVMFunctionFlags::None) {
				if ((~mAsyncAllowedMask & funcAsyncMask) != ATVMFunctionFlags::None) {
					if (mAsyncAllowedMask != ATVMFunctionFlags::None)
						return ReportErrorF("Cannot call '%.*s' as it can suspend in a mode not supported by the current context", (int)mTokIdent.size(), mTokIdent.data());
					else
						return ReportErrorF("Cannot call '%.*s' as it can suspend, which is not supported by the current context", (int)mTokIdent.size(), mTokIdent.data());
				}

				mpCurrentFunctionInfo->mFlags |= funcAsyncMask;
			}

			mFunctionDependencies.emplace(mpCurrentFunctionInfo->mFunctionIndex, fi.mFunctionIndex);

			if (func.mReturnType.mClass == ATVMTypeClass::Int)
				mByteCodeBuffer.push_back((uint8)ATVMOpcode::FunctionCallInt);
			else
				mByteCodeBuffer.push_back((uint8)ATVMOpcode::FunctionCallVoid);

			mByteCodeBuffer.push_back((uint8)0);
			mByteCodeBuffer.push_back((uint8)fi.mFunctionIndex);

			returnType = func.mReturnType;
		} else
			return ReportErrorF("Unknown variable or function '%.*s'", (int)mTokIdent.size(), mTokIdent.data());

	} else if (tok == kTokSpecialIdent) {
		auto it = mSpecialVariableLookup.find(mTokIdent);

		if (it == mSpecialVariableLookup.end()) {
			it = mThreadVariableLookup.find(mTokIdent);
			if (it == mThreadVariableLookup.end())
				return ReportErrorF("Unknown special variable '$%.*s'", (int)mTokIdent.size(), mTokIdent.data());

			mByteCodeBuffer.push_back((uint8)ATVMOpcode::ITLoad);
		} else {
			if (mSpecialVariablesReferenced.size() <= it->second.mIndex)
				mSpecialVariablesReferenced.resize(it->second.mIndex + 1, false);

			mSpecialVariablesReferenced[it->second.mIndex] = true;

			mByteCodeBuffer.push_back((uint8)ATVMOpcode::ISLoad);
		}

		mByteCodeBuffer.push_back((uint8)it->second.mIndex);

		if (it->second.mClass == ATVMTypeClass::ObjectLValue)
			returnType = TypeInfo { TypeClass::Object, 0, it->second.mpObjectClass };
		else
			returnType = TypeInfo { TypeClass::Int };
	} else if (tok == kTokInteger || tok == kTokStringLiteral || tok == kTokTrue || tok == kTokFalse || tok == kTokFunction) {
		Push(tok);
		return ParseConstantValue(returnType);
	} else if (tok == '(') {
		if (!ParseLogicalOrExpression(returnType))
			return false;

		if (Token() != ')')
			return ReportError("Expected ')'");
	} else
		return ReportError("Expected expression value");

	return true;
}

bool ATVMCompiler::ParseConstantValue(TypeInfo& returnType) {
	uint32 tok = Token();

	if (tok == kTokInteger)
		returnType = TypeInfo { TypeClass::IntConst, (uint32)mTokValue };
	else if (tok == kTokTrue)
		returnType = TypeInfo { TypeClass::IntConst, 1 };
	else if (tok == kTokFalse)
		returnType = TypeInfo { TypeClass::IntConst, 0 };
	else if (tok == kTokStringLiteral) {
		const uint32 id = (uint32)mpDomain->mGlobalObjects.size();

		for(const char c : mTokIdent) {
			if (c < 0x20 || c >= 0x7f)
				return ReportError("String literals can only contain printable ASCII characters");
		}

		size_t len = mTokIdent.size();
		ATVMStringObject *sobj = (ATVMStringObject *)mpDomain->mAllocator.Allocate(sizeof(ATVMStringObject) + len + 1);
		char *s = (char *)(sobj + 1);

		memcpy(s, mTokIdent.data(), len);
		s[len] = 0;

		mpDomain->mGlobalObjects.push_back(sobj);

		returnType = TypeInfo { TypeClass::StringConst, id };
	} else if (tok == kTokIdentifier) {
		auto it = mVariableLookup.find_as(mTokIdent);
		
		if (it == mVariableLookup.end())
			return ReportErrorF("Unknown variable '%.*s'", (int)mTokIdent.size(), mTokIdent.data());

		// variable reference
		returnType = it->second;

		if (returnType.mClass != ATVMTypeClass::ObjectLValue)
			return ReportError("Expected constant value");
	} else if (tok == kTokFunction) {
		ATVMDataValue value;
		if (!ParseInlineScript(value))
			return false;

		const ATVMFunction *func = DeferCompile(kATVMTypeVoid, *value.mpScript, ATVMFunctionFlags::AsyncAll);
		mpDomain->mFunctions.push_back(func);

		LoadConst((sint32)(mpDomain->mFunctions.size() - 1));

		returnType = ATVMTypeInfo { ATVMTypeClass::FunctionPointer, 0 };
	} else
		return ReportError("Expected constant value");

	return true;
}

bool ATVMCompiler::ParseInlineScript(ATVMDataValue& value) {
	if (Token() != '{')
		return ReportError("Expected '}' after 'function'");

	ATVMScriptFragment *fragment = mpDomain->mAllocator.Allocate<ATVMScriptFragment>();

	fragment->mpSrc = mpSrc;

	uint32 nestingLevel = 1;
	const char *p;

	for(;;) {
		p = mpSrc;
		uint32 tok = Token();

		if (tok == kTokEnd)
			return ReportError("End of file encountered while parsing inline function");

		if (tok == '{')
			++nestingLevel;
		else if (tok == '}') {
			if (!--nestingLevel)
				break;
		}
	}

	fragment->mSrcLength = (size_t)(p - fragment->mpSrc);

	value.mType = ATVMDataType::Script;
	value.mLength = 0;
	value.mpScript = fragment;
	return true;
}

void ATVMCompiler::ConvertToRvalue(TypeInfo& returnType) {
	if (returnType.mClass == TypeClass::IntConst) {
		LoadConst((sint32)returnType.mIndex);

		returnType.mClass = TypeClass::Int;
		returnType.mIndex = 0;
	} else if (returnType.mClass == TypeClass::IntLValueVariable) {
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::IVLoad);
		mByteCodeBuffer.push_back((uint8)returnType.mIndex);

		returnType.mClass = TypeClass::Int;
		returnType.mIndex = 0;
	} else if (returnType.mClass == TypeClass::IntLValueLocal) {
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::ILLoad);
		mByteCodeBuffer.push_back((uint8)returnType.mIndex);

		returnType.mClass = TypeClass::Int;
		returnType.mIndex = 0;
	} else if (returnType.mClass == TypeClass::ObjectLValue) {
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::IVLoad);
		mByteCodeBuffer.push_back((uint8)returnType.mIndex);

		returnType.mClass = TypeClass::Object;
		returnType.mIndex = 0;
	} else if (returnType.mClass == TypeClass::StringConst) {
		LoadConst((sint32)returnType.mIndex);

		returnType.mClass = TypeClass::String;
		returnType.mIndex = 0;
	}
}

uint32 ATVMCompiler::GetFunctionPointerTypeIndex(vdvector_view<const ATVMTypeInfo> types) {
	auto it = mFunctionPointerTypeLookup.find(types);

	if (it != mFunctionPointerTypeLookup.end())
		return it->second;

	const uint32 index = (uint32)mFunctionPointerTypeLookup.size();

	ATVMTypeInfo *internedTypes = mpDomain->mAllocator.AllocateArray<ATVMTypeInfo>(types.size());
	std::copy(types.begin(), types.end(), internedTypes);

	mFunctionPointerTypeLookup.insert(vdvector_view(internedTypes, types.size())).first->second = index;
	return index;
}

void ATVMCompiler::LoadConst(sint32 v) {
	if (v >= -128 && v <= 127) {
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntConst8);
		mByteCodeBuffer.push_back((uint8)v);
	} else {
		mByteCodeBuffer.push_back((uint8)ATVMOpcode::IntConst);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);

		VDWriteUnalignedLES32(&*(mByteCodeBuffer.end() - 4), v);
	}
}

void ATVMCompiler::JumpToOffset(uint32 targetOffset) {
	JumpToOffset(ATVMOpcode::Ljmp, targetOffset);
}

void ATVMCompiler::JumpToOffset(ATVMOpcode opcode, uint32 targetOffset) {
	mByteCodeBuffer.push_back((uint8)opcode);
	mByteCodeBuffer.push_back(0);
	mByteCodeBuffer.push_back(0);
	mByteCodeBuffer.push_back(0);
	mByteCodeBuffer.push_back(0);

	sint32 branchDistance = targetOffset - (sint32)mByteCodeBuffer.size();
	VDWriteUnalignedLEU32(&*(mByteCodeBuffer.end() - 4), branchDistance);
}

uint32 ATVMCompiler::BeginLoop() {
	uint32 label = AllocLabel();
	mBreakTargetLabels.push_back(label);
	return label;
}

bool ATVMCompiler::EndLoop() {
	uint32 label = mBreakTargetLabels.back();
	mBreakTargetLabels.pop_back();

	return PatchLabel(label);
}

bool ATVMCompiler::EmitBreakTarget() {
	if (!mBreakTargetLabels.back())
		return ReportError("No loop for break statement");

	EmitPendingBranchTarget(mBreakTargetLabels.back());
	return true;
}

uint32 ATVMCompiler::AllocLabel() {
	return mLabelCounter++;
}

void ATVMCompiler::EmitPendingBranchTarget(uint32 label) {
	mByteCodeBuffer.push_back(0);
	mByteCodeBuffer.push_back(0);
	mByteCodeBuffer.push_back(0);
	mByteCodeBuffer.push_back(0);

	mPendingBranchTargets.push_back(
		PendingBranchTarget {
			label, 
			(uint32)mByteCodeBuffer.size()
		}
	);
}

bool ATVMCompiler::PatchLabel(uint32 label) {
	sint32 curOffset = (sint32)mByteCodeBuffer.size();
	auto it = mPendingBranchTargets.begin();
	bool patched = false;

	while(it != mPendingBranchTargets.end()) {
		if (it->mLabel == label) {
			VDWriteUnalignedLES32(&mByteCodeBuffer[it->mPatchOffset - 4], curOffset - (sint32)it->mPatchOffset);
			patched = true;

			*it = mPendingBranchTargets.back();
			mPendingBranchTargets.pop_back();
		} else {
			++it;
		}
	}

	return patched;
}

uint32 ATVMCompiler::Token() {
	if (mPushedToken) {
		uint32 tok = mPushedToken;
		mPushedToken = 0;

		return tok;
	}

	char c;
	for(;;) {
		if (mpSrc == mpSrcEnd)
			return kTokEnd;

		c = *mpSrc++;

		if (c == '/' && mpSrc != mpSrcEnd && *mpSrc == '/') {
			while(mpSrc != mpSrcEnd) {
				c = *mpSrc;

				if (c == '\r' || c == '\n')
					break;
				
				++mpSrc;
			}

			continue;
		}

		if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
			break;
	}

	if (mpSrc != mpSrcEnd) {
		char d = *mpSrc;

		if (c == '<') {
			if (d == '<') {
				++mpSrc;
				return kTokLeftShift;
			}

			if (d == '=') {
				++mpSrc;
				return kTokLe;
			}
		} else if (c == '>') {
			if (d == '>') {
				++mpSrc;
				return kTokRightShift;
			}

			if (d == '=') {
				++mpSrc;
				return kTokGe;
			}
		} else if (c == '!') {
			if (d == '=') {
				++mpSrc;
				return kTokNe;
			}
		} else if (c == '=') {
			if (d == '=') {
				++mpSrc;
				return kTokEq;
			}
		} else if (c == '|') {
			if (d == '|') {
				++mpSrc;
				return kTokLogicalOr;
			}
		} else if (c == '&') {
			if (d == '&') {
				++mpSrc;
				return kTokLogicalAnd;
			}
		} else if (c == '+') {
			if (d == '+') {
				++mpSrc;
				return kTokIncrement;
			}
		} else if (c == '-') {
			if (d == '-') {
				++mpSrc;
				return kTokDecrement;
			}
		}
	}

	if (c == '"' || c == '\'') {
		const char *strStart = mpSrc;
		char term = c;

		for(;;) {
			if (mpSrc == mpSrcEnd && (*mpSrc == '\r' ||*mpSrc == '\n')) {
				ReportError("Unterminated string literal");
				return kTokError;
			}

			c = *mpSrc++;
			if (c == term)
				break;
		}

		mTokIdent = VDStringSpanA(strStart, mpSrc - 1);
		return kTokStringLiteral;
	}

	if (c == '$') {
		uint32 value = 0;
		bool hexvalid = true;

		const char *idStart = mpSrc;

		while(mpSrc != mpSrcEnd) {
			c = *mpSrc;

			if (c >= '0' && c <= '9') {
				value = (value << 4) + (uint32)(c - '0');
			} else if (c >= 'A' && c <= 'F') {
				value = (value << 4) + (uint32)(c - 'A') + 10;
			} else if (c >= 'a' && c <= 'f') {
				value = (value << 4) + (uint32)(c - 'a') + 10;
			} else {
				if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && c != '_')
					break;

				hexvalid = false;
			}

			++mpSrc;
		}

		if (mpSrc == idStart) {
			ReportError("Expected hex constant or special variable name after '$'");
			return kTokError;
		}

		if (hexvalid) {
			mTokValue = (sint32)value;
			return kTokInteger;
		}

		mTokIdent = VDStringSpanA(idStart, mpSrc);
		return kTokSpecialIdent;
	}

	if (c >= '0' && c <= '9') {
		uint32 value = (uint32)(c - '0');

		while(mpSrc != mpSrcEnd) {
			c = *mpSrc;

			if (c < '0' || c > '9')
				break;

			value = (value * 10) + (uint32)(c - '0');

			++mpSrc;
		}

		mTokValue = (sint32)value;
		return kTokInteger;
	}

	if (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
		const char *idStart = mpSrc - 1;

		while(mpSrc != mpSrcEnd) {
			c = *mpSrc;

			if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
				break;

			++mpSrc;
		}

		const char *idEnd = mpSrc;

		VDStringSpanA ident(idStart, idEnd);

		if (ident == "if") {
			return kTokIf;
		} else if (ident == "return") {
			return kTokReturn;
		} else if (ident == "int") {
			return kTokInt;
		} else if (ident == "void") {
			return kTokVoid;
		} else if (ident == "function") {
			return kTokFunction;
		} else if (ident == "else") {
			return kTokElse;
		} else if (ident == "true") {
			return kTokTrue;
		} else if (ident == "false") {
			return kTokFalse;
		} else if (ident == "loop") {
			return kTokLoop;
		} else if (ident == "break") {
			return kTokBreak;
		} else if (ident == "do") {
			return kTokDo;
		} else if (ident == "while") {
			return kTokWhile;
		} else if (ident == "event") {
			return kTokEvent;
		} else if (ident == "option") {
			return kTokOption;
		} else {
			mTokIdent = ident;
			return kTokIdentifier;
		}
	}

	if (strchr("()<>[]=+-*/;{}.,&^|~%!:", c))
		return (uint32)c;

	if (c >= 0x20 && c < 0x7F)
		ReportErrorF("Unexpected character '%c'", c);
	else
		ReportErrorF("Unexpected character 0x%02X", (unsigned char)c);

	return kTokError;
}

bool ATVMCompiler::ReportError(const char *msg) {
	if (mError.empty()) {
		mError = msg;
		mErrorPos = (sint32)(mpSrc - mpSrc0);
	}

	return false;
}

bool ATVMCompiler::ReportError(uint32 srcOffset, const char *msg) {
	if (mError.empty()) {
		mError = msg;
		mErrorPos = srcOffset;
	}

	return false;
}

bool ATVMCompiler::ReportErrorF(const char *format, ...) {
	if (mError.empty()) {
		va_list val;
		va_start(val, format);
		mError.append_vsprintf(format, val);
		va_end(val);

		mErrorPos = (sint32)(mpSrc - mpSrc0);
	}

	return false;
}

size_t ATVMCompiler::TypeInfoListHashPred::operator()(vdvector_view<const ATVMTypeInfo> typeList) const {
	size_t hash = 0;

	for(const ATVMTypeInfo& info : typeList) {
		hash += (uint32)info.mClass;
		hash += info.mIndex << 16;
		hash += (size_t)(uintptr_t)info.mpObjectClass;

		hash = (hash << 7) | (hash >> (sizeof(size_t) * 8 - 7));
	}

	return hash;
}

bool ATVMCompiler::TypeInfoListHashPred::operator()(vdvector_view<const ATVMTypeInfo> x, vdvector_view<const ATVMTypeInfo> y) const {
	auto n = x.size();

	if (n != y.size())
		return false;

	for(decltype(n) i = 0; i < n; ++i) {
		if (x[i] != y[i])
			return false;
	}

	return true;
}
