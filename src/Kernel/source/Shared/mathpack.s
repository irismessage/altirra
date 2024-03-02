;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Decimal Floating-Point Math Pack
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

.macro	ckaddr
.if * < %%1
		.print (%%1-*),' bytes free before ',%%1
		org		%%1
.elif * > %%1
		.error 'Out of space: ',*,' > ',%%1,' (',*-%%1,' bytes over)'
		.endif
.endm

;==========================================================================
; AFP [D800]	Convert ASCII string at INBUFF[CIX] to FR0
;
	org		$d800
_afp = afp
.proc afp
dotflag = fr2
xinvert = fr2+1
cix0 = fr2+2
sign = fr2+3
digit2 = fr2+4

	;skip initial spaces
	jsr		skpspc

	;init FR0 and one extra mantissa byte
	lda		#$7f
	sta		fr0
	sta		digit2
	
	ldx		#fr0+1
	jsr		zf1

	;clear decimal flag
	sta		dotflag
	sta		sign
	
	;check for sign
	ldy		cix
	lda		(inbuff),y
	cmp		#'+'
	beq		isplus
	cmp		#'-'
	bne		postsign
	ror		sign
isplus:
	iny
postsign:	
	sty		cix0

	;skip leading zeroes
	lda		#'0'
	jsr		fp_skipchar
	
	;check if next char is a dot, indicating mantissa <1
	lda		(inbuff),y
	cmp		#'.'
	bne		not_tiny
	iny
	
	;set dot flag
	ror		dotflag
	
	;skip zeroes and adjust exponent
	lda		#'0'
tiny_denorm_loop:
	cmp		(inbuff),y
	bne		tiny_denorm_loop_exit
	dec		fr0
	iny
	bne		tiny_denorm_loop
tiny_denorm_loop_exit:
	
not_tiny:

	;grab digits left of decimal point
	ldx		#1
nextdigit:
	lda		(inbuff),y
	cmp		#'E'
	beq		isexp
	iny
	cmp		#'.'
	beq		isdot
	eor		#'0'
	cmp		#10
	bcs		termcheck
	
	;write digit if we haven't exceeded digit count
	cpx		#6
	bcs		afterwrite
	
	bit		digit2
	bpl		writehi

	;clear second digit flag
	dec		digit2
	
	;merge in low digit
	ora		fr0,x
	sta		fr0,x
	
	;advance to next byte
	inx
	bne		afterwrite
	
writehi:
	;set second digit flag
	inc		digit2
	
	;shift digit to high nibble and write
	asl
	asl
	asl
	asl
	sta		fr0,x

afterwrite:
	;adjust digit exponent if we haven't seen a dot yet
	bit		dotflag
	smi:inc	fr0
	
	;go back for more
	jmp		nextdigit
	
isdot:
	lda		dotflag
	bne		termcheck
	
	;set the dot flag and loop back for more
	ror		dotflag
	bne		nextdigit

termcheck:
	dey
	cpy		cix0
	beq		err_carryset
term:
	;stash offset
	sty		cix

term_rollback_exp:
	;divide digit exponent by two and merge in sign
	rol		sign
	ror		fr0
	
	;check if we need a one digit shift
	bcs		nodigitshift

	;shift right one digit
	jsr		fp_shr4

nodigitshift:
	jmp		fp_normalize

err_carryset:
	rts

isexp:
	cpy		cix0
	beq		err_carryset
	
	;save off this point as a fallback in case we don't actually have
	;exponential notation
	sty		cix

	;check for sign
	ldx		#0
	iny
	lda		(inbuff),y
	cmp		#'+'
	beq		isexpplus
	cmp		#'-'
	bne		postexpsign
	ldx		#$ff
isexpplus:
	iny
postexpsign:
	stx		xinvert

	;pull up to two exponent digits
	lda		(inbuff),y
	iny
	eor		#'0'
	
	;better be a digit
	cmp		#10
	bcs		term_rollback_exp
	
	;stash first digit
	tax
	
	;check for another digit
	lda		(inbuff),y
	eor		#'0'
	cmp		#10
	bcs		notexpzero2
	iny

	adc		fp_mul10,x
	tax
notexpzero2:
	txa
	
	;zero is not a valid exponent
	beq		term_rollback_exp
	
	;check if mantissa is zero -- if so, don't bias
;	ldx		fr0+1
;	beq		term
	
	;apply sign to exponent
	eor		xinvert
	rol		xinvert

	;bias digit exponent
	adc		fr0
	sta		fr0
expterm:
	jmp		term

.endp

fp_shr4:
	ldx		#4
digitshift:
	lsr		fr0+1
	ror		fr0+2
	ror		fr0+3
	ror		fr0+4
	ror		fr0+5
	dex
	bne		digitshift
	rts

