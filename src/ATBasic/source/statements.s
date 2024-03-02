; Altirra BASIC - Statement module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

?statements_start = *

;===========================================================================
.proc stColor
		jsr		evaluateInt
		stx		grColor
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
; ENTER may be used in deferred mode, but execution stops after the
; statement is executed. This is equivalent to a STOP, so sounds and I/O
; channels are not affected.
;
.proc stEnter
_vectmp = $0500

		;Use IOCB #7 for compatibility with Atari BASIC
		jsr		IoSetupIOCB7
		
		;get filename
		jsr		evaluate
		
		;issue open call for read
		jsr		IoDoOpenReadWithFilename

		;set exec IOCB to #7
		mvx		#$70 iocbexec
		
		;restart execution loop
		jmp		execLoop.loop2
.endp


;===========================================================================
stLet = evaluateAssignment

;===========================================================================
.proc stRem
		;remove return address
		pla
		pla

		;warp to end of current line
		jmp		exec.next_line
.endp


;===========================================================================
stData = stRem

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
		
		;check if it is zero and skip the line if so
		lda		fr0
		;;##TRACE "If condition: %g" fr0
		beq		stRem

		;skip the THEN token, which is always present
		;##ASSERT db(dw(stmcur)+db(exLineOffset)) = $1B
		inc		exLineOffset
		
		;check if this is the end of the statement
		ldy		exLineOffset
		cpy		exLineOffsetNxt
		beq		statement_follows
		
		;no, it isn't... process the implicit GOTO.
		ldx		#<-6
copy_loop:
		iny
		lda		(stmcur),y
		sta		fr0+6,x
		inx
		bne		copy_loop
		jsr		ExprConvFR0Int
		jmp		stGoto.gotoFR0Int
		
statement_follows:
		;a statement follows, so execute it
		rts
.endp


;===========================================================================
.proc stFor
		;get and save variable
		lda		(stmcur),y
		pha
		
		;execute assignment to set variable initial value
		jsr		evaluateAssignment
				
		;skip TO keyword, evaluate stop value and push
		jsr		ExprSkipCommaAndEval		;actually skipping TO, not a comma
		jsr		pushNumber
		
		;check for a STEP keyword
		ldy		exLineOffset
		lda		(stmcur),y
		cmp		#$1a
		bne		no_step
		
		;skip STEP keyword, then evaluate and store step
		jsr		ExprSkipCommaAndEval
		jmp		had_step
no_step:
		jsr		fld1
had_step:
		jsr		pushNumber

		;push frame and exit
		pla
pushFrame:
		ldy		#0
		ldx		stmcur
		jsr		push_ax
		lda		stmcur+1
		ldx		exLineOffsetNxt
		jsr		push_ax
raise_stack:
		ldx		#memtop2
		tya
		jmp		VarAdvancePtrX

push_ax:
		sta		(memtop2),y+
		txa
		sta		(memtop2),y+
		rts
		
pushNumber:
		ldy		#0
loop1:
		lda		fr0,y
		sta		(memtop2),y
		iny
		cpy		#6
		bne		loop1
		beq		raise_stack
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
		jsr		stReturn.check_rtstack_empty
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
		
		;load loop variable
		jsr		VarLoadFR0
		
		;load step		
		ldy		#11
		mva		(memtop2),y- fr1+5
		mva		(memtop2),y- fr1+4
		mva		(memtop2),y- fr1+3
		mva		(memtop2),y- fr1+2
		mva		(memtop2),y- fr1+1
		mva		(memtop2),y fr1

		;save off step sign
		sta		stScratch

		;add step to variable		
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
		
		ror
		eor		stScratch
		bpl		not_done
		
loop_done:
		;skip variable and exit
		;;##TRACE "Terminating FOR loop"
		inc		exLineOffset
		rts

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
_index = stScratch

fail_value_err:
		jmp		errorValueErr

_entry:
		;fetch and convert the selection value
		jsr		ExprEvalPopIntPos

		;issue error 3 if value is greater than 255		
		bne		fail_value_err
				
		;exit immediately if index is zero		
		txa
		beq		xit
		sta		_index

		;next token should be GOTO or GOSUB
		ldy		exLineOffset
		inc		exLineOffset
		lda		(stmcur),y

		;save GOTO/GOSUB token
		pha

count_loop:
		;check if it's time to branch
		dec		_index
		beq		do_branch

		;evaluate a line number
		jsr		ExprEvalPopIntPos
		
		;read next token and check if it is a comma
		jsr		ExecGetComma
		beq		count_loop
		
		;out of line numbers -- continue with next statement
		pla
