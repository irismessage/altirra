; Altirra BASIC - Expression evaluator module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

;===========================================================================
; We try to match Atari BASIC's stack here as much as possible.
;
; Argument stack format:
;
;	Numeric constant:		00 00 <value,6>
;	Var-sourced number:		00 <varidx> <value,6>
;	String constant:		80 00 <addr,2> <len,2> 00 00
;

;===========================================================================
.proc ExprBeginPushStringVal
		ldy		argsp
		lda		#$83
.def :ExprPushRawByteAsWord = *
		sta		(argstk),y+
		mva		#0 (argstk),y+
		rts
.endp

;===========================================================================
ExprSkipCommaAndEvalPopInt:
		inc		exLineOffset
.proc	evaluateInt
		jsr		evaluate
		jmp		expPopFR0Int
.endp

;===========================================================================
.proc evaluateHashIOCBOpt
		;default to IOCB #0
		lda		#0
		sta		iocbidx
		
		;check if we have an IOCB
		ldy		exLineOffset
		lda		(stmcur),y
		cmp		#TOK_EXP_HASH
		bne		valid_iocb

.def :evaluateHashIOCB = *
		jsr		evaluateHashIOCBNoCheckOpen

		;okay, now we need to check if this IOCB is actually open
		tax
		ldy		ichid,x
		bpl		valid_iocb_2
		
		;force an IOCB not open error
		lda		#$85
		sta		errno
		jmp		errorDispatch
		
valid_iocb_2:
		lda		#0		;set Z=1 to indicate #iocb found
valid_iocb:
		rts
.endp

;===========================================================================
.proc	evaluateHashIOCBNoCheckOpen
		jsr		ExprSkipCommaAndEvalPopIntPos
		lda		fr0+1
		bne		invalid_iocb
		lda		fr0
		cmp		#8
		bcc		plausible_iocb
invalid_iocb:
		jmp		errorBadDeviceNo
		
plausible_iocb:
		asl
		asl
		asl
		asl
		sta		iocbidx
		rts
.endp

;===========================================================================
.proc	evaluateAssignment
		lda		#$ff
		jmp		evaluate._assign_entry
.endp

;===========================================================================
ExprSkipCommaAndEvalVar
		inc		exLineOffset
.proc	evaluateVar
		jsr		evaluate
		jmp		expPopVar
.endp

;===========================================================================
.proc	evaluate
_tmpadr = fr0+1

		;set up rvalue context
		lda		#0
_assign_entry = *
		sta		expAsnCtx
		
		;;##TRACE "Beginning evaluation at $%04x+$%02x = $%04x" dw(stmcur) db(exLineOffset) dw(stmcur)+db(exLineOffset)

		;reset stack pointers
		ldy		#$ff
		sty		opsp
		iny
		sty		argsp
		sty		expCommas
loop:
		;get next token
		ldy		exLineOffset
		inc		exLineOffset
		lda		(stmcur),y
		;;##TRACE "Processing token: $%02x ($%04x+$%02x=$%04x)" (a) dw(stmcur) y dw(stmcur)+y
		
		;check if this token needs to be reduced immediately
		bmi		is_imm
		cmp		#$10
		bcs		not_imm
is_imm:
		jsr		dispatch
		;##ASSERT (db(argsp)&7)=0
		jmp		loop
not_imm:
			
		;==== reduce loop ====
				
		;reduce while precedence of new operator is equal or lower than
		;precedence of last operator on stack
		sta		expCurOp
		
		;get push-on / shift precedence
		jsr		ExprGetPrecedence
		lsr
		and		#$55

		sta		expCurPrec
		;;##TRACE "Current operator get-on precedence = $%02x" a

reduce_loop:		
		ldy		opsp
		iny
		beq		reduce_done
		lda		(argstk),y
		
		;get pull-off/reduce precendence
		jsr		ExprGetPrecedence
		and		#$55
		
		;stop reducing if the current operator has higher precedence
		;;##TRACE "Checking precedence: tos $%02x vs. cur $%02x" a db(expCurPrec)
		cmp		expCurPrec
		bcc		reduce_done
		
		lda		(argstk),y
		sty		opsp
		jsr		dispatch
		;##ASSERT (db(argsp)&7)=0
		jmp		reduce_loop
reduce_done:
		;exit if this is not an expression token
		lda		expCurPrec
		beq		done
		
		;push current operator on stack
		ldy		opsp
		lda		expCurOp
		;;##TRACE "Shift: $%02x" (a)
		sta		(argstk),y
		dey
		sty		opsp	
		jmp		loop
done:	
		;;##TRACE "Exiting evaluator"
		dec		exLineOffset
		rts
		
