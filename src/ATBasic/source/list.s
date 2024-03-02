; Altirra BASIC - LIST module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

?list_start = *

;==========================================================================
; LIST [filespec,] [lineno [,lineno]]
;
; If filespec is specified, IOCB #7 is used to send to that output.
;
; If one lineno is specified, only that line is listed. If two linenos
; are specified, lines within that range inclusive are listed. If the range
; is inverted, no lines are listed.
;
; Errors:
;	Error 3 if lineno not in [0,65535]
;	Error 7 if lineno in [32768, 65535]
;
; Unusual as it may be, it is perfectly OK to have a LIST statement inside
; of a running program. Therefore, we have to be careful not to disturb
; running execution state. We can, however, take over the argument stack
; area as well as the parser pointers.
;
.proc stList
_endline = stScratch3

		;init start and end lines
		lda		#$ff
		sta		_endline
		lsr
		sta		_endline+1
		lda		#0
		sta		parptr
		sta		parptr+1
		
		;assume IOCB #0
		sta		iocbidx

		;check if there is an argument
		jsr		ExecTestEnd
		beq		do_list
		
		;evaluate it
		jsr		evaluate
		
		;test if it is a filespec
		ldy		#0
		lda		(argstk),y
		bpl		not_filespec
		
		;it's a filespec -- set and close IOCB #7
		ldx		#$70
		stx		iocbidx
		jsr		IoCloseX
		
		;eval filename
		jsr		expPopAbsString
		
		;open IOCB for write
		lda		#$08
		sta		icax1+$70
		jsr		IoDoOpenWithFilename
		
		;do listing
		jsr		do_list
		
		;close IOCB and exit
		jmp		IoClose
		
do_list:
		;check if we have a comma and skip it if so
		jsr		ExecTestEnd
		cmp		#TOK_EXP_COMMA
		sne:inc	exLineOffset
		
		;check for a line number
		jsr		ExecTestEnd
		beq		no_lineno		
		
		;parse first line number
		jsr		evaluate
not_filespec:
		jsr		expPopFR0IntPos
		ldx		#parptr
		jsr		ExprStoreFR0Int
		ldx		#_endline
		jsr		ExprStoreFR0Int
		
		;check for a second line number
		jsr		ExecTestEnd
		beq		no_lineno
		
		jsr		ExprSkipCommaAndEvalPopIntPos
		ldx		#_endline
		jsr		ExprStoreFR0Int

no_lineno:
		;init first statement
		mwa		parptr a0
		lda		#parptr
		jsr		exFindLineInt
		
		;turn on LIST mode display
		sec
		ror		dspflg
		
lineloop:
		;check that we haven't hit the end line; we'll always eventually
		;hit the immediate mode line
		ldy		#0
		sec
		lda		_endline
		sbc		(parptr),y
		iny
		lda		_endline+1
		sbc		(parptr),y
		bcs		not_done
		
		;turn off LIST mode display
		asl		dspflg
		
		;we're done
		rts
not_done:
		
		;convert line number; this will also set INBUFF = LBUFF
		lda		(parptr),y
		sta		fr0+1
		dey
		lda		(parptr),y
		sta		fr0
		jsr		IoPrintInt
		
		;add a space
		jsr		IoPutSpace

		;begin processing statements		
		ldy		#3
statement_loop:
		;read and cache the end of statement
		mva		(parptr),y+ parStBegin
		
		;read next token
		lda		(parptr),y+
		sty		parout
		
		cmp		#TOK_ILET
		beq		implicit_let
		cmp		#$36
		bcs		statement_done
		
		;lookup and print statement name
		pha
		tax
		lda		stname_table,x
		add		#<statement_table
		sta		inbuff
		lda		#>statement_table
		adc		#0
		sta		inbuff+1
		jsr		printStringINBUFF
		
		;print space
		jsr		IoPutSpace
		
		;check if we just printed REM, DATA or ERROR -- we must switch
		;to raw printing after this
		pla
		cmp		#$02				;check for TOK_REM ($00) or TOK_DATA ($01)
		bcc		print_raw
		cmp		#TOK_SXERROR
		beq		print_raw

		;check if we have additional tokens
		ldy		parout
		lda		(parptr),y
		
		cmp		#TOK_EOL
		beq		statement_done
		
implicit_let:
		;process function tokens
		jsr		do_function_tokens
		
statement_done:
		ldy		#2
		lda		(parptr),y
		cmp		parStBegin
		beq		line_done
		
		;next statement
		ldy		parStBegin
		jmp		statement_loop
		
line_done:
		;add a newline
		jsr		IoPutNewline
		
		;advance to next line
		ldy		#2
		lda		(parptr),y
		ldx		#parptr
		jsr		VarAdvancePtrX		
		jmp		lineloop
		
print_raw:
		jsr		ListGetByte
		cmp		#$9b
		beq		line_done
		jsr		putchar
		jmp		print_raw
		