fp_mul10:
	dta		0,10,20,30,40,50,60,70,80,90
	
;==========================================================================
		ckaddr $d8e6
_fasc = fasc
.proc fasc
dotcntr = fr2
expflg = fr2+1
absexp = fr2+2
expval = fr2+3
	jsr		ldbufa
	ldy		#0
	sty		expval

	;check if number is zero
	lda		fr0
	bne		notzero
	
	lda		#$b0
	sta		(inbuff),y
	rts
	
notzero:
	;check if number is negative
	bpl		ispos
	and		#$7f
	tax
	lda		#'-'
	sta		(inbuff),y
	iny
	txa
ispos:

	;compute digit offset to place dot
	;  0.001 (10.0E-04) = 3E 10 00 00 00 00 -> -1
	;   0.01 ( 1.0E-02) = 3F 01 00 00 00 00 -> 1
	;    0.1 (10.0E-02) = 3F 10 00 00 00 00 -> 1
	;    1.0 ( 1.0E+00) = 40 01 00 00 00 00 -> 3
	;   10.0 (10.0E+00) = 40 10 00 00 00 00 -> 3
	;  100.0 ( 1.0E+02) = 40 01 00 00 00 00 -> 5
	; 1000.0 (10.0E+02) = 40 10 00 00 00 00 -> 5
	asl
	sbc		#124		;!! carry is clear

	;check if we should go to exponential form (exp >= 10 or <=-3)
	bmi		exp
	cmp		#12
	bcc		noexp

exp:
	;compute and stash explicit exponent
	sec
	sbc		#2
	sta		expval
	
	;reset dot counter
	lda		#2

noexp:
	sta		dotcntr
	
	;set up for 5 mantissa bytes
	ldx		#-5
	
	;check if number is less than 1.0 and init dot counter
	cmp		#2
	bcs		not_tiny
	
	;insert a sixth mantissa byte
	mva		#0 fr0
	inc		dotcntr
	inc		dotcntr
	dex
not_tiny:
	
	;check if number begins with a leading zero, and if so, skip high digit
	lda		fr0+6,x
	cmp		#$10
	bcs		digitloop
	lda		#$fe
	and		expval
	sta		expval
	bne		writelow
	dec		dotcntr
	bcc		writelow

	;write out mantissa digits
digitloop:
	dec		dotcntr
	bne		no_hidot
	lda		#'.'
	sta		(inbuff),y
	iny
no_hidot:

	;write out high digit
	lda		fr0+6,x
	lsr
	lsr
	lsr
	lsr
	ora		#$30
	sta		(inbuff),y
	iny
	
writelow:
	;write out low digit
	dec		dotcntr
	bne		no_lodot
	lda		#'.'
	sta		(inbuff),y
	iny
no_lodot:
	
	lda		fr0+6,x
	and		#$0f
	ora		#$30
	sta		(inbuff),y
	iny

	;next digit
	inx
	bne		digitloop

	;skip trim if dot hasn't been written
	lda		dotcntr
	bpl		skip_zero_trim	
	
	;trim off leading zeroes
	lda		#'0'
lzloop:
	dey
	cmp		(inbuff),y
	beq		lzloop

	;trim off dot
	lda		(inbuff),y
	cmp		#'.'
	bne		no_trailing_dot

skip_zero_trim:
	dey
	lda		(inbuff),y
no_trailing_dot:

	;check if we have an exponent to deal with
	ldx		expval
	beq		noexp2
	
	;print an 'E'
	lda		#'E'
	iny
	sta		(inbuff),y
	
	;check for a negative exponent
	txa
	bpl		exppos
	eor		#$ff
	tax
	inx
	lda		#'-'
	bne		expneg
exppos:
	lda		#'+'
expneg:
	iny
	sta		(inbuff),y
	
	;print tens digit, if any
	txa
	sec
	ldx		#$2f
tensloop:
	inx
	sbc		#10
	bcs		tensloop
	pha
	txa
	iny
	sta		(inbuff),y
	pla
	adc		#$3a
	iny
noexp2:
	;set high bit on last char
	ora		#$80
	sta		(inbuff),y
	rts
.endp

;==========================================================================
; IPF [D9AA]	Convert 16-bit integer at FR0 to FP
;
; !NOTE! Cannot use FR2/FR3 -- MAC/65 requires that $DE-DF be preserved.
;
	ckaddr	$d9aa
.proc ipf
	sed

	ldx		#fr0+2
	ldy		#4
	jsr		zfl
	
	ldy		#16
	sty		fr0+6
	
	ldx		#fr0+2
	ldy		#fr0+2
byteloop:
	asl		fr0
	rol		fr0+1
	
	jsr		fp_fastadc3
	
	dec		fr0+6
	bne		byteloop
	
	lda		#$43
	sta		fr0
	
	cld	
	jmp		fp_normalize
	
