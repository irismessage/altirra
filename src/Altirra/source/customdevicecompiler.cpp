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
#include "customdevicecompiler.h"
#include "customdevicevm.h"

ATCDCompileError::ATCDCompileError(const ATCDVMDataValue& ref, const char *err)
	: MyError(err)
	, mSrcOffset(ref.mSrcOffset)
{
}

ATCDCompileError ATCDCompileError::Format(const ATCDVMDataValue& ref, const char *format, ...) {
	ATCDCompileError e;
	e.mSrcOffset = ref.mSrcOffset;

	va_list val;

	va_start(val, format);
	e.vsetf(format, val);
	va_end(val);

	return e;
}

///////////////////////////////////////////////////////////////////////////

ATCustomDeviceCompiler::ATCustomDeviceCompiler(ATCDVMDomain& domain)
	: mpDomain(&domain)
{
	// function pointer type 0 is always void()
	(void)GetFunctionPointerTypeIndex(vdvector_view(&kATCDVMTypeVoid, 1));
}

void ATCustomDeviceCompiler::SetBindEventHandler(vdfunction<bool(ATCustomDeviceCompiler&, const char *, const ATCDVMScriptFragment&)> bindEventFn) {
	mpBindEvent = std::move(bindEventFn);
}

void ATCustomDeviceCompiler::SetOptionHandler(vdfunction<bool(ATCustomDeviceCompiler&, const char *, const ATCDVMDataValue&)> setOptionFn) {
	mpSetOption = std::move(setOptionFn);
}

