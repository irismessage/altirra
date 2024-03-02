; Altirra BASIC - Execution control module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

;===========================================================================
; Input:
;	P.C		Set to run in direct mode, clear to run in normal mode
;
.proc exec				
		;reset error number
		mvy		#1 errno

		;fix the runtime stack to addresses
		php
		jsr		ExecFixStack
		plp

		;if we're running in direct mode, stmcur is already set and we
		;should bypass changing that and checking for line 32768.		
		bcs		direct_bypass
		
		mwa		stmtab stmcur
		
		;reset DATA pointer
		jsr		execRestore

		;skip line number, but stash statement length
new_line:

		;##TRACE "Processing line: $%04X (line=%d)" dw(stmcur) dw(dw(stmcur))
		;##ASSERT dw(stmcur) != 32768
		;##ASSERT dw(stmcur)>=dw(stmtab) and dw(stmcur)<dw(starp)

		;check line number and make sure we're not executing the immediate
		;mode line
		ldy		#1
		lda		(stmcur),y
		bpl		direct_bypass

		;hitting the end of immediate mode does an implicit END
		jmp		stEnd

direct_bypass:
		ldy		#2

		;stash statement length
		mva		(stmcur),y+ exLineEnd
loop:
		;check if break has been pressed
		lda		brkkey
		bpl		execStop

		;check if we are at EOL
		cpy		exLineEnd
		bcs		next_line

		;stash statement offset
		lda		(stmcur),y
		sta		exLineOffsetNxt
		iny

		;get statement token
		lda		(stmcur),y
		;##TRACE "Processing statement: $%04X+$%04x -> %y (line=%d)" dw(stmcur) y db(statementJumpTableLo+a)+db(statementJumpTableHi+a)*256+1 dw(dw(stmcur))
		iny
		sty		exLineOffset
		
		tax
		jsr		dispatch
		
		;skip continuation or EOL token
		ldy		exLineOffsetNxt
		jmp		loop

next_line:
		;check if we're at the immediate mode line already
		ldy		#1
		lda		(stmcur),y
		bmi		hit_end

		;refetch EOL (saves a few bytes with CONT)
		iny
		lda		(stmcur),y

		;bump statement pointer
		ldx		#stmcur
		jsr		VarAdvancePtrX
		jmp		new_line

hit_end:
		jmp		execLoop
		
dispatch:
		;push statement address onto stack
		lda		statementJumpTableHi,x
		pha
		lda		statementJumpTableLo,x
		pha
		
		;execute the statement
		rts
.endp

;===========================================================================
.proc execStop
		mva		#$80 brkkey
		sta		errno

		;load current line number
		ldx		stmcur
		ldy		stmcur+1
		jsr		fld0r

		jmp		errorDispatch
.endp

;===========================================================================
.macro STATEMENT_JUMP_TABLE
		;$00
		dta		:1[stRem-1]
		dta		:1[stData-1]
		dta		:1[stInput-1]
		dta		:1[stColor-1]
		dta		:1[stList-1]
		dta		:1[stEnter-1]
		dta		:1[stLet-1]
		dta		:1[stIf-1]
		dta		:1[stFor-1]
		dta		:1[stNext-1]
		dta		:1[stGoto-1]
		dta		:1[stGoto2-1]
		dta		:1[stGosub-1]
		dta		:1[stTrap-1]
		dta		:1[stBye-1]
		dta		:1[stCont-1]
		
		;$10
		dta		:1[stCom-1]
		dta		:1[stClose-1]
		dta		:1[stClr-1]
		dta		:1[stDeg-1]
		dta		:1[stDim-1]
		dta		:1[stEnd-1]
		dta		:1[stNew-1]
		dta		:1[stOpen-1]
		dta		:1[stLoad-1]
		dta		:1[stSave-1]
		dta		:1[stStatus-1]
		dta		:1[stNote-1]
		dta		:1[stPoint-1]
		dta		:1[stXio-1]
		dta		:1[stOn-1]
		dta		:1[stPoke-1]
		dta		:1[stPrint-1]
		dta		:1[stRad-1]
		dta		:1[stRead-1]
		dta		:1[stRestore-1]
		dta		:1[stReturn-1]
		dta		:1[stRun-1]
		dta		:1[stStop-1]
		dta		:1[stPop-1]
		dta		:1[stQuestionMark-1]
		dta		:1[stGet-1]
		dta		:1[stPut-1]
		dta		:1[stGraphics-1]
		dta		:1[stPlot-1]
		dta		:1[stPosition-1]
		dta		:1[stDos-1]
		dta		:1[stDrawto-1]
		dta		:1[stSetcolor-1]
		dta		:1[stLocate-1]
		dta		:1[stSound-1]
		dta		:1[stLprint-1]
		dta		:1[stCsave-1]
		dta		:1[stCload-1]
		dta		:1[stImpliedLet-1]
		dta		:1[stSyntaxError-1]
		dta		:1[stRem-1]
		dta		:1[stRem-1]
		dta		:1[stRem-1]
		dta		:1[stRem-1]
		dta		:1[stRem-1]
		dta		:1[stRem-1]
		dta		:1[stDpoke-1]
		dta		:1[stRem-1]

		;$40
		dta		:1[stRem-1]
		dta		:1[stRem-1]
		dta		:1[stRem-1]
		dta		:1[stBput-1]
		dta		:1[stBget-1]
		dta		:1[stRem-1]
		dta		:1[stCp-1]
		dta		:1[stFileOp-1]
		dta		:1[stFileOp-1]
		dta		:1[stFileOp-1]
		dta		:1[stDir-1]
		dta		:1[stFileOp-1]
		dta		:1[stMove-1]
