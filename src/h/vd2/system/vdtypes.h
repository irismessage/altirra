//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2007 Avery Lee, All Rights Reserved.
//
//	Beginning with 1.6.0, the VirtualDub system library is licensed
//	differently than the remainder of VirtualDub.  This particular file is
//	thus licensed as follows (the "zlib" license):
//
//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.

#ifndef f_VD2_SYSTEM_VDTYPES_H
#define f_VD2_SYSTEM_VDTYPES_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <algorithm>
#include <stdio.h>
#include <stdarg.h>
#include <new>

#ifndef NULL
#define NULL 0
#endif

///////////////////////////////////////////////////////////////////////////
//
//	compiler detection
//
///////////////////////////////////////////////////////////////////////////

#ifndef VD_COMPILER_DETECTED
	#define VD_COMPILER_DETECTED

	#if defined(_MSC_VER)
		#define VD_COMPILER_MSVC	_MSC_VER

		#if _MSC_VER < 1600
			#error Visual Studio 2010 or newer is required.
		#endif

		#define VD_COMPILER_MSVC_VC8_OR_LATER 1

		#ifdef __clang__
			#define VD_COMPILER_MSVC_CLANG 1
		#endif
	#elif defined(__GNUC__)
		#define VD_COMPILER_GCC
		#if defined(__MINGW32__) || defined(__MINGW64__)
			#define VD_COMPILER_GCC_MINGW
		#endif
	#endif
#endif

#ifndef VD_CPU_DETECTED
	#define VD_CPU_DETECTED

	#if defined(_M_AMD64)
		#define VD_CPU_AMD64	1
	#elif defined(_M_IX86) || defined(__i386__)
		#define VD_CPU_X86		1
	#elif defined(_M_ARM)
		#define VD_CPU_ARM
	#endif
#endif

#ifndef VD_PTR_SIZE
	#if defined(VD_CPU_AMD64)
		#define VD_PTR_SIZE		8
	#else
		#define VD_PTR_SIZE		4
	#endif
#endif

///////////////////////////////////////////////////////////////////////////
//
//	types
//
///////////////////////////////////////////////////////////////////////////

// Use stdint.h if we have it

#ifndef VD_STANDARD_TYPES_DECLARED
	#if defined(_MSC_VER) && _MSC_VER >= 1600
		#include <stdint.h>

		typedef int64_t				sint64;
		typedef uint64_t			uint64;
		typedef int32_t				sint32;
		typedef uint32_t			uint32;
		typedef int16_t				sint16;
		typedef uint16_t			uint16;
		typedef int8_t				sint8;
		typedef uint8_t				uint8;

		typedef int64_t				int64;
		typedef int32_t				int32;
		typedef int16_t				int16;
		typedef int8_t				int8;
	#else
		#if defined(__GNUC__)
			typedef signed long long	sint64;
			typedef unsigned long long	uint64;
		#endif
		typedef signed int			sint32;
		typedef unsigned int		uint32;
		typedef signed short		sint16;
		typedef unsigned short		uint16;
		typedef signed char			sint8;
		typedef unsigned char		uint8;

		typedef sint64				int64;
		typedef sint32				int32;
		typedef sint16				int16;
		typedef sint8				int8;
	#endif

	#ifdef _M_AMD64
		typedef sint64 sintptr;
		typedef uint64 uintptr;
	#else
		typedef __w64 sint32 sintptr;
		typedef __w64 uint32 uintptr;
	#endif
#endif

#if defined(_MSC_VER)
	#define VD64(x) x##i64
#elif defined(__GNUC__)
	#define VD64(x) x##ll
#else
	#error Please add an entry for your compiler for 64-bit constant literals.
#endif

	
#define VDAPIENTRY			__cdecl

typedef int64 VDTime;
typedef int64 VDPosition;
typedef	struct __VDGUIHandle *VDGUIHandle;

// enforce wchar_t under Visual C++

#if defined(_MSC_VER) && !defined(_WCHAR_T_DEFINED)
	#include <ctype.h>
#endif

///////////////////////////////////////////////////////////////////////////
//
//	allocation
//
///////////////////////////////////////////////////////////////////////////

#define new_nothrow new(std::nothrow)

///////////////////////////////////////////////////////////////////////////
//
//	attribute support
//
///////////////////////////////////////////////////////////////////////////

#if defined(VD_COMPILER_MSVC)
	#define VDINTERFACE			__declspec(novtable)
	#define VDNORETURN			__declspec(noreturn)
	#define VDPUREFUNC
	#define VDRESTRICT			__restrict

	#define VDNOINLINE			__declspec(noinline)
	#define VDFORCEINLINE		__forceinline
	#define VDALIGN(alignment)	__declspec(align(alignment))

	#define VDCDECL				__cdecl
#elif defined(VD_COMPILER_GCC)
	#define VDINTERFACE
	#define VDNORETURN			__attribute__((noreturn))
	#define VDPUREFUNC			__attribute__((pure))
	#define VDRESTRICT			__restrict
	#define VDNOINLINE			__attribute__((noinline))
	#define VDFORCEINLINE		inline __attribute__((always_inline))
	#define VDALIGN(alignment)	__attribute__((aligned(alignment)))
	#define VDCDECL
#else
	#define VDINTERFACE
	#define VDNORETURN
	#define VDPUREFUNC
	#define VDRESTRICT
	#define VDFORCEINLINE
	#define VDALIGN(alignment)
	#define VDCDECL
#endif

///////////////////////////////////////////////////////////////////////////
//
//	debug support
//
///////////////////////////////////////////////////////////////////////////

enum VDAssertResult {
	kVDAssertBreak,
	kVDAssertContinue,
	kVDAssertIgnore
};