xit:
		rts
				
do_branch:
		;check if we should do GOTO or GOSUB
		pla
		eor		#TOK_EXP_GOTO
		beq		stGoto

		;!! fall through to stGosub!
.endp

stGosub:
		;push gosub frame
		;##TRACE "Pushing GOSUB frame: $%04x+$%02x" dw(stmcur) db(exLineOffset)
		lda		#0
		jsr		stFor.pushFrame

		;fall through

.proc stGoto
		;get line number
		jsr		ExprEvalPopIntPos
gotoFR0Int:
		jsr		exFindLineInt
		sta		stmcur
		sty		stmcur+1
		bcc		not_found

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
.proc stTrap
		jsr		evaluateInt
		stx		exTrapLine
		sta		exTrapLine+1
xit:
		rts
.endp


;===========================================================================
.proc stBye
		ldx		#$ff
		txs
		jmp		blkbdv
.endp


;===========================================================================
; CONT
;
; Resumes execution from the last stop or error point.
;
; Quirks:
;	- The documentation says that CONT resumes execution at the next lineno,
;	  but this is incorrect. Instead, Atari BASIC appears to do an insertion
;	  search for the stop line itself, then skip to the end of that line.
;	  This means that if the stop line is deleted, execution will resume at
;	  the line AFTER the next line.
;
.proc stCont
		;check if we are executing the immediate mode line
		ldy		#1
		lda		(stmcur),y

		;if we aren't (deferred mode), it's a no-op
		bpl		stTrap.xit

		;bail if stop line is >=32K
		ldx		stopln
		lda		stopln+1
		bmi		stGoto.not_found

		;search for the stop line -- okay if this fails
		jsr		exFindLineInt

		;jump to that line
		sta		stmcur
		sty		stmcur+1

		;warp to end of line and continue execution
		jmp		exec.next_line
.endp

;===========================================================================
stCom = stDim

;===========================================================================
; CLOSE #iocb
;
; Closes the given I/O channel. No error results if the IOCB is already
; closed.
;
; Errors:
;	Error 20 if IOCB #0 or #1-32767
;	Error 7 if IOCB #32768-65535
;	Error 3 if IOCB# not in [0,65535]
;
.proc stClose
		jsr		evaluateHashIOCB
close_iocb:
		jmp		IoClose
.endp

;===========================================================================
.proc stDir
		jsr		evaluate
		jsr		IoSetupIOCB7
		lda		#6
		ldy		argsp
		bne		open_fn
		ldy		#<devpath_d1all
		jsr		IoOpenStockDeviceIOCB7
		bpl		read_loop
open_fn:
		jsr		IoDoOpenWithFilename
read_loop:
		ldx		#$70
		jsr		IoSetupReadLine
		jsr		ciov
		bpl		ok
		cpy		#$88
		beq		stClose.close_iocb
		jsr		ioCheck
ok:
		ldx		#0
		jsr		IoSetupReadLine
		lda		#CIOCmdPutRecord
		jsr		IoDoCmdX
		bpl		read_loop			;!! N=0 - error would be trapped
.endp

;===========================================================================
; CLR
;
; Clears all numeric values to zero and un-dimensions any string and numeric
; arrays. The runtime stack is also cleared, so no FOR or GOSUB frames are
; left afterward.
;
; The current COLOR, degree/radian mode, and TRAP line are not affected.
;
.proc stClr
		jsr		zfr0			;!! - also sets A=0
		jsr		VarGetAddr0
		bne		loopstart
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
clear_arrays:
		ldx		#<-4
reset_loop:
		lda		starp+4,x
		sta		runstk+4,x
		inx
		bne		reset_loop

		;reset APPMHI
		jmp		MemAdjustAPPMHI
.endp


;===========================================================================
.nowarn .proc stDegRad
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
		;DIM is the only statement that allows undimensioned strings to
		;be referenced, so we set a special flag.
		lda		#$40
		jsr		evaluate._assign_entry
		jsr		ExecGetComma
		beq		loop
		rts
.endp


;===========================================================================
; END
;
; Silences audio channels and closes IOCBs. Does not reset TRAP or clear
; variables.
;
stEnd = immediateModeReset

;===========================================================================
; NEW
;
; Erases all program text and variables, clears the TRAP line, silences
; sound, closes IOCBs, resets the tab width to 10, and resets angular mode
; to radians.
;
; Not affected by new: COLOR
;
.proc stNew
		jsr		reset_entry
		jmp		immediateModeReset