bool ATCustomDeviceCompiler::IsValidVariableName(const char *name) {
	char c = *name;

	if (c != '_' && (c < 'a' || c > 'z') && (c < 'A' || c > 'Z'))
		return false;

	while((c = *++name)) {
		if (c != '_' && (c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && (c < '0' || c > '9'))
			return false;
	}

	return true;
}

bool ATCustomDeviceCompiler::IsSpecialVariableReferenced(const char *name) const {
	auto it = mSpecialVariableLookup.find_as(name);

	if (it == mSpecialVariableLookup.end())
		return false;
	
	uint32 idx = it->second.mIndex;

	return idx < mSpecialVariablesReferenced.size() && mSpecialVariablesReferenced[idx];
}

void ATCustomDeviceCompiler::DefineClass(const ATCDVMObjectClass& objClass, DefineInstanceFn *defineFn) {
	auto r = mClassLookup.insert_as(objClass.mpClassName);

	if (r.second) {
		r.first->second.mTypeInfo = ATCDVMTypeInfo { ATCDVMTypeClass::ObjectClass, 0, &objClass };
	} else
		VDASSERT(r.first->second.mTypeInfo.mpObjectClass == &objClass);

	if (defineFn)
		r.first->second.mpDefineFn = std::move(*defineFn);
}

bool ATCustomDeviceCompiler::DefineObjectVariable(const char *name, ATCDVMObject *obj, const ATCDVMObjectClass& objClass) {
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

bool ATCustomDeviceCompiler::DefineSpecialObjectVariable(const char *name, ATCDVMObject *obj, const ATCDVMObjectClass& objClass) {
	auto r = mSpecialVariableLookup.insert_as(name);
	VDASSERT(r.second);

	mpDomain->mSpecialVariables.push_back((sint32)mpDomain->mGlobalObjects.size());
	mpDomain->mGlobalObjects.push_back(obj);
	r.first->second = TypeInfo { TypeClass::ObjectLValue, (uint8)(mpDomain->mSpecialVariables.size() - 1), &objClass };

	DefineClass(objClass);
	return true;
}

bool ATCustomDeviceCompiler::DefineIntegerVariable(const char *name) {
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

void ATCustomDeviceCompiler::DefineSpecialVariable(const char *name) {
	auto r = mSpecialVariableLookup.insert_as(name);
	VDASSERT(r.second);

	mpDomain->mSpecialVariables.push_back(0);
	r.first->second = TypeInfo { TypeClass::IntLValueVariable, (uint8)(mpDomain->mSpecialVariables.size() - 1) };
}

void ATCustomDeviceCompiler::DefineThreadVariable(const char *name) {
	auto r = mThreadVariableLookup.insert_as(name);
	VDASSERT(r.second);

	++mpDomain->mNumThreadVariables;
	mpDomain->mSpecialVariables.push_back(0);
	r.first->second = TypeInfo { TypeClass::IntLValueVariable, (uint8)(mThreadVariableLookup.size() - 1) };
}

const ATCDVMTypeInfo *ATCustomDeviceCompiler::GetVariable(const char *name) const {
	auto it = mVariableLookup.find_as(name);

	if (it == mVariableLookup.end())
		return nullptr;

	return &it->second;
}

const ATCDVMFunction *ATCustomDeviceCompiler::DeferCompile(const ATCDVMTypeInfo& returnType, const ATCDVMScriptFragment& scriptFragment, ATCDVMFunctionFlags asyncAllowedMask, ATCDConditionalMask conditionalMask) {
	ATCDVMFunction *func = mpDomain->mAllocator.Allocate<ATCDVMFunction>();

	mDeferredCompiles.emplace_back(
		[returnType, &scriptFragment, asyncAllowedMask, conditionalMask, func, this] {
			mpReturnType = &returnType;

			const auto prevSrc = mpSrc;
			const auto prevSrcEnd = mpSrcEnd;

			InitSource(scriptFragment.mpSrc, scriptFragment.mSrcLength);

			FunctionInfo fi {};
			mpCurrentFunctionInfo = &fi;
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

bool ATCustomDeviceCompiler::CompileDeferred() {
	for(const auto& step : mDeferredCompiles) {
		if (!step())
			return false;
	}

	return true;
}

bool ATCustomDeviceCompiler::CompileFile(const char *src, size_t len) {
	mpSrc0 = src;
	InitSource(src, len);

	return ParseFile();
}

const char *ATCustomDeviceCompiler::GetError() const {
	return mError.empty() ? nullptr : mError.c_str();
}

std::pair<uint32, uint32> ATCustomDeviceCompiler::GetErrorLinePos() const {
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

void ATCustomDeviceCompiler::InitSource(const void *src, size_t len) {
	mpSrc = (const char *)src;
	mpSrcStart = mpSrc;
	mpSrcEnd = mpSrc + len;

	mMethodPtrs.clear();
	mByteCodeBuffer.clear();
	mPushedToken = 0;

	mError.clear();
}

bool ATCustomDeviceCompiler::ParseFile() {
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

bool ATCustomDeviceCompiler::ParseVariableDefinition(uint32 typeTok) {
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
			ATCDVMDataValue initializer {};

			tok = Token();
			if (tok == ':') {
				if (!ParseDataValue(initializer))
					return false;
			} else
				Push(tok);

			try {
				if (!classInfo->mpDefineFn(*this, varName.c_str(), initializer.mType != ATCDVMDataType::Invalid ? &initializer : nullptr))
					return false;
			} catch(const ATCDCompileError& e) {
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

bool ATCustomDeviceCompiler::ParseEventBinding() {
	if (Token() != kTokStringLiteral)
		return ReportError("Event name expected");

	VDStringA eventName(mTokIdent);

	if (Token() != ':')
		return ReportError("Expected ':' after event name");

	ATCDVMDataValue value;
	if (!ParseDataValue(value))
		return false;

	if (!value.IsScript())
		return ReportError("Expected inline script");

	if (Token() != ';')
		return ReportError("Expected ';' at end of event binding");

	return mpBindEvent(*this, eventName.c_str(), *value.mpScript);
}

bool ATCustomDeviceCompiler::ParseOption() {
	if (Token() != kTokStringLiteral)
		return ReportError("Option name expected");

	VDStringA optionName(mTokIdent);

	if (Token() != ':')
		return ReportError("Expected ':' after option name");

	ATCDVMDataValue value;
	if (!ParseDataValue(value))
		return false;

	if (Token() != ';')
		return ReportError("Expected ';' at end of event binding");

	return mpSetOption(*this, optionName.c_str(), value);
}

bool ATCustomDeviceCompiler::ParseFunction() {
	uint32 tok = Token();
	ATCDVMTypeInfo returnType;

	if (tok == kTokVoid)
		returnType = ATCDVMTypeInfo { ATCDVMTypeClass::Void };
	else if (tok == kTokInt)
		returnType = ATCDVMTypeInfo { ATCDVMTypeClass::Int };
	else
		return ReportError("Return type expected (int or void)");

	mpReturnType = &returnType;

	tok = Token();
	if (tok != kTokIdentifier)
		return ReportError("Function name expected");

	char *name = (char *)mpDomain->mAllocator.Allocate(mTokIdent.size() + 1);
	memcpy(name, mTokIdent.data(), mTokIdent.size());
	name[mTokIdent.size()] = 0;

	if (mpDomain->mFunctions.size() >= 256)
		return ReportError("Named function count limit exceeded (256 max)");

	if (mVariableLookup.find_as(name) != mVariableLookup.end())
		return ReportError("Variable with same name has already been declared");

	auto r = mFunctionLookup.insert_as(name);
	if (!r.second)
		return ReportError("Function with same name has already been declared");

	FunctionInfo& fi = r.first->second;
	fi.mFunctionIndex = mpDomain->mFunctions.size();
	fi.mFlags = {};

	tok = Token();
	if (tok != '(')
		return ReportError("Expected '('");

	tok = Token();
	if (tok != ')')
		return ReportError("Expected ')'");

	tok = Token();
	if (tok != '{')
		return ReportError("Expected '{'");

	ATCDVMFunction *func = mpDomain->mAllocator.Allocate<ATCDVMFunction>();

	mpCurrentFunctionInfo = &fi;
	bool success = ParseFunction(func, returnType, (ATCDVMFunctionFlags)~UINT32_C(0), ATCDConditionalMask::None);
	mpCurrentFunctionInfo = nullptr;
	if (!success)
		return false;

	func->mpName = name;

	tok = Token();
	if (tok != '}')
		return ReportError("Expected '}' at end of function");

	mpDomain->mFunctions.push_back(func);

	return true;
}

bool ATCustomDeviceCompiler::ParseFunction(ATCDVMFunction *func, const ATCDVMTypeInfo& returnType, ATCDVMFunctionFlags asyncAllowedMask, ATCDConditionalMask conditionalMask) {
	bool hasReturn = false;

	mMethodPtrs.clear();
	mByteCodeBuffer.clear();
	mLocalLookup.clear();
	
	mpCurrentFunction = func;
	mAsyncAllowedMask = asyncAllowedMask;
	mConditionalMask = conditionalMask;
	func->mLocalSlotsRequired = 0;

	if (!ParseBlock(hasReturn))
		return false;

	if (!hasReturn) {
		if (returnType.mClass == ATCDVMTypeClass::Void)
			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::ReturnVoid);
		else
			return ReportError("No return at end of function");
	}

	uint32 bclen = (uint32)mByteCodeBuffer.size();

	uint8 *byteCode = (uint8 *)mpDomain->mAllocator.Allocate(bclen);
	memcpy(byteCode, mByteCodeBuffer.data(), bclen);

	void (**methodTable)() = nullptr;
	
	if (!mMethodPtrs.empty()) {
		methodTable = mpDomain->mAllocator.AllocateArray<void (*)()>(mMethodPtrs.size());
		memcpy(methodTable, mMethodPtrs.data(), mMethodPtrs.size() * sizeof(methodTable[0]));
	}

	func->mpName = nullptr;
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
				return ReportError("Bytecode validation failed (invalid branch target)");

			if (ipToStackId[ip] < 0)
				ipToStackId[ip] = stackId;
			else {
				if (ipToStackId[ip] != stackId)
					return ReportError("Bytecode validation failed (stack mismatch)");

				break;
			}

			ATCDVMOpcode opcode = (ATCDVMOpcode)byteCode[ip];
			uint32 popCount = 0;
			TypeClass pushType = TypeClass::Void;

			switch(opcode) {
				case ATCDVMOpcode::Nop:
				case ATCDVMOpcode::ReturnVoid:
				case ATCDVMOpcode::IntNeg:
				case ATCDVMOpcode::IntNot:
				case ATCDVMOpcode::Jmp:
				case ATCDVMOpcode::Ljmp:
				case ATCDVMOpcode::LoopChk:
				case ATCDVMOpcode::Not:
					// no stack change
					break;

				case ATCDVMOpcode::Dup:
					pushType = stackStateList[stackId].mTopType;
					break;

				case ATCDVMOpcode::IVLoad:
				case ATCDVMOpcode::ILLoad:
				case ATCDVMOpcode::ISLoad:
				case ATCDVMOpcode::ITLoad:
				case ATCDVMOpcode::IntConst:
				case ATCDVMOpcode::IntConst8:
					pushType = TypeClass::Int;
					break;

				case ATCDVMOpcode::MethodCallVoid:
					popCount = byteCode[ip+1] + 1;
					break;

				case ATCDVMOpcode::MethodCallInt:
					popCount = byteCode[ip+1];
					break;

				case ATCDVMOpcode::StaticMethodCallVoid:
				case ATCDVMOpcode::FunctionCallVoid:
					popCount = byteCode[ip+1];
					break;

				case ATCDVMOpcode::StaticMethodCallInt:
				case ATCDVMOpcode::FunctionCallInt:
					popCount = byteCode[ip+1];
					pushType = TypeClass::Int;
					break;

				case ATCDVMOpcode::IntAdd:
				case ATCDVMOpcode::IntSub:
				case ATCDVMOpcode::IntMul:
				case ATCDVMOpcode::IntDiv:
				case ATCDVMOpcode::IntMod:
				case ATCDVMOpcode::IntAnd:
				case ATCDVMOpcode::IntOr:
				case ATCDVMOpcode::IntXor:
				case ATCDVMOpcode::IntAsr:
				case ATCDVMOpcode::IntAsl:
				case ATCDVMOpcode::IVStore:
				case ATCDVMOpcode::ILStore:
				case ATCDVMOpcode::And:
				case ATCDVMOpcode::Or:
				case ATCDVMOpcode::IntLt:
				case ATCDVMOpcode::IntLe:
				case ATCDVMOpcode::IntGt:
				case ATCDVMOpcode::IntGe:
				case ATCDVMOpcode::IntEq:
				case ATCDVMOpcode::IntNe:
				case ATCDVMOpcode::Jz:
				case ATCDVMOpcode::Jnz:
				case ATCDVMOpcode::Ljz:
				case ATCDVMOpcode::Ljnz:
				case ATCDVMOpcode::Pop:
				case ATCDVMOpcode::ReturnInt:
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

			if (opcode == ATCDVMOpcode::Jnz || opcode == ATCDVMOpcode::Jz) {
				sint32 branchTarget = ip + (sint8)byteCode[ip + 1] + 2;

				traversalStack.push_back({branchTarget, stackId});
			} else if (opcode == ATCDVMOpcode::Ljnz || opcode == ATCDVMOpcode::Ljz) {
				sint32 branchTarget = ip + VDReadUnalignedLES32(&byteCode[ip + 1]) + 5;

				traversalStack.push_back({branchTarget, stackId});
			} else if (opcode == ATCDVMOpcode::Jmp) {
				ip += (sint8)byteCode[ip + 1] + 2;
				continue;
			} else if (opcode == ATCDVMOpcode::Ljmp) {
				ip += VDReadUnalignedLES32(&byteCode[ip + 1]) + 5;
				continue;
			} else if (opcode == ATCDVMOpcode::ReturnInt || opcode == ATCDVMOpcode::ReturnVoid) {
				break;
			} else if (opcode == ATCDVMOpcode::ILStore || opcode == ATCDVMOpcode::ILLoad) {
				if (byteCode[ip + 1] >= func->mLocalSlotsRequired)
					return ReportError("Bytecode validation failed (invalid local index)");
			}

			ip += ATCDVMGetOpcodeLength(opcode);
		}
	}

	func->mStackSlotsRequired += func->mLocalSlotsRequired;

	mpCurrentFunction = nullptr;

	return true;
}

bool ATCustomDeviceCompiler::ParseBlock(bool& hasReturn) {
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

bool ATCustomDeviceCompiler::ParseStatement(uint32 tok, bool& hasReturn) {
	if (tok == '[') {
		tok = Token();

		bool inv = false;
		if (tok == '!') {
			tok = Token();
			inv = true;
		}

		if (tok != kTokIdentifier)
			return ReportError("Expected attribute name");

		if (mTokIdent != "debug")
			return ReportErrorF("Unrecognized attribute '%.*s'", (int)mTokIdent.size(), mTokIdent.size());

		if (Token() != ']')
			return ReportError("Expected ']' after attribute name");

		if (!((uint32)mConditionalMask & (uint32)ATCDConditionalMask::Allowed))
			return ReportError("Conditional attributes not supported in this function");

		if ((uint32)mConditionalMask & (inv ? (uint32)ATCDConditionalMask::DebugEnabled : (uint32)ATCDConditionalMask::NonDebugEnabled)) {
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
		if (!ParseLoopStatement())
			return false;

		if (mpReturnType->mClass == ATCDVMTypeClass::Void)
			hasReturn = true;

		return true;
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

bool ATCustomDeviceCompiler::ParseIfStatement(bool& hasReturn) {
	if (Token() != '(')
		return ReportError("Expected '(' before if condition");

	if (!ParseIntExpression())
		return false;

	if (Token() != ')')
		return ReportError("Expected ')' after if condition");

	mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::Ljz);
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
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::Ljmp);
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

bool ATCustomDeviceCompiler::ParseReturnStatement() {
	uint32 tok = Token();
	if (tok == ';') {
		if (mpReturnType->mClass != ATCDVMTypeClass::Void)
			return ReportError("Return value required");

		Push(tok);
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::ReturnVoid);
		return true;
	}

	Push(tok);

	TypeInfo returnType;
	if (!ParseExpression(returnType))
		return false;

	ConvertToRvalue(returnType);

	if (returnType.mClass != mpReturnType->mClass || (returnType.mClass == ATCDVMTypeClass::Object && returnType.mpObjectClass != mpReturnType->mpObjectClass))
		return ReportError("Return type mismatch");

	if (returnType.mClass == TypeClass::Void)
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::ReturnVoid);
	else if (returnType.mClass == TypeClass::Int)
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::ReturnInt);
	else
		return ReportError("Cannot return expression type");

	return true;
}

bool ATCustomDeviceCompiler::ParseLoopStatement() {
	const sint32 branchAnchor = (sint32)mByteCodeBuffer.size();

	bool hasReturn = false;
	if (!ParseStatement(Token(), hasReturn))
		return false;

	mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::LoopChk);
	mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::Ljmp);
	mByteCodeBuffer.push_back(0);
	mByteCodeBuffer.push_back(0);
	mByteCodeBuffer.push_back(0);
	mByteCodeBuffer.push_back(0);

	sint32 branchDistance = branchAnchor - (sint32)mByteCodeBuffer.size();
	VDWriteUnalignedLEU32(&*(mByteCodeBuffer.end() - 4), branchDistance);
	return true;
}

bool ATCustomDeviceCompiler::ParseDeclStatement(uint32 typeToken) {
	VDASSERT(typeToken == kTokInt);

	for(;;) {
		uint32 tok = Token();

		if (tok != kTokIdentifier)
			return ReportError("Expected variable name");

		auto ins = mLocalLookup.insert_as(mTokIdent);
		if (!ins.second)
			return ReportErrorF("Local variable '%.*s' already declared", (int)mTokIdent.size(), mTokIdent.data());

		uint32 varIndex = mpCurrentFunction->mLocalSlotsRequired;
		ins.first->second = ATCDVMTypeInfo { ATCDVMTypeClass::IntLValueLocal, varIndex };
		++mpCurrentFunction->mLocalSlotsRequired;

		tok = Token();

		// check for initializer
		if (tok == '=') {
			if (!ParseIntExpression())
				return false;

			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::ILStore);
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

bool ATCustomDeviceCompiler::ParseExpressionStatement(uint32 tok) {
	Push(tok);

	TypeInfo returnType;
	if (!ParseExpression(returnType))
		return false;

	// discard rvalues, and don't bother doing lvalue conversion as we don't need it on the stack
	if (returnType.mClass == TypeClass::Int || returnType.mClass == TypeClass::Object)
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::Pop);

	return true;
}

// dataValue := dataObject | dataArray | expr
// dataValues = dataValue | dataValue ',' dataValues
// dataArray := '[' dataValues? ']'
// dataObject := '{' dataMembers? '}'
// dataMembers := dataMember | dataMember ',' dataMembers
// dataMember := identifier ':' dataValue

bool ATCustomDeviceCompiler::ParseDataValue(ATCDVMDataValue& value) {
	uint32 tok = Token();

	value.mSrcOffset = (uint32)(mpSrc - mpSrc0);

	if (tok == '[') {
		ATCDVMDataValue *array = nullptr;
		vdfastvector<ATCDVMDataValue> arrayElements;

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

		array = mpDomain->mAllocator.AllocateArray<ATCDVMDataValue>(arrayElements.size());

		std::copy(arrayElements.begin(), arrayElements.end(), array);

		value.mType = ATCDVMDataType::Array;
		value.mLength = (uint32)arrayElements.size();
		value.mpArrayElements = array;
	} else if (tok == '{') {
		ATCDVMDataMember *members = nullptr;

		struct ATCDVMDataMemberHash {
			size_t operator()(const ATCDVMDataMember& m) const {
				return m.mNameHash;
			}
		};

		struct ATCDVMDataMemberPred {
			bool operator()(const ATCDVMDataMember& a, const ATCDVMDataMember& b) const {
				return a.mNameHash == b.mNameHash && !strcmp(a.mpName, b.mpName);
			}
		};

		std::unordered_set<ATCDVMDataMember, ATCDVMDataMemberHash, ATCDVMDataMemberPred> memberElements;

		for(;;) {
			tok = Token();

			if (tok == '}')
				break;

			if (tok != kTokIdentifier)
				return ReportError("Expected data member name");

			tok = Token();
			if (tok != ':')
				return ReportError("Expected ':' after data member name");

			ATCDVMDataMember mb {};
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

		members = mpDomain->mAllocator.AllocateArray<ATCDVMDataMember>(memberElements.size());
		std::copy(memberElements.begin(), memberElements.end(), members);

		value.mType = ATCDVMDataType::DataObject;
		value.mLength = (uint32)memberElements.size();
		value.mpObjectMembers = members;
	} else if (tok == kTokFunction) {
		if (Token() != '{')
			return ReportError("Expected '}' after 'function'");

		ATCDVMScriptFragment *fragment = mpDomain->mAllocator.Allocate<ATCDVMScriptFragment>();

		fragment->mpSrc = mpSrc;

		uint32 nestingLevel = 1;
		const char *p;

		for(;;) {
			p = mpSrc;
			tok = Token();

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

		value.mType = ATCDVMDataType::Script;
		value.mLength = 0;
		value.mpScript = fragment;
	} else {
		Push(tok);

		ATCDVMTypeInfo valueType;
		if (!ParseConstantExpression(valueType))
			return false;

		if (valueType.mClass == TypeClass::IntConst) {
			value.mType = ATCDVMDataType::Int;
			value.mLength = 0;
			value.mIntValue = (sint32)valueType.mIndex;
		} else if (valueType.mClass == TypeClass::StringConst) {
			value.mType = ATCDVMDataType::String;
			value.mLength = 0;
			value.mpStrValue = static_cast<const ATCDVMStringObject *>(mpDomain->mGlobalObjects[valueType.mIndex])->c_str();
		} else if (valueType.mClass == TypeClass::ObjectLValue) {
			value.mType = ATCDVMDataType::RuntimeObject;
			value.mLength = mpDomain->mGlobalVariables[valueType.mIndex];
			value.mpRuntimeObjectClass = valueType.mpObjectClass;
		} else
			return ReportError("Cannot use this type in a data object");
	}

	return true;
}

bool ATCustomDeviceCompiler::ParseConstantExpression(TypeInfo& returnType) {
	return ParseConstantValue(returnType);
}

bool ATCustomDeviceCompiler::ParseIntExpression() {
	TypeInfo returnType;
	if (!ParseExpression(returnType))
		return false;

	ConvertToRvalue(returnType);

	if (returnType.mClass != TypeClass::Int)
		return ReportError("Integer expression required");

	return true;
}

bool ATCustomDeviceCompiler::ParseExpression(TypeInfo& returnType) {
	return ParseAssignmentExpression(returnType);
}

bool ATCustomDeviceCompiler::ParseAssignmentExpression(TypeInfo& returnType) {
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
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IVStore);
	else
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::ILStore);

	mByteCodeBuffer.push_back((uint8)returnType.mIndex);

	returnType = TypeInfo { TypeClass::Void };
	return true;
}

bool ATCustomDeviceCompiler::ParseLogicalOrExpression(TypeInfo& returnType) {
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

		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::Dup);
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::Ljnz);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);

		const auto branchAnchor = mByteCodeBuffer.size();
	
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::Pop);

		if (!ParseLogicalAndExpression(returnType))
			return false;

		ConvertToRvalue(returnType);

		const uint32 branchDistance = (uint32)(mByteCodeBuffer.size() - branchAnchor);

		VDWriteUnalignedLEU32(&mByteCodeBuffer[branchAnchor - 4], branchDistance);
	}
}

bool ATCustomDeviceCompiler::ParseLogicalAndExpression(TypeInfo& returnType) {
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

		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::Dup);
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::Ljz);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);

		const auto branchAnchor = mByteCodeBuffer.size();
	
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::Pop);

		if (!ParseEqualityExpression(returnType))
			return false;

		ConvertToRvalue(returnType);

		const uint32 branchDistance = (uint32)(mByteCodeBuffer.size() - branchAnchor);

		VDWriteUnalignedLEU32(&mByteCodeBuffer[branchAnchor - 4], branchDistance);
	}
}

