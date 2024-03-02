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

#ifndef f_AT_ATVM_VM_H
#define f_AT_ATVM_VM_H

#include <type_traits>
#include <vd2/system/vdtypes.h>
#include <vd2/system/function.h>
#include <vd2/system/linearalloc.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdstl_vectorview.h>

struct ATVMObject;
struct ATVMFunction;
class ATVMDomain;
class ATVMThread;

enum class ATVMOpcode : uint8 {
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

uint32 ATVMGetOpcodeLength(ATVMOpcode opcode);

struct ATVMObject {};

struct ATVMStringObject final : public ATVMObject {
	const char *c_str() const {
		return (const char *)(this + 1);
	}
};

struct ATVMObjectClass;

enum class ATVMTypeClass : uint8 {
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

struct ATVMTypeInfo {
	ATVMTypeClass mClass;
	uint32 mIndex;
	const ATVMObjectClass *mpObjectClass;

	bool operator==(const ATVMTypeInfo& other) const {
		return mClass == other.mClass
			&& mIndex == other.mIndex
			&& mpObjectClass == other.mpObjectClass;
	}

	bool operator!=(const ATVMTypeInfo& other) const {
		return mClass != other.mClass
			|| mIndex != other.mIndex
			|| mpObjectClass != other.mpObjectClass;
	}
};

extern const ATVMTypeInfo kATVMTypeVoid;
extern const ATVMTypeInfo kATVMTypeInt;
extern const ATVMTypeInfo kATVMTypeString;
extern const ATVMTypeInfo kATVMTypeFunctionPtr;

template<typename T>
inline constexpr ATVMTypeInfo kATVMTypeClass {
	ATVMTypeClass::Object,
	0,
	&T::kVMObjectClass
};

struct ATVMDataMember;

enum class ATVMDataType : uint8 {
	Invalid,
	Int,
	String,
	Array,
	Script,
	DataObject,
	RuntimeObject
};

struct ATVMScriptFragment {
	const char *mpSrc;
	sint32 mSrcOffset;
	size_t mSrcLength;
};

struct ATVMDataValue {
	ATVMDataType mType : 8;
	uint32 mSrcOffset : 24;
	uint32 mLength;

	union {
		sint32 mIntValue;
		const char *mpStrValue;
		const ATVMDataValue *mpArrayElements;
		const ATVMDataMember *mpObjectMembers;
		const ATVMScriptFragment *mpScript;
		const ATVMObjectClass *mpRuntimeObjectClass;
	};

	bool IsInteger() const { return mType == ATVMDataType::Int; }
	bool IsString() const { return mType == ATVMDataType::String; }
	bool IsScript() const { return mType == ATVMDataType::Script; }
	bool IsArray() const { return mType == ATVMDataType::Array; }
	bool IsDataObject() const { return mType == ATVMDataType::DataObject; }

	template<typename T>
	bool IsRuntimeObject() const {
		return mType == ATVMDataType::RuntimeObject && mpRuntimeObjectClass == &T::kVMObjectClass;
	}

	const char *AsString() const {
		return mpStrValue;
	}

	vdvector_view<const ATVMDataValue> AsArray() const {
		return vdvector_view(mpArrayElements, mLength);
	}

	template<typename T>
	T *AsRuntimeObject(const ATVMDomain& domain) const;
};

struct ATVMDataMember {
	uint32 mNameHash;
	const char *mpName;
	ATVMDataValue mValue;
};

typedef void (*ATVMExternalVoidMethod)(ATVMDomain& domain, const sint32 *args);
typedef sint32 (*ATVMExternalIntMethod)(ATVMDomain& domain, const sint32 *args);

enum class ATVMFunctionFlags : uint32 {
	None = 0,
	Async = 1,
	AsyncSIO = 2,
	AsyncRawSIO = 4,
	AsyncAll = 0xFF,
	Static = 0x100
};

inline ATVMFunctionFlags operator&(ATVMFunctionFlags x, ATVMFunctionFlags y) { return (ATVMFunctionFlags)((uint32)x & (uint32)y); }
inline ATVMFunctionFlags operator|(ATVMFunctionFlags x, ATVMFunctionFlags y) { return (ATVMFunctionFlags)((uint32)x | (uint32)y); }
inline ATVMFunctionFlags& operator&=(ATVMFunctionFlags& x, ATVMFunctionFlags y) { x = (ATVMFunctionFlags)((uint32)x & (uint32)y); return x; }
inline ATVMFunctionFlags& operator|=(ATVMFunctionFlags& x, ATVMFunctionFlags y) { x = (ATVMFunctionFlags)((uint32)x | (uint32)y); return x; }
inline ATVMFunctionFlags operator~(ATVMFunctionFlags x) { return (ATVMFunctionFlags)(~(uint32)x); }

struct ATVMExternalMethod {
	template<auto T>
	static constexpr ATVMExternalMethod Bind(const char *name, ATVMFunctionFlags flags = ATVMFunctionFlags::None);