print_const_number:
		ldx		#$fa
print_const_number_1:
		lda		(parptr),y+
		sta		fr0+6,x
		inx
		bne		print_const_number_1
		sty		parout
		jsr		IoPrintNumber
		jmp		do_function_tokens_resume

print_const_string:
		sty		parout
		lda		#'"'
		jsr		putchar
		
		;get length
		jsr		ListGetByte
		sta		fr0
		beq		print_const_string_2
print_const_string_1:
		jsr		ListGetByte
		jsr		putchar
		dec		fr0
		bne		print_const_string_1
print_const_string_2:
		lda		#'"'
		jsr		putchar
		jmp		do_function_tokens_resume

do_function_tokens_done:
		rts
do_function_tokens_resume:
		ldy		parout		
do_function_tokens:
		;IF statements will abruptly stop after the THEN, so we must
		;catch that case
		cpy		parStBegin
		beq		do_function_tokens_done
		
		;fetch next function token
		lda		(parptr),y
		iny
		tax
		bmi		print_var
		cmp		#$0e
		beq		print_const_number
		cmp		#$0f
		beq		print_const_string
		sub		#$12
		tax
		lda		funtok_name_offset_table,x
		beq		do_function_tokens_done
		add		#<[funtok_name_table - 1]
		sta		inbuff
		lda		#>[funtok_name_table - 1]
		adc		#0
		sta		inbuff+1
		sty		parout
		jsr		printStringINBUFF
		ldy		parout
		jmp		do_function_tokens

print_var:
		sty		parout
		mwa		vntp iterPtr
		stx		stScratch
		bne		print_var_entry
print_var_loop:
		jsr		VarAdvanceName
print_var_entry:
		dec		stScratch
		bmi		print_var_loop
print_var_done:
		mwa		iterPtr inbuff
		jsr		printStringINBUFF
		
		;check if we got an array var -- if so, we need to skip the open
		;parens token that's coming
		cmp		#'('+$80
		sne:inc	parout
		
		jmp		do_function_tokens_resume
		
.endp

;==========================================================================
.proc ListGetByte
		ldy		parout
		inc		parout
		lda		(parptr),y
		rts
.endp

;==========================================================================
.proc funtok_name_offset_table
		dta		funtok_name_12_3C - funtok_name_table + 1
		dta		funtok_name_13 - funtok_name_table + 1
		dta		funtok_name_14 - funtok_name_table + 1
		dta		funtok_name_15 - funtok_name_table + 1
		dta		0
		dta		funtok_name_17 - funtok_name_table + 1
		dta		funtok_name_18 - funtok_name_table + 1
		dta		funtok_name_19 - funtok_name_table + 1
		dta		funtok_name_1A - funtok_name_table + 1
		dta		funtok_name_1B - funtok_name_table + 1
		dta		funtok_name_1C - funtok_name_table + 1
		dta		funtok_name_1D_2F - funtok_name_table + 1
		dta		funtok_name_1E_30 - funtok_name_table + 1
		dta		funtok_name_1F_31 - funtok_name_table + 1
		dta		funtok_name_20_32 - funtok_name_table + 1
		dta		funtok_name_21_33 - funtok_name_table + 1
		dta		funtok_name_22_34 - funtok_name_table + 1
		dta		funtok_name_23 - funtok_name_table + 1
		dta		funtok_name_24 - funtok_name_table + 1
		dta		funtok_name_25_35 - funtok_name_table + 1
		dta		funtok_name_26_36 - funtok_name_table + 1
		dta		funtok_name_27 - funtok_name_table + 1
		dta		funtok_name_28 - funtok_name_table + 1
		dta		funtok_name_29 - funtok_name_table + 1
		dta		funtok_name_2A - funtok_name_table + 1
		dta		funtok_name_2B_37_38_39_3A_3B - funtok_name_table + 1
		dta		funtok_name_2C - funtok_name_table + 1
		dta		funtok_name_2D_2E - funtok_name_table + 1
		dta		funtok_name_2D_2E - funtok_name_table + 1
		dta		funtok_name_1D_2F - funtok_name_table + 1
		dta		funtok_name_1E_30 - funtok_name_table + 1
		dta		funtok_name_1F_31 - funtok_name_table + 1
		dta		funtok_name_20_32 - funtok_name_table + 1
		dta		funtok_name_21_33 - funtok_name_table + 1
		dta		funtok_name_22_34 - funtok_name_table + 1
		dta		funtok_name_25_35 - funtok_name_table + 1
		dta		funtok_name_26_36 - funtok_name_table + 1
		dta		funtok_name_2B_37_38_39_3A_3B - funtok_name_table + 1
		dta		funtok_name_2B_37_38_39_3A_3B - funtok_name_table + 1
		dta		funtok_name_2B_37_38_39_3A_3B - funtok_name_table + 1
		dta		funtok_name_2B_37_38_39_3A_3B - funtok_name_table + 1
		dta		funtok_name_2B_37_38_39_3A_3B - funtok_name_table + 1
		dta		funtok_name_12_3C - funtok_name_table + 1
		dta		funtok_name_3D - funtok_name_table + 1
		dta		funtok_name_3E - funtok_name_table + 1
		dta		funtok_name_3F - funtok_name_table + 1
		dta		funtok_name_40 - funtok_name_table + 1
		dta		funtok_name_41 - funtok_name_table + 1
		dta		funtok_name_42 - funtok_name_table + 1
		dta		funtok_name_43 - funtok_name_table + 1
		dta		funtok_name_44 - funtok_name_table + 1
		dta		funtok_name_45 - funtok_name_table + 1
		dta		funtok_name_46 - funtok_name_table + 1
		dta		funtok_name_47 - funtok_name_table + 1
		dta		funtok_name_48 - funtok_name_table + 1
		dta		funtok_name_49 - funtok_name_table + 1
		dta		funtok_name_4A - funtok_name_table + 1
		dta		funtok_name_4B - funtok_name_table + 1
		dta		funtok_name_4C - funtok_name_table + 1
		dta		funtok_name_4D - funtok_name_table + 1
		dta		funtok_name_4E - funtok_name_table + 1
		dta		funtok_name_4F - funtok_name_table + 1
		dta		funtok_name_50 - funtok_name_table + 1
		dta		funtok_name_51 - funtok_name_table + 1
		dta		funtok_name_52 - funtok_name_table + 1
		dta		funtok_name_53 - funtok_name_table + 1
		dta		funtok_name_54 - funtok_name_table + 1
