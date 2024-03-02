; Altirra BASIC - Parser module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

;===========================================================================
; Parser module
;===========================================================================
;
; The parser is responsible for accepting a line of input and converting it
; to a tokenized line, either deferred (line 0-32767) or immediate (line
; 32768).
;
; Execution phases and error handling
; -----------------------------------
; Oddly enough, the parser may be invoked by program execution by means of
; the ENTER statement being used in deferred mode. However, execution stops
; after this happens. In Atari BASIC, CONT can be used to resume execution.
;
; One corner case that must be dealt with is that it is possible for the
; parser to run out of memory trying to expand the variable or statement
; tables, raising Error 2. This can in turn invoke TRAP! This means that
; STMCUR must be on a valid line for the optimized line lookup to succeed,
; although any line will do.
;
; Entering a line number between 32768 and 65535 produces a syntax error.
; However, entering a line number that fails FPI (not in 0-65535) causes
; error 3. This can also invoke TRAP.
;
; If an error does occur during table expansion, any variables added during
; parsing are NOT rolled back.
;
; Memory usage
; ------------
; The argument stack area (LOMEM to LOMEM+$ff) is used as the temporary
; tokenization buffer and the 6502 stack is used to handle backtracking.
; Unlike Atari BASIC, the region from $0480-$057F is not used by the parser.
;
;===========================================================================

?parser_start = *

;============================================================================
.proc parseLine
		;reset errno in case we trigger an error
		lda		#1
		sta		errno

		;clear last statement marker and reset input pointer
		lsr
		sta		parStBegin
		sta		cix

		;push backtrack sentinel onto stack
		pha

		;check if we've got a line number
		jsr		afp
		bcc		lineno_valid
		
		;use line 32768 for immediate statements (A:X)
		lda		#$80
		ldx		#$00
		
		;restart at beginning of line
		stx		cix
		beq		lineno_none
		
lineno_valid:
		;convert line to integer and throw error 3 if it fails
		;(yes, you can invoke TRAP this way!)
		jsr		ExprConvFR0Int
		
		;A:X = line number
		;check for a line >=32768, which is illegal
		bmi		paCmdFail

lineno_none:
		;stash the line number and a dummy byte as the line length
		ldy		#1
		sta		(argstk),y
		dey
		txa
		sta		(argstk),y
		ldy		#3
		sty		parout
		
		;check if the line is empty
		jsr		skpspc
		lda		(inbuff),y
		cmp		#$9b
		sne:jmp	paCmdStEnd.accept
		
		;begin parsing at state 0
		jmp		parseJump0
		
		;begin parsing loop
parse_loop_inc:
		jsr		paFetch
parse_loop:
		jsr		paFetch
		;##TRACE "Parse: Executing opcode [$%04x]=$%02x [%y] @ offset %d (%c); stack(%x)=%x %x %x" dw(parptr) (a) dw(parptr) db(cix) db(dw(inbuff)+db(cix)) s db($101+s) db($102+s) db($103+s)
		bmi		is_state
		
		;check if it is a command
		cmp		#' '
		bcc		is_command
		
		;it's a literal char -- check it against the next char
		bne		not_space
		jsr		skpspc
		jmp		parse_loop
		
.def :paCmdEOL = *
		lda		#$9b
not_space:
		ldy		cix
		cmp		(inbuff),y
		bne		parseFail
		tax
		bmi		parse_loop
		inc		cix
		bne		parse_loop

is_command:
		tax
		lda		parse_dispatch_table_hi,x
		pha
		lda		parse_dispatch_table_lo,x
		pha
		rts

is_state:
		;double the state ID and check if we have a call
		tax
		lsr
		bcc		is_state_jump
		dex
		
		;it's a call -- push frame on stack
		ldy		#0
		lda		parptr
		pha
		lda		parptr+1
		pha
		lda		#$ff
		pha
		pha
		
is_state_jump:
		;jump to new state
		mwa		parse_state_table-$80,x parptr
		bcs		parse_loop
		jmp		parseJump.btc_loop
.endp

;============================================================================
parseFail = paCmdFail.entry
.proc paCmdFail
entry:
		;see if we can backtrack
pop_loop:
		pla
		beq		backtrack_fail
		
		cmp		#$ff
		bne		not_jsr
		
		;pop off the jsr frame and keep going
		pla
		pla
		pla
		jmp		pop_loop
		