.print	'IFP Current address: ',*,' -> $D9D2'
.endp

;==========================================================================
; FPI [D9D2]	Convert FR0 to 16-bit integer at FR0 with rounding
;
; This cannot overwrite FR1. Darg relies on being able to stash a value
; there across a call to FPI in its startup.
;
	org		$d9d2
.nowarn .proc fpi
_acc0 = fr2
_acc1 = fr2+2
	
	;error out if it's guaranteed to be too big or negative (>999999)
	ldx		fr0
	cpx		#$43
	bcs		err
	
	;clear temp accum
	lda		#0
	sta		_acc0+1
	sta		_acc0
	
	;zero number if it's guaranteed to be too small (<0.01)
	cpx		#$3f
	bcc		too_small

	ldx		#$3f
	bne		shloop_start
shloop:
	;multiply by 10 twice
	ldy		#2
mul10_loop:
	lda		_acc0+1
	sta		_acc1+1
	lda		_acc0
	sta		_acc1
	
	;x2
	asl
	rol		_acc0+1
	bcs		err

	;x2 -> x4
	asl
	rol		_acc0+1
	bcs		err
	
	;+1 -> x5
	adc		_acc1
	sta		_acc0
	lda		_acc0+1
	adc		_acc1+1
	bcs		err
	
	;x2 -> x10
	asl		_acc0
	rol
	sta		_acc0+1
	bcs		err

	dey
	bne		mul10_loop
	
	;convert BCD digit pair to binary
	lda		fr0-$3e,x
	lsr
	lsr
	lsr
	lsr
	tay
	lda		fr0-$3e,x
	and		#$0f
	clc
	adc		fp_mul10,y
	adc		_acc0
	sta		_acc0
	bcc		add2
	inc		_acc0+1
	beq		err
add2:
	
	;loop until we've done all digits
	inx
shloop_start:
	cpx		fr0
	bne		shloop
	
	;move result back to FR0, with rounding
	ldy		fr0-$3e,x
	cpy		#$50
	adc		#0
	sta		fr0
	lda		_acc0+1
	adc		#0
	sta		fr0+1
err:
	rts

too_small:
	jmp		zfr0
	
drconst:
	dta		$42, $90, $00, $00, $50, $00
.print	'FPI Current address: ',*,' -> $DA44 (', $DA44-*, ' bytes left)'
.endp

;==========================================================================
; ZFR0 [DA44]	Zero FR0
; ZF1 [DA46]	Zero float at (X)
; ZFL [DA48]	Zero float at (X) with length Y (UNDOCUMENTED)
;
	ckaddr	$da44
zfr0:
	ldx		#fr0
	ckaddr	$da46
zf1:
	ldy		#6
	ckaddr	$da48
zfl:
	lda		#0
zflloop:
	sta		0,x
	inx
	dey
	bne		zflloop
	rts

;==========================================================================
; LDBUFA [DA51]	Set LBUFF to #INBUFF (UNDOCUMENTED)
;
		ckaddr	$da51
ldbufa:
	mwa		#lbuff inbuff
	rts

;==========================================================================
; FPILL_SHL16 [DA5A] Shift left 16-bit word at $F7:F8 (UNDOCUMENTED)
;
; Illegal entry point used by MAC/65 when doing hex conversion.
;
; Yes, even the byte ordering is wrong.
;
		ckaddr	$da5a
	
.nowarn .proc fpill_shl16
		asl		$f8
		rol		$f7
		rts
.endp

;** 1 byte free**

;==========================================================================
; FSUB [DA60]	Subtract FR1 from FR0; FR1 is altered
; FADD [DA66]	Add FR1 to FR0; FR1 is altered
		ckaddr	$da60
fadd = fsub._fadd
.proc fsub

_diffmode = fr1

	;toggle sign on FR1
	lda		fr1
	eor		#$80
	sta		fr1
	
	;fall through to FADD
	
	ckaddr	$da66
_fadd:
	;if fr1 is zero, we're done
	lda		fr1
	beq		xit
	
	;if fr0 is zero, swap
	lda		fr0
	beq		swap

	;clear additional fr0 bytes
	ldx		#fr0+6
	ldy		#6
	jsr		zfl	

	;compute difference in exponents, ignoring sign
	lda		fr0			;load fr0 sign
	eor		fr1			;compute fr0 ^ fr1 signs
	and		#$80		;mask to just sign
	tay
	eor		fr0			;flip fr0 sign to match fr1
	sec
	sbc		fr1			;compute difference in exponents
	bcs		noswap
	
	;swap FR0 and FR1
swap:
	jsr		fp_swap
	
	;loop back and retry
	bmi		_fadd
	
