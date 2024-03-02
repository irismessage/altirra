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
	;set cassette buffer size to 128 bytes and mark it empty
	lda		#$80
	sta		bptr
	sta		blim
	
	;clear EOF flag
	lda		#0
	sta		feof
	
	;all done
	ldy		#1
	rts
.endp

;==========================================================================
.proc CassetteClose
	jsr		BugCheck
.endp

;==========================================================================
.proc CassetteGetByte
	;check if we can still fetch a byte
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
	bne		noteof

	;signal EOF
	ldy		#CIOStatEndOfFile
	rts
	
noteof:
	;fetch more bytes
	jsr		BugCheck
.endp

;==========================================================================
.proc CassettePutByte
	jsr		BugCheck
.endp

;==========================================================================
.proc CassetteGetStatus
	jsr		BugCheck
.endp

;==========================================================================
.proc CassetteSpecial
	jsr		BugCheck
.endp
