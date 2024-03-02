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

#ifndef f_AT_CUSTOMDEVICEVM_H
#define f_AT_CUSTOMDEVICEVM_H

#include <type_traits>
#include <vd2/system/vdtypes.h>
#include <vd2/system/function.h>
#include <vd2/system/linearalloc.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdstl_vectorview.h>

struct ATCDVMObject;
struct ATCDVMFunction;
class ATCDVMDomain;
class ATCDVMThread;

enum class ATCDVMOpcode : uint8 {
	Nop,
	Pop,
	Dup,
	IVLoad,			// varIdx.b
	IVStore,		// varIdx.b
	ILLoad,			// localIdx.b
	ILStore,		// localIdx.b
	ISLoad,			// specialVarIdx.b
	ITLoad,			// threadVarIdx.b
	IntConst,		// const.l
	IntConst8,		// const.b
	IntAdd,
	IntSub,
	IntMul,
	IntDiv,
	IntMod,
	IntAnd,
	IntOr,
	IntXor,
	IntAsr,
	IntAsl,
	Not,
	And,
	Or,
	IntLt,
	IntLe,
	IntGt,
	IntGe,
	IntEq,
	IntNe,
	IntNeg,
	IntNot,
	Jz,
	Jnz,
	Jmp,
	Ljz,
	Ljnz,
	Ljmp,
	LoopChk,
	MethodCallVoid,			// argcount.b, methodindex.b
	MethodCallInt,			// argcount.b, methodindex.b
	StaticMethodCallVoid,	// argcount.b, methodindex.b
	StaticMethodCallInt,	// argcount.b, methodindex.b
	FunctionCallVoid,	// argcount.b, methodindex.b
	FunctionCallInt,	// argcount.b, methodindex.b
	ReturnVoid,
	ReturnInt
};

uint32 ATCDVMGetOpcodeLength(ATCDVMOpcode opcode);

struct ATCDVMObject {};

struct ATCDVMStringObject final : public ATCDVMObject {
	const char *c_str() const {
		return (const char *)(this + 1);
	}
};

struct ATCDVMObjectClass;

enum class ATCDVMTypeClass : uint8 {
	Void,
	Int,
	IntConst,
	IntLValueVariable,
	IntLValueLocal,
	Object,
	ObjectLValue,
	String,
	StringConst,
	FunctionPointer,
	ObjectClass
};

struct ATCDVMTypeInfo {
	ATCDVMTypeClass mClass;
	uint32 mIndex;
	const ATCDVMObjectClass *mpObjectClass;

	bool operator==(const ATCDVMTypeInfo& other) const {
		return mClass == other.mClass
			&& mIndex == other.mIndex
			&& mpObjectClass == other.mpObjectClass;
	}

	bool operator!=(const ATCDVMTypeInfo& other) const {
		return mClass != other.mClass
			|| mIndex != other.mIndex
			|| mpObjectClass != other.mpObjectClass;
	}
};

extern const ATCDVMTypeInfo kATCDVMTypeVoid;
extern const ATCDVMTypeInfo kATCDVMTypeInt;
extern const ATCDVMTypeInfo kATCDVMTypeString;
extern const ATCDVMTypeInfo kATCDVMTypeFunctionPtr;

template<typename T>
inline constexpr ATCDVMTypeInfo kATCDVMTypeClass {
	ATCDVMTypeClass::Object,
	0,
	&T::kVMObjectClass
};

struct ATCDVMDataMember;

enum class ATCDVMDataType : uint8 {
	Invalid,
	Int,
	String,
	Array,
	Script,
	DataObject,
	RuntimeObject
};

struct ATCDVMScriptFragment {
	const char *mpSrcLineStart;
	const char *mpSrc;
	size_t mSrcLength;
	uint32 mLineNo;
};

struct ATCDVMDataValue {
	ATCDVMDataType mType : 8;
	uint32 mSrcOffset : 24;
	uint32 mLength;

	union {
		sint32 mIntValue;
		const char *mpStrValue;
		const ATCDVMDataValue *mpArrayElements;
		const ATCDVMDataMember *mpObjectMembers;
		const ATCDVMScriptFragment *mpScript;
		const ATCDVMObjectClass *mpRuntimeObjectClass;
	};

