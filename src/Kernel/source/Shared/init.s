;	Altirra - Atari 800/800XL emulator
;	Kernel ROM replacement - Initialization
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

.if _KERNEL_XLXE
.proc InitBootSignature
	dta		$5C,$93,$25
.endp
.endif

.proc InitHandlerTable
	dta		c'P',a(printv)
	dta		c'C',a(casetv)
	dta		c'E',a(editrv)
	dta		c'S',a(screnv)
	dta		c'K',a(keybdv)
.endp

InitColdStart = InitReset.cold_boot
.proc InitReset
	;mask interrupts and initialize CPU
	sei
	cld
	ldx		#$ff
	txs
	
	;wait for everything to stabilize (0.1s)
	ldy		#140
	ldx		#0
stabilize_loop:
	dex:rne
	dey
	bne		stabilize_loop
	
	.if _KERNEL_XLXE
	;check for warmstart signature (XL/XE)
	ldx		#2
warm_check:
	lda		pupbt1,x
	cmp		InitBootSignature,x
	bne		cold_boot
	dex
	bpl		warm_check
	
	jmp		InitWarmStart
	.endif

cold_boot:
	; 1. initialize CPU
	sei
	cld
	ldx		#$ff
	txs
	
	; 2. clear warmstart flag
	mva		#0 warmst
	
	; 3. test for diagnostic cartridge
	lda		$bffc
	bne		not_diag
	ldx		$bfff			;prevent diagnostic cart from activating if addr is $FFxx
	inx
	bne		not_diag
	ldx		#0
	mva		#$ff $bffc
	sta		$bffc
	cmp		not_diag
	bne		not_diag
	
	; is it enabled?
	bit		$bffd
	bpl		not_diag
	
	; start diagnostic cartridge
	jmp		($bffe)
	
not_diag:
	
	jsr		InitHardwareReset

	.if _KERNEL_XLXE	
	;check for OPTION and enable BASIC (note that we do NOT set BASICF just yet)
	lda		#4
	bit		consol
	beq		no_basic
	
	ldx		#$fd
	
	;check for keyboard present + SELECT or no keyboard + no SELECT and enable game if so
	lda		trig2		;check keyboard present (1 = present)
	asl
	eor		consol		;XOR against select (0 = pressed)
	and		#$02
	seq:ldx	#$bf
	stx		portb		;enable GAME or BASIC

no_basic:
	.endif

	; 4. measure memory -> tramsz
	jsr		InitMemory
	
	; 6. clear memory from $0008 up to [tramsz,0]
	ldy		#8
	mva		#0 a1
	sta		a1+1
clearloop:
	lda		#0
clearloop2:
	sta		(a1),y
	iny
	bne		clearloop2
	inc		a1+1
	lda		a1+1
	cmp		tramsz
	bne		clearloop
	
	; 7. set dosvec to blackboard routine
.if _KERNEL_USE_BOOT_SCREEN
	mwa		#SelfTestEntry dosvec
.else
	mwa		#Blackboard dosvec
.endif
	
	; 8. set coldstart flag
	mva		#$ff coldst
	
.if _KERNEL_XLXE
	; set BASIC flag
	lda		portb
	and		#$02
	sta		basicf
	
	; set warmstart signature
	ldx		#2
	mva:rpl	InitBootSignature,x pupbt1,x-
.endif

	; 9. set screen margins
	; 10. initialize RAM vectors
	; 11. set misc database values
	; 12. enable IRQ interrupts
	; 13. initialize device table
	; 14. initialize cartridges
	; 15. use IOCB #0 to open screen editor (E)
	; 16. wait for VBLANK so screen is initialized
	; 17. do cassette boot, if it was requested
	; 18. do disk boot
	; 19. reset coldstart flag
	; 20. run cartridges or blackboard
	jmp		InitEnvironment
.endp