extern VDAssertResult VDAssert(const char *exp, const char *file, int line);
extern VDAssertResult VDAssertPtr(const char *exp, const char *file, int line);
extern void VDDebugPrint(const char *format, ...);

#if defined(_MSC_VER)
	#define VDBREAK		__debugbreak()
#elif defined(__GNUC__)
	#define VDBREAK		__asm__ volatile ("int3" : : )
#else
	#define VDBREAK		*(volatile char *)0 = *(volatile char *)0
#endif

#define VDASSERTCT(exp)	static_assert((exp), #exp)

#ifdef _DEBUG

	namespace {
		template<int line>
		struct VDAssertHelper {
			VDAssertHelper(const char *exp, const char *file) {
				switch(VDAssert(exp, file, line)) {
				case kVDAssertBreak:
					VDBREAK;
					break;
				}
			}
		};
	}

	#define VDFAIL(str)			if (VDAssert(#str, __FILE__, __LINE__) == kVDAssertBreak) { VDBREAK; } else ((void)0)
	#define VDASSERT(exp)		if (!(exp) && VDAssert   (#exp, __FILE__, __LINE__) == kVDAssertBreak) { VDBREAK; } else ((void)0)
	#define VDASSERTPTR(exp) 	if (!(exp) && VDAssertPtr(#exp, __FILE__, __LINE__) == kVDAssertBreak) { VDBREAK; } else ((void)0)
	#define VDVERIFY(exp)		if (!(exp) && VDAssert   (#exp, __FILE__, __LINE__) == kVDAssertBreak) { VDBREAK; } else ((void)0)
	#define VDVERIFYPTR(exp) 	if (!(exp) && VDAssertPtr(#exp, __FILE__, __LINE__) == kVDAssertBreak) { VDBREAK; } else ((void)0)

	#define VDINLINEASSERT(exp)			((exp)||(VDAssertHelper<__LINE__>(#exp, __FILE__),false))
	#define VDINLINEASSERTFALSE(exp)	((exp)&&(VDAssertHelper<__LINE__>("!("#exp")", __FILE__),true))

	#define NEVER_HERE			do { if (VDAssert( "[never here]", __FILE__, __LINE__ )) VDBREAK; __assume(false); } while(false)
	#define	VDNEVERHERE			do { if (VDAssert( "[never here]", __FILE__, __LINE__ )) VDBREAK; __assume(false); } while(false)

	#define VDDEBUG				VDDebugPrint

#else

	#if defined(_MSC_VER)
		#define VDASSERT(exp)		((void)0)
		#define VDASSERTPTR(exp)	((void)0)
	#elif defined(__GNUC__)
		#define VDASSERT(exp)		__builtin_expect(0 != (exp), 1)
		#define VDASSERTPTR(exp)	__builtin_expect(0 != (exp), 1)
	#endif

	#define VDFAIL(str)			(void)(str)
	#define VDVERIFY(exp)		(void)(exp)
	#define VDVERIFYPTR(exp)	(void)(exp)

	#define VDINLINEASSERT(exp)	(exp)
	#define VDINLINEASSERTFALSE(exp)	(exp)

	#if defined(VD_COMPILER_MSVC)
		#define NEVER_HERE			__assume(false)
		#define	VDNEVERHERE			__assume(false)
	#else
		#define NEVER_HERE			VDASSERT(false)
		#define	VDNEVERHERE			VDASSERT(false)
	#endif

	extern int VDDEBUG_Helper(const char *, ...);
	#define VDDEBUG				(void)sizeof VDDEBUG_Helper

#endif

#define VDDEBUG2			VDDebugPrint

#if defined(_MSC_VER) && _MSC_VER < 1900
	#define vdnoexcept			throw()
	#define vdnoexcept_(cond)
	#define	vdnoexcept_false
	#define vdnoexcept_true		throw()
#elif defined(_MSC_VER) && _MSC_VER >= 1900 && defined(_M_IX86)
	// The VC++ x86 compiler has an awful implementation of noexcept that incurs
	// major runtime costs, so we force the non-standard throw() extension
	// instead.
	#define vdnoexcept			throw()
	#define vdnoexcept_(cond)	noexcept(cond)
	#define	vdnoexcept_false	noexcept(false)
	#define vdnoexcept_true		throw()
#else
	#define vdnoexcept			noexcept
	#define vdnoexcept_(cond)	noexcept(cond)
	#define vdnoexcept_false	noexcept(false)
	#define vdnoexcept_true		noexcept(true)
#endif

///////////////////////////////////////////////////////////////////////////
//
// Object scope macros
//
// vdobjectscope() allows you to define a construct where an object is
// constructed and live only within the controlled statement.  This is
// used for vdsynchronized (thread.h) and protected scopes below.
// It relies on a strange quirk of C++ regarding initialized objects
// in the condition of a selection statement and also horribly abuses
// the switch statement, generating rather good code in release builds.
// The catch is that the controlled object must implement a conversion to
// bool returning false and must only be initialized with one argument (C
// syntax).
//
// Unfortunately, handy as this macro is, it is also damned good at
// breaking compilers.  For a start, declaring an object with a non-
// trivial destructor in a switch() kills both VC6 and VC7 with a C1001.
// The bug is fixed in VC8 (MSC 14.00).
//
// A somewhat safer alternative is the for() statement, along the lines
// of:
//
// switch(bool v=false) case 0: default: for(object_def; !v; v=true)
//
// This avoids the conversion operator but unfortunately usually generates
// an actual loop in the output.

#define vdobjectscope(object_def) switch(object_def) case 0: default:

#endif