not_jsr:
		;backtrack frame - restore state and parser IP and try again
		sta		cix
		pla
		sta		parout
		pla
		sta		parptr+1
		pla
		sta		parptr
		;##TRACE "Parser: Backtracking to IP $%04x, pos $%02x" dw(parptr) db(cix)
		jmp		parseLine.parse_loop
		
backtrack_fail:
		;##TRACE "Parser: No backtrack -- failing."
		ldx		#0
		sta		iocbidx
		sta		parout
		ldx		#<msg_error2
		jsr		IoPrintMessage
		
		inc		cix
print_loop:
		ldx		parout
		inc		parout
		lda		lbuff,x
		pha
		dec		cix
		bne		no_invert
		eor		#$80
		cmp		#$1b
		bne		not_eol
		lda		#$a0
		jsr		putchar
		lda		#$9b
no_invert:
not_eol:
		jsr		putchar
		pla
		cmp		#$9b
		bne		print_loop
		
		;We use loop2 here because an syntax error does not interrupt
		;ENTER.
		jmp		execLoop.loop2
.endp

;============================================================================
parse_dispatch_table_lo:
		dta		<[paCmdFail-1]			;$00
		dta		<[paCmdAccept-1]		;$01
		dta		<[paCmdTryStatement-1]	;$02
		dta		<[paCmdOr-1]			;$03
		dta		<[paCmdEOL-1]			;$04
		dta		<[paCmdBranch-1]		;$05
		dta		<[paCmdBranchChar-1]	;$06
		dta		<[paCmdEmit-1]			;$07
		dta		<[paCmdCopyLine-1]		;$08
		dta		<[paCmdRts-1]			;$09
		dta		<[paCmdTryNumber-1]		;$0A
		dta		<[paCmdTryVariable-1]	;$0B
		dta		<[paCmdTryFunction-1]	;$0C
		dta		<[paCmdHex-1]			;$0D
		dta		<[paCmdStEnd-1]			;$0E
		dta		<[paCmdString-1]		;$0F
		dta		<[paCmdBranchStr-1]		;$10
		dta		<[paCmdNum-1]			;$11
		dta		<[paCmdStr-1]			;$12
		dta		<[paCmdEmitBranch-1]	;$13
		dta		<[paCmdTryArrayVar-1]	;$14
		dta		<[paCmdBranchEOS-1]		;$15

parse_dispatch_table_hi:
		dta		>[paCmdFail-1]			;$00
		dta		>[paCmdAccept-1]		;$01
		dta		>[paCmdTryStatement-1]	;$02
		dta		>[paCmdOr-1]			;$03
		dta		>[paCmdEOL-1]			;$04
		dta		>[paCmdBranch-1]		;$05
		dta		>[paCmdBranchChar-1]	;$06
		dta		>[paCmdEmit-1]			;$07
		dta		>[paCmdCopyLine-1]		;$08
		dta		>[paCmdRts-1]			;$09
		dta		>[paCmdTryNumber-1]		;$0A
		dta		>[paCmdTryVariable-1]	;$0B
		dta		>[paCmdTryFunction-1]	;$0C
		dta		>[paCmdHex-1]			;$0D
		dta		>[paCmdStEnd-1]			;$0E
		dta		>[paCmdString-1]		;$0F
		dta		>[paCmdBranchStr-1]		;$10
		dta		>[paCmdNum-1]			;$11
		dta		>[paCmdStr-1]			;$12
		dta		>[paCmdEmitBranch-1]	;$13
		dta		>[paCmdTryArrayVar-1]	;$14
		dta		>[paCmdBranchEOS-1]		;$15
		
;============================================================================
.proc paFetch
		inw		parptr
		ldy		#0
		lda		(parptr),y
		rts
.endp

;============================================================================
.proc paApplyBranch
		;get branch offset
		jsr		paFetch

		;decrement by a page if the original displacement was negative
		spl:dec	parptr+1
		
		;apply unsigned offset
		ldx		#parptr
		jmp		VarAdvancePtrX
.endp

;============================================================================
.proc paCmdAccept
		;remove backtracking entry
		;##TRACE "Parser: Accepting (removing backtracking entry)."
		pla
		pla
		pla
		pla
		jmp		parseLine.parse_loop
.endp

;============================================================================
.proc paCmdTryStatement
		;skip any spaces at the beginning
		jsr		skpspc
		
		;reserve byte for statement length and record its location
		ldy		parout
		sty		parStBegin
		inc		parout
		
		;scan the statement table
		ldx		#<statement_table
		lda		#>statement_table
		ldy		#0
		jmp		paSearchTable
