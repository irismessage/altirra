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

#ifndef f_AT_CUSTOMDEVICECOMPILER_H
#define f_AT_CUSTOMDEVICECOMPILER_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/error.h>
#include <vd2/system/vdstl_vectorview.h>
#include "customdevicevm.h"

class ATCDVMDomain;
struct ATCDVMFunction;
struct ATCDVMDataValue;

class ATCDCompileError final : public MyError {
public:
	using MyError::MyError;

	ATCDCompileError(const ATCDVMDataValue& ref, const char *err);

	static ATCDCompileError Format(const ATCDVMDataValue& ref, const char *format, ...);

	uint32 mSrcOffset = ~UINT32_C(0);
};


enum class ATCDConditionalMask : uint32 {
	None = 0,
	Allowed = 1,
	DebugEnabled = 2,
	NonDebugEnabled = 4,

	DebugOnly = Allowed | DebugEnabled,
	NonDebugOnly = Allowed | NonDebugEnabled
};

class ATCustomDeviceCompiler {
public:
	typedef vdfunction<bool(ATCustomDeviceCompiler&, const char *, const ATCDVMDataValue *)> DefineInstanceFn;

	ATCustomDeviceCompiler(ATCDVMDomain& domain);

	void SetBindEventHandler(vdfunction<bool(ATCustomDeviceCompiler&, const char *, const ATCDVMScriptFragment&)> bindEventFn);
	void SetOptionHandler(vdfunction<bool(ATCustomDeviceCompiler&, const char *, const ATCDVMDataValue&)> setOptionFn);

	static bool IsValidVariableName(const char *name);
	bool IsSpecialVariableReferenced(const char *name) const;

	void DefineClass(const ATCDVMObjectClass& objClass, DefineInstanceFn *defineFn = nullptr);
	bool DefineObjectVariable(const char *name, ATCDVMObject *obj, const ATCDVMObjectClass& objClass);
	bool DefineSpecialObjectVariable(const char *name, ATCDVMObject *obj, const ATCDVMObjectClass& objClass);

	template<typename T>
	bool DefineObjectVariable(const char *name, T *obj) {
		static_assert(std::is_convertible_v<T, ATCDVMObject>);
		return DefineObjectVariable(name, obj, T::kVMObjectClass);
	}

	template<typename T>
	bool DefineSpecialObjectVariable(const char *name, T *obj) {
		static_assert(std::is_convertible_v<T, ATCDVMObject>);
		return DefineSpecialObjectVariable(name, obj, T::kVMObjectClass);
	}

	bool DefineIntegerVariable(const char *name);
	void DefineSpecialVariable(const char *name);
	void DefineThreadVariable(const char *name);

	const ATCDVMTypeInfo *GetVariable(const char *name) const;

	const ATCDVMFunction *DeferCompile(const ATCDVMTypeInfo& returnType, const ATCDVMScriptFragment& scriptFragment, ATCDVMFunctionFlags asyncAllowedMask = ATCDVMFunctionFlags::None, ATCDConditionalMask conditionalMask = ATCDConditionalMask::None);
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
		kTokEvent,
		kTokOption
	};

	typedef ATCDVMTypeClass TypeClass;
	typedef ATCDVMTypeInfo TypeInfo;

	struct ClassInfo {
		TypeInfo mTypeInfo;

		DefineInstanceFn mpDefineFn;
	};

	void InitSource(const void *src, size_t len);

	bool ParseFile();
	bool ParseVariableDefinition(uint32 tok);
	bool ParseEventBinding();
	bool ParseOption();
	bool ParseFunction();
	bool ParseFunction(ATCDVMFunction *func, const ATCDVMTypeInfo& returnType, ATCDVMFunctionFlags asyncAllowedMask, ATCDConditionalMask conditionalMask);
	bool ParseBlock(bool& hasReturn);
	bool ParseStatement(uint32 tok, bool& hasReturn);
	bool ParseIfStatement(bool& hasReturn);
	bool ParseReturnStatement();
	bool ParseLoopStatement();
	bool ParseDeclStatement(uint32 typeToken);
	bool ParseExpressionStatement(uint32 tok);
	bool ParseDataValue(ATCDVMDataValue& value);
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
	void ConvertToRvalue(TypeInfo& returnType);
	uint32 GetFunctionPointerTypeIndex(vdvector_view<const ATCDVMTypeInfo> types);

	void LoadConst(sint32 v);

	void Push(uint32 tok) { mPushedToken = tok; }
	uint32 Token();

	const char *mpSrc0 = nullptr;
	const char *mpSrc = nullptr;
	const char *mpSrcStart = nullptr;
	const char *mpSrcEnd = nullptr;

	uint32 mPushedToken = 0;
	sint32 mTokValue = 0;
	VDStringSpanA mTokIdent;
	VDStringA mError;
	sint32 mErrorPos = -1;

	ATCDVMDomain *mpDomain = nullptr;
	vdfastvector<uint8> mByteCodeBuffer;
	vdfastvector<void(*)()> mMethodPtrs;
	const ATCDVMTypeInfo *mpReturnType = nullptr;
	ATCDVMFunction *mpCurrentFunction = nullptr;
	ATCDVMFunctionFlags mAsyncAllowedMask = {};
	ATCDConditionalMask mConditionalMask = ATCDConditionalMask::None;

	struct FunctionInfo {
		uint32 mFunctionIndex = 0;
		ATCDVMFunctionFlags mFlags = {};
	};
	
	FunctionInfo *mpCurrentFunctionInfo = nullptr;

	uint32 mVariableCount = 0;
	vdhashmap<VDStringA, TypeInfo, vdhash<VDStringA>, vdstringpred> mVariableLookup;
	vdhashmap<VDStringA, TypeInfo, vdhash<VDStringA>, vdstringpred> mSpecialVariableLookup;
	vdfastvector<bool> mSpecialVariablesReferenced;
	vdhashmap<VDStringA, TypeInfo, vdhash<VDStringA>, vdstringpred> mThreadVariableLookup;
	vdhashmap<VDStringA, TypeInfo, vdhash<VDStringA>, vdstringpred> mLocalLookup;
	vdhashmap<VDStringA, ClassInfo, vdhash<VDStringA>, vdstringpred> mClassLookup;
	vdhashmap<VDStringA, FunctionInfo, vdhash<VDStringA>, vdstringpred> mFunctionLookup;

	struct TypeInfoListHashPred {
		size_t operator()(vdvector_view<const ATCDVMTypeInfo> typeList) const;
		bool operator()(vdvector_view<const ATCDVMTypeInfo> x, vdvector_view<const ATCDVMTypeInfo> y) const;
	};

	vdhashmap<vdvector_view<const ATCDVMTypeInfo>, uint32, TypeInfoListHashPred, TypeInfoListHashPred> mFunctionPointerTypeLookup;

	vdvector<vdfunction<bool()>> mDeferredCompiles;
	vdfunction<bool(ATCustomDeviceCompiler&, const char *, const ATCDVMScriptFragment&)> mpBindEvent;
	vdfunction<bool(ATCustomDeviceCompiler&, const char *, const ATCDVMDataValue&)> mpSetOption;
};

#endif
