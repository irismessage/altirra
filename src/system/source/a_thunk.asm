		.686
		.model		flat
		.code
		
		align		16

_VDMethodToFunctionThunk32 proc
		pop			eax					;get return address in thunk
		
		;re-copy arguments
		movzx		ecx, byte ptr [eax+1]
		mov			edx, ecx
argsloop:
		push		dword ptr [esp+edx]
		sub			ecx, 4
		jnz			argsloop

		push		eax					;replace thunk return address
		
		mov			ecx, [eax+7]		;load 'this' pointer
		jmp			dword ptr [eax+3]	;tail-call function
_VDMethodToFunctionThunk32 endp

		align		16
_VDMethodToFunctionThunk32_4 proc
		pop			eax					;get return address in thunk
		push		dword ptr [esp+4]	;replicate 1st argument
		push		eax					;replace thunk return address
		mov			ecx, [eax+7]		;load 'this' pointer
		jmp			dword ptr [eax+3]	;tail-call function
_VDMethodToFunctionThunk32_4 endp

		align		16
_VDMethodToFunctionThunk32_8 proc
		pop			eax					;get return address in thunk
		push		dword ptr [esp+8]	;replicate 2nd argument
		push		dword ptr [esp+8]	;replicate 1st argument
		push		eax					;replace thunk return address
		mov			ecx, [eax+7]		;load 'this' pointer
		jmp			dword ptr [eax+3]	;tail-call function
_VDMethodToFunctionThunk32_8 endp

		align		16
_VDMethodToFunctionThunk32_12 proc
		pop			eax					;get return address in thunk
		push		dword ptr [esp+12]	;replicate 3rd argument
		push		dword ptr [esp+12]	;replicate 2nd argument
		push		dword ptr [esp+12]	;replicate 1st argument
		push		eax					;replace thunk return address
		mov			ecx, [eax+7]		;load 'this' pointer
		jmp			dword ptr [eax+3]	;tail-call function
_VDMethodToFunctionThunk32_12 endp

		align		16
_VDMethodToFunctionThunk32_16 proc
		pop			eax					;get return address in thunk
		push		dword ptr [esp+16]	;replicate 4th argument
		push		dword ptr [esp+16]	;replicate 3rd argument
		push		dword ptr [esp+16]	;replicate 2nd argument
		push		dword ptr [esp+16]	;replicate 1st argument
		push		eax					;replace thunk return address
		mov			ecx, [eax+7]		;load 'this' pointer
		jmp			dword ptr [eax+3]	;tail-call function
_VDMethodToFunctionThunk32_16 endp

		end