noswap:	
	;stash add/sub flag
	sty		_diffmode

	;check if FR1 is too small in magnitude to matter
	cmp		#6
	bcs		xit

	;compute positions for add/subtract	
	ldy		#fr1+1
	adc		#fr0+1
	tax
	
	;jump to decimal mode and prepare for add/sub loops
	sed

	;check if we are doing a sum or a difference
	rol		_diffmode
	bcs		do_subtract
	
	;set up rounding
	lda		#$50
	sta		fr0+6
		
	;add FR0 and FR1 mantissas
	jsr		fp_fastadc5
		
	;check if we had a carry out
	bcc		xit
	
	;carry it up
	bcs		sum_carryloop_start
sum_carryloop:
	lda		0,x
	adc		#0
	sta		0,x
	bcc		xit
sum_carryloop_start:
	dex
	cpx		#fr0
	bne		sum_carryloop

	;adjust exponent
	inc		fr0

	;shift down FR0
	ldx		#4
sum_shiftloop:
	lda		fr0,x
	sta		fr0+1,x
	dex
	bne		sum_shiftloop
	
	;add a $01 at the top
	inx
	stx		fr0+1
	
	;all done
	bne		xit

do_subtract:
	;subtract FR0 and FR1 mantissas
	jsr		fp_fastsbc5
	
	;propagate borrow up as needed
	bcs		xit
	bcc		borrow_loop_start
borrow_loop:
	lda		0,x
	sbc		#1
	sta		0,x
	bcs		xit
borrow_loop_start:
	dex
	cpx		#fr0
	bne		borrow_loop

no_borrow1:
	jsr		fp_borrow
xit:
	;exit decimal mode
	cld

	;normalize if necessary and exit (needed for borrow, as well to check over/underflow)
	jmp		fp_normalize

.print	'FADD/FSUB Current address: ',*,' -> $DADB (',$dadb-*,' bytes free)'

.endp

;==========================================================================
; FMUL [DADB]:	Multiply FR0 * FR1 -> FR0
;
	org		$dadb
.proc fmul

	;We use FR0:FR3 as a double-precision accumulator, and copy the
	;original multiplicand value in FR0 to FR1. The multiplier in
	;FR1 is converted to binary digit pairs into FR2.
	
_offset = _fr3+5
_offset2 = fr2

	;if FR0 is zero, we're done
	lda		fr0
	beq		xit
	
	;if FR1 is zero, zero FR0 and exit
	lda		fr1
	bne		nonzero
	clc
	jmp		zfr0

nonzero:
	
	;move fr0 to fr2
	ldx		#<fr2
	ldy		#>fr2
	jsr		fst0r
	
	;compute new exponent and stash
	lda		fr1
	clc
	jsr		fp_adjust_exponent.fmul_entry
					
	jsr		fp_fmul_innerloop

	jmp		fp_normalize
xit:
	clc
	rts	
		
.print	'FMUL Current address: ',*,' -> $DB28 (', $db28-* ,' bytes free)'
.endp

.proc fp_adjust_exponent
fdiv_entry:
	lda		fr1
	eor		#$7f
	sec
fmul_entry:
	;stash modified exp1
	tax
	
	;compute new sign
	eor		fr0
	and		#$80
	sta		fr1
	
	;merge exponents
	txa
	adc		fr0
	tax
	eor		fr1
	
	;check for underflow/overflow
	cmp		#128-49
	bcc		underflow_overflow
	
	cmp		#128+49
	bcs		underflow_overflow
	
	;rebias exponent
	txa
	sec
	sbc		#$40
	rts
	
underflow_overflow:
	pla
	pla
	jmp		zfr0
.endp

;==========================================================================
; FDIV [DB28]	Divide FR0 / FR1 -> FR0
;
; 
;
		ckaddr		$db28
.proc fdiv
_digit = _fr3+1
_index = _fr3+2
	;check if dividend is zero
	lda		fr0
	beq		ok
	
	;check if divisor is zero
	lda		fr1
	beq		err
	
	ldx		#fr2
	jsr		zf1
	
	;compute new exponent
	jsr		fp_adjust_exponent.fdiv_entry
	sta		_fr3
	
	jsr		fp_fdiv_init	
digitloop:
	;just keep going if we're accurate
	lda		fr0
	ora		fr0+1
	beq		nextdigit
	
	;check if we should either divide or add based on current sign (stored in carry)
	bcc		incloop

decloop:
	;increment quotient mantissa byte
	clc
	ldx		_index
	lda		_digit
uploop:
	adc		fr2+6,x
	sta		fr2+6,x
	lda		#0
	dex
	bcs		uploop

	;subtract mantissas
	ldx		#fr0
	ldy		#fr1
	jsr		fp_fastsub6
	
	;keep going until we underflow
	bcs		decloop
	bcc		nextdigit
	
incloop:
	;decrement quotient mantissa byte
	sec
	ldx		_index
	lda		#0
	sbc		_digit
