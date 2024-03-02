; Altirra BASIC - READ/INPUT data module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

;==========================================================================
;
; Entry:
;	IOCBIDX = IOCB to use for INPUT, or -1 for READ
;
.proc DataRead
read_loop:
		;check if IOCB #0 was specified and we're in INPUT mode, and if so,
		;print a question mark. note that this happens even if the IOCB was
		;specified, i.e. INPUT #0,A$.		
		ldx		iocbidx
		bne		skip_prompt
		
		;print ? prompt
		jsr		imprint
		dta		'?',0
		
skip_prompt:
		;reset read pointer
		lda		#0
		sta		cix

		;check if we are doing READ or INPUT
		ldx		iocbidx
		bpl		read_line_input
		
		;we're doing READ -- check if we have a valid cached line
		lda		dataptr+1
		bne		have_data_line
		
		;call into exec to get next line
		mwa		dataln a0
		lda		#dataptr
		jsr		exFindLineInt
		
		;reset starting index
have_data_line_2:
		mva		#0 dataoff
		
		;cache off line length
		ldy		#2
		mva		(dataptr),y dataLnEnd

have_data_line:
		;check if we have a valid index into a DATA statement already
		ldy		dataoff
		beq		need_data_line
		cpy		dataLnEnd
		bcs		data_line_end
		bcc		have_data
		
need_data_line:

		;check if we are at line 32768
		;#ASSERT dw(dataptr) >= dw(stmtab) && dw(dataptr) < dw(starp)
		ldy		#1
		lda		(dataptr),y
		bpl		data_line_valid
		
		;yup -- no data left
		jmp		errorOutOfData
		
data_line_valid:
		;##TRACE "Data: Scanning for DATA token on line %u ($%04X)" dw(dw(dataptr)) dw(dataptr)
		ldy		#3
data_line_scan:
		;scan the line to find the next DATA statement
		cpy		dataLnEnd
		beq		data_line_end
		
		;fetch next statement token
		iny
		lda		(dataptr),y
		
		;is it the DATA token?
		cmp		#TOK_DATA
		beq		have_data_stmt
		
		;no... jump to next statement
		dey
		lda		(dataptr),y
		tay
		bne		data_line_scan
		
data_line_end:
		;jump to next line
		ldy		#2
		lda		(dataptr),y
		ldx		#dataptr
		jsr		VarAdvancePtrX
		
		;##TRACE "Data: Advancing to line %u ($%04X)" dw(dw(dataptr)) dw(dataptr)
		;stash off line number
		ldy		#0
		mva		(dataptr),y dataln
		iny
		mva		(dataptr),y dataln+1
		jmp		have_data_line_2
		
have_data_stmt:
		iny
have_data:
		sty		cix
		mwa		dataptr inbuff
		;##TRACE "Beginning READ with data: $%04X+$%02X [%.*s]" dw(dataptr) db(cix) db(dataLnEnd)-db(cix) dw(dataptr)+db(cix)
		jmp		parse_loop

read_line_input:
		;read line to LBUFF
		jsr		IoSetupReadLine
		jsr		ioChecked
		
		;vector INBUFF to line buffer
		jsr		ldbufa
		
parse_loop:
		;get next variable
		jsr		evaluate
		jsr		expPopVar
				
		;check type of variable
		ldy		#0
		lda		(varptr),y
		bpl		is_numeric
		
		;check if string is dimensioned
		lsr
		bcs		strvar_ok
		jmp		errorDimError
strvar_ok:

		;copy string data to FR0 for convenience
		ldy		#8
		sty		argsp
		jsr		expPopFR0
		
		;we have a string... compute the remaining length
		;READ statements will stop string reads at a comma; INPUT
		;statements will consume the comma
		ldy		cix
		ldx		#0
len_loop:
		lda		(inbuff),y
		cmp		#$9b
		beq		len_loop_end
		iny
		bit		iocbidx
		bpl		no_comma_stop
		cmp		#','
		beq		len_loop_end
no_comma_stop:
		inx
		bne		len_loop
len_loop_end:

		;stash the end of the string -- we need this before
		;we do truncation to the buffer length
		tya
		pha
		
		;compare against the capacity in the string buffer and truncate
		;as necessary
		ldy		fr0+5
		bne		string_fits		;>=256 chars... guaranteed to fit
		txa
		cmp		fr0+4
		scc:lda	fr0+4			;use capacity if it is smaller (or equal)
string_fits:
		
		;write length to string variable
		tax
		ldy		#5
		mva		#0 (varptr),y-
		txa
		sta		(varptr),y
		sta		fr0+2			;for convenience below
		
		;copy string to string storage		
		ldy		cix
		ldx		#0
strcpy_loop:
		lda		(inbuff),y
		sta		(fr0,x)
		inw		fr0
		iny
		dec		fr0+2
		bne		strcpy_loop
		
		;warp to end of the input
		pla
		sta		cix
		
read_next:
		;check if we have more vars to read
		jsr		DataCheckMore
		
		;read new line if line is empty, else keep parsing
		ldy		cix
		lda		(inbuff),y
		cmp		#$9b
		bne		parse_loop
		
		;check if we are processing a DATA statement -- if so, force end so
		;we go to the next one
		bit		iocbidx
		bpl		not_data_end
		
		mva		dataLnEnd dataoff
not_data_end:
		jmp		read_loop
		
parse_next:
		;store numeric value to variable
		;##TRACE "READ -> %g" fr0
		jsr		VarStoreFR0
		jmp		read_next
		
is_numeric:
		;attempt to parse out a number
		jsr		afp
		bcs		parse_error
		
		;advance to next input, checking for spaces or a comma -- we must
		;do this before we store the parsed value and even if there are no
		;other values to retrieve
		ldy		cix
		lda		(inbuff),y
		cmp		#$9b
		beq		parse_next
		inc		cix
		cmp		#','
		beq		parse_next
		cmp		#' '
		bne		parse_error
		
		;okay, we have spaces... eat the spaces, then check if there's
		;anything afterward. Nothing can follow, not even a comma.
		jsr		skpspc
		cmp		#$9b
		beq		parse_next
parse_error:
		jmp		errorInputStmt
.endp

;==========================================================================
.proc DataCheckMore
		;read current token
		ldy		exLineOffset
		lda		(stmcur),y
		
		;check if it's a comma
		cmp		#TOK_EXP_COMMA
		beq		have_more
		
		;no, it's not -- pop so we will be ending the current statement
		pla
		pla
		
		;check if we are processing a DATA statement -- if so, stash the current
		;offset.
		bit		iocbidx
		bpl		skip_data_update
		
		;stash off the current offset
		ldy		cix
		lda		(inbuff),y
		cmp		#$9b
		sne:ldy	dataLnEnd
		sty		dataoff
skip_data_update:		
		rts
		
have_more:
		inc		exLineOffset
		rts
.endp
