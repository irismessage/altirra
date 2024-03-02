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

.proc BootDisk
	;read first sector to $0400
	mva		#1		dunit
	mva		#$52	dcomnd
	mwa		#$0400	dbuflo
	mwa		#1		daux1
	jsr		dskinv
	bmi		fail
	
	mva		$0400	dflags
	mva		$0401	dbsect
	mwa		$0402	bootad
	mwa		$0404	dosini
	
	;copy page 0 to boot address
	mwa		$0402	a0
	mwa		a0		dbuflo
	ldy		#$7f
page0copy:
	lda		$0400,y
	sta		(a0),y
	dey
	bpl		page0copy

	;load remaining sectors
sectorloop:
	dec		dbsect
	beq		loaddone
	inc		daux1
	adw		dbuflo #$80
	jsr		dskinv
	bpl		sectorloop
	
	;read failed
fail:
	cpy		#SIOErrorTimeout
	bne		failmsg
	rts
	
failmsg:
	jsr		BootShowError
	jmp		BootDisk

loaddone:
	;Restore load address; this is necessary for the smb demo (1.atr) to load.
	;The standard OS does this because it has a buggy SIO handler and always
	;loads into and copies from $0400. Most boot loaders, like the DOS 2.0
	;loader, don't have this problem because they don't rely on the value
	;of DBYTLO.
	mwa		#$0400 dbuflo

	mva		#1 boot?
	jsr		multiboot
	jmp		appinit
	
multiboot:
	lda		bootad
	add		#$05
	tax
	lda		bootad+1
	adc		#0
	pha
	txa
	pha
	rts
	
appinit:
	jmp		(dosini)

.endp

;============================================================================

.proc BootCassette
	;open cassette device
	jsr		csopiv
	
	;read first block
	jsr		rblokv
	bmi		load_failure
	
	mva		casbuf+4 iccomt
	mwa		casbuf+5 bufadr
	mwa		casbuf+7 casini
	
	;copy init address
	mwa		bufadr ramlo

block_loop:
	ldy		#$7f
copy_block:
	lda		casbuf+3,y
	sta		(bufadr),y
	dey
	bpl		copy_block
	
	;update write address
	lda		bufadr
	eor		#$80
	sta		bufadr
	smi:inc	bufadr+1
	
	dec		iccomt
	beq		block_loop_exit

	;read next block
	jsr		rblokv
	bmi		load_failure
	jmp		block_loop

block_loop_exit:
	
	;set cassette boot flag
	mva		#2 boot?

	;run loader
	jsr		go_loader

	;run cassette init routine
	jsr		go_init
	
	;run application
	jmp		(dosvec)

load_failure:
	jsr		CassetteClose
	jmp		BootShowError

go_loader:
	jmp		(ramlo)
	
go_init:
	jmp		(casini)
.endp

;============================================================================

.proc BootShowError
	ldx		#0
msgloop:
	txa
	pha
	lda		errormsg,x
	jsr		EditorPutByte
	pla
	tax
	inx
	cpx		#11
	bne		msgloop
	rts
	
errormsg:
	dta		'BOOT ERROR',$9B
.endp

	nop
