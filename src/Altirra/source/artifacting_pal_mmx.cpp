//	Altirra - Atari 800/800XL/5200 emulator
//	PAL artifacting acceleration - x86 MMX
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

#ifdef VD_COMPILER_MSVC
	#pragma warning(disable: 4733)	// warning C4733: Inline asm assigning to 'FS:0' : handler not registered as safe handler
#endif

#ifdef VD_CPU_X86

void __declspec(naked) __stdcall ATArtifactPALLuma_MMX(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels) {
	__asm {
		push	ebp
		push	edi
		push	esi
		push	ebx

		push	0
		push	fs:dword ptr [0]
		mov		fs:dword ptr [0], esp

		mov		esi, [esp+12+24]
		shr		esi, 3
		mov		edi, [esp+16+24]
		mov		ebp, [esp+4+24]
		mov		esp, [esp+8+24]
		pxor	mm0, mm0
		pxor	mm1, mm1
xloop:
		movzx	eax, byte ptr [esp]
		movzx	ebx, byte ptr [esp+1]
		movzx	ecx, byte ptr [esp+2]
		movzx	edx, byte ptr [esp+3]

		shl		eax, 6
		shl		ebx, 6
		shl		ecx, 6
		shl		edx, 6

		lea		eax, [eax+eax*2]
		lea		ebx, [ebx+ebx*2 + 24]
		lea		ecx, [ecx+ecx*2 + 24*2]
		lea		edx, [edx+edx*2 + 24*3]

		paddw	mm0, [edi+eax]
		paddw	mm1, [edi+eax+8]
		movq	mm2, [edi+eax+16]
		paddw	mm0, [edi+ebx]
		paddw	mm1, [edi+ebx+8]
		paddw	mm2, [edi+ebx+16]
		movq	[ebp], mm0

		paddw	mm1, [edi+ecx]
		paddw	mm2, [edi+ecx+8]
		movq	mm3, [edi+ecx+16]
		paddw	mm1, [edi+edx]
		paddw	mm2, [edi+edx+8]
		paddw	mm3, [edi+edx+16]
		movq	[ebp+8], mm1

		movzx	eax, byte ptr [esp+4]
		movzx	ebx, byte ptr [esp+5]
		movzx	ecx, byte ptr [esp+6]
		movzx	edx, byte ptr [esp+7]
		add		esp, 8
		shl		eax, 6
		shl		ebx, 6
		shl		ecx, 6
		shl		edx, 6

		lea		eax, [eax+eax*2 + 24*4]
		lea		ebx, [ebx+ebx*2 + 24*5]
		lea		ecx, [ecx+ecx*2 + 24*6]
		lea		edx, [edx+edx*2 + 24*7]

		paddw	mm2, [edi+eax]
		paddw	mm3, [edi+eax+8]
		movq	mm0, [edi+eax+16]
		paddw	mm2, [edi+ebx]
		paddw	mm3, [edi+ebx+8]
		paddw	mm0, [edi+ebx+16]
		movq	[ebp+16], mm2

		paddw	mm3, [edi+ecx]
		paddw	mm0, [edi+ecx+8]
		movq	mm1, [edi+ecx+16]
		paddw	mm3, [edi+edx]
		paddw	mm0, [edi+edx+8]
		paddw	mm1, [edi+edx+16]

		movq	[ebp+24], mm3
		add		ebp, 32

		dec		esi
		jne		xloop

		movq	[ebp], mm0
		movq	[ebp+8], mm1

		mov		esp, fs:dword ptr [0]
		pop		eax
		pop		ecx

		emms
		pop		ebx
		pop		esi
		pop		edi
		pop		ebp
		ret		16
	}
}

