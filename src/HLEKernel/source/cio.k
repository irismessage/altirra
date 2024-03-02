;	Altirra - Atari 800/800XL emulator
;	Kernel ROM replacement
;	Copyright (C) 2008 Avery Lee
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

;==============================================================================
;CIO status bytes
CIOStatBreak		= $80	;break key abort
CIOStatIOCBInUse	= $81	;IOCB in use
CIOStatUnkDevice	= $82	;unknown device
CIOStatWriteOnly	= $83	;opened for write only
CIOStatInvalidCmd	= $84	;invalid command
CIOStatNotOpen		= $85	;device or file not open
CIOStatInvalidIOCB	= $86	;invalid IOCB number
CIOStatReadOnly		= $87	;opened for read only
CIOStatEndOfFile	= $88	;end of file reached
CIOStatTruncRecord	= $89	;record truncated
CIOStatTimeout		= $8A	;device timeout
CIOStatNAK			= $8B	;device NAK
CIOStatSerFrameErr	= $8C	;serial bus framing error
CIOStatCursorRange	= $8D	;cursor out of range
CIOStatSerOverrun	= $8E	;serial frame overrun error
CIOStatSerChecksum	= $8F	;serial checksum error
CIOStatDeviceDone	= $90	;device done error
CIOStatBadScrnMode	= $91	;bad screen mode
CIOStatNotSupported	= $92	;function not supported by handler
CIOStatOutOfMemory	= $93	;not enough memory
CIOStatDriveNumErr	= $A0	;disk drive # error
CIOStatTooManyFiles	= $A1	;too many open disk files
CIOStatDiskFull		= $A2	;disk full
CIOStatFatalDiskIO	= $A3	;fatal disk I/O error
CIOStatFileNumDiff	= $A4	;internal file # mismatch
CIOStatFileNameErr	= $A5	;filename error
CIOStatPointDLen	= $A6	;point data length error
CIOStatFileLocked	= $A7	;file locked
CIOStatInvDiskCmd	= $A8	;invalid command for disk
CIOStatDirFull		= $A9	;directory full (64 files)
CIOStatFileNotFound	= $AA	;file not found
CIOStatInvPoint		= $AB	;invalid point

CIOCmdOpen			= $03
CIOCmdGetRecord		= $05
CIOCmdGetChars		= $07
CIOCmdPutRecord		= $09
CIOCmdPutChars		= $0B
CIOCmdClose			= $0C
CIOCmdGetStatus		= $0D
CIOCmdSpecial		= $0E	;$0E and up is escape

.proc CIOInit
	lda		#$ff
	sta		ichid
	sta		ichid+$10
	sta		ichid+$20
	sta		ichid+$30
	sta		ichid+$40
	sta		ichid+$50
	sta		ichid+$60
	sta		ichid+$70
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
.proc CIO
	;stash IOCB offset (X) and acc (A)
	sta		ciochr
	stx		icidno

	;validate IOCB offset
	txa
	and		#$8f
	beq		validIOCB
		
	;return invalid IOCB error
	ldy		#CIOStatInvalidIOCB
	jmp		xit
	
validIOCB:

	;copy IOCB to ZIOCB
	;
	; [OSManual p236] "Although both the outer level IOCB and the Zero-page
	; IOCB are defined to be 16 bytes in size, only the first 12 bytes are
	; moved by CIO."	
	;
	ldy		#0
copyToZIOCB:
	lda		ichid,x
	sta		ziocb,y
	inx
	iny
	cpy		#12
	bne		copyToZIOCB
	
	;check if we're handling the OPEN command
	lda		iccomz
	sec
	sbc		#CIOCmdOpen
	beq		cmdOpen
	bcs		dispatch
	
	;invalid command <$03
cmdInvalid:
	ldy		#CIOStatInvalidCmd
	jmp		xit
	
dispatch:
	;check if the IOCB is open
	asl
	tax
	lda		ichidz
	bpl		isOpen
	
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
	jmp		xit
	
preOpen:
	;If the device is not open when a SPECIAL command is issued, parse the path
	;and soft-open the device in the zero page IOCB.
	jsr		parsePath
isOpen:
	;check for special command
	lda		iccomz
	cmp		#CIOCmdGetStatus
	beq		cmdGetStatusSoftOpen
	cmp		#CIOCmdSpecial
	bcs		cmdSpecialSoftOpen
	
	;dispatch through command table
	lda		commandTable-1,x
	pha
	lda		commandTable-2,x
	pha
	rts
	
;--------------------------------------------------------------------------
cmdGetStatusSoftOpen:
	ldy		#9
	jsr		invoke
	jsr		soft_close
	jmp		xit

cmdGetStatus:
	ldy		#9
	jsr		invoke
	jmp		xit

;--------------------------------------------------------------------------
cmdSpecialSoftOpen:
	ldy		#11
	jsr		invoke
	jsr		soft_close
	jmp		xit
	
