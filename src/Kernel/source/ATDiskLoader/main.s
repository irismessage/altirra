;	Altirra - Atari 800/800XL/5200 emulator
;	Disk-based snapshot loader
;	Copyright (C) 2008-2011 Avery Lee
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

rtclok	equ	$12
a0		equ	$80
a1		equ	$82
d0		equ	$88
d1		equ	$89
loadpr	equ	$8a
		;	$8b
sdlstl	equ	$0230			;shadow for DLISTL ($D402)
sdlsth	equ	$0231			;shadow for DLISTH ($D403)
gprior	equ	$026f			;shadow for PRIOR ($D01B)
pcolr0	equ	$02c0			;shadow for COLPM0 ($D012)
dunit	equ $0301			;device number
dcomnd	equ $0302			;command byte
dbuflo	equ $0304			;buffer address lo
dbufhi	equ $0305			;buffer address hi
dbytlo	equ $0308			;byte count lo
dbythi	equ $0309			;byte count hi
daux1	equ $030a			;sector number lo
daux2	equ $030b			;sector number hi
hposm0	equ	$d004
sizem	equ	$d00c
grafm	equ	$d011
gractl	equ	$d01d
portb	equ	$d301
pbctl	equ	$d303
dmactl	equ	$d400
vcount	equ	$d40b
nmien	equ	$d40e
wsync	equ	$d40a
dskinv	equ	$e453
siov	equ	$e459

;==========================================================================
; Memory map timeline:
;	0800-09FF				Loader
;	4000-77FF				Stage 1 load (14K, 112 sectors)
;	4000-4FFF -> C000-CFFF	Stage 1a copy
;	5000-77FF -> D800-FFFF	Stage 1b copy
;	0A00-BFFF				Stage 2 load (45.5K, 364 sectors)
;	4000-49FF(E)			Stage 3 load (10K, 80 sectors)
;	2400-27FF -> 7400-77FF(E)	Relocation
;	4000-49FF -> 0000-09FF	Stage 3 copy
;	
		opt		o+h-f+
		org		$0800
		dta		$00				;flags
		dta		$04				;number of sectors
		dta		a($0800)		;load address
		dta		a($0806)		;init address

__main:
		;replace display list
		sei
		mwa		#display_list sdlstl
		cli
	
		;set up progress bar
		lda		#0
		sta		loadpr
		sta		loadpr+1
		
		;wait for vbl
		lda		rtclok+2
		cmp:req	rtclok+2

		;4000-77FF				Stage 1 load (14K, 112 sectors)
		lda		#$40
		ldx		#112
		jsr		LoadBlock

		;kill interrupts and page out kernel ROM
		sei
		mva		#0 nmien
		lda		pbctl
		ora		#$04
		sta		pbctl
		lda		portb
		and		#$fe
		sta		portb

		;4000-4FFF -> C000-CFFF	Stage 1a copy
		ldy		#$40
		lda		#$c0
		ldx		#$10
		jsr		MoveBlock

		;5000-77FF -> D800-FFFF	Stage 1b copy
		ldy		#$50
		lda		#$d8
		ldx		#$28
		jsr		MoveBlock

		;page kernel ROM back in and re-enable interrupts
		lda		portb
		ora		#$01
		sta		portb
		mva		#$40 nmien
		cli

		;0A00-BFFF				Stage 2 load (45.5K, 364 sectors)
		lda		#$0a
		ldx		#0
		jsr		LoadBlock
		lda		#$8a
		ldx		#108
		jsr		LoadBlock

		;enable extended RAM at bank 0
		lda		#$ef
		sta		portb
		
		;4000-49FF(E)			Stage 3 load (2.5K, 20 sectors)
		lda		#$40
		ldx		#20
		jsr		LoadBlock

		;copy stage 3 loader
		ldx		#0
s3copy:
		lda		$0900,x
		sta		$7f00,x
		inx
		bne		s3copy
		jmp		stage3

;==========================================================================
; display
;==========================================================================

display_list:
		dta		$70
		dta		$70
		dta		$70
		dta		$42,a(playfield)
		dta		$41,a(display_list)

playfield:
		;		  0123456789012345678901234567890123456789
		dta		d'Altirra Loader                          '

;==========================================================================
; Inputs:
;  A		High byte of address
;  X		Sector count
;  DAUX2:DAUX1	Starting sector
;
.proc LoadBlock
		sta		dbufhi
		stx		d0
		lda		#0
		sta		dbuflo
		sta		dbythi
		mva		#$80 dbytlo
		mva		#$52 dcomnd
		mva		#1 dunit
