; Altirra BASIC - Function module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

?functions_start = *

;===========================================================================
.proc funStringCompare
_str0 = fr0
_str1 = fr1

		;reset expression type back to numeric
		stx		expType

		lda		funCompare.compare_mode_tab-TOK_EXP_STR_LE,x
		sta		a3

		jsr		expPopFR1
		
		;compare lengths
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
		txa
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
		jmp		funCompare.push_flags_as_bool
.endp

;===========================================================================
.proc funCompare
		;save comparison mode
		lda		compare_mode_tab-TOK_EXP_LE,x
		sta		a3
		
		;pop first argument off
		jsr		expPopFR1

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
		jmp		zfr0
nonzero:
		jmp		fld1
		
cmptab:	;input = (z,c)
		;output = (=, <>, <, >=, >, <=)
		dta		%010110		;fr1 > fr0
		dta		%011001		;fr1 < fr0
		dta		%100101		;fr1 = fr0
		dta		%100101		;fr1 = fr0
		
compare_mode_tab:
		dta		$01,$10,$04,$08,$02,$20
.endp

;===========================================================================
; ^ operator (exponentiation)
;
; Quirks (arguably bugs):
;	0^0 = 0
;	0^1E+80 -> Error 11
;	1^131072 = 2 with XL/XE OS, even though LOG/CLOG(1) = 0
;
; Errors:
;	Error 3 if x=0 and y<0
;	Error 11 if x<0 and y not integer
;	Error 11 if underflow/overflow
;
.proc funPower
_flags = funScratch1
_rneg = funScratch2

		;unfortunately, we have to futz with the stack here since the
		;parameters are in the wrong order....
		ldx		#6
push_loop:
		lda		fr0-1,x
		pha
		dex
		bne		push_loop
		stx		_rneg

		jsr		expPopFR0

		;check if x<0 and take abs(x) if so
		jsr		MathSplitSign

		;check if x=0 and cache nonzero flag
		cmp		#1
		ror		_flags
		bpl		x_zero

		;compute log(x)
		jsr		log10
		bcs		funAdd.arith_error

x_zero:
		;pop y
		ldx		#<-6
pop_loop:
		pla
		sta		fr1+6,x
		inx
		bne		pop_loop

		asl		_flags
		bcc		x_zero2
		bpl		x_positive

		;x is negative... check if y is an integer
		lda		fr1
		beq		y_zero
		and		#$7f

		;bias y and skip if it's too large to be odd or have a fraction
		sbc		#$45
		bpl		y_large_integer

		;check if y>0 and y<1, which means it must be fractional
		cmp		#<-5
		bcc		funAdd.arith_error

		;load least significant integer byte and copy oddness to sign
		tax
		lda		fr1+6,x
		lsr
		ror		_rneg

		;check for fraction
		bcc		y_fracstart
y_fracloop:
		lda		fr1+6,x
		bne		funAdd.arith_error
y_fracstart:
		inx
		bmi		y_fracloop		

y_large_integer:
x_positive:
y_zero:
		;compute log(x)*y
		jsr		fmul
		bcs		funAdd.arith_error

		;compute exp(log(x)*y)
		jsr		exp10
		bcs		funAdd.arith_error

		;flip sign if x<0 and y odd
		lda		fr0
		eor		_rneg
		sta		fr0

push_exit:
		rts

x_zero2:
		;x is zero, so fire error if y<0
		lda		fr1
		bmi		funAdd.arith_error

		;return zero
		rts
.endp

;===========================================================================
; * operator
;
; Errors:
;	Error 11 if underflow/overflow
;
.proc funMultiply
		jsr		expPopFR1
		jsr		fmul
		jmp		funAdd.arith_exit
.endp


;===========================================================================
; - operator
;
; Errors:
;	Error 11 if overflow
;
;---------------------------------------------------------------------------
; + operator
;
; Errors:
;	Error 11 if overflow
;
funSubtract:
		jsr		funUnaryMinus
.proc funAdd
		jsr		expPopFR1
_sub_entry:
		jsr		fadd
arith_exit:
		bcc		funPower.push_exit
arith_error:
		jmp		errorFPError
.endp

;===========================================================================
; / operator
;
; Errors:
;	Error 11 if underflow/overflow/div0
;
.proc funDivide
		jsr		fmove
		jsr		expPopFR0
		jsr		fdiv
		jmp		funAdd.arith_exit
.endp

;===========================================================================
.proc funNot
		lda		fr0+1
		beq		is_zero
push_zero:
		jmp		zfr0