	bool IsInteger() const { return mType == ATCDVMDataType::Int; }
	bool IsString() const { return mType == ATCDVMDataType::String; }
	bool IsScript() const { return mType == ATCDVMDataType::Script; }
	bool IsArray() const { return mType == ATCDVMDataType::Array; }
	bool IsDataObject() const { return mType == ATCDVMDataType::DataObject; }

	template<typename T>
	bool IsRuntimeObject() const {
		return mType == ATCDVMDataType::RuntimeObject && mpRuntimeObjectClass == &T::kVMObjectClass;
	}

	const char *AsString() const {
		return mpStrValue;
	}

	vdvector_view<const ATCDVMDataValue> AsArray() const {
		return vdvector_view(mpArrayElements, mLength);
	}

	template<typename T>
	T *AsRuntimeObject(const ATCDVMDomain& domain) const;
};

struct ATCDVMDataMember {
	uint32 mNameHash;
	const char *mpName;
	ATCDVMDataValue mValue;
};

typedef void (*ATCDVMExternalVoidMethod)(ATCDVMDomain& domain, const sint32 *args);
typedef sint32 (*ATCDVMExternalIntMethod)(ATCDVMDomain& domain, const sint32 *args);

enum class ATCDVMFunctionFlags : uint32 {
	None = 0,
	Async = 1,
	AsyncSIO = 2,
	AsyncRawSIO = 4,
	AsyncAll = 0xFF,
	Static = 0x100
};

inline ATCDVMFunctionFlags operator&(ATCDVMFunctionFlags x, ATCDVMFunctionFlags y) { return (ATCDVMFunctionFlags)((uint32)x & (uint32)y); }
inline ATCDVMFunctionFlags operator|(ATCDVMFunctionFlags x, ATCDVMFunctionFlags y) { return (ATCDVMFunctionFlags)((uint32)x | (uint32)y); }
inline ATCDVMFunctionFlags& operator&=(ATCDVMFunctionFlags& x, ATCDVMFunctionFlags y) { x = (ATCDVMFunctionFlags)((uint32)x & (uint32)y); return x; }
inline ATCDVMFunctionFlags& operator|=(ATCDVMFunctionFlags& x, ATCDVMFunctionFlags y) { x = (ATCDVMFunctionFlags)((uint32)x | (uint32)y); return x; }
inline ATCDVMFunctionFlags operator~(ATCDVMFunctionFlags x) { return (ATCDVMFunctionFlags)(~(uint32)x); }

struct ATCDVMExternalMethod {
	template<auto T>
	static constexpr ATCDVMExternalMethod Bind(const char *name, ATCDVMFunctionFlags flags = ATCDVMFunctionFlags::None);

	const char *mpName;

	union {
		ATCDVMExternalVoidMethod mpVoidMethod;
		ATCDVMExternalIntMethod mpIntMethod;
	};

	const ATCDVMTypeInfo *const *mpTypes;
	uint32 mNumArgs;
	ATCDVMFunctionFlags mFlags;
};

struct ATCDVMObjectClass {
	const char *mpClassName;
	std::initializer_list<ATCDVMExternalMethod> mMethods;
};

struct ATCDVMFunction {
	const ATCDVMFunction *mpNext;
	const char *mpName;

	ATCDVMTypeInfo mReturnType;
	uint8 *mpByteCode;
	void (*const *mpMethodTable)();
	uint32 mByteCodeLen;
	uint32 mStackSlotsRequired;
	uint32 mLocalSlotsRequired;
};

class ATCDVMDomain {
public:
	VDLinearAllocator mAllocator;
	vdfastvector<sint32> mGlobalVariables;
	vdfastvector<sint32> mSpecialVariables;
	vdfastvector<ATCDVMObject *> mGlobalObjects;
	vdfastvector<const ATCDVMFunction *> mFunctions;
	uint32 mNumThreadVariables = 0;

	vdfastvector<ATCDVMThread *> mThreads;

	ATCDVMThread *mpActiveThread = nullptr;

	void Clear();
};

class ATCDVMThread;

