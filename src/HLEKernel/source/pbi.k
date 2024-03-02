;	Altirra - Atari 800/800XL/5200 emulator
;	HLE Kernel ROM - Parallel Bus Interface support
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

;==========================================================================
; Add handler to HATABS.
;
; Input:
;	X		Name of device
;	A:Y		CIO handler table address
;
; Returns:
;	N=1		HATABS is full.
;	C=0		Handler added successfully.
;	C=1		Handler already exists; X points to address entry
;			A:Y preserved (required by SDX 4.43rc)
;
.proc	PBIAddHandler
		pha
		tya
		pha
		txa
		ldx		#33
search_loop:
		cmp		hatabs,x
		beq		found_existing
		dex
		dex
		dex
		bpl		search_loop	
		
insert_loop:
		inx
		inx
		inx
		ldy		hatabs,x
		beq		found_empty
		cpx		#36
		bne		insert_loop
		
		;oops... table is full!
		pla
		pla
		lda		#$ff
		sec
		rts

found_existing:
		pla
		tay
		pla
		inx					;X=address offset, N=0 (not full)
		sec					;C=1 (already exists)
		rts

found_empty:
		sta		hatabs,x
		pla
		sta		hatabs+1,x
		pla
		sta		hatabs+2,x
		asl					;N=0 (not full)
		clc					;C=0 (added successfully)
		rts
.endp

;==========================================================================
