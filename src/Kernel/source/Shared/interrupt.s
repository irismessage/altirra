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
	
.if _KERNEL_XLXE
	;Required by XEGS carts to run since they have a clone of the XL/XE
	;OS in them.
	mva		trig3 gintlk
.endif

	rts
.endp

;==========================================================================
.proc IntDispatchNMI
	bit		nmist		;check nmi status
	bpl		not_dli		;skip if not a DLI
	jmp		(vdslst)	;jump to display list vector

.if !_KERNEL_XLXE
is_system_reset:
	jmp		warmsv
.endif

not_dli:
	pha
	
.if _KERNEL_XLXE
	;Only XL/XE OSes cleared the decimal bit.
	cld
.else
	;The stock OS treats 'not RNMI' as VBI. We'd best follow its example.
	lda		#$20
	bit		nmist
	bne		is_system_reset
.endif

	txa
	pha
	tya
	pha
	sta		nmires		;reset VBI interrupt
	jmp		(vvblki)	;jump through vblank immediate vector	
.endp

.proc IntDispatchIRQ
.if _KERNEL_XLXE
	cld
.endif
	jmp		(vimirq)
.endp

;==============================================================================
IntExitHandler_A = VBIProcess.exit_a
IntExitHandler_None = VBIProcess.exit_none
