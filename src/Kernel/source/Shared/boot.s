;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Boot Code
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
	mwa		$0404	dosini
	
	lda		$0402
	sta		bootad
	sta		adress
	sta		dbuflo
	lda		$0403
	sta		bootad+1
	sta		adress+1
	sta		dbuflo+1
	
	ldy		#$7f
page0copy:
	lda		$0400,y
	sta		(adress),y
	dey
	bpl		page0copy

	;load remaining sectors
sectorloop:
	dec		dbsect
	beq		loaddone
	inc		daux1
	lda		dbuflo
	eor		#$80
	sta		dbuflo
	smi:inc	dbufhi
	jsr		dskinv
	bpl		sectorloop
	
	;read failed
fail:
	cpy		#SIOErrorTimeout
	bne		failmsg
	rts
	
failmsg:
.if _KERNEL_USE_BOOT_SCREEN
	jmp		SelfTestEntry
.else
	jsr		BootShowError
	jmp		BootDisk
.endif

loaddone:
	;Restore load address; this is necessary for the smb demo (1.atr) to load.
	;The standard OS does this because it has a buggy SIO handler and always
	;loads into and copies from $0400. Most boot loaders, like the DOS 2.0
	;loader, don't have this problem because they don't rely on the value
	;of DBYTLO.
	mwa		#$0400 dbuflo

	mva		#1 boot?
	jsr		multiboot
	bcs		failmsg
	
	;Diskette Boot Process, step 7 (p.161 of the OS Manual) is misleading. It
	;says that DOSVEC is invoked after DOSINI, but actually that should NOT
	;happen here -- it happens AFTER cartridges have had a chance to run.
	;This is necessary for BASIC to gain control before DOS goes to load
	;DUP.SYS.
	jmp		(dosini)
	
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
.endp


;============================================================================

.proc BootCassette
	;open cassette device
	jsr		csopiv
	
	;read first block
	jsr		rblokv
	bmi		load_failure
	
	mva		casbuf+4 iccomt
	mwa		casbuf+7 casini
	
	;copy init address
	lda		casbuf+5
	sta		bufadr
	clc
	adc		#6				;loader is at load address + 6
	sta		ramlo
	lda		casbuf+6
	sta		bufadr+1
	adc		#0
	sta		ramlo+1

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
