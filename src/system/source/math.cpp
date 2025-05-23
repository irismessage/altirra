//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
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

#include <stdafx.h>
#include <math.h>
#include <vd2/system/math.h>
#include <vd2/system/int128.h>

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
#include <immintrin.h>
#endif

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
int VDRoundToInt(float x) {
	return (int)_mm_cvtss_si32(_mm_set_ss(x));
}

int VDRoundToInt(double x) {
	return (int)_mm_cvtsd_si32(_mm_set_sd(x));
}

sint32 VDRoundToInt32(float x) {
	return (sint32)_mm_cvtss_si32(_mm_set_ss(x));
}

sint32 VDRoundToInt32(double x) {
	return (sint32)_mm_cvtsd_si32(_mm_set_sd(x));
}

sint64 VDRoundToInt64(float x) {
	return (sint64)floorf(x + 0.5f);
}

sint64 VDRoundToInt64(double x) {
	return (sint64)floor(x + 0.5);
}
#else
int VDRoundToInt(float x) {
	return (int)floorf(x + 0.5f);
}

int VDRoundToInt(double x) {
	return (int)floor(x + 0.5);
}

sint32 VDRoundToInt32(float x) {
	return (sint32)floorf(x + 0.5f);
}

sint32 VDRoundToInt32(double x) {
	return (sint32)floor(x + 0.5);
}

sint64 VDRoundToInt64(float x) {
	return (sint64)floorf(x + 0.5f);
}

sint64 VDRoundToInt64(double x) {
	return (sint64)floor(x + 0.5);
}
#endif

#if defined(VD_CPU_X86) && defined(VD_COMPILER_MSVC) && !defined(VD_COMPILER_MSVC_CLANG)
	sint64 __declspec(naked) __stdcall VDFractionScale64(uint64 a, uint32 b, uint32 c, uint32& remainder) {
		__asm {
			push	edi
			push	ebx
			mov		edi, [esp+12+8]			;edi = b
			mov		eax, [esp+4+8]			;eax = a[lo]
			mul		edi						;edx:eax = a[lo]*b
			mov		ecx, eax				;ecx = (a*b)[lo]
			mov		eax, [esp+8+8]			;eax = a[hi]
			mov		ebx, edx				;ebx = (a*b)[mid]
			mul		edi						;edx:eax = a[hi]*b
			add		eax, ebx
			mov		ebx, [esp+16+8]			;ebx = c
			adc		edx, 0
			div		ebx						;eax = (a*b)/c [hi], edx = (a[hi]*b)%c
			mov		edi, eax				;edi = (a[hi]*b)/c
			mov		eax, ecx				;eax = (a*b)[lo]
			mov		ecx, [esp+20+8]
			div		ebx						;eax = (a*b)/c [lo], edx = (a*b)%c
			mov		[ecx], edx
			mov		edx, edi
			pop		ebx
			pop		edi
			ret		20
		}
	}

	uint64 __declspec(naked) __stdcall VDUMulDiv64x32(uint64 a, uint32 b, uint32 c) {
		__asm {
			mov		eax, [esp+4]			;eax = a0
			mul		dword ptr [esp+12]		;edx:eax = a0*b
			mov		dword ptr [esp+4], eax	;tmp = a0*b[0:31]
			mov		ecx, edx				;ecx = a0*b[32:63]
			mov		eax, [esp+8]			;eax = a1
			mul		dword ptr [esp+12]		;edx:eax = a1*b
			add		eax, ecx				;edx:eax += a0*b[32:95]
			adc		edx, 0					;(cont.)
			cmp		edx, [esp+16]			;test if a*b[64:95] >= c; equiv to a*b >= (c<<64)
			jae		invalid					;abort if so (overflow)
			div		dword ptr [esp+16]		;edx,eax = ((a*b)[32:95]/c, (a*b)[32:95]%c)
			mov		ecx, eax
			mov		eax, [esp+4]
			div		dword ptr [esp+16]
			mov		edx, ecx
			ret		16
invalid:
			mov		eax, -1					;return FFFFFFFFFFFFFFFF
			mov		edx, -1
			ret		16
		}
	}
