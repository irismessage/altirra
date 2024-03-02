		.code
		
VDMethodToFunctionThunk64	proc frame
		;prolog
		db			48h				;emit REX prefix -- first instruction must be two bytes for hot patching
		push		rbp
		.pushreg	rbp
		
		mov			rbp, rsp		;create stack pointer
		.setframe	rbp, 0
		
		mov			[rbp+16], rcx	;save arg1
		.savereg	rcx, 0
		
		mov			[rbp+24], rdx	;save arg2
		.savereg	rcx, 8

		mov			[rbp+32], r8	;save arg3
		.savereg	rcx, 16

		mov			[rbp+40], r9	;save arg4
		.savereg	rcx, 24
		
		.endprolog
				
		;re-copy arguments 4 and up
		mov			ecx, [rax+24]
		or			ecx, ecx
		jz			argsdone
		lea			rdx, [rcx+32]
argsloop:
		push		qword ptr [rsp+rdx]
		sub			ecx, 8
		jnz			argsloop
argsdone:
		
		;load 'this' pointer
		mov			rcx, [rax+16]
		
		;reload arguments 1-3
		mov			rdx, [rbp+16]
		mov			r8, [rbp+24]
		mov			r9, [rbp+32]
		
		;reserve argument 1-4 space on stack
		sub			rsp, 32
		
		;call function
		call		qword ptr [rax+8]
		
		;epilog
		lea			rsp, [rbp]		;pop off stack frame and any additional arg space
		pop			rbp				;restore base pointer
		ret							;all done
VDMethodToFunctionThunk64	endp

		end