bool ATCustomDeviceCompiler::ParseEqualityExpression(TypeInfo& returnType) {
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
			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntEq);
		else if (tok == kTokNe)
			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntNe);
	}
}

bool ATCustomDeviceCompiler::ParseRelationalExpression(TypeInfo& returnType) {
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
			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntLt);
		else if (tok == '>')
			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntGt);
		else if (tok == kTokLe)
			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntLe);
		else if (tok == kTokGe)
			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntGe);
	}
}

bool ATCustomDeviceCompiler::ParseBitwiseOrExpression(TypeInfo& returnType) {
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

		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntOr);
	}
}

bool ATCustomDeviceCompiler::ParseBitwiseXorExpression(TypeInfo& returnType) {
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

		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntXor);
	}
}

bool ATCustomDeviceCompiler::ParseBitwiseAndExpression(TypeInfo& returnType) {
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

		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntAnd);
	}
}

bool ATCustomDeviceCompiler::ParseShiftExpression(TypeInfo& returnType) {
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
			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntAsl);
		else if (tok == kTokRightShift)
			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntAsr);
	}
}

bool ATCustomDeviceCompiler::ParseAdditiveExpression(TypeInfo& returnType) {
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
			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntAdd);
		else if (tok == '-')
			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntSub);
	}
}