downloop:
	adc		fr2+6,x
	sta		fr2+6,x
	lda		#$99
	dex
	bcc		downloop
	
	;add mantissas
	ldx		#fr0
	ldy		#fr1
	jsr		fp_fastadd6
	
	;keep going until we overflow
	bcc		incloop	
	
nextdigit:
	;shift dividend (make sure to save carry state)
	php
	jsr		fp_fr0_shl4
	plp
	
	;next digit
	lda		_digit
	eor		#$11
	sta		_digit
	and		#$10
	beq		digitloop
	
	;next quo byte
	inc		_index
	bne		digitloop
	
	;move back to fr0
	ldx		#fr2-1
	ldy		_fr3
	lda		fr2
	bne		no_normstep
	inx
	dey
no_normstep:
	sty		0,x
	jsr		fld0r_zp

	cld
ok:
	clc
	rts
err:
	sec
	rts
	
.print	'FDIV Current address: ',*,' -> $DBA1 (', $dba1-* ,' bytes free)'
.endp

;==========================================================================
; SKPSPC [DBA1]	Increment CIX while INBUFF[CIX] is a space
		ckaddr	$dba1
skpspc:
	lda		#' '
	ldy		cix
fp_skipchar:
skpspc_loop:
	cmp		(inbuff),y
	bne		skpspc_xit
	iny
	bne		skpspc_loop
skpspc_xit:
	sty		cix
	rts

;==========================================================================
; ISDIGT [DBAF]	Check if INBUFF[CIX] is a digit (UNDOCUMENTED)
		ckaddr	$dbaf
isdigt = _isdigt
.proc _isdigt
	ldy		cix
	lda		(inbuff),y
	sec
	sbc		#'0'
	cmp		#10
	rts
.endp

;==========================================================================
.proc fp_swap
	ldx		#5
swaploop:
	lda		fr0,x
	ldy		fr1,x
	sta		fr1,x
	sty		fr0,x
	dex
	bpl		swaploop
	rts
.endp

;==========================================================================
; NORMALIZE [DC00]	Normalize FR0 (UNDOCUMENTED)
		ckaddr	$dc00
fp_normalize = normalize
normalize .proc
	ldy		#5
normloop:
	lda		fr0
	and		#$7f
	beq		underflow2
	
	ldx		fr0+1
	beq		need_norm

	;Okay, we're done normalizing... check if the exponent is in bounds.
	;It needs to be within +/-48 to be valid. If the exponent is <-49,
	;we set it to zero; otherwise, we mark overflow.
	
	cmp		#64-49
	bcc		underflow
	cmp		#64+49
	rts
	
need_norm:
	dec		fr0
	ldx		#-5
normloop2:
	mva		fr0+7,x fr0+6,x
	inx
	bne		normloop2
	stx		fr0+6
	dey
	bne		normloop
	
	;Hmm, we shifted out everything... must be zero; reset exponent. This
	;is critical since Atari Basic depends on the exponent being zero for
	;a zero result.
	sty		fr0
	sty		fr0+1
xit:
	clc
	rts
	
underflow2:
	clc
underflow:
	jmp		zfr0
	
.endp

;==========================================================================
; HELPER ROUTINES
;==========================================================================

.proc fp_fr0_shl4
	ldx		#4
bitloop:
	asl		fr0+5
	rol		fr0+4
	rol		fr0+3
	rol		fr0+2
	rol		fr0+1
	rol		fr0
	dex
	bne		bitloop
	rts
.endp

.proc fp_fdiv_init		
	ldx		#0
	stx		fr0
	stx		fr1
	
	jsr		fp_fdiv_tenscheck
	stx		fdiv._digit
	sed
	sec
	lda		#0-6
	sta		fdiv._index
	rts
.endp

.proc fp_fdiv_tenscheck
	inx	
	
	;check if dividend begins with a leading zero digit -- if so, shift it left 4
	;and begin with the tens digit
	lda		fr1+1
	cmp		#$10
	bcs		start_with_ones

	ldy		#4
bitloop:
	asl		fr1+5
	rol		fr1+4
	rol		fr1+3
	rol		fr1+2
	rol		fr1+1
	dey
	bne		bitloop

	ldx		#$10
	
start_with_ones:
	rts
.endp

.proc fp_borrow
	ldx		#5
	sec
diff_borrow:
	lda		#0
	sbc		fr0,x
	sta		fr0,x
	dex
	bne		diff_borrow
	lda		#$80
	eor		fr0
	sta		fr0
	rts
.endp