reset_entry:
		ldy		memlo+1
		ldx		memlo

		;set up second argument stack pointer
		txa
		clc
		adc		#$6c
		sta		argstk2
		tya
		adc		#0
		sta		argstk2+1

		;initialize LOMEM from MEMLO
		iny
		sty		lomem+1
		stx		lomem

		;clear remaining tables
		;copy LOMEM to VNTP/VNTD/VVTP/STMTAB/STMCUR/STARP/RUNSTK/MEMTOP2
		ldx		#<-16
		jsr		stClr.reset_loop

		dec		lomem+1

		;reset trap line
		ldx		#$ff
		stx		exTrapLine+1

		;reset tab width
		lda		#10
		sta		ptabw

		;insert byte at VNTD
		inx
		stx		degflg			;!! - set degflg to $00 (radians)
		stx		a2+1
		inx
		stx		a2
		ldx		#vvtp
		jsr		MemAdjustTablePtrs

		;insert three bytes at STARP
		lda		#3
		sta		a2
		ldx		#starp
		jsr		MemAdjustTablePtrs
		
		;write sentinel into variable name table
		ldy		#3
		mva:rpl	empty_program,y (vntp),y-

		;all done
		rts

empty_program:
		dta		$00,$00,$80,$03
.endp

;===========================================================================
; RUN [sexp]
;
; Optionally loads a file from disk and begins execution.
;
; All open IOCBs are closed and sound channels silenced prior to execution.
;
;===========================================================================
; LOAD filespec
;
; Loads a BASIC program in binary format.
;
; Execution is reset prior to beginning the load.
;
.proc stLoadRun
_vectmp = fr0
_loadflg = stScratch		;N=0 for run, N=1 for load

.def :stRun
		;set up for run
		lsr		_loadflg

		;check if we have a filename
		jsr		ExecTestEnd
		beq		do_imm_or_run
		bne		run_entry

.def :stLoad
		sec
		ror		_loadflg
		
run_entry:
		;Use IOCB #7 for compatibility with Atari BASIC
		jsr		IoSetupIOCB7
		
		;pop filename
		jsr		evaluate

loader_entry:
		;do open
		jsr		IoDoOpenReadWithFilename

with_open_iocb:
		;load vector table to temporary area (14 bytes)
		ldy		#CIOCmdGetChars
		jsr		setup_vector_io
		
		;check if first pointer is zero -- if not, assume bad file
		lda		_vectmp
		ora		_vectmp+1
		bne		bogus

		;check if MEMTOP2 will be pushed at or above OS MEMTOP
		lda		_vectmp+12
		clc
		adc		lomem
		tax
		lda		_vectmp+13
		clc
		adc		lomem+1
		bcs		too_long
		cmp		memtop+1
		bcc		memory_ok
		bne		too_long
		cpx		memtop
		bcs		too_long
memory_ok:
		
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

do_imm_or_run:
		;close IOCBs (including the one we just used) and reset sound
		jsr		ExecReset
		
		;clear runtime variables
		jsr		stClr
		
		;check if we should run
		asl		_loadflg		;!! - sets C=0 for normal run
		scs:jmp	exec
		
		;jump to immediate mode loop
		jmp		immediateMode
		
too_long:
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
		jmp		ioChecked
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
		jsr		evaluate
		
		;issue open call for write
		lda		#8
		jsr		IoDoOpenWithFilename
		
with_open_iocb:

		;Set up and relocate pointers. There are two gotchas here:
		;
		;1) We must actually subtract LOMEM from itself, producing an
		;   offset of 0. This is in fact required.
		;
		;2) Atari BASIC assumes that the load length is the STARP offset
		;   minus $0100, but rev. B has a bug where it extends the argument
		;   stack by 16 bytes each time it saves. This results in a
		;   corresponding amount of junk at the end of the file that must
		;   be there for the file to load. Because this is rather dumb,
		;   we fix up the offsets on save. We don't do this on load
		;   because there are a number of programs in the wild that have
		;   such offsets and we don't want to shift the memory layout.

		ldx		#12
relocloop:
		sec
		lda		lomem,x
		sbc		vntp
		sta		_vectmp,x
		lda		lomem+1,x
		sbc		vntp+1
		adc		#0
		sta		_vectmp+1,x
		dex
		dex
		bne		relocloop

		;now reset LOMEM and VNTP offsets
		stx		_vectmp
		stx		_vectmp+1
		stx		_vectmp+2
		inx
		stx		_vectmp+3
		
		;write vector table (14 bytes)
		ldy		#CIOCmdPutChars
		ldx		#$70
		jsr		stLoadRun.setup_vector_io
		
		;write from VNTP from STARP
		jsr		stLoadRun.setup_main_io
		
		;close and exit
		jmp		IoClose