bool ATCustomDeviceCompiler::ParseMultiplicativeExpression(TypeInfo& returnType) {
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
			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntMul);
		else if (tok == '/')
			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntDiv);
		else if (tok == '%')
			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntMod);
	}
}

bool ATCustomDeviceCompiler::ParseUnaryExpression(TypeInfo& returnType) {
	uint32 tok = Token();
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
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntNeg);
	else if (tok == '~')
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntNot);
	else if (tok == '!')
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::Not);

	return true;
}

bool ATCustomDeviceCompiler::ParsePostfixExpression(TypeInfo& returnType) {
	if (!ParseValue(returnType))
		return false;

	uint32 tok = Token();

	if (tok != '.') {
		Push(tok);

		if (returnType.mClass == ATCDVMTypeClass::IntConst)
			ConvertToRvalue(returnType);

		return true;
	}

	ConvertToRvalue(returnType);

	if (returnType.mClass != TypeClass::Object && returnType.mClass != TypeClass::ObjectClass)
		return ReportError("'.' operator can only be used on object");

	tok = Token();
	if (tok != kTokIdentifier)
		return ReportError("Expected method name after '.' operator");

	const ATCDVMExternalMethod *foundMethod = nullptr;
	for(const ATCDVMExternalMethod& method : returnType.mpObjectClass->mMethods) {
		if (mTokIdent == method.mpName) {
			foundMethod = &method;
			break;
		}
	}

	if (!foundMethod)
		return ReportErrorF("Class '%s' does not have method called '%.*s'", returnType.mpObjectClass->mpClassName, (int)mTokIdent.size(), mTokIdent.data());

	const bool isStaticCall = (returnType.mClass == TypeClass::ObjectClass);
	if ((foundMethod->mFlags & ATCDVMFunctionFlags::Static) != ATCDVMFunctionFlags::None)
	{
		if (!isStaticCall)
			return ReportErrorF("Static method '%.*s' must be called on a class instance", (int)mTokIdent.size(), mTokIdent.data());
	}
	else
	{
		if (isStaticCall)
			return ReportErrorF("Instance method '%.*s' must be called on an object instance", (int)mTokIdent.size(), mTokIdent.data());
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

	if (foundMethod->mpTypes[0]->mClass == ATCDVMTypeClass::Int) {
		mByteCodeBuffer.push_back(isStaticCall ? (uint8)ATCDVMOpcode::StaticMethodCallInt : (uint8)ATCDVMOpcode::MethodCallInt);
		mMethodPtrs.push_back((void(*)())foundMethod->mpIntMethod);
	} else {
		mByteCodeBuffer.push_back(isStaticCall ? (uint8)ATCDVMOpcode::StaticMethodCallVoid : (uint8)ATCDVMOpcode::MethodCallVoid);
		mMethodPtrs.push_back((void(*)())foundMethod->mpVoidMethod);
	}

	mByteCodeBuffer.push_back((uint8)argCount);
	mByteCodeBuffer.push_back((uint8)(mMethodPtrs.size() - 1));

	returnType = *foundMethod->mpTypes[0];
	return true;
}

bool ATCustomDeviceCompiler::ParseValue(TypeInfo& returnType) {
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
			const FunctionInfo& fi = it->second;
			const ATCDVMFunction& func = *mpDomain->mFunctions[fi.mFunctionIndex];

			tok = Token();
			if (tok != '(') {
				// no function call, we are returning a function pointer
				Push(tok);

				LoadConst(fi.mFunctionIndex);

				returnType = ATCDVMTypeInfo { ATCDVMTypeClass::FunctionPointer, GetFunctionPointerTypeIndex(vdvector_view(&func.mReturnType, 1)) };
				return true;
			}

			// we have a function call -- check for arguments (currently we don't support any)
			tok = Token();
			if (tok != ')')
				return ReportError("Expected ')' after function name");

			ATCDVMFunctionFlags funcAsyncMask = fi.mFlags & ATCDVMFunctionFlags::AsyncAll;

			if (funcAsyncMask != ATCDVMFunctionFlags::None) {
				if ((~mAsyncAllowedMask & funcAsyncMask) != ATCDVMFunctionFlags::None) {
					if (mAsyncAllowedMask != ATCDVMFunctionFlags::None)
						return ReportErrorF("Cannot call '%.*s' as it can suspend in a mode not supported by the current context", (int)mTokIdent.size(), mTokIdent.data());
					else
						return ReportErrorF("Cannot call '%.*s' as it can suspend, which is not supported by the current context", (int)mTokIdent.size(), mTokIdent.data());
				}

				mpCurrentFunctionInfo->mFlags |= fi.mFlags;
			}

			if (func.mReturnType.mClass == ATCDVMTypeClass::Int)
				mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::FunctionCallInt);
			else
				mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::FunctionCallVoid);

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

			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::ITLoad);
		} else {
			if (mSpecialVariablesReferenced.size() <= it->second.mIndex)
				mSpecialVariablesReferenced.resize(it->second.mIndex + 1, false);

			mSpecialVariablesReferenced[it->second.mIndex] = true;

			mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::ISLoad);
		}

		mByteCodeBuffer.push_back((uint8)it->second.mIndex);

		if (it->second.mClass == ATCDVMTypeClass::ObjectLValue)
			returnType = TypeInfo { TypeClass::Object, 0, it->second.mpObjectClass };
		else
			returnType = TypeInfo { TypeClass::Int };
	} else if (tok == kTokInteger || tok == kTokStringLiteral || tok == kTokTrue || tok == kTokFalse) {
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

bool ATCustomDeviceCompiler::ParseConstantValue(TypeInfo& returnType) {
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
		ATCDVMStringObject *sobj = (ATCDVMStringObject *)mpDomain->mAllocator.Allocate(sizeof(ATCDVMStringObject) + len + 1);
		char *s = (char *)(sobj + 1);

		memcpy(s, mTokIdent.data(), len);
		s[len] = 0;

		mpDomain->mGlobalObjects.push_back(sobj);

		returnType = TypeInfo { TypeClass::StringConst, id };
	} else if (auto it = mVariableLookup.find_as(mTokIdent); it != mVariableLookup.end()) {
		// variable reference
		returnType = it->second;

		if (returnType.mClass != ATCDVMTypeClass::ObjectLValue)
			return ReportError("Expected constant value");
	} else
		return ReportError("Expected constant value");

	return true;
}