;==============================================================================
.proc InitWarmStart
	; A. initialize CPU
	sei
	cld
	ldx		#$ff
	txs

	; B. set warmstart flag
	stx		warmst
	
	; reinitialize hardware without doing a full clear
	jsr		InitHardwareReset
	
	.if _KERNEL_XLXE
	; reinitialize BASIC
	lda		basicf
	sne:mva #$fd portb
	.endif
	
	; C. check for diag, measure memory, clear hw registers
	jsr		InitMemory
	
	; D. zero 0010-007F and 0200-03EC (must not clear BASICF).
	ldx		#$5f
	lda		#0
zpclear:
	sta		$0010,x
	dex
	bpl		zpclear
	
	ldx		#0
dbclear:
	sta		$0200,x
	sta		$02ed,x
	inx
	bne		dbclear
	
	; E. steps 9-16 above
	; F. if cassette boot was successful on cold boot, execute cassette init
	; G. if disk boot was successful on cold boot, execute disk init
	; H. same as steps 19 and 20
	jmp		InitEnvironment
.endp

;==============================================================================
.proc InitHardwareReset
	; clear all hardware registers
	ldx		#0
	txa
hwclear:
	sta		$d000,x
	sta		$d200,x
	sta		$d400,x
	inx
	bne		hwclear

	;initialize PIA
.if _KERNEL_XLXE
	lda		#$3c
	ldx		#$38
	ldy		#0
	stx		pactl		;switch to DDRA
	sty		porta		;portA -> input
	sta		pactl		;switch to IORA
	sty		porta		;portA -> $00
	sta		pbctl		;switch to IORB
	dey
	sty		portb		;portB -> $FF
	stx		pbctl		;switch to DDRB
	sty		portb		;portB -> all output
	sta		pbctl		;switch to IORB
.else
	lda		#$3c
	ldx		#$38
	ldy		#0
	stx		pactl		;switch to DDRA
	sty		porta		;portA -> input
	sty		portb		;portB -> input
	sta		pactl		;switch to IORA
	sta		pbctl		;switch to IORB
	sty		porta		;portA -> $00
	sty		portb		;portB -> $00
.endif
	rts
.endp

;==============================================================================
.proc InitMemory	
	; 4. measure memory -> tramsz
	ldy		#$00
	sty		adress
	ldx		#$02
pageloop:
	stx		adress+1
	lda		(adress),y
	eor		#$ff
	sta		(adress),y
	cmp		(adress),y
	bne		notRAM
	eor		#$ff
	sta		(adress),y
	inx
	cpx		#$c0
	bne		pageloop
notRAM:
	stx		tramsz
	
	rts
.endp

;==============================================================================
.proc InitVectorTable1
	dta		a(IntExitHandler_None)
	dta		a(IntExitHandler_A)
	dta		a(IntExitHandler_A)
	dta		a(IntExitHandler_A)
	dta		a(KeyboardIRQ)
	dta		a(SIOInputReadyHandler)
	dta		a(SIOOutputReadyHandler)
	dta		a(SIOOutputCompleteHandler)
	dta		a(IntExitHandler_A)
	dta		a(IntExitHandler_A)
	dta		a(IntExitHandler_A)
	dta		a(IrqHandler)
end:
.endp

;==============================================================================
.proc InitEnvironment	
	mva		tramsz ramsiz
	
	; 9. set screen margins
	mva		#2 lmargn
	mva		#39 rmargn
	
	;set PAL/NTSC flag
	ldx		#0
	lda		pal
	sne:ldx	#$ff
	stx		palnts
	
	; 10. initialize RAM vectors
	ldx		#InitVectorTable1.end-InitVectorTable1-1
	mva:rpl	InitVectorTable1,x vdslst,x-

	mwa		#VBIStage1				vvblki
	mwa		#VBIExit				vvblkd
	mwa		#KeyboardBreakIRQ		brkky
	mwa		#0						cdtma1
	
	; 11. set misc database values
	mva		#$ff brkkey
	mva		#0 memtop
	mva		tramsz memtop+1
	mwa		#$0700 memlo
	
	jsr		DiskInit
	jsr		ScreenInit
	;jsr	DisplayInit
	jsr		KeyboardInit
	;jsr	PrinterInit
	;jsr	CassetteInit
	jsr		cioinv
	jsr		SIOInit
	jsr		IntInitInterrupts