.endm

statementJumpTableLo:
		STATEMENT_JUMP_TABLE	<

statementJumpTableHi:
		STATEMENT_JUMP_TABLE	>

;===========================================================================
; Input:
;	A:X		Line number (integer)
;
; Output:
;	Y:A		Line address
;	P.C		Set if found, unset otherwise
;
; If the line is not found, the insertion point for the line is returned
; instead.
;
.proc exFindLineInt
_lptr = iterPtr
		;search for line
		stx		fr0
		sta		fr0+1
		
		;check if line is >= current line -- if so start there
		txa
		ldx		#0
		cmp		(stmcur,x)
		ldy		#1
		lda		fr0+1
		sbc		(stmcur),y
		bcc		use_start
		dta		{bit $0100}		;leave X=0 (stmcur-stmtab)
use_start:
		ldx		#$fe
		
		;load pointer
		lda.b	stmcur+1,x
		sta		_lptr+1
		lda.b	stmcur,x

		ldx		#0
search_loop:
		sta		_lptr
		lda		(_lptr),y		;load current lineno hi
		cmp		fr0+1			;subtract desired lineno hi
		bcc		next			;cur_hi < desired_hi => next line
		bne		not_found		;cur_hi > desired_hi => not found
		lda		(_lptr,x)
		cmp		fr0
		bcs		at_or_past
next:
		iny
		lda		_lptr
		adc		(_lptr),y
		dey
		bcc		search_loop
		inc		_lptr+1
		bne		search_loop		;!! - unconditional jump
		
at_or_past:
		beq		found
not_found:
		clc
found:
		lda		_lptr
		ldy		_lptr+1
.def :ExNop
		rts
.endp

;===========================================================================
; Check if the current token is end of statement/line.
;
; Output:
;	Y = current line offset
;	A = current token
;	P.Z = set if end of statement/line
;
.proc ExecTestEnd
		ldy		exLineOffset
		lda		(stmcur),y
		cmp		#TOK_EOS
		beq		is_end
		cmp		#TOK_EOL
is_end:
		rts
.endp

;===========================================================================
.proc ExecGetComma
		ldy		exLineOffset
		inc		exLineOffset
		lda		(stmcur),y
		cmp		#TOK_EXP_COMMA
		rts
.endp

;===========================================================================
.proc execRestore
		lda		#0
		sta		dataln
		sta		dataln+1
		sta		dataptr+1
		rts
.endp

;===========================================================================
.proc ExecReset		
		;close IOCBs 1-7
		ldx		#$70
close_loop:
		jsr		IoCloseX
		txa
		sec
		sbc		#$10
		tax
		bne		close_loop

		;silence all sound channels
		ldx		#7
		sta:rpl	$d200,x-		;!! - A=0 from above

		rts
.endp

;===========================================================================
.proc ExecFixStack
		bit		exFloatStk
		bpl		xit
		lsr		exFloatStk

		lda		memtop2
		pha
		lda		memtop2+1
		pha
		bne		loop_start
loop:
		lda		#$fc
		jsr		stPop.dec_ptr_2

		;fetch the line number
		ldy		#1
		lda		(memtop2),y
		tax
		iny
		lda		(memtop2),y

		;do line lookup
		jsr		exFindLineInt
		bcc		not_found
		ldx		iterPtr+1

		ldy		#1
		jsr		stFor.push_ax

		;advance pointer
		jsr		stPop.pop_frame_remainder

loop_start:
		;check if we're at the bottom of the stack
		jsr		stReturn.check_rtstack_empty
		bcc		loop

done:
		pla
		sta		memtop2+1
		pla
		sta		memtop2
xit:
		rts

not_found:
		jmp		errorGOSUBFORGone
.endp

;===========================================================================
.proc ExecFloatStack
		bit		exFloatStk
		bmi		ExecFixStack.xit
		sec
		ror		exFloatStk

		lda		memtop2
		pha
		lda		memtop2+1
		pha
loop:
		;check if we're at the bottom of the stack
		jsr		stReturn.check_rtstack_empty
		bcs		ExecFixStack.done

		lda		#$fc
		jsr		stPop.dec_ptr_2

		;fetch the line address
		ldy		#2
		lda		(memtop2),y
		sta		fr0+1
		dey
		lda		(memtop2),y
		sta		fr0

		;fetch the line number
		lda		(fr0),y
		tax
		dey
		lda		(fr0),y

		iny
		jsr		stFor.push_ax

		;advance pointer
		jsr		stPop.pop_frame_remainder
		jmp		loop
.endp
