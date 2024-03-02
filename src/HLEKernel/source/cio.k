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
;	Returns
;		A = depends on operation
;		X = IOCB offset (# x 16)
;		Y = status (reflected in P)
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
	;don't issue an error for close ($0C); you can "close #5" as much as you
	;want from basic
	ldy		#1
	lda		iccomz
	cmp		#CIOCmdClose
	beq		ignoreOpen
	cmp		#CIOCmdSpecial
	bcs		preOpen				;closed IOCB is OK for special
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
	cmp		#CIOCmdSpecial
	bcs		cmdSpecial
	
	;dispatch through command table
	lda		commandTable-1,x
	pha
	lda		commandTable-2,x
	pha
	rts
	
cmdSpecial:
	ldy		#11
	jsr		invoke
	jmp		xit
		
	;--------------------------------------------------------------------------
	;	OPEN
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
	mwa		hatabs+1,x bufadr
	ldy		#6
	lda		(bufadr),y
	ldx		icidno
	sta		icptl,x
	iny
	lda		(bufadr),y
	sta		icpth,x

	ldy		#1
	jmp		xit	

invoke:
	ldx		ichidz
	mwa		hatabs+1,x bufadr
	lda		(bufadr),y
	pha
	dey
	lda		(bufadr),y
	pha
	ldy		#CIOStatNotSupported
	ldx		icidno
	lda		ciochr
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
	;check if buffer is full
	lda		icbllz
	bne		cmdGetRecordGetByte
	lda		icblhz
	beq		cmdGetRecordBufferFull
cmdGetRecordGetByte:
	;fetch byte
	ldy		#5
	jsr		invoke
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
	bne		cmdGetRecordGetByte

	;update byte count in IOCB
cmdGetRecordXit:
	ldx		icidno
	sec
	lda		icbll,x
	sbc		icbllz
	sta		icbll,x
	lda		icblh,x
	sbc		icblhz
	sta		icblh,x
	tya
	jmp		xit
	
cmdGetChars:
	lda		icbllz
	ora		icblhz
	beq		cmdGetCharsSingle
cmdGetCharsLoop:
	ldy		#5
	jsr		invoke
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
	jmp		cmdGetRecordXit
cmdGetCharsSingle:
	ldy		#5
	jsr		invoke
	sta		ciochr
	jmp		cmdGetRecordXit
	
cmdPutRecord:
	lda		icbllz
	ora		icblhz
	beq		cmdPutRecordEOL
	ldy		#0
	mva		(icbalz),y	ciochr
	ldy		#7
	jsr		invoke
	tya
	bmi		cmdPutRecordError
	inw		icbalz
	dew		icbllz
	lda		#$9b
	cmp		ciochr
	beq		cmdPutRecordDone
	bne		cmdPutRecord
	ldy		#7
	jsr		invoke
cmdPutRecordError:
cmdPutRecordDone:
	jmp		xit
cmdPutRecordEOL:
	lda		#$9b
	ldy		#7
	jsr		invoke
	jmp		xit
	
cmdPutChars:
	lda		icbllz
	ora		icblhz
	beq		cmdPutCharsSingle
cmdPutCharsLoop:
	ldy		#0
	mva		(icbalz),y	ciochr
	ldy		#7
	jsr		invoke
	inw		icbalz
	dew		icbllz
	bne		cmdPutCharsLoop
	lda		icblhz
	bne		cmdPutCharsLoop
	jmp		xit
cmdPutCharsSingle:
	lda		ciochr
	ldy		#7
	jsr		invoke
	jmp		xit
	
cmdClose:
	ldy		#3
	jsr		invoke
	
	ldx		icidno
	mva		#$ff	ichid,x
	jmp		xit
	
cmdGetStatus:
	ldy		#9
	jsr		invoke
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
	
	;check for a device number
	iny
	lda		(icbalz),y
	sec
	sbc		#'0'
	beq		nodevnum
	cmp		#4
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
	dta		a(cmdInvalid-1)		;$04
	dta		a(cmdGetRecord-1)	;$05
	dta		a(cmdInvalid-1)		;$06
	dta		a(cmdGetChars-1)	;$07
	dta		a(cmdInvalid-1)		;$08
	dta		a(cmdPutRecord-1)	;$09
	dta		a(cmdInvalid-1)		;$0A
	dta		a(cmdPutChars-1)	;$0B
	dta		a(cmdClose-1)		;$0C
	dta		a(cmdGetStatus-1)	;$0D
.endp