;--------------------------------------------------------------------------
.proc fp_fmul_innerloop
_offset = _fr3+5
_offset2 = fr2
	sta		fr0
	inc		fr0

	;clear accumulator through to exponent byte of fr1
	ldx		#fr0+1
	ldy		#12
	jsr		zfl

	;set up for 7 bits per digit pair (0-99 in 0-127)
	ldx		#7
	stx		_offset
	sed

	;set rounding byte, assuming renormalize needed (fr0+2 through fr0+6)
	lda		#$50
	sta		fr0+7

	;begin outer loop -- this is where we process one _bit_ out of each
	;multiplier byte in FR2's mantissa (note that this is inverted in that
	;it is bytes-in-bits instead of bits-in-bytes)
offloop:

	;begin inner loop -- here we process the same bit in each multiplier
	;byte, going from byte 5 down to byte 1
	ldx		#fr0+5
offloop2:
	;shift a bit out of fr1 mantissa
	lsr		fr2-fr0,x
	lda		fr2-fr0,x
	and		#8
	beq		no_half_carry
	ldy		fr2-fr0,x
	dey
	dey
	dey
	sty		fr2-fr0,x
no_half_carry:
	bcc		noadd
			
	;add fr1 to fr0 at offset
	stx		_offset2
	
	ldy		#fr1
	jsr		fp_fastadd6
	
	;check if we have a carry out to the upper bytes
	bcc		no_carry
carryloop:
	dex
	lda		0,x
	adc		#0
	sta		0,x
	bcs		carryloop
no_carry:

	;restore byte offset
	ldx		_offset2
	
noadd:
	;go back for next byte
	dex
	cpx		#fr0
	bne		offloop2

	;double fr1
	ldx		#fr1
	ldy		#fr1
	clc
	jsr		fp_fastadd6

	;loop back until all mantissa bytes finished
	dec		_offset
	bne		offloop
	
	;check if no renormalize is needed, and if so, re-add new rounding
	lda		fr0+1
	beq		renorm_needed

	lda		#$50
	ldx		#6
round_loop:
	adc		fr0,x
	sta		fr0,x
	dex
	lda		#0
	bcs		round_loop

renorm_needed:
	;all done
	cld
	rts
.endp

;==========================================================================
fp_fastadc5 = fp_fastadd6.adc5_entry
fp_fastadc3 = fp_fastadd6.adc3_entry
.proc fp_fastadd6
	clc
	lda		5,x
	adc		5,y
	sta		5,x
adc5_entry:
	lda		4,x
	adc		4,y
	sta		4,x
	lda		3,x
	adc		3,y
	sta		3,x
adc3_entry:
	lda		2,x
	adc		2,y
	sta		2,x
	lda		1,x
	adc		1,y
	sta		1,x
	lda		0,x
	adc		0,y
	sta		0,x
	rts
.endp

;==========================================================================
fp_fastsbc5 = fp_fastsub6.sbc5_entry
.proc fp_fastsub6
	sec
	lda		5,x
	sbc		5,y
	sta		5,x
sbc5_entry:
	lda		4,x
	sbc		4,y
	sta		4,x
	lda		3,x
	sbc		3,y
	sta		3,x
	lda		2,x
	sbc		2,y
	sta		2,x
	lda		1,x
	sbc		1,y
	sta		1,x
	lda		0,x
	sbc		0,y
	sta		0,x
	rts
.endp

.print	'Pre-PLYEVL Current address: ',*,' -> $DD40 (', $dd40-* ,' bytes free)'

;==========================================================================
; PLYEVL [DD40]	Eval polynomial at (X:Y) with A coefficients using FR0
;
		ckaddr	$dd40
.proc plyevl
	;stash arguments
	stx		fptr2
	sty		fptr2+1
	sta		_fpcocnt
	
	;copy FR0 -> PLYARG
	ldx		#<plyarg
	ldy		#>plyarg
	jsr		fst0r
	
	jsr		zfr0
	
	;enter while loop
	jmp		loop0
	
loop:
	;copy PLYARG -> FR1
	ldx		#<plyarg
	ldy		#>plyarg
	jsr		fld1r
	
	;multiply accumulator by Z
	jsr		fmul
	bcs		xit

loop0:	
	;load next coefficient and increment coptr
	lda		fptr2
	tax
	clc
	adc		#6
	sta		fptr2
	ldy		fptr2+1
	scc:inc	fptr2+1
	jsr		fld1r

	;add coefficient to acc
	jsr		fadd
	bcs		xit

	dec		_fpcocnt
	bne		loop
xit:
	rts
	
.print	'PLYEVL Current address: ',*,' -> $DD89 (', $dd89-* ,' bytes free)'
.endp

;==========================================================================
; FLD0R [DD89]	Load FR0 from (X:Y)
; FLD0P [DD8D]	Load FR0 from (FLPTR)
;
	org		$dd87
fld0r_zp:
	ldy		#0
fld0r:
	stx		flptr
	sty		flptr+1
	ckaddr	$dd8d
fld0p:
	ldy		#5