void __declspec(naked) __stdcall ATArtifactPALLumaTwin_MMX(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels) {
	__asm {
		push	ebp
		push	edi
		push	esi
		push	ebx

		push	0
		push	fs:dword ptr [0]
		mov		fs:dword ptr [0], esp

		mov		esi, [esp+12+24]
		shr		esi, 3
		mov		edi, [esp+16+24]
		mov		ebp, [esp+4+24]
		mov		esp, [esp+8+24]
		pxor	mm0, mm0
		pxor	mm1, mm1
xloop:
		movzx	eax, byte ptr [esp]
		movzx	ecx, byte ptr [esp+2]

		shl		eax, 5
		shl		ecx, 5

		lea		eax, [eax+eax*2]
		lea		ecx, [ecx+ecx*2 + 24*1]

		paddw	mm0, [edi+eax]
		paddw	mm1, [edi+eax+8]
		movq	mm2, [edi+eax+16]
		movq	[ebp], mm0

		paddw	mm1, [edi+ecx]
		paddw	mm2, [edi+ecx+8]
		movq	mm3, [edi+ecx+16]
		movq	[ebp+8], mm1

		movzx	eax, byte ptr [esp+4]
		movzx	ecx, byte ptr [esp+6]
		add		esp, 8
		shl		eax, 5
		shl		ecx, 5

		lea		eax, [eax+eax*2 + 24*2]
		lea		ecx, [ecx+ecx*2 + 24*3]

		paddw	mm2, [edi+eax]
		paddw	mm3, [edi+eax+8]
		movq	mm0, [edi+eax+16]
		movq	[ebp+16], mm2

		paddw	mm3, [edi+ecx]
		paddw	mm0, [edi+ecx+8]
		movq	mm1, [edi+ecx+16]

		movq	[ebp+24], mm3
		add		ebp, 32

		dec		esi
		jne		xloop

		movq	[ebp], mm0
		movq	[ebp+8], mm1

		mov		esp, fs:dword ptr [0]
		pop		eax
		pop		ecx

		emms
		pop		ebx
		pop		esi
		pop		edi
		pop		ebp
		ret		16
	}
}

