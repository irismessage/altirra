; Altirra BASIC - Misc math module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

;===========================================================================
;FCOMP		Floating point compare routine.
;
; Inputs:
;	FR0
;	FR1
;
; Outputs:
;	Z, C set for comparison result like SBC
;
.proc fcomp
		;check for sign difference
		lda		fr1
		tax
		eor		fr0
		bpl		signs_same

		;signs are different
		cpx		fr0
xit:
		rts
		
signs_same:
		;check for both values being zero, as only signexp and first
		;mantissa byte are guaranteed to be $00 in that case
		txa
		ora		fr0
		beq		xit
		
		;compare signexp and mantissa bytes in order
		ldx		#<-6
loop:
		lda		fr0+6,x
		cmp		fr1+6,x
		bne		diff
		inx
		bne		loop
		rts
		
diff:
		;okay, we've confirmed that the numbers are different, but the
		;carry flag may be going the wrong way if the numbers are
		;negative... so let's fix that.
		ror
		eor		fr0
		sec
		rol
		rts
.endp

;===========================================================================
.proc	fld1
		ldx		#<const_one
		ldy		#>const_one
		jmp		fld0r
.endp

;===========================================================================
.proc	MathFloor
		;if exponent is > $44 then there can be no decimals
		lda		fr0
		and		#$7f
		cmp		#$45
		bcs		done
		
		;if exponent is < $40 then we have zero or -1
		cmp		#$40
		bcs		not_tiny
		lda		fr0
		bmi		neg_tiny
		
		;positive... load 0
		jmp		zfr0
		
neg_tiny:
		;negative... load -1
		ldx		#<const_negone
		ldy		#>const_negone
		jsr		fld0r
done:
		rts
		
not_tiny:
		;ok... using the exponent, compute the first digit offset we should
		;check
		adc		#$bb		;note: carry is set coming in
		tax
		
		;check digit pairs until we find a non-zero fractional digit pair,
		;zeroing as we go
		lda		#0
		tay
zero_loop:
		ora		fr0+6,x
		sty		fr0+6,x
		inx
		bne		zero_loop
		
		;skip rounding if it was already integral
		tay
		beq		done
		
neg_round:
		;check if we have a negative number; if so, we need to add one
		lda		fr0
		bpl		done
		
		;subtract one to round down
		jsr		MathLoadOneFR1
		jmp		fsub
		
.endp

;===========================================================================
.proc MathByteToInt
		sta		fr0
		lda		#0
		sta		fr0+1
		jmp		ifp
.endp

;===========================================================================
.proc MathLoadOneFR1
		ldx		#<const_one
		ldy		#>const_one
		jmp		fld1r
.endp

;===========================================================================
.proc MathStoreFR0_FPSCR
		ldx		#<fpscr
		ldy		#>fpscr
		jmp		fst0r
.endp

;===========================================================================
.proc MathLoadFR1_FPSCR
		ldx		#<fpscr
		ldy		#>fpscr
		jmp		fld1r
.endp

;===========================================================================

const_one:
		.fl		1.0
const_negone:
		.fl		-1.0
const_half:
		.fl		0.5
fpconst_pi2:
		.fl		1.5707963267949
fp_180_div_pi:
		.fl		57.295779513082