#elif defined(VD_CPU_AMD64)
	#if defined(VD_COMPILER_CLANG)
		sint64 VDFractionScale64(uint64 a, uint32 b, uint32 c, uint32& remainder) {
			return (sint64)(((__uint128_t)a * b) / c);
		}
	#else
		sint64 VDFractionScale64(uint64 a, uint32 b, uint32 c, uint32& remainder) {
			unsigned __int64 hi = 0;
			unsigned __int64 lo = _umul128(a, b, &hi);
			unsigned __int64 r = 0;

			return _udiv128(hi, lo, c, &r);
		}
	#endif
#else
	sint64 VDFractionScale64(uint64 a, uint32 b, uint32 c, uint32& remainder) {
		uint32 a0 = (uint32)a;
		uint32 a1 = (uint32)(a >> 32);

		uint64 m0 = (uint64)a0*b;
		uint64 m1 = (uint64)a1*b;

		// collect all multiplier terms
		uint32 s0  = (uint32)m0;
		uint32 s1a = (uint32)(m0 >> 32);
		uint32 s1b = (uint32)m1;
		uint32 s2  = (uint32)(m1 >> 32);

		// form 96-bit intermediate product
		uint32 acc0 = s0;
		uint32 acc1 = s1a + s1b;
		uint32 acc2 = s2 + (acc1 < s1b);

		// check for overflow (or divide by zero)
		if (acc2 >= c)
			return 0xFFFFFFFFFFFFFFFFULL;

		// do divide
		uint64 div1 = ((uint64)acc2 << 32) + acc1;
		uint64 q1 = div1 / c;
		uint64 div0 = ((div1 % c) << 32) + acc0;
		uint32 q0 = (uint32)(div0 / c);

		remainder = (uint32)(div0 % c);

		return (q1 << 32) + q0;
	}

	uint64 VDUMulDiv64x32(uint64 a, uint32 b, uint32 c) {
		uint32 r;

		return VDFractionScale64(a, b, c, r);
	}
#endif

sint64 VDMulDiv64(sint64 a, sint64 b, sint64 c) {
	bool flip = false;

	if (a < 0) {
		a = -a;
		flip = true;
	}

	if (b < 0) {
		b = -b;
		flip = !flip;
	}

	if (c < 0) {
		c = -c;
		flip = !flip;
	}

	uint64 rem;
	uint64 v = VDUDiv128x64To64(VDUMul64x64To128((uint64)a, (uint64)b), (uint64)c, rem);

	if ((rem+rem) >= (uint64)c)
		++v;

	return flip ? -(sint64)v : (sint64)v;
}

bool VDVerifyFiniteFloats(const float *p0, uint32 n) {
	const uint32 *p = (const uint32 *)p0;

	while(n--) {
		uint32 v = *p++;

		// 00000000				zero
		// 00000001-007FFFFF	denormal
		// 00800000-7F7FFFFF	finite
		// 7F800000				infinity
		// 7F800001-7FBFFFFF	SNaN
		// 7FC00000-7FFFFFFF	QNaN

		if ((v & 0x7FFFFFFF) >= 0x7F800000)
			return false;
	}

	return true;
}

VDFastMathScope::VDFastMathScope() {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	mPrevValue = _mm_getcsr();

	// Turn on flush-to-zero (FTZ) to help with IIR filters.
	// Denormals-as-zero (DAZ) requires additional checking for old P4s,
	// and we don't really need it, though it won't hurt if it is already on.
	_mm_setcsr(mPrevValue | _MM_FLUSH_ZERO_ON);
#endif
}

VDFastMathScope::~VDFastMathScope() {
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	_mm_setcsr(mPrevValue);
#endif
}

///////////////////////////////////////////////////////////////////////////

static_assert(VDFloorToInt(-2147483648.0) == -0x7FFFFFFF - 1);
static_assert(VDFloorToInt(-2.0) == -2);
static_assert(VDFloorToInt(-1.5) == -2);
static_assert(VDFloorToInt(-1.0) == -1);
static_assert(VDFloorToInt(-0.5) == -1);
static_assert(VDFloorToInt(0.0) == 0);
static_assert(VDFloorToInt(+0.5) == 0);
static_assert(VDFloorToInt(+1.0) == 1);
static_assert(VDFloorToInt(+1.5) == 1);
static_assert(VDFloorToInt(+2.0) == 2);
static_assert(VDFloorToInt(+2147483647.0) == 0x7FFFFFFF);

