;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Vertical Blank Interrupt Services
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

;==========================================================================
; VBIStage1 - Vertical Blank Stage 1 Processor
;
.proc VBIStage1

	;increment real-time clock and do attract processing
	inc		rtclok+2
	bne		ClockDone
	inc		atract
	inc		rtclok+1
	bne		ClockDone
	inc		rtclok
ClockDone

	;Pole Position depends on DRKMSK and COLRSH being reset from VBI as it
	;clears kernel vars after startup.
	ldx		#$fe				;default to no mask
	ldy		#$00				;default to no color alteration
	lda		atract				;check attract counter
	bpl		attract_off			;skip if attract is off
	mva		#$fe atract			;lock the attract counter
	ldx		#$f6				;set mask to dim colors
	ldy		rtclok+1			;use clock to randomize colors
attract_off:	
	stx		drkmsk				;set color mask
	sty		colrsh				;set color modifier

	;decrement timer 1 and check for underflow
	lda		cdtmv1				;check low byte
	bne		timer1_lobytezero	;if non-zero, decrement and check for fire
	lda		cdtmv1+1			;check high byte
	beq		timer1_done			;if clear, timer's not running
	dec		cdtmv1+1			;decrement high byte
	dec		cdtmv1				;decrement low byte
	bne		timer1_done			;we're done
timer1_lobytezero:
	dec		cdtmv1				;decrement low byte
	bne		timer1_done
	lda		cdtmv1+1			;check if high byte is zero
	bne		timer1_done			;if it's not, we're not done yet ($xx00 > 0)
	jsr		timer1_dispatch		;jump through timer vector
timer1_done:

	;check for critical operation
	lda		critic			;is the critical flag set?
	bne		xit				;yes, abort
	lda		#$04			;I flag
	tsx
	and		$0104,x			;I flag set on pushed stack?
	beq		VBIStage2	;exit if so
xit:
	pla
	tay
	pla
	tax
	pla
	rti

timer1_dispatch:
	jmp		(cdtma1)
.endp

;==========================================================================
; VBIExit - Vertical Blank Interrupt Exit Routine
;
; This is a drop-in replacement for XITVBV.
;
VBIExit = VBIStage1.xit
	
;==========================================================================
; VBIStage2 - Vertical Blank Stage 2 Processor
;
.proc VBIStage2
	
	;======== stage 2 processing
	
	;re-enable interrupts
	cli

	;update shadow registers
	mva		sdlsth	dlisth
	mva		sdlstl	dlistl
	mva		sdmctl	dmactl
	mva		chbas	chbase
	mva		chact	chactl
	mva		gprior	prior
	
	ldx		#8
ColorLoop
	lda		pcolr0,x
	eor		colrsh
	and		drkmsk
	sta		colpm0,x
	dex
	bpl		ColorLoop

	mva		#8		consol
	
	;decrement timer 2 and check for underflow
	ldx		#2
	jsr		VBIDecrementTimer
	sne:jsr	Timer2Dispatch

	;decrement timers 3-5 and set flags
	ldx		#8
timer_n_loop:
	jsr		VBIDecrementTimer
	sne:mva	#0 cdtmf3-4,x
	dex
	dex
	cpx		#4
	bcs		timer_n_loop
	
	;Read POKEY keyboard register and handle auto-repeat
	lda		skstat				;get key status
	and		#$04				;check if key is down
	bne		no_repeat			;skip if not
	dec		srtimr				;decrement repeat timer
	bne		no_repeat			;skip if not time to repeat yet
	mva		#$06 srtimr			;reset repeat timer
	mva		kbcode ch			;repeat last key
no_repeat:

	;decrement keyboard debounce counter
	lda		keydel
	bne		NoDebounce
	dec		keydel
NoDebounce:
	
	;update controller shadows
	ldx		#3
