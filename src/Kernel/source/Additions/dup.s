; Altirra DOS - DUP.SYS
; Copyright (C) 2014-2015 Avery Lee, All Rights Reserved.
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
; SOFTWARE. 

		icl		'hardware.inc'
		icl		'kerneldb.inc'
		icl		'cio.inc'
		icl		'sio.inc'
		icl		'dosexports.inc'

runad	equ		$02e0
initad	equ		$02e2
dskinv	equ		$e453
ciov	equ		$e456

dosvecoff_lnoff = $0a
dosvecoff_fnbuf = $21
dosvecoff_lnbuf = $40

;==========================================================================
; DOS 2.0S's buffers end at $1DFC when the default of 3 sector buffers is
; used, and at $1FFC at most with the max of 8 buffers.

		org		$80
		opt		o-

intrin_idx	.byte
put_idx		.byte
lasterr		.byte
msg_ptr		.word
fn_ptr		.word
ln_ptr		.word

		org		$2000
		opt		o+

;==========================================================================
; Message base
;
.pages 1
msg_base:

msg_prompt:
		dta		$9B,'D1:',0

msg_no_cartridge:
		dta		'No cartridge',$9B,0

msg_error:
		dta		'Error ',0

msg_welcome_banner:
		dta		'Altirra DOS 0.1',$9B
.endpg
		dta		$9B
		dta		'A. Disk directory  I. Format diskette',$9B
		dta		'B. Run cartridge   J. Duplicate disk',$9B
		dta		'C. Copy file       K. Binary save',$9B
		dta		'D. Delete file     L. Binary load',$9B
		dta		'E. Rename file     M. Run at address',$9B
		dta		'F. Lock file       N. Create MEM.SAV',$9B
		dta		'G. Unlock file     O. Duplicate file',$9B
		dta		'H. Write DOS files',$9B
		dta		$9B
		dta		'Select item or ','RETURN'+$80,' for menu',$9B
		dta		0

msg_errors:
msg_err80	dta		'User break',0
msg_err81	dta		'IOCB in use',0
msg_err82	dta		'Unknown device',0
msg_err83	dta		'IOCB write only',0
msg_err84	dta		'Invalid command',0
msg_err85	dta		'Not open',0
msg_err86	dta		'Invalid IOCB',0
msg_err87	dta		'IOCB read only',0
msg_err88	dta		'End of file',0
msg_err89	dta		'Truncated record',0
msg_err8A	dta		'Timeout',0
msg_err8B	dta		'Device NAK',0
msg_err8C	dta		'Framing error',0
msg_err8D	dta		'Cursor out of range',0
msg_err8E	dta		'Overrun error',0
msg_err8F	dta		'Checksum error',0
msg_err90	dta		'Device error',0
msg_err91	dta		'Bad screen mode',0
msg_err92	dta		'Not supported',0
msg_err93	dta		'Out of memory',0
msg_err94	dta		0
msg_err95	dta		0
msg_err96	dta		0
msg_err97	dta		0
msg_err98	dta		0
msg_err99	dta		0
msg_err9A	dta		0
msg_err9B	dta		0
msg_err9C	dta		0
msg_err9D	dta		0
msg_err9E	dta		0
msg_err9F	dta		0
msg_errA0	dta		'Bad drive #',0
msg_errA1	dta		'Too many files',0
msg_errA2	dta		'Disk full',0
msg_errA3	dta		'Fatal disk error',0
msg_errA4	dta		'File number mismatch',0
msg_errA5	dta		'Bad file name',0
msg_errA6	dta		'Bad POINT offset',0
msg_errA7	dta		'File locked',0
msg_errA8	dta		'Invalid disk command',0
msg_errA9	dta		'Directory full',0
msg_errAA	dta		'File not found',0
msg_errAB	dta		'Invalid POINT',0

;==========================================================================
.proc DupMain
		;save user zero page
		ldx		#0
		mva:rpl	$80,x zpsave,x+

		;initialize get filename pointer
		lda		dosvec
		clc
		adc		#3
		sta		DupGetFilename.getfn_vec
		lda		dosvec+1
		adc		#0
		sta		DupGetFilename.getfn_vec+1

		;initialize filename pointer
		lda		dosvec
		clc
		adc		#dosvecoff_fnbuf
		sta		fn_ptr
		lda		dosvec+1
		adc		#0
		sta		fn_ptr+1

		;initialize line pointer
		lda		dosvec
		clc
		adc		#dosvecoff_lnbuf
		sta		ln_ptr
		lda		dosvec+1
		adc		#0
		sta		ln_ptr+1

		;print welcome banner
		ldx		#<msg_welcome_banner
		jsr		DupPrintMessage

