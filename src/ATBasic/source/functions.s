; Altirra BASIC - Function module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

?functions_start = *

;===========================================================================
.proc expNumConst
		ldx		#<-6
		ldy		exLineOffset
var_loop:
		lda		(stmcur),y+
		sta		fr0+6,x
		inx
		bne		var_loop
		sty		exLineOffset
		jmp		expPushFR0
.endp

;===========================================================================
.proc expStrConst
		;load and stash string literal length (so we don't have to thrash Y)
		ldy		exLineOffset
		lda		(stmcur),y
		tax

		;build argument stack entry
		jsr		ExprBeginPushStringVal
		
		;address
		lda		stmcur
		sec								;+1 to skip length
		adc		exLineOffset
		sta		(argstk),y+
		lda		stmcur+1
		adc		#0
		sta		(argstk),y+
		
		;length
		txa
		jsr		ExprPushRawByteAsWord
		
		;dimensioned length	
		txa
		jsr		ExprPushRawByteAsWord
		
		;skip past length and string in statement text
		txa
		sec
		adc		exLineOffset
		sta		exLineOffset
		
		;all done
		sty		argsp
		rts
.endp


;===========================================================================
.proc expComma
		;leave arguments on stack
		rts
.endp


;===========================================================================
.proc funStringCompare
		lda		funCompare.compare_mode_tab-TOK_EXP_STR_LE,x
		sta		a3

		jsr		compareStrings
		jmp		funCompare.push_flags_as_bool
.endp

;===========================================================================
.proc funCompare
		;save comparison mode
		lda		compare_mode_tab-TOK_EXP_LE,x
		sta		a3
		
		;pop both arguments off
		jsr		expPopFR1FR0

		;do FP comparison
		jsr		fcomp
		
push_flags_as_bool:
		;look up comparison results
		php
		pla
		and		#3
		tax
		lda		cmptab,x
		
		;select the desired relation
		and		a3
		
		;push and exit
		bne		nonzero
		jsr		zfr0
		jmp		expPushFR0
nonzero:
		jsr		fld1
		jmp		expPushFR0
		
cmptab:	;input = (z,c)
		;output = (=, <>, <, >=, >, <=)
		dta		%011001		;fr0 < fr1
		dta		%010110		;fr0 > fr1
		dta		%100101		;fr0 = fr1
		dta		%100101		;fr0 = fr1
		
compare_mode_tab:
		dta		$01,$10,$04,$08,$02,$20
.endp

;===========================================================================
.proc funPower
		;unfortunately, we have to futz with the stack here since the
		;parameters are in the wrong order....
		lda		argsp
		sub		#8
		tay
		jsr		expPopFR0.with_offset
		jsr		log10
		lda		argsp
		pha
		add		#16
		tay
		jsr		expPopFR1.with_offset
		jsr		fmul
		jsr		exp10
		pla
		sta		argsp
		jmp		expPushFR0
.endp

;===========================================================================
.proc funMultiply
		jsr		expPopFR1FR0
		jsr		fmul
		bcs		funDivide.onerr
		jmp		expPushFR0
.endp


;===========================================================================
.proc funAdd
		jsr		expPopFR1FR0
		jsr		fadd
		bcs		funDivide.onerr
		jmp		expPushFR0
.endp


;===========================================================================
.proc funSubtract
		jsr		expPopFR1FR0
		jsr		fsub
		bcs		funDivide.onerr
		jmp		expPushFR0
.endp


;===========================================================================
.proc funDivide
		jsr		expPopFR1FR0
		jsr		fdiv
		bcs		onerr
		jmp		expPushFR0
onerr:
		jmp		errorFPError
.endp


;===========================================================================
.proc funNot
		jsr		expPopFR0
		lda		fr0+1
		beq		is_zero
push_zero:
		jsr		zfr0
		jmp		expPushFR0
is_zero:
push_one:
		jsr		fld1
push_fr0:
		jmp		expPushFR0
.endp

;===========================================================================
.proc funOr
		jsr		expPopFR1FR0
		lda		fr0+1
		ora		fr1+1
		bne		funNot.push_one
		jmp		expPushFR0
.endp

;===========================================================================
.proc funAnd
		jsr		expPopFR1FR0
		lda		fr0+1
		beq		funNot.push_fr0
		lda		fr1+1
		bne		funNot.push_one
		beq		funNot.push_zero
.endp

;===========================================================================
.proc funOpenParens
		;reset comma count
		lda		expCommas
		sta		expFCommas
		lda		#0
		sta		expCommas

		;pop the return address and force next token to be processed --
		;this prevents any further reduction and the close parens from
		;shifting onto the stack.
		pla
		pla
		jmp		evaluate.loop
.endp

;===========================================================================
.proc funAssignNum
_tmpadr = fr1
		;copy number to FR0
		jsr		expPopFR0

		;pop variable from stack
		jsr		expPopVar
		
		;copy FR0 to variable or array element
		;;##TRACE "Assigning %g to element at $%04x" fr0 dw(lvarptr)
		ldy		#5
copy_loop:
		mva		fr0,y (lvarptr),y
		dey
		bpl		copy_loop
		rts
.endp


;===========================================================================
; String assignment
;
; There is a really annoying case we have to deal with here:
;
;	READY
;	DIM A$(10)
;
;	READY
;	A$(5,8)="XY"
;
;	READY
;	PRINT LEN(A$)
;	6
;
; What this means is that the length of the string array is affected by
; both the subscript and the string assigned into it. Amusingly (or
; annoyingly), Atari BASIC also doesn't actually initialize the string in
; this case, resulting in four nulls at the beginning of the string.
;
; The rules for an assignment of length N to A$(X):
;	- Assignment begins at an offset of X-1 in the string buffer.
;	- The copy is truncated at the end of the string buffer.
;	- The string length is set to X-1+N, subject to capacity limits. This
;	  can both raise and lower the length. Basically, the string buffer is
;	  terminated at the end of the copied string.
;	- If the length is raised, no bytes prior to the assign point are
;	  initialized and can be junk (typically hearts or existing data).
;
; The rules for an assignment of length N to A$(X,Y):
;	- Assignment begins at an offset of X-1 in the string buffer.
;	- The copy is truncated at the end of the string buffer.
;	- The copy is truncated at a max length of Y-X+1.
;	- The string length is raised to min(X-1+N, Y). The length is never
;	  lowered. This means that the two-subscript form cannot ever truncate
;	  a string.
;	- If the copy length is shorter than the range, the extra chars in
;	  the buffer are untouched.
;	- If the length is raised, no bytes prior to the assign point are
;	  initialized and can be junk (typically hearts or existing data).
;
.proc funAssignStr
		;pop source string to FR1
		jsr		expPopFR1
		
		;pop dest string to FR0
		jsr		ExprPopExtFR0
		
		;##TRACE "Dest string: $%04x+%u [%u] = [%.*s]" dw(fr0) dw(fr0+2) dw(fr0+4) dw(fr0+2) dw(fr0)
		;##TRACE "Source string: $%04x+%u [%u] = [%.*s]" dw(fr1) dw(fr1+2) dw(fr1+4) dw(fr1+2) dw(fr1)
		
		;check that dest string is dimmed
		lda		prefr0
		lsr
		bcs		is_dimmed
		
		;oops... issue error
		jmp		errorDimError
		
is_dimmed:

		;check if we need to truncate length (len(src) > capacity(dst))
		;;##TRACE "source length %x" dw(fr1+2)
		;;##TRACE "dest capacity %x" dw(fr0+4)
		ldx		fr1+3			;get source length hi
		lda		fr1+2			;get source length lo
		cpx		fr0+5			;compare dest capacity hi
		sne:cmp	fr0+4			;compare dest capacity lo
		bcc		len_ok
		;source string is shorter, so use it
		;;##TRACE "Truncating length"
		ldx		fr0+5
		lda		fr0+4
len_ok:

		;set copy length (a3)
		sta		a3
		stx		a3+1

		;look up source variable
		;;##TRACE "Variable %x" db(prefr0+1)
		lda		prefr0+1
		jsr		VarGetAddr0
		
		;check if we need to alter the source length:
		; - for A$(X)=B$, the length is always set to min(X+len(B$)-1, capacity(A$))
		; - for A$(X,Y)=B$, this only happens if the new length is greater than the existing length
		
		;compute relative offset and add copy length
		;;##TRACE "Var is at %x, dest is at %x, copy len is %x, dest offset is %x" dw(starp)+dw(dw(varptr)+2) dw(fr0) dw(a3) dw(a3)+(dw(dw(varptr)+2)+dw(starp))-dw(fr0)
		sec
		lda		fr0
		sbc		starp
		tax
		lda		fr0+1
		sbc		starp+1
		tay
		
		clc
		txa
		adc		a3
		tax
		tya
		adc		a3+1
		sta		fr1+5
		
		ldy		#2
		sec
		txa
		sbc		(varptr),y
		tax
		iny
		lda		fr1+5
		sbc		(varptr),y
		sta		fr1+5
		
		;check if we are doing A$(X)
		lda		expAsnCtx
		lsr
		bcs		update_length
		
		;check if the new length is longer than the existing length
		;##TRACE "Comparing var length: existing %u, proposed %u" dw(dw(varptr)+4) db(fr1+5)*256+x
		txa
		iny
		cmp		(varptr),y
		iny
		lda		fr1+5
		sbc		(varptr),y
		bcc		no_update_length
		
update_length:
		;##TRACE "Setting var length to %d" db(fr1+5)*256+x
		ldy		#5
		mva		fr1+5 (varptr),y
		dey
		txa
		sta		(varptr),y
no_update_length:

		;copy source address to dest pointer (a1)
		ldx		fr1
		ldy		fr1+1
		stx		a1
		sty		a1+1

		;##TRACE "String assignment: copy ($%04x+%d -> $%04x)" dw(a1) dw(a3) dw(a0)
		;##ASSERT dw(a0) >= dw(starp) and dw(a0)+dw(a3) <= dw(runstk)
		
		;do memcpy and we're done
		jmp		copyAscending
.endp


;===========================================================================
.proc compareStrings
_str0 = fr0
_str1 = fr1
		jsr		expPopFR1FR0
		
		ldx		_str0+3
		cpx		_str1+3
		bne		compdone
		lda		_str0+2
		cmp		_str1+2
compdone:
		php
		pla
		sta		funScratch1
		
		ldy		#0
		bcc		start
		mva		_str1+2 _str0+2
		ldx		_str1+3
start:
		beq		loop2_start
loop:
		lda		(_str0),y
		cmp		(_str1),y
		bne		done
		iny
		bne		loop
		inc		_str0+1
		inc		_str1+1
		dex
		bne		loop
		beq		loop2_start
loop2:
		lda		(_str0),y
		cmp		(_str1),y
		bne		done
		iny
loop2_start:
		cpy		_str0+2
		bne		loop2

		lda		funScratch1
		pha
		plp
done:
		rts
.endp

;===========================================================================
.proc funUnaryMinus
		jsr		expPopFR0
		
		;test for zero
		lda		fr0
		beq		done
		eor		#$80
		sta		fr0
done:
		jmp		expPushFR0
.endp

;===========================================================================
.proc funArrayComma
		inc		expCommas
.def :funUnaryPlus = *
		rts
.endp

;===========================================================================
; This is used for expressions of the form:
;
;	A$(start)
;	A$(start, end)
;
; Both start and end are 1-based and the end is inclusive. Error 5 results
; if start is 0, end is less than start, or end is beyond the end of the
; string (length for rvalue, capacity for lvalue).
;
; What makes this operator tricky to handle is determining whether the
; subscripts should be checked against the current or max string length:
;
;	DIM A$(10)
;	A$="XYZ"
;	A$(LEN(A$(1,2))+4)="AB"
;
; As can be seen above, it is possible for both lvalue and rvalue contexts
; to occur in the same expression. We detect an lvalue context by the
; global assignment flag and whether we're at the bottom of the eval stack;
; once we are on the right side of the assignment, the eval stack will have
; the lvalue at the bottom and therefore everything else must be in rvalue
; context.
;
; Annoyingly, if we're in an assignment, we can't update the string
; length yet as it depends on the length of the string assigned. 
;
.proc funArrayStr
		;check for a second subscript
		lda		expCommas
		beq		no_second
		
		;convert second subscript to int and move into place
		jsr		expPopFR0IntPos
		
		;##TRACE "String subscript 2 = %d" dw(fr0)
		ldx		#fr1+4
		jsr		ExprStoreFR0Int
no_second:
		;convert first subscript to int and subtract 1 to convert 1-based
		;to 0-based indexing
		jsr		expPopFR0IntPos
		
		;##TRACE "String subscript 1 = %d" dw(fr0)
		sec
		txa
		sbc		#1
		sta		fr1+2
		lda		fr0+1
		sbc		#0
		sta		fr1+3
		
		;first subscript can't be zero since it's 1-based
		bcc		bad_subscript

		;pop off the array variable
		jsr		ExprPopExtFR0
		
		;check that it is dimensioned
		lda		prefr0
		lsr
		bcs		dim_ok
		jmp		errorDimError
dim_ok:

		;##TRACE "String var: adr=$%04x, len=%d, capacity=%d" dw(fr0) dw(fr0+2) dw(fr0+4)

		;determine whether we should use the length or the capacity to
		;bounds check
		ldx		#fr0+2				;use length
		lda		argsp				;bottom of stack?
		bne		use_length			;nope, can't be root assignment... use length
		lda		expAsnCtx			;in assignment context?
		beq		use_length			;nope, use length
		ldx		#fr0+4				;use capacity
		lda		expCommas
		seq:asl	expAsnCtx
use_length:
		
		;check if we had a second subscript
		lda		expCommas
		bne		check_second
		
		;no second subscript - copy the limit
		mwa		0,x fr1+4
		jmp		second_ok

check_second:
		;yes, we did - bounds check it against the limit
		lda		fr1+5
		cmp		1,x
		bcc		second_ok
		beq		second_eq
bad_subscript:
		jmp		errorStringLength
second_eq:
		lda		fr1+4
		cmp		0,x
		bcc		second_ok
		bne		bad_subscript
second_ok:
		;check the second subscript against the first subscript
		lda		fr1+2
		cmp		fr1+4
		lda		fr1+3
		sbc		fr1+5
		bcs		bad_subscript		;(x-1)-y >= 0 => (x-1) >= y --> invalid
		
		;Merge subscripts back into string descriptor:
		; - offset address by X
		; - decrease length by X
		; - decrease capacity by X
		;
		;##ASSERT dw(fr1+2) < dw(fr0+4)
		;##ASSERT dw(fr1+4) <= dw(fr0+4)
		;address += start
		lda		fr1+2
		adc		fr0					;!! carry is clear from branch above
		sta		fr0
		lda		fr1+3
		adc		fr0+1
		sta		fr0+1

		;length -= start; clamp if needed
		sec
		lda		fr0+2
		sbc		fr1+2
		sta		fr0+2
		lda		fr0+3
		sbc		fr1+3
		sta		fr0+3

		;capacity -= start
		sec
		lda		fr1+4
		sbc		fr1+2
		sta		fr0+4
		tax
		lda		fr1+5
		sbc		fr1+3
		sta		fr0+5
		
		;limit length against capacity
		cpx		fr0+2
		lda		fr0+5
		sbc		fr0+3
		bcs		length_ok
		stx		fr0+2
		mva		fr0+5 fr0+3
length_ok:
		
		;push subscripted result back onto eval stack
		;##TRACE "Pushing substring: var %02X address $%04X length $%04X capacity $%04X" db(prefr0+1) dw(fr0) dw(fr0+2) dw(fr0+4)
		
		jsr		ExprPushExtFR0
		
		;all done - do standard open parens processing
		jmp		funOpenParens
.endp

;===========================================================================
; Numeric array indexing
;
;	A(aexp)
;	A(aexp,aexp)
;
; Errors:
;	Error 9 if either bound is out of bounds
;
; Numeric arrays are indexed from 0..N where N is the bound from the DIM
; statement. If the second index is omitted, it is assumed to be 0. 1D/2D
; indexing may be used with either 1D/2D DIM'd arrays. The first index is
; the lower order index, so the offset for A(X,Y) for DIM A(N,M) is
; X+Y*(N+1).
;
.proc funArrayNum
		lda		#0
		sta		fr1
		sta		fr1+1

		;check if we have two subscripts
		lda		expCommas
		beq		one_dim
		
		;load second subscript
		jsr		expPopFR0Int
		
		;bounds check against second array size
		lda		argsp
		sec
		sbc		#10
		tay
		txa
		sbc		(argstk),y
		iny
		lda		fr0+1
		sbc		(argstk),y
		bcc		bound2_ok
		
invalid_bound:
		;index out of bound -- issue dim error
		jmp		errorDimError

bound2_ok:
		;multiply by first array size
		dey
		dey
		lda		(argstk),y
		sta		fr1+1
		dey
		lda		(argstk),y
		sta		fr1
		jsr		umul16x16
		jsr		fmove
		
one_dim:
		jsr		expPopFR0Int
		
		;bounds check against first array size
		lda		argsp
		sec
		sbc		#4
		tay
		txa
		sbc		(argstk),y
		iny
		lda		fr0+1
		sbc		(argstk),y
		bcs		invalid_bound
		
		;add in second index offset
		clc
		lda		fr0
		adc		fr1
		sta		fr0
		lda		fr0+1
		adc		fr1+1
		sta		fr0+1
				
		;multiply by 6
		jsr		umul16_6
		
		;add address of array (stack always has abs)
		lda		argsp
		;##ASSERT db(dw(argstk)+a-8)=$43
		;##TRACE "Doing array indexing: offset=$%04x, array=$%04x" dw(fr0) dw(dw(argstk)+a-6)
		sub		#6
		tay
		lda		(argstk),y
		iny
		add		fr0
		sta		fr1
		lda		(argstk),y
		adc		fr0+1
		sta		fr1+1
		dey
		dey
		dey
		sty		argsp
		
		;check if this is the first entry on the arg stack -- if so,
		;stash off the element address for possible assignment
		bne		not_first
		
		;##TRACE "Array element pointer: %04x" dw(fr1)
		mwa		fr1 lvarptr
		
not_first:
		;load variable to fr0
		ldx		#$fa
		ldy		#0
copyloop:
		lda		(fr1),y
		iny
		sta		fr0+6,x
		inx
		bne		copyloop
		
		;push value onto stack
		jsr		expPushFR0
		
		;all done - do standard open parens processing
		jmp		funOpenParens
.endp

;===========================================================================
; DIM avar(M)
; DIM avar(M,N)
;
; Sets dimensions for a numeric array variable.
;
; Errors:
;	Error 9 if M=0 or N=0
;	Error 9 if out of memory
;	Error 3 if M/N outside of [0, 65535]
;
.proc funDimArray
		ldx		#fr1
		jsr		zf1
		jsr		expPopFR0Int
		lda		expCommas
		beq		one_dim
		jsr		fmove
		jsr		expPopFR0Int
one_dim:
		inw		fr0
		inw		fr1
		jsr		expPopVar
		
		;check if it is undimensioned
		ldy		#0
		lda		(varptr),y
		lsr
		bcc		not_dimmed
		
		;already dimensioned -- error
		jmp		errorDimError
		
not_dimmed:
		;##TRACE "Allocating new array with dimensions: %ux%u" dw(fr0) dw(fr1)
		
		;store relative address from STARP
		jsr		funDimStr.set_array_offset
		
		;store dimensions
		mva		fr0 (varptr),y+
		mva		fr0+1 (varptr),y+
		mva		fr1 (varptr),y+
		mva		fr1+1 (varptr),y
		
		;bump both dimensions by one and compute array size
		jsr		umul16x16
		bcs		overflow
		jsr		umul16_6
		bcs		overflow
		
		;##TRACE "Allocating %u bytes" dw(fr0)
		
		ldy		#0
		mva		#$41 (varptr),y
		
		;relocate runtime stack
		lda		fr0
		ldy		fr0+1
		mwx		runstk a0
		ldx		#runstk
		jsr		expandTable

		jmp		funOpenParens
overflow:
		jmp		errorNoMemory

.endp

;===========================================================================
.proc funDimStr
		;pop string length
		jsr		expPopFR0IntPos
		
		;throw dim error if it is zero
		ora		fr0
		bne		not_zero
		jmp		errorDimError
		
not_zero:
		;get variable reference
		jsr		expPopVar
		
		;check if var is undimensioned
		ldy		#0
		lda		(varptr),y
		and		#$03
		beq		not_dimmed
		
		;already dimensioned -- error
		jmp		errorDimError
		
not_dimmed:
		;store new address, length, and dimension
		jsr		set_array_offset
		
		lda		#0
		sta		(varptr),y+
		sta		(varptr),y+
		mva		fr0 (varptr),y+
		mva		fr0+1 (varptr),y
		
		;attempt to allocate memory
		ldy		fr0+1
		lda		fr0
		mwx		runstk a0
		ldx		#runstk
		jsr		expandTable
		
		;mark as dimensioned string
		;##TRACE "Allocating new string for var $%02X" (dw(varptr)-dw(vvtp))/8+$80
		ldy		#0
		lda		#$81
		sta		(varptr),y

		;all done
		jmp		funOpenParens
		
set_array_offset:
		ldy		#2
		lda		runstk
		sub		starp
		sta		(varptr),y+
		lda		runstk+1
		sbc		starp+1
		sta		(varptr),y+
		rts
.endp

;===========================================================================
.proc funStr
		;convert TOS to string
		jsr		expPopFR0
		jsr		fasc
		
		;determine length of string and fix last char
		ldy		#$ff
lenloop:
		iny
		lda		(inbuff),y
		bpl		lenloop
		eor		#$80
		sta		(inbuff),y
		iny
		tya
		tax
		
		;push string onto stack
		jsr		ExprBeginPushStringVal
		mwa		inbuff (argstk),y+
		txa
		jmp		funChr.finish_str_entry
.endp


;===========================================================================
; CHR$(aexp)
;
; Returns a single character string containing the character with the given
; value.
;
; Quirks:
; - Atari BASIC only uses a single buffer for the result of this function,
;   so using it more than once in an expression such that the results
;   overlap results in erroneous results. This can only occur with string
;   comparisons, which is why the manual warns against doing so. However,
;   CHR$() and STR$() can occur together, so they must use different
;   buffers. We don't have control over the STR$() position since FASC sets
;   INBUFF, so we offset our location here instead.
;
.proc funChr
		jsr		expPopFR0Int
		stx		lbuff+$40
		
		;push string onto stack
		jsr		ExprBeginPushStringVal
		mwa		#lbuff+$40 (argstk),y+
		lda		#1
finish_str_entry:
		jsr		ExprPushRawByteAsWord
		jsr		ExprPushRawByteAsWord
		sty		argsp
		rts		
.endp


;===========================================================================
; USR(aexp [,aexp...])
;
.proc funUsr
usrArgCnt = funScratch1

		;copy off arg count
		;##TRACE "Dispatching user routine at %g with %u arguments" dw(argstk)+db(argsp)-8*db(expFCommas)+2 db(expFCommas)
		mva		expFCommas usrArgCnt

		;establish return address for user function
		jsr		arg_loop_start

		;push result back onto stack and return
		jmp		expPushFR0Int
		
arg_loop:
		;arguments on eval stack to words on native stack
		;(!!) For some reason, Atari BASIC pushes these on in reverse order!
		jsr		expPopFR0Int
		txa
		pha
		lda		fr0+1
		pha
arg_loop_start:
		dec		expFCommas
		bpl		arg_loop
		
		;push arg count onto stack
		lda		usrArgCnt
		pha

		;extract address
		jsr		expPopFR0Int
		
		;dispatch
		jmp		(fr0)
.endp

;===========================================================================
; ASC(sexp)
;
; Returns the character value of the first character of a string as a
; number.
;
; Quirks:
;	- Atari BASIC does not check whether the string is empty and returns
;	  garbage instead.
;
.proc funAsc
		jsr		expPopAbsString
push_byte_at_fr0_addr:
		ldy		#0
		lda		(fr0),y
		sta		fr0
		sty		fr0+1
		jmp		expPushFR0Int
.endp


;===========================================================================
.proc funPeek
		jsr		expPopFR0Int
		jmp		funAsc.push_byte_at_fr0_addr
.endp

;===========================================================================
; VAL(sexp)
;
; Converts a number at the beginning of a string to a numerical value
; according to AFP rules. Leading spaces are allowed; trailing characters
; are ignored.
;
; Examples:
;	VAL("") -> Error 18
;	VAL(" ") -> Error 18
;	VAL("0") -> 0
;	VAL(" 0") -> 0
;	VAL(" 0 ") -> 0
;	VAL("0 1") -> 0
;	VAL("1E+060") -> 1000000
;	A$="12345": VAL(A$(1,2)) -> 12		!! tricky case
;
.proc funVal
		jsr		expPopAbsString
		mva		#0 cix
		jsr		IoTerminateString
		jsr		afp
		jsr		IoUnterminateString
		bcs		err
		jmp		expPushFR0
err:
		jmp		errorInvalidString
.endp


;===========================================================================
; LEN(sexp)
;
; Returns the length in characters of a string expression.
;
.proc funLen
		jsr		expPopAbsString
		mwa		fr0+2 fr0
		jmp		expPushFR0Int
.endp


;===========================================================================
; ADR(sexp)
;
; Returns the starting address of a string expression.
;
.proc funAdr
		jsr		expPopAbsString
		jmp		expPushFR0Int
.endp


;===========================================================================
; ATN(aexp)
;
; Returns the arctangent of aexp.
;
; If DEG has been issued, the result is returned in degrees instead of
; radians.
;
.proc funAtn
_sign = funScratch1

		jsr		expPopFR0

		;stash off sign and take abs
		lda		fr0
		asl
		ror		_sign
		lsr
		sta		fr0
		
		;check if |x| >= 1; if so, use approximation directly
		cmp		#$40
		bcs		is_big
		jsr		do_approx
		jmp		xit
		
is_big:
		;compute pi/2 - f(1/x)
		jsr		fmove
		jsr		fld1
		jsr		fdiv
		jsr		do_approx
		jsr		fmove
		ldx		#<fpconst_pi2
		ldy		#>fpconst_pi2
		jsr		fld0r
		jsr		fsub
xit:
		lda		degflg
		beq		use_radians
		
		;convert radians to degrees
		ldx		#<fp_180_div_pi
		ldy		#>fp_180_div_pi
		jsr		fld1r
		jsr		fmul
		
use_radians:
		;merge in sign
		lda		_sign
		eor		fr0
		and		#$80
		eor		fr0
		sta		fr0
		jmp		expPushFR0

do_approx:
		;save x
		jsr		MathStoreFR0_FPSCR

		;compute z = x*x
		jsr		fmove
		jsr		fmul
		
		;compute f(x^2)
		ldx		#<fpconst_atncoef
		ldy		#>fpconst_atncoef
		lda		#11
		jsr		plyevl
		
		;compute x*f(x^2)
		jsr		MathLoadFR1_FPSCR
		jmp		fmul
.endp


;===========================================================================
.proc funCos
		ldx		#1
		jmp		funSin.cos_entry
.endp


;===========================================================================
.proc funSin
_cosFlag = funScratch1
_quadrant = funScratch2

		ldx		#0
cos_entry:
		;save sincos flag
		stx		_cosFlag
		
		;get arg
		jsr		expPopFR0

		;convert from radians/degrees to quarter-angle binary fraction
		;FMUL would be faster, but we use FDIV for better accuracy for
		;quarter angles
		lda		#<angle_conv_tab
		clc
		adc		degflg
		tax
		ldy		#>angle_conv_tab
		jsr		fld1r
		jsr		fdiv
		
		;stash and then floor
		jsr		MathStoreFR0_FPSCR

		jsr		MathFloor
		
		;find the appropriate mantissa byte to identify which
		;quadrant we are in
		lda		fr0
		and		#$7f
		tax
		lda		#$00
		cpx		#$40				;check if |z| < 1.0
		bcc		is_tiny_or_big		;can't be odd if it is this small
		cpx		#$45				;check if |z| >= 10^10
		bcs		is_tiny_or_big		;can't be odd if it is this big
		lda		fr0-$3f,x			;load mantissa byte
is_tiny_or_big:

		;reduce to quadrant -- note that we are in BCD, so we need to
		;XOR bit 4 into bit 1
		sta		_quadrant
		lsr
		lsr
		lsr
		eor		_quadrant
		sta		_quadrant
		
		;modify for negative and cosine if needed
		clc
		bit		fr0
		bpl		is_positive
		eor		#3
		sec
is_positive:
		adc		_cosFlag
		sta		_quadrant

		;now compute fraction
		jsr		MathLoadFR1_FPSCR
		jsr		fsub
		
		;now we are doing only sin() -- check if we need to compute
		;f(1-x) for quadrants II and IV
		lsr		_quadrant
		bcc		odd_quadrant
		
		jsr		MathLoadOneFR1
		jsr		fadd
odd_quadrant:

		;take abs() of FR0 since depending on quadrant we would have
		;computed either -z or 1-z above
		lda		fr0
		and		#$7f
		sta		fr0
		
		;stash z
		jsr		MathStoreFR0_FPSCR
		
		;compute z^2
		jsr		fmove
		jsr		fmul
		
		;do polynomial expansion y = f(z^2)
		ldx		#<coefficients
		ldy		#>coefficients
		lda		#6
		jsr		plyevl
		
		;compute y' = z*f(z^2) so we have odd terms
		jsr		MathLoadFR1_FPSCR
		jsr		fmul
		
		;check if polynomial expansion is zero
		lda		fr0
		beq		frac_zero
		
		;negate result if we are in quadrants III or IV
		lsr		_quadrant
		bcc		skip_quadrant_negation

		eor		#$80
		sta		fr0
		
frac_zero:
skip_quadrant_negation:
		;push result and exit
		jmp		expPushFR0

coefficients:
		;The Maclaurin expansion for sin(x) is as follows:
		;
		; sin(x) = x - x^3/3! + x^5/5! - x^7/7! + x^9/9! - x^11/11!...
		;
		;We modify it this way:
		;
		; let y = x / pi2 (for x in [0, pi], pi2 = pi/2
		; sin(x) = y*[pi2 - y^2*pi2^3/3! + y^4*pi2^5/5! - y^6*pi2^7/7! + y^8*pi2*9/9! - y^10*pi2^11/11!...]
		;
		; let z = y^2
		; sin(x) = y*[pi2 - z*pi2^3/3! + z^2*pi2^5/5! - z^3*pi2^7/7! + z^4*pi2*9/9! - z^5*pi2^11/11!...]
		;
		dta		$BD,$03,$43,$18,$69,$61		;-0.00 00 03 43 18 69 61 07114469471
		dta		$3E,$01,$60,$25,$47,$91		; 0.00 01 60 25 47 91 80067132008
		dta		$BE,$46,$81,$65,$78,$84		;-0.00 46 81 65 78 83 6641486819
		dta		$3F,$07,$96,$92,$60,$37		; 0.07 96 92 60 37 48579552158
		dta		$BF,$64,$59,$64,$09,$56		;-0.64 59 64 09 55 8200198258
		dta		$40,$01,$57,$07,$96,$33		; 1.57 07 96 32 67682236008

.endp


;===========================================================================
.proc funRnd
_temp = fr0+6
_temp2 = fr0+7
		;pop off dummy argument
		jsr		expPopFR0

		mva		#$3f fr0
		ldx		#5
loop:
		;keep looping until we get a 7-bit value below 100
loop2:
		lda		random
		cmp		#200
		bcs		loop2
		sta		_temp2

		;convert binary value to BCD		
		ldy		#7
		lda		#0
		sed
bitloop:
		asl		_temp2
		sta		_temp
		adc		_temp
		dey
		bne		bitloop
		cld
		
		;store digit pair
		sta		fr0,x
		
		;continue until we have 5 digits
		dex
		bne		loop
		
		;renormalize random value and exit
		jsr		normalize
		jmp		expPushFR0
.endp


;===========================================================================
; FRE(aexp)
;
; Returns the number of free bytes available. This is defined as the
; difference between the top of the runtime stack (BASIC MEMTOP) and OS
; MEMTOP.
;
; Quirks:
;	The returned value is actually off by one as OS MEMTOP is inclusive.
;
.proc funFre
		jsr		expPopFR0
		lda		memtop
		sub		memtop2
		sta		fr0
		lda		memtop+1
		sbc		memtop2+1
		sta		fr0+1
		jmp		expPushFR0Int
.endp


;===========================================================================
.proc funExp
		jsr		expPopFR0
		jsr		exp
		bcs		funLog.err
		jmp		expPushFR0
.endp


;===========================================================================
.proc funLog
		jsr		expPopFR0
		jsr		log
		bcs		err
		jmp		expPushFR0
err:
		jmp		errorValueErr
.endp


;===========================================================================
.proc funClog
		jsr		expPopFR0
		jsr		log10
		bcs		funLog.err
		jmp		expPushFR0
.endp


;===========================================================================
; SQR(aexpr)
;
; Returns the square root of aexpr.
;
; If aexpr is negative, Error 3 is returned.
;
; The traditional way of implementing a square root is to use an iterative
; approximation to the reciprocal square root and then compute x*rsqrt(x).
; We don't use that method here as the base 100 representation makes it
; harder to get a good initial guess and it requires about 6-7 iterations
; to converge to 10 digits.
;
; Because division is about the same speed as multiplication in the Atari
; math pack, we use the Babylonian method instead, which has fewer
; multiply/divide operations:
;
;	x' = (x + (S/x))/2
;
; To ensure fast convergence, we first reduce the range of the mantissa
; to between 0.10 and 1.00. In this way, we can get to 10 sig digits in
; four iterations.
;
.proc funSqr
_itercount = funScratch1

		;get argument
		jsr		expPopFR0
		
		;check if it is zero
		lda		fr0
		beq		done
		
		;error out if negative
		bpl		is_positive
		jmp		errorValueErr
		
is_positive:
		;stash original value
		jsr		MathStoreFR0_FPSCR
		
		;compute a good initial guess
		lda		fr0+1
		ldx		#7
guess_loop:
		cmp		approx_compare_tab-1,x
		bcs		guess_ok
		dex
		bne		guess_loop
guess_ok
		lda		approx_value_tab,x
		
		;divide exponent by two and check if we need to
		;multiply by ten
		lsr		fr0
		bcs		no_tens
		
		and		#$0f
no_tens:
		sta		fr0+1
		
		;rebias exponent
		lda		#$20
		clc
		adc		fr0
		sta		fr0
		
		;do 4 iterations
		mva		#4 _itercount
				
iter_loop:
		;FR1 = x
		jsr		fmove
		
		;PLYARG = x
		ldx		#<plyarg
		ldy		#>plyarg
		jsr		fst0r
		
		;compute S/x
		ldx		#<fpscr
		ldy		#>fpscr
		jsr		fld0r
		jsr		fdiv
		
		;compute S/x + x
		ldx		#<plyarg
		ldy		#>plyarg
		jsr		fld1r
		jsr		fadd
		
		;divide by two
		ldx		#<const_half
		ldy		#>const_half
		jsr		fld1r
		jsr		fmul
		
		;loop back until iterations completed
		dec		_itercount
		bne		iter_loop
		
done:
		jmp		expPushFR0
		
approx_compare_tab:
		dta		$03,$08,$15,$25,$37,$56,$67,$88
approx_value_tab:
		dta		$11,$22,$33,$44,$55,$66,$77,$88,$99
.endp


;===========================================================================
; SGN(aexp)
;
; Returns the sign of a number, as -1/0/+1.
;
.proc funSgn
		jsr		expPopFR0
		
		;check if the number is zero
		lda		fr0
		beq		is_zero
		
		;convert to +/-1
		pha
		jsr		fld1
		pla
		and		#$80
		ora		#$40
		sta		fr0
is_zero:
		jmp		expPushFR0
.endp


;===========================================================================
.proc funAbs
		jsr		expPopFR0
		asl		fr0
		lsr		fr0
		jmp		expPushFR0
.endp


;===========================================================================
; This is really floor().
.proc funInt
		jsr		expPopFR0
		jsr		MathFloor		
		jmp		expPushFR0
.endp


;===========================================================================
; PADDLE(aexp)
;
; Returns the rotational position of the given paddle controller, from 0-7.
;
; Errors:
;	3 - if aexp<0 or aexp>255
;
; Quirks:
;	Invalid paddle numbers 8-255 aren't trapped and return data from
;	other parts of the OS database.
;
.proc funPaddle
		jsr		expPopFR0IntPos
		lda		paddl0,x
return_entry:
		sta		fr0
		jmp		expPushFR0Int
.endp


;===========================================================================
.proc funStick
		jsr		expPopFR0IntPos
		lda		stick0,x
		jmp		funPaddle.return_entry
.endp


;===========================================================================
.proc funPtrig
		jsr		expPopFR0IntPos
		lda		ptrig0,x
		jmp		funPaddle.return_entry
.endp


;===========================================================================
.proc funStrig
		jsr		expPopFR0IntPos
		lda		strig0,x
		jmp		funPaddle.return_entry
.endp

;===========================================================================
.echo "- Function module length: ",*-?statements_start