fld0ploop:
	lda		(flptr),y
	sta		fr0,y
	dey
	bpl		fld0ploop
	rts

;==========================================================================
; FLD1R [DD98]	Load FR1 from (X:Y)
; FLD1P [DD9C]	Load FR1 from (FLPTR)
;
	org		$dd98
fld1r:
	stx		flptr
	sty		flptr+1
	ckaddr	$dd9c
fld1p:
	ldy		#5
fld1ploop:
	lda		(flptr),y
	sta		fr1,y
	dey
	bpl		fld1ploop
	rts

;==========================================================================
; FST0R [DDA7]	Store FR0 to (X:Y)
; FST0P [DDAB]	Store FR0 to (FLPTR)
;
	org		$dda7
fst0r:
	stx		flptr
	sty		flptr+1
	ckaddr	$ddab
fst0p:
	ldy		#5
fst0ploop:
	lda		fr0,y
	sta		(flptr),y
	dey
	bpl		fst0ploop
	rts

;==========================================================================
; FMOVE [DDB6]	Move FR0 to FR1
;
	org		$ddb6
fmove:
	ldx		#5
fmoveloop:
	lda		fr0,x
	sta		fr1,x
	dex
	bpl		fmoveloop
	rts

;==========================================================================
; EXP [DDC0]	Compute e^x
; EXP10 [DDCC]	Compute 10^x
;
	org		$ddc0
exp10 = exp._exp10
.proc exp
	ldx		#<log10_e
	ldy		#>log10_e
	jsr		fld1r
	jsr		fmul
	bcs		err2

	ckaddr	$ddcc
_exp10:
	;stash sign and compute abs
	lda		fr0
	and		#$80
	sta		_fptemp1
	eor		fr0
	sta		fr0

	ldy		#0
	
	;check for |exp| >= 100 which would guarantee over/underflow
	sec
	sbc		#$40
	bcc		abs_ok
	bne		abs_too_big
	
	;|exp|>=1, so split it into integer/fraction
	lda		fr0+1
	lsr
	lsr
	lsr
	lsr
	tax
	lda		fr0+1
	and		#$0f
	ora		fp_mul10,x
	tay

	dec		fr0
	ldx		#<-4
frac_loop:
	lda		fr0+6,x
	sta		fr0+5,x
	inx
	bne		frac_loop
	stx		fr0+5
	beq		abs_ok
	
abs_too_big:
	;okay, the |x| is too big... check if the original was negative.
	;if so, zero and exit, otherwise error.
	lda		_fptemp1
	beq		err2
	clc
	jmp		zfr0
		
abs_ok:
	;stash integer portion of exponent
	sty		_fptemp0
		
	;compute approximation z = 10^y
	ldx		#<coeff
	ldy		#>coeff
	lda		#10
	jsr		plyevl
	
	;tweak exponent
	lsr		_fptemp0
	
	;scale by 10 if necessary
	bcc		even
	ldx		#<ten
	ldy		#>ten
	jsr		fld1r
	jsr		fmul
	bcs		abs_too_big
even:

	;bias exponent
	lda		_fptemp0
	adc		fr0
	cmp		#64+49
	bcs		err2
	sta		fr0
	
	;check if we should invert
	rol		_fptemp1
	bcc		xit2
	
	jsr		fmove
	ldx		#<fp_one
	ldy		#>fp_one
	jsr		fld0r
	jmp		fdiv

err:
	sec
err2:
xit2:
	rts
	
log10_e .fl 0.43429448190325182765112891891661
ten:
	.fl		10

coeff:		;Maclaurin series for 10^x, 10 coefficients
	.fl		0.016339722042
	.fl		-0.009236016545
	.fl		0.1050976426
	.fl		0.1793116355
	.fl		0.5519673808
	.fl		1.16781028
	.fl		2.03521631
	.fl		2.65090666
	.fl		2.30258638
	.fl		1
             
.print	'EXP10 Current address: ',*,' -> $DE95 (', $DE95-* ,' bytes free)'
.endp	

;==========================================================================
; REDRNG [DE95]	Reduce range via y = (x-C)/(x+C) (undocumented)
;
; X:Y = pointer to C argument
;
	org		$de95
redrng = _redrng
.proc _redrng
	stx		fptr2
	sty		fptr2+1
	jsr		fld1r
	ldx		#<fpscr
	ldy		#>fpscr
	jsr		fst0r
	jsr		fadd
	bcs		fail
	ldx		#<plyarg
	ldy		#>plyarg
	jsr		fst0r
	ldx		#<fpscr
	ldy		#>fpscr
	jsr		fld0r
	ldx		fptr2
	ldy		fptr2+1
	jsr		fld1r
	jsr		fsub
	bcs		fail
	ldx		#<plyarg
	ldy		#>plyarg
	jsr		fld1r
	jsr		fdiv
