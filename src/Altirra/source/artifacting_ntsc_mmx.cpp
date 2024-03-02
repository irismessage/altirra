//	Altirra - Atari 800/800XL/5200 emulator
//	PAL artifacting acceleration - x86 SSE2
//	Copyright (C) 2009-2011 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>

#if defined(VD_COMPILER_MSVC) && defined(VD_CPU_X86)

void __declspec(naked) __cdecl ATArtifactNTSCAccum_MMX(void *rout, const void *table, const void *src, uint32 count) {
	static const __declspec(align(8)) uint64 kBiasedZero = 0x4000400040004000ull;

	__asm {
		push	ebp
		push	edi
		push	esi
		push	ebx

		mov		edx, [esp+4+16]		;dst
		mov		esi, [esp+8+16]		;table
		mov		ebx, [esp+12+16]	;src
		mov		ebp, [esp+16+16]	;count

		movq	mm7, kBiasedZero
		movq	mm0, mm7
		movq	mm1, mm7
		movq	mm2, mm7
		movq	mm3, mm7

		align	16
xloop:
		movzx	eax, byte ptr [ebx]
		imul	ecx, eax, 60h
		add		ecx, esi

		paddw	mm0, [ecx]
		paddw	mm1, [ecx+8]
		paddw	mm2, [ecx+16]
		paddw	mm3, [ecx+24]
		movq	mm4, [ecx+32]

		movzx	eax, byte ptr [ebx+1]
		add		ebx, 2
		imul	ecx, eax, 60h
		add		ecx, esi

		paddw	mm0, [ecx+48]
		movq	[edx], mm0
		paddw	mm1, [ecx+56]
		paddw	mm2, [ecx+64]
		movq	mm0, mm1
		paddw	mm3, [ecx+72]
		movq	mm1, mm2
		paddw	mm4, [ecx+80]
		movq	mm2, mm3

		movq	mm3, mm4

		add		edx, 8
		sub		ebp, 2
		jnz		xloop

		movq	[edx], mm0
		movq	[edx+8], mm1
		movq	[edx+16], mm2
		movq	[edx+24], mm3

		emms
		pop		ebx
		pop		esi
		pop		edi
		pop		ebp
		ret
	}
}

void __declspec(naked) __cdecl ATArtifactNTSCAccumTwin_MMX(void *rout, const void *table, const void *src, uint32 count) {
	static const __declspec(align(8)) uint64 kBiasedZero = 0x4000400040004000ull;

	__asm {
		push	ebp
		push	edi
		push	esi
		push	ebx

		mov		edx, [esp+4+16]		;dst
		mov		esi, [esp+8+16]		;table
		mov		ebx, [esp+12+16]	;src
		mov		ebp, [esp+16+16]	;count

		movq	mm7, kBiasedZero
		movq	mm0, mm7
		movq	mm1, mm7
		movq	mm2, mm7
		movq	mm3, mm7

		align	16
xloop:
		movzx	eax, byte ptr [ebx]
		add		ebx, 2
		imul	ecx, eax, 30h
		add		ecx, esi

		paddw	mm0, [ecx]
		paddw	mm1, [ecx+8]
		movq	[edx], mm0
		paddw	mm2, [ecx+16]
		movq	mm0, mm1
		paddw	mm3, [ecx+24]
		movq	mm1, mm2
		movq	mm4, [ecx+32]
		movq	mm2, mm3

		movq	mm3, mm4

		add		edx, 8
		sub		ebp, 2
		jnz		xloop

		movq	[edx], mm0
		movq	[edx+8], mm1
		movq	[edx+16], mm2
		movq	[edx+24], mm3

		emms
		pop		ebx
		pop		esi
		pop		edi
		pop		ebp
		ret
	}
}

void __declspec(naked) __cdecl ATArtifactNTSCFinal_MMX(void *dst, const void *srcr, const void *srcg, const void *srcb, uint32 count) {
	static const uint64 k3ff03ff03ff03ff0 = 0x3ff03ff03ff03ff0ull;
	__asm {
		push	ebp
		push	edi
		push	esi
		push	ebx

		mov		edi, [esp+4+16]		;dst
		mov		ebx, [esp+8+16]		;srcr
		mov		ecx, [esp+12+16]	;srcg
		mov		edx, [esp+16+16]	;srcb
		mov		esi, [esp+20+16]	;count

		movq	mm7, qword ptr k3ff03ff03ff03ff0
xloop:
		movq	mm0, [ebx]		;red
		add		ebx, 8
		movq	mm1, [ecx]		;green
		add		ecx, 8
		movq	mm2, [edx]		;blue
		add		edx, 8

		psubw	mm0, mm7
		psubw	mm1, mm7
		psubw	mm2, mm7
		psraw	mm0, 5
		psraw	mm1, 5
		psraw	mm2, 5
		packuswb	mm0, mm0
		packuswb	mm1, mm1
		packuswb	mm2, mm2

		punpcklbw	mm2, mm1	;gb
		punpcklbw	mm0, mm1	;gr
		movq	mm1, mm2
		punpcklwd	mm1, mm0	;grgb
		punpckhwd	mm2, mm0
		movq	[edi], mm1
		movq	[edi+8], mm2
		add		edi, 16

		sub		esi, 2
		jnz		xloop

		emms
		pop		ebx
		pop		esi
		pop		edi
		pop		ebp
		ret
	}
}

#endif
