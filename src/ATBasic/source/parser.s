; Altirra BASIC - Parser module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

?parser_start = *

;============================================================================
.proc parseLine
		;clear last statement marker and reset input pointer
		lda		#0
		sta		parStBegin
		sta		cix

		;push backtrack sentinel onto stack
		pha

		;check if we've got a line number
		jsr		afp
		bcc		lineno_valid
		
		;use line 32768 for immediate statements
		lda		#$80
		sta		fr0+1
		asl
		sta		fr0
		
		;restart at beginning of line
		sta		cix
		beq		lineno_none
		
lineno_valid:
		jsr		fpi
		
		;check for a line >=32768, which is illegal
		lda		fr0+1
		bpl		lineno_valid2
		
		jmp		paCmdFail

lineno_valid2:
lineno_none:
		;stash the line number and a dummy byte as the line length
		ldy		#0
		mva		fr0 (argstk),y+
		lda		fr0+1
		jsr		ExprPushRawByteAsWord	;!! push FR0+1 then $00
		
		;save room for statement length
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
		jsr		imprint
		dta		c"ERROR-   ",0
		
		jsr		ldbufa
		ldy		cix
		lda		(inbuff),y
		cmp		#$9b
		beq		is_cr
		eor		#$80
		sta		(inbuff),y
		jmp		not_cr
is_cr:
		lda		#$a0
		sta		(inbuff),y+
		lda		#$9b
		sta		(inbuff),y
not_cr:
		jsr		printLineINBUFF
		
		jmp		immediateMode
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
		dta		<[paCmdStBegin-1]		;$0D
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
		dta		>[paCmdStBegin-1]		;$0D
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
_stateIdx = fr0

		jsr		skpspc

		;first character must be a letter or a ?
		lda		(inbuff),y
		cmp		#'?'
		beq		valid
		tax
		sbc		#'A'			;!! carry set if >'?', cleared if below
		cmp		#26
		scc:jmp	parseLine.parse_loop
valid:
		txa
		
		;okay, it's a letter... let's go down the statement table.
		ldx		#0
		stx		_stateIdx
table_loop:
		ldy		cix
statement_loop:
		lda		statement_table,x
		and		#$7f
		cmp		(inbuff),y
		bne		fail
		
		;progress to next char
		inx
		iny
		lda		statement_table-1,x
		bpl		statement_loop
		
accept:
		;looks like we've got a hit -- update input pointer, emit token, and change the state.
		sty		cix
		
		lda		_stateIdx
		ldy		parout
		inc		parout
		sta		(argstk),y
		adc		#PST_STATEMENT_BASE-1		;!! carry is set!
		jmp		parseJump
		
fail:
		;allow . as a trivial accept
		lda		(inbuff),y
		iny
		cmp		#'.'
		beq		accept
		dey
skip_loop:
		;skip chars until we're at the end of the entry
		inx
		lda		statement_table-1,x
		bpl		skip_loop
		
		;keep going if we have more statements
		inc		_stateIdx
		lda		statement_table,x
		bne		table_loop
		
		;whoops
		jmp		parseLine.parse_loop
		
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
		beq		no_emit
		
		jsr		paCmdEmit.doEmitByte

no_emit:
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
		sta		(argstk),y
		
		;all done
		rts
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
		ldy		parout
		cpy		#255-6
		bcs		overflow
		
		mva		#TOK_EXP_CNUM (argstk),y+
		
		;emit the number
		ldx		#-6
copyloop:
		mva		fr0+6,x (argstk),y+
		inx
		bne		copyloop
		sty		parout
		
		;all done
		jmp		paCmdBranch
overflow:
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
		lda		#1
		sta		_nameLen
		
		iny
		
namelen_loop:
		lda		(inbuff),y
		cmp		#'A'
		bcc		namelen_not_alpha
		cmp		#'Z'+1
		bcc		namelen_loop_ok
namelen_not_alpha:
		cmp		#'0'
		bcc		namelen_loop_end
		cmp		#'9'+1
		bcs		namelen_loop_end
namelen_loop_ok:
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
		sne:jmp	create_new
		
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
		ldy		parout
		mva		_index (argstk),y
		iny
		beq		overflow
		sty		parout
		
		;set expr type flag
		ldx		cix
		lsr		parStrType
		lda		lbuff-1,x
		cmp		#'$'
		sne:ror	parStrType
		
		;branch
		;##TRACE "Taking variable branch"
		jmp		paCmdBranch

