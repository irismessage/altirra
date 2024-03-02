;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Interrupt Handlers
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
; Dispatched from INTINV. Used by SpartaDOS X.
;
.proc IntInitInterrupts
	mva		#$40 nmien
	
	;Required by XEGS carts to run since they have a clone of the XL/XE
	;OS in them.
	mva		trig3 gintlk
	rts
.endp

;==========================================================================
.proc IntDispatchNMI
	bit		nmist		;check nmi status
	bpl		not_dli		;skip if not a DLI
	jmp		(vdslst)	;jump to display list vector

not_vbi:
.if !_KERNEL_XLXE
	lda		#$20
	bit		nmist
	sta		nmires
	bne		is_system_reset
	rti
is_system_reset:
.endif
exit_a:
	pla
exit_none:
	rti

not_dli:
	pha
	bvc		not_vbi		;skip if not a VBI
	txa
	pha
	tya
	pha
	sta		nmires		;reset VBI interrupt
	jmp		(vvblki)	;jump through vblank immediate vector	
.endp

.proc IntDispatchIRQ
	jmp		(vimirq)
.endp

;==============================================================================

IntExitHandler_A = IntDispatchNMI.exit_a
IntExitHandler_None = IntDispatchNMI.exit_none