void __declspec(naked) __stdcall ATArtifactPALChroma_MMX(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels) {
	__asm {
		push	ebp
		push	edi
		push	esi
		push	ebx

		push	0
		push	fs:dword ptr [0]
		mov		fs:dword ptr [0], esp

		mov		esi, [esp+12+24]
		mov		edi, [esp+16+24]
		mov		ebp, [esp+4+24]
		mov		esp, [esp+8+24]
		pxor	mm0, mm0
		pxor	mm1, mm1
		pxor	mm2, mm2
		pxor	mm3, mm3
		pxor	mm4, mm4
		pxor	mm5, mm5
		jmp		entry

		align	16
xloop:
		movzx	eax, byte ptr [esp]
		movzx	ebx, byte ptr [esp+1]
		movzx	ecx, byte ptr [esp+2]
		movzx	edx, byte ptr [esp+3]
		shl		eax, 9
		shl		ebx, 9
		shl		ecx, 9
		shl		edx, 9
		paddw	mm0, [edi+eax+0*64]
		paddw	mm1, [edi+eax+0*64+8]
		paddw	mm2, [edi+eax+0*64+16]
		paddw	mm3, [edi+eax+0*64+24]
		paddw	mm4, [edi+eax+0*64+32]
		paddw	mm5, [edi+eax+0*64+40]
		movq	mm6, [edi+eax+0*64+48]
		paddw	mm0, [edi+ebx+1*64]
		paddw	mm1, [edi+ebx+1*64+8]
		paddw	mm2, [edi+ebx+1*64+16]
		paddw	mm3, [edi+ebx+1*64+24]
		paddw	mm4, [edi+ebx+1*64+32]
		paddw	mm5, [edi+ebx+1*64+40]
		paddw	mm6, [edi+ebx+1*64+48]
		movq	[ebp], mm0
		paddw	mm1, [edi+ecx+2*64]
		paddw	mm2, [edi+ecx+2*64+8]
		paddw	mm3, [edi+ecx+2*64+16]
		paddw	mm4, [edi+ecx+2*64+24]
		paddw	mm5, [edi+ecx+2*64+32]
		paddw	mm6, [edi+ecx+2*64+40]
		movq	mm7, [edi+ecx+2*64+48]
		paddw	mm1, [edi+edx+3*64]
		paddw	mm2, [edi+edx+3*64+8]
		paddw	mm3, [edi+edx+3*64+16]
		paddw	mm4, [edi+edx+3*64+24]
		paddw	mm5, [edi+edx+3*64+32]
		paddw	mm6, [edi+edx+3*64+40]
		paddw	mm7, [edi+edx+3*64+48]
		movq	[ebp+8], mm1

		movzx	eax, byte ptr [esp+4]
		movzx	ebx, byte ptr [esp+5]
		movzx	ecx, byte ptr [esp+6]
		movzx	edx, byte ptr [esp+7]
		shl		eax, 9
		shl		ebx, 9
		shl		ecx, 9
		shl		edx, 9
		paddw	mm2, [edi+eax+4*64]
		paddw	mm3, [edi+eax+4*64+8]
		paddw	mm4, [edi+eax+4*64+16]
		paddw	mm5, [edi+eax+4*64+24]
		paddw	mm6, [edi+eax+4*64+32]
		paddw	mm7, [edi+eax+4*64+40]
		movq	mm0, [edi+eax+4*64+48]
		paddw	mm2, [edi+ebx+5*64]
		paddw	mm3, [edi+ebx+5*64+8]
		paddw	mm4, [edi+ebx+5*64+16]
		paddw	mm5, [edi+ebx+5*64+24]
		paddw	mm6, [edi+ebx+5*64+32]
		paddw	mm7, [edi+ebx+5*64+40]
		paddw	mm0, [edi+ebx+5*64+48]
		movq	[ebp+16], mm2
		paddw	mm3, [edi+ecx+6*64]
		paddw	mm4, [edi+ecx+6*64+8]
		paddw	mm5, [edi+ecx+6*64+16]
		paddw	mm6, [edi+ecx+6*64+24]
		paddw	mm7, [edi+ecx+6*64+32]
		paddw	mm0, [edi+ecx+6*64+40]
		movq	mm1, [edi+ecx+6*64+48]
		paddw	mm3, [edi+edx+7*64]
		paddw	mm4, [edi+edx+7*64+8]
		paddw	mm5, [edi+edx+7*64+16]
		paddw	mm6, [edi+edx+7*64+24]
		paddw	mm7, [edi+edx+7*64+32]
		paddw	mm0, [edi+edx+7*64+40]
		paddw	mm1, [edi+edx+7*64+48]
		movq	[ebp+24], mm3

		movzx	eax, byte ptr [esp+8]
		movzx	ebx, byte ptr [esp+9]
		movzx	ecx, byte ptr [esp+10]
		movzx	edx, byte ptr [esp+11]
		shl		eax, 9
		shl		ebx, 9
		shl		ecx, 9
		shl		edx, 9
		paddw	mm4, [edi+eax+0*64]
		paddw	mm5, [edi+eax+0*64+8]
		paddw	mm6, [edi+eax+0*64+16]
		paddw	mm7, [edi+eax+0*64+24]
		paddw	mm0, [edi+eax+0*64+32]
		paddw	mm1, [edi+eax+0*64+40]
		movq	mm2, [edi+eax+0*64+48]
		paddw	mm4, [edi+ebx+1*64]
		paddw	mm5, [edi+ebx+1*64+8]
		paddw	mm6, [edi+ebx+1*64+16]
		paddw	mm7, [edi+ebx+1*64+24]
		paddw	mm0, [edi+ebx+1*64+32]
		paddw	mm1, [edi+ebx+1*64+40]
		paddw	mm2, [edi+ebx+1*64+48]
		movq	[ebp+32], mm4
		paddw	mm5, [edi+ecx+2*64]
		paddw	mm6, [edi+ecx+2*64+8]
		paddw	mm7, [edi+ecx+2*64+16]
		paddw	mm0, [edi+ecx+2*64+24]
		paddw	mm1, [edi+ecx+2*64+32]
		paddw	mm2, [edi+ecx+2*64+40]
		movq	mm3, [edi+ecx+2*64+48]
		paddw	mm5, [edi+edx+3*64]
		paddw	mm6, [edi+edx+3*64+8]
		paddw	mm7, [edi+edx+3*64+16]
		paddw	mm0, [edi+edx+3*64+24]
		paddw	mm1, [edi+edx+3*64+32]
		paddw	mm2, [edi+edx+3*64+40]
		paddw	mm3, [edi+edx+3*64+48]
		movq	[ebp+40], mm5

		movzx	eax, byte ptr [esp+12]
		movzx	ebx, byte ptr [esp+13]
		movzx	ecx, byte ptr [esp+14]
		movzx	edx, byte ptr [esp+15]
		shl		eax, 9
		add		esp, 16
		shl		ebx, 9
		shl		ecx, 9
		shl		edx, 9
		paddw	mm6, [edi+eax+4*64]
		paddw	mm7, [edi+eax+4*64+8]
		paddw	mm0, [edi+eax+4*64+16]
		paddw	mm1, [edi+eax+4*64+24]
		paddw	mm2, [edi+eax+4*64+32]
		paddw	mm3, [edi+eax+4*64+40]
		movq	mm4, [edi+eax+4*64+48]
		paddw	mm6, [edi+ebx+5*64]
		paddw	mm7, [edi+ebx+5*64+8]
		paddw	mm0, [edi+ebx+5*64+16]
		paddw	mm1, [edi+ebx+5*64+24]
		paddw	mm2, [edi+ebx+5*64+32]
		paddw	mm3, [edi+ebx+5*64+40]
		paddw	mm4, [edi+ebx+5*64+48]
		movq	[ebp+48], mm6
		paddw	mm7, [edi+ecx+6*64]
		paddw	mm0, [edi+ecx+6*64+8]
		paddw	mm1, [edi+ecx+6*64+16]
		paddw	mm2, [edi+ecx+6*64+24]
		paddw	mm3, [edi+ecx+6*64+32]
		paddw	mm4, [edi+ecx+6*64+40]
		movq	mm5, [edi+ecx+6*64+48]
		paddw	mm7, [edi+edx+7*64]
		paddw	mm0, [edi+edx+7*64+8]
		paddw	mm1, [edi+edx+7*64+16]
		paddw	mm2, [edi+edx+7*64+24]
		paddw	mm3, [edi+edx+7*64+32]
		paddw	mm4, [edi+edx+7*64+40]
		paddw	mm5, [edi+edx+7*64+48]
		movq	[ebp+56], mm7
		add		ebp, 64

entry:
		sub		esi, 16
		jns		xloop

		test	esi, 8
		jz		noodd

		movzx	eax, byte ptr [esp]
		movzx	ebx, byte ptr [esp+1]
		movzx	ecx, byte ptr [esp+2]
		movzx	edx, byte ptr [esp+3]
		shl		eax, 9
		shl		ebx, 9
		shl		ecx, 9
		shl		edx, 9
		paddw	mm0, [edi+eax+0*64]
		paddw	mm1, [edi+eax+0*64+8]
		paddw	mm2, [edi+eax+0*64+16]
		paddw	mm3, [edi+eax+0*64+24]
		paddw	mm4, [edi+eax+0*64+32]
		paddw	mm5, [edi+eax+0*64+40]
		movq	mm6, [edi+eax+0*64+48]
		paddw	mm0, [edi+ebx+1*64]
		paddw	mm1, [edi+ebx+1*64+8]
		paddw	mm2, [edi+ebx+1*64+16]
		paddw	mm3, [edi+ebx+1*64+24]
		paddw	mm4, [edi+ebx+1*64+32]
		paddw	mm5, [edi+ebx+1*64+40]
		paddw	mm6, [edi+ebx+1*64+48]
		movq	[ebp], mm0
		paddw	mm1, [edi+ecx+2*64]
		paddw	mm2, [edi+ecx+2*64+8]
		paddw	mm3, [edi+ecx+2*64+16]
		paddw	mm4, [edi+ecx+2*64+24]
		paddw	mm5, [edi+ecx+2*64+32]
		paddw	mm6, [edi+ecx+2*64+40]
		movq	mm7, [edi+ecx+2*64+48]
		paddw	mm1, [edi+edx+3*64]
		paddw	mm2, [edi+edx+3*64+8]
		paddw	mm3, [edi+edx+3*64+16]
		paddw	mm4, [edi+edx+3*64+24]
		paddw	mm5, [edi+edx+3*64+32]
		paddw	mm6, [edi+edx+3*64+40]
		paddw	mm7, [edi+edx+3*64+48]
		movq	[ebp+8], mm1

		movzx	eax, byte ptr [esp+4]
		movzx	ebx, byte ptr [esp+5]
		movzx	ecx, byte ptr [esp+6]
		movzx	edx, byte ptr [esp+7]
		shl		eax, 9
		shl		ebx, 9
		shl		ecx, 9
		shl		edx, 9
		paddw	mm2, [edi+eax+4*64]
		paddw	mm3, [edi+eax+4*64+8]
		paddw	mm4, [edi+eax+4*64+16]
		paddw	mm5, [edi+eax+4*64+24]
		paddw	mm6, [edi+eax+4*64+32]
		paddw	mm7, [edi+eax+4*64+40]
		movq	mm0, [edi+eax+4*64+48]
		paddw	mm2, [edi+ebx+5*64]
		paddw	mm3, [edi+ebx+5*64+8]
		paddw	mm4, [edi+ebx+5*64+16]
		paddw	mm5, [edi+ebx+5*64+24]
		paddw	mm6, [edi+ebx+5*64+32]
		paddw	mm7, [edi+ebx+5*64+40]
		paddw	mm0, [edi+ebx+5*64+48]
		movq	[ebp+16], mm2
		paddw	mm3, [edi+ecx+6*64]
		paddw	mm4, [edi+ecx+6*64+8]
		paddw	mm5, [edi+ecx+6*64+16]
		paddw	mm6, [edi+ecx+6*64+24]
		paddw	mm7, [edi+ecx+6*64+32]
		paddw	mm0, [edi+ecx+6*64+40]
		movq	mm1, [edi+ecx+6*64+48]
		paddw	mm3, [edi+edx+7*64]
		paddw	mm4, [edi+edx+7*64+8]
		paddw	mm5, [edi+edx+7*64+16]
		paddw	mm6, [edi+edx+7*64+24]
		paddw	mm7, [edi+edx+7*64+32]
		paddw	mm0, [edi+edx+7*64+40]
		paddw	mm1, [edi+edx+7*64+48]
		movq	[ebp+24], mm3
		movq	[ebp+32], mm4
		movq	[ebp+40], mm5
		jmp		short xit

noodd:
		movq	[ebp], mm0
		movq	[ebp+8], mm1

xit:
		mov		esp, fs:dword ptr [0]
		pop		eax
		pop		ecx

		emms
		pop		ebx
		pop		esi
		pop		edi
		pop		ebp
		ret		16
	}
}