fail:
	rts
.endp
.print	'RNGRED Current address: ',*,' -> $DECD (', $DECD-* ,' bytes free)'

;==========================================================================
; LOG [DECD]	Compute ln x
; LOG10 [DED1]	Compute log10 x
;
	org		$decd
log10 = log._log10
.proc log
	lsr		_fptemp1
	bpl		entry
	ckaddr	$ded1
_log10:
	sec
	ror		_fptemp1
entry:
	;throw error on negative number
	lda		fr0
	bmi		err		
	
	;stash exponentx2 - 128
	asl
	eor		#$80
	sta		_fptemp0
	
	;raise error if argument is zero
	lda		fr0+1
	beq		err

	;reset exponent so we are in 1 <= z < 100
	ldx		#$40
	stx		fr0
	
	;split into three ranges based on mantissa:
	;  1/sqrt(10) <= x < 1:            [31, 99] divide by 100
	;  sqrt(10)/100 <= x < 1/sqrt(10): [ 3, 30] divide by 10
	;  0 < x < sqrt(10)/100:           [ 1,  2] leave as-is
	
	cmp		#$03
	bcc		post_range_adjust
	cmp		#$31
	bcc		mid_range

	;increase result by 1 (equivalent to *10 input)
	inc		_fptemp0
	bne		adjust_exponent
	
mid_range:
	;multiply by 10
	ldx		#<exp.ten
	ldy		#>exp.ten
	jsr		fld1r
	jsr		fmul
	bcs		err

adjust_exponent:
	;increase result by 1 (equivalent to *10 input)
	inc		_fptemp0
	
	;divide fraction by 100
	dec		fr0
	
post_range_adjust:
	;at this point, we have 1 <= z < 10; apply y = (z-1)/(z+1) transform
	;so we can use a faster converging series... this reduces y to
	;0 <= y < 0.81
	ldx		#<fp_one
	ldy		#>fp_one
	jsr		redrng
	
	;stash y so we can later multiply it back in
	ldx		#<fpscr
	ldy		#>fpscr
	jsr		fst0r
	
	;square the value so we compute a series on y^2n
	jsr		fmove
	jsr		fmul
	
	;do polynomial expansion
	ldx		#<coeff
	ldy		#>coeff
	lda		#10
	jsr		plyevl
	bcs		err2
	
	;multiply back in so we have series on y^(2n+1)
	ldx		#<fpscr
	ldy		#>fpscr
	jsr		fld1r
	jsr		fmul
	
	;stash
	jsr		fmove
	
	;convert exponent adjustment back to float (signed)
	lda		#0
	sta		fr0+1
	ldx		_fptemp0
	bpl		expadj_positive
	sec
	sbc		_fptemp0
	tax
expadj_positive:
	stx		fr0
	jsr		ipf
	
	;merge (cannot fail)
	asl		fr0
	asl		_fptemp0
	ror		fr0
	jsr		fadd
	
	;scale if doing log
	bit		_fptemp1
	bmi		xit2
	
	ldx		#<ln10
	ldy		#>ln10
	jsr		fld1r
	jmp		fmul

err:
	sec
xit2:
err2:
	rts
		
ln10:
	.fl		2.3025850929940456840179914546844
	
;==========================================================================
.print 'pre-HALF Current address: ',*
	org		$df6c
half:
	.fl		0.5
	
coeff:		;Maclaurin series expansion for log10((z-1)/(z+1))
	.fl		0.0457152086	;0.0457152086213949218618
	.fl		0.0510934685	;0.0510934684592060928132
	.fl		0.0579059309	;0.0579059309204335709298
	.fl		0.0668145357	;0.0668145356774233617481
	.fl		0.0789626331	;0.0789626330733185083366
	.fl		0.0965098849	;0.0965098848673892756311
	.fl		0.1240841377	;0.1240841376866433642956
	.fl		0.1737177928	;0.1737177927613007266672
	.fl		0.2895296546	;0.2895296546021678407712
	.fl		0.8685889638	;0.8685889638065035223136
 
.print 'LOG10 Current address: ',*
.endp

;==========================================================================
.print 'Math pack current address: ',*,' (',$dfae-*,' bytes free)'

	org		$dfae
atncoef:	;coefficients for atn(x) ~= f(x^2)
			;see Abramowitz & Stegun 4.4.49
	.fl		0
	.fl		0	
	.fl		0.0028662257
	.fl		-0.0161657367
	.fl		0.0492096138
	.fl		-0.0752896400
	.fl		0.1065626393
	.fl		-0.1420889944
	.fl		0.1999355085
	.fl		-0.3333314528
fp_one:
	.fl		1.0				;also an arctan coeff
	org		$dff0
fp_pi4:	;pi/4 - needed by Atari Basic ATN()
	.fl		0.78539816339744830961566084581988
