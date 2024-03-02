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
	__asm {
		push	ebp
		push	edi
		push	esi
		push	ebx

		mov		edx, [esp+4+16]		;dst
		mov		esi, [esp+8+16]		;table
		mov		ebx, [esp+12+16]	;src
		mov		ebp, [esp+16+16]	;count

		pxor	xmm0, xmm0
		pxor	xmm1, xmm1
		pxor	xmm2, xmm2

		add		esi, 80h

		align	16
xloop1:
		mov		ecx, dword ptr [ebx]
		mov		eax, ecx
		rol		eax, 8
		cmp		eax, ecx
		jz		fast_path

slow_path:
		movzx	eax, cl
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
		jnz		xloop1

xit:
		movdqa	[edx], xmm0
		movdqa	[edx+10h], xmm1
		movdqa	[edx+20h], xmm2

		pop		ebx
		pop		esi
		pop		edi
		pop		ebp
		ret

		align	16
fast_path_reload:
		mov		ecx, eax
		rol		eax, 8
		cmp		eax, ecx
		jnz		slow_path

fast_path:
		movzx	eax, cl
		shl		eax, 6
		add		eax, esi

		movdqa	xmm4, [eax-80h+18000h]
		movdqa	xmm5, [eax-70h+18000h]
		movdqa	xmm6, [eax-60h+18000h]
		movdqa	xmm7, [eax-50h+18000h]
		jmp		short fast_accum

		align	16
xloop2:
		mov		eax, dword ptr [ebx]
		cmp		eax, ecx
		jnz		fast_path_reload

fast_accum:
		add		ebx, 4
		paddw	xmm0, xmm4
		paddw	xmm1, xmm5
		paddw	xmm2, xmm6

		movdqa	[edx], xmm0
		movdqa	xmm0, xmm1
		movdqa	xmm1, xmm2
		movdqa	xmm2, xmm7

		add		edx, 16
		sub		ebp, 4
		jnz		xloop2
		jmp		xit
	}
}

void __declspec(naked) __cdecl ATArtifactNTSCAccumTwin_SSE2(void *rout, const void *table, const void *src, uint32 count) {
	__asm {
		push	ebp
		push	edi
		push	esi
		push	ebx

		mov		edx, [esp+4+16]		;dst
		mov		esi, [esp+8+16]		;table
		mov		ebx, [esp+12+16]	;src
		mov		ebp, [esp+16+16]	;count

		pxor	xmm0, xmm0
		pxor	xmm1, xmm1
		pxor	xmm2, xmm2

		align	16
xloop:
		movzx	eax, byte ptr [ebx]
		movzx	ecx, byte ptr [ebx+2]
		add		ebx, 4
		cmp		eax, ecx
		jz		fast_path

slow_path:
		shl		eax, 7
		shl		ecx, 7
		add		eax, esi
		add		ecx, esi

		paddw	xmm0, [eax]
		paddw	xmm1, [eax+10h]
		paddw	xmm2, [eax+20h]
		movdqa	xmm3, [eax+30h]

		paddw	xmm0, [ecx+40h]
		paddw	xmm1, [ecx+50h]
		movdqa	[edx], xmm0
		paddw	xmm2, [ecx+60h]
		movdqa	xmm0, xmm1
		paddw	xmm3, [ecx+70h]
		movdqa	xmm1, xmm2

		movdqa	xmm2, xmm3
		add		edx, 16
		sub		ebp, 4
		jnz		xloop

xit:
		movdqa	[edx], xmm0
		movdqa	[edx+10h], xmm1
		movdqa	[edx+20h], xmm2

		pop		ebx
		pop		esi
		pop		edi
		pop		ebp
		ret

		align	16
fast_path_reload:
		movzx	eax, byte ptr [ebx]
		movzx	ecx, byte ptr [ebx+2]
		add		ebx, 4
		cmp		eax, ecx
		jnz		slow_path

fast_path:
		mov		ecx, [ebx-4]
		shl		eax, 6
		add		eax, esi
		movdqa	xmm4, [eax+8000h]
		movdqa	xmm5, [eax+8010h]
		movdqa	xmm6, [eax+8020h]
		movdqa	xmm7, [eax+8030h]
		jmp		short fast_accum

		align	16
xloop2:
		cmp		ecx, dword ptr [ebx]
		jnz		fast_path_reload
		add		ebx, 4
fast_accum:
		paddw	xmm0, xmm4
		paddw	xmm1, xmm5
		movdqa	[edx], xmm0
		movdqa	xmm0, xmm1
		paddw	xmm2, xmm6
		movdqa	xmm1, xmm2
		movdqa	xmm2, xmm7

		add		edx, 16
		sub		ebp, 4
		jnz		xloop2
		jmp		xit
	}
}

#endif
