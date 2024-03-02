;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Parallel Bus Interface routines
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

.if _KERNEL_PBI_SUPPORT

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
; Scan for PBI devices.
;
; The details:
;	- $D1FF is the PBI device select register; 1 selects a device.
;	- A selected device, if it exists, maps its ROM into $D800-DFFF, on
;	  top of the math pack.
;	- ID bytes are checked to ensure that a device ROM is actually present.
;	  (This means that the math pack must not match those bytes!)
;	- SHPDVS is the device selection shadow variable. It must match the
;	  value written to $D1FF whenever the PBI ROM is invoked so that the
;	  PBI device code can detect its ID.
;	- PDVMSK is the device enable mask and is used by SIO to call into
;	  PBI devices. Each bit in this mask indicates that a PBI device is
;	  available to possibly service requests. It is NOT set by this
;	  routine, but by the PBI device init code.
;
.proc PBIScan
		mva		#$01 shpdvs
loop:
		;select next device
		mva		shpdvs $d1ff
		
		;check ID bytes
		ldy		$d803
		cpy		#$80
		bne		invalid

		ldy		$d80b
		cpy		#$91
		bne		invalid
				
		;init PBI device
		jsr		$d819		
invalid:

		;next device
		asl		shpdvs
		bne		loop
		
		;deselect last device
		mva		#0 $d1ff
		rts
.endp

;==========================================================================
.proc	PBIAttemptSIO
		lda		pdvmsk
		bne		begin_scan
fail:
		clc
		rts

begin_scan:
		lda		#0
		sec
loop:
		;advance to next device bit
		rol
		
		;check if we've scanned all 8 IDs
		bcs		fail
		
		;check if device exists
		bit		pdvmsk
		
		;keep scanning if not
		beq		loop
		
		;select the device
		sta		$d1ff
		
		;attempt I/O
		pha
		jsr		$d805
		pla
		
		;loop back if PBI handler didn't claim the I/O
		bcc		loop
		
		;all done
		rts
.endp

.endif
