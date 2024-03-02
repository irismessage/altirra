;	Altirra - Atari 800/800XL/5200 emulator
;	Additions - color map utility
;	Copyright (C) 2008-2017 Avery Lee
;
;	Copying and distribution of this file, with or without modification,
;	are permitted in any medium without royalty provided the copyright
;	notice and this notice are preserved.  This file is offered as-is,
;	without any warranty.

		icl		'kernel.inc'
		icl		'kerneldb.inc'
		icl		'hardware.inc'

pfptr	= $80
		; $81
dlsav	= $87
		; $88
dlisav	= $89
		; $8A
prsav	= $8B

colsav	= $a0

		org		$2200

;==========================================================================
.proc Main
		;initialize tables
		ldx		#136
		ldy		#0
colinit_loop:
		txa
		sec
		sbc		#1
		lsr
		lsr
		lsr
		tay
		lda		col_tab,y
		sta		coldat,x
		dex
		bne		colinit_loop

		ldy		#16
		ldx		#136
charinit_loop1:
		mva		char_tab_lo,y pfptr
		mva		char_tab_hi,y pfptr+1
		tya
		pha
		ldy		#0
charinit_loop2:
		lda		(pfptr),y
		sta		chardat,x
		dex
		iny
		cpy		#8
		bne		charinit_loop2
		pla
		tay
		dey
		bpl		charinit_loop1

		;save and swap colors
		ldx		#8
swapcol_loop:
		mva		pcolr0,x colsav,x
		mva		color_table,x pcolr0,x
		dex
		bpl		swapcol_loop

		;swap in display list handler
		mva		#$40 nmien
		mwa		vdslst dlisav
		mwa		#DliHandler vdslst

		;swap in display list and enable DLI
		mwa		sdlstl dlsav
		mva		gprior prsav
		sei
		mwa		#dlist sdlstl
		mva		#$c0 nmien
		mva		#$11 gprior
		cli

		;position masking sprites
		mva		#0 gractl
		sta		pcolr0
		sta		pcolr1
		ldx		#17
		mva:rpl	pmsetup_table,x hposp0,x-

		;wait for a key
		lda		#$ff
		sta		ch
		ldx		#0
waitkey:
		stx		atract
		cmp		ch
		beq		waitkey
		sta		ch

		;restore colors, display list, and character set
		sei
		mva		#$40 nmien
		mwa		dlisav vdslst
		mwa		dlsav sdlstl
		mva		prsav gprior
		ldx		#8
		mva:rpl	colsav,x pcolr0,x-
		cli

		;shut off players and missiles
		ldx		#7
		lda		#0
pmoff_loop:
		sta		hposp0,x
		sta		grafp0,x
		dex
		bpl		pmoff_loop

		;wait for display change to take place
		sec
		ror		strig0
		lda:rmi	strig0

		rts

pmsetup_table:
		dta		$74,$b8,$10,$d0		;hposp0-p3
		dta		$40,$48,$50,$58		;hposm0-m3
		dta		$00,$00,$03,$03,$ff	;sizep0-p3,sizem
		dta		$f0,$ff,$ff,$ff,$00	;grafp0-p3,grafm

color_table:
		dta		$00,$00,$00,$00
		dta		$00,$0e,$00,$00,$00

col_tab:
		dta		$10
		:16 dta	[15-#]*16

char_tab_lo:
		dta		<["1"*8+$E000]
		:6 dta <[["A"+(5-#)]*8+$E000]
		:10 dta <[["0"+(9-#)]*8+$E000]

char_tab_hi:
		dta		>["1"*8+$E000]
		:6 dta >[["A"+(5-#)]*8+$E000]
		:10 dta >[["0"+(9-#)]*8+$E000]
.endp

;==========================================================================
.proc DliHandler
		pha
		tya
		pha
		txa
		pha
		sta		wsync
		ldx		#136
		lda		#0
loop:
		sta		wsync
		sta		colbk
		lda		chardat,x
		sta		playfield+16
		lda		#$ff
		sta		grafm
		lda		coldat,x
		sta		colpf3
		sta		colbk
		nop
		ldy		#$51
		sty		prior
		nop
		nop
		bit		$01
		lda		#0
		ldy		#$11
		dex
		sty		prior
		bne		loop

		lda		#0
		sta		wsync
		sta		colbk
		sta		grafm

		pla
		tax
		pla
		tay
		pla
		rti
.endp

;==========================================================================

		org		$2e00
chardat:

		org		$2f00
coldat:


		org		$3000

dlist:
		dta		$70
		dta		$70
		dta		$e0
		dta		$00
		:136 dta $4f, a(playfield)
		dta		$42, a(playfield2)
		dta		$70
		dta		$02
		dta		$41, a(dlist)

playfield:
		:4 dta	0
		:4 dta	$AA
		:4 dta	$55
		:6 dta	0
		:16	dta #*$11
		:7 dta	0
playfield2:
		;		 0123456789012345678901234567890123456789
		dta		"    Even Odd      0123456789ABCDEF      "
		dta		"   Artifacting      Color table         "

		run		Main
