;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Screen Handler extension routines
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
ScreenEncodingOffsetTable:
	dta		$00			;4-bit
	dta		$10			;2-bit
	dta		$14			;1-bit

ScreenEncodingTable:
	dta		$00,$11,$22,$33,$44,$55,$66,$77,$88,$99,$aa,$bb,$cc,$dd,$ee,$ff
	dta		$00,$55,$aa,$ff
	dta		$00,$ff

;==========================================================================
; ScreenFineScrollDLI
;
; This DLI routine is used to set the PF1 color to PF2 to kill junk that
; would appear on the extra line added with vertical scrolling.
;
.if _KERNEL_XLXE
.proc ScreenFineScrollDLI
	pha
	lda		color2
	eor		colrsh
	and		drkmsk
	sta		colpf1
	pla
	rti
.endp
.endif

;==========================================================================
; ScreenResetLogicalLineMap
;
; Marks all lines as the start of logical lines.
;
; Exit:
;	X = 0
;
.proc ScreenResetLogicalLineMap
	ldx		#$ff
	stx		logmap
	stx		logmap+1
	stx		logmap+2
	
	;reset line read position
	inx
	stx		bufstr
	lda		lmargn
	sta		bufstr+1
	
	;note - X=0 relied on here by EditorOpen
	rts
.endp

;==========================================================================
; ScreenSetLastPosition
;
; Copies COLCRS/ROWCRS to OLDCOL/OLDROW.
;
.proc ScreenSetLastPosition
	ldx		#2
loop:
	lda		rowcrs,x
	sta		oldrow,x
	dex
	bpl		loop
	rts
.endp

;==========================================================================
; ScreenAdvancePosMode0
;
; Advance to the next cursor position in reading order, for mode 0.
;
; Exit:
;	C = 1 if no wrap, 0 if wrapped
;
; Modified:
;	X
;
; Preserved:
;	A
;
.proc ScreenAdvancePosMode0
	inc		colcrs
	ldx		rmargn
	cpx		colcrs
	bcs		post_wrap
	ldx		lmargn
	stx		colcrs
	inc		rowcrs
post_wrap:
	rts
.endp

;==========================================================================
; Convert an ATASCII character to displayable INTERNAL format.
;
; Entry:
;	A = ATASCII char
;
; Exit:
;	A = INTERNAL char
;
.proc	ScreenConvertATASCIIToInternal
	jsr		ScreenConvertSetup
	eor		ATASCIIToInternalTab,x
	rts
.endp

;==========================================================================
; Close screen (S:).
;
; This is a no-op in OS-B mode. In XL/XE mode, it reopens the device in
; Gr.0 if fine scrolling is on, since this is necessary to clear the DLI.
; This happens even if S: doesn't correspond to the text window. Only
; the high bit of FINE is checked.
;
.if !_KERNEL_XLXE
ScreenClose = CIOExitSuccess
.else
.proc ScreenClose
	bit		fine
	bpl		no_fine_scrolling
	
	;turn off DLI
	mva		#$40 nmien
	
	;restore vdslst
	ldx		#<IntExitHandler_None
	lda		#>IntExitHandler_None
	jsr		ScreenOpenGr0.write_vdslst
	
	jmp		ScreenOpenGr0
no_fine_scrolling:
	ldy		#1
	rts
.endp
.endif

;==========================================================================
.if !_KERNEL_XLXE
	_SCREEN_TABLES_2
.endif