is_zero:
push_one:
		jmp		fld1
.endp

;===========================================================================
.proc funOr
		jsr		expPopFR1
		lda		fr0+1
		ora		fr1+1
		jmp		funAnd.push_z
.endp

;===========================================================================
.proc funAnd
		jsr		expPopFR1
		lda		fr0+1
		beq		push_fr0
		lda		fr1+1
push_z:
		bne		funNot.push_one
		beq		funNot.push_zero
push_fr0:
		rts
.endp

;===========================================================================
.proc funAssignNum
		;copy FR0 to variable or array element
		;;##TRACE "Assigning %g to element at $%04x" fr0 dw(lvarptr)
		ldy		#5
copy_loop:
		mva		fr0,y (lvarptr),y
		dey
		bpl		copy_loop

		;since this is an assignment, the stack must be empty afterward...
		;but we don't care about the state of the stack.
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
		jsr		fmove
		
		;pop dest string to FR0
		jsr		ExprPopExtFR0
		
		;##TRACE "Dest string: $%04x+%u [%u] = [%.*s]" dw(fr0) dw(fr0+2) dw(fr0+4) dw(fr0+2) dw(fr0)
		;##TRACE "Source string: $%04x+%u [%u] = [%.*s]" dw(fr1) dw(fr1+2) dw(fr1+4) dw(fr1+2) dw(fr1)

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
		
		ldy		#0
		sec
		txa
		sbc		(lvarptr),y
		tax
		iny
		lda		fr1+5
		sbc		(lvarptr),y
		sta		fr1+5
		
		;check if we are doing A$(X)=...
		bit		expAsnCtx
		bmi		update_length
		
		;check if the new length is longer than the existing length
		;##TRACE "Comparing var length: existing %u, proposed %u" dw(dw(varptr)+4) db(fr1+5)*256+x
		txa
		iny
		cmp		(lvarptr),y
		iny
		lda		fr1+5
		sbc		(lvarptr),y
		bcc		no_update_length
		
update_length:
		;##TRACE "Setting var length to %d" db(fr1+5)*256+x
		ldy		#3
		mva		fr1+5 (lvarptr),y
		dey
		txa
		sta		(lvarptr),y
no_update_length:

		;copy source address to dest pointer (a1)
		ldx		fr1
		ldy		fr1+1
		stx		a1
		sty		a1+1

		;##TRACE "String assignment: copy ($%04x+%d -> $%04x)" dw(a1) dw(a3) dw(a0)
		;##ASSERT dw(a0) >= dw(starp) and dw(a0)+dw(a3) <= dw(runstk)
		
		;do memcpy and we're done
		jsr		copyAscending
		jmp		ExprPopExtFR0
.endp

;===========================================================================
.proc funUnaryMinus
		;test for zero
		lda		fr0
		beq		done
		eor		#$80
		sta		fr0
done:
		rts
.endp

;===========================================================================
.proc funArrayComma
		inc		expCommas
.def :funUnaryPlus = *
.def :expComma = *
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
		jsr		ExprConvFR0IntPos
		
		;##TRACE "String subscript 2 = %d" dw(fr0)
		stx		fr1+4
		sta		fr1+5
		jsr		ExprPopExtFR0
no_second:
		;convert first subscript to int and subtract 1 to convert 1-based
		;to 0-based indexing
		jsr		ExprConvFR0IntPos
		
		;##TRACE "String subscript 1 = %d" dw(fr0)
		tay
		txa
		bne		sub1_ok
		dey
		;first subscript can't be zero since it's 1-based
		bmi		bad_subscript
sub1_ok:
		dex

		stx		fr1+2
		sty		fr1+3
		
		;pop off the array variable
		jsr		ExprPopExtFR0
		
		;##TRACE "String var: adr=$%04x, len=%d, capacity=%d" dw(fr0) dw(fr0+2) dw(fr0+4)

		;determine whether we should use the length or the capacity to
		;bounds check
		ldx		#fr0+2				;use length
		stx		expType				;!! - set expression type to string!
		lda		argsp				;bottom of stack?
		cmp		#3
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
		ldx		#fr0
		ldy		#fr1+2
		jsr		IntAdd

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
		tay
		sbc		fr0+3
		bcs		length_ok
		stx		fr0+2
		sty		fr0+3
