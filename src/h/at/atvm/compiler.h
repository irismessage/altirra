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

#ifndef f_AT_ATVM_COMPILER_H
#define f_AT_ATVM_COMPILER_H

#include <unordered_set>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/error.h>
#include <vd2/system/vdstl_vectorview.h>
#include <at/atvm/vm.h>

class ATVMDomain;
struct ATVMFunction;
struct ATVMDataValue;

class ATVMCompileError final : public MyError {
public:
	using MyError::MyError;

	ATVMCompileError(const ATVMDataValue& ref, const char *err);

	static ATVMCompileError Format(const ATVMDataValue& ref, const char *format, ...);

	uint32 mSrcOffset = ~UINT32_C(0);
};


enum class ATVMConditionalMask : uint32 {
	None = 0,
	Allowed = 1,
	DebugReadEnabled = 2,
	NonDebugReadEnabled = 4,
	DebugEnabled = 8,
	NonDebugEnabled = 16,

	DebugReadOnly = Allowed | DebugReadEnabled,
	NonDebugReadOnly = Allowed | NonDebugReadEnabled
};

inline ATVMConditionalMask& operator|=(ATVMConditionalMask& x, ATVMConditionalMask y) { x = (ATVMConditionalMask)((uint32)x | (uint32)y); return x; }

class ATVMCompiler {
public:
	typedef vdfunction<bool(ATVMCompiler&, const char *, const ATVMDataValue *)> DefineInstanceFn;

	ATVMCompiler(ATVMDomain& domain);

	void SetBindEventHandler(vdfunction<bool(ATVMCompiler&, const char *, const ATVMScriptFragment&)> bindEventFn);
	void SetOptionHandler(vdfunction<bool(ATVMCompiler&, const char *, const ATVMDataValue&)> setOptionFn);

	static bool IsValidVariableName(const char *name);
	bool IsSpecialVariableReferenced(const char *name) const;

	void DefineClass(const ATVMObjectClass& objClass, DefineInstanceFn *defineFn = nullptr);
	bool DefineObjectVariable(const char *name, ATVMObject *obj, const ATVMObjectClass& objClass);
	bool DefineSpecialObjectVariable(const char *name, ATVMObject *obj, const ATVMObjectClass& objClass);

	template<typename T>
	bool DefineObjectVariable(const char *name, T *obj) {
		static_assert(std::is_convertible_v<T, ATVMObject>);
		return DefineObjectVariable(name, obj, T::kVMObjectClass);
	}

	template<typename T>
	bool DefineSpecialObjectVariable(const char *name, T *obj) {
		static_assert(std::is_convertible_v<T, ATVMObject>);
		return DefineSpecialObjectVariable(name, obj, T::kVMObjectClass);
	}

	bool DefineIntegerVariable(const char *name);
	void DefineSpecialVariable(const char *name);
	void DefineThreadVariable(const char *name);

	const ATVMTypeInfo *GetVariable(const char *name) const;

	const ATVMFunction *DeferCompile(const ATVMTypeInfo& returnType, const ATVMScriptFragment& scriptFragment, ATVMFunctionFlags asyncAllowedMask = ATVMFunctionFlags::None, ATVMConditionalMask conditionalMask = ATVMConditionalMask::None);
	bool CompileDeferred();

	bool CompileFile(const char *src, size_t len);

	const char *GetError() const;
	std::pair<uint32, uint32> GetErrorLinePos() const;

	bool ReportError(const char *msg);
	bool ReportError(uint32 srcOffset, const char *msg);
	bool ReportErrorF(const char *format, ...);

private:
	enum Token : uint8 {
		kTokEnd = 0,
		kTokError = 128,
		kTokInteger,
		kTokIdentifier,
		kTokSpecialIdent,
		kTokLe,
		kTokGe,
		kTokNe,
		kTokEq,
		kTokLeftShift,
		kTokRightShift,
		kTokLogicalAnd,
		kTokLogicalOr,
		kTokIncrement,
		kTokDecrement,
		kTokIf,
		kTokReturn,
		kTokVoid,
		kTokInt,
		kTokFunction,
		kTokElse,
		kTokTrue,
		kTokFalse,
		kTokStringLiteral,
		kTokLoop,
		kTokBreak,
		kTokDo,
		kTokWhile,
		kTokEvent,
		kTokOption
	};

	typedef ATVMTypeClass TypeClass;
	typedef ATVMTypeInfo TypeInfo;

	struct ClassInfo {
		TypeInfo mTypeInfo;

		DefineInstanceFn mpDefineFn;
	};

	void InitSource(const void *src0, const void *src, size_t len);