struct ATCDVMThreadWaitQueue {
	vdlist<ATCDVMThread> mThreadList;

	void Reset();
	void Suspend(ATCDVMThread& thread);

	ATCDVMThread *GetNext() const;
	void ResumeVoid();
	void ResumeInt(sint32 v);
};

class ATCDVMThread : public vdlist_node {
	ATCDVMThread(ATCDVMThread&) = delete;
	ATCDVMThread& operator=(const ATCDVMThread&) = delete;

public:
	ATCDVMThread() = default;

	struct StackFrame {
		const ATCDVMFunction *mpFunction;
		const uint8 *mpPC;
		uint32 mBP;
		uint32 mSP;
	};

	ATCDVMDomain *mpDomain;
	uint32 mThreadIndex;
	vdfastvector<StackFrame> mStackFrames;
	vdfastvector<sint32> mArgStack;
	vdfastvector<sint32> mThreadVariables;
	ATCDVMThreadWaitQueue *mpSuspendQueue = nullptr;
	bool mbSuspended;
	const vdfunction<void(ATCDVMThread&)> *mpSuspendAbortFn = nullptr;

	void *mSuspendData[4];

	void Init(ATCDVMDomain& domain);
	void Reset();

	void StartVoid(const ATCDVMFunction& function);
	bool RunVoid(const ATCDVMFunction& function);
	sint32 RunInt(const ATCDVMFunction& function);

	void Suspend(const vdfunction<void(ATCDVMThread&)> *suspendAbortFn = nullptr) {
		mbSuspended = true;
		mpSuspendAbortFn = suspendAbortFn;
	}

	void SetResumeInt(sint32 v);
	bool Resume();
	void Abort();

private:
	bool Run();
};

///////////////////////////////////////////////////////////////////////////

// convert C++ type to VM type info
template<typename T>
inline constexpr const ATCDVMTypeInfo& ATCDVMGetType() {
	return kATCDVMTypeClass<std::remove_pointer_t<T>>;
}

template<> inline constexpr const ATCDVMTypeInfo& ATCDVMGetType<void>() { return kATCDVMTypeVoid; }
template<> inline constexpr const ATCDVMTypeInfo& ATCDVMGetType<sint32>() { return kATCDVMTypeInt; }
template<> inline constexpr const ATCDVMTypeInfo& ATCDVMGetType<const char *>() { return kATCDVMTypeString; }
template<> inline constexpr const ATCDVMTypeInfo& ATCDVMGetType<const ATCDVMFunction *>() { return kATCDVMTypeFunctionPtr; }
template<> inline constexpr const ATCDVMTypeInfo& ATCDVMGetType<ATCDVMDomain>() { return kATCDVMTypeVoid; }

// convert C++ type list to VM type info array
template<typename T_Return, typename... T_Args>
inline constexpr const ATCDVMTypeInfo *g_ATCDVMExternalMethodArgs[] = {
	&ATCDVMGetType<std::remove_cv_t<std::remove_reference_t<T_Return>>>(),
	&ATCDVMGetType<std::remove_cv_t<std::remove_reference_t<T_Args>>>()...
};

// thunk generator
template<typename T>
T ATCDVMDecodeThunkArgument(ATCDVMDomain& domain, const sint32 *argBase, uint32 index) {
	return static_cast<T>(domain.mGlobalObjects[argBase[index]]);
}

template<>
inline sint32 ATCDVMDecodeThunkArgument<sint32>(ATCDVMDomain& domain, const sint32 *argBase, uint32 index) {
	return argBase[index];
}

template<>
inline const char *ATCDVMDecodeThunkArgument<const char *>(ATCDVMDomain& domain, const sint32 *argBase, uint32 index) {
	return static_cast<ATCDVMStringObject *>(domain.mGlobalObjects[argBase[index]])->c_str();
}

template<>
inline const ATCDVMFunction *ATCDVMDecodeThunkArgument<const ATCDVMFunction *>(ATCDVMDomain& domain, const sint32 *argBase, uint32 index) {
	return domain.mFunctions[argBase[index]];
}

