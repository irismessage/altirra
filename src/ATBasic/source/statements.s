; Altirra BASIC - Statement module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

?statements_start = *

;===========================================================================
.proc stRem
		;remove return address
		pla
		pla
		
		;warp to end of current line
		ldy		#2
		lda		(stmcur),y
		jmp		exec.next_line_2
.endp


;===========================================================================
stData = stRem

;===========================================================================
; INPUT [#aexp{,|;}] var [,var...]
;
; Reads input from the console or an I/O channel.
;
; If the IOCB is #0, either implicitly or explicitly, a question mark is
; printed to the console first. Whether the IOCB number is followed by
; a comma or semicolon doesn't matter.
;
; Leading spaces are included for string input and skipped for numeric
; input. Numeric input is considered invalid if the immediately following
; character is not a comma or a string of spaces (spaces followed by
; a comma is _not_ accepted). Numeric inputs may not be empty -- either
; a blank line or no input after a comma will cause a numeric input to
; fail.
;
; When multiple variables are supplied, subsequent variables are read in
; from the same line if more values are available as a comma separated
; list. If not, a new line is read in. String reads eat the comma as part
; of the string and always force a new line.
;
; The default line buffer is always used (255 bytes), even if a larger
; string buffer is supplied. The string is silently truncated as necessary.
; EOLs are not placed into string arrays. If the string array is not
; dimensioned, error 9 is issued, but only when that string array is
; reached and after input has been read.
;
; End of file gives an EOF error.
;
.proc stInput
		;parse optional #iocb and set iocbidx
		jsr		evaluateHashIOCBOpt
		bne		no_iocb
		
		;eat following comma or semicolon
		inc		exLineOffset
no_iocb:
		jmp		DataRead
.endp


;===========================================================================
.proc stColor
		jsr		evaluateInt
		mva		fr0 grColor
		rts
.endp

;===========================================================================
; ENTER filespec
;
; Loads lines from a file and executes them. The lines are executed as-is;
; if there are any immediate mode commands without a line number, they are
; executed immediately.
;
; Note that ENTER uses the same IOCB (#7) that the RUN, LOAD, and SAVE
; commands use. RUN and LOAD terminate execution, so no problem there,
; but SAVE causes the ENTER process to crap out with an I/O error. The
; SAVE itself *does* succeed, which means that SAVE must close IOCB #7
; first.
;
.proc stEnter
_vectmp = $0500

		;Use IOCB #7 for compatibility with Atari BASIC
		jsr		IoSetupIOCB7
		
		;get filename
		jsr		ExprEvalAndPopString
		
		;set open mode to read
		mva		#4 icax1+$70
		
		;issue open call
		jsr		IoDoOpenWithFilename

		;set exec IOCB to #7
		mvx		#$70 iocbexec
		
		;restart execution loop
		jmp		execLoop.loop2
.endp


;===========================================================================
stLet = evaluateAssignment

;===========================================================================
; IF aexp THEN {lineno | statement [:statement...]}
;
; Evaluates aexp and checks whether it is non-zero. If so, the THEN clause
; is executed. Otherwise, execution proceeds at the next line (NOT the next
; statement). aexp is evaluated in float context so it may be negative or
; >65536.
;
; The token setup for the THEN clause is a bit wonky. If lineno is present,
; it shows up as an expression immediately after the THEN token, basically
; treating the THEN token as an operator. Otherwise, the statement abruptly
; ends at the THEN with no end of statement token and a new statement
; follows.
;
.proc stIf
		;evaluate condition
		jsr		evaluate
		jsr		expPopFR0
		
		;check if it is zero
		lda		fr0
		;;##TRACE "If condition: %g" fr0
		bne		non_zero
		
		;condition is false... skip the line
		jmp		stRem
		
non_zero:
		;skip the THEN token, which is always present
		;##ASSERT db(dw(stmcur)+db(exLineOffset)) = $1B
		inc		exLineOffset
		
		;check if this is the end of the statement
		ldy		exLineOffset
		cpy		exLineOffsetNxt
		beq		statement_follows
		
		;no, it isn't... process the implicit GOTO.
		jmp		stGoto
		
statement_follows:
		;a statement follows, so execute it
		rts
.endp


;===========================================================================
.proc stFor
		;get and save variable
		lda		(stmcur),y+
		pha
		
		;skip equals
		iny
		sty		exLineOffset
		
		;evaluate initial loop value and store in variable
		jsr		evaluate
		pla
		pha
		jsr		varStoreArgStk
		
		;skip the TO keyword
		inc		exLineOffset
		
		;evaluate stop value and push
		jsr		evalAndPush
		
		;check for a STEP keyword
		ldy		exLineOffset
		lda		(stmcur),y
		cmp		#$1a
		bne		no_step
		
		;skip STEP keyword
		inc		exLineOffset
		
		;evaluate and store step
		jsr		evalAndPush
		jmp		had_step
no_step:
		;set step to 1.0 and push on stack
		ldx		#$fa
		ldy		#0
oneloop:
		lda		const_one-$fa,x
		sta		(memtop2),y
		inw		memtop2
		inx
		bne		oneloop
had_step:

		;push frame and exit
		pla
pushFrame:
		ldx		#0
		jsr		pushByte
		lda		stmcur
		jsr		pushByte
		lda		stmcur+1
		jsr		pushByte
		lda		exLineOffsetNxt
		jsr		pushByte
		;##ASSERT dw(runstk)<=dw(memtop2) and !((dw(memtop2)-dw(runstk))&3) and (db(dw(memtop2)-4)=0 or db(dw(memtop2)-4)>=$80) and dw(dw(memtop2)-3) >= dw(stmtab)
		rts
		
evalAndPush:
		jsr		evaluate
pushFR1:
		ldx		#0
		ldy		#2
		mva		#6 fr1
loop1:
		lda		(argstk),y
		iny
		jsr		pushByte
		dec		fr1
		bne		loop1
		rts
pushByte:
		sta		(memtop2,x)
		inw		memtop2
		rts
.endp


;===========================================================================
; NEXT avar
;
; Closes a FOR loop, checks the loop condition, and loops back to the FOR
; statement if the loop is still active. The runtime stack is search for
; the appropriate matching FOR; any other FOR frames found in between are
; discarded. If a GOSUB frame is found first, error 13 is issued.
;
; The step factor is added to the loop variable before a check occurs. This
; means that a FOR I=1 TO 10:NEXT I will run ten times for I=[0..10] and
; exit with I=11. The check is > for a positive STEP and < for a negative
; STEP; the loop will terminate if the loop variable is manually modified
; to be beyond the end value. A FOR I=0 TO 0 STEP 0 loop will not normally
; terminate, but is considered positive step and will stop if I is modified
; to 1 or greater.
;
.proc stNext
		;pop entries off runtime stack until we find the right frame
loop:
		lda		runstk
		cmp		memtop2
		lda		runstk+1
		sbc		memtop2+1
		bcc		stack_not_empty
		
error:
		jmp		errorNoMatchingFOR
		
stack_not_empty:

		;pop back one frame
		lda		#$f0
		jsr		stPop.dec_ptr_2
		
		;check that it's a FOR
		ldy		#$0c
		lda		(memtop2),y
		beq		error
		
		;check that it's the right one
		ldy		exLineOffset
		cmp		(stmcur),y
		bne		loop
		
		;compute variable address
		jsr		VarGetAddr0
		
		;add step to variable
		jsr		VarLoadExtendedFR0
		ldy		memtop2+1
		lda		memtop2
		add		#6
		scc:iny
		tax
		jsr		fld1r
		
		;save off step sign
		mva		fr1 stScratch
		
		jsr		fadd
		jsr		VarStoreFR0
		
		;compare to end value
		ldx		memtop2
		ldy		memtop2+1
		jsr		fld1r
		
		;;##TRACE "NEXT: Checking %g <= %g" fr0 fr1
		jsr		fcomp
		
		;exit if current value is > termination value for positive step,
		;< termination value for negative step
		beq		not_done
		
		bit		stScratch
		bmi		negative_step
		
		bcc		not_done
		
loop_done:
		;skip variable and exit
		;;##TRACE "Terminating FOR loop"
		inc		exLineOffset
		rts
		
negative_step:
		bcc		loop_done

not_done:
		;warp to FOR end
		;;##TRACE "Continuing FOR loop"
		ldy		#$0d
		jsr		pop_frame
		
		;restore frame on stack and continue execution after for
		lda		#$10
		ldx		#memtop2
		jmp		VarAdvancePtrX
		
pop_frame:
		mva		(memtop2),y+ stmcur
		mva		(memtop2),y+ stmcur+1
		mva		(memtop2),y exLineOffsetNxt
		
		;fixup line info cache
		ldy		#2
		mva		(stmcur),y exLineEnd

		;##ASSERT dw(stmcur) >= dw(stmtab) and dw(stmcur) < dw(starp)
		;##ASSERT dw(memtop2) >= dw(runstk) and ((dw(memtop2)-dw(runstk))&3)=0
		;##ASSERT dw(memtop2) = dw(runstk) or db(dw(memtop2)-4)=0 or db(dw(memtop2)-4)>=$80
		rts
.endp


;===========================================================================
.proc stGoto
		;get line number
		jsr		evaluate
gotoTOS:
		jsr		expPopFR0Int
gotoFR0Int:
		lda		#stmcur
		jsr		exFindLineInt
		bcc		not_found
gotoFoundLineConfirmed:
		;jump to it
		pla
		pla
		jmp		exec.new_line
not_found:
		jmp		errorLineNotFound
.endp


;===========================================================================
stGoto2 = stGoto

;===========================================================================
.proc stGosub
		;get line number
		jsr		evaluate

		;push gosub frame
		;##TRACE "Pushing GOSUB frame: $%04x+$%02x" dw(stmcur) db(exLineOffset)
		lda		#0
		jsr		stFor.pushFrame
		
		;jump to new location
		;##ASSERT dw(runstk)<=dw(memtop2) and !((dw(memtop2)-dw(runstk))&3) and (db(dw(memtop2)-4)=0 or db(dw(memtop2)-4)>=$80) and dw(dw(memtop2)-3) >= dw(stmtab)
		jmp		stGoto.gotoTOS
.endp


;===========================================================================
.proc stTrap
		jsr		evaluateInt
		ldx		#exTrapLine
		jmp		ExprStoreFR0Int
.endp


;===========================================================================
.proc stBye
		ldx		#$ff
		txs
		jmp		blkbdv
.endp


;===========================================================================
.proc stCont
		;check if we are executing the immedate mode line
		ldy		#0
		lda		(stmcur),y
		bmi		stGoto.not_found
		rts
.endp

;===========================================================================
stCom = stDim

;===========================================================================
.proc stClose
		jsr		evaluateHashIOCBNoCheckOpen
		jmp		IoClose
.endp


;===========================================================================
; CLR
;
; Clears all numeric values to zero and un-dimensions any string and numeric
; arrays. The runtime stack is also cleared, so no FOR or GOSUB frames are
; left afterward.
;
.proc stClr
		lda		#0
		jsr		VarGetAddr0
		jsr		zfr0
		jmp		loopstart
clearloop:
		;clear variable info and value
		jsr		VarStoreFR0
		
		;clear dimensioned bits for arrays/strings
		ldy		#0
		lda		(varptr),y
		and		#$c0
		sta		(varptr),y
		
		;next variable
		lda		#8
		ldx		#varptr
		jsr		VarAdvancePtrX
loopstart:
		lda		varptr
		cmp		stmtab
		lda		varptr+1
		sbc		stmtab+1
		bcc		clearloop
		
		;empty the string/array table region and runtime stack		
		;note: this loop is reused by NEW!
		ldx		#<-4
reset_loop:
		lda		starp+4,x
		sta		runstk+4,x
		inx
		bne		reset_loop
		
		rts
.endp


;===========================================================================
.proc stDegRad
.def :stDeg = *
		lda		#6
		dta		{bit $0100}
.def :stRad = *
		lda		#0
		sta		degflg
done:
		rts
.endp

;===========================================================================
.proc stDim
loop:
		jsr		evaluate

		ldy		exLineOffset
		lda		(stmcur),y
		cmp		#TOK_EXP_COMMA
		bne		stDegRad.done
		inc		exLineOffset
		bcs		loop
.endp


;===========================================================================
stEnd = immediateModeReset

;===========================================================================
.proc stNew
		ldy		lomem+1
		ldx		lomem

		;reset trap line
		mva		#$80 exTrapLine+1
		
		;reserve a page for the argument stack
		iny
		
		;place variable name table above argument stack
		stx		vntp
		sty		vntp+1
		stx		vntd
		sty		vntd+1

		;reserve byte for sentinel in variable name table
		inx
		sne:iny
		
		;place variable value table
		stx		vvtp
		sty		vvtp+1

		;copy VVTP to STMTAB/STMCUR/STARP/RUNSTK/MEMTOP2
		ldx		#<-10
		jsr		stClr.reset_loop
		
		;write sentinel into variable name table
		txa
		tay
		sta		(vntp),y

		;all done
		jmp		immediateModeReset
.endp


;===========================================================================
; OPEN #iocb, aexp1, aexp2, filename
;
; Opens an I/O channel.
;
; Errors:
;	Error 7 if aexp1 or aexp2 in [32768, 65535]
;	Error 3 if aexp1 or aexp2 not in [0, 65535]
;
.proc stOpen
		;get #IOCB
		jsr		evaluateHashIOCBNoCheckOpen
		
		;get AUX1 and AUX2 bytes
		jsr		ExprSkipCommaAndEvalPopIntPos
		txa
		ldx		iocbidx
		sta		icax1,x
		
		jsr		ExprSkipCommaAndEvalPopIntPos
		txa
		ldx		iocbidx
		sta		icax2,x
		
		;get filename
		jsr		ExprSkipCommaEvalAndPopString
		mwa		fr0 icbal,x
				
		;issue open
		jmp		IoDoOpenWithFilename
.endp


;===========================================================================
; LOAD filespec
;
; Loads a BASIC program in binary format.
;
; Execution is reset prior to beginning the load.
;
.proc stLoad
_vectmp = fr0
_loadflg = stScratch
		sec
run_entry:
		ror		_loadflg
		
		;close IOCBs and silence audio
		jsr		ExecReset
		
		;Use IOCB #7 for compatibility with Atari BASIC
		ldx		#$70

withIOCB:
		;save IOCB #
		stx		iocbidx
		
		;pop filename
		jsr		ExprEvalAndPopString
				
		;do open
		ldx		iocbidx
		mva		#$04 icax1,x
		jsr		IoDoOpenWithFilename

with_open_iocb:
		;load vector table to temporary area (14 bytes)
		ldy		#CIOCmdGetChars
		jsr		setup_vector_io
		
		;check if first pointer is zero -- if not, assume bad file
		lda		_vectmp
		ora		_vectmp+1
		bne		bogus
		
		;relocate pointers
		ldx		#12
relocloop:
		lda		_vectmp,x
		add		lomem
		sta		lomem,x
		lda		_vectmp+1,x
		adc		lomem+1
		sta		lomem+1,x
		dex
		dex
		bne		relocloop
		
		;load remaining data at VNTP
		jsr		setup_main_io
		jsr		ioChecked
				
		;close IOCB
		jsr		IoClose
		
		;clear runtime variables
		jsr		stClr
		
		;check if we should run
		asl		_loadflg
		scs:jmp	exec
		
		;jump to immediate mode loop
		jmp		immediateMode
		
bogus:
		jmp		errorLoadError
		
setup_vector_io:
		mva		#<_vectmp icbal,x
		lda		#$00
		sta		icblh,x
		sta		icbah,x
		mva		#14 icbll,x
		tya
		jmp		IoDoCmdX

setup_main_io:
		ldx		iocbidx
		lda		vntp
		sta		icbal,x
		lda		vntp+1
		sta		icbah,x
		sbw		starp vntp icbll,x
		rts
.endp


;===========================================================================
; SAVE filespec
;
; It is possible to issue a SAVE command during ENTER processing. For this
; reason, we must close IOCB #7 before reopening it.
;
.proc stSave
_vectmp = fr0

		;Use IOCB #7 for compatibility with Atari BASIC
		;close it in case ENTER is active
		jsr		IoSetupIOCB7
		
		;get filename
		jsr		ExprEvalAndPopString
		
		;set open mode to write
		mva		#8 icax1+$70
		
		;issue open call
		jsr		IoDoOpenWithFilename
		
with_open_iocb:
		;set up and relocate pointers
		;note that this subtracts LOMEM from itself, producing an offset
		;of $0000 -- this is in fact required.
		ldx		#12
relocloop:
		sec
		lda		lomem,x
		sbc		lomem
		sta		_vectmp,x
		lda		lomem+1,x
		sbc		lomem+1
		sta		_vectmp+1,x
		dex
		dex
		bpl		relocloop
		
		;write vector table (14 bytes)
		ldy		#CIOCmdPutChars
		ldx		#$70
		jsr		stLoad.setup_vector_io
		
		;write from VNTP from STARP
		jsr		stLoad.setup_main_io
		jsr		ioChecked
		
		;close and exit
		jmp		IoClose
.endp


;===========================================================================
.proc stStatus
		jsr		evaluateHashIOCB
		jsr		ExprSkipCommaAndEvalVar
		
		lda		#CIOCmdGetStatus
		jsr		IoDoCmd
		ldx		iocbidx

		lda		icsta,x
		jmp		stGet.store_byte_to_var
.endp

;===========================================================================
; NOTE #iocb, avar, avar
;
.proc stNote
		;consume #iocb,
		jsr		evaluateHashIOCB
		inc		exLineOffset		;eat comma
		
		;issue XIO 38 to get current position
		ldx		iocbidx
		mva		#38 iccmd,x
		jsr		ioChecked
		
		;copy ICAX3/4 into first variable
		jsr		evaluateVar
		ldx		iocbidx
		mwa		icax3,x fr0
		jsr		ifp
		jsr		VarStoreFR0
		
		;copy ICAX5 into second variable
		jsr		ExprSkipCommaAndEvalVar
		ldx		iocbidx
		lda		icax5,x
		jsr		MathByteToInt		
		jmp		VarStoreFR0
.endp


;===========================================================================
; POINT #iocb, avar, avar
;
; Note that there is only one byte in the IOCB for the sector offset (AUX5);
; Atari BASIC silently drops the high byte without error.
;
.proc stPoint
		;consume #iocb,
		jsr		evaluateHashIOCB
		
		;consume comma and then first var, which holds sector number
		jsr		ExprSkipCommaAndEvalPopInt
		
		;move to ICAX3/4
		ldx		iocbidx
		mwa		fr0 icax3,x
		
		;consume comma and then second var, which holds sector offset
		jsr		ExprSkipCommaAndEvalPopInt
		
		;move to ICAX5
		txa
		ldx		iocbidx
		sta		icax5,x
		
		;issue XIO 37 and exit
		mva		#37 iccmd,x
		jmp		ioChecked
.endp


;===========================================================================
; XIO cmdno, #aexp, aexp1, aexp2, filespec
;
; Performs extended I/O to an IOCB.
;
; This issues a CIO call as follows:
;	ICCMD = cmdno
;	ICAX1 = aexp1
;	ICAX2 = aexp2
;	ICBAL/H = filespec
;
; Quirks:
;	- Neither AUX bytes are modified until all arguments are successfully
;	  evaluated.
;	- Because ICAX1 is modified and not restored even in the event of
;	  success, subsequent attempts to read from the channel can fail due
;	  to AUX1 permission checks in SIO. Writes can work because BASIC
;	  bypasses CIO and jumps directly to ICPTL/H+1, but reads still go
;	  through CIO. One symptom that occurs is LOCATE commands failing with
;	  Error 131 after filling with XIO 18,#6,0,0,"S".
;
;	  This is avoided if the handler itself restores AUX1 or aexp1 is set
;	  to the permission byte instead.
;
.proc stXio
		jsr		evaluateInt
		mva		fr0 iocbcmd
		inc		exLineOffset
		jsr		evaluateHashIOCBNoCheckOpen
		jsr		ExprSkipCommaAndEvalPopInt
		txa
		pha
		jsr		ExprSkipCommaAndEvalPopInt
		txa
		pha

		;get filename
		jsr		ExprSkipCommaEvalAndPopString
		
		ldx		iocbidx
		mwa		fr0 icbal,x
		pla
		sta		icax2,x
		pla
		sta		icax1,x
				
		;issue command
		lda		iocbcmd
		jmp		IoDoWithFilename
.endp


;===========================================================================
; ON aexp {GOTO | GOSUB} lineno [,lineno...]
;
; aexp is converted to integer with rounding, using standard FPI rules. The
; resulting integer is then used to select following line numbers, where
; 1 selects the first lineno, etc. Zero or greater than the number provided
; results in execution continuing with the next statement.
;
; The selection value and all line numbers up to that value must pass
; FPI conversion and be below 32768, or else errors 3 and 7 result,
; respectively. In addition, the selection value must be below 256 or
; error 3 results. If the selection value converts to 0, none of the line
; numbers are evaluated or checked, and if it is greater than the number
; provided, all are evaluated.
;
; Examples:
;	ON 1 GOTO 10, 20 (Jumps to line 10)
;	ON 2 GOTO 10, 20 (Jumps to line 20)
;	ON 3 GOTO 10, 20 (Continues execution)
;	ON -0.01 GOTO 10 (Error 3)
;	ON 255.5 GOTO 10 (Error 3)
;	ON 32768 GOTO 10 (Error 7)
;	ON 65536 GOTO 10 (Error 3)
;	ON 0 GOTO 1/0 (Continues execution)
;	ON 2 GOTO 1/0 (Error 11)
;	ON 1 GOTO 10,1/0 (Jumps to line 10)
;
stOn = _stOn._entry
.proc _stOn
_index = parout

fail_value_err:
		jmp		errorValueErr

_entry:
		;fetch and convert the selection value
		jsr		ExprEvalPopIntPos
				
		;next token should be GOTO or GOSUB
		ldy		exLineOffset
		inc		exLineOffset
		lda		(stmcur),y
		
		;save GOTO/GOSUB token
		pha
		
		;issue error 3 if it is greater than 255
		lda		fr0+1
		bne		fail_value_err
		
		lda		fr0
		beq		xit
		sta		_index

count_loop:
		;evaluate a line number
		jsr		ExprEvalPopIntPos
		
		;check if it's time to branch
		dec		_index
		beq		do_branch
		
		;read next token and check if it is a comma
		ldy		exLineOffset
		inc		exLineOffset
		lda		(stmcur),y
		cmp		#TOK_EXP_COMMA
		beq		count_loop
		
		;out of line numbers -- continue with next statement
xit:
		pla
		rts
		
not_found:
		jmp		errorLineNotFound
		
do_branch:
		lda		#fr0
		jsr		exFindLineInt
		bcc		not_found
		
		;warp to end of statement for GOSUB
		mvy		exLineOffsetNxt exLineOffset

		;retrieve GOTO/GOSUB token
		pla
		eor		#TOK_EXP_GOTO
		beq		is_goto
		
		;push gosub frame
		lda		#0
		jsr		stFor.pushFrame
		
is_goto:
		;jump to new location
		ldx		#stmcur
		jsr		ExprStoreFR0Int
		jmp		stGoto.gotoFoundLineConfirmed
.endp

;===========================================================================
.proc stPoke
		;evaluate address
		jsr		evaluateInt
		
		;save address
		lda		fr0+1
		sta		stScratch+1
		lda		fr0
		sta		stScratch
		
		;skip comma and evaluate value
		jsr		ExprSkipCommaAndEvalPopInt
		
		;do poke
		;;##TRACE "POKE %u,%u" dw(fr0+1) db(fr0)
		ldy		#0
		txa
		sta		(stScratch),y
		
		;done
		rts
.endp

;===========================================================================
;
; A comma causes movement to the next tab stop, where tab stops are 10
; columns apart. These are independent from the E: tab stops. Position
; relative to the tab stops is determined by the number of characters
; output since the beginning of the PRINT statement and is independent of
; the actual cursor position or any embedded EOLs in printed strings. A
; minimum of two spaces are always printed.
;
.proc stPrint
		;reset comma tab stop position
		mva		#10 ioPrintCol

		;set IOCB, defaulting to #0 if there is none
		jsr		evaluateHashIOCBOpt

have_iocb_entry:
		;clear dangling flag
		lsr		printDngl
		
		;begin loop
		bpl		token_loop

token_semi:
		;set dangling flag
		sec
		ror		printDngl
		
token_next:
		inc		exLineOffset
token_loop:
		jsr		ExecTestEnd
		bne		not_eos

		;skip EOL if print ended in semi or comma
		bit		printDngl
		bmi		skip_eol
		
		jsr		IoPutNewline

skip_eol:
		rts

not_eos:
		;check if we have a semicolon; we just ignore these.
		cmp		#TOK_EXP_SEMI
		beq		token_semi
		
		;check if we have a comma
		cmp		#TOK_EXP_COMMA
		bne		not_comma
		
		;emit spaces until we are at the next tabstop, with a two-space
		;minimum
		jsr		IoPutSpace
comma_tab_loop:
		jsr		IoPutSpace
		lda		ioPrintCol
		cmp		#10
		bne		comma_tab_loop
		beq		token_semi
		
not_comma:
		;must be an expression -- clear the dangling flag
		lsr		printDngl
		
		;evaluate expr
		jsr		evaluate
		
		;check if we have a number on the argstack
		ldy		#0
		lda		(argstk),y
		bmi		is_string
		
		;print the number
		jsr		expPopFR0
		jsr		IoPrintNumber
		bmi		token_loop
		
is_string:
		;extract string var
		jsr		expPopAbsString
		
		;print chars
		mva		#0 cix
strprint_loop:
		lda		fr0+2
		bne		strprint_loop1
		lda		fr0+3
		beq		token_loop
		dec		fr0+3
strprint_loop1:
		dec		fr0+2
		
		ldy		cix
		lda		(fr0),y
		inc		cix
		sne:inc	fr0+1
		jsr		putchar
		jmp		strprint_loop
.endp

;===========================================================================
.proc stRead
		sec
		ror		iocbidx
		jmp		DataRead		
.endp

;===========================================================================
; RESTORE [aexp]
;
; Resets the line number to be used next for READing data.
;
; Errors:
;	Error 3 if line not in [0,65535]
;	Error 7 if line in [32768,65535]
;
; The specified line does not have to exist at the time that RESTORE is
; issued. The next READ will start searching at the next line >= the
; specified line.
;
.proc stRestore
		jsr		execRestore
		
		;check if we have a line number
		jsr		evaluate
		ldy		argsp
		beq		no_lineno
		
		;we have a line number -- pop it and copy to dataln
		jsr		expPopFR0IntPos
		stx		dataln
		sta		dataln+1
no_lineno:
		rts
.endp


;===========================================================================
.proc stRun
		;check if we have a filename
		jsr		ExecTestEnd
		beq		no_filename
		clc
		jmp		stLoad.run_entry
		
no_filename:
		jsr		stClr
		clc
		jmp		exec
.endp

;===========================================================================
; RETURN
;
; Returns to the execution point after the most recent GOSUB. Any
; intervening FOR frames are discarded (GOMOKO.BAS relies on this).
;
.proc stReturn
		;pop entries off runtime stack until we find the right frame
loop:
		;##ASSERT dw(runstk)<=dw(memtop2) and !((dw(memtop2)-dw(runstk))&3) and (dw(runstk)=dw(memtop2) or ((db(dw(memtop2)-4)=0 or db(dw(memtop2)-4)>=$80) and dw(dw(memtop2)-3) >= dw(stmtab)))
		lda		memtop2
		cmp		runstk
		bne		stack_not_empty
		lda		memtop2+1
		cmp		runstk+1
		bne		stack_not_empty
error:
		jmp		errorBadRETURN
stack_not_empty:

		;check if we have a GOSUB frame (varbyte=0) or a FOR frame (varbyte>=$80)
		dec		memtop2+1
		ldy		#<-4
		lda		(memtop2),y
		spl:ldy	#<-16

		;pop back one frame, regardless of its type
		ldx		#memtop2
		tya
		jsr		VarAdvancePtrX
		
		;keep going if it was a FOR
		cpy		#<-16
		beq		loop
		
		;switch context and exit
		ldy		#1
		jmp		stNext.pop_frame
.endp


;===========================================================================
stStop = execStop

;===========================================================================
; POP
;
; This statement removes a GOSUB or FOR frame from the runtime stack. No
; error is issued if the runtime stack is empty.
;
; Test case: GOMOKO.BAS
;
.proc stPop
		;##ASSERT dw(runstk)<=dw(memtop2) and !((dw(memtop2)-dw(runstk))&3) and (dw(runstk)=dw(memtop2) or ((db(dw(memtop2)-4)=0 or db(dw(memtop2)-4)>=$80) and dw(dw(memtop2)-3) >= dw(stmtab)))

		;check if runtime stack is empty
		lda		runstk
		cmp		memtop2
		lda		runstk+1
		sbc		memtop2+1
		bcs		done
		
		;pop back one frame
		lda		#$fc
		jsr		dec_ptr			;(!) carry is clear
		
		;check if we popped off a GOSUB frame
		ldy		#0
		lda		(memtop2),y
		bpl		done
		lda		#$f4
dec_ptr_2:
		clc
dec_ptr:
		adc		memtop2
		sta		memtop2
		scs:dec	memtop2+1
done:
		rts		
.endp


;===========================================================================
stQuestionMark = stPrint

;===========================================================================
.proc stGet
		jsr		evaluateHashIOCB
		jsr		ExprSkipCommaAndEvalVar
		
get_and_store:
		ldx		iocbidx
		lda		#0
		sta		icbll,x
		sta		icblh,x
		lda		#CIOCmdGetChars
		jsr		IoDoCmd
		
store_byte_to_var:
		;convert retrieved byte to float
		jsr		MathByteToInt
		
		;store into variable and exit
		jmp		VarStoreFR0
.endp


;===========================================================================
.proc stPut
		jsr		evaluateHashIOCB
		jsr		ExprSkipCommaAndEvalPopInt
		txa
		jsr		IoPutCharDirect
		jmp		ioCheck
.endp


;===========================================================================
; GRAPHICS aexp
;
; Open a graphics mode.
;
; An oddity of this command is that it opens S: on IOCB #6 even in mode 0.
; This means that the I/O environment is different post-boot and after a
; GR.0 -- before then, graphics commands like PLOT and LOCATE will fail on
; the text mode screen, but after issuing GR.0 they will work. CLUES.BAS
; depends on this.
;
.proc stGraphics
		jsr		evaluateInt

		;close IOCB 0 and 6
		ldx		#$00
		jsr		IoCloseX
		ldx		#$60
		jsr		IoCloseX
		
		;open IOCB 0 (E:)
		lda		#$0c
		ldx		#0
		ldy		#<devname_e
		jsr		IoOpenStockDevice
		
		lda		fr0
		and		#$0f
		sta		icax2+$60
		lda		fr0
		and		#$30
		eor		#$1c 
		ldx		#$60
		ldy		#<devname_s
		jmp		IoOpenStockDevice
.endp


;===========================================================================
; PLOT aexp1, aexp2
;
; Plot a point on the graphics screen with the current color.
;
; Errors:
;	Error 3 if X or Y outside of [0,65535]
;	Error 3 if Y in [256, 32767]
;	Error 7 if Y in [32768, 65535]
;
.proc stPlot
		jsr		stSetupCommandXY
		ldx		#$60
		lda		grColor
		jmp		IoPutCharDirectX
.endp


;===========================================================================
stSetupCommandXY = stPosition
.proc stPosition
		;evaluate X
		jsr		evaluateInt
		lda		fr0
		pha
		lda		fr0+1
		pha
		
		;skip comma and evaluate Y
		jsr		ExprSkipCommaAndEvalPopIntPos
		bne		out_of_range
		txa
		
		;position at (X,Y)
		sta		rowcrs
		pla
		sta		colcrs+1
		pla
		sta		colcrs
		rts
		
out_of_range:
		jmp		errorValueErr
.endp


;===========================================================================
.proc stDos
		ldx		#$ff
		txs
		jmp		(dosvec)
.endp


;===========================================================================
.proc stDrawto
		jsr		stSetupCommandXY
		mva		grColor atachr

		ldx		#$60
		lda		#$11
		jmp		IoDoCmdX
.endp

;===========================================================================
; LOCATE aexp1, aexp2, var
;
; Positions the cursor at an (X, Y) location and reads a pixel.
;
; Errors:
;	Error 3 - X<0, Y<0, or Y>=256
;	Error 131 - S: not open on IOCB #6
;
; This statement only works with S: and will fail if IOCB #6 is closed. This
; leads to an oddity where LOCATE doesn't work immediately after BASIC boots,
; but will work if GR.0 is issued, because in that case S: is opened.
; CLUES.BAS depends on this behavior.
;
.proc stLocate
		jsr		stSetupCommandXY
		
		;skip comma and evaluate variable
		jsr		ExprSkipCommaAndEvalVar
		
		;select IOCB #6
		ldx		#$60
		stx		iocbidx
		
		;do get char and store
		jmp		stGet.get_and_store
.endp


;===========================================================================
; SOUND voice, pitch, distortion, volume
;
; Modifies a POKEY sound channel.
;
; Errors:
;	Error 3 if voice not in [0,65535] or in [4,32767]
;	Error 7 if voice in [32768,65535]
;	Error 3 if pitch not in [0,65535]
;	Error 3 if distortion not in [0,65535]
;	Error 3 if volume not in [0,65535]
;
; The values are mixed together as follows:
;	AUDFn <- pitch
;	AUDCn <- distortion*16+value
;
; All audio channels are set to unlinked, 64KHz clock, and 17-bit noise by
; this command.
;
.proc stSound
_channel = stScratch

		;get voice
		jsr		ExprEvalPopIntPos
		bne		oob_value
		txa
		cmp		#4
		bcs		oob_value
		asl
		sta		_channel
		
		;get pitch
		jsr		ExprSkipCommaAndEvalPopInt
		txa
		pha

		;get distortion and volume
		jsr		stSetcolor.decode_dual

		ldx		_channel
		sta		audc1,x
		pla
		sta		audf1,x
		
		;force all audio channels to 64K clock and unlinked
		mva		#0 audctl
		rts

oob_value:
		jmp		errorValueErr
.endp

;===========================================================================
; SETCOLOR aexpr1, aexpr2, aexpr3
;
; Set OS color register aexpr1 to aexpr2*16+aexpr3.
;
; Errors:
;	Error 7 if aexpr1 in [32768, 65535].
;	Error 3 if aexpr1, aexpr2, or aexpr3 not in [0, 65535].
;
.proc stSetcolor
_channel = stScratch

		;get color index
		jsr		ExprEvalPopIntPos
		cpx		#5
		bcs		stSound.oob_value
		stx		_channel
		
		;get hue and luma
		jsr		decode_dual
		
		;store new color
		ldx		_channel
		sta		color0,x
		rts
		
decode_dual:
		;get hue/distortion
		jsr		ExprSkipCommaAndEvalPopInt
		txa
		:4 asl					;compute x16
		pha
		
		;get luma/volume
		jsr		ExprSkipCommaAndEvalPopInt
		pla
		clc
		adc		fr0				;X*16+Y
		
		rts
.endp

;===========================================================================
.proc stLprint
		;open IOCB #7 to P: device for read
		ldy		#<devname_p
		lda		#8
		jsr		IoOpenStockDeviceIOCB7
		
		;do PRINT
		jsr		stPrint.have_iocb_entry
		
		;close IOCB #7
		jmp		IoClose
.endp


;===========================================================================
.proc stCsave
		;open IOCB #7 to C: device for write
		lda		#$08
		jsr		IoOpenCassette
		
		;do load
		jmp		stSave.with_open_iocb
.endp


;===========================================================================
.proc stCload
		;open IOCB #7 to C: device for read
		lda		#$04
		jsr		IoOpenCassette
		
		;do load
		sec
		ror		stLoad._loadflg
		jmp		stLoad.with_open_iocb
.endp


;===========================================================================
stImpliedLet = evaluateAssignment

;===========================================================================
stSyntaxError = errorWTF

.echo "- Statement module length: ",*-?statements_start