	bool ParseFile();
	bool ParseVariableDefinition(uint32 tok);
	bool ParseEventBinding();
	bool ParseOption();
	bool ParseFunction();
	bool ParseFunction(ATVMFunction *func, const ATVMTypeInfo& returnType, ATVMFunctionFlags asyncAllowedMask, ATVMConditionalMask conditionalMask);
	bool ParseBlock(bool& hasReturn);
	bool ParseStatement(uint32 tok, bool& hasReturn);
	bool ParseIfStatement(bool& hasReturn);
	bool ParseReturnStatement();
	bool ParseLoopStatement(bool& hasReturn);
	bool ParseDoWhileStatement();
	bool ParseWhileStatement();
	bool ParseBreakStatement();
	bool ParseDeclStatement(uint32 typeToken);
	bool ParseExpressionStatement(uint32 tok);
	bool ParseDataValue(ATVMDataValue& value);
	bool ParseConstantExpression(TypeInfo& returnType);
	bool ParseIntExpression();
	bool ParseExpression(TypeInfo& returnType);
	bool ParseLogicalOrExpression(TypeInfo& returnType);
	bool ParseLogicalAndExpression(TypeInfo& returnType);
	bool ParseEqualityExpression(TypeInfo& returnType);
	bool ParseRelationalExpression(TypeInfo& returnType);
	bool ParseBitwiseOrExpression(TypeInfo& returnType);
	bool ParseBitwiseXorExpression(TypeInfo& returnType);
	bool ParseBitwiseAndExpression(TypeInfo& returnType);
	bool ParseShiftExpression(TypeInfo& returnType);
	bool ParseAdditiveExpression(TypeInfo& returnType);
	bool ParseMultiplicativeExpression(TypeInfo& returnType);
	bool ParseUnaryExpression(TypeInfo& returnType);
	bool ParsePostfixExpression(TypeInfo& returnType);
	bool ParseAssignmentExpression(TypeInfo& returnType);
	bool ParseValue(TypeInfo& returnType);
	bool ParseConstantValue(TypeInfo& returnType);
	bool ParseInlineScript(ATVMDataValue& value);
	void ConvertToRvalue(TypeInfo& returnType);
	uint32 GetFunctionPointerTypeIndex(vdvector_view<const ATVMTypeInfo> types);

	void LoadConst(sint32 v);
	void JumpToOffset(uint32 offset);
	void JumpToOffset(ATVMOpcode opcode, uint32 offset);

	uint32 BeginLoop();
	bool EndLoop();
	bool EmitBreakTarget();

	uint32 AllocLabel();
	void EmitPendingBranchTarget(uint32 label);
	bool PatchLabel(uint32 label);

	void Push(uint32 tok) { mPushedToken = tok; }
	uint32 Token();

	template<typename T, typename... T_Args>
	T *AllocTemp(T_Args... args) {
		static_assert(std::is_trivially_destructible_v<T>);

		return mTempAllocator.Allocate<T>(std::forward<T_Args>(args)...);
	}

	const char *mpSrc0 = nullptr;
	const char *mpSrc = nullptr;
	const char *mpSrcStart = nullptr;
	const char *mpSrcEnd = nullptr;

	uint32 mPushedToken = 0;
	sint32 mTokValue = 0;
	VDStringSpanA mTokIdent;
	VDStringA mError;
	sint32 mErrorPos = -1;

	bool mbCompileDebugCode = false;

	ATVMDomain *mpDomain = nullptr;
	vdfastvector<uint8> mByteCodeBuffer;

	struct PendingBranchTarget {
		uint32 mLabel;			// label that will be filled in for branch target
		uint32 mPatchOffset;	// offset-after of branch target offset to patch
	};

	vdfastvector<PendingBranchTarget> mPendingBranchTargets;
	uint32 mLabelCounter = 1;

	vdfastvector<uint32> mBreakTargetLabels;

	vdfastvector<void(*)()> mMethodPtrs;
	const ATVMTypeInfo *mpReturnType = nullptr;
	ATVMFunction *mpCurrentFunction = nullptr;
	ATVMFunctionFlags mAsyncAllowedMask = {};
	ATVMConditionalMask mConditionalMask = ATVMConditionalMask::None;

	struct FunctionInfo {
		uint32 mFunctionIndex = 0;
		sint32 mDefinitionPos = -1;
		ATVMFunctionFlags mFlags = {};
		ATVMFunctionFlags mAsyncAllowedMask = {};
	};
	
	FunctionInfo *mpCurrentFunctionInfo = nullptr;

	uint32 mVariableCount = 0;
	vdhashmap<VDStringA, TypeInfo, vdhash<VDStringA>, vdstringpred> mVariableLookup;
	vdhashmap<VDStringA, TypeInfo, vdhash<VDStringA>, vdstringpred> mSpecialVariableLookup;
	vdfastvector<bool> mSpecialVariablesReferenced;
	vdhashmap<VDStringA, TypeInfo, vdhash<VDStringA>, vdstringpred> mThreadVariableLookup;
	vdhashmap<VDStringA, TypeInfo, vdhash<VDStringA>, vdstringpred> mLocalLookup;
	vdhashmap<VDStringA, ClassInfo, vdhash<VDStringA>, vdstringpred> mClassLookup;
	vdhashmap<VDStringA, FunctionInfo *, vdhash<VDStringA>, vdstringpred> mFunctionLookup;
	vdfastvector<FunctionInfo *> mFunctionInfoTable;

	struct TypeInfoListHashPred {
		size_t operator()(vdvector_view<const ATVMTypeInfo> typeList) const;
		bool operator()(vdvector_view<const ATVMTypeInfo> x, vdvector_view<const ATVMTypeInfo> y) const;
	};

	vdhashmap<vdvector_view<const ATVMTypeInfo>, uint32, TypeInfoListHashPred, TypeInfoListHashPred> mFunctionPointerTypeLookup;

	struct HashFnDep {
		size_t operator()(const std::pair<uint32, uint32>& x) const {
			return x.first * 31 + x.second;
		}
	};

	std::unordered_set<std::pair<uint32, uint32>, HashFnDep> mFunctionDependencies;

	vdvector<vdfunction<bool()>> mDeferredCompiles;
	vdfunction<bool(ATVMCompiler&, const char *, const ATVMScriptFragment&)> mpBindEvent;
	vdfunction<bool(ATVMCompiler&, const char *, const ATVMDataValue&)> mpSetOption;

	VDLinearAllocator mTempAllocator;
};

#endif