.endp

;============================================================================
; Entry:
;	A:X = search table
;	Y = token base (0 for statements, $3D for functions)
;
.proc paSearchTable
_stateIdx = fr0
_functionMode = fr0+1

		sty		_functionMode
		sty		_stateIdx
		stx		iterPtr
		sta		iterPtr+1

		;check if first character is a letter, a qmark, or a period
		ldy		cix
		lda		(inbuff),y
		cmp		#'?'
		beq		first_ok
		cmp		#'.'
		bne		not_period
		lda		_functionMode
		beq		first_ok
not_period:
		sub		#'A'
		cmp		#26
		bcs		fail_try

first_ok:
		;okay, it's a letter... let's go down the statement table.
table_loop:
		;check if we hit the end of the table
		ldy		#0
		lda		(iterPtr),y
		beq		fail_try

		;begin scan
		ldx		cix
statement_loop:
		lda		(iterPtr),y
		and		#$7f
		inx
		cmp		lbuff-1,x
		bne		fail_stcheck

		;check if this was the last char
		lda		(iterPtr),y
		asl
		
		;progress to next char
		iny
		bcc		statement_loop
		
check_term:

		;Term check
		;
		;For statements, a partial match is accepted:
		;
		;	PRINTI -> PRINT I
		;
		;However, this is not true for functions. A function name will not match
		;if there is an alphanumeric character after it. Examples:
		;
		;	PRINT SIN(0) -> OK, parsed as function call
		;	PRINT SIN0(0) -> OK, parsed as array reference
		;	PRINT SINE(0) -> OK, parsed as array reference
		;	PRINT SIN$(0) -> Parse error at $
		;	PRINT STR(0) -> OK, parsed as array reference

		ldy		_functionMode
		beq		accept

		lda		lbuff,x
		jsr		paIsalnum
		bcc		fail

accept:
		;looks like we've got a hit -- update input pointer, emit token, and change the state.
		stx		cix
		
		lda		_stateIdx
		jsr		paCmdEmit.doEmitByte
		tax

		;init for statements
		lda		parse_state_table_statements,x
		ldy		#>pa_statements_begin

		;check if we're doing functions
		lsr		_functionMode
		bcc		do_branch

		;init for functions

		stx		stScratch
		jsr		paApplyBranch
				
		;push frame on stack
		lda		parptr
		pha
		lda		parptr+1
		pha
		lda		parout
		pha
		lda		#0
		pha
		
		ldx		stScratch
		lda		parse_state_table_functions-$3d,x
		ldy		#>pa_functions_begin

do_branch:
		jmp		parseJump.do_branch

fail_stcheck:
		;check for a ., which is a trivial accept -- this is only allowed for
		;statements and not functions
		ldy		_functionMode
		bne		fail

		lda		lbuff-1,x
		cmp		#'.'
		beq		accept

fail:
		;skip chars until we're at the end of the entry
		jsr		VarAdvanceName
		
		;loop back for more
		inc		_stateIdx
		bne		table_loop
		
		;whoops
fail_try:
		lda		_functionMode
		beq		paCmdBranch.next
		jmp		parseLine.parse_loop_inc
.endp

;============================================================================
.proc paCmdOr
		;push backtracking entry with offset onto stack
		;##TRACE "Parser: Pushing backtracking state IP=$%04x, pos=$%02x, out=$%02x" dw(parptr)+dsb(dw(parptr)+1)+1 db(cix) db(parout)
		ldy		#1
		lda		parptr
		sec
		adc		(parptr),y
		pha
		lda		parptr+1
		adc		#0
		pha
		lda		parout
		pha
		lda		cix
		pha
		;##TRACE "Parser: %02x" s
		
		;resume parsing
		jmp		parseLine.parse_loop_inc
.endp

;============================================================================
.proc paCmdBranchStr
		lda		parStrType
		bmi		paCmdBranch
		jmp		parseLine.parse_loop_inc
.endp


;============================================================================
paCmdEmitBranch:
		jsr		paCmdEmit.doEmit
.proc paCmdBranch
		jsr		paApplyBranch
next:
		jmp		parseLine.parse_loop
.endp