length_ok:
		
		;push subscripted result back onto eval stack
		;##TRACE "Pushing substring: var %02X address $%04X length $%04X capacity $%04X" db(prefr0+1) dw(fr0) dw(fr0+2) dw(fr0+4)
		
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
		;check if we have two subscripts and clear sub2 if not
		lda		expCommas
		beq		one_dim
		
		;load second subscript
		jsr		ExprConvFR0Int
		stx		fr0+4
		sta		fr0+5
		
		;bounds check against second array size (offset 2, one level up)
		lda		argsp
		sec
		sbc		#4
		tay
		txa
		sbc		(argstk),y
		lda		fr0+1
		sbc		(argstk2),y
		bcs		invalid_bound
		
bound2_ok:
		;multiply by first array size
		dey
		lda		(argstk2),y
		sta		fr0+3
		lda		(argstk),y
		sta		fr0+2
		jsr		umul16x16
		jsr		fmove
		jsr		ExprPopExtFR0
		
one_dim:
		jsr		ExprConvFR0Int
		
		;bounds check against first array size
		ldy		argsp
		dey
		dey
		txa
		cmp		(argstk),y
		lda		fr0+1
		sbc		(argstk2),y
		dey
		sty		argsp
		bcs		invalid_bound
		
		;add in second index offset, if there is one
		lda		expCommas
		beq		skip_add_dim2
		ldx		#fr0
		ldy		#fr1
		jsr		IntAdd
skip_add_dim2:
				
		;multiply by 6
		jsr		umul16_6
		
		;add address of array (stack always has abs)
		ldy		argsp
		;##TRACE "Doing array indexing: offset=$%04x, array=$%02x%02x, argsp=$%02x" dw(fr0) db(dw(argstk)+y-3) db(dw(argstk2)+y-3) y
		lda		(argstk),y
		adc		fr0
		sta		varptr
		tax
		lda		(argstk2),y
		adc		fr0+1
		sta		varptr+1
		
		;check if this is the first entry on the arg stack -- if so,
		;stash off the element address for possible assignment
		cpy		#3
		bne		not_first
		
		;##TRACE "Array element pointer: %04x" dw(fr1)
		sta		lvarptr+1
		stx		lvarptr
		
not_first:
		;load variable to fr0
		ldy		#5
		jsr		VarLoadFR0_OffsetY
		
		;all done - do standard open parens processing
.def :funOpenParens = *
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

invalid_bound:
dim_error:
		;index out of bound -- issue dim error
		jmp		errorDimError
.endp

