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

void __declspec(naked) __cdecl ATArtifactNTSCAccum_SSE2(void *rout, const void *table, const void *src, uint32 count) {
	static const __declspec(align(16)) uint64 kBiasedZero[2] = { 0x4000400040004000ull, 0x4000400040004000ull };

	__asm {
		push	ebp
		push	edi
		push	esi
		push	ebx

		mov		edx, [esp+4+16]		;dst
		mov		esi, [esp+8+16]		;table
		mov		ebx, [esp+12+16]	;src
		mov		ebp, [esp+16+16]	;count

		movdqa	xmm7, xmmword ptr kBiasedZero
		movdqa	xmm0, xmm7
		movdqa	xmm1, xmm7
		movdqa	xmm2, xmm7
		movdqa	xmm3, xmm7

		add		esi, 80h

		align	16
xloop:
		movzx	eax, byte ptr [ebx]
		shl		eax, 8
		add		eax, esi

		paddw	xmm0, [eax-80h]
		paddw	xmm1, [eax-70h]
		paddw	xmm2, [eax-60h]
		movdqa	xmm3, [eax-50h]

		movzx	eax, byte ptr [ebx+1]
		shl		eax, 8
		add		eax, esi

		paddw	xmm0, [eax-40h]
		paddw	xmm1, [eax-30h]
		paddw	xmm2, [eax-20h]
		paddw	xmm3, [eax-10h]

		movzx	eax, byte ptr [ebx+2]
		shl		eax, 8
		add		eax, esi

		paddw	xmm0, [eax]
		paddw	xmm1, [eax+10h]
		paddw	xmm2, [eax+20h]
		paddw	xmm3, [eax+30h]

		movzx	eax, byte ptr [ebx+3]
		add		ebx, 4
		shl		eax, 8
		add		eax, esi

		paddw	xmm0, [eax+40h]
		paddw	xmm1, [eax+50h]
		paddw	xmm2, [eax+60h]
		paddw	xmm3, [eax+70h]

		movdqa	[edx], xmm0
		movdqa	xmm0, xmm1
		movdqa	xmm1, xmm2
		movdqa	xmm2, xmm3

		add		edx, 16
		sub		ebp, 4
		jnz		xloop

		movdqa	[edx], xmm0
		movdqa	[edx+10h], xmm1
		movdqa	[edx+20h], xmm2

		pop		ebx
		pop		esi
		pop		edi
		pop		ebp
		ret
	}
}

void __declspec(naked) __cdecl ATArtifactNTSCAccumTwin_SSE2(void *rout, const void *table, const void *src, uint32 count) {
	static const __declspec(align(16)) uint64 kBiasedZero[2] = { 0x4000400040004000ull, 0x4000400040004000ull };

	__asm {
		push	ebp
		push	edi
		push	esi
		push	ebx

		mov		edx, [esp+4+16]		;dst
		mov		esi, [esp+8+16]		;table
		mov		ebx, [esp+12+16]	;src
		mov		ebp, [esp+16+16]	;count

		movdqa	xmm7, xmmword ptr kBiasedZero
		movdqa	xmm0, xmm7
		movdqa	xmm1, xmm7
		movdqa	xmm2, xmm7
		movdqa	xmm3, xmm7

		align	16
xloop:
		movzx	eax, byte ptr [ebx]
		shl		eax, 7
		add		eax, esi

		paddw	xmm0, [eax]
		paddw	xmm1, [eax+10h]
		paddw	xmm2, [eax+20h]
		movdqa	xmm3, [eax+30h]

		movzx	eax, byte ptr [ebx+2]
		add		ebx, 4
		shl		eax, 7
		add		eax, esi

		paddw	xmm0, [eax+40h]
		paddw	xmm1, [eax+50h]
		movdqa	[edx], xmm0
		paddw	xmm2, [eax+60h]
		movdqa	xmm0, xmm1
		paddw	xmm3, [eax+70h]
		movdqa	xmm1, xmm2

		movdqa	xmm2, xmm3
		add		edx, 16
		sub		ebp, 4
		jnz		xloop

		movdqa	[edx], xmm0
		movdqa	[edx+10h], xmm1
		movdqa	[edx+20h], xmm2

		pop		ebx
		pop		esi
		pop		edi
		pop		ebp
		ret
	}
}

#endif