;============================================================================
.proc paCmdBranchChar
		;get character and check
		jsr		paFetch
		ldy		cix
		cmp		(inbuff),y
		beq		char_match

		;skip past branch offset and emit char and continue execution
		jsr		paFetch
		jmp		parseLine.parse_loop_inc

char_match:
		;eat the char
		;##TRACE "Parser: Branching on char: %c" (a)
		inc		cix
		
		;check if we need to emit
		jsr		paFetch
		beq		paCmdBranch
		
		jsr		paCmdEmit.doEmitByte
		jmp		paCmdBranch
.endp

;============================================================================
.proc paCmdBranchEOS
		;get character and check
		ldy		cix
		lda		(inbuff),y
		cmp		#':'
		beq		paCmdBranch
		cmp		#$9b
		beq		paCmdBranch

		;skip past branch offset and continue execution
		jmp		parseLine.parse_loop_inc
.endp

;============================================================================
.proc paCmdCopyLine
copy_loop:
		ldy		cix
		lda		(inbuff),y
		jsr		paCmdEmit.doEmitByte
		inc		cix
		cmp		#$9b
		bne		copy_loop
		dec		cix
		jmp		paCmdStEnd
.endp

;============================================================================
.proc paCmdEmit
		jsr		doEmit
		jmp		parseLine.parse_loop

doEmit:
		;get token to emit
		jsr		paFetch
		
doEmitByte:
		;emit the token
		ldy		parout
		inc		parout
		beq		overflow
		sta		(argstk),y
		
		;all done
		rts

overflow:
		jmp		errorLineTooLong
.endp

;============================================================================
.proc paCmdRts
		;remove backtracking indicator
		pla
		
		;remove dummy output val
		pla

		;remove return address
		pla
		sta		parptr+1
		pla
		sta		parptr
		
		jmp		parseLine.parse_loop
.endp

;============================================================================
.proc paCmdTryNumber
		;try to parse
		lda		cix
		pha
		jsr		afp
		pla
		bcc		succeeded
		sta		cix
		jmp		parseLine.parse_loop_inc
succeeded:

		;emit a constant number token
		lda		#TOK_EXP_CNUM
emit_number:
		jsr		paCmdEmit.doEmitByte
		
		;emit the number
		ldx		#-6
copyloop:
		lda		fr0+6,x
		jsr		paCmdEmit.doEmitByte
		inx
		bne		copyloop
		
		;all done
		jmp		paCmdBranch
.endp

;============================================================================
; Exit:
;	C = 0 if alphanumeric
;	C = 1 if not alphanumeric
;
.proc paIsalnum
		pha
		sec
		sbc		#'0'
		cmp		#10
		bcc		success
		sbc		#'A'-'0'
		cmp		#26
success:
		pla
		rts
.endp

;============================================================================
.proc paCmdTryArrayVar
		lda		#$ff
		bne		paCmdTryVariable._array_entry
.endp

.proc paCmdTryVariable
_index = a5
_reqarray = a5+1
_nameLen = a4
_nameLen2 = a4+1
_nameEnd = a3
		lda		#0
_array_entry:
		sta		_reqarray

		;first non-space character must be a letter
		jsr		skpspc
		lda		(inbuff),y
		sub		#'A'
		cmp		#26
		bcs		reject

		;compute length of the name
		iny
		
namelen_loop:
		lda		(inbuff),y
		jsr		paIsalnum
		bcs		namelen_loop_end
		iny
		bne		namelen_loop
		
namelen_loop_end:
		;check if we have an array or string specifier
		cmp		#'('
		beq		is_array_var
		cmp		#'$'
		beq		is_array_var
		
		;not an array... reject if we needed one
		bit		_reqarray
		bpl		not_array
		
reject:
		jmp		parseLine.parse_loop_inc

is_array_var:
		;capture the extra char
		iny
		
not_array:
		;record the ending position
		sty		_nameEnd

		;search the variable name table
		mwa		vntp iterPtr
		mva		#$80 _index
search_loop:
		;check if we've hit the sentinel at the end
		ldy		#0
		lda		(iterPtr),y
		beq		create_new
		
		;check characters one at a time for this entry
		ldx		cix
match_loop:
		lda		(iterPtr),y
		bmi		match_last
		cmp		lbuff,x
		bne		no_match
		inx
		iny
		bne		match_loop
		
match_last:
		;reject if we're not on the last character of the varname in the input
		inx
		cpx		_nameEnd
		bne		no_match
		
		;reject if the last char doesn't match
		eor		#$80
		cmp		lbuff-1,x
		bne		no_match