	const char *mpName;

	union {
		ATVMExternalVoidMethod mpVoidMethod;
		ATVMExternalIntMethod mpIntMethod;
	};

	const ATVMTypeInfo *const *mpTypes;
	uint32 mNumArgs;
	ATVMFunctionFlags mFlags;
};

struct ATVMObjectClass {
	const char *mpClassName;
	std::initializer_list<ATVMExternalMethod> mMethods;
};

struct ATVMFunction {
	const ATVMFunction *mpNext = nullptr;
	const char *mpName = nullptr;

	ATVMTypeInfo mReturnType {};
	uint8 *mpByteCode = nullptr;
	void (*const *mpMethodTable)() = nullptr;
	uint32 mByteCodeLen = 0;
	uint32 mStackSlotsRequired = 0;
	uint32 mLocalSlotsRequired = 0;
};

class ATVMDomain {
public:
	VDLinearAllocator mAllocator;
	vdfastvector<sint32> mGlobalVariables;
	vdfastvector<sint32> mSpecialVariables;
	vdfastvector<ATVMObject *> mGlobalObjects;
	vdfastvector<const ATVMFunction *> mFunctions;
	uint32 mNumThreadVariables = 0;

	vdfastvector<ATVMThread *> mThreads;

	ATVMThread *mpActiveThread = nullptr;

	vdfunction<void(const char *)> mInfiniteLoopHandler;

	void Clear();
};

class ATVMThread;

struct ATVMThreadWaitQueue {
	vdlist<ATVMThread> mThreadList;

	void Reset();
	void Suspend(ATVMThread& thread);

	ATVMThread *GetNext() const;
	ATVMThread *Pop();
	void ResumeVoid();
	void ResumeInt(sint32 v);

	void TransferNext(ATVMThreadWaitQueue& dest);
	void TransferAll(ATVMThreadWaitQueue& dest);
};

class ATVMThread : public vdlist_node {
	ATVMThread(ATVMThread&) = delete;
	ATVMThread& operator=(const ATVMThread&) = delete;

public:
	ATVMThread() = default;

	struct StackFrame {
		const ATVMFunction *mpFunction;
		const uint8 *mpPC;
		uint32 mBP;
		uint32 mSP;
	};

	ATVMDomain *mpDomain;
	uint32 mThreadIndex;
	vdfastvector<StackFrame> mStackFrames;
	vdfastvector<sint32> mArgStack;
	vdfastvector<sint32> mThreadVariables;
	ATVMThreadWaitQueue mJoinQueue;
	ATVMThreadWaitQueue *mpSuspendQueue = nullptr;
	bool mbSuspended;
	const vdfunction<void(ATVMThread&)> *mpSuspendAbortFn = nullptr;

	void *mSuspendData[4];

	bool IsStarted() const { return !mStackFrames.empty(); }

	void Init(ATVMDomain& domain);
	void Reset();

	void StartVoid(const ATVMFunction& function);
	bool RunVoid(const ATVMFunction& function);
	sint32 RunInt(const ATVMFunction& function);