sectorloop:
		inw		daux1
		jsr		dskinv
		lda		loadpr
		clc
		adc		#((20*256) / 497)
		sta		loadpr
		ldx		loadpr+1
		scc:inx
		stx		loadpr+1
		lda		#$80
		sta		playfield+20,x
		eor		dbuflo
		sta		dbuflo
		sne:inc	dbufhi
		dec		d0
		bne		sectorloop
		rts
.endp

;==========================================================================
; Inputs:
;  Y		Source page
;  A		Destination page
;  X		Pages to move
;
.proc MoveBlock
		sty		a0+1
		sta		a1+1
		ldy		#0
		sty		a0
		sty		a1
copyloop:
		lda		(a0),y
		sta		(a1),y
		iny
		bne		copyloop
		inc		a0+1
		inc		a1+1
		dex
		bne		copyloop
		rts
.endp

;===========================================================================
; STAGE 3 LOADER
		org		*+$7600,*
.proc stage3
		;disable interrupts and DMA
		sei
		mva		#0 nmien
		mva		#0 dmactl

		;*** NO ZERO PAGE PAST THIS POINT ***

		;copy $4000-49FF down to $0000-09FF
		ldy		#$0a
		ldx		#0
copyloop:
		lda		$4000,x
		dta $9d, $00, $00		;sta $0000,x
		inx
		bne		copyloop
		inc		copyloop+2
		inc		copyloop+5
		dey
		bne		copyloop

		;reload GTIA
		ldx		#$1d
gtload:
		lda		gtiadat,x
		sta		$d000,x
		dex
		bpl		gtload

		;reload POKEY
		ldx		#8
pkload:
		lda		pokeydat,x
		sta		$d200,x
		dex
		bpl		pkload

		;reload ANTIC
		; $D401 CHACTL
		; $D402 DLISTL
		; $D403 DLISTH
		; $D404 HSCROL
		; $D405 VSCROL
		; $D407 PMBASE
		; $D409 CHBASE
		; $D40E NMIEN
		ldx		#8
anload:
		lda		anticdat,x
		sta		$d401,x
		dex
		bpl		anload
		
		; $D400 DMACTL
		
		;reload PBCTL, PORTB
		lda		#$00
_insert_pbctl = *-1;
		ldx		#$00
_insert_portb1 = *-1;
		sta		pbctl
		stx		portb
		eor		#$04
		sta		pbctl
			
		lda		#244/2
		cmp:rne	vcount		;end 243
		cmp:req	vcount		;end 245
		sta		wsync		;end 246
		sta		wsync		;end 247

		;reload S, X, and Y registers
		ldx		#$ff
_insert_s = * - 1
		txs					;[104, 105]
		ldx		#$00		;[106, 107]
_insert_x = * - 1
		ldy		#$00		;[108, 109]
_insert_y = * - 1
		lda		#$00		;[110, 111] load DMACTL value
_insert_dmactl = * - 1
		sta		dmactl		;[112, 113, 0, 1]
		lda		#$00		;[2, 3]
_insert_nmien = * - 1
		sta		nmien		;[4, 5, 6, 7] reenable NMIs
		lda		#$fe		;[8, 9]
_insert_portb2 = * - 1
		jmp		$0100		;[10, 11, 12] jump to stack thunk
_insert_ipc = * - 2
.endp

;==========================================================================
; extra data
;==========================================================================

gtiadat:
		:30 dta 0			;$D000 to $D01D

pokeydat:
		:9 dta 0			;$D200 to $D208

anticdat:
		:9 dta 0			;$D401 to $D409
		
		.print	"End of loader: ",$8000-*

		org		$0a00
		
;==========================================================================
; metadata
;==========================================================================

		dta		a(_insert_a - $0800)
		dta		a(stage3._insert_x - $7e00)
		dta		a(stage3._insert_y - $7e00)
		dta		a(stage3._insert_s - $7e00)
		dta		a(stage3._insert_ipc - $7e00)
		dta		a(stage3._insert_pbctl - $7e00)
		dta		a(stage3._insert_portb1 - $7e00)
		dta		a(stage3._insert_portb2 - $7e00)
		dta		a(stage3._insert_dmactl - $7e00)
		dta		a(stage3._insert_nmien - $7e00)
		dta		<gtiadat
		dta		stack_thunk_end - stack_thunk_begin

stack_thunk_begin:
		sta		portb		;[13, 14, 15, 16] restore PORTB state
		lda		#$00		;[17, 18] reload accumulator
_insert_a = * - 1
		rti					;[19, 20, 21, 22, 23, 24] jump to VBI routine
stack_thunk_end:

		end