match_ok:
		stx		cix
match_ok_2:
		lda		_index
		jsr		paCmdEmit.doEmitByte
		
		;set expr type flag
		ldx		cix
		lsr		parStrType
		lda		lbuff-1,x
		cmp		#'$'
		sne:ror	parStrType
		
		;branch
		;##TRACE "Taking variable branch"
		jmp		paCmdBranch
				
no_match:
		;skip remaining chars in this entry
		jsr		VarAdvanceName

		inc		_index
		jmp		search_loop
		
create_new:
		;Check whether we have enough room to create a new variable. Since we
		;were already tracking the variable index, doing this is simple: it
		;must not be zero.
		;##TRACE "Creating new variable $%02x [%.*s]" db(paCmdTryVariable._index) db(paCmdTryVariable._nameLen) lbuff+db(cix)
		lda		_index
		bne		create_new_have_space
		
		;oops... too many variables!
		jmp		errorTooManyVars
		
create_new_have_space:
		;bump input pointer to end of name and compute name length
		lda		_nameEnd
		tax
		sec
		sbc		cix
		stx		cix
		sta		_nameLen
		
		;OK, now we need to make room at the top of the VNT for the new name,
		;plus add another 8 chars at the top of the VVT. To save some time,
		;we only make room at the top of the VVT first, then we move just
		;the VVT. Do NOT adjust stmtab itself.
		mwx		stmtab a0
		ldy		#0
		add		#8
		sta		_nameLen2
		ldx		#stmtab+2
		jsr		expandTable
		
		;now we need to move the VVT
		;##TRACE "vntp=$%04x, vvtp=$%04x, stmtab=$%04x" dw(vntp) dw(vvtp) dw(stmtab)
		sbw		stmtab vvtp a3		;compute VVT size
		lda		stmtab
		sta		a1
		clc
		adc		_nameLen
		sta		stmtab
		sta		a0
		lda		stmtab+1
		sta		a1+1				;A0 = dest = stmtab
		adc		#0
		sta		stmtab+1			;bump up STMTAB by total offset
		sta		a0+1				;A1 = source = stmtab
		jsr		copyDescending
		
		;relocate STMTAB
		lda		#8
		ldx		#stmtab
		jsr		VarAdvancePtrX
		
		;relocate VNTD and VVTP
		lda		_nameLen
		ldx		#vvtp
		jsr		VarAdvancePtrX
		lda		_nameLen
		ldx		#vntd
		jsr		VarAdvancePtrX
		
		;copy the name just below the vvt, complementing the first byte
		dec		vvtp+1		;(!!) temporarily bump this down so we can index
		ldx		cix
		ldy		#$ff
		lda		#0
		sta		(vvtp),y-
		lda		#$80
name_copy:
		dex
		eor		lbuff,x
		sta		(vvtp),y-
		lda		#0
		dec		_nameLen
		bne		name_copy
		
		inc		vvtp+1		;(!!) restore vvtp
		
		;zero out the new variable
		dec		stmtab+1
		
		;check if we have a string or array and change type accordingly
		ldx		cix
		ldy		lbuff-1,x
		cpy		#'$'
		bne		type_not_string
		lda		#$80
type_not_string:
		cpy		#'('
		bne		type_not_array
		lda		#$40
type_not_array:

		;set type
		ldy		#$f8
		sta		(stmtab),y

		;set variable index in variable value entry
		iny
		lda		_index
		and		#$7f
		;##TRACE "Setting VVTP entry to index $%02X" (a)
		sta		(stmtab),y
		iny
		
		;zero out remaining bytes
		lda		#0
		sta:rne	(stmtab),y+
		
		inc		stmtab+1
		jmp		match_ok_2

.endp

;============================================================================
.proc paCmdTryFunction
		;scan the function table
		ldx		#<funtok_name_table
		lda		#>funtok_name_table
		ldy		#$3d
		jmp		paSearchTable		
.endp


;============================================================================
.proc paCmdStEnd
		;backpatch the statement skip byte
		lda		parout
		ldy		parStBegin
		sta		(argstk),y

		;check if we have EOL
		ldy		cix
		lda		(inbuff),y
		cmp		#$9b
		beq		is_eol
		
		;jump to state 0
		jmp		parseJump0
		
is_eol:
accept:
		;remove backtracking entries