;===========================================================================
; DIM avar(M)
; DIM avar(M,N)
;
; Sets dimensions for a numeric array variable.
;
; Atari BASIC throws an error 9 if the array size exceeds 32K-1 bytes. We
; lift that limitation here (there's no good reason for it).
;
; Errors:
;	Error 9 if M=0 or N=0
;	Error 9 if out of memory
;	Error 3 if M/N outside of [0, 65535]
;
.proc funDimArray
		jsr		ExprConvFR0Int
		ldy		expCommas
		sty		fr1
		sty		fr1+1
		beq		one_dim
		stx		fr1
		sta		fr1+1
		jsr		expPopFR0Int
one_dim:
		tay
		inx
		stx		fr0+2
		sne:iny
		sty		fr0+3

		lda		fr1
		clc
		adc		#1
		sta		fr0+4
		lda		#0
		sta		argsp				;!! - reset stack while we have a zero
		adc		fr1+1
		sta		fr0+5

		;check if var is already dimensioned
		;store new address, length, and dimension
		jsr		set_array_offset
				
		;compute array size
		jsr		umul16x16
		bcs		funArrayNum.dim_error
		jsr		umul16_6
		bcs		funArrayNum.dim_error
		
		;##TRACE "Allocating %u bytes" dw(fr0)
				
		;allocate space
		lda		fr0
		ldy		fr0+1
allocate_and_exit:
		mwx		runstk a0
		ldx		#runstk
		jsr		expandTable

		;mark dimensioned
		lda		stScratch
		ora		#$01
		ldy		#$fe
		dec		lvarptr+1
		sta		(lvarptr),y

		;all done
		jmp		funOpenParens
.endp

;===========================================================================
; starting: $4E
;
set_array_offset:
		;check if the array is already dimensioned, but do NOT mark it
		;yet -- we need to allocate the space before that
		ldy		#$fe
		dec		lvarptr+1
		lda		(lvarptr),y
		inc		lvarptr+1
		sta		stScratch
		lsr
		bcs		funArrayNum.dim_error

		lda		runstk
		sub		starp
		sta		fr0
		lda		runstk+1
		sbc		starp+1
		sta		fr0+1
		jmp		funAssignNum

.proc funDimStr
		;pop string length
		jsr		ExprConvFR0IntPos

		;move to capacity location (fr0+4)
		stx		fr0+4
		sta		fr0+5
		
		;throw dim error if it is zero
		ora		fr0
		beq		funArrayNum.dim_error

		lda		#0
		sta		argsp				;!! - reset stack while we have a zero
		sta		fr0+2
		sta		fr0+3

		;check if var is already dimensioned
		;store new address, length, and dimension
		jsr		set_array_offset
		
		;allocate memory, relocate runtime stack and exit
		lda		fr0+4
		ldy		fr0+5
		jmp		funDimArray.allocate_and_exit
.endp

;===========================================================================
.proc funHex
		;convert string to hex at LBUFF
		clc
		jsr		IoConvNumToHex

		;push string onto stack
		bne		funStr.push_lbuff
.endp

;===========================================================================
.proc funStr
		;convert TOS to string
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
		
push_lbuff:
		;push string onto stack
		lda		inbuff
		ldx		inbuff+1
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
		jsr		ExprConvFR0Int
		stx		lbuff+$40
		
		;push string onto stack
		lda		#<[lbuff+$40]
		ldx		#>[lbuff+$40]
		ldy		#1
finish_str_entry:
		sta		fr0
		stx		fr0+1
		sty		fr0+2
		sty		fr0+4
		ldx		#0
		stx		fr0+3
		stx		fr0+5
		dex
		stx		expType
		rts		
.endp


;===========================================================================
; USR(aexp [,aexp...])
;
; Errors:
;	Error 3 if any values not in [0,65535]
;
.proc funUsr
usrArgCnt = funScratch1

		;copy off arg count
		;##TRACE "Dispatching user routine at %g with %u arguments" dw(argstk)+db(argsp)-8*db(expFCommas)+2 db(expFCommas)
		mva		expFCommas usrArgCnt

		;establish return address for user function
		jsr		arg_loop_start

		;push result back onto stack and return
		jmp		ifp
		
arg_loop:
		;arguments on eval stack to words on native stack
		;(!!) For some reason, Atari BASIC pushes these on in reverse order!
		jsr		ExprConvFR0Int
		txa
		pha
		lda		fr0+1
		pha
		jsr		ExprPopExtFR0
arg_loop_start:
		dec		expFCommas
		bpl		arg_loop
		
		;push arg count onto stack
		lda		usrArgCnt
		pha

		;extract address
		jsr		fpi
		
		;dispatch
		jmp		(fr0)
.endp

;===========================================================================
; PEEK(aexp)
;
; Returns the byte at the given location.
;
; Errors:
;	Error 3 if value not in [0,65536)
;
;---------------------------------------------------------------------------
; ASC(sexp)
;
; Returns the character value of the first character of a string as a
; number.
;
; Quirks:
;	- Atari BASIC does not check whether the string is empty and returns
;	  garbage instead.
;
.proc funPeek
		jsr		ExprConvFR0Int
.def :funAsc = *
		ldy		#0
		sty		expType
push_fr0_y:
		ldx		#0
		beq		funDpeek.peek_cont
.endp

;===========================================================================
; DPEEK(aexp)
;
; Returns the word at the given location.
;
; Errors:
;	Error 3 if value not in [0,65536)
;
.proc funDpeek
		jsr		ExprConvFR0Int
		ldy		#1
		lda		(fr0),y
		tax
		dey
		sty		expType
peek_cont:
		lda		(fr0),y
		jmp		MathWordToFP
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
		mva		#0 cix
		sta		expType
		jsr		IoTerminateString
		jsr		afp
		jsr		IoUnterminateString
		bcs		err
		rts
err:
		jmp		errorInvalidString
.endp


;===========================================================================
; LEN(sexp)
;
; Returns the length in characters of a string expression.
;
;===========================================================================
; ADR(sexp)
;
; Returns the starting address of a string expression.
;
.proc funLen
		mwa		fr0+2 fr0
.def :funAdr = *
		lsr		expType
		jmp		ifp
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
		;stash off sign and take abs
		jsr		MathSplitSign
		
		;check if |x| < 1; if so, use approximation directly
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
		ldx		#<fpconst_neg_pi2
		jsr		MathLoadConstFR1
		jsr		fadd
xit:
		lda		degflg
		beq		use_radians
		
		;convert radians to degrees
		ldx		#<fp_180_div_pi
		jsr		MathLoadConstFR1
		jsr		fmul
		
use_radians:
		;merge in sign
		asl		fr0
		asl		_sign
		ror		fr0
		rts

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
_cosFlag = funScratch1
_quadrant = funScratch2

		ldx		#1
		dta		{bit $0100}

.def :funSin = *
		ldx		#0

		;save sincos flag
		stx		_cosFlag

		;convert from radians/degrees to quarter-angle binary fraction
		;FMUL would be faster, but we use FDIV for better accuracy for
		;quarter angles
		lda		#<angle_conv_tab
		clc
		adc		degflg
		tax
		jsr		MathLoadConstFR1
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
		and		#$02
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
		jsr		funAbs				;!! - this also stomps funScratch1 (_cosFlag)
		
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
				
		;negate result if we are in quadrants III or IV
		lsr		_quadrant
		bcc		skip_quadrant_negation
		jsr		funUnaryMinus
		
skip_quadrant_negation:
		;clamp to +/-1.0
		lda		fr0
		asl
		bpl		abs_below_one
		lda		#0
		sta		fr0+5

		;push result and exit
abs_below_one:
		rts

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
		jmp		normalize
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
		lda		memtop
		sub		memtop2
		tay
		lda		memtop+1
		sbc		memtop2+1
		tax
		tya
		jmp		MathWordToFP
.endp

;===========================================================================
; EXP(aexp)
;
; Errors:
;	Error 3 if underflow/overflow
;
.proc funExp
		jsr		exp
		jmp		funLog.test_exit
.endp


;===========================================================================
; EXP(aexp)
;
; Errors:
;	Error 3 if underflow/overflow
;
.proc funLog
		jsr		log
test_exit:
		bcs		err
		rts
err:
		jmp		errorValueErr
.endp


;===========================================================================
; EXP(aexp)
;
; Errors:
;	Error 3 if underflow/overflow
;
.proc funClog
		jsr		log10
		jmp		funLog.test_exit
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
; TICKTOCK.BAS is sensitive to errors here.
;
.proc funSqr
_itercount = funScratch1		
		;check if arg is zero
		lda		fr0
		beq		done
		
		;error out if negative
		bmi		funLog.err

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
		jsr		MathStoreFR0_Page5
		
		;compute S/x
		ldx		#<fpscr
		ldy		#>fpscr
		jsr		fld0r
		jsr		fdiv
		
		;compute S/x + x
		ldx		#<plyarg
		jsr		MathLoadFR1_Page5
		jsr		fadd
		
		;divide by two
		ldx		#<const_half
		jsr		MathLoadConstFR1
		jsr		fmul
		
		;loop back until iterations completed
		dec		_itercount
		bne		iter_loop
		
done:
		rts
		
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
.nowarn .proc _funHVStick
table:
		dta	$00,$FF,$01,$00
.def :funHstick
.def :funVstick
		cpx		#TOK_EXP_VSTICK
		php
		jsr		ExprConvFR0IntPos
		lda		stick0,x
		pha
		jsr		zfr0
		pla
		plp
		beq		vstick
		lsr
		lsr
		eor		#$03
vstick:
		and		#$03
		tax
		lda		table,x
		sta		fr0
.endp
		;!! - fall through
.proc funSgn
		;check if the number is zero
		asl		fr0
		beq		is_zero
		
		;convert to +/-1
		lda		#$80
		ror
		pha
		jsr		fld1
		pla
		sta		fr0
is_zero:
		rts
.endp


;===========================================================================
funAbs = MathSplitSign

;===========================================================================
; This is really floor().
funInt = MathFloor		


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
.proc funPaddleStick
		lda		offset_table-$51,x
		pha
		jsr		ExprConvFR0IntPos
		lda		#2
		sta		fr0+1
		pla
		tay
		jmp		funPeek.push_fr0_y

offset_table:
		dta		<paddl0
		dta		<stick0
		dta		<ptrig0
		dta		<strig0
.endp

;===========================================================================
.proc funBitwiseSetup
		jsr		ExprConvFR0Int
		jsr		fmove
		jmp		expPopFR0Int
.endp

.proc funBitwiseAnd
		jsr		funBitwiseSetup
		and		fr1+1
		tay
		txa
		and		fr1
finish:
		sty		fr0+1
		jmp		MathWordToFP_FR0Hi_A
.endp

.proc funBitwiseOr
		jsr		funBitwiseSetup
		ora		fr1+1
		tay
		txa
		ora		fr1
		jmp		funBitwiseAnd.finish
.endp

.proc funBitwiseXor
		jsr		funBitwiseSetup
		eor		fr1+1
		tay
		txa
		eor		fr1
		jmp		funBitwiseAnd.finish
.endp

;===========================================================================
.echo "- Function module length: ",*-?statements_start