cmdSpecial:
	ldy		#11
	jsr		invoke
	jmp		xit
		
;--------------------------------------------------------------------------
; Open command ($03).
;
cmdOpen:
	;check if the IOCB is already open
	lda		ichidz
	cmp		#$ff
	beq		notAlreadyOpen
	
	;IOCB is already open - error
	ldy		#CIOStatIOCBInUse
	jmp		xit
	
notAlreadyOpen:
	jsr		parsePath

	;request open
	ldy		#1
	jsr		invoke
	tya
	bpl		openOK
	jmp		xit
	
openOK:
	;move handler ID and device number to IOCB
	ldx		icidno
	mva		ichidz ichid,x
	mva		icdnoz icdno,x

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

	ldy		#1
	jmp		xit	

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

xit:
	;copy status back to IOCB
	ldx		icidno
	tya
	sta		icsta,x
	php
	lda		ciochr
	plp
	rts
	
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
	ldy		#5
	jsr		invoke
	cpy		#0
	bmi		cmdGetRecordXitTrunc

	;exit if EOL
	cmp		#$9b
	bne		cmdGetRecordBufferFull
cmdGetRecordXitTrunc:
	ldy		#CIOStatTruncRecord
	jmp		cmdGetRecordXit

cmdGetRecord:
	ldy		#5
	jsr		load_vector
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
	dew		icbllz
	inw		icbalz
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
	jmp		xit
	
;--------------------------------------------------------------------------
cmdGetChars:
	ldy		#5
	jsr		load_vector
	lda		icbllz
	ora		icblhz
	beq		cmdGetCharsSingle
cmdGetCharsLoop:
	jsr		invoke_vector
	cpy		#0
	bmi		cmdGetCharsError
	ldy		#0
	sta		(icbalz),y
	inw		icbalz
	dew		icbllz
	bne		cmdGetCharsLoop
	lda		icblhz
	bne		cmdGetCharsLoop
cmdGetCharsError:
	jmp		cmdGetPutDone
	
cmdGetCharsSingle:
	jsr		invoke_vector
	sta		ciochr
	jmp		xit
	
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
	ldy		#7
	jsr		load_vector
	lda		icbllz
	ora		icblhz
	beq		cmdPutRecordEOL
	ldy		#0
	mva		(icbalz),y	ciochr
	jsr		invoke_vector
	tya
	bmi		cmdPutRecordError
	inw		icbalz
	dew		icbllz
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
	ldy		#7
	jsr		load_vector
	lda		icbllz
	ora		icblhz
	beq		cmdPutCharsSingle
cmdPutCharsLoop:
	ldy		#0
	mva		(icbalz),y	ciochr
	jsr		invoke_vector
	inw		icbalz
	dew		icbllz
	bne		cmdPutCharsLoop
	lda		icblhz
	bne		cmdPutCharsLoop
	jmp		cmdGetPutDone
cmdPutCharsSingle:
	lda		ciochr
	jsr		invoke_vector
	jmp		xit
	
;--------------------------------------------------------------------------
cmdClose:
	ldy		#3
	jsr		invoke
	
	ldx		icidno
	mva		#$ff	ichid,x
	jmp		xit

parsePath:
	;pull first character of filename
	ldy		#0
	lda		(icbalz),y

	;search for handler
	ldx		#11*3
findHandler:
	cmp		hatabs,x
	beq		foundHandler
	dex
	dex
	dex
	bpl		findHandler
	
	;return unknown device error
	ldy		#CIOStatUnkDevice
	pla
	pla
	jmp		xit
	
foundHandler:

	;store handler ID
	stx		ichidz
	
	;default to device #1
	mva		#1 icdnoz
	
	;Check for a device number.
	;
	; - D1:-D9: is supported. D0: also gives unit 1, and any digits beyond
	;   the first are ignored.
	;
	iny
	lda		(icbalz),y
	sec
	sbc		#'0'
	beq		nodevnum
	cmp		#10
	bcs		nodevnum
	
	sta		icdnoz
	iny
	
nodevnum:

; We don't validate the colon anymore -- Atari OS allows opening just "C" to get
; to the cassette.
;
;	;check for ':'
;	lda		(icbalz),y
;	cmp		#':'
;	beq		foundColon
;	
;	;invalid filename
;	ldy		#CIOStatFileNameErr
;	jmp		xit
	
foundColon:
	rts

commandTable:
	dta		a(cmdGetRecord-1)	;$04
	dta		a(cmdGetRecord-1)	;$05
	dta		a(cmdGetChars-1)	;$06
	dta		a(cmdGetChars-1)	;$07
	dta		a(cmdPutRecord-1)	;$08
	dta		a(cmdPutRecord-1)	;$09
	dta		a(cmdPutChars-1)	;$0A
	dta		a(cmdPutChars-1)	;$0B
	dta		a(cmdClose-1)		;$0C
	dta		a(cmdGetStatus-1)	;$0D
.endp
