; Altirra BASIC - Variables module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

;==========================================================================
; Input:
;	A = index of variable to look up ($00-7F or $80-FF)
;	X = ZP variable to store result in
;	Y = offset to variable pointer (0-7)
;
; Output:
;	varptr = address of variable
VarGetAddr0:
		ldy		#0
VarGetAddr:
		ldx		#varptr
.proc	VarGetAddrX
		;;##TRACE "Looking up variable: $%02x" a|$80
		asl					;!! ignore bit 7 of variable index
		asl
		rol		1,x
		asl
		rol		1,x
		sty		0,x
		clc
		adc		0,x
		adc		vvtp
		sta		0,x
		lda		1,x
		and		#$03
		adc		vvtp+1
		sta		1,x
		;##ASSERT ((dw(x)-dw(vvtp)-y)&7)=0
		;##ASSERT db(dw(x)+1-y)=(dw(x)-dw(vvtp))/8
		;;##TRACE "varptr=$%04x" dw(x)
		rts
.endp

;==========================================================================
.proc VarLoadExtendedFR0
		ldy		#7
varloop:
		lda		(varptr),y
		sta		prefr0,y
		dey
		bpl		varloop
		rts
.endp

;==========================================================================
.proc varStoreArgStk
		jsr		VarGetAddr0
		jsr		expPopFR0
.def :VarStoreFR0 = *
		ldy		#2
loop:
		mva		prefr0,y (varptr),y
		iny
		cpy		#8
		bne		loop
		rts
.endp

;==========================================================================
.proc VarAdvanceName
		ldy		#0
skip_loop:
		lda		(iterPtr),y
		iny
		cmp		#0
		bpl		skip_loop
		tya
.def :VarAdvancePtr = *
		ldx		#iterPtr
.def :VarAdvancePtrX = *
		clc
		adc		0,x
		sta		0,x
		scc:inc	1,x
		rts
.endp