void __declspec(naked) __stdcall ATArtifactPALChromaTwin_MMX(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels) {
	__asm {
		push	ebp
		push	edi
		push	esi
		push	ebx

		push	0
		push	fs:dword ptr [0]
		mov		fs:dword ptr [0], esp

		mov		esi, [esp+12+24]
		mov		edi, [esp+16+24]
		mov		ebp, [esp+4+24]
		mov		esp, [esp+8+24]
		pxor	mm0, mm0
		pxor	mm1, mm1
		pxor	mm2, mm2
		pxor	mm3, mm3
		pxor	mm4, mm4
		pxor	mm5, mm5
		jmp		entry

		align	16
xloop:
		movzx	eax, byte ptr [esp]
		movzx	ecx, byte ptr [esp+2]
		shl		eax, 8
		shl		ecx, 8
		paddw	mm0, [edi+eax+0*64]
		paddw	mm1, [edi+eax+0*64+8]
		paddw	mm2, [edi+eax+0*64+16]
		paddw	mm3, [edi+eax+0*64+24]
		paddw	mm4, [edi+eax+0*64+32]
		paddw	mm5, [edi+eax+0*64+40]
		movq	mm6, [edi+eax+0*64+48]
		movq	[ebp], mm0
		paddw	mm1, [edi+ecx+1*64]
		paddw	mm2, [edi+ecx+1*64+8]
		paddw	mm3, [edi+ecx+1*64+16]
		paddw	mm4, [edi+ecx+1*64+24]
		paddw	mm5, [edi+ecx+1*64+32]
		paddw	mm6, [edi+ecx+1*64+40]
		movq	mm7, [edi+ecx+1*64+48]
		movq	[ebp+8], mm1

		movzx	eax, byte ptr [esp+4]
		movzx	ecx, byte ptr [esp+6]
		shl		eax, 8
		shl		ecx, 8
		paddw	mm2, [edi+eax+2*64]
		paddw	mm3, [edi+eax+2*64+8]
		paddw	mm4, [edi+eax+2*64+16]
		paddw	mm5, [edi+eax+2*64+24]
		paddw	mm6, [edi+eax+2*64+32]
		paddw	mm7, [edi+eax+2*64+40]
		movq	mm0, [edi+eax+2*64+48]
		movq	[ebp+16], mm2
		paddw	mm3, [edi+ecx+3*64]
		paddw	mm4, [edi+ecx+3*64+8]
		paddw	mm5, [edi+ecx+3*64+16]
		paddw	mm6, [edi+ecx+3*64+24]
		paddw	mm7, [edi+ecx+3*64+32]
		paddw	mm0, [edi+ecx+3*64+40]
		movq	mm1, [edi+ecx+3*64+48]
		movq	[ebp+24], mm3

		movzx	eax, byte ptr [esp+8]
		movzx	ecx, byte ptr [esp+10]
		shl		eax, 8
		shl		ecx, 8
		paddw	mm4, [edi+eax+0*64]
		paddw	mm5, [edi+eax+0*64+8]
		paddw	mm6, [edi+eax+0*64+16]
		paddw	mm7, [edi+eax+0*64+24]
		paddw	mm0, [edi+eax+0*64+32]
		paddw	mm1, [edi+eax+0*64+40]
		movq	mm2, [edi+eax+0*64+48]
		movq	[ebp+32], mm4
		paddw	mm5, [edi+ecx+1*64]
		paddw	mm6, [edi+ecx+1*64+8]
		paddw	mm7, [edi+ecx+1*64+16]
		paddw	mm0, [edi+ecx+1*64+24]
		paddw	mm1, [edi+ecx+1*64+32]
		paddw	mm2, [edi+ecx+1*64+40]
		movq	mm3, [edi+ecx+1*64+48]
		movq	[ebp+40], mm5

		movzx	eax, byte ptr [esp+12]
		movzx	ecx, byte ptr [esp+14]
		shl		eax, 8
		add		esp, 16
		shl		ecx, 8
		paddw	mm6, [edi+eax+2*64]
		paddw	mm7, [edi+eax+2*64+8]
		paddw	mm0, [edi+eax+2*64+16]
		paddw	mm1, [edi+eax+2*64+24]
		paddw	mm2, [edi+eax+2*64+32]
		paddw	mm3, [edi+eax+2*64+40]
		movq	mm4, [edi+eax+2*64+48]
		movq	[ebp+48], mm6
		paddw	mm7, [edi+ecx+3*64]
		paddw	mm0, [edi+ecx+3*64+8]
		paddw	mm1, [edi+ecx+3*64+16]
		paddw	mm2, [edi+ecx+3*64+24]
		paddw	mm3, [edi+ecx+3*64+32]
		paddw	mm4, [edi+ecx+3*64+40]
		movq	mm5, [edi+ecx+3*64+48]
		movq	[ebp+56], mm7
		add		ebp, 64

entry:
		sub		esi, 16
		jns		xloop

		test	esi, 8
		jz		noodd

		movzx	eax, byte ptr [esp]
		movzx	ecx, byte ptr [esp+2]
		shl		eax, 8
		shl		ecx, 8
		paddw	mm0, [edi+eax+0*64]
		paddw	mm1, [edi+eax+0*64+8]
		paddw	mm2, [edi+eax+0*64+16]
		paddw	mm3, [edi+eax+0*64+24]
		paddw	mm4, [edi+eax+0*64+32]
		paddw	mm5, [edi+eax+0*64+40]
		movq	mm6, [edi+eax+0*64+48]
		movq	[ebp], mm0
		paddw	mm1, [edi+ecx+1*64]
		paddw	mm2, [edi+ecx+1*64+8]
		paddw	mm3, [edi+ecx+1*64+16]
		paddw	mm4, [edi+ecx+1*64+24]
		paddw	mm5, [edi+ecx+1*64+32]
		paddw	mm6, [edi+ecx+1*64+40]
		movq	mm7, [edi+ecx+1*64+48]
		movq	[ebp+8], mm1

		movzx	eax, byte ptr [esp+4]
		movzx	ecx, byte ptr [esp+6]
		shl		eax, 8
		shl		ecx, 8
		paddw	mm2, [edi+eax+2*64]
		paddw	mm3, [edi+eax+2*64+8]
		paddw	mm4, [edi+eax+2*64+16]
		paddw	mm5, [edi+eax+2*64+24]
		paddw	mm6, [edi+eax+2*64+32]
		paddw	mm7, [edi+eax+2*64+40]
		movq	mm0, [edi+eax+2*64+48]
		movq	[ebp+16], mm2
		paddw	mm3, [edi+ecx+3*64]
		paddw	mm4, [edi+ecx+3*64+8]
		paddw	mm5, [edi+ecx+3*64+16]
		paddw	mm6, [edi+ecx+3*64+24]
		paddw	mm7, [edi+ecx+3*64+32]
		paddw	mm0, [edi+ecx+3*64+40]
		movq	mm1, [edi+ecx+3*64+48]
		movq	[ebp+24], mm3
		movq	[ebp+32], mm4
		movq	[ebp+40], mm5
		jmp		short xit

noodd:
		movq	[ebp], mm0
		movq	[ebp+8], mm1

xit:
		mov		esp, fs:dword ptr [0]
		pop		eax
		pop		ecx

		emms
		pop		ebx
		pop		esi
		pop		edi
		pop		ebp
		ret		16
	}
}