.endp


;===========================================================================
; STATUS #iocb, avar
;
; Retrieves the status code of an I/O channel and puts it into the given
; numeric variable.
;
; Bugs:
;	Atari BASIC allows a numeric array element to be passed as the second
;	parameter and stomps the array entry with a number. We do not currently
;	support this bug.
;
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
		
		;issue XIO 38 to get current position
		ldx		iocbidx
		mva		#38 iccmd,x
		jsr		ioChecked
		
		;copy ICAX3/4 into first variable
		jsr		ExprSkipCommaAndEvalVar
		ldy		iocbidx
		lda		icax3,y
		ldx		icax4,y
		jsr		MathWordToFP
		jsr		VarStoreFR0
		
		;copy ICAX5 into second variable
		jsr		ExprSkipCommaAndEvalVar
		ldx		iocbidx
		lda		icax5,x
		jmp		stGet.store_byte_to_var
.endp


;===========================================================================
; POINT #iocb, avar, avar
;
; Note that there is only one byte in the IOCB for the sector offset (AUX5);
; Atari BASIC silently drops the high byte without error.
;
; For some reason, this command only takes avars instead of aexps, even
; though they're incoming parameters.
;
.proc stPoint
		;consume #iocb,
		jsr		evaluateHashIOCB
		
		;consume comma and then first var, which holds sector number
		jsr		ExprSkipCommaAndEvalPopInt
		
		;move to ICAX3/4
		ldy		iocbidx
		sta		icax4,y
		txa
		sta		icax3,y
		
		;consume comma and then second var, which holds sector offset
		jsr		ExprSkipCommaAndEvalPopInt
		
		;move to ICAX5
		txa
		ldx		iocbidx
		sta		icax5,x
		
		;issue XIO 37 and exit
		lda		#37
		jmp		IoDoCmdX
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
;===========================================================================
; OPEN #iocb, aexp1, aexp2, filename
;
; Opens an I/O channel.
;
; Errors:
;	Error 7 if aexp1 or aexp2 in [32768, 65535]
;	Error 3 if aexp1 or aexp2 not in [0, 65535]
;	Error 20 if iocb #0
;
; Quirks:
;	AUX1/2 are overwritten and CIOV is called without checking whether the
;	IOCB is already open. This means that if the IOCB is already in use,
;	its permission byte will be stomped by the conflicting OPEN.
;
.proc stXio
		jsr		evaluateInt
		inc		exLineOffset
		txa
		dta		{bit $0100}
.def :stOpen = *
		lda		#CIOCmdOpen
		pha
		jsr		evaluateHashIOCB
		jsr		ExprSkipCommaAndEvalPopInt
		txa
		pha
		jsr		ExprSkipCommaAndEvalPopInt
		txa
		pha

		;get filename
		jsr		ExprSkipCommaAndEval
		
		ldx		iocbidx
		pla
		sta		icax2,x
		pla
		sta		icax1,x
				
		;issue command
		pla
		jmp		IoDoWithFilename
.endp

;===========================================================================
stPoke = stDpoke
.proc stDpoke
		stx		stScratch3

		;evaluate and save address
		jsr		evaluateInt

		;save address
		sta		stScratch+1
		stx		stScratch
		
		;skip comma and evaluate value
		jsr		ExprSkipCommaAndEvalPopInt
		
		;set up for DPOKE
		ldy		#1

		;check if we're doing POKE -- note that POKE's token is odd ($1F)
		;while DPOKE's is even ($3E)		
		lsr		stScratch3
		bcs		poke_only

		;do poke
		;;##TRACE "POKE %u,%u" dw(fr0+1) db(fr0)
		sta		(stScratch),y
poke_only:
		txa
		dey
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
		mva		ptabw ioPrintCol

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
		cmp		ptabw
		bne		comma_tab_loop
		beq		token_semi
		
not_comma:
		;must be an expression -- clear the dangling flag
		lsr		printDngl
		
		;evaluate expr
		jsr		evaluate
		
		;check if we have a number on the argstack
		lda		expType
		bmi		is_string
		
		;print the number
		jsr		IoPrintNumber
		bmi		token_loop
		
is_string:
		;print chars
		mwa		fr0 inbuff
strprint_loop:
		lda		fr0+2
		bne		strprint_loop1
		lda		fr0+3
		beq		token_loop
		dec		fr0+3
strprint_loop1:
		dec		fr0+2
		
		ldy		#0
		lda		(inbuff),y
		jsr		IoPutCharAndInc
		bpl		strprint_loop
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
		jsr		ExprConvFR0IntPos
		stx		dataln
		sta		dataln+1