input_loop:
		;close IOCB #1 in case it was left open
		jsr		DupCloseIOCB1

		;print prompt
		ldx		#<msg_prompt
		jsr		DupPrintMessage

		;read line
		ldx		#0
		jsr		DupSetupReadLine
		jsr		ciov

		;scan for an intrinsic command
		ldx		#0
		stx		intrin_idx
intrinsic_scan_loop:
		inc		intrin_idx
		ldy		#0
intrinsic_compare_loop:
		lda		(ln_ptr),y
		and		#$df
		eor		intrinsic_commands,x
		inx
		asl
		bne		intrinsic_mismatch
		iny
		bcc		intrinsic_compare_loop

		;next char must be space or EOL
		lda		(ln_ptr),y
		cmp		#$9b
		beq		intrinsic_hit
		cmp		#' '
		bne		intrinsic_mismatch

intrinsic_hit:
		tya
		ldy		#dosvecoff_lnoff
		sta		(dosvec),y
		jsr		DupDispatchIntrinsic
		jmp		input_loop

intrinsic_mismatch:
		lda		intrinsic_commands,x
		beq		intrinsic_fail
		asl
		inx
		bcc		intrinsic_mismatch
		bcs		intrinsic_scan_loop

intrinsic_fail:

		;parse out filename
		mva		#0 dosvec_lnoff
		jsr		DupGetFilename
		beq		input_loop

		;yes, we did -- check if it has an extension
		ldy		#0
dotscan_loop:
		lda		(fn_ptr),y
		iny
		cmp		#'.'
		beq		has_ext
		cmp		#$9b
		bne		dotscan_loop

		;add .COM at the end (if there is room)
		cpy		#13
		bcs		has_ext

		ldx		#3
		dey
comadd_loop:
		lda		com_ext,x
		sta		(fn_ptr),y
		iny
		dex
		bpl		comadd_loop

has_ext:
		;attempt to open exe
		mwa		fn_ptr icbal+$10
		lda		#0
		sta		icax1+$10
		mva		#CIOCmdOpen iccmd+$10
		mva		#4 icax1+$10
		mva		#0 icax2+$10
		ldx		#$10
		jsr		ciov
		bpl		open_ok

error:
		jsr		DupPrintError

		ldx		#$10
		mva		#CIOCmdClose iccmd+$10
		jsr		ciov
run_succeeded:
		jmp		input_loop

open_ok:
		;attempt to run it by XIO 40 and then exit (this usually does
		;not return on success)
		mva		#40 iccmd+$10
		mva		#0 icax1+$10
		jsr		ciov
		bpl		run_succeeded
		bmi		error

com_ext:
		dta		'MOC.'
.endp

;==========================================================================
.proc DupPrintError
		sty		lasterr
		ldx		#<msg_error
		jsr		DupPrintMessage
		lda		lasterr
		pha
		sec
		sbc		#100
		bcc		no_hundreds
		sta		lasterr
		lda		#'1'
		jsr		DupPutchar
no_hundreds:
		;do tens
		ldx		#'0'
		lda		lasterr
		cmp		#10
		bcc		no_tens
tens_loop:
		cmp		#10
		bcc		tens_done
		inx
		sbc		#10
		bcs		tens_loop

tens_done:
		pha
		txa
		jsr		DupPutchar
		pla
no_tens:
		ora		#$30
		jsr		DupPutchar

		pla

		cmp		#$ac
		bcs		xit

		sta		lasterr
		lda		#' '
		jsr		DupPutchar

		mwa		#msg_errors-1 msg_ptr
msg_loop:
		dec		lasterr
		bpl		print_loop
skip_loop:
		jsr		getchar
		bne		skip_loop
		beq		msg_loop

print_loop_2:
		jsr		DupPutchar
print_loop:
		jsr		getchar
		bne		print_loop_2

xit:
		lda		#$9b
		jmp		DupPutchar
		
getchar:
		inw		msg_ptr
		ldy		#0
		lda		(msg_ptr),y
		rts
.endp

;==========================================================================
; Entry:
;	X = start of message
;
.proc DupPrintMessage
		ldy		#>msg_base
.def :DupPrintMessageYX
		stx		msg_ptr
		sty		msg_ptr+1
