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

#ifndef f_VD2_SYSTEM_CPUACCEL_H
#define f_VD2_SYSTEM_CPUACCEL_H

#if VD_CPU_X86 || VD_CPU_X64
static constexpr auto CPUF_SUPPORTS_CPUID		= (0x00000001L);
static constexpr auto CPUF_SUPPORTS_FPU			= (0x00000002L);
static constexpr auto CPUF_SUPPORTS_MMX			= (0x00000004L);
static constexpr auto CPUF_SUPPORTS_INTEGER_SSE	= (0x00000008L);
static constexpr auto CPUF_SUPPORTS_SSE			= (0x00000010L);
static constexpr auto CPUF_SUPPORTS_SSE2		= (0x00000020L);
// unused, formerly 3DNow!						= (0x00000040L);
// unused, formerly Extended 3DNow!				= (0x00000080L);
static constexpr auto CPUF_SUPPORTS_SSE3		= (0x00000100L);
static constexpr auto CPUF_SUPPORTS_SSSE3		= (0x00000200L);
static constexpr auto CPUF_SUPPORTS_SSE41		= (0x00000400L);
static constexpr auto CPUF_SUPPORTS_AVX			= (0x00000800L);
static constexpr auto CPUF_SUPPORTS_AVX2		= (0x00001000L);
static constexpr auto CPUF_SUPPORTS_SHA			= (0x00002000L);
static constexpr auto CPUF_SUPPORTS_CLMUL		= (0x00004000L);	// CLMUL - carryless multiply; also implies SSE4.1/2
static constexpr auto CPUF_SUPPORTS_LZCNT		= (0x00008000L);	// LZCNT - lzcnt instruction; Intel Haswell+, AMD K10+
static constexpr auto CPUF_SUPPORTS_MASK		= (0x0000FFFFL);
#else
static constexpr auto VDCPUF_SUPPORTS_CRYPTO	= (0x00000001L);
static constexpr auto VDCPUF_SUPPORTS_MASK		= (0x00000001L);
#endif

long CPUCheckForExtensions();
long CPUEnableExtensions(long lEnableFlags);

inline long CPUGetEnabledExtensions() {
	extern long g_lCPUExtensionsEnabled;
	return g_lCPUExtensionsEnabled;
}

void VDCPUCleanupExtensions();

#if VD_CPU_X86 || VD_CPU_X64
extern "C" bool FPU_enabled, MMX_enabled, SSE_enabled, ISSE_enabled, SSE2_enabled;
#endif

#endif
