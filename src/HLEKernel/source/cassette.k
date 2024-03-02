;	Altirra - Atari 800/800XL emulator
;	HLE Kernel ROM
;	Copyright (C) 2008-2010 Avery Lee
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

;==========================================================================
.proc CassetteInit
	rts
.endp

;==========================================================================
.proc CassetteOpen
	;check mode byte for read/write modes
	lda		icax1z
	and		#$0c
	ldx		#0
	cmp		#$04			;read?
	beq		found_mode
	cmp		#$08			;write?
	beq		found_write_mode
	
	;invalid mode
	ldy		#CIOStatInvalidCmd
	rts
	
found_write_mode:
	ldx		#$80
found_mode:
	stx		wmode
	
	;stash continuous mode flag
	lda		icax2z
	sta		ftype

	;set cassette buffer size to 128 bytes and mark it empty
	lda		#$80
	sta		bptr
	sta		blim
	
	;clear EOF flag
	lda		#0
	sta		feof
	
	;turn on motor (continuous mode or not)
	lda		#$34
	sta		pactl
	
	;wait for leader (9.6 seconds)
	mva		#$ff timflg
	lda		#1
	ldx		#>576
	ldy		#<576
	jsr		SetVBlankVector
	lda:rne	timflg
	
	;all done
	ldy		#1
	rts
.endp

;==========================================================================
.proc CassetteClose
	;check if we are in write mode
	lda		wmode
	bpl		notwrite
	
	;check if we have data to write
	lda		bptr
	beq		nopartial
	
	;flush partial record ($FA)
	mva		#$fa casbuf+2
	ldy		#'W'
	jsr		CassetteDoIO
	
nopartial:
	;write EOF record ($FE)
	mva		#$fe casbuf+2
	ldy		#'W'
	jsr		CassetteDoIO
	
notwrite:
	;stop the motor
	lda		#$3c
	sta		pactl
	
	;all done
	ldy		#1
	rts
.endp

;==========================================================================
.proc CassetteGetByte
	;check if we can still fetch a byte
fetchbyte:
	ldx		bptr
	cpx		blim
	beq		nobytes

	lda		casbuf+3,x
	inc		bptr
	ldy		#1
	rts
	
nobytes:
	;check if we have an EOF condition
	lda		feof
	beq		noteof

	;signal EOF
	ldy		#CIOStatEndOfFile
	rts
	
noteof:
	;fetch more bytes
	ldy		#'R'
	jsr		CassetteDoIO
	
	;check control byte
	lda		casbuf+2
	cmp		#$fe
	bne		noteofbyte
	
	;found $FE (EOF) - set flag and return EOF
	sta		feof
	bne		nobytes
	
noteofbyte:
	;reset buffer ptr
	mvx		#0 bptr

	;check if we have a partial block
	cmp		#$fa
	bne		notpartialbyte
	
	;set length of partial block and loop back
	mva		casbuf+130 blim
	jmp		fetchbyte
	
notpartialbyte:
	;set buffer size to full
	mvx		#$80 blim
	
	;check if we have a full block and loop back if so
	cmp		#$fc
	beq		fetchbyte
	
	;uh oh... bad control byte.
	ldy		#CIOStatFatalDiskIO
	rts
.endp

;==========================================================================
.proc CassettePutByte
	jsr		BugCheck
.endp

;==========================================================================
.proc CassetteGetStatus
	lda		#1
	rts
.endp

;==========================================================================
.proc CassetteSpecial
	rts
.endp

;==========================================================================
; CassetteDoIO
;
;	Y = SIO command byte
;
.proc CassetteDoIO
	;start the motor if not already running
	lda		#$34
	sta		pactl

rolling_start:
	;issue SIO read/write
	sty		dcomnd
	mwa		#casbuf dbuflo
	mwa		#131 dbytlo
	mwa		#$5f ddevic
	mva		#1 dunit
	jsr		siov
	
	;check if we are in continuous mode (again)
	lda		ftype
	bmi		rolling_stop
	
	;not in continuous mode -- stop the motor
	lda		#$3c
	sta		pactl
	
rolling_stop:
	tya
	rts
.endp

;==========================================================================
.proc CassetteOpenRead
	mva		#$04 icax1z
	mva		#$80 icax2z
	jmp		CassetteOpen
.endp

;==========================================================================
.proc CassetteReadBlock
	ldy		#'R'
	jmp		CassetteDoIO
.endp