put_loop:
		ldy		#0
		lda		(msg_ptr),y
		beq		xit
		jsr		DupPutchar
		inw		msg_ptr
		jmp		put_loop

xit:
		rts
.endp

;==========================================================================
.proc DupPutchar
		sta		ciochr
		lda		icpth
		pha
		lda		icptl
		pha
		lda		ciochr
		ldx		#0
		rts
.endp

;==========================================================================
; Retrieve next filename on command line.
;
; Exit:
;	Z = 0: no filename retrieved
;	Z = 1: filename retrieved of the form (Dn:<file>)
;	fn_ptr: points to filename, ending in EOL
;
.proc DupGetFilename
		jmp		$0100
getfn_vec = *-2
.endp

;==========================================================================
.proc DupSetupReadLine
		lda		dosvec
		clc
		adc		#dosvecoff_lnbuf
		sta		icbal,x
		lda		#0
		sta		icblh,x
		adc		dosvec+1
		sta		icbah,x
		lda		#64
		sta		icbll,x
		lda		#CIOCmdGetRecord
		sta		iccmd,x
		rts
.endp

;==========================================================================
.proc DupDoIO
		jsr		ciov
		bmi		error
		rts
error:
		ldx		#$ff
		txs
		jsr		DupPrintError
		jmp		DupMain.input_loop
.endp

;==========================================================================
.proc DupCloseIOCB1
		ldx		#$10
		mva		#CIOCmdClose iccmd+$10
		jmp		ciov
.endp

;==========================================================================
.proc DupDispatchIntrinsic
		;dispatch to intrinsic
		ldx		intrin_idx
		lda		intrinsic_dispatch_hi-1,x
		pha
		lda		intrinsic_dispatch_lo-1,x
		pha
		rts
.endp

;==========================================================================
intrinsic_commands:
		dta		'CAR','T'+$80
		dta		'DI','R'+$80
		dta		0


.macro _INTRINSIC_TABLE
		dta		:1[intrin_cart - 1]
		dta		:1[intrin_dir - 1]
.endm

intrinsic_dispatch_lo:
		_INTRINSIC_TABLE	<

intrinsic_dispatch_hi:
		_INTRINSIC_TABLE	>

;==========================================================================
.proc intrin_cart
		;check if we have a cartridge
		lda		ramtop
		cmp		#$a1
		bcs		no_cartridge

		;restore zero page
		ldx		#0
		mva:rpl	zpsave,x $80,x+

		;return to DOS so it can restore the user area and invoke the cart
		ldx		#$ff
		txs
		jmp		DOSRunCart

no_cartridge:
		ldx		#<msg_no_cartridge
		jmp		DupPrintMessage

.endp

;==========================================================================
.proc intrin_dir
restart:
		;parse out filename
		jsr		DupGetFilename
		bne		have_filename

		;we have no filename... shove D: into the command line and retry
		ldy		#dosvecoff_lnoff
		lda		#0
		sta		(dosvec),y

		ldx		#2
		ldy		#dosvecoff_lnbuf+2
lncopy_loop:
		lda		d_path,x
		sta		(dosvec),y-
		dex
		bpl		lncopy_loop
		bmi		restart

have_filename:
		;check if the filename ends in just a drive prefix
		ldy		#3
		lda		(fn_ptr),y
		cmp		#$9b
		bne		have_pattern

		;no pattern... add *.*
		lda		#'*'
		sta		(fn_ptr),y+
		lda		#'.'
		sta		(fn_ptr),y+
		lda		#'*'
		sta		(fn_ptr),y+
		lda		#$9b
		sta		(fn_ptr),y

have_pattern:
		;open IOCB #1 for directory read mode
		mwa		fn_ptr icbal+$10
		mva		#CIOCmdOpen iccmd+$10
		mva		#$06 icax1+$10
		mva		#$00 icax2+$10
		ldx		#$10
		jsr		DupDoIO

read_loop:
		;read a line at a time
		ldx		#$10
		jsr		DupSetupReadLine
		jsr		ciov
		bpl		read_ok
		cpy		#CIOStatEndOfFile
		beq		read_done
read_ok:
		ldx		#0
		jsr		DupSetupReadLine
		mva		#CIOCmdPutRecord iccmd
		jsr		ciov
		jmp		read_loop

read_done:
		;we let the command interpreter close IOCB #1
		rts

d_path:
		dta		'D:',$9B
.endp

;==========================================================================
; BSS
;
zpsave:
		.ds		$80

;==========================================================================
		run		DupMain
