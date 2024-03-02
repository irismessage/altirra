		section		.rdata
		align		64
window_table:
		dq			0, 0, -1, -1, 0, 0
lowbit_mask:
		dq			0f0f0f0f0f0f0f0fh,0f0f0f0f0f0f0f0fh

		section		.code

;==========================================================================
;
; Inputs:
;	void *dst,
;	const uint8 *src,
;	uint32 n
;
		global		_atasm_update_playfield_160_sse2
_atasm_update_playfield_160_sse2:
		push		ebp
		push		edi
		push		esi
		push		ebx

		mov			eax, [esp+16+16]
		mov			ecx, [esp+8+16]
		mov			edi, [esp+4+16]
		movdqa		xmm3, oword [lowbit_mask]

		;check if n==0
		mov			edx, [esp+12+16]
		or			edx, edx
		jz			.xit

		;compute srcEnd = src + n
		mov			ebx, ecx
		add			ebx, edx
		mov			[esp+12+16], ebx
		
		;check if we have a start offset
		mov			edx, ecx
		and			edx, 15
		jz			.xstart
		
		;remove start offset
		sub			edi, edx
		sub			ecx, edx
		sub			edi, edx
		
		;check if we have overlapping start and stop masks
		mov			esi, eax
		xor			esi, edx
		and			esi, 0fffffff0h
		jz			.dosingle

		;process start section
		xor			edx, 15
		inc			edx
		call		.domask
		
.xstart:
		mov			eax, ebx
		sub			eax, ecx
		sub			eax, 16
		js			.endcheck
.xloop:
		movdqa		xmm0, [ecx]
		add			ecx, 16
		movdqa		xmm1, xmm0
		psrlq		xmm0, 4
		movdqa		xmm2, xmm0
		punpcklbw	xmm0, xmm1
		punpckhbw	xmm2, xmm1
		pand		xmm0, xmm3
		pand		xmm2, xmm3
		movdqa		[edi], xmm0
		movdqa		[edi+16], xmm2
		add			edi, 32
		sub			eax, 16
		jns			.xloop
.endcheck:
		and			eax, 15
		jz			.xit
		
		mov			edx, 32
		sub			edx, eax
		call		.domask
		
.xit:
		sfence
		pop			ebx
		pop			esi
		pop			edi
		pop			ebp
		ret

.dosingle:
		xor			edx, 15
		movq		xmm4, qword [window_table+edx+1]
		movq		xmm5, qword [window_table+edx+9]
		movq		xmm0, qword [window_table+edx+17]
		movq		xmm1, qword [window_table+edx+25]
		pand		xmm4, xmm0
		pand		xmm5, xmm1
		call		.domask2
		jmp			short .xit

.domask:
		movq		xmm4, qword [window_table+edx]
		movq		xmm5, qword [window_table+edx+8]
.domask2:
		punpcklbw	xmm4, xmm4
		punpcklbw	xmm5, xmm5
		movdqa		xmm0, [ecx]
		add			ecx, 16
		movdqa		xmm1, xmm0
		psrlq		xmm0, 4
		movdqa		xmm2, xmm0
		punpcklbw	xmm0, xmm1
		punpckhbw	xmm2, xmm1
		pand		xmm0, xmm3
		pand		xmm2, xmm3
		movdqa		xmm6, [edi]
		movdqa		xmm7, [edi+16]
		maskmovdqu	xmm0, xmm4
		add			edi, 16
		maskmovdqu	xmm2, xmm5
		add			edi, 16
		ret

		end