dispatch:
		;;##TRACE "Reduce: $%02x (%y) - %u values on stack (%g %g)" (a) db(functionDispatchTableLo-14+a)+256*db(functionDispatchTableLo-14+a)+1 db(argsp)/8 dw(argstk)+db(argsp)-14 dw(argstk)+db(argsp)-6
		tax
		bmi		is_variable
		lda		functionDispatchTableHi-$0e,x
		pha
		lda		functionDispatchTableLo-$0e,x
		pha
		ldy		argsp
		rts
		
is_variable:
		;get value address of variable
		jsr		VarGetAddr0
		
		;check if this is the first var at the base -- if so, set the
		;lvalue ptr for possible assignment
		ldy		argsp
		bne		not_lvalue
		
		clc
		lda		varptr
		adc		#2
		sta		lvarptr
		lda		varptr+1
		adc		#0
		sta		lvarptr+1
		
not_lvalue:

		;push variable entry from VNTP onto argument stack

		;load variable
		jsr		VarLoadExtendedFR0
		
		;check if we had an array or string		
		;;##TRACE "arg %02x %02x %02x %02x" db(dw(argstk)+0) db(dw(argstk)+1) db(dw(argstk)+2) db(dw(argstk)+3)
		cmp		#$40
		bcc		not_relative_arraystr
		
		;check if we have a relative pointer
		and		#$02
		bne		not_relative_arraystr
		
		;it's relative -- convert relative pointer to absolute
		;;##TRACE "Converting to absolute"
		lda		prefr0
		adc		#$01				;!! carry is set here, and this clears carry
		sta		prefr0
		lda		fr0
		adc		starp
		sta		fr0
		lda		fr0+1
		adc		starp+1
		sta		fr0+1
not_relative_arraystr:

		;push variable onto argument stack and exit
		jmp		ExprPushExtFR0
.endp

;===========================================================================
; Precedence tables
;
; There are two precedences for each operator, a go-on and come-off
; precedence. A reduce happens if prec_on(cur) <= prec_off(tos); a
; shift happens otherwise. A prec_on of zero also terminates evaluation
; after the entire stack is reduced.
;
; For values, prec_on and prec_off are always the same high value, always
; forcing a shift and an immediate reduce.
;
; For arithmetic operators, prec_on <= prec_off for left associativity and
; prec_on > prec_off for right associativity.
;
; Parens and commas deserve special attention here. For the open parens
; operators, prec_on is high in order to force a shift and prec_off is
; low in order to stall reduction. For the close parens operators, prec_on
; is low to force a reduce immediately and prec_off is low so that nothing
; causes it to reduce except an open parens. In order to prevent a close
; parens from consuming more than one open parens, the close parens routine
; short-circuits the reduce loop, preventing any further reduction and
; preventing the close parens from being shifted onto the stack.
;

.proc	ExprGetPrecedence
		cmp		#$0e
		bcc		other
		cmp		#$3d
		scc:lda	#$3d
		tax
		lda		prectbl-$0e,x
		rts
other:
		lda		#$ff
		rts
.endp

.macro	_PREC
		dta [:1&8]*16+[:1&4]*8+[:1&2]*4+[:1&1]*2+[:2&8]*8+[:2&4]*4+[:2&2]*2+[:2&1]*1
.endm

