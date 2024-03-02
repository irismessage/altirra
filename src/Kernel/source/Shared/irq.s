;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - IRQ routines
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

.proc IRQHandler
	pha
	
	;check for serial output ready IRQ
	lda		#$10
	bit		irqst
	bne		NotSerOutReady
	lda		pokmsk
	and		#$ef
	sta		irqen
	ora		#$10
	sta		irqen
	jmp		(vseror)
NotSerOutReady:

	;check for serial input ready IRQ
	lda		#$20
	bit		irqst
	bne		NotSerInReady
	lda		pokmsk
	and		#$df
	sta		irqen
	ora		#$20
	sta		irqen
	jmp		(vserin)
NotSerInReady:

	txa
	pha

	;check for remaining pokey irqs
	ldx		#4
PokeyIntLoop:
	lda		irqtab,x
	bit		irqst
	bne		NotInt

	eor		#$ff
	and		pokmsk
	sta		irqen
	lda		pokmsk
	sta		irqen

	lda		vectab,x
	tax
	mva		$0200,x jveck
	mva		$0201,x jveck+1
	pla
	tax
	jmp		(jveck)

NotInt:
	dex
	bpl		PokeyIntLoop

	;check for serial output complete (not a latch, so must mask)
	lda		#$08
	and		pokmsk
	beq		NotSerOutComplete
	bit		irqst
	bne		NotSerOutComplete
	
	pla
	tax
	jmp		(vseroc)
	
NotSerOutComplete:
	;check for serial bus proceed line
	bit		pactl
	bpl		NotSerBusProceed

	;clear serial bus proceed interrupt
	lda		porta

	;jump to vector
	pla
	tax
	jmp		(vprced)
	
NotSerBusProceed:

	;check for serial bus interrupt line
	bit		pbctl
	bpl		NotSerBusInterrupt
	
	;clear serial bus interrupt interrupt
	lda		portb
	
	;jmp to vector
	pla
	tax
	jmp		(vinter)
	
NotSerBusInterrupt:

	;check for break instruction
	tsx
	lda		$0103,x
	and		#$20
	beq		NotBrkInstruction

	pla
	tax
	jmp		(vbreak)

NotBrkInstruction:
	pla
	tax
	pla
	rti

irqtab:
	dta		$80		;break key
	dta		$40		;keyboard key
	dta		$04		;pokey timer 4
	dta		$02		;pokey timer 2
	dta		$01		;pokey timer 1
	dta		$08		;serial out complete
	
vectab:
	dta		<brkky
	dta		<vkeybd
	dta		<vtimr4
	dta		<vtimr2
	dta		<vtimr1
		
.endp