void ATCustomDeviceCompiler::ConvertToRvalue(TypeInfo& returnType) {
	if (returnType.mClass == TypeClass::IntConst) {
		LoadConst((sint32)returnType.mIndex);

		returnType.mClass = TypeClass::Int;
		returnType.mIndex = 0;
	} else if (returnType.mClass == TypeClass::IntLValueVariable) {
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IVLoad);
		mByteCodeBuffer.push_back((uint8)returnType.mIndex);

		returnType.mClass = TypeClass::Int;
		returnType.mIndex = 0;
	} else if (returnType.mClass == TypeClass::IntLValueLocal) {
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::ILLoad);
		mByteCodeBuffer.push_back((uint8)returnType.mIndex);

		returnType.mClass = TypeClass::Int;
		returnType.mIndex = 0;
	} else if (returnType.mClass == TypeClass::ObjectLValue) {
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IVLoad);
		mByteCodeBuffer.push_back((uint8)returnType.mIndex);

		returnType.mClass = TypeClass::Object;
		returnType.mIndex = 0;
	} else if (returnType.mClass == TypeClass::StringConst) {
		LoadConst((sint32)returnType.mIndex);

		returnType.mClass = TypeClass::String;
		returnType.mIndex = 0;
	}
}

uint32 ATCustomDeviceCompiler::GetFunctionPointerTypeIndex(vdvector_view<const ATCDVMTypeInfo> types) {
	auto it = mFunctionPointerTypeLookup.find(types);

	if (it != mFunctionPointerTypeLookup.end())
		return it->second;

	const uint32 index = (uint32)mFunctionPointerTypeLookup.size();

	ATCDVMTypeInfo *internedTypes = mpDomain->mAllocator.AllocateArray<ATCDVMTypeInfo>(types.size());
	std::copy(types.begin(), types.end(), internedTypes);

	mFunctionPointerTypeLookup.insert(vdvector_view(internedTypes, types.size())).first->second = index;
	return index;
}