overflow:
		jmp		errorArgStkOverflow
				
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
		sta		a0
		clc
		adc		_nameLen
		sta		stmtab
		sta		a1
		lda		stmtab+1
		sta		a0+1				;A0 = source = stmtab
		adc		#0
		sta		stmtab+1			;bump up STMTAB by total offset
		sta		a1+1				;A1 = dest = stmtab
		jsr		copyDescending
		
		;relocate STMTAB
		lda		#8
		ldx		#stmtab
		jsr		VarAdvancePtrX
		
		;relocate VVT pointer
		lda		_nameLen
		ldx		#vvtp
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
_stateIdx = fr0
		;first character must be a letter
		ldy		cix
		lda		(inbuff),y
		tax
		sub		#'A'
		cmp		#26
		bcc		letter_start
		jmp		parseLine.parse_loop_inc
letter_start:
		txa
		
		;okay, it's a letter... let's go down the function table.
		ldx		#$3d
		stx		_stateIdx
		ldx		#0
table_loop:
		ldy		cix
function_loop:
		lda		funtok_name_table,x
		and		#$7f
		cmp		(inbuff),y
		bne		fail
		
		;progress to next char
		inx
		iny
		lda		funtok_name_table-1,x
		bpl		function_loop
		
accept:
		;looks like we've got a hit -- update input pointer and emit the token,
		;then do a jsr.
		sty		cix
		
		ldy		parout
		mva		_stateIdx (argstk),y
		iny
		beq		overflow
		sty		parout
		
		;compute state offset
		add		#PST_FUNCTION_BASE-$3d
		asl
		sta		stScratch

		;apply branch
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
		
		;jump
		ldx		stScratch
		jsr		ParseSetJump
		jmp		parseLine.parse_loop

overflow:
		brk
		
fail:
skip_loop:
		;skip chars until we're at the end of the entry
		inx
		lda		funtok_name_table-1,x
		bpl		skip_loop
		
		;keep going if we have more statements
		inc		_stateIdx
		lda		funtok_name_table,x
		bne		table_loop
		
		;whoops
		jmp		parseLine.parse_loop_inc
		
.endp


;============================================================================
.proc paCmdStBegin
		;skip any spaces at the beginning
		jsr		skpspc
		
		;reserve byte for statement length and record its location
		ldy		parout
		sty		parStBegin
		iny
		beq		overflow
		sty		parout
		
		;resume execution
		jmp		parseLine.parse_loop
		
overflow:
		brk
.endp

;============================================================================
.proc paCmdStEnd
		;backpatch the statement skip byte
		lda		parout
		ldy		parStBegin
		sta		(argstk),y

		;check if we have EOL
		ldx		cix
		lda		lbuff,x
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
		mva		(argstk),y fr0+1
		dey
		mva		(argstk),y fr0
		lda		#fr0
		jsr		exFindLineInt
		
		;save off address
		ldx		#stmcur
		jsr		ExprStoreFR0Int
		
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
		
		;all done
done:
		rts
.endp

;============================================================================
; String literal.
;
; Note that an unterminated (dangling) string literal is permitted. Floyd
; of the Jungle (1982) relies on this.
;
.proc paCmdString
		;add string literal token
		ldy		parout
		mva		#TOK_EXP_CSTR (argstk),y+
		
		;reserve length and stash offset
		sty		a0
		iny
		beq		overflow

		;advance and copy until we find the terminating quote
		ldx		cix
loop:
		lda		lbuff,x
		inx
		sta		(argstk),y
		iny
		beq		overflow
		cmp		#$9b
		beq		unterminated
		cmp		#'"'
		bne		loop
		inx
unterminated:
		dex
		
		;save new locations
		stx		cix
		dey
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
		
overflow:
		jmp		errorArgStkOverflow
.endp

;============================================================================
.proc paCmdBranchStr
		lda		parStrType
		spl:jmp	paCmdBranch
		jmp		parseLine.parse_loop_inc
.endp

;============================================================================
paCmdStr:
		sec
		ror		parStrType
		dta		{bit $0100}
paCmdNum:
		lsr		parStrType
		jmp		parseLine.parse_loop

;============================================================================
parseJump0:
		lda		#0
.proc parseJump
		asl
		tax
		jsr		ParseSetJump
		
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

;============================================================================
.proc ParseSetJump
		mwa		parse_state_table,x parptr
		rts
.endp

.echo "- Parser length: ",*-?parser_start
