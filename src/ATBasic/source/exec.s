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
		ldx		#$ff
		txs
		mwa		runstk memtop2
				
		;reset error number
		mvy		#1 errno

		;if we're running in direct mode, stmcur is already set and we
		;should bypass changing that and checking for line 32768.		
		iny
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
		spl:jmp	immediateMode
		iny
		
direct_bypass:
		;check if break has been pressed
		lda		brkkey
		bmi		nobreak
		jmp		execStop
nobreak:
		
		;stash statement length
		mva		(stmcur),y+ exLineEnd
loop:
		;check if we are at EOL
		cpy		exLineEnd
		bcs		next_line

		;stash statement offset
		lda		(stmcur),y
		sta		exLineOffsetNxt
		iny

		;get statement token
		;##TRACE "Processing statement: $%04X+$%04x -> %y (line=%d)" dw(stmcur) y dw(statementJumpTable+2*db(dw(stmcur)+y))+1 dw(dw(stmcur))
		lda		(stmcur),y+
		sty		exLineOffset
		
		asl
		tax
		jsr		dispatch
		
		;skip continuation or EOL token
		ldy		exLineOffsetNxt
		jmp		loop
next_line:
		;bump statement pointer
		tya
next_line_2:
		ldx		#stmcur
		jsr		VarAdvancePtrX
		
		;check that we're not at the end
		cmp		starp
		lda		stmcur+1
		sbc		starp+1
		bcc		new_line
		jmp		execLoop
		
dispatch:
		;push statement address onto stack
		lda		statementJumpTable+1,x
		pha
		lda		statementJumpTable,x
		pha
		
		;execute the statement
		rts
.endp

;===========================================================================
.proc execStop
		mva		#$80 brkkey

		;load current line number
		ldx		stmcur
		ldy		stmcur+1
		jsr		fld0r
		
		;if we were in immediate mode, jump to READY prompt
		lda		fr0+1
		spl:jmp	immediateMode

		ldx		#0
		stx		iocbidx
		jsr		imprint
		dta		$9B,c"STOPPED AT LINE ",0
		
		jsr		IoPrintInt
		jsr		IoPutNewline
		
		jmp		execLoop.loop2
.endp

;===========================================================================
statementJumpTable:
		;$00
		dta		a(stRem-1)
		dta		a(stData-1)
		dta		a(stInput-1)
		dta		a(stColor-1)
		dta		a(stList-1)
		dta		a(stEnter-1)
		dta		a(stLet-1)
		dta		a(stIf-1)
		dta		a(stFor-1)
		dta		a(stNext-1)
		dta		a(stGoto-1)
		dta		a(stGoto2-1)
		dta		a(stGosub-1)
		dta		a(stTrap-1)
		dta		a(stBye-1)
		dta		a(stCont-1)
		
		;$10
		dta		a(stCom-1)
		dta		a(stClose-1)
		dta		a(stClr-1)
		dta		a(stDeg-1)
		dta		a(stDim-1)
		dta		a(stEnd-1)
		dta		a(stNew-1)
		dta		a(stOpen-1)
		dta		a(stLoad-1)
		dta		a(stSave-1)
		dta		a(stStatus-1)
		dta		a(stNote-1)
		dta		a(stPoint-1)
		dta		a(stXio-1)
		dta		a(stOn-1)
		dta		a(stPoke-1)
		dta		a(stPrint-1)
		dta		a(stRad-1)
		dta		a(stRead-1)
		dta		a(stRestore-1)
		dta		a(stReturn-1)
		dta		a(stRun-1)
		dta		a(stStop-1)
		dta		a(stPop-1)
		dta		a(stQuestionMark-1)
		dta		a(stGet-1)
		dta		a(stPut-1)
		dta		a(stGraphics-1)
		dta		a(stPlot-1)
		dta		a(stPosition-1)
		dta		a(stDos-1)
		dta		a(stDrawto-1)
		dta		a(stSetcolor-1)
		dta		a(stLocate-1)
		dta		a(stSound-1)
		dta		a(stLprint-1)
		dta		a(stCsave-1)
		dta		a(stCload-1)
		dta		a(stImpliedLet-1)
		dta		a(stSyntaxError-1)

;===========================================================================
; Input:
;	A0 (FR0)	Line number (integer)
;	A		Destination pointer
;
; Output:
;	(X)		Line address
;	P.C		Set if found, unset otherwise
;
; If the line is not found, the insertion point for the line is returned
; instead.
;
.proc exFindLineInt
_lptr = iterPtr
		;search for line
		pha
		
		;check if line is >= current line -- if so start there
		ldx		#0
		ldy		#0
		lda		fr0
		cmp		(stmcur),y
		iny
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
		sta		_lptr			;!! must enter loop with this in A
		bne		search_loop_start

search_loop:
		lda		(_lptr),y		;load current lineno hi
		cmp		fr0+1			;subtract desired lineno hi
		bcc		next			;cur_hi < desired_hi => next line
		bne		not_found		;cur_hi > desired_hi => not found
		dey
		lda		fr0
		cmp		(_lptr),y
		bcc		not_found
		beq		found
next:
		ldy		#2
		lda		(_lptr),y
		dey
		jsr		VarAdvancePtr
search_loop_start:
		cmp		starp
		lda		_lptr+1
		sbc		starp+1
		bcc		search_loop
		
		;whoops... hit the end!
not_found:
		clc
found:
		pla
		tax
		mwa		_lptr 0,x
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
.proc execRestore
		lda		#0
		sta		dataln
		sta		dataln+1
		sta		dataptr+1
		rts
.endp

;===========================================================================
.proc ExecReset
		;silence all sound channels
		ldx		#7
		lda		#0
		sta:rpl	$d200,x-
		
		;close IOCBs 1-7
		ldx		#$70
close_loop:
		jsr		IoCloseX
		txa
		sec
		sbc		#$10
		tax
		bne		close_loop
		rts
.endp