template<>
inline ATCDVMDomain& ATCDVMDecodeThunkArgument<ATCDVMDomain&>(ATCDVMDomain& domain, const sint32 *argBase, uint32 index) {
	return domain;
}

template<typename T> struct ATCDVMThunk;

template<typename T_Return, typename T_Object, typename... T_Args>
struct ATCDVMThunk<T_Return (T_Object::*)(T_Args...)> {
	typedef T_Return ReturnType;
	
	static constexpr ATCDVMFunctionFlags kMethodFlags = ATCDVMFunctionFlags::None;

	template<bool... Ts>
	static constexpr uint32 Sum() {
		return (0 + ... + Ts);
	}

	static constexpr uint32 kNumArgs = Sum<(std::is_same_v<T_Args, ATCDVMDomain&> ? 0 : 1)...>();

	template<T_Return (T_Object::*T_Fn)(T_Args...)>
	static constexpr void GetPrototype(ATCDVMExternalMethod& method) {
		method.mpTypes = g_ATCDVMExternalMethodArgs<T_Return, T_Args...>;
		method.mNumArgs = kNumArgs;
	}

	template<T_Return (T_Object::*T_Fn)(T_Args...), uint32... T_ArgIndices>
	static constexpr auto GetThunkFunction(std::integer_sequence<uint32, T_ArgIndices...>) {
		return [](ATCDVMDomain& domain, const sint32 *args) {
			return (static_cast<T_Object *>(domain.mGlobalObjects[args[0]])->*T_Fn)(
				ATCDVMDecodeThunkArgument<T_Args>(domain, args, T_ArgIndices + 1)...
			);
		};
	}

	template<T_Return (T_Object::*T_Fn)(T_Args...)>
	static constexpr auto GetThunkFunction() {
		return GetThunkFunction<T_Fn>(std::make_integer_sequence<uint32, sizeof...(T_Args)>());
	}
};

template<typename T_Return, typename... T_Args>
struct ATCDVMThunk<T_Return (*)(T_Args...)> {
	typedef T_Return ReturnType;

	static constexpr ATCDVMFunctionFlags kMethodFlags = ATCDVMFunctionFlags::Static;

	template<bool... Ts>
	static constexpr uint32 Sum() {
		return (0 + ... + Ts);
	}

	static constexpr uint32 kNumArgs = Sum<(std::is_same_v<T_Args, ATCDVMDomain&> ? 0 : 1)...>();

	template<T_Return (*T_Fn)(T_Args...)>
	static constexpr void GetPrototype(ATCDVMExternalMethod& method) {
		method.mpTypes = g_ATCDVMExternalMethodArgs<T_Return, T_Args...>;
		method.mNumArgs = kNumArgs;
	}

	template<T_Return (*T_Fn)(T_Args...), uint32... T_ArgIndices>
	static constexpr auto GetThunkFunction(std::integer_sequence<uint32, T_ArgIndices...>) {
		return [](ATCDVMDomain& domain, const sint32 *args) {
			return T_Fn(
				ATCDVMDecodeThunkArgument<T_Args>(domain, args, T_ArgIndices)...
			);
		};
	}

	template<T_Return (*T_Fn)(T_Args...)>
	static constexpr auto GetThunkFunction() {
		return GetThunkFunction<T_Fn>(std::make_integer_sequence<uint32, sizeof...(T_Args)>());
	}
};

template<auto T>
constexpr ATCDVMExternalMethod ATCDVMExternalMethod::Bind(const char *name, ATCDVMFunctionFlags flags) {
	using ThunkType = ATCDVMThunk<decltype(T)>;

	ATCDVMExternalMethod method {};

	if constexpr (std::is_same_v<typename ThunkType::ReturnType, sint32>)
		method.mpIntMethod = ThunkType::template GetThunkFunction<T>();
	else
		method.mpVoidMethod = ThunkType::template GetThunkFunction<T>();

	ThunkType::GetPrototype<T>(method);

	method.mpName = name;
	method.mFlags = flags | ThunkType::kMethodFlags;

	return method;
}

template<typename T>
T *ATCDVMDataValue::AsRuntimeObject(const ATCDVMDomain& domain) const {
	return static_cast<T *>(domain.mGlobalObjects[mLength]);
}

#endif
