; Altirra BASIC - I/O module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

;==========================================================================
; Print a message from the message database.
;
; Entry:
;	X = LSB of message pointer
;
.proc IoPrintMessage
		stx		inbuff
loop:
		ldx		inbuff
		lda		msg_base&$ff00,x
		beq		xit
		jsr		IoPutCharAndInc
		jmp		loop
xit:
		rts
.endp

;==========================================================================
IoPrintInt:
		jsr		ifp
IoPrintNumber:
		jsr		fasc
.proc printStringINBUFF
loop:
		ldy		#0
		lda		(inbuff),y
		pha
		and		#$7f
		jsr		IoPutCharAndInc
		pla
		bpl		loop
		rts
.endp

;==========================================================================
.proc IoConvNumToHex
		php
		jsr		ldbufa
		jsr		fpi
		ldy		#0
		plp
		lda		fr0+1
		bcc		force_16bit
		beq		print_8bit
force_16bit:
		jsr		print_digit_pair
print_8bit:
		lda		fr0
print_digit_pair:
		pha
		lsr
		lsr
		lsr
		lsr
		jsr		print_digit
		pla
		and		#$0f
print_digit:
		cmp		#10
		scc:adc	#6
		adc		#$30
		sta		(inbuff),y
		iny
		rts
.endp

;==========================================================================
.proc IoPutCharAndInc
		inw		inbuff
		jmp		putchar
.endp

;==========================================================================
IoPutCharDirectX = putchar.direct_with_x
IoPutCharDirect = putchar.direct

IoPutNewline:
		lda		#$9b
		dta		{bit $0100}
IoPutSpace:
		lda		#' '
.proc putchar
		dec		ioPrintCol
		bne		not_tabstop
		mvx		ptabw ioPrintCol
not_tabstop:
direct:
		ldx		iocbidx
direct_with_x:
		jsr		dispatch
		tya

.def :ioCheck = *
		bpl		done
dispatch_error:
		sty		errno
		jmp		errorDispatch
		
dispatch:
		sta		ciochr
		lda		icax1,x
		sta		icax1z
		lda		icpth,x
		pha
		lda		icptl,x
		pha
		lda		ciochr
done:
		rts
.endp

;==========================================================================
ioChecked = IoDoCmd._check_entry2
IoDoCmdX = IoDoCmd._with_x
.proc IoDoCmd
		ldx		iocbidx
_with_x:
		sta		iccmd,x
_check_entry2:
		jsr		ciov
		jmp		ioCheck
.endp

;==========================================================================
; Issue I/O call with a filename.
;
; Entry:
;	A = command to run
;	fr0 = Pointer to string info (ptr/len)
;	iocbidx = IOCB to use
;
; ICBAL/ICBAH is automatically filled in by this fn. Because BASIC strings
; are not terminated, this routine temporarily overwrites the end of the
; string with an EOL, issues the CIO call, and then restores that byte.
; The string is limited to 255 characters.
;
; I/O errors are checked after calling CIO and the error handler is issued
; if one occurs.
;
IoDoOpenReadWithFilename:
		lda		#4
IoDoOpenWithFilename:
		ldx		iocbidx
		sta		icax1,x
		lda		#CIOCmdOpen
.proc IoDoWithFilename
		;stash command
		ldx		iocbidx
		sta		iccmd,x
						
		;move pointer to ICBAL/H
		mwa		fr0 icbal,x
		
		;call CIO
		jsr		IoTerminateString
		jsr		ciov
		jsr		IoUnterminateString
		
		;now we can check for errors and exit
		jmp		ioCheck
.endp

;==========================================================================
IoSetupIOCB7:
		ldx		#$70
		stx		iocbidx
IoCloseX = IoClose.with_IOCB_X
.proc IoClose
		ldx		iocbidx
with_IOCB_X:
		lda		#CIOCmdClose
.def :IoTryCmdX = *
		sta		iccmd,x
		jmp		ciov
.endp

;==========================================================================
; Replace the byte after a string with an EOL terminator.
;
; Entry:
;	FR0 = string pointer
;	FR0+2 = string length (16-bit)

; Registers:
;	A, Y modified; X preserved
;
; Exit:
;	INBUFF = string pointer
;
; This is needed anywhere where a substring needs to be passed to a module
; that expects a terminated string, such as the math pack or CIO. This
; will temporarily munge the byte _after_ the string, which can be a
; following program token, the first byte of another string or array, or
; even the runtime stack. Therefore, the following byte MUST be restored
; ASAP.
;
; The length of the string is limited to 255 characters.
;
.proc IoTerminateString
		;compute termination offset		
		ldy		fr0+2
		lda		fr0+3
		seq:ldy	#$ff
		sty		ioTermOff
		
		;copy term address
		mwa		fr0 inbuff
		
		;save existing byte
		lda		(inbuff),y
		sta		ioTermSave
		
		;stomp it with an EOL
		lda		#$9b
		sta		(inbuff),y
		rts
.endp

;==========================================================================
; Entry:
;	INBUFF = string pointer
;
; Registers:
;	Y, P preserved
;
.proc IoUnterminateString
		php
		tya
		pha
		ldy		ioTermOff
		lda		ioTermSave
		sta		(inbuff),y
		pla
		tay
		plp
		rts
.endp

;==========================================================================
; Open the cassette (C:) device or any other stock device.
;
; Entry (IoOpenCassette):
;	None
;
; Entry (IoOpenStockDeviceIOCB7):
;	A = AUX1 mode
;	Y = Low byte of device name address in constant page
;
; Entry (IoOpenStockDevice):
;	A = AUX1 mode
;	X = IOCB #
;	Y = Low byte of device name address in constant page
;
.proc IoOpenCassette
		ldy		#<devname_c
.def :IoOpenStockDeviceIOCB7 = *
		ldx		#$70
		stx		iocbidx
.def :IoOpenStockDevice = *
		sta		icax1,x
		tya
		sta		icbal,x
		mva		#>devname_c icbah,x
		lda		#CIOCmdOpen
		jmp		IoDoCmdX
.endp

;==========================================================================
.proc IoSetupReadLine
		;we are using some pretty bad hacks here:
		;- GET RECORD and >LBUFF are $05
		;- <LBUFF is $80
		mva		#CIOCmdGetRecord iccmd,x
		sta		icbah,x
		lda		#$80
		sta		icbal,x
		asl
		sta		icblh,x
		lda		#$ff
		sta		icbll,x
		rts
.endp