.endp

funtok_name_table:
funtok_name_3D		dta		c'STR',c'$'+$80
funtok_name_3E		dta		c'CHR',c'$'+$80
funtok_name_3F		dta		c'US',c'R'+$80
funtok_name_40		dta		c'AS',c'C'+$80
funtok_name_41		dta		c'VA',c'L'+$80
funtok_name_42		dta		c'LE',c'N'+$80
funtok_name_43		dta		c'AD',c'R'+$80
funtok_name_44		dta		c'AT',c'N'+$80
funtok_name_45		dta		c'CO',c'S'+$80
funtok_name_46		dta		c'PEE',c'K'+$80
funtok_name_47		dta		c'SI',c'N'+$80
funtok_name_48		dta		c'RN',c'D'+$80
funtok_name_49		dta		c'FR',c'E'+$80
funtok_name_4A		dta		c'EX',c'P'+$80
funtok_name_4B		dta		c'LO',c'G'+$80
funtok_name_4C		dta		c'CLO',c'G'+$80
funtok_name_4D		dta		c'SQ',c'R'+$80
funtok_name_4E		dta		c'SG',c'N'+$80
funtok_name_4F		dta		c'AB',c'S'+$80
funtok_name_50		dta		c'IN',c'T'+$80
funtok_name_51		dta		c'PADDL',c'E'+$80
funtok_name_52		dta		c'STIC',c'K'+$80
funtok_name_53		dta		c'PTRI',c'G'+$80
funtok_name_54		dta		c'STRI',c'G'+$80
					dta		0
funtok_name_12_3C	dta		c','+$80
funtok_name_13		dta		c'$'+$80
funtok_name_14		dta		c':'+$80
funtok_name_15		dta		c';'+$80
funtok_name_17		dta		c' GOTO',c' '+$80
funtok_name_18		dta		c' GOSUB',c' '+$80
funtok_name_19		dta		c' TO',c' '+$80
funtok_name_1A		dta		c' STEP',c' '+$80
funtok_name_1B		dta		c' THEN',c' '+$80
funtok_name_1C		dta		c'#'+$80
funtok_name_1D_2F	dta		c'<',c'='+$80
funtok_name_1E_30	dta		c'<',c'>'+$80
funtok_name_1F_31	dta		c'>',c'='+$80
funtok_name_20_32	dta		c'<'+$80
funtok_name_21_33	dta		c'>'+$80
funtok_name_22_34	dta		c'='+$80
funtok_name_23		dta		c'^'+$80
funtok_name_24		dta		c'*'+$80
funtok_name_25_35	dta		c'+'+$80
funtok_name_26_36	dta		c'-'+$80
funtok_name_27		dta		c'/'+$80
funtok_name_28		dta		c' NOT',c' '+$80
funtok_name_29		dta		c' OR',c' '+$80
funtok_name_2A		dta		c' AND',c' '+$80
funtok_name_2B_37_38_39_3A_3B		dta		c'('+$80
funtok_name_2C		dta		c')'+$80
funtok_name_2D_2E	dta		c'='+$80

		_STATIC_ASSERT *-funtok_name_table<254, "Function token name table is too long."

.echo "- List module length: ",*-?list_start