PotReadLoop:
	lda		pot0,x
	sta		paddl0,x
	lda		pot4,x
	sta		paddl4,x
	lda		#1
	sta		ptrig4,x
	dex
	bpl		PotReadLoop

	lda		trig0
	sta		strig0
	sta		strig2
	lda		trig1
	sta		strig1
	sta		strig3
	
	lda		porta
	tax
	and		#$0f
	sta		stick0
	txa
	lsr
	lsr
	tax
	lsr
	lsr
	sta		stick1
	lsr
	lsr
	tay
	and		#$01
	sta		ptrig2
	tya
	lsr
	sta		ptrig3
	txa
	and		#$01
	sta		ptrig0
	txa
	lsr
	and		#$01
	sta		ptrig1

	lda		#$0f
	sta		stick2
	sta		stick3
	
	;restart pots (required for SysInfo)
	sta		potgo
	
	jmp		(vvblkd)	;jump through vblank deferred vector
	
Timer2Dispatch
	jmp		(cdtma2)
.endp

;==========================================================================
; VBIDecrementTimer
;
; Entry:
;	X = timer index (0-4)
;
; Exit:
;	Z = 0 if timer not fired, 1 if fired
;
.proc VBIDecrementTimer
	lda		cdtmv1,x			;check low byte
	bne		lobyte_nonzero		;if non-zero, decrement and check for fire
	lda		cdtmv1+1,x			;check high byte
	bne		running_lobytezero	;if non-zero, decrement both bytes
	lda		#1					;counter=0, so timer isn't running
	rts							;ret Z=0
running_lobytezero:
	dec		cdtmv1+1,x			;decrement high byte
	dec		cdtmv1,x			;decrement low byte ($FF)
	rts							;we're done (return Z=0)
	
lobyte_nonzero:
	dec		cdtmv1,x			;decrement low byte
	bne		done
	lda		cdtmv1+1,x			;return as fired if high byte zero
done:
	rts
.endp

;==========================================================================
; VBISetVector - set vertical blank vector or counter
;
; This is a drop-in replacement for SETVBV.
;
; A = item to update
;	1-5	timer 1-5 counter value
;	6	VVBLKI
;	7	VVBLKD
; X = MSB
; Y = LSB
;
.proc VBISetVector
	;A = item to update
	;	1-5	timer 1-5 counter value
	;	6	VVBLKI
	;	7	VVBLKD
	;X = MSB
	;Y = LSB
	;
	;NOTE:
	;The Atari OS Manual says that DLIs will be disabled after SETVBV is called.
	;This is a lie -- neither the OS-B nor XL kernels do this, and the Bewesoft
	;8-players demo depends on it being left enabled.
	
	asl
	sta		intemp
	sei
	tya
	ldy		intemp
	
	;We're relying on a rather tight window here. We can't touch NMIEN, so we have
	;to wing it with DLIs enabled. Problem is, in certain conditions we can be under
	;very tight timing constraints. In order to do this safely we have to finish
	;before a DLI can execute. The worst case is a wide mode 2 line at the end of
	;a vertically scrolled region with P/M graphics enabled and an LMS on the next
	;mode line. In that case we only have 7 cycles before we hit the P/M graphics
	;and another two cycles after that until the DLI fires. The exact cycle timing
	;looks like this:
	;
	;*		sta wsync
	;*		sta abs,y (1/5)
	;ANTIC halts CPU until cycle 105
	;105	playfield DMA
	;106	refresh DMA
	;107	sta abs,y (2/5)
	;108	sta abs,y (3/5)
	;109	sta abs,y (4/5)
	;110	sta abs,y (5/5)
	;111	txa (1/2)
	;112	txa (2/2)
	;113	sta abs,y (1/5)
	;0		missiles
	;1		display list
	;2		player 0
	;3		player 1
	;4		player 2
	;5		player 3
	;6		display list address low
	;7		display list address high
	;8		sta abs,y (2/5)
	;9		sta abs,y (3/5)
	;10		sta abs,y (4/5)
	;11		sta abs,y (5/5)
	;
	;We rely on the 6502 not being able to service interrupts until the end of an
	;instruction for this to work.
	
	sta		wsync
	sta		cdtmv1-2,y
	txa
	sta		cdtmv1-1,y
	cli
	rts
.endp
