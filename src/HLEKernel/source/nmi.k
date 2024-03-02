;	Altirra - Atari 800/800XL emulator
;	HLE Kernel ROM
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

.proc NmiRoutine
	bit		nmist		;check nmi status
	bpl		NotDLI		;skip if not a DLI
	jmp		(vdslst)	;jump to display list vector
NotVBI:
	lda		#$20
	bit		nmist
	bne		IsBreak
	sta		nmires
	rti
IsBreak:
	pla
	rti

NotDLI:
	pha
	bvc		NotVBI		;skip if not a VBI
	txa
	pha
	tya
	pha
	sta		nmires		;reset VBI interrupt
	jmp		(vvblki)	;jump through vblank immediate vector	
.endp

.proc VBlankImmediate
	#{
		// increment RTCLOK
		bool slowTick = false;
		for(int i=2; i>=0; --i) {
			uint8 c = kdb.RTCLOK[i] + 1;
			kdb.RTCLOK[i] = c;
			if (c)
				break;
			slowTick = true;
		}
		
		// attract colors and copy to GTIA
		uint8 attract = kdb.ATRACT;
		
		if (slowTick)
			++attract;

		if (attract >= 0x80) {
			attract = 0xFE;

			kdb.DRKMSK = 0xF6;
			kdb.COLRSH = kdb.RTCLOK[1];
		} else {
			// Pole Position depends on these being reset as it clears kernel
			// vars after startup.
			kdb.DRKMSK = 0xFE;
			kdb.COLRSH = 0x00;
		}

		kdb.ATRACT = attract;
	#}

	;decrement timer 1 and check for underflow
	lda		cdtmv1				;check low byte
	bne		Timer1DecCheck		;if non-zero, decrement and check for fire
	lda		cdtmv1+1			;check high byte
	beq		Timer1Done			;if clear, timer's not running
	dec		cdtmv1+1			;decrement high byte
	dec		cdtmv1				;decrement low byte
	bne		Timer1Done			;we're done
Timer1DecCheck:
	dec		cdtmv1				;decrement low byte
	bne		Timer1Done
	jsr		Timer1Dispatch
Timer1Done:

	;check for critical operation
	lda		critic			;is the critical flag set?
	bne		VBlankExit		;yes, abort
	lda		#$04			;I flag
	tsx
	and		$0104,x			;I flag set on pushed stack?
	beq		VBlankStage2	;exit if so
xit:
	pla
	tay
	pla
	tax
	pla
	rti

Timer1Dispatch:
	jmp		(cdtma1)
.endp

VBlankExit = VBlankImmediate.xit
	
.proc VBlankStage2
	
	;======== stage 2 processing
	
	#{
		// re-enable interrupts
		mpCPU->SetP(mpCPU->GetP() & ~AT6502::kFlagI);
		
		// Update hardware registers from shadow registers.
		kdb.DLISTH = kdb.SDLSTH;
		kdb.DLISTL = kdb.SDLSTL;
		kdb.DMACTL = kdb.SDMCTL;
		kdb.CHBASE = kdb.CHBAS;
		kdb.CHACTL = kdb.CHACT;
		kdb.PRIOR  = kdb.GPRIOR;
		
		// Update color registers from shadows with attract.
		uint8 drkmsk = mpMemory->ReadByte(DRKMSK);
		uint8 colrsh = mpMemory->ReadByte(COLRSH);

		for(uint32 i=0; i<9; ++i)
			mpMemory->WriteByte(COLPM0 + i, (mpMemory->ReadByte(PCOLR0 + i) ^ colrsh) & drkmsk);

		// Reset console click.
		kdb.CONSOL = 0x08;
	#}
	
	;decrement timer 2 and check for underflow
	lda		cdtmv2				;check low byte
	bne		Timer2DecCheck		;if non-zero, decrement and check for fire
	lda		cdtmv2+1			;check high byte
	beq		Timer2Done			;if clear, timer's not running
	dec		cdtmv2+1			;decrement high byte
	dec		cdtmv2				;decrement low byte
	bne		Timer2Done			;we're done
Timer2DecCheck
	dec		cdtmv2				;decrement low byte
	bne		Timer2Done
	jsr		Timer2Dispatch
Timer2Done

	;decrement timers 3-5 and set flags
	ldx		#4
TimerNLoop:
	lda		cdtmv3,x			;check low byte
	bne		TimerNDecCheck		;if non-zero, decrement and check for fire
	lda		cdtmv3+1,x			;check high byte
	beq		TimerNDone			;if clear, timer's not running
	dec		cdtmv3+1,x			;decrement high byte
	dec		cdtmv3,x			;decrement low byte
	bne		TimerNDone			;we're done
TimerNDecCheck:
	dec		cdtmv3,x			;decrement low byte
	bne		TimerNDone
	mva		#0 cdtmf3,x
TimerNDone:
	dex
	dex
	bpl		TimerNLoop
	
	;TODO: read POKEY keyboard register and handle auto-repeat
	
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
	lda		trig0,x
	sta		strig0,x
	lda		#1
	sta		ptrig4,x
	dex
	bpl		PotReadLoop
	
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
	
	jmp		(vvblkd)	;jump through vblank deferred vector
	
Timer2Dispatch
	jmp		(cdtma2)
.endp

.proc VBlankDeferred	
	jmp		VBlankExit
.endp

.proc SetVBlankVector
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