void ATCustomDeviceCompiler::LoadConst(sint32 v) {
	if (v >= -128 && v <= 127) {
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntConst8);
		mByteCodeBuffer.push_back((uint8)v);
	} else {
		mByteCodeBuffer.push_back((uint8)ATCDVMOpcode::IntConst);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);
		mByteCodeBuffer.push_back(0);

		VDWriteUnalignedLES32(&*(mByteCodeBuffer.end() - 4), v);
	}
}

uint32 ATCustomDeviceCompiler::Token() {
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
		uint32 digits = 0;
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

bool ATCustomDeviceCompiler::ReportError(const char *msg) {
	if (mError.empty()) {
		mError = msg;
		mErrorPos = (sint32)(mpSrc - mpSrc0);
	}

	return false;
}

bool ATCustomDeviceCompiler::ReportError(uint32 srcOffset, const char *msg) {
	if (mError.empty()) {
		mError = msg;
		mErrorPos = srcOffset;
	}

	return false;
}

bool ATCustomDeviceCompiler::ReportErrorF(const char *format, ...) {
	if (mError.empty()) {
		va_list val;
		va_start(val, format);
		mError.append_vsprintf(format, val);
		va_end(val);

		mErrorPos = (sint32)(mpSrc - mpSrc0);
	}

	return false;
}

size_t ATCustomDeviceCompiler::TypeInfoListHashPred::operator()(vdvector_view<const ATCDVMTypeInfo> typeList) const {
	size_t hash = 0;

	for(const ATCDVMTypeInfo& info : typeList) {
		hash += (uint32)info.mClass;
		hash += info.mIndex << 16;
		hash += (size_t)(uintptr_t)info.mpObjectClass;

		hash = (hash << 7) | (hash >> (sizeof(size_t) * 8 - 7));
	}

	return hash;
}

bool ATCustomDeviceCompiler::TypeInfoListHashPred::operator()(vdvector_view<const ATCDVMTypeInfo> x, vdvector_view<const ATCDVMTypeInfo> y) const {
	auto n = x.size();

	if (n != y.size())
		return false;

	for(decltype(n) i = 0; i < n; ++i) {
		if (x[i] != y[i])
			return false;
	}

	return true;
}
