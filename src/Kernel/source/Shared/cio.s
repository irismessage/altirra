;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Character Input/Output Facility
;	Copyright (C) 2008-2012 Avery Lee
;
;	This program is free software; you can redistribute it and/or modify
;	it under the terms of the GNU General Public License as published by
;	the Free Software Foundation; either version 2 of the License, or
;	(at your option) any later version.
;
;	This program is distributed in the hope that it will be useful,
;	but WITHOUT ANY WARRANTY; without even the implied warranty of
;	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;	GNU General Public License for more details.
;
;	You should have received a copy of the GNU General Public License
;	along with this program; if not, write to the Free Software
;	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

.proc CIOInit	
	sec
	ldy		#$70
iocb_loop:
	lda		#$ff
	sta		ichid,y
	tya
	sbc		#$10
	tay
	bpl		iocb_loop
	rts
.endp

;==============================================================================
;	Character I/O entry vector
;
;	On entry:
;		X = IOCB offset (# x 16)
;
;	Returns:
;		A = depends on operation
;		X = IOCB offset (# x 16)
;		Y = status (reflected in P)
;
;	Notes:
;		BUFADR must not be touched from CIO. DOS XE relies on this for
;		temporary storage and breaks if it is modified.
;
;	XL/XE mode notes:
;		HNDLOD is always set to $00 afterward, per Sweet 16 supplement 3.
;
;		CIO can optionally attempt a provisional open by doing a type 4 poll
;		over the SIO bus. This happens unconditionally if HNDLOD is non-zero
;		and only after the device is not found in HATABS if HNDLOD is zero.
;		If this succeeds, the IOCB is provisionally opened. Type 4 polling
;		ONLY happens for direct opens -- it does not happen for a soft open.
;
.proc CIO
	;stash IOCB offset (X) and acc (A)
	sta		ciochr
	stx		icidno
	jsr		process
xit:
	;copy status back to IOCB
	ldx		icidno
	tya
	sta		icsta,x
	php
	
.if _KERNEL_XLXE
	mva		#0 hndlod
.endif

	lda		ciochr
	plp
	rts
	
process:
	;validate IOCB offset
	txa
	and		#$8f
	beq		validIOCB
		
	;return invalid IOCB error
	ldy		#CIOStatInvalidIOCB
	rts
	
validIOCB:
	jsr		CIOLoadZIOCB
	
	;check if we're handling the OPEN command
	lda		iccomz
	cmp		#CIOCmdOpen
	beq		cmdOpen
	bcs		dispatch
	
	;invalid command <$03
cmdInvalid:
	ldy		#CIOStatInvalidCmd
	rts
	
dispatch:
	;check if the IOCB is open
	ldy		ichidz
	
.if !_KERNEL_XLXE
	bpl		isOpen
.else
	bmi		not_open
	
	;check for a provisionally open IOCB
	iny
	bpl		isOpen
	
	;okay, it's provisionally open... check if it's a close
	cmp		#CIOCmdClose
	sne:jmp	cmdCloseProvisional
	
	;check if we're allowed to load a handler
	lda		hndlod
	beq		not_open
	
	;try to load the handler
	jsr		CIOLoadHandler
	bpl		isOpen
	rts
.endif
	
not_open:
	;IOCB isn't open - issue error
	;
	;Special cases;
	; - No error issued for close ($0C). This is needed so that extra CLOSE
	;   commands from BASIC don't trip errors.
	; - Get status ($0D) and special ($0E+) do soft open and close if needed.
	;   $0D case is required for Top Dos 1.5a to boot; $0E+ case is encountered
	;   with R: device XIO commands.
	;
	ldy		#1
	lda		iccomz
	cmp		#CIOCmdClose
	beq		ignoreOpen
	cmp		#CIOCmdGetStatus
	bcs		preOpen				;closed IOCB is OK for get status and special
	ldy		#CIOStatNotOpen
ignoreOpen:
	rts
	
preOpen:
	;If the device is not open when a SPECIAL command is issued, parse the path
	;and soft-open the device in the zero page IOCB.
	jsr		CIOParsePath

	;check for special command
	lda		iccomz
	cmp		#CIOCmdGetStatus
	beq		cmdGetStatusSoftOpen
	cmp		#CIOCmdSpecial
	bcs		cmdSpecialSoftOpen
	
isOpen:
	ldx		iccomz
	cpx		#CIOCmdSpecial
	scc:ldx	#$0e

	;load command table vector
	lda		command_table_hi-4,x
	pha
	lda		command_table_lo-4,x
	pha
	
	;preload dispatch vector and dispatch to command
	ldy		vector_preload_table-4,x
load_vector:
	ldx		ichidz
	mwa		hatabs+1,x icax3z
	lda		(icax3z),y
	tax
	dey
	lda		(icax3z),y
	sta		icax3z
	stx		icax3z+1
	rts
	
;--------------------------------------------------------------------------
cmdGetStatusSoftOpen:
	ldy		#9
	bne		invoke_and_soft_close_xit

;--------------------------------------------------------------------------
cmdSpecialSoftOpen:
	ldy		#11
invoke_and_soft_close_xit:
	jsr		invoke
	jmp		soft_close
		
;--------------------------------------------------------------------------
; Open command ($03).
;
cmdOpen:
	;check if the IOCB is already open
	ldy		ichidz
	iny
	beq		notAlreadyOpen
	
	;IOCB is already open - error
	ldy		#CIOStatIOCBInUse
	rts
	
notAlreadyOpen:
	jsr		CIOParsePath
	
	;check for a provisional open and skip the handler call if so
	ldx		ichidz
	inx
	bmi		provisional_open

open_entry:
	;request open
	ldy		#1
	jsr		invoke

	;move handler ID and device number to IOCB
	ldx		icidno
	mva		ichidz ichid,x
	mva		icdnoz icdno,x

	tya
	bpl		openOK
	rts
	
openOK:

	;copy PUT BYTE vector for Atari Basic
	ldx		ichidz
	mwa		hatabs+1,x icax3z
	ldy		#6
	lda		(icax3z),y
	ldx		icidno
	sta		icptl,x
	iny
	lda		(icax3z),y
	sta		icpth,x

provisional_open:
	ldy		#1
	rts

;This routine must NOT touch CIOCHR. The DOS 2.5 Ramdisk depends on seeing
;the last character from PUT RECORD before the EOL is pushed by CIO, so we
;can't force a write of that EOL into CIOCHR to dispatch it here.
invoke:
	jsr		load_vector		
invoke_vector:
	tay
	lda		icax3z+1
	pha
	lda		icax3z
	pha
	tya
	ldy		#CIOStatNotSupported
	ldx		icidno
	rts

cmdGetStatus:
cmdSpecial:
	jmp		invoke_vector
	
;--------------------------------------------------------------------------
soft_close:
	tya
	pha
	ldx		icidno
	ldy		#3
	jsr		invoke
	pla
	tay
	rts
	
;--------------------------------------------------------------------------
cmdGetRecordBufferFull:
	;read byte to discard
	jsr		invoke_vector
	cpy		#0
	bmi		cmdGetRecordXitTrunc

	;exit if EOL
	cmp		#$9b
	bne		cmdGetRecordBufferFull
cmdGetRecordXitTrunc:
	ldy		#CIOStatTruncRecord
	jmp		cmdGetRecordXit

cmdGetRecord:
cmdGetRecordLoop:
	;check if buffer is full
	lda		icbllz
	bne		cmdGetRecordGetByte
	lda		icblhz
	beq		cmdGetRecordBufferFull
cmdGetRecordGetByte:
	;fetch byte
	jsr		invoke_vector
	cpy		#0
	bmi		cmdGetRecordXit
	
	;store byte (even if EOL)
	ldy		#0
	sta		(icbalz),y
	pha
	jsr		advance_pointers
	pla
	
	;loop back for more bytes if not EOL
	cmp		#$9b
	bne		cmdGetRecordLoop

	;update byte count in IOCB
cmdGetRecordXit:
cmdGetPutDone:
	ldx		icidno
	sec
	lda		icbll,x
	sbc		icbllz
	sta		icbll,x
	lda		icblh,x
	sbc		icblhz
	sta		icblh,x
	
	;Several of the routines will exit with return code 0 on success;
	;we need to change that to 1. (required by Pacem in Terris)
	tya
	sne:iny
	rts
	
;--------------------------------------------------------------------------
cmdGetChars:
	lda		icbllz
	ora		icblhz
	beq		cmdGetCharsSingle
cmdGetCharsLoop:
	jsr		invoke_vector
	cpy		#0
	bmi		cmdGetCharsError
	ldy		#0
	sta		(icbalz),y
	jsr		advance_pointers
	bne		cmdGetCharsLoop
	lda		icblhz
	bne		cmdGetCharsLoop
cmdGetCharsError:
	jmp		cmdGetPutDone
	
cmdGetCharsSingle:
	jsr		invoke_vector
	sta		ciochr
	rts
	
;--------------------------------------------------------------------------
; PUT RECORD handler ($09)
;
; Exit:
;	ICBAL/ICBAH: Not changed
;	ICBLL/ICBLH: Number of bytes processed
;
; If the string does not contain an EOL character, one is printed at the
; end. Also, in this case CIOCHR must reflect the last character in the
; buffer and not the EOL. (Required by Atari DOS 2.5 RAMDISK banner)
;
cmdPutRecord:
	lda		icbllz
	ora		icblhz
	beq		cmdPutRecordEOL
	ldy		#0
	mva		(icbalz),y	ciochr
	jsr		invoke_vector
	tya
	bmi		cmdPutRecordError
	jsr		advance_pointers
	lda		#$9b
	cmp		ciochr
	beq		cmdPutRecordDone
	bne		cmdPutRecord
	
cmdPutRecordEOL:
	lda		#$9b
	sta		ciochr
	jsr		invoke_vector
cmdPutRecordError:
cmdPutRecordDone:
	jmp		cmdGetPutDone
	
;--------------------------------------------------------------------------
cmdPutChars:
	lda		icbllz
	ora		icblhz
	beq		cmdPutCharsSingle
cmdPutCharsLoop:
	ldy		#0
	mva		(icbalz),y	ciochr
	jsr		invoke_vector
	jsr		advance_pointers
	bne		cmdPutCharsLoop
	lda		icblhz
	bne		cmdPutCharsLoop
	jmp		cmdGetPutDone
cmdPutCharsSingle:
	lda		ciochr
	jmp		invoke_vector
	
;--------------------------------------------------------------------------

advance_pointers:
	inw		icbalz
	dew		icbllz
	rts

;--------------------------------------------------------------------------
cmdClose:
	jsr		invoke_vector
cmdCloseProvisional:	
	ldx		icidno
	mva		#$ff	ichid,x
	rts

vector_preload_table:
	dta		$05					;$04 (get record)
	dta		$05					;$05 (get record)
	dta		$05					;$06 (get chars)
	dta		$05					;$07 (get chars)
	dta		$07					;$08 (put record)
	dta		$07					;$09 (put record)
	dta		$07					;$0A (put chars)
	dta		$07					;$0B (put chars)
	dta		$03					;$0C (close)
	dta		$09					;$0D (get status)
	dta		$0b					;$0E (special)

command_table_lo:
	dta		<(cmdGetRecord-1)	;$04
	dta		<(cmdGetRecord-1)	;$05
	dta		<(cmdGetChars-1)	;$06
	dta		<(cmdGetChars-1)	;$07
	dta		<(cmdPutRecord-1)	;$08
	dta		<(cmdPutRecord-1)	;$09
	dta		<(cmdPutChars-1)	;$0A
	dta		<(cmdPutChars-1)	;$0B
	dta		<(cmdClose-1)		;$0C
	dta		<(cmdGetStatus-1)	;$0D
	dta		<(cmdSpecial-1)		;$0E

command_table_hi:
	dta		>(cmdGetRecord-1)	;$04
	dta		>(cmdGetRecord-1)	;$05
	dta		>(cmdGetChars-1)	;$06
	dta		>(cmdGetChars-1)	;$07
	dta		>(cmdPutRecord-1)	;$08
	dta		>(cmdPutRecord-1)	;$09
	dta		>(cmdPutChars-1)	;$0A
	dta		>(cmdPutChars-1)	;$0B
	dta		>(cmdClose-1)		;$0C
	dta		>(cmdGetStatus-1)	;$0D
	dta		>(cmdSpecial-1)		;$0E
.endp

;==========================================================================
; Copy IOCB to ZIOCB.
;
; Entry:
;	X = IOCB
;
; [OSManual p236] "Although both the outer level IOCB and the Zero-page
; IOCB are defined to be 16 bytes in size, only the first 12 bytes are
; moved by CIO."	
;
.proc CIOLoadZIOCB
	;We used to do a trick here where we would count Y from $F4 to $00...
	;but we can't do that because the 65C816 doesn't wrap abs,Y within
	;bank 0 even in emulation mode. Argh!
	
	ldy		#0
copyToZIOCB:
	lda		ichid,x
	sta		ziocb,y
	inx
	iny
	cpy		#12	
	bne		copyToZIOCB
	rts
.endp

;==========================================================================
.proc CIOParsePath
	;pull first character of filename and stash it
	ldy		#0
	lda		(icbalz),y
	sta		icax4z
	
	;default to device #1
	ldx		#1
	
	;Check for a device number.
	;
	; - D1:-D9: is supported. D0: also gives unit 1, and any digits beyond
	;   the first are ignored.
	;
	; We don't validate the colon anymore -- Atari OS allows opening just "C" to get
	; to the cassette.
	;
	iny
	lda		(icbalz),y
	sec
	sbc		#'0'
	beq		nodevnum
	cmp		#10
	bcs		nodevnum
	tax
	
	iny
	
nodevnum:
	stx		icdnoz
	
.if _KERNEL_XLXE
	;check if we are doing a true open and if we should do a type 4 poll
	lda		iccomz
	cmp		#CIOCmdOpen
	bne		skip_poll
	
	;clear DVSTAT+0/+1 to indicate no poll
	lda		#0
	sta		dvstat
	sta		dvstat+1
	
	;check if we should do an unconditional poll (HNDLOD nonzero).
	lda		hndlod
	bne		unconditional_poll
	
	;search handler table
	jsr		CIOFindHandler
	beq		found
	
unconditional_poll:
	;do type 4 poll
	jsr		CIOPollForDevice
	bmi		unknown_device
	
	;mark provisionally open
	ldx		icidno
	mva		#$7f ichid,x
	mva		icax4z icax3,x
	mva		dvstat+2 icax4,x
	mwa		#CIOPutByteLoadHandler-1 icptl,x
	mva		icdnoz icdno,x
	ldy		#1
	rts

skip_poll:
.endif

	;search handler table
	jsr		CIOFindHandler
	beq		found
	
unknown_device:
	;return unknown device error
	ldy		#CIOStatUnkDevice
	pla
	pla
found:
	rts
.endp

;==========================================================================
; Attempt to find a handler entry in HATABS.
;
.proc CIOFindHandler
	;search for handler
	lda		icax4z
	ldx		#11*3
findHandler:
	cmp		hatabs,x
	beq		foundHandler
	dex
	dex
	dex
	bpl		findHandler
foundHandler:
	;store handler ID
	stx		ichidz
	rts
.endp

;==========================================================================
; Poll SIO bus for CIO device
;
; Issues a type 4 poll ($4F/$40/devname/devnumber).
;
.if _KERNEL_XLXE
.proc CIOPollForDevice
	sta		daux1
	sty		daux2
	ldy		#$4f
	lda		#$40
	jmp		CIODoHandlerIO
.endp
.endif

;==========================================================================
.if _KERNEL_XLXE
.proc CIODoHandlerIO
	sty		ddevic
	sta		dcomnd
	mvx		#$01 dunit
	mvx		#$40 dtimlo
	mwx		#dvstat dbuflo
	mwx		#4 dbytlo
	mvx		#$40 dstats
	jmp		siov
.endp
.endif

;==========================================================================
; Load handler for a provisionally open IOCB.
;
.if _KERNEL_XLXE
.proc CIOLoadHandler
	;load handler over SIO bus
	mwa		dvstat+2 loadad
	ldx		icidno
	mva		icax4,x ddevic
	jsr		PHLoadHandler
	bcs		fail
	
	;let's see if we can look up the handler now
	ldx		icidno
	mva		icax3,x icax4z
	jsr		CIOFindHandler
	bne		fail
	
	;follow through with open
	jsr		CIO.open_entry
	bpl		ok
fail:
	ldy		#CIOStatUnkDevice
ok:
	rts
.endp
.endif

;==========================================================================
; PUT BYTE handler for provisionally open IOCBs.
;
; This handler is used when an IOCB has been provisionally opened pending
; a handler load over the SIO bus. It is used when a direct call is made
; through ICPTL/ICPTH. If HNDLOD=0, the call fails as handler loading is
; not set up; if it is nonzero, the handler is loaded over the SIO bus and
; then the PUT BYTE call continues if everything is good.
;
.if _KERNEL_XLXE
.proc CIOPutByteLoadHandler
	;save off A/X
	sta		ciochr
	stx		icidno
		
	;check if we're allowed to load a handler and bail if not
	lda		hndlod
	beq		load_error

	;copy IOCB to ZIOCB
	jsr		CIOLoadZIOCB

	;try to load the handler
	jsr		CIOLoadHandler
	bmi		load_error
	
	;all good... let's invoke the standard handler
	lda		ciochr
	ldy		#7
	jsr		CIO.invoke
	jmp		xit
	
load_error:
	ldy		#CIOStatUnkDevice
xit:
	php
	lda		ciochr
	ldx		icidno
	plp
	rts
.endp
.endif