void __declspec(naked) __stdcall ATArtifactPALFinal_MMX(uint32 *dst, const uint32 *ybuf, const uint32 *ubuf, const uint32 *vbuf, uint32 *ulbuf, uint32 *vlbuf, uint32 n) {
	static const __declspec(align(8)) sint16 kCoeffU[4]={
		-3182*4, -3182*4, -3182*4, -3182*4	// -co_ug / co_ub * 16384 * 4
	};

	static const __declspec(align(8)) sint16 kCoeffV[4]={
		-8346*4+0x10000, -8346*4+0x10000, -8346*4+0x10000, -8346*4+0x10000	// -co_vg / co_vr * 16384 * 4, wrapped around
	};

	__asm {
		push	ebp
		push	edi
		push	esi
		push	ebx

		mov		eax, [esp+12+16]	;ubuf
		mov		ebx, [esp+16+16]	;vbuf
		mov		ecx, [esp+20+16]	;ulbuf
		mov		edx, [esp+24+16]	;vlbuf
		mov		esi, [esp+28+16]	;n
		mov		edi, [esp+4+16]		;dst
		mov		ebp, [esp+8+16]		;ybuf

		shr		esi, 1

		movq	mm6, qword ptr kCoeffU
		movq	mm7, qword ptr kCoeffV
xloop:
		movq	mm0, [ecx]			;read prev U
		movq	mm1, [eax+16]		;read current U
		add		eax, 8
		paddw	mm0, mm1			;add (average) U
		movq	[ecx], mm1			;update prev U
		add		ecx, 8

		movq	mm2, [edx]			;read prev V
		movq	mm3, [ebx+16]		;read current V
		add		ebx, 8
		paddw	mm2, mm3			;add (average) V
		movq	[edx], mm3			;update prev V
		add		edx, 8

		movq	mm4, [ebp]			;read current Y
		add		ebp, 8

		movq	mm1, mm0
		movq	mm3, mm2
		pmulhw	mm0, mm6			;compute U impact on green
		pmulhw	mm2, mm7			;compute V impact on green
		psubsw	mm2, mm3

		paddw	mm0, mm2
		paddw	mm1, mm4			;U + Y = blue
		paddw	mm0, mm4			;green
		paddw	mm3, mm4			;V + Y = red

		psraw	mm0, 6
		psraw	mm1, 6
		psraw	mm3, 6

		packuswb	mm0, mm0
		packuswb	mm1, mm1
		packuswb	mm3, mm3
		punpcklbw	mm1, mm3
		punpcklbw	mm0, mm0
		movq		mm3, mm1
		punpcklbw	mm1, mm0
		punpckhbw	mm3, mm0

		movq	[edi], mm1
		movq	[edi+8], mm3
		add		edi, 16

		dec		esi
		jne		xloop

		emms
		pop		ebx
		pop		esi
		pop		edi
		pop		ebp
		ret		16
	}
}

#endif