	void Suspend(const vdfunction<void(ATVMThread&)> *suspendAbortFn = nullptr) {
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
inline constexpr const ATVMTypeInfo& ATVMGetType() {
	return kATVMTypeClass<std::remove_pointer_t<T>>;
}

template<> inline constexpr const ATVMTypeInfo& ATVMGetType<void>() { return kATVMTypeVoid; }
template<> inline constexpr const ATVMTypeInfo& ATVMGetType<sint32>() { return kATVMTypeInt; }
template<> inline constexpr const ATVMTypeInfo& ATVMGetType<const char *>() { return kATVMTypeString; }
template<> inline constexpr const ATVMTypeInfo& ATVMGetType<const ATVMFunction *>() { return kATVMTypeFunctionPtr; }
template<> inline constexpr const ATVMTypeInfo& ATVMGetType<ATVMDomain>() { return kATVMTypeVoid; }

// convert C++ type list to VM type info array
template<typename T_Return, typename... T_Args>
inline constexpr const ATVMTypeInfo *g_ATVMExternalMethodArgs[] = {
	&ATVMGetType<std::remove_cv_t<std::remove_reference_t<T_Return>>>(),
	&ATVMGetType<std::remove_cv_t<std::remove_reference_t<T_Args>>>()...
};

// thunk generator
template<typename T>
T ATVMDecodeThunkArgument(ATVMDomain& domain, const sint32 *argBase, uint32 index) {
	return static_cast<T>(domain.mGlobalObjects[argBase[index]]);
}

template<>
inline sint32 ATVMDecodeThunkArgument<sint32>(ATVMDomain& domain, const sint32 *argBase, uint32 index) {
	return argBase[index];
}

template<>
inline const char *ATVMDecodeThunkArgument<const char *>(ATVMDomain& domain, const sint32 *argBase, uint32 index) {
	return static_cast<ATVMStringObject *>(domain.mGlobalObjects[argBase[index]])->c_str();
}

template<>
inline const ATVMFunction *ATVMDecodeThunkArgument<const ATVMFunction *>(ATVMDomain& domain, const sint32 *argBase, uint32 index) {
	return domain.mFunctions[argBase[index]];
}

template<>
inline ATVMDomain& ATVMDecodeThunkArgument<ATVMDomain&>(ATVMDomain& domain, const sint32 *argBase, uint32 index) {
	return domain;
}

template<typename T> struct ATVMThunk;

template<typename T_Return, typename T_Object, typename... T_Args>
struct ATVMThunk<T_Return (T_Object::*)(T_Args...)> {
	typedef T_Return ReturnType;
	
	static constexpr ATVMFunctionFlags kMethodFlags = ATVMFunctionFlags::None;

	template<bool... Ts>
	static constexpr uint32 Sum() {
		return (0 + ... + Ts);
	}

	static constexpr uint32 kNumArgs = Sum<(std::is_same_v<T_Args, ATVMDomain&> ? 0 : 1)...>();

	template<T_Return (T_Object::*T_Fn)(T_Args...)>
	static constexpr void GetPrototype(ATVMExternalMethod& method) {
		method.mpTypes = g_ATVMExternalMethodArgs<T_Return, T_Args...>;
		method.mNumArgs = kNumArgs;
	}

	template<T_Return (T_Object::*T_Fn)(T_Args...), uint32... T_ArgIndices>
	static constexpr auto GetThunkFunction(std::integer_sequence<uint32, T_ArgIndices...>) {
		return [](ATVMDomain& domain, const sint32 *args) {
			return (static_cast<T_Object *>(domain.mGlobalObjects[args[0]])->*T_Fn)(
				ATVMDecodeThunkArgument<T_Args>(domain, args, T_ArgIndices + 1)...
			);
		};
	}

	template<T_Return (T_Object::*T_Fn)(T_Args...)>
	static constexpr auto GetThunkFunction() {
		return GetThunkFunction<T_Fn>(std::make_integer_sequence<uint32, sizeof...(T_Args)>());
	}
};

template<typename T_Return, typename... T_Args>
struct ATVMThunk<T_Return (*)(T_Args...)> {
	typedef T_Return ReturnType;

	static constexpr ATVMFunctionFlags kMethodFlags = ATVMFunctionFlags::Static;

	template<bool... Ts>
	static constexpr uint32 Sum() {
		return (0 + ... + Ts);
	}

	static constexpr uint32 kNumArgs = Sum<(std::is_same_v<T_Args, ATVMDomain&> ? 0 : 1)...>();

	template<T_Return (*T_Fn)(T_Args...)>
	static constexpr void GetPrototype(ATVMExternalMethod& method) {
		method.mpTypes = g_ATVMExternalMethodArgs<T_Return, T_Args...>;
		method.mNumArgs = kNumArgs;
	}

	template<T_Return (*T_Fn)(T_Args...), uint32... T_ArgIndices>
	static constexpr auto GetThunkFunction(std::integer_sequence<uint32, T_ArgIndices...>) {
		return [](ATVMDomain& domain, const sint32 *args) {
			return T_Fn(
				ATVMDecodeThunkArgument<T_Args>(domain, args, T_ArgIndices)...
			);
		};
	}

	template<T_Return (*T_Fn)(T_Args...)>
	static constexpr auto GetThunkFunction() {
		return GetThunkFunction<T_Fn>(std::make_integer_sequence<uint32, sizeof...(T_Args)>());
	}
};

template<auto T>
constexpr ATVMExternalMethod ATVMExternalMethod::Bind(const char *name, ATVMFunctionFlags flags) {
	using ThunkType = ATVMThunk<decltype(T)>;

	ATVMExternalMethod method {};

	if constexpr (std::is_same_v<typename ThunkType::ReturnType, sint32>)
		method.mpIntMethod = ThunkType::template GetThunkFunction<T>();
	else
		method.mpVoidMethod = ThunkType::template GetThunkFunction<T>();

	ThunkType::template GetPrototype<T>(method);

	method.mpName = name;
	method.mFlags = flags | ThunkType::kMethodFlags;

	return method;
}

template<typename T>
T *ATVMDataValue::AsRuntimeObject(const ATVMDomain& domain) const {
	return static_cast<T *>(domain.mGlobalObjects[mLength]);
}

#endif