no_lineno:
		rts
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
		jsr		check_rtstack_empty
		bcs		stack_empty
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

stack_empty:
		jmp		errorBadRETURN

check_rtstack_empty:
		lda		runstk
		cmp		memtop2
		lda		runstk+1
		sbc		memtop2+1
		rts
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
		jsr		stReturn.check_rtstack_empty
		bcs		done
		
		;pop back one frame
		lda		#$fc
		jsr		dec_ptr			;(!) carry is clear

pop_frame_remainder:
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
		jsr		MathByteToFP
		
		;store into variable and exit
		jmp		VarStoreFR0
.endp


;===========================================================================
; PUT #iocb, aexp
;
; Writes the given character by number to the specified I/O channel.
;
; Errors:
;	Error 20 if IOCB #0 or 8-32767
;	Error 7 if IOCB #32768-65535
;	Error 3 if IOCB# not in [0,65535]
;
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
; Errors:
;	3 - if mode <0 or >65535
;
; An oddity of this command is that it opens S: on IOCB #6 even in mode 0.
; This means that the I/O environment is different post-boot and after a
; GR.0 -- before then, graphics commands like PLOT and LOCATE will fail on
; the text mode screen, but after issuing GR.0 they will work. CLUES.BAS
; depends on this.
;
; This command must not reopen the E: device. Doing so will break Space
; Station Multiplication due to overwriting graphics data already placed
; at $BFxx.
;
.proc stGraphics
		jsr		evaluateInt

		;close IOCB 6
		ldx		#$60
		jsr		IoCloseX
				
		;reopen IOCB 6 with S:
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
		
		;position at (X,Y)
		stx		rowcrs
		pla
		sta		colcrs+1
		pla
		sta		colcrs
		rts
		
out_of_range:
		jmp		errorValueErr
.endp


;===========================================================================
stCp = stDos
.proc stDos
		jsr		ExecReset
		;We may end up returning if DOS fails to load (MEM.SAV error, user
		;backs out!).
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
; Asynchronous receive mode is also turned off in SKCTL so that channels 3+4
; function. However, a quirk in Atari BASIC is that the shadow SSKCTL is
; *not* updated.
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

		;force off asynchronous mode so that channels 3 and 4 work
		mva		#3 skctl
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
		;open IOCB #7 to P: device for write
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
		ror		stLoadRun._loadflg
		jmp		stLoadRun.with_open_iocb
.endp


;===========================================================================
stImpliedLet = evaluateAssignment

;===========================================================================
stSyntaxError = errorWTF

;===========================================================================
.proc stFileOp
		lda		op_table-TOK_ERASE,x
		pha
		jsr		evaluate
		jsr		IoSetupIOCB7
		pla
		jmp		IoDoWithFilename

op_table:
		dta		$21,$23,$24,0,$20
.endp

;===========================================================================
.proc stMove
		;get source address and save
		jsr		evaluateInt
		pha
		txa
		pha

		;get destination address and save
		jsr		ExprSkipCommaAndEvalPopInt
		pha
		txa
		pha

		;get length
		jsr		ExprSkipCommaAndEval

		;split off the sign and take abs
		jsr		MathSplitSign

		;convert to int and save
		jsr		ExprConvFR0Int
		stx		a3
		sta		a3+1

		;unpack destination address
		pla
		sta		a0
		pla
		sta		a0+1

		;unpack source address
		pla
		sta		a1
		pla
		sta		a1+1

		;check if we are doing a descending copy
		bit		funScratch1
		bmi		copy_down

		;copy up
		jmp		copyAscending

copy_down:
		;adjust pointers
		ldy		#a3
		ldx		#a0
		jsr		IntAdd
		ldx		#a1
		jsr		IntAdd

		;copy down
		jmp		copyDescending
.endp

;===========================================================================
.proc stBput
		lda		#CIOCmdPutChars
		dta		{bit $0100}
.def :stBget = *
		lda		#CIOCmdGetChars
		pha

		;consume #iocb,
		jsr		evaluateHashIOCB
		
		;consume comma and then first val (address)
		jsr		ExprSkipCommaAndEvalPopInt
		
		;store address
		ldy		iocbidx
		sta		icbah,y
		txa
		sta		icbal,y
		
		;consume comma and then second val (length)
		jsr		ExprSkipCommaAndEvalPopInt

		;store length
		ldy		iocbidx
		sta		icblh,y
		txa
		sta		icbll,y

		;issue read call and exit
		pla
		jmp		IoDoCmd
.endp

.echo "- Statement module length: ",*-?statements_start