static_assert(VDFloorToInt32(-2147483648.0f) == -0x7FFFFFFF - 1);
static_assert(VDFloorToInt32(-2.0f) == -2);
static_assert(VDFloorToInt32(-1.5f) == -2);
static_assert(VDFloorToInt32(-1.0f) == -1);
static_assert(VDFloorToInt32(-0.5f) == -1);
static_assert(VDFloorToInt32(0.0f) == 0);
static_assert(VDFloorToInt32(+0.5f) == 0);
static_assert(VDFloorToInt32(+1.0f) == 1);
static_assert(VDFloorToInt32(+1.5f) == 1);
static_assert(VDFloorToInt32(+2.0f) == 2);
static_assert(VDFloorToInt32(-0x1000000.0p0f) == -0x1000000);
static_assert(VDFloorToInt32( 0x0FFFFFF.0p0f) ==  0x0FFFFFF);
static_assert(VDFloorToInt32(0x7FFFFF80.0p0f) == 0x7FFFFF80);

static_assert(VDFloorToInt32(-2147483648.0) == -0x7FFFFFFF - 1);
static_assert(VDFloorToInt32(-2.0) == -2);
static_assert(VDFloorToInt32(-1.5) == -2);
static_assert(VDFloorToInt32(-1.0) == -1);
static_assert(VDFloorToInt32(-0.5) == -1);
static_assert(VDFloorToInt32(0.0) == 0);
static_assert(VDFloorToInt32(+0.5) == 0);
static_assert(VDFloorToInt32(+1.0) == 1);
static_assert(VDFloorToInt32(+1.5) == 1);
static_assert(VDFloorToInt32(+2.0) == 2);
static_assert(VDFloorToInt32(+2147483647.0) == 0x7FFFFFFF);

static_assert(VDFloorToInt64(-4294967296.0) == -0x100000000LL);
static_assert(VDFloorToInt64(-2147483648.0) == -0x80000000LL);
static_assert(VDFloorToInt64(-2.0) == -2);
static_assert(VDFloorToInt64(-1.5) == -2);
static_assert(VDFloorToInt64(-1.0) == -1);
static_assert(VDFloorToInt64(-0.5) == -1);
static_assert(VDFloorToInt64(0.0) == 0);
static_assert(VDFloorToInt64(+0.5) == 0);
static_assert(VDFloorToInt64(+1.0) == 1);
static_assert(VDFloorToInt64(+1.5) == 1);
static_assert(VDFloorToInt64(+2.0) == 2);
static_assert(VDFloorToInt64(+2147483647.0) == 0x7FFFFFFF);
static_assert(VDFloorToInt64(+4294967296.0) == 0x100000000LL);

static_assert(VDCeilToInt(-2147483648.0) == -0x7FFFFFFF - 1);
static_assert(VDCeilToInt(-2.0) == -2);
static_assert(VDCeilToInt(-1.5) == -1);
static_assert(VDCeilToInt(-1.0) == -1);
static_assert(VDCeilToInt(-0.5) == 0);
static_assert(VDCeilToInt(0.0) == 0);
static_assert(VDCeilToInt(+0.5) == 1);
static_assert(VDCeilToInt(+1.0) == 1);
static_assert(VDCeilToInt(+1.5) == 2);
static_assert(VDCeilToInt(+2.0) == 2);
static_assert(VDCeilToInt(+2147483647.0) == 0x7FFFFFFF);

static_assert(VDCeilToInt64(-4294967296.0) == -0x100000000LL);
static_assert(VDCeilToInt64(-2147483648.0) == -0x80000000LL);
static_assert(VDCeilToInt64(-2.0) == -2);
static_assert(VDCeilToInt64(-1.5) == -1);
static_assert(VDCeilToInt64(-1.0) == -1);
static_assert(VDCeilToInt64(-0.5) == 0);
static_assert(VDCeilToInt64(0.0) == 0);
static_assert(VDCeilToInt64(+0.5) == 1);
static_assert(VDCeilToInt64(+1.0) == 1);
static_assert(VDCeilToInt64(+1.5) == 2);
static_assert(VDCeilToInt64(+2.0) == 2);
static_assert(VDCeilToInt64(+2147483647.0) == 0x7FFFFFFF);
static_assert(VDCeilToInt64(+4294967296.0) == 0x100000000LL);