.proc	prectbl
		;on, off
		_PREC	 0,0				;$0E	numeric constant
		_PREC	 0,0				;$0F	string constant
		_PREC	 0,0				;$10
		_PREC	 0,0				;$11
		_PREC	 0,0				;$12	,
		_PREC	 0,0				;$13	$
		_PREC	 0,0				;$14	: (statement end)
		_PREC	 0,0				;$15	;
		_PREC	 0,0				;$16	EOL
		_PREC	 0,0				;$17	goto
		_PREC	 0,0				;$18	gosub
		_PREC	 0,0				;$19	to
		_PREC	 0,0				;$1A	step
		_PREC	 0,0				;$1B	then
		_PREC	 0,0				;$1C	#
		_PREC	 8,8				;$1D	<=
		_PREC	 8,8				;$1E	<>
		_PREC	 8,8				;$1F	>=
		_PREC	 8,8				;$20	<
		_PREC	 8,8				;$21	>
		_PREC	 8,8				;$22	=
		_PREC	11,11				;$23	^
		_PREC	10,10				;$24	*
		_PREC	 9,9				;$25	+
		_PREC	 9,9				;$26	-
		_PREC	10,10				;$27	/
		_PREC	 7,7				;$28	not
		_PREC	 5,5				;$29	or
		_PREC	 6,6				;$2A	and
		_PREC	13,3				;$2B	(
		_PREC	 2,2				;$2C	)
		_PREC	 4,4				;$2D	= (numeric assignment)
		_PREC	 4,4				;$2E	= (string assignment)
		_PREC	12,12				;$2F	<= (strings)
		_PREC	12,12				;$30	<>
		_PREC	12,12				;$31	>=
		_PREC	12,12				;$32	<
		_PREC	12,12				;$33	>
		_PREC	12,12				;$34	=
		_PREC	11,11				;$35	+ (unary)
		_PREC	11,11				;$36	-
		_PREC	14,3				;$37	( (string left paren)
		_PREC	14,3				;$38	( (array left paren)
		_PREC	14,3				;$39	( (dim array left paren)
		_PREC	14,3				;$3A	( (fun left paren)
		_PREC	14,3				;$3B	( (dim str left paren)
		_PREC	 4,3				;$3C	, (array/argument comma)
		
		;$3D and on are functions
		_PREC	13,13
.endp

;===========================================================================
.proc	expPopVar
		lda		argsp
		;##ASSERT (a&7)=0 and a
		sub		#7
		tay
		lda		(argstk),y
		dey
		sty		argsp
		jmp		VarGetAddr0
.endp

;===========================================================================
.proc	ExprPopExtFR0
		ldx		#7
copyloop:
		dec		argsp
		ldy		argsp
		mva		(argstk),y prefr0,x
		dex
		bpl		copyloop
		rts
.endp

;===========================================================================
expPopFR1FR0:
		jsr		expPopFR1
.proc	expPopFR0
		ldy		argsp
		;##ASSERT (y&7)=0 and y
with_offset:
		ldx		#5
copyloop:
		dey
		mva		(argstk),y fr0,x
		dex
		bpl		copyloop
		dey
		dey
		sty		argsp
		rts
.endp

;===========================================================================
; Output:
;	A:X = integer value
;	P.N,Z = set from A
;
.proc	expPopFR0Int
		jsr		expPopFR0
		jsr		fpi
		bcs		fail
		ldx		fr0
		lda		fr0+1
		rts
fail:
		jmp		errorValueErr
.endp

;===========================================================================
; Output:
;	A:X = integer value
;	P.N,Z = set from A
;
ExprSkipCommaAndEvalPopIntPos:
		inc		exLineOffset
ExprEvalPopIntPos:
		jsr		evaluate
.proc	expPopFR0IntPos
		jsr		expPopFR0Int
		bmi		is_neg
		rts
is_neg:
		jmp		errorValue32K
.endp

;===========================================================================
.proc	expPopFR1
		ldy		argsp
		;##ASSERT (y&7)=0 and y
with_offset:
		ldx		#5
copyloop:
		dey
		mva		(argstk),y fr1,x
		dex
		bpl		copyloop
		dey
		dey
		sty		argsp
		rts
.endp

;===========================================================================
.proc	ExprPushExtFR0
		ldy		argsp
		ldx		#<-8
		bne		expPushFR0.copyloop
.endp

;===========================================================================
expPushFR0Int:
		jsr		ifp
.proc	expPushFR0
		ldy		argsp
		lda		#0
		jsr		ExprPushRawByteAsWord
		ldx		#-6
copyloop:
		mva		fr0+6,x (argstk),y+
		inx
		bne		copyloop
		sty		argsp
		rts
.endp

;===========================================================================
; Obtain absolute address of string on top of evaluation stack.
;
; Inputs:
;	String entry on eval TOS.
;
; Outputs:
;	FR0 = string data
;	VARPTR = ID bytes
;
ExprSkipCommaEvalAndPopString:
		inc		exLineOffset
ExprEvalAndPopString:
		jsr		evaluate
.proc	expPopAbsString
		;lower SP
		ldy		argsp
		;##ASSERT (y&7)=0 and y
		
		;copy upper 6 bytes to FR0
		ldx		#5
copy_loop:
		dey
		lda		(argstk),y
		sta		fr0,x
		dex
		bpl		copy_loop
		
		;copy first two string bytes to VARPTR
		dey
		lda		(argstk),y
		sta		varptr+1
		dey
		lda		(argstk),y
		sta		varptr
		
		sty		argsp
		
		;result should always be a string, and absolute if dimensioned
		;##ASSERT db(varptr)=$80 or db(varptr)=$82 or db(varptr)=$83
		rts
.endp

;===========================================================================
; Inputs:
;	X = zero page location to store FR0+1,FR0 to
;
.proc ExprStoreFR0Int
		mwa		fr0 0,x
		rts
.endp

;===========================================================================
.proc functionDispatchTableLo
		;$0E
		dta		<[expNumConst-1]
		dta		<[expStrConst-1]
		
		;$10
		dta		<[0]
		dta		<[0]
		dta		<[expComma-1]
		dta		<[0]
		dta		<[0]
		dta		<[0]
		dta		<[0]
		dta		<[0]
		dta		<[0]
		dta		<[0]
		dta		<[0]
		dta		<[0]
		dta		<[0]
		dta		<[funCompare-1]
		dta		<[funCompare-1]
		dta		<[funCompare-1]

		;$20
		dta		<[funCompare-1]
		dta		<[funCompare-1]
		dta		<[funCompare-1]
		dta		<[funPower-1]
		dta		<[funMultiply-1]
		dta		<[funAdd-1]
		dta		<[funSubtract-1]
		dta		<[funDivide-1]
		dta		<[funNot-1]
		dta		<[funOr-1]
		dta		<[funAnd-1]
		dta		<[funOpenParens-1]
		dta		<[0]
		dta		<[funAssignNum-1]
		dta		<[funAssignStr-1]
		dta		<[funStringCompare-1]

		;$30
		dta		<[funStringCompare-1]
		dta		<[funStringCompare-1]
		dta		<[funStringCompare-1]
		dta		<[funStringCompare-1]
		dta		<[funStringCompare-1]
		dta		<[funUnaryPlus-1]
		dta		<[funUnaryMinus-1]
		dta		<[funArrayStr-1]
		dta		<[funArrayNum-1]
		dta		<[funDimArray-1]
		dta		<[funOpenParens-1]
		dta		<[funDimStr-1]
		dta		<[funArrayComma-1]
		
		;$3D
		dta		<[funStr-1]
		dta		<[funChr-1]
		dta		<[funUsr-1]

		;$40
		dta		<[funAsc-1]
		dta		<[funVal-1]
		dta		<[funLen-1]
		dta		<[funAdr-1]
		dta		<[funAtn-1]
		dta		<[funCos-1]
		dta		<[funPeek-1]
		dta		<[funSin-1]
		dta		<[funRnd-1]
		dta		<[funFre-1]
		dta		<[funExp-1]
		dta		<[funLog-1]
		dta		<[funClog-1]
		dta		<[funSqr-1]
		dta		<[funSgn-1]
		dta		<[funAbs-1]
		
		;$50
		dta		<[funInt-1]
		dta		<[funPaddle-1]
		dta		<[funStick-1]
		dta		<[funPtrig-1]
		dta		<[funStrig-1]
.endp

.proc functionDispatchTableHi
		;$0E
		dta		>[expNumConst-1]
		dta		>[expStrConst-1]
		
		;$10
		dta		>[0]
		dta		>[0]
		dta		>[expComma-1]
		dta		>[0]
		dta		>[0]
		dta		>[0]
		dta		>[0]
		dta		>[0]
		dta		>[0]
		dta		>[0]
		dta		>[0]
		dta		>[0]
		dta		>[0]
		dta		>[funCompare-1]
		dta		>[funCompare-1]
		dta		>[funCompare-1]

		;$20
		dta		>[funCompare-1]
		dta		>[funCompare-1]
		dta		>[funCompare-1]
		dta		>[funPower-1]
		dta		>[funMultiply-1]
		dta		>[funAdd-1]
		dta		>[funSubtract-1]
		dta		>[funDivide-1]
		dta		>[funNot-1]
		dta		>[funOr-1]
		dta		>[funAnd-1]
		dta		>[funOpenParens-1]
		dta		>[0]
		dta		>[funAssignNum-1]
		dta		>[funAssignStr-1]
		dta		>[funStringCompare-1]

		;$30
		dta		>[funStringCompare-1]
		dta		>[funStringCompare-1]
		dta		>[funStringCompare-1]
		dta		>[funStringCompare-1]
		dta		>[funStringCompare-1]
		dta		>[funUnaryPlus-1]
		dta		>[funUnaryMinus-1]
		dta		>[funArrayStr-1]
		dta		>[funArrayNum-1]
		dta		>[funDimArray-1]
		dta		>[funOpenParens-1]
		dta		>[funDimStr-1]
		dta		>[funArrayComma-1]
		
		;$3D
		dta		>[funStr-1]
		dta		>[funChr-1]
		dta		>[funUsr-1]

		;$40
		dta		>[funAsc-1]
		dta		>[funVal-1]
		dta		>[funLen-1]
		dta		>[funAdr-1]
		dta		>[funAtn-1]
		dta		>[funCos-1]
		dta		>[funPeek-1]
		dta		>[funSin-1]
		dta		>[funRnd-1]
		dta		>[funFre-1]
		dta		>[funExp-1]
		dta		>[funLog-1]
		dta		>[funClog-1]
		dta		>[funSqr-1]
		dta		>[funSgn-1]
		dta		>[funAbs-1]
		
		;$50
		dta		>[funInt-1]
		dta		>[funPaddle-1]
		dta		>[funStick-1]
		dta		>[funPtrig-1]
		dta		>[funStrig-1]
.endp