.if _KERNEL_PBI_SUPPORT
	jsr		PBIScan
.endif
	
	; check for START key, and if so, set cassette boot flag
	lda		consol
	ror
	bcc		nocasboot
	mva		#1 ckey
nocasboot:

	; 12. enable IRQ interrupts
	cli
	
	; 13. initialize device table
	ldx		#37
	lda		#0
htabinit2:
	sta		hatabs,x
	dex
	cpx		#14
	bne		htabinit2

	mva:rpl	InitHandlerTable,x hatabs,x-
	
	; 14. initialize cartridges
	mva		#0 tstdat
	lda		$9ffc
	bne		skipCartBInit
	lda		$9ffb
	tax
	eor		#$ff
	sta		$9ffb
	cmp		$9ffb
	stx		$9ffb
	beq		skipCartBInit
	jsr		InitCartB
	mva		#1 tstdat
skipCartBInit:
	
	mva		#0 tramsz
	lda		$bffc
	bne		skipCartAInit
	lda		$bffb
	tax
	eor		#$ff
	sta		$bffb
	cmp		$bffb
	stx		$bffb
	beq		skipCartAInit
	jsr		InitCartA
	mva		#1 tramsz
skipCartAInit:

	; 15. use IOCB #0 ($0340) to open screen editor (E)
	mva		#$03 iccmd		;OPEN
	mva		#$c0 icax1		;read/write, no forced read
	mva		#0 icax2		;mode 0
	mwa		#ScreenEditorName icbal
	ldx		#0
	jsr		ciov
	
	; 16. wait for VBLANK so screen is initialized
	lda		rtclok+2
waitvbl:
	cmp		rtclok+2
	beq		waitvbl

;-----------------------------------------------------------

	.ifdef	_KERNEL_PRE_BOOT_HOOK
	jsr		InitPreBootHook
	.endif

;-----------------------------------------------------------

	; 17. do cassette boot, if it was requested
	; F. if cassette boot was successful on cold boot, execute cassette init
	
	; The cold boot path must check the warm start flag and switch paths if
	; necessary. SpartaDOS X relies on being able to set the warm start
	; flag from its cart init handler.
	
	lda		warmst
	bne		reinitcas
	
	lda		ckey
	bne		postcasboot
	jsr		BootCassette
	jmp		postcasboot

reinitcas:	
	lda		boot?
	cmp		#2
	bne		postcasboot
	jsr		initCassette
postcasboot:

	; 18. do disk boot
	; G. if disk boot was successful on cold boot, execute disk init
	lda		warmst
	bne		reinitDisk
	
	;check for cart B requesting boot
	lda		tstdat
	beq		noCartBBoot
	lda		#$01
	bit		$9ffd
	bne		boot_disk
	lda		tramsz
	beq		postDiskBoot
noCartBBoot:

	;check for cart A requesting boot
	lda		tramsz
	beq		noCartABoot
	lda		#$01
	bit		$bffd
	beq		postDiskBoot
noCartABoot:
boot_disk:
	jsr		BootDisk
	jmp		postDiskBoot
	
reinitDisk:
	lda		boot?
	cmp		#1
	bne		postDiskBoot
	jsr		initDisk
postDiskBoot:

	; H. same as steps 19 and 20
	; 19. reset coldstart flag
	
	mva		#0 coldst
	
	; 20. run cartridges or blackboard
	
	; try to boot cart A
	lda		tramsz
	beq		NoBootCartA
	lda		#$04
	bit		$bffd
	beq		NoBootCartA
	jmp		($bffa)
NoBootCartA:

	; try to boot cart B
	lda		tstdat
	beq		NoBootCartB
	bit		$9ffd
	beq		NoBootCartB
	jmp		($9ffa)
NoBootCartB:

	; run blackboard
	jmp		(dosvec)

initCassette:
	jmp		(casini)

initDisk:
	jmp		(dosini)
	
InitCartA:
	jmp		($bffe)

InitCartB:
	jmp		($9ffe)
	
ScreenEditorName:
	dta		c"E",$9B

.endp

;==============================================================================

	nop