bac_loop:
		pla
		beq		bac_done
		pla
		pla
		pla
		jmp		bac_loop
bac_done:
		;mark statement length
		lda		parout
		cmp		#4
		bcs		not_empty
		
		;hmm, empty -- we're deleting, then.
		lda		#0
		sta		parout
		
not_empty:
		ldy		#2
		sta		(argstk),y
		
		;determine where this should fit in statement table
		dey
		lda		(argstk),y
		pha
		dey
		lda		(argstk),y
		tax
		pla
		jsr		exFindLineInt
		sta		fr0
		sty		fr0+1
		
		;save off address
		sta		stmcur
		sty		stmcur+1
		
		;check whether we should expand or contract the statement table
		bcc		not_found

		;##TRACE "Parser: Have %u bytes, found existing %u bytes" db(parout) db(dw(fr0)+2)
		ldy		#2
		lda		parout
		sec
		sbc		(fr0),y
		beq		same_length
		bcs		do_expand
do_contract:
		;sign extend negative delta
		ldy		#$ff

		;move statements and tables down in memory
		ldx		#starp
		jsr		contractTable
		jmp		same_length
		
not_found:
		;check if we are trying to delete a line that doesn't exist (which is ok)
		lda		parout
		beq		done
do_expand:
		ldy		#0
		
		;move statements and tables up in memory
		ldx		#starp
		jsr		expandTable

same_length:
		clc
		lda		parout
		beq		done

		;copy line from temporary memory into statement table
		ldy		#0
copyloop:
		lda		(argstk),y
		sta		(stmcur),y
		iny
		cpy		parout
		bne		copyloop
		
		;all done - exit C=0 for delete, C=1 for insert/replace/immediate
done:
		rts
.endp

;============================================================================
.proc paCmdHex
		;zero FR0 (although really only need 16-bit)
		jsr		zfr0

		;set empty flag
		ldx		#1
digit_loop:
		;try to parse a digit
		jsr		isdigt
		bcc		digit_ok
		and		#$df
		cmp		#$11
		bcc		parse_end
		cmp		#$17
		bcs		parse_end
		sbc		#7-1

digit_ok:
		inc		cix

		;shl4
		ldx		#4
shl4_loop:
		asl		fr0
		rol		fr0+1
		dex					;!! - also clears empty flag
		bne		shl4_loop

		;merge in new digit
		ora		fr0
		sta		fr0

		;loop back if we're not full
		lda		fr0+1
		cmp		#$10
		bcc		digit_loop

parse_end:
		;check if we actually got anything
		txa
		seq:jmp	paCmdFail

		;convert to FP
		jsr		ifp

		;emit and then branch
		lda		#TOK_EXP_CHEX
		jmp		paCmdTryNumber.emit_number
.endp

;============================================================================
; String literal.
;
; Note that an unterminated (dangling) string literal is permitted. Floyd
; of the Jungle (1982) relies on this.
;
.proc paCmdString
		;add string literal token
		lda		#TOK_EXP_CSTR
		jsr		paCmdEmit.doEmitByte
		
		ldx		cix
		dex

		;reserve length and stash offset
		iny
		sty		a0
		bne		loop_start		;!! - relying on A != EOL or "

		;advance and copy until we find the terminating quote
loop:
		lda		lbuff,x
loop_start:
		jsr		paCmdEmit.doEmitByte
		cmp		#$9b
		beq		unterminated
		inx
		cmp		#'"'
		bne		loop
unterminated:
		
		;save new locations
		stx		cix
		sty		parout
		
		;compute length
		tya
		clc						;(!!) -1 to not include length byte
		sbc		a0
		
		;store length
		ldy		a0
		sta		(argstk),y
		
		;resume
		jmp		parseLine.parse_loop
.endp

;============================================================================
paCmdStr:
paCmdNum:
		cpx		#$12
		ror		parStrType
		jmp		parseLine.parse_loop

;============================================================================
parseJump0:
		lda		#0
.proc parseJump
		asl
		tax
		lda		parse_state_table,x
		ldy		parse_state_table+1,x

do_branch:
		sta		parptr
		sty		parptr+1
		
		;clear any backtracking entries from the stack
btc_loop:
		pla
		beq		bt_cleared
		pla
		pla
		pla
		jmp		btc_loop
bt_cleared:
		pha
		jmp		parseLine.parse_loop
.endp

.echo "- Parser length: ",*-?parser_